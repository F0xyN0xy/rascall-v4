#include "rl.h"
#include <cmath>
#include <iostream>
#include <random>
#include <cstring>

RLTrainer Trainer;

// ── Training opening positions (global, file-scope) ───────────────────────────
static std::vector<std::string> TRAINING_OPENINGS = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkb1r/pppp1ppp/5n2/4p3/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 3",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4",
    "r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 3",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 3",
    "rnbqkbnr/ppp2ppp/8/3pp3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/pppp1ppp/8/4p3/4PP2/8/PPPP2PP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppppp2/6p1/7p/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/4p3/8/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b KQkq - 0 3",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR b KQkq - 0 2",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR w KQkq - 0 3",
    "rnbqkb1r/pppp1ppp/5n2/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR b KQkq - 0 3",
    "rnbqkbnr/ppp2ppp/4p3/3p4/3P4/2N5/PPP2PPP/R1BQKBNR w KQkq - 0 4",
    "rnbqkbnr/pppp1ppp/8/4p3/3P1B2/8/PPP2PPP/RN1QKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/3P4/5N2/PPP2PPP/RNBQKB1R b KQkq - 0 2",
    "rnbqkb1r/pppp1ppp/5n2/4p3/3P4/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 3",
    "rnbqkbnr/pppp1ppp/8/4p3/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/2p5/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/6P1/PP1PPP1P/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/2P5/6P1/PP1PPP1P/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/5NP1/PPPPPP1P/RNBQKB1R b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/3P4/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/2P5/PP1PPPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/1P6/P1PPPPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/P7/1PPPPPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/4P3/PPPP1PPP/RNBQK1NR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/N7/PPPPPPPP/R1BQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/8/7N/PPPPPPPP/RNBQKB1R b KQkq - 0 2",
    "rnbqkb1r/pppp1ppp/5n2/4p3/8/4P3/PPPP1PPP/RNBQK1NR w KQkq - 0 3",
    "rnbqkbnr/ppp2ppp/4p3/3p4/3P4/4P3/PPP3PP/RNBQKBNR b KQkq - 0 3",
    "rnbqkbnr/pp1ppppp/8/2p5/3P4/8/PPP2PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/8/2p5/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/3P1P2/8/PPP3PP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/3P2P1/8/PPP1P1PP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/3P4/P7/1PP1PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/3P4/1P6/P1P1PPPP/RNBQKBNR b KQkq - 0 2",
};

static std::string getRandomOpening() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<> dist(0, TRAINING_OPENINGS.size() - 1);
    return TRAINING_OPENINGS[dist(rng)];
}

// ── Weight serialization ──────────────────────────────────────────────────────
void RLTrainer::weightsToVec(const EvalWeights& w, std::vector<float>& v) {
    v.clear();
    // Material values (not king)
    for (int i = 0; i < 5; i++) v.push_back(float(w.material[i]));
    // PST values (all pieces except king for simplicity)
    for (int pt = 0; pt < 5; pt++)
        for (int sq = 0; sq < 64; sq++)
            v.push_back(float(w.pst[pt][sq]));
    // Pawn structure
    v.push_back(float(w.doubledPawnPenalty));
    v.push_back(float(w.isolatedPawnPenalty));
    for (int i = 0; i < 8; i++) v.push_back(float(w.passedPawnBonus[i]));
    // Mobility
    for (int i = 0; i < 5; i++) v.push_back(float(w.mobilityBonus[i]));
    // King safety
    v.push_back(float(w.kingShieldBonus));
}

void RLTrainer::vecToWeights(const std::vector<float>& v, EvalWeights& w) {
    int idx = 0;
    for (int i = 0; i < 5; i++) w.material[i] = int(v[idx++]);
    for (int pt = 0; pt < 5; pt++)
        for (int sq = 0; sq < 64; sq++)
            w.pst[pt][sq] = int(v[idx++]);
    w.doubledPawnPenalty  = int(v[idx++]);
    w.isolatedPawnPenalty = int(v[idx++]);
    for (int i = 0; i < 8; i++) w.passedPawnBonus[i] = int(v[idx++]);
    for (int i = 0; i < 5; i++) w.mobilityBonus[i]   = int(v[idx++]);
    w.kingShieldBonus = int(v[idx++]);
}

