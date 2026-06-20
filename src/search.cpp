#include "search.h"
#include <algorithm>
#include <iostream>
#include <cstring>

Searcher Engine;

static const int PIECE_VAL[7] = { 100, 320, 330, 500, 900, 20000, 0 };

// ── Time management ───────────────────────────────────────────────────────────
bool Searcher::timeUp() const {
    if (timeLimit_ <= 0) return false;
    // Only hit the clock every 2048 nodes — steady_clock::now() is cheap but not free,
    // and calling it on literally every node measurably slows down NPS at high depths.
    if ((nodes_ & 2047) != 0) return timeUpCached_;
    auto now = std::chrono::steady_clock::now();
    int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    timeUpCached_ = (elapsed >= timeLimit_);
    return timeUpCached_;
}

bool Searcher::shouldStopEarly(float scale) const {
    if (softLimit_ <= 0) return false;
    auto now = std::chrono::steady_clock::now();
    int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    return elapsed >= int(softLimit_ * scale);
}

void Searcher::setTimeLimits(int softMs, int hardMs) {
    softLimit_ = softMs;
    timeLimit_ = hardMs;
    startTime_ = std::chrono::steady_clock::now();
}

// ── Move ordering ─────────────────────────────────────────────────────────────
static void orderMovesWithBoard(const Board& b, MoveList& ml, Move ttMove, Move killers[2], int history[64][64]) {
    static int scores[MAX_MOVES];
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (m == ttMove) { scores[i] = 2'000'000; continue; }
        if (isCapture(m)) {
            int victim   = PIECE_VAL[typeOf(b.mailbox[toSq(m)])];
            int attacker = PIECE_VAL[typeOf(b.mailbox[fromSq(m)])];
            scores[i] = 1'000'000 + victim * 10 - attacker;
            continue;
        }
        if (m == killers[0]) { scores[i] = 900'000; continue; }
        if (m == killers[1]) { scores[i] = 800'000; continue; }
        scores[i] = history[fromSq(m)][toSq(m)];
    }
    for (int i = 1; i < ml.count; i++) {
        int s = scores[i]; Move mv = ml.moves[i];
        int j = i-1;
        while (j >= 0 && scores[j] < s) { scores[j+1]=scores[j]; ml.moves[j+1]=ml.moves[j]; j--; }
        scores[j+1] = s; ml.moves[j+1] = mv;
    }
}

// ── Quiescence search ─────────────────────────────────────────────────────────
int Searcher::quiesce(Board& b, int alpha, int beta) {
    if (stop) return 0;
    nodes_++;

    int stand_pat = evaluate(b);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    MoveList ml;
    generateCaptures(b, ml);

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        b.makeMove(m);
        if (b.isAttacked(b.kingSquare(~b.sideToMove), b.sideToMove)) {
            b.unmakeMove(); continue;
        }
        int score = -quiesce(b, -beta, -alpha);
        b.unmakeMove();
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// ── Main alpha-beta with mate distance tracking ───────────────────────────────
int Searcher::alphaBeta(Board& b, int alpha, int beta, int depth, int ply, bool nullOk) {
    if (stop || timeUp()) return 0;
    nodes_++;

    pvLen_[ply] = 0;

    // Mate distance pruning
    int mateAlpha = std::max(alpha, -MATE_SCORE + ply);
    int mateBeta  = std::min(beta,  MATE_SCORE - ply - 1);
    if (mateAlpha >= mateBeta) return mateAlpha;

    if (ply > 0) {
        if (b.halfmoveClock >= 100) return 0;
        int reps = 0;
        for (int i = (int)b.history.size()-2; i >= 0; i -= 2) {
            if (b.history[i].hash == b.hash) { reps++; if (reps >= 1) return 0; }
        }
    }

    if (depth <= 0) return quiesce(b, alpha, beta);

    bool inCheck = b.inCheck();
    if (inCheck) depth++;

    Move ttMove = NULL_MOVE;
    int ttMateIn = 0;
    TTEntry* tte = TT.probe(b.hash);
    if (tte) {
        ttMove = tte->move;
        ttMateIn = tte->mateIn;
        if (tte->depth >= depth && ply > 0) {
            int adjustedScore = tte->score;
            if (ttMateIn > 0) {
                adjustedScore = MATE_SCORE - ply - (ttMateIn - 1) * 2;
            } else if (ttMateIn < 0) {
                adjustedScore = -MATE_SCORE + ply + (-ttMateIn - 1) * 2;
            }
            if (tte->flag == TT_EXACT) return adjustedScore;
            if (tte->flag == TT_LOWER && adjustedScore >= beta)  return adjustedScore;
            if (tte->flag == TT_UPPER && adjustedScore <= alpha) return adjustedScore;
        }
    }

    // Null move pruning
    if (nullOk && !inCheck && depth >= 3 && ply > 0) {
        int R = 2 + depth / 4;
        b.makeNullMove();
        int nullScore = -alphaBeta(b, -beta, -beta+1, depth-R-1, ply+1, false);
        b.unmakeNullMove();
        if (nullScore >= beta) return beta;
    }

    MoveList ml;
    generateMoves(b, ml);
    orderMovesWithBoard(b, ml, ttMove, killers_[ply], history_);

    int  bestScore = -INF;
    Move bestMove  = NULL_MOVE;
    int  movesMade = 0;
    TTFlag flag    = TT_UPPER;
    int  bestMateIn = 0;

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        b.makeMove(m);
        if (b.isAttacked(b.kingSquare(~b.sideToMove), b.sideToMove)) {
            b.unmakeMove(); continue;
        }
        movesMade++;

        int score;
        // Late Move Reduction (LMR)
        if (movesMade > 4 && depth >= 3 && !inCheck && !isCapture(m) && !isPromo(m)) {
            int R = 1 + (movesMade > 10 ? 1 : 0) + (depth >= 6 ? 1 : 0);
            score = -alphaBeta(b, -alpha-1, -alpha, depth-R-1, ply+1, true);
            if (score > alpha)
                score = -alphaBeta(b, -beta, -alpha, depth-1, ply+1, true);
        } else if (movesMade > 1) {
            score = -alphaBeta(b, -alpha-1, -alpha, depth-1, ply+1, true);
            if (score > alpha && score < beta)
                score = -alphaBeta(b, -beta, -alpha, depth-1, ply+1, true);
        } else {
            score = -alphaBeta(b, -beta, -alpha, depth-1, ply+1, true);
        }

        b.unmakeMove();

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
            bestMateIn = scoreToMateIn(score, ply + 1);

            // === BUILD PV ===
            pv_[ply][0] = m;
            for (int j = 0; j < pvLen_[ply + 1]; j++) {
                pv_[ply][j + 1] = pv_[ply + 1][j];
            }
            pvLen_[ply] = pvLen_[ply + 1] + 1;
        }
        
        if (score > alpha) {
            alpha = score;
            flag  = TT_EXACT;
            if (!isCapture(m)) {
                history_[fromSq(m)][toSq(m)] += depth * depth;
                if (history_[fromSq(m)][toSq(m)] > 1'000'000)
                    history_[fromSq(m)][toSq(m)] = 1'000'000;
            }
        }
        if (score >= beta) {
            if (!isCapture(m)) {
                killers_[ply][1] = killers_[ply][0];
                killers_[ply][0] = m;
            }
            TT.store(b.hash, bestScore, bestMove, depth, TT_LOWER, bestMateIn);
            return score;
        }
    }

    // Checkmate or stalemate
    if (movesMade == 0) {
        if (inCheck) {
            int mateScore = -MATE_SCORE + ply;
            int mateIn = scoreToMateIn(mateScore, ply);
            TT.store(b.hash, mateScore, NULL_MOVE, depth, TT_EXACT, mateIn);
            return mateScore;
        } else {
            TT.store(b.hash, 0, NULL_MOVE, depth, TT_EXACT, 0);
            return 0;
        }
    }

    TT.store(b.hash, bestScore, bestMove, depth, flag, bestMateIn);
    return bestScore;
}

