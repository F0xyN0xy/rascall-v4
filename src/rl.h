#pragma once
#include "board.h"
#include "search.h"
#include "eval.h"
#include <vector>
#include <string>
#include <functional>

// ── Game outcome ──────────────────────────────────────────────────────────────
enum GameResult { WHITE_WIN = 1, BLACK_WIN = -1, DRAW_RESULT = 0 };

// ── Self-play game record ─────────────────────────────────────────────────────
struct GameRecord {
    std::vector<Move> moves;
    GameResult        result;
};

// ── RL Trainer ────────────────────────────────────────────────────────────────
//
// Uses SPSA (Simultaneous Perturbation Stochastic Approximation) to tune
// evaluation weights. This is the same technique used by Stockfish (Fishtest).
//
// Algorithm:
//   For each iteration:
//     1. Perturb weights +delta and -delta
//     2. Play N self-play games with each set of weights
//     3. Estimate gradient from win-rate difference
//     4. Update weights in gradient direction
//
class RLTrainer {
public:
    struct Config {
        int   selfPlayDepth = 4;      // Search depth for self-play games
        int   gamesPerIter  = 20;     // Games to play per SPSA iteration
        int   iterations    = 100;    // Total SPSA iterations
        float a             = 10.0f;  // SPSA step size numerator
        float c             = 50.0f;  // SPSA gradient estimate step
        float A             = 10.0f;  // Stability constant
        float alpha         = 0.602f; // Step decay exponent
        float gamma         = 0.101f; // Gradient decay exponent
        bool  useBookInTraining = false;   // Never use book for RL
        int   randomOpenings    = 50;      // Number of random FENs to use
        bool  asymmetricDepth   = true;
        std::string weightsFile = "weights.bin";
    };

    Config cfg;

    // Run full RL training loop
    void train(int totalIter = -1);

    // Play a single game between two weight configs, return result
    GameResult playGame(EvalWeights& w1, EvalWeights& w2);

    // Evaluate w1 vs w2 over N games, return w1's score (wins + 0.5*draws)
    float evaluate(EvalWeights& w1, EvalWeights& w2, int games);

    // One SPSA iteration
    void spsa_step(int iter);

    // Callback called after each iteration (for logging/UI)
    std::function<void(int iter, float winrate, const EvalWeights&)> onIterCallback;

private:
    // Get weight values as a flat float array for SPSA perturbation
    void   weightsToVec(const EvalWeights& w, std::vector<float>& v);
    void   vecToWeights(const std::vector<float>& v, EvalWeights& w);
    int    weightDim();

    // Detect game end: returns true if game is over
    bool   isGameOver(Board& b, GameResult& result, int moveCount);
};

extern RLTrainer Trainer;
extern std::vector<std::string> OPENING_FENS;