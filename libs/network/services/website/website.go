// network/services/website/main.go
package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	_ "github.com/lib/pq"
)

// ---------- Configuration ----------
const (
	DB_MAX_CONNS      = 50
	DB_MAX_IDLE       = 25
	CACHE_DURATION    = 5 * time.Second // how often leaderboard cache refreshes
	RATE_LIMIT_ATTEMPTS = 100           // max requests per minute per IP
	RATE_LIMIT_WINDOW   = 60            // seconds
)

var db *sql.DB

// ---------- Rate Limiter ----------
type rateLimiter struct {
	mu      sync.Mutex
	visits  map[string]*visit // IP -> last attempts
	cleanup time.Time
}

type visit struct {
	count    int
	reset    time.Time
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
		rl.visits[ip] = &visit{count: 1, reset: time.Now().Add(RATE_LIMIT_WINDOW * time.Second)}
		return true
	}
	if time.Now().After(v.reset) {
		v.count = 1
		v.reset = time.Now().Add(RATE_LIMIT_WINDOW * time.Second)
		return true
	}
	if v.count < RATE_LIMIT_ATTEMPTS {
		v.count++
		return true
	}
	return false
}

// ---------- Rate Limiting Middleware ----------
func rateLimitMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		ip, _, err := net.SplitHostPort(r.RemoteAddr)
		if err != nil {
			ip = r.RemoteAddr
		}
		if !limiter.allow(ip) {
			http.Error(w, "Rate limit exceeded. Please wait a minute.", http.StatusTooManyRequests)
			return
		}
		next(w, r)
	}
}

// ---------- Leaderboard Cache ----------
type leaderboardCache struct {
	mu      sync.RWMutex
	data    []Player
	expires time.Time
}

var lbCache = &leaderboardCache{}

// ---------- Database ----------
func initDB() error {
	connStr := os.Getenv("GAME_DATABASE_URL")
	if connStr == "" {
		connStr = "postgres://game:gamepass@localhost:5432/game_db?sslmode=disable"
	}
	var err error
	db, err = sql.Open("postgres", connStr)
	if err != nil {
		return err
	}
	db.SetMaxOpenConns(DB_MAX_CONNS)
	db.SetMaxIdleConns(DB_MAX_IDLE)
	db.SetConnMaxLifetime(5 * time.Minute)

	if err := db.Ping(); err != nil {
		return err
	}
	log.Println("Website DB connected (pool: %d)", DB_MAX_CONNS)
	return nil
}

// ---------- Types ----------
type Player struct {
	ID       int    `json:"id"`
	Username string `json:"username"`
	Skill    int    `json:"skill_rating"`
}

type Match struct {
	ID         int   `json:"match_id"`
	MapName    string `json:"map_name"`
	GameMode   string `json:"game_mode"`
	Duration   int    `json:"duration"`
	WinnerTeam int    `json:"winner_team"`
	Timestamp  int64  `json:"timestamp"`
	ViewCount  int    `json:"view_count"`
}

type MatchPlayer struct {
	PlayerID   int    `json:"player_id"`
	PlayerName string `json:"player_name"`
	Team       int    `json:"team"`
	Score      int    `json:"score"`
	MMRAtTime  int    `json:"mmr_at_time"`
}

// ---------- API Handlers ----------