// ── Aspiration window search ──────────────────────────────────────────────────
int Searcher::aspirationWindow(Board& b, int prevScore, int depth) {
    int delta = 50;
    int alpha = prevScore - delta;
    int beta  = prevScore + delta;

    while (true) {
        int score = alphaBeta(b, alpha, beta, depth, 0, false);
        if (stop || timeUp()) return score;

        if (score <= alpha) {
            beta = (alpha + beta) / 2;
            alpha = score - delta;
            if (alpha < -INF) alpha = -INF;
        } else if (score >= beta) {
            beta = score + delta;
            if (beta > INF) beta = INF;
        } else {
            return score;
        }

        delta += delta / 2;
        if (delta > 500) {
            return alphaBeta(b, -INF, INF, depth, 0, false);
        }
    }
}

// ── Root search with iterative deepening ──────────────────────────────────────
SearchResult Searcher::search(Board& b, const SearchLimits& lim) {
    startTime_ = std::chrono::steady_clock::now();
    timeLimit_ = lim.movetime;
    softLimit_ = lim.softTime;
    nodes_     = 0;
    stop       = false;
    timeUpCached_ = false;
    std::memset(killers_, 0, sizeof(killers_));
    std::memset(history_, 0, sizeof(history_));

    TT.newSearch();

    // ── Opening book probe (instant, no thinking) ────────────────────────────
    if (lim.useBook) {
        Move bookMove = Book.probe(b);
        if (bookMove != NULL_MOVE) {
            SearchResult res;
            res.bestMove = bookMove;
            res.score    = 0;
            res.depth    = 1;
            res.nodes    = 1;
            res.mateIn   = 0;
            auto now = std::chrono::steady_clock::now();
            res.timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
            std::cout << "info depth 1 score cp 0 nodes 1 time " << res.timeMs 
            << " pv " << moveToStr(bookMove) << " bookmove" << std::endl;
            return res;
        }
    }
    // ─────────────────────────────────────────────────────────────────────────

    SearchResult result;
    Move bestMove = NULL_MOVE;
    Move prevBestMove = NULL_MOVE;
    int bestMateIn = 0;
    int prevScore = 0;
    int stableDepths = 0;  // how many consecutive depths the best move hasn't changed

    for (int depth = 1; depth <= lim.depth; depth++) {
        // Soft time check: stop after depth 2 so we always have a real result.
        // Give extra time if the best move keeps changing (unstable position).
        if (depth > 2 && bestMove != NULL_MOVE) {
            float stabilityScale = (stableDepths >= 3) ? 0.75f : 1.0f; // stop sooner if stable
            if (shouldStopEarly(stabilityScale)) break;
        }

        int score;
        if (depth >= 4 && !isMateScore(prevScore)) {
            score = aspirationWindow(b, prevScore, depth);
        } else {
            score = alphaBeta(b, -INF, INF, depth, 0, false);
        }

        if (stop || timeUp()) {
            if (bestMove != NULL_MOVE) break;
        }

        prevScore = score;

        TTEntry* tte = TT.probe(b.hash);
        if (tte && tte->move != NULL_MOVE) {
            bestMove = tte->move;
            bestMateIn = tte->mateIn;
        }

        // Track stability: count consecutive depths where best move didn't change
        if (bestMove == prevBestMove && bestMove != NULL_MOVE)
            stableDepths++;
        else
            stableDepths = 0;
        prevBestMove = bestMove;

        result.bestMove = bestMove;
        result.score    = score;
        result.depth    = depth;
        result.nodes    = nodes_;
        result.mateIn   = bestMateIn;

        auto now = std::chrono::steady_clock::now();
        int ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
        int nps  = (ms > 0) ? (int)(nodes_ / ms * 1000) : 0;
        result.timeMs = ms;

        // Output info with proper newlines
        std::cout << "info depth " << depth;

        if (bestMateIn != 0) {
            std::cout << " score mate " << bestMateIn;
        } else {
            std::cout << " score cp " << score;
        }

        std::cout << " nodes " << nodes_
                  << " nps " << nps
                  << " time " << ms
                  << " hashfull " << TT.hashfull();

        // Build PV from TT
        std::cout << " pv";
        for (int i = 0; i < pvLen_[0]; i++) {
            std::cout << " " << moveToStr(pv_[0][i]);
        }
        std::cout << std::endl;
        Board pvBoard = b;
        Move pvMove = bestMove;
        int pvLen = 0;
        U64 pvHashes[32] = {};
        while (pvMove != NULL_MOVE && pvLen < 12) {
            bool repeat = false;
            for (int i = 0; i < pvLen; i++) {
                if (pvHashes[i] == pvBoard.hash) { repeat = true; break; }
            }
            if (repeat) break;
            pvHashes[pvLen] = pvBoard.hash;

            std::cout << " " << moveToStr(pvMove);
            pvBoard.makeMove(pvMove);
            TTEntry* pvTte = TT.probe(pvBoard.hash);
            pvMove = (pvTte && pvTte->move != NULL_MOVE) ? pvTte->move : NULL_MOVE;
            pvLen++;
        }
        std::cout << std::endl;

        // If we found a forced mate, stop searching deeper
        if (bestMateIn > 0 && bestMateIn <= (depth + 1) / 2) {
            break;
        }
    }

    result.mateIn = bestMateIn;
    return result;
}