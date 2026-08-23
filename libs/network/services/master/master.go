// network/services/master/master.go
package main

import (
	"bufio"
	"crypto/hmac"
	"crypto/sha256"
	"database/sql"
	"encoding/base64"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	_ "github.com/lib/pq"
)

// ---------- Configuration ----------
const (
	TCP_PORT           = 5555
	UDP_PORT           = 5556
	MATCH_PLAYER_COUNT = 8
	SERVER_TIMEOUT     = 20 * time.Second
	TOKEN_LIFETIME     = 3600
	SHARED_SECRET      = "your-very-long-shared-secret-change-this"
	DB_MAX_CONNS       = 100              // increased from 50
	DB_MAX_IDLE        = 50
	NUM_RESULT_WORKERS = 20               // worker pool size
	RESULT_CHAN_SIZE   = 5000             // buffer size for result channel
)

var db *sql.DB

var (
	servers     = make(map[string]*DedicatedServer)
	serverMu    sync.RWMutex
	queueMu     sync.Mutex
	matches     []*Match
	matchMu     sync.Mutex
	udpPortMap  = make(map[string]int) // remoteAddr -> UDP port

	// ----- Skill buckets (replaces global queue slice) -----
	buckets = make(map[string][]*WaitingPlayer) // key = region:skillBucket
)