int RLTrainer::weightDim() {
    std::vector<float> v; weightsToVec(EvalWeights{}, v); return int(v.size());
}

// ── Game-over detection ───────────────────────────────────────────────────────
bool RLTrainer::isGameOver(Board& b, GameResult& result, int moveCount) {
    // 50-move rule
    if (b.halfmoveClock >= 100) { result = DRAW_RESULT; return true; }

    // Move limit (avoid infinite games)
    if (moveCount >= 500) { result = DRAW_RESULT; return true; }

    // Check for legal moves
    MoveList ml;
    generateMoves(b, ml);
    bool hasLegal = false;
    for (int i = 0; i < ml.count; i++) {
        b.makeMove(ml.moves[i]);
        bool legal = !b.isAttacked(b.kingSquare(~b.sideToMove), b.sideToMove);
        b.unmakeMove();
        if (legal) { hasLegal = true; break; }
    }

    if (!hasLegal) {
        if (b.inCheck()) {
            // Checkmate: current side to move loses
            result = (b.sideToMove == WHITE) ? BLACK_WIN : WHITE_WIN;
        } else {
            result = DRAW_RESULT;  // Stalemate
        }
        return true;
    }

    // Repetition detection (simple: check hash in history)
    int reps = 0;
    for (int i = (int)b.history.size()-2; i >= 0; i -= 2)
        if (b.history[i].hash == b.hash && ++reps >= 2) {
            result = DRAW_RESULT; return true;
        }

    return false;
}

// ── Play one full game, alternating weights w1 (white) vs w2 (black) ─────────
GameResult RLTrainer::playGame(EvalWeights& w1, EvalWeights& w2) {
    Board b;
    
    // Random opening position
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> dist(0, TRAINING_OPENINGS.size() - 1);
    b.setFEN(TRAINING_OPENINGS[dist(rng)]);
    
    // Play 2-4 random moves from both sides to increase diversity
    // (optional: ensures we don't always start at the same position in the line)
    
    SearchLimits lim;
    lim.useBook = false;  // NEVER use book in training
    
    GameResult result = DRAW_RESULT;
    int moveCount = 0;
    
    while (true) {
        if (isGameOver(b, result, moveCount)) break;
        
        EvalWeights savedEW = EW;
        EW = (b.sideToMove == WHITE) ? w1 : w2;
        
        // Asymmetric depth: one side searches 1 ply deeper
        // This creates imbalance → more wins/losses → stronger gradient
        if (cfg.asymmetricDepth) {
            bool w1IsWhite = (&w1 == &EW);  // crude check, better to pass as param
            // Actually, let's just alternate:
            if (moveCount % 4 < 2) {
                lim.depth = cfg.selfPlayDepth;
            } else {
                lim.depth = cfg.selfPlayDepth + 1;
            }
        } else {
            lim.depth = cfg.selfPlayDepth;
        }
        
        lim.movetime = 0;  // Fixed depth only, no time management
        
        TT.clear();
        SearchResult sr = Engine.search(b, lim);
        EW = savedEW;
        
        if (sr.bestMove == NULL_MOVE) { result = DRAW_RESULT; break; }
        b.makeMove(sr.bestMove);
        moveCount++;
    }
    return result;
}

// ── Evaluate w1 vs w2: returns w1's score (0..games) ─────────────────────────
float RLTrainer::evaluate(EvalWeights& w1, EvalWeights& w2, int games) {
    float score = 0.0f;
    for (int i = 0; i < games; i++) {
        // Alternate colors to reduce bias
        if (i % 2 == 0) {
            GameResult r = playGame(w1, w2);
            if (r == WHITE_WIN) score += 1.0f;
            else if (r == DRAW_RESULT) score += 0.5f;
        } else {
            GameResult r = playGame(w2, w1);
            if (r == BLACK_WIN) score += 1.0f;   // w1 was black and won
            else if (r == DRAW_RESULT) score += 0.5f;
        }
    }
    return score / games;
}

