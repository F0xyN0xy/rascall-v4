#include "search.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdlib>

Searcher Engine;

static const int PIECE_VAL[7] = { 100, 320, 330, 500, 900, 20000, 0 };

// ── Futility pruning margins ─────────────────────────────────────────────────
// If static eval + margin < alpha, we can skip quiet moves at this depth
static const int FUTILITY_MARGIN[4] = { 0, 150, 300, 500 };

// ── Reverse futility pruning margins ─────────────────────────────────────────
// If static eval - margin >= beta, we can return beta without searching
static const int REVERSE_FUTILITY_MARGIN[4] = { 0, 100, 200, 350 };

// ── Razoring margin ──────────────────────────────────────────────────────────
static const int RAZOR_MARGIN = 400;

// ── Time management ───────────────────────────────────────────────────────────
static int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int Searcher::elapsedMs() const {
    const int64_t start = startTimeNs_.load(std::memory_order_relaxed);
    if (start == 0) return 0;
    return static_cast<int>((steadyNowNs() - start) / 1'000'000);
}

bool Searcher::timeUp() const {
    const int limit = timeLimit_.load(std::memory_order_relaxed);
    return limit > 0 && elapsedMs() >= limit;
}

bool Searcher::shouldStopEarly() const {
    const int limit = softLimit_.load(std::memory_order_relaxed);
    return limit > 0 && elapsedMs() >= limit;
}

void Searcher::setTimeLimits(int softMs, int hardMs) {
    // Called by the UCI thread on ponderhit while the search thread is active.
    // Atomics avoid racing with the search's time checks.
    startTimeNs_.store(steadyNowNs(), std::memory_order_relaxed);
    softLimit_.store(std::max(0, softMs), std::memory_order_relaxed);
    timeLimit_.store(std::max(0, hardMs), std::memory_order_relaxed);
}

// ── Move ordering ────────────────────────────────────────────────────────────
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
int Searcher::quiesce(Board& b, int alpha, int beta, int ply) {
    if (stop) return 0;
    if (timeUp()) {
        stop = true;
        return 0;
    }
    if (ply >= MAX_PLY - 1) return evaluate(b);
    nodes_++;

    const bool inCheck = b.inCheck();
    if (!inCheck) {
        int standPat = evaluate(b);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    }

    // A checked side cannot stand pat: it must search every legal evasion,
    // including quiet king moves and blocks, not just captures.
    MoveList ml;
    if (inCheck) generateMoves(b, ml);
    else generateCaptures(b, ml);

    int legalMoves = 0;
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        b.makeMove(m);
        if (b.isAttacked(b.kingSquare(~b.sideToMove), b.sideToMove)) {
            b.unmakeMove();
            continue;
        }
        legalMoves++;

        int score = -quiesce(b, -beta, -alpha, ply + 1);
        b.unmakeMove();

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    if (inCheck && legalMoves == 0) return -MATE_SCORE + ply;
    return alpha;
}