// ----- Worker pool channels and sync -----
type matchResultEvent struct {
	matchID    int
	winnerTeam int
	m          *Match
	scores     []int
}
var resultChan = make(chan matchResultEvent, RESULT_CHAN_SIZE)

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

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS accounts (
			id SERIAL PRIMARY KEY,
			username TEXT UNIQUE NOT NULL,
			mu DOUBLE PRECISION DEFAULT 25.0,
			sigma DOUBLE PRECISION DEFAULT 8.333,
			skill_rating INTEGER DEFAULT 1000,
			created_at BIGINT DEFAULT (EXTRACT(EPOCH FROM NOW()))
		);
		CREATE TABLE IF NOT EXISTS replays (
			id SERIAL PRIMARY KEY,
			match_id INTEGER UNIQUE NOT NULL,
			map_name TEXT,
			game_mode TEXT,
			duration INTEGER,
			winner_team INTEGER,
			timestamp BIGINT,
			view_count INTEGER DEFAULT 0,
			download_count INTEGER DEFAULT 0
		);
		CREATE TABLE IF NOT EXISTS replay_players (
			replay_id INTEGER REFERENCES replays(id) ON DELETE CASCADE,
			player_id INTEGER,
			player_name TEXT,
			team INTEGER,
			score INTEGER,
			mmr_at_time INTEGER
		);
		CREATE INDEX IF NOT EXISTS idx_replay_players_name ON replay_players(player_name);
		CREATE INDEX IF NOT EXISTS idx_replays_timestamp ON replays(timestamp DESC);
	`)
	if err != nil {
		return err
	}
	log.Println("Game database initialized (with mu/sigma)")
	return nil
}

// ---------- Account ----------
type Account struct {
	ID       int
	Username string
	Mu       float64
	Sigma    float64
	Skill    int
}

func getAccountByID(id int) (*Account, error) {
	var acc Account
	row := db.QueryRow(
		"SELECT id, username, mu, sigma, skill_rating FROM accounts WHERE id = $1",
		id,
	)
	err := row.Scan(&acc.ID, &acc.Username, &acc.Mu, &acc.Sigma, &acc.Skill)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &acc, nil
}

func createAccountIfMissing(id int, username string) error {
	var exists int
	err := db.QueryRow("SELECT 1 FROM accounts WHERE id = $1", id).Scan(&exists)
	if err == nil {
		return nil
	}
	if err != sql.ErrNoRows {
		return err
	}
	_, err = db.Exec(
		"INSERT INTO accounts (id, username, mu, sigma, skill_rating) VALUES ($1, $2, 25.0, 8.333, 1000)",
		id, username,
	)
	return err
}

func updatePlayerRating(id int, mu, sigma float64) error {
	rating := Rating{Mu: mu, Sigma: sigma}
	mmr := LeaderboardMMR(&rating)
	_, err := db.Exec(
		"UPDATE accounts SET mu = $1, sigma = $2, skill_rating = $3 WHERE id = $4",
		mu, sigma, int(mmr), id,
	)
	return err
}

func saveReplayMetadata(matchID, winnerTeam int, players []*WaitingPlayer, scores []int) error {
	tx, err := db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()

	now := time.Now().Unix()
	_, err = tx.Exec(
		"INSERT INTO replays (match_id, map_name, game_mode, duration, winner_team, timestamp) VALUES ($1, 'Arena', 'Ranked 3v3', 300, $2, $3)",
		matchID, winnerTeam, now,
	)
	if err != nil {
		return err
	}

	for i, p := range players {
		acc, _ := getAccountByID(p.AccountID)
		name := "Unknown"
		skill := 1000
		if acc != nil {
			name = acc.Username
			skill = acc.Skill
		}
		_, err = tx.Exec(
			"INSERT INTO replay_players (replay_id, player_id, player_name, team, score, mmr_at_time) VALUES ((SELECT id FROM replays WHERE match_id = $1), $2, $3, $4, $5, $6)",
			matchID, p.AccountID, name, i/4, scores[i], skill,
		)
		if err != nil {
			return err
		}
	}
	return tx.Commit()
}

// ---------- Token Validation ----------
func validateToken(token string) (int, error) {
	secret := os.Getenv("SHARED_SECRET")
	if secret == "" {
		secret = SHARED_SECRET
	}
	decoded, err := base64.URLEncoding.DecodeString(token)
	if err != nil {
		return 0, fmt.Errorf("invalid encoding")
	}
	parts := strings.SplitN(string(decoded), ":", 3)
	if len(parts) != 3 {
		return 0, fmt.Errorf("invalid format")
	}
	userIDStr, timestampStr, sigHex := parts[0], parts[1], parts[2]

	payload := userIDStr + ":" + timestampStr
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write([]byte(payload))
	expected := mac.Sum(nil)

	if !hmac.Equal([]byte(sigHex), expected) {
		return 0, fmt.Errorf("invalid signature")
	}
	ts, err := strconv.ParseInt(timestampStr, 10, 64)
	if err != nil {
		return 0, fmt.Errorf("invalid timestamp")
	}
	if time.Now().Unix()-ts > TOKEN_LIFETIME {
		return 0, fmt.Errorf("token expired")
	}
	userID, err := strconv.Atoi(userIDStr)
	if err != nil {
		return 0, fmt.Errorf("invalid user ID")
	}
	return userID, nil
}

// ---------- Dedicated Server Pool ----------
type DedicatedServer struct {
	IP             string
	Port           int
	Region         string
	MaxMatches     int
	CurrentMatches int
	Active         bool
	LastHeartbeat  time.Time
}

func updateHeartbeat(ip string, port int, region string, maxMatches int) {
	key := fmt.Sprintf("%s:%d", ip, port)
	serverMu.Lock()
	defer serverMu.Unlock()
	s, ok := servers[key]
	if !ok {
		s = &DedicatedServer{IP: ip, Port: port}
		servers[key] = s
	}
	s.Region = region
	s.MaxMatches = maxMatches
	s.Active = true
	s.LastHeartbeat = time.Now()
}

func expireServers() {
	serverMu.Lock()
	defer serverMu.Unlock()
	for key, s := range servers {
		if time.Since(s.LastHeartbeat) > SERVER_TIMEOUT {
			s.Active = false
			delete(servers, key)
		}
	}
}

func getAvailableServer(region string) *DedicatedServer {
	serverMu.RLock()
	defer serverMu.RUnlock()
	var best *DedicatedServer
	minLoad := 9999
	for _, s := range servers {
		if s.Active && s.Region == region && s.CurrentMatches < s.MaxMatches {
			if s.CurrentMatches < minLoad {
				minLoad = s.CurrentMatches
				best = s
			}
		}
	}
	return best
}

// ---------- Matchmaking (Skill Buckets) ----------
type WaitingPlayer struct {
	AccountID int
	Skill     int
	Region    string
	Conn      net.Conn
	Token     string
	Addr      net.IP
	UDPPort   int
}

type Match struct {
	ID         int
	Players    []*WaitingPlayer
	ServerIP   string
	ServerPort int
}

func addToQueue(p *WaitingPlayer) {
	queueMu.Lock()
	defer queueMu.Unlock()
	// Key = region + ":" + skill/100
	bucketKey := p.Region + ":" + strconv.Itoa(p.Skill/100)
	buckets[bucketKey] = append(buckets[bucketKey], p)
}

func tryFormMatches(udpConn *net.UDPConn) {
	queueMu.Lock()
	defer queueMu.Unlock()

	var matched []*WaitingPlayer
	var remainingBuckets = make(map[string][]*WaitingPlayer)

	for key, players := range buckets {
		if len(players) < MATCH_PLAYER_COUNT {
			remainingBuckets[key] = players
			continue
		}
		// Sort players by skill
		sort.Slice(players, func(i, j int) bool {
			return players[i].Skill < players[j].Skill
		})

		// Extract matches
		unmatched := players
		for len(unmatched) >= MATCH_PLAYER_COUNT {
			group := unmatched[:MATCH_PLAYER_COUNT]
			if group[len(group)-1].Skill-group[0].Skill <= 200 {
				matched = append(matched, group...)
				unmatched = unmatched[MATCH_PLAYER_COUNT:]
			} else {
				// Too wide skill spread: drop the lowest player
				unmatched = unmatched[1:]
			}
		}
		if len(unmatched) > 0 {
			remainingBuckets[key] = unmatched
		}
	}

	buckets = remainingBuckets // update buckets for next tick

	if len(matched) < MATCH_PLAYER_COUNT {
		return
	}

	// Find a server (use the first matched player's region)
	server := getAvailableServer(matched[0].Region)
	if server == nil {
		return
	}

	matchID := int(time.Now().UnixNano() % 1000000)
	m := &Match{
		ID:         matchID,
		Players:    matched,
		ServerIP:   server.IP,
		ServerPort: server.Port,
	}

	matchMu.Lock()
	matches = append(matches, m)
	matchMu.Unlock()

	server.CurrentMatches++

	sendAssignment(udpConn, m)

	for _, p := range matched {
		reply := fmt.Sprintf("MATCH_FOUND %d %s %d\n", matchID, server.IP, server.Port)
		p.Conn.Write([]byte(reply))
		p.Conn.Close()
	}
}

func sendAssignment(conn *net.UDPConn, m *Match) {
	buf := make([]byte, 0, 512)
	buf = append(buf, 'A')
	buf = binary.BigEndian.AppendUint32(buf, uint32(m.ID))
	buf = append(buf, byte(len(m.Players)))

	for _, p := range m.Players {
		buf = binary.BigEndian.AppendUint32(buf, uint32(p.AccountID))
		ip := p.Addr.To4()
		if len(ip) == 4 {
			buf = append(buf, ip...)
		} else {
			buf = append(buf, []byte{0, 0, 0, 0}...)
		}
		buf = binary.BigEndian.AppendUint16(buf, uint16(p.UDPPort))
	}

	addr := net.UDPAddr{IP: net.ParseIP(m.ServerIP), Port: m.ServerPort}
	conn.WriteToUDP(buf, &addr)
	log.Printf("Assigned match %d to %s:%d", m.ID, m.ServerIP, m.ServerPort)
}

// ---------- Worker Pool for Match Results ----------
func processMatchResult(matchID, winnerTeam int, m *Match, scores []int) {
	// ----- RANKSKILL: gather current ratings -----
	ratings := make([]Rating, len(m.Players))
	for i, p := range m.Players {
		acc, err := getAccountByID(p.AccountID)
		if err != nil || acc == nil {
			ratings[i] = Rating{Mu: 25.0, Sigma: 8.333}
			continue
		}
		ratings[i] = Rating{Mu: acc.Mu, Sigma: acc.Sigma}
	}

	// Build team info
	teamCounts := []int{len(m.Players) / 2, len(m.Players) - len(m.Players)/2}
	ranks := make([]int, 2)
	if winnerTeam == 0 {
		ranks[0] = 0
		ranks[1] = 1
	} else {
		ranks[0] = 1
		ranks[1] = 0
	}
	beta := 4.166
	gamma := 0.001

	err := RateMatch(ratings, teamCounts, ranks, beta, gamma)
	if err != nil {
		log.Printf("Rank update failed for match %d: %v", matchID, err)
		// fallback: use simple update? but we'll just log and continue.
	}

	// ----- Batch update in a transaction -----
	tx, err := db.Begin()
	if err != nil {
		log.Printf("Begin tx failed for match %d: %v", matchID, err)
		return
	}
	defer tx.Rollback()

	for i, p := range m.Players {
		if i < len(ratings) {
			rating := ratings[i]
			mmr := int(LeaderboardMMR(&rating))
			_, err = tx.Exec(
				"UPDATE accounts SET mu = $1, sigma = $2, skill_rating = $3 WHERE id = $4",
				rating.Mu, rating.Sigma, mmr, p.AccountID,
			)
			if err != nil {
				log.Printf("Failed to update rating for player %d: %v", p.AccountID, err)
			}
		}
	}

	// Save replay metadata (also inside the transaction)
	if err := saveReplayMetadataTx(tx, matchID, winnerTeam, m.Players, scores); err != nil {
		log.Printf("Failed to save replay metadata: %v", err)
	}

	if err := tx.Commit(); err != nil {
		log.Printf("Commit failed for match %d: %v", matchID, err)
	} else {
		log.Printf("Match %d processed (rankskill updated, batch commit)", matchID)
	}
}

// Helper to save replay metadata within a transaction
func saveReplayMetadataTx(tx *sql.Tx, matchID, winnerTeam int, players []*WaitingPlayer, scores []int) error {
	now := time.Now().Unix()
	_, err := tx.Exec(
		"INSERT INTO replays (match_id, map_name, game_mode, duration, winner_team, timestamp) VALUES ($1, 'Arena', 'Ranked 3v3', 300, $2, $3)",
		matchID, winnerTeam, now,
	)
	if err != nil {
		return err
	}

	for i, p := range players {
		acc, _ := getAccountByID(p.AccountID)
		name := "Unknown"
		skill := 1000
		if acc != nil {
			name = acc.Username
			skill = acc.Skill
		}
		_, err = tx.Exec(
			"INSERT INTO replay_players (replay_id, player_id, player_name, team, score, mmr_at_time) VALUES ((SELECT id FROM replays WHERE match_id = $1), $2, $3, $4, $5, $6)",
			matchID, p.AccountID, name, i/4, scores[i], skill,
		)
		if err != nil {
			return err
		}
	}
	return nil
}

func resultWorker() {
	for event := range resultChan {
		processMatchResult(event.matchID, event.winnerTeam, event.m, event.scores)
		// After processing, we could clean up the match from the global list, but it's already removed by the UDP handler after sending.
	}
}

// ---------- TCP Handler ----------
func handleTCP(conn net.Conn) {
	defer conn.Close()
	reader := bufio.NewReader(conn)
	remoteAddr := conn.RemoteAddr().String()

	for {
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

		switch cmd {
		case "UDP_PORT":
			if len(parts) < 2 {
				conn.Write([]byte("ERR UDP_PORT <port>\n"))
				continue
			}
			port, err := strconv.Atoi(parts[1])
			if err != nil || port < 1024 || port > 65535 {
				conn.Write([]byte("ERR Invalid port\n"))
				continue
			}
			udpPortMap[remoteAddr] = port
			conn.Write([]byte("OK UDP port set\n"))

		case "MATCH":
			if len(parts) < 2 {
				conn.Write([]byte("ERR MATCH session_token [region] [skill]\n"))
				continue
			}
			tok := parts[1]
			accountID, err := validateToken(tok)
			if err != nil {
				conn.Write([]byte("ERR Invalid or expired token\n"))
				continue
			}
			acc, err := getAccountByID(accountID)
			if err != nil {
				conn.Write([]byte("ERR Database error\n"))
				continue
			}
			if acc == nil {
				if err := createAccountIfMissing(accountID, "unknown"); err != nil {
					conn.Write([]byte("ERR Could not create account\n"))
					continue
				}
				acc, _ = getAccountByID(accountID)
				if acc == nil {
					conn.Write([]byte("ERR Account not found\n"))
					continue
				}
			}
			region := "US-EAST"
			if len(parts) >= 3 && parts[2] != "" {
				region = parts[2]
			}
			skill := acc.Skill
			if len(parts) >= 4 {
				if s, err := strconv.Atoi(parts[3]); err == nil {
					skill = s
				}
			}
			udpPort := udpPortMap[remoteAddr]
			if udpPort == 0 {
				conn.Write([]byte("ERR UDP_PORT not set\n"))
				continue
			}

			p := &WaitingPlayer{
				AccountID: accountID,
				Skill:     skill,
				Region:    region,
				Conn:      conn,
				Token:     tok,
				Addr:      conn.RemoteAddr().(*net.TCPAddr).IP,
				UDPPort:   udpPort,
			}
			addToQueue(p)
			conn.Write([]byte("QUEUED\n"))

		case "LOGOUT":
			conn.Write([]byte("OK\n"))
			return

		default:
			conn.Write([]byte("ERR Unknown command. Use: UDP_PORT, MATCH, LOGOUT\n"))
		}
	}
}

// ---------- UDP Handler ----------
func handleUDP(conn *net.UDPConn) {
	buf := make([]byte, 1024)
	for {
		n, addr, err := conn.ReadFromUDP(buf)
		if err != nil {
			log.Printf("UDP read error: %v", err)
			continue
		}
		if n < 1 {
			continue
		}
		data := buf[:n]

		switch data[0] {
		case 'H': // Heartbeat
			if n < 4 {
				continue
			}
			region := string(data[1:])
			nullPos := 0
			for i, b := range data[1:] {
				if b == 0 {
					nullPos = i + 1
					break
				}
			}
			if nullPos == 0 || nullPos+3 > n {
				continue
			}
			maxMatches := int(data[nullPos])
			port := int(binary.BigEndian.Uint16(data[nullPos+1 : nullPos+3]))
			updateHeartbeat(addr.IP.String(), port, region, maxMatches)

		case 'R': // Match result - push to worker pool
			if n < 6 {
				continue
			}
			matchID := int(binary.BigEndian.Uint32(data[1:5]))
			winnerTeam := int(data[5])

			matchMu.Lock()
			var m *Match
			for _, m2 := range matches {
				if m2.ID == matchID {
					m = m2
					break
				}
			}
			matchMu.Unlock()

			if m == nil {
				log.Printf("Unknown match %d", matchID)
				continue
			}

			// Parse scores (offset 6)
			offset := 6
			scores := make([]int, 0, len(m.Players))
			for i := 0; i < len(m.Players) && offset+8 <= n; i++ {
				score := int(binary.BigEndian.Uint32(data[offset+4 : offset+8]))
				offset += 8
				scores = append(scores, score)
			}

			// Decrement server load (we'll do this immediately)
			serverMu.Lock()
			if s, ok := servers[fmt.Sprintf("%s:%d", m.ServerIP, m.ServerPort)]; ok && s.CurrentMatches > 0 {
				s.CurrentMatches--
			}
			serverMu.Unlock()

			// Remove match from list (it will be processed by worker)
			matchMu.Lock()
			newMatches := []*Match{}
			for _, m2 := range matches {
				if m2.ID != matchID {
					newMatches = append(newMatches, m2)
				}
			}
			matches = newMatches
			matchMu.Unlock()

			// Send to worker pool
			select {
			case resultChan <- matchResultEvent{matchID, winnerTeam, m, scores}:
				// enqueued
			default:
				log.Printf("WARN: result channel full, dropping match %d", matchID)
			}
		}
	}
}

// ---------- Main ----------
func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)
	log.Println("Master Server starting (optimized)...")

	// Initialize rankskill (loads c_rank_data or generates it)
	RankSkillInit()
	log.Println("Rankskill system initialized")

	if err := initDB(); err != nil {
		log.Fatalf("DB init: %v", err)
	}

	// Start worker pool for match results
	for i := 0; i < NUM_RESULT_WORKERS; i++ {
		go resultWorker()
	}
	log.Printf("Started %d result workers", NUM_RESULT_WORKERS)

	// TCP
	tcpListener, err := net.Listen("tcp", fmt.Sprintf(":%d", TCP_PORT))
	if err != nil {
		log.Fatalf("TCP listen: %v", err)
	}
	defer tcpListener.Close()
	log.Printf("TCP listening on %d", TCP_PORT)

	// UDP
	udpAddr, _ := net.ResolveUDPAddr("udp", fmt.Sprintf(":%d", UDP_PORT))
	udpConn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		log.Fatalf("UDP listen: %v", err)
	}
	defer udpConn.Close()
	log.Printf("UDP listening on %d", UDP_PORT)

	// Matchmaking ticker (100ms)
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for range ticker.C {
			tryFormMatches(udpConn)
		}
	}()

	// Housekeeping (1 minute)
	go func() {
		ticker := time.NewTicker(1 * time.Minute)
		defer ticker.Stop()
		for range ticker.C {
			expireServers()
		}
	}()

	for {
		conn, err := tcpListener.Accept()
		if err != nil {
			log.Printf("Accept error: %v", err)
			continue
		}
		go handleTCP(conn)
	}
}