// handleLeaderboard - uses cache to avoid DB hammering
func handleLeaderboard(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	lbCache.mu.RLock()
	if time.Now().Before(lbCache.expires) && len(lbCache.data) > 0 {
		// Serve from cache
		data := lbCache.data
		lbCache.mu.RUnlock()
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(data)
		return
	}
	lbCache.mu.RUnlock()

	// Cache expired or empty - fetch from DB
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	rows, err := db.QueryContext(ctx,
		"SELECT id, username, skill_rating FROM accounts ORDER BY skill_rating DESC LIMIT 100",
	)
	if err != nil {
		log.Printf("Leaderboard query error: %v", err)
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	players := []Player{}
	for rows.Next() {
		var p Player
		if err := rows.Scan(&p.ID, &p.Username, &p.Skill); err != nil {
			continue
		}
		players = append(players, p)
	}

	// Update cache
	lbCache.mu.Lock()
	lbCache.data = players
	lbCache.expires = time.Now().Add(CACHE_DURATION)
	lbCache.mu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(players)
}

// handlePlayer - single player profile
func handlePlayer(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 4 {
		http.Error(w, "Missing player ID", http.StatusBadRequest)
		return
	}
	id, err := strconv.Atoi(parts[3])
	if err != nil {
		http.Error(w, "Invalid player ID", http.StatusBadRequest)
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	var p Player
	err = db.QueryRowContext(ctx,
		"SELECT id, username, skill_rating FROM accounts WHERE id = $1",
		id,
	).Scan(&p.ID, &p.Username, &p.Skill)
	if err == sql.ErrNoRows {
		http.Error(w, "Player not found", http.StatusNotFound)
		return
	}
	if err != nil {
		log.Printf("Player query error: %v", err)
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(p)
}

// handleMatches - match history
func handleMatches(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	playerID := r.URL.Query().Get("player_id")
	limit := 20
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	var rows *sql.Rows
	var err error

	if playerID != "" {
		pid, _ := strconv.Atoi(playerID)
		rows, err = db.QueryContext(ctx,
			`SELECT r.match_id, r.map_name, r.game_mode, r.duration, r.winner_team, r.timestamp, r.view_count
			 FROM replays r
			 JOIN replay_players rp ON rp.replay_id = r.id
			 WHERE rp.player_id = $1
			 ORDER BY r.timestamp DESC
			 LIMIT $2`,
			pid, limit,
		)
	} else {
		rows, err = db.QueryContext(ctx,
			`SELECT match_id, map_name, game_mode, duration, winner_team, timestamp, view_count
			 FROM replays
			 ORDER BY timestamp DESC
			 LIMIT $1`,
			limit,
		)
	}
	if err != nil {
		log.Printf("Matches query error: %v", err)
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	matches := []Match{}
	for rows.Next() {
		var m Match
		if err := rows.Scan(&m.ID, &m.MapName, &m.GameMode, &m.Duration, &m.WinnerTeam, &m.Timestamp, &m.ViewCount); err != nil {
			continue
		}
		matches = append(matches, m)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(matches)
}

// handleMatchDetail - full match details
func handleMatchDetail(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	parts := strings.Split(r.URL.Path, "/")
	if len(parts) < 4 {
		http.Error(w, "Missing match ID", http.StatusBadRequest)
		return
	}
	matchID, err := strconv.Atoi(parts[3])
	if err != nil {
		http.Error(w, "Invalid match ID", http.StatusBadRequest)
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	var match Match
	err = db.QueryRowContext(ctx,
		`SELECT match_id, map_name, game_mode, duration, winner_team, timestamp, view_count
		 FROM replays WHERE match_id = $1`,
		matchID,
	).Scan(&match.ID, &match.MapName, &match.GameMode, &match.Duration, &match.WinnerTeam, &match.Timestamp, &match.ViewCount)
	if err == sql.ErrNoRows {
		http.Error(w, "Match not found", http.StatusNotFound)
		return
	}
	if err != nil {
		log.Printf("Match detail query error: %v", err)
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}

	rows, err := db.QueryContext(ctx,
		`SELECT player_id, player_name, team, score, mmr_at_time
		 FROM replay_players
		 WHERE replay_id = (SELECT id FROM replays WHERE match_id = $1)
		 ORDER BY team, player_name`,
		matchID,
	)
	if err != nil {
		log.Printf("Match players query error: %v", err)
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	players := []MatchPlayer{}
	for rows.Next() {
		var mp MatchPlayer
		if err := rows.Scan(&mp.PlayerID, &mp.PlayerName, &mp.Team, &mp.Score, &mp.MMRAtTime); err != nil {
			continue
		}
		players = append(players, mp)
	}

	response := struct {
		Match   Match          `json:"match"`
		Players []MatchPlayer `json:"players"`
	}{Match: match, Players: players}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleSearch - search players
func handleSearch(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}
	query := r.URL.Query().Get("q")
	if len(query) < 2 {
		http.Error(w, "Search query must be at least 2 characters", http.StatusBadRequest)
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	rows, err := db.QueryContext(ctx,
		`SELECT id, username, skill_rating FROM accounts WHERE username ILIKE $1 ORDER BY skill_rating DESC LIMIT 20`,
		"%"+query+"%",
	)
	if err != nil {
		log.Printf("Search query error: %v", err)
		http.Error(w, "Database error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	players := []Player{}
	for rows.Next() {
		var p Player
		if err := rows.Scan(&p.ID, &p.Username, &p.Skill); err != nil {
			continue
		}
		players = append(players, p)
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(players)
}

// ---------- Static File Server ----------
func serveStatic(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/" {
		http.ServeFile(w, r, "./static/index.html")
		return
	}
	http.FileServer(http.Dir("./static")).ServeHTTP(w, r)
}

// ---------- Main ----------
func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)
	log.Println("Website Backend starting (optimized)...")

	if err := initDB(); err != nil {
		log.Fatalf("DB init: %v", err)
	}

	// Register routes (with rate limiting applied to heavy endpoints)
	http.HandleFunc("/api/leaderboards", rateLimitMiddleware(handleLeaderboard))
	http.HandleFunc("/api/player/", rateLimitMiddleware(handlePlayer))
	http.HandleFunc("/api/matches", rateLimitMiddleware(handleMatches))
	http.HandleFunc("/api/match/", rateLimitMiddleware(handleMatchDetail))
	http.HandleFunc("/api/search", rateLimitMiddleware(handleSearch))
	http.HandleFunc("/", serveStatic) // static files (no rate limit, but can add if needed)

	port := os.Getenv("WEBSITE_PORT")
	if port == "" {
		port = "8080"
	}

	// Configure HTTP server with timeouts to prevent slow clients
	srv := &http.Server{
		Addr:              ":" + port,
		ReadTimeout:       5 * time.Second,
		ReadHeaderTimeout: 3 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    1 << 16, // 64KB
	}

	log.Printf("Website listening on port %s (rate limit: %d/min/IP)", port, RATE_LIMIT_ATTEMPTS)
	log.Fatal(srv.ListenAndServe())
}