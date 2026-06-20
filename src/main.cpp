#include "uci.h"
#include "bitboard.h"
#include "tt.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // ── Initialize subsystems ──────────────────────────────────────────────────
    initAttacks();          // Pre-compute knight/king/pawn attack tables
    Zobrist::init();        // Initialize Zobrist hashing keys
    TT.resize(64);          // 64MB transposition table

    // ── Optional: run training directly from command line ─────────────────────
    // Usage: ./chess-engine train [iterations] [depth] [games_per_iter]
    if (argc >= 2 && std::string(argv[1]) == "train") {
        int iters = (argc >= 3) ? std::stoi(argv[2]) : 50;
        int depth = (argc >= 4) ? std::stoi(argv[3]) : 4;
        int games = (argc >= 5) ? std::stoi(argv[4]) : 10;

        Trainer.cfg.iterations    = iters;
        Trainer.cfg.selfPlayDepth = depth;
        Trainer.cfg.gamesPerIter  = games;

        std::cout << "Starting RL training: "
                  << iters << " iterations, depth " << depth
                  << ", " << games << " games/iter\n";
        Trainer.train();
        return 0;
    }

    // ── Default: run UCI loop (for GUI / Cutechess / Arena) ───────────────────
    std::cout << "ChessRL Engine ready. Type 'uci' to begin.\n";
    std::cout.flush();
    uciLoop();
    return 0;
}