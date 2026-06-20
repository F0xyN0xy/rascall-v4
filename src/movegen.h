#pragma once
#include "board.h"

// ── Move list ─────────────────────────────────────────────────────────────────
struct MoveList {
    Move moves[MAX_MOVES];
    int  count = 0;
    void add(Move m) { moves[count++] = m; }
};

// Generate all pseudo-legal moves (caller must verify king not left in check)
void generateMoves(const Board& b, MoveList& ml);

// Generate only captures + promotions (for quiescence search)
void generateCaptures(const Board& b, MoveList& ml);

// Convert move to UCI string (e.g. "e2e4", "e7e8q")
std::string moveToStr(Move m);

// Parse UCI string to Move (needs board for context)
Move strToMove(Board& b, const std::string& s);