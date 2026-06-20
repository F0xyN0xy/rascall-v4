#pragma once
#include "board.h"
#include <array>
#include <string>

// ── Evaluation weights (tunable via RL / SPSA) ────────────────────────────────
struct EvalWeights {
    // Material values (in centipawns)
    int material[6] = { 100, 320, 330, 500, 900, 20000 };

    // Piece-square tables (bonus for each square, from white's perspective)
    // Rows go rank1..rank8, files a..h
    int pst[6][64] = {
        // Pawn
        {  0,  0,  0,  0,  0,  0,  0,  0,
           5, 10, 10,-20,-20, 10, 10,  5,
           5, -5,-10,  0,  0,-10, -5,  5,
           0,  0,  0, 20, 20,  0,  0,  0,
           5,  5, 10, 25, 25, 10,  5,  5,
          10, 10, 20, 30, 30, 20, 10, 10,
          50, 50, 50, 50, 50, 50, 50, 50,
           0,  0,  0,  0,  0,  0,  0,  0 },
        // Knight
        { -50,-40,-30,-30,-30,-30,-40,-50,
          -40,-20,  0,  5,  5,  0,-20,-40,
          -30,  5, 10, 15, 15, 10,  5,-30,
          -30,  0, 15, 20, 20, 15,  0,-30,
          -30,  5, 15, 20, 20, 15,  5,-30,
          -30,  0, 10, 15, 15, 10,  0,-30,
          -40,-20,  0,  0,  0,  0,-20,-40,
          -50,-40,-30,-30,-30,-30,-40,-50 },
        // Bishop
        { -20,-10,-10,-10,-10,-10,-10,-20,
          -10,  5,  0,  0,  0,  0,  5,-10,
          -10, 10, 10, 10, 10, 10, 10,-10,
          -10,  0, 10, 10, 10, 10,  0,-10,
          -10,  5,  5, 10, 10,  5,  5,-10,
          -10,  0,  5, 10, 10,  5,  0,-10,
          -10,  0,  0,  0,  0,  0,  0,-10,
          -20,-10,-10,-10,-10,-10,-10,-20 },
        // Rook
        {  0,  0,  0,  5,  5,  0,  0,  0,
          -5,  0,  0,  0,  0,  0,  0, -5,
          -5,  0,  0,  0,  0,  0,  0, -5,
          -5,  0,  0,  0,  0,  0,  0, -5,
          -5,  0,  0,  0,  0,  0,  0, -5,
          -5,  0,  0,  0,  0,  0,  0, -5,
           5, 10, 10, 10, 10, 10, 10,  5,
           0,  0,  0,  0,  0,  0,  0,  0 },
        // Queen
        { -20,-10,-10, -5, -5,-10,-10,-20,
          -10,  0,  5,  0,  0,  0,  0,-10,
          -10,  5,  5,  5,  5,  5,  0,-10,
            0,  0,  5,  5,  5,  5,  0, -5,
           -5,  0,  5,  5,  5,  5,  0, -5,
          -10,  0,  5,  5,  5,  5,  0,-10,
          -10,  0,  0,  0,  0,  0,  0,-10,
          -20,-10,-10, -5, -5,-10,-10,-20 },
        // King (middlegame)
        {  20, 30, 10,  0,  0, 10, 30, 20,
           20, 20,  0,  0,  0,  0, 20, 20,
          -10,-20,-20,-20,-20,-20,-20,-10,
          -20,-30,-30,-40,-40,-30,-30,-20,
          -30,-40,-40,-50,-50,-40,-40,-30,
          -30,-40,-40,-50,-50,-40,-40,-30,
          -30,-40,-40,-50,-50,-40,-40,-30,
          -30,-40,-40,-50,-50,-40,-40,-30 }
    };

    // Mobility bonus per move (per piece type)
    int mobilityBonus[6] = { 0, 4, 3, 2, 1, 0 };

    // Pawn structure
    int doubledPawnPenalty  = -10;
    int isolatedPawnPenalty = -20;
    int passedPawnBonus[8]  = { 0, 10, 20, 30, 50, 70, 100, 0 };

    // King safety
    int kingShieldBonus     = 10;

    // Save/load weights for RL persistence
    void save(const std::string& path) const;
    void load(const std::string& path);
};

// Global evaluation weights (modified by RL training)
extern EvalWeights EW;

// ── Evaluation function ────────────────────────────────────────────────────────
// Returns score in centipawns from the perspective of the side to move
int evaluate(const Board& b);

// Helpers
int staticMaterial(const Board& b, Color c);