// ── Main alpha-beta with modern pruning ─────────────────────────────────────
int Searcher::alphaBeta(Board& b, int alpha, int beta, int depth, int ply, bool nullOk) {
    // Aggressive time check
    if ((nodes_ & 1023) == 0 && timeUp()) {
        stop = true;
        return 0;
    }
    if (stop) return 0;
    nodes_++;

    // Start this node's PV as empty. A TT cutoff may leave it empty;
    // a searched alpha-improving move will populate it below.
    if (ply < MAX_PLY) pvLen_[ply] = ply;

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

    if (depth <= 0) return quiesce(b, alpha, beta, ply);

    bool inCheck = b.inCheck();
    if (inCheck) depth++;

    // ── REVERSE FUTILITY PRUNING (static null move) ─────────────────────────
    // If we're way ahead, don't bother searching
    if (!inCheck && ply > 0 && nullOk && depth <= 3) {
        int staticEval = evaluate(b);
        int margin = REVERSE_FUTILITY_MARGIN[std::min(depth, 3)];
        if (staticEval - margin >= beta) {
            return beta;
        }
    }

    // ── RAZORING ─────────────────────────────────────────────────────────────
    // At pre-frontier nodes, if eval + margin < alpha, drop to quiescence
    if (!inCheck && ply > 0 && depth == 3) {
        int staticEval = evaluate(b);
        if (staticEval + RAZOR_MARGIN < alpha) {
            return quiesce(b, alpha, beta, ply);
        }
    }

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

    // ── FUTILITY PRUNING setup ───────────────────────────────────────────────
    bool canFutilityPrune = false;
    int futilityBase = 0;
    if (!inCheck && ply > 0 && depth <= 3) {
        futilityBase = evaluate(b);
        canFutilityPrune = true;
    }

    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        b.makeMove(m);
        if (b.isAttacked(b.kingSquare(~b.sideToMove), b.sideToMove)) {
            b.unmakeMove(); continue;
        }
        movesMade++;

        // ── FUTILITY PRUNING ────────────────────────────────────────────────
        // Skip quiet moves that can't possibly raise alpha
        if (canFutilityPrune && movesMade > 1 && !isCapture(m) && !isPromo(m) && !b.inCheck()) {
            int margin = FUTILITY_MARGIN[std::min(depth, 3)];
            if (futilityBase + margin <= alpha) {
                b.unmakeMove();
                continue;
            }
        }

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
        }
        if (score > alpha) {
            alpha = score;
            flag  = TT_EXACT;

            // Preserve the actual searched principal variation instead of
            // reconstructing it from TT entries that can be overwritten.
            if (ply < MAX_PLY - 1) {
                pv_[ply][ply] = m;
                const int childPvEnd = std::min(pvLen_[ply + 1], MAX_PLY);
                for (int j = ply + 1; j < childPvEnd; ++j)
                    pv_[ply][j] = pv_[ply + 1][j];
                pvLen_[ply] = std::max(ply + 1, childPvEnd);
            } else {
                pv_[ply][ply] = m;
                pvLen_[ply] = ply + 1;
            }
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
    // For ponder searches, startSearch() initializes an unlimited timer before
    // launching this worker. Do not overwrite a ponderhit that may arrive
    // immediately, even before this thread begins executing.
    if (!lim.ponder) {
        startTimeNs_.store(steadyNowNs(), std::memory_order_relaxed);
        timeLimit_.store(lim.movetime, std::memory_order_relaxed);
        softLimit_.store(lim.softTime, std::memory_order_relaxed);
    }
    nodes_     = 0;
    // The caller owns the stop flag so an immediate UCI stop cannot be lost.
    std::memset(killers_, 0, sizeof(killers_));
    std::memset(history_, 0, sizeof(history_));
    std::memset(pv_, 0, sizeof(pv_));
    std::fill(pvLen_, pvLen_ + MAX_PLY, 0);

    TT.newSearch();

    SearchResult result;
    Move bestMove = NULL_MOVE;
    int bestMateIn = 0;
    int prevScore = 0;
    const int baseSoftLimit = lim.softTime;
    const int hardLimit = lim.movetime;
    Move previousBestMove = NULL_MOVE;
    int previousCompletedScore = 0;
    int stableBestMoveIterations = 0;

    for (int depth = 1; depth <= lim.depth; depth++) {
        // Check soft limit BEFORE starting new depth
        if (depth > 2 && shouldStopEarly() && bestMove != NULL_MOVE) {
            if (!isMateScore(prevScore) && !isMateScore(-prevScore)) {
                break;
            }
        }

        // Check hard limit
        if (timeUp()) {
            if (bestMove != NULL_MOVE) break;
        }

        int score;
        if (depth >= 4 && !isMateScore(prevScore)) {
            score = aspirationWindow(b, prevScore, depth);
        } else {
            score = alphaBeta(b, -INF, INF, depth, 0, false);
        }

        // Do not publish a partially completed iteration as the new result.
        // Keep the best move from the last fully completed iteration instead.
        if (stop || timeUp()) break;

        prevScore = score;

        // The root PV is authoritative for the completed iteration. It is
        // built during alpha-beta and does not depend on TT chain stability.
        if (pvLen_[0] > 0 && pv_[0][0] != NULL_MOVE) {
            bestMove = pv_[0][0];
            result.ponderMove = pvLen_[0] > 1 ? pv_[0][1] : NULL_MOVE;
        }
        TTEntry* tte = TT.probe(b.hash);
        if (tte) bestMateIn = tte->mateIn;

        result.bestMove = bestMove;
        result.score    = score;
        result.depth    = depth;
        result.nodes    = nodes_;
        result.mateIn   = bestMateIn;

        auto now = std::chrono::steady_clock::now();
        int ms   = elapsedMs();
        int nps  = (ms > 0) ? (int)(nodes_ / ms * 1000) : 0;
        result.timeMs = ms;

        // Adapt the soft target using completed-iteration stability. A stable
        // root move can finish sooner; score swings or repeated move changes
        // can use more of the reserved hard budget.
        if (baseSoftLimit > 0 && hardLimit > 0) {
            if (bestMove == previousBestMove && bestMove != NULL_MOVE)
                stableBestMoveIterations++;
            else
                stableBestMoveIterations = 0;

            const int scoreSwing = std::abs(score - previousCompletedScore);
            double factor = 1.0;
            factor -= std::min(0.30, stableBestMoveIterations * 0.075);
            if (scoreSwing > 60)
                factor += std::min(0.40, double(scoreSwing - 60) / 250.0);
            factor = std::clamp(factor, 0.70, 1.40);

            const int adjustedSoft = std::min(
                hardLimit, std::max(1, int(baseSoftLimit * factor)));
            softLimit_.store(adjustedSoft, std::memory_order_relaxed);
            previousBestMove = bestMove;
            previousCompletedScore = score;
        }

        // === BUILD INFO LINE AS SINGLE STRING ===
        std::string infoLine = "info depth " + std::to_string(depth);

        if (bestMateIn != 0) {
            infoLine += " score mate " + std::to_string(bestMateIn);
        } else {
            infoLine += " score cp " + std::to_string(score);
        }

        infoLine += " nodes " + std::to_string(nodes_);
        infoLine += " nps " + std::to_string(nps);
        infoLine += " time " + std::to_string(ms);
        infoLine += " hashfull " + std::to_string(TT.hashfull());

        // Emit the PV captured directly from the completed alpha-beta search.
        infoLine += " pv";
        const int rootPvLen = std::min(pvLen_[0], 32);
        for (int i = 0; i < rootPvLen; ++i) {
            if (pv_[0][i] == NULL_MOVE) break;
            infoLine += " " + moveToStr(pv_[0][i]);
        }

        std::cout << infoLine << std::endl;

        // Stop if forced mate found
        if (bestMateIn > 0 && bestMateIn <= (depth + 1) / 2) {
            break;
        }
    }

    result.mateIn = bestMateIn;
    return result;
}