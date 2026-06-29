#pragma once
#include "board.h"
#include <array>
#include <string>

struct EvalWeights {
    int material[6] = { 100, 320, 330, 500, 900, 20000 };

    // Pawn structure
    int doubledPawnPenalty  = -10;
    int isolatedPawnPenalty = -20;
    int passedPawnBonus[8]  = { 0, 10, 20, 30, 50, 70, 100, 0 };

    // Mobility bonus per move
    int mobilityBonus[6] = { 0, 4, 3, 2, 1, 0 };

    // King safety
    int kingShieldBonus     = 10;

    void save(const std::string& path) const;
    void load(const std::string& path);
};

extern EvalWeights EW;

int evaluate(const Board& b);
int staticMaterial(const Board& b, Color c);