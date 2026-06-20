#pragma once
#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "book.h"
#include <atomic>
#include <chrono>

struct SearchLimits {
    int  depth     = MAX_PLY;
    int  movetime  = 0;       // Hard limit (must stop)
    int  softTime  = 0;       // Soft limit (can stop if eval stable)
    bool infinite  = false;
    bool useBook   = true;
    bool ponder    = false;
};

struct SearchResult {
    Move bestMove  = NULL_MOVE;
    int  score     = 0;
    int  depth     = 0;
    uint64_t nodes = 0;
    int  mateIn    = 0;       // 0 = no mate, +N = mate in N for side to move
    int  timeMs    = 0;
};

class Searcher {
public:
    std::atomic<bool> stop { false };
    SearchResult search(Board& b, const SearchLimits& lim);

    // For pondering: update time limits mid-search
    void setTimeLimits(int softMs, int hardMs);

private:
    uint64_t nodes_ = 0;
    std::chrono::steady_clock::time_point startTime_;
    int       timeLimit_ = 0;    // Hard limit
    int       softLimit_ = 0;    // Soft limit (can stop early)
    mutable bool timeUpCached_ = false; // throttled timeUp() result, refreshed every 2048 nodes
    int       bestScoreRoot_ = 0;
    int       prevBestScore_ = -INF;
    bool      stableEval_ = false;

    Move killers_[MAX_PLY][2] = {};
    int  history_[64][64]     = {};

    int  alphaBeta(Board& b, int alpha, int beta, int depth, int ply, bool nullOk);
    int  quiesce(Board& b, int alpha, int beta);
    void orderMoves(MoveList& ml, Move ttMove, int ply);
    int  mvvLva(const Board& b, Move m) const;
    bool timeUp() const;
    bool shouldStopEarly(float scale = 1.0f) const;  // Soft time check
    int  extension(const Board& b) const { return b.inCheck() ? 1 : 0; }

    // Aspiration window search
    int  aspirationWindow(Board& b, int prevScore, int depth);
    Move pv_[MAX_PLY][MAX_PLY];
    int pvLen_[MAX_PLY];
};

extern Searcher Engine;