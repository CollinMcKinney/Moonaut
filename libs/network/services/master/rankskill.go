// rankskill.go – Weng‑Lin (logistic) rating system
// Place this file next to master.go. It will become part of the `main` package.

package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"sync"
)

// ---------- Constants (internal) ----------

const (
	// K controls in‑game rank volatility. Change per patch.
	K = 15.0

	// NumRanks is the number of ranks in the system (e.g., 50).
	NumRanks = 50

	// MinSigma prevents division by zero and over‑confidence.
	MinSigma = 0.0001

	// DataFileName is the file that stores the conservative thresholds (C_RANK).
	DataFileName = "c_rank_data"

	// Default initial rating values (mu = 25.0, sigma = 25.0/3 ≈ 8.333)
	DefaultMu    = 25.0
	DefaultSigma = 25.0 / 3.0
)

// ---------- Rating type ----------

// Rating represents a player's skill estimate (mu) and uncertainty (sigma).
type Rating struct {
	Mu    float64
	Sigma float64
}

// ---------- Internal package state ----------

var (
	cRank    []float64 // conservative thresholds for each rank
	mmrRank  []float64 // MMR targets for each rank
	initOnce sync.Once
)

// ---------- Initialisation ----------

// RankSkillInit initialises the rank system. It loads C_RANK from file,
// or generates default Gaussian values and writes them out.
// Safe to call multiple times (uses sync.Once).
func RankSkillInit() {
	initOnce.Do(func() {
		cRank = make([]float64, NumRanks)
		mmrRank = make([]float64, NumRanks)

		// Try to load C_RANK from file
		if !loadCRankFromFile(cRank) {
			// Generate default Gaussian
			defaultCRankFill(cRank)
			// Write it to disk so it loads next time
			writeCRankToFile(cRank)
		}

		// Generate MMR_RANK (always the same)
		generateMMRRank(mmrRank)
	})
}

// ensureInit is a helper to guarantee Init() has been called.
func ensureInit() {
	RankSkillInit() // sync.Once ensures it runs only once
}

// ---------- Internal helpers ----------

// defaultCRankFill fills arr with a Gaussian distribution.
func defaultCRankFill(arr []float64) {
	n := len(arr)
	for i := 0; i < n; i++ {
		p := (float64(i) + 0.5) / float64(n)
		var z float64
		if p < 0.5 {
			z = -math.Sqrt(-2.0 * math.Log(1.0-2.0*p))
		} else {
			z = math.Sqrt(-2.0 * math.Log(2.0*(1.0-p)))
		}
		arr[i] = z*15.0 - 2.0
	}
}

// writeCRankToFile writes the C_RANK array to a binary file.
func writeCRankToFile(arr []float64) {
	f, err := os.Create(DataFileName)
	if err != nil {
		return // not critical
	}
	defer f.Close()
	_ = binary.Write(f, binary.LittleEndian, arr)
}

// loadCRankFromFile tries to load C_RANK from file.
// Returns true on success.
func loadCRankFromFile(arr []float64) bool {
	// Try binary first
	f, err := os.Open(DataFileName)
	if err != nil {
		return false
	}
	defer f.Close()

	// Check if file size matches exactly 50*8 bytes
	info, err := f.Stat()
	if err != nil {
		return false
	}
	if info.Size() == int64(len(arr))*8 {
		// Binary read
		err = binary.Read(f, binary.LittleEndian, &arr)
		if err == nil {
			return true
		}
		// If binary failed, fall through to text parsing
	}
	// Seek back to start for text parsing
	f.Seek(0, 0)
	scanner := bufio.NewScanner(f)
	scanner.Split(bufio.ScanWords)
	i := 0
	for scanner.Scan() && i < len(arr) {
		var val float64
		_, err := fmt.Sscan(scanner.Text(), &val)
		if err != nil {
			continue
		}
		arr[i] = val
		i++
	}
	return i == len(arr) && scanner.Err() == nil
}

// generateMMRRank fills arr with MMR targets.
// Anchors: rank 0 → 100, median → 1350, last → 2800.
func generateMMRRank(arr []float64) {
	n := len(arr)
	lastIdx := n - 1
	midIdx := n/2 - 1

	// Segment 1: 100 → 1350
	slope1 := (1350.0 - 100.0) / float64(midIdx)
	for i := 0; i <= midIdx; i++ {
		arr[i] = 100.0 + float64(i)*slope1
	}

	// Segment 2: 1350 → 2800
	slope2 := (2800.0 - 1350.0) / float64(lastIdx-midIdx)
	for i := midIdx + 1; i <= lastIdx; i++ {
		arr[i] = 1350.0 + float64(i-midIdx)*slope2
	}
}

// catmullRom evaluates a Catmull‑Rom spline segment.
func catmullRom(t, p0, p1, p2, p3 float64) float64 {
	t2 := t * t
	t3 := t2 * t
	return 0.5 * (2*p1 +
		(-p0+p2)*t +
		(2*p0-5*p1+4*p2-p3)*t2 +
		(-p0+3*p1-3*p2+p3)*t3)
}

// updatePair computes the delta updates for two players.
func updatePair(muI, sigmaI, muJ, sigmaJ, outcomeI, beta float64) (deltaMuI, deltaMuJ, sigmaFactorI, sigmaFactorJ float64) {
	c := math.Sqrt(2*beta*beta + sigmaI*sigmaI + sigmaJ*sigmaJ)
	p := 1.0 / (1.0 + math.Exp(-(muI-muJ)/c))
	outcomeJ := 1.0 - outcomeI

	factorI := (sigmaI * sigmaI) / c
	factorJ := (sigmaJ * sigmaJ) / c

	deltaMuI = factorI * (outcomeI - p)
	deltaMuJ = factorJ * (outcomeJ - (1.0 - p))

	varianceReductionI := (sigmaI * sigmaI) / (c * c) * p * (1.0 - p)
	varianceReductionJ := (sigmaJ * sigmaJ) / (c * c) * p * (1.0 - p)

	fI := 1.0 - varianceReductionI
	fJ := 1.0 - varianceReductionJ
	if fI < 0.0 {
		fI = 0.0
	}
	if fJ < 0.0 {
		fJ = 0.0
	}
	sigmaFactorI = fI
	sigmaFactorJ = fJ
	return
}

// ---------- Public API ----------

// InitRating initialises a Rating with the given mu and sigma.
// If mu or sigma are 0, default values are used.
func InitRating(mu, sigma float64) Rating {
	if mu == 0.0 {
		mu = DefaultMu
	}
	if sigma == 0.0 {
		sigma = DefaultSigma
	}
	return Rating{Mu: mu, Sigma: sigma}
}

// RateMatch rates a match with one or more teams.
// Parameters:
//
//	ratings - slice of Ratings (length = total players)
//	teamPlayerCounts - slice where team i has this many players
//	teamRanks - slice where 0 = first place, 1 = second, etc.
//	beta - performance variance (recommended: sigma_initial / 2)
//	gamma - dynamics factor (recommended: 0.001)
//
// Returns:
//
//	error - nil on success; otherwise an error describing the issue.
func RateMatch(ratings []Rating, teamPlayerCounts []int, teamRanks []int, beta, gamma float64) error {
	if len(ratings) == 0 {
		return nil // nothing to do
	}
	if len(teamPlayerCounts) == 0 || len(teamRanks) == 0 {
		return ErrInvalidTeams
	}
	if len(teamPlayerCounts) != len(teamRanks) {
		return ErrMismatchedTeams
	}

	totalPlayers := 0
	for _, c := range teamPlayerCounts {
		if c <= 0 {
			return ErrInvalidCount
		}
		totalPlayers += c
	}
	if totalPlayers != len(ratings) {
		return ErrPlayerCountMismatch
	}

	// Copy original mu/sigma and prepare accumulators
	origMu := make([]float64, totalPlayers)
	origSigma := make([]float64, totalPlayers)
	deltaMu := make([]float64, totalPlayers)
	sigmaFactor := make([]float64, totalPlayers)
	teamStart := make([]int, len(teamPlayerCounts))

	teamStart[0] = 0
	for i := 1; i < len(teamPlayerCounts); i++ {
		teamStart[i] = teamStart[i-1] + teamPlayerCounts[i-1]
	}

	for i := 0; i < totalPlayers; i++ {
		origMu[i] = ratings[i].Mu
		origSigma[i] = math.Sqrt(ratings[i].Sigma*ratings[i].Sigma + gamma*gamma)
		deltaMu[i] = 0.0
		sigmaFactor[i] = 1.0
	}

	// Compare each pair of teams
	for t := 0; t < len(teamPlayerCounts); t++ {
		startT := teamStart[t]
		countT := teamPlayerCounts[t]
		rankT := teamRanks[t]

		for u := t + 1; u < len(teamPlayerCounts); u++ {
			startU := teamStart[u]
			countU := teamPlayerCounts[u]
			rankU := teamRanks[u]

			var outcomeT float64
			if rankT < rankU {
				outcomeT = 1.0
			} else if rankT > rankU {
				outcomeT = 0.0
			} else {
				outcomeT = 0.5
			}

			for a := 0; a < countT; a++ {
				idxA := startT + a
				for b := 0; b < countU; b++ {
					idxB := startU + b
					dMuA, dMuB, sfA, sfB := updatePair(
						origMu[idxA], origSigma[idxA],
						origMu[idxB], origSigma[idxB],
						outcomeT, beta,
					)
					deltaMu[idxA] += dMuA
					deltaMu[idxB] += dMuB
					sigmaFactor[idxA] *= sfA
					sigmaFactor[idxB] *= sfB
				}
			}
		}
	}

	// Apply updates
	for i := 0; i < totalPlayers; i++ {
		ratings[i].Mu = origMu[i] + deltaMu[i]
		newSigma := origSigma[i] * math.Sqrt(sigmaFactor[i])
		if newSigma < MinSigma {
			newSigma = MinSigma
		}
		ratings[i].Sigma = newSigma
	}

	return nil
}

// Rate1v1 is a convenience wrapper for a 1‑vs‑1 match.
// winnerIsFirst: true if player 1 beat player 2.
// Returns nil on success, error otherwise.
func Rate1v1(p1, p2 *Rating, winnerIsFirst bool, beta, gamma float64) error {
	if p1 == nil || p2 == nil {
		return ErrNilRating
	}
	ratings := []Rating{*p1, *p2}
	counts := []int{1, 1}
	ranks := []int{0, 1}
	if !winnerIsFirst {
		ranks = []int{1, 0}
	}
	err := RateMatch(ratings, counts, ranks, beta, gamma)
	if err == nil {
		*p1 = ratings[0]
		*p2 = ratings[1]
	}
	return err
}

// DisplayRank returns the in‑game displayed rank (1‑based, clamped).
func DisplayRank(r *Rating) float64 {
	ensureInit()
	if r == nil {
		return 1.0
	}
	raw := r.Mu - K*r.Sigma
	if raw < 1.0 {
		return 1.0
	}
	if raw > float64(NumRanks) {
		return float64(NumRanks)
	}
	return raw
}

// LeaderboardMMR computes the scaled, Elo‑style MMR for leaderboard sorting.
// Higher is better. Uses Catmull‑Rom spline, unbounded at the top.
func LeaderboardMMR(r *Rating) float64 {
	ensureInit()
	if r == nil {
		midIdx := NumRanks/2 - 1
		return mmrRank[midIdx]
	}

	c := r.Mu - K*r.Sigma
	if c < cRank[0] {
		c = cRank[0]
	}

	// Binary search for segment
	lo, hi := 0, NumRanks-1
	idx := 0
	for lo <= hi {
		mid := (lo + hi) / 2
		if c >= cRank[mid] {
			idx = mid
			lo = mid + 1
		} else {
			hi = mid - 1
		}
	}
	if idx >= NumRanks-1 {
		idx = NumRanks - 2
	}

	// Interpolate
	t := (c - cRank[idx]) / (cRank[idx+1] - cRank[idx])
	if t < 0.0 {
		t = 0.0
	}
	if t > 1.0 {
		t = 1.0
	}

	var p0, p1, p2, p3 float64
	if idx == 0 {
		p0 = 2.0*mmrRank[0] - mmrRank[1]
		p1 = mmrRank[0]
		p2 = mmrRank[1]
		p3 = mmrRank[2]
	} else if idx >= NumRanks-2 {
		p0 = mmrRank[NumRanks-3]
		p1 = mmrRank[NumRanks-2]
		p2 = mmrRank[NumRanks-1]
		p3 = 2.0*mmrRank[NumRanks-1] - mmrRank[NumRanks-2]
	} else {
		p0 = mmrRank[idx-1]
		p1 = mmrRank[idx]
		p2 = mmrRank[idx+1]
		p3 = mmrRank[idx+2]
	}

	mmr := catmullRom(t, p0, p1, p2, p3)

	// Unbounded extrapolation beyond top rank
	if c > cRank[NumRanks-1] {
		slope := (mmrRank[NumRanks-1] - mmrRank[NumRanks-2]) /
			(cRank[NumRanks-1] - cRank[NumRanks-2])
		mmr = mmrRank[NumRanks-1] + (c-cRank[NumRanks-1])*slope
	}

	return mmr
}

// ---------- Error definitions ----------

var (
	ErrInvalidTeams        = &rankError{"invalid team parameters"}
	ErrMismatchedTeams     = &rankError{"team count mismatch"}
	ErrInvalidCount        = &rankError{"team has zero players"}
	ErrPlayerCountMismatch = &rankError{"total players do not match ratings length"}
	ErrNilRating           = &rankError{"nil rating pointer"}
)

type rankError struct{ msg string }

func (e *rankError) Error() string { return "rankskill: " + e.msg }
