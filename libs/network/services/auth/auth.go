// network/services/auth/main.go
package main

import (
	"bufio"
	"context"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"fmt"
	"log"
	"net"
	"os"
	"strings"
	"sync"
	"time"

	_ "github.com/lib/pq"
	"golang.org/x/crypto/argon2"
)

// ---------- Configuration ----------
const (
	PORT            = 5560
	DB_MAX_CONNS    = 100
	DB_MAX_IDLE     = 50
	TOKEN_LIFETIME  = 3600
	SHARED_SECRET   = "your-very-long-shared-secret-change-this"

	// Rate limiting (per IP)
	RATE_LIMIT_ATTEMPTS = 100 // max attempts per minute
	RATE_LIMIT_WINDOW   = 60  // seconds

	// Argon2id parameters
	ARGON2_TIME    = 2
	ARGON2_MEMORY  = 64 * 1024
	ARGON2_THREADS = 1
	ARGON2_KEYLEN  = 32
	ARGON2_SALTLEN = 16
)

var db *sql.DB

// ---------- Rate Limiter ----------
type rateLimiter struct {
	mu      sync.Mutex
	visits  map[string]*visit // IP -> last attempts
	cleanup time.Time
}

type visit struct {
	count    int       // attempts within the window
	reset    time.Time // when this window resets
}

var limiter = &rateLimiter{
	visits:  make(map[string]*visit),
	cleanup: time.Now().Add(time.Minute),
}

func (rl *rateLimiter) allow(ip string) bool {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	// Cleanup expired entries every minute
	if time.Now().After(rl.cleanup) {
		for k, v := range rl.visits {
			if time.Now().After(v.reset) {
				delete(rl.visits, k)
			}
		}
		rl.cleanup = time.Now().Add(time.Minute)
	}

	v, ok := rl.visits[ip]
	if !ok {
		// First visit: create entry
		rl.visits[ip] = &visit{
			count: 1,
			reset: time.Now().Add(time.Duration(RATE_LIMIT_WINDOW) * time.Second),
		}
		return true
	}

	// Check if reset time has passed
	if time.Now().After(v.reset) {
		v.count = 1
		v.reset = time.Now().Add(time.Duration(RATE_LIMIT_WINDOW) * time.Second)
		return true
	}

	// Within window: check if under limit
	if v.count < RATE_LIMIT_ATTEMPTS {
		v.count++
		return true
	}

	return false // rate limited
}