// ── SPSA step ─────────────────────────────────────────────────────────────────
void RLTrainer::spsa_step(int iter) {
    int n = weightDim();
    static std::mt19937 rng(42);
    std::bernoulli_distribution bern(0.5);

    // SPSA schedule
    float ak = cfg.a / std::pow(iter + 1 + cfg.A, cfg.alpha);
    float ck = cfg.c / std::pow(iter + 1, cfg.gamma);

    // Random ±1 perturbation vector (Bernoulli)
    std::vector<float> delta(n);
    for (auto& d : delta) d = bern(rng) ? 1.0f : -1.0f;

    // Current weights as vector
    std::vector<float> w(n);
    weightsToVec(EW, w);

    // w+ and w-
    std::vector<float> wPlus(n), wMinus(n);
    for (int i = 0; i < n; i++) {
        wPlus[i]  = w[i] + ck * delta[i];
        wMinus[i] = w[i] - ck * delta[i];
    }

    EvalWeights ewPlus, ewMinus;
    vecToWeights(wPlus,  ewPlus);
    vecToWeights(wMinus, ewMinus);

    // Estimate gradient
    float yPlus  = evaluate(ewPlus,  ewMinus, cfg.gamesPerIter / 2);
    float yMinus = evaluate(ewMinus, ewPlus,  cfg.gamesPerIter / 2);
    float yDiff  = yPlus - yMinus;  // Positive = w+ is better

    std::cout << "[RL] iter=" << iter+1
              << " ak=" << ak << " ck=" << ck
              << " winrate_diff=" << yDiff << "\n";

    // Update weights: move toward better direction
    for (int i = 0; i < n; i++) {
        float grad = yDiff / (2.0f * ck * delta[i]);
        w[i] += ak * grad;
    }
    vecToWeights(w, EW);

    // Clip material values to sensible ranges
    EW.material[PAWN]   = std::max(50,  std::min(200,  EW.material[PAWN]));
    EW.material[KNIGHT] = std::max(200, std::min(500,  EW.material[KNIGHT]));
    EW.material[BISHOP] = std::max(200, std::min(500,  EW.material[BISHOP]));
    EW.material[ROOK]   = std::max(300, std::min(800,  EW.material[ROOK]));
    EW.material[QUEEN]  = std::max(500, std::min(1500, EW.material[QUEEN]));

    // Save updated weights
    EW.save(cfg.weightsFile);

    if (onIterCallback) onIterCallback(iter, yPlus, EW);
}

// ── Main training loop ────────────────────────────────────────────────────────
void RLTrainer::train(int totalIter) {
    if (totalIter < 0) totalIter = cfg.iterations;

    // Try to load existing weights
    EW.load(cfg.weightsFile);

    std::cout << "=== RL Training: SPSA self-play ===\n";
    std::cout << "Depth: " << cfg.selfPlayDepth
              << "  Games/iter: " << cfg.gamesPerIter
              << "  Iterations: " << totalIter << "\n\n";

    for (int i = 0; i < totalIter; i++) {
        std::cout << "--- Iteration " << i+1 << "/" << totalIter << " ---\n";
        spsa_step(i);
        // Print current material values after each iteration
        std::cout << "[RL] Weights: P=" << EW.material[PAWN]
                  << " N=" << EW.material[KNIGHT]
                  << " B=" << EW.material[BISHOP]
                  << " R=" << EW.material[ROOK]
                  << " Q=" << EW.material[QUEEN] << "\n\n";
    }

    std::cout << "=== Training complete! Weights saved to "
              << cfg.weightsFile << " ===\n";
}