// ---------- Database ----------
func initDB() error {
	connStr := os.Getenv("AUTH_DATABASE_URL")
	if connStr == "" {
		connStr = "postgres://game:gamepass@localhost:5433/auth_db?sslmode=disable"
	}
	var err error
	db, err = sql.Open("postgres", connStr)
	if err != nil {
		return err
	}
	db.SetMaxOpenConns(DB_MAX_CONNS)
	db.SetMaxIdleConns(DB_MAX_IDLE)
	db.SetConnMaxLifetime(5 * time.Minute)

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS accounts (
			id SERIAL PRIMARY KEY,
			username TEXT UNIQUE NOT NULL,
			password_hash BYTEA NOT NULL,
			salt BYTEA NOT NULL,
			created_at BIGINT DEFAULT (EXTRACT(EPOCH FROM NOW()))
		);
	`)
	if err != nil {
		return err
	}
	log.Println("Auth DB initialized (pool: %d)", DB_MAX_CONNS)
	return nil
}

type Account struct {
	ID   int
	Hash []byte
	Salt []byte
}

func getAccountByUsername(username string) (*Account, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	var acc Account
	row := db.QueryRowContext(ctx,
		"SELECT id, password_hash, salt FROM accounts WHERE username = $1",
		username,
	)
	err := row.Scan(&acc.ID, &acc.Hash, &acc.Salt)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &acc, nil
}

func createAccount(username, password string) (int, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	salt := make([]byte, ARGON2_SALTLEN)
	_, err := rand.Read(salt)
	if err != nil {
		return 0, err
	}
	hash := argon2.IDKey([]byte(password), salt, ARGON2_TIME, ARGON2_MEMORY, ARGON2_THREADS, ARGON2_KEYLEN)

	var id int
	err = db.QueryRowContext(ctx,
		"INSERT INTO accounts (username, password_hash, salt) VALUES ($1, $2, $3) RETURNING id",
		username, hash, salt,
	).Scan(&id)
	if err != nil {
		return 0, err
	}
	return id, nil
}

func generateToken(accountID int) string {
	secret := os.Getenv("SHARED_SECRET")
	if secret == "" {
		secret = SHARED_SECRET
	}
	timestamp := time.Now().Unix()
	payload := fmt.Sprintf("%d:%d", accountID, timestamp)
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write([]byte(payload))
	signature := mac.Sum(nil)
	full := fmt.Sprintf("%s:%x", payload, signature)
	return base64.URLEncoding.EncodeToString([]byte(full))
}

// ---------- TCP Handler ----------
func handleConnection(conn net.Conn) {
	defer conn.Close()
	reader := bufio.NewReader(conn)
	remoteAddr := conn.RemoteAddr().String()
	ip := strings.Split(remoteAddr, ":")[0] // extract IP without port

	for {
		// Read with timeout
		conn.SetReadDeadline(time.Now().Add(30 * time.Second))
		line, err := reader.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		parts := strings.SplitN(line, " ", 3)
		if len(parts) < 1 {
			continue
		}
		cmd := parts[0]

		// Rate limiting: check before processing any command
		if !limiter.allow(ip) {
			log.Printf("Rate limit exceeded for IP %s", ip)
			conn.Write([]byte("ERR Rate limit exceeded. Please wait.\n"))
			// Close connection immediately to free resources
			return
		}

		switch cmd {
		case "REGISTER":
			if len(parts) < 3 {
				conn.Write([]byte("ERR REGISTER username password\n"))
				continue
			}
			username, password := parts[1], parts[2]
			// Basic input validation to prevent SQL injection (though prepared statements handle it)
			if len(username) < 2 || len(username) > 32 {
				conn.Write([]byte("ERR Invalid username length\n"))
				continue
			}
			if len(password) < 4 {
				conn.Write([]byte("ERR Password too short\n"))
				continue
			}

			id, err := createAccount(username, password)
			if err != nil {
				if strings.Contains(err.Error(), "unique constraint") {
					conn.Write([]byte("ERR Username exists\n"))
				} else {
					log.Printf("Register error: %v", err)
					conn.Write([]byte("ERR Internal error\n"))
				}
				continue
			}
			conn.Write([]byte(fmt.Sprintf("OK %d\n", id)))

		case "LOGIN":
			if len(parts) < 3 {
				conn.Write([]byte("ERR LOGIN username password\n"))
				continue
			}
			username, password := parts[1], parts[2]

			acc, err := getAccountByUsername(username)
			if err != nil {
				log.Printf("DB error: %v", err)
				conn.Write([]byte("ERR Database error\n"))
				continue
			}
			if acc == nil {
				// Do NOT reveal if user exists – use generic error
				conn.Write([]byte("ERR Invalid credentials\n"))
				continue
			}
			// Verify password
			hash := argon2.IDKey([]byte(password), acc.Salt, ARGON2_TIME, ARGON2_MEMORY, ARGON2_THREADS, ARGON2_KEYLEN)
			if !hmac.Equal(hash, acc.Hash) {
				conn.Write([]byte("ERR Invalid credentials\n"))
				continue
			}
			token := generateToken(acc.ID)
			conn.Write([]byte(fmt.Sprintf("OK %s\n", token)))

		default:
			conn.Write([]byte("ERR Unknown command. Use: REGISTER, LOGIN\n"))
		}
	}
}

// ---------- Main ----------
func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)
	log.Println("Auth Server starting (optimized)...")

	if err := initDB(); err != nil {
		log.Fatalf("DB init: %v", err)
	}

	// Start background stats logger (optional)
	go func() {
		ticker := time.NewTicker(30 * time.Second)
		defer ticker.Stop()
		for range ticker.C {
			log.Printf("Active connections: (approx) -- rate limit map size: %d", len(limiter.visits))
		}
	}()

	listener, err := net.Listen("tcp", fmt.Sprintf(":%d", PORT))
	if err != nil {
		log.Fatalf("Listen: %v", err)
	}
	defer listener.Close()
	log.Printf("Auth listening on port %d (rate limit: %d/min/IP)", PORT, RATE_LIMIT_ATTEMPTS)

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("Accept error: %v", err)
			continue
		}
		go handleConnection(conn)
	}
}