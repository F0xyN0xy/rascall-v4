#pragma once
#include "types.h"
#include "bitboard.h"
#include "tt.h"
#include <string>
#include <vector>

// ── Undo information (what we need to restore after unmakeMove) ───────────────
struct UndoInfo {
    Move move;
    Piece captured;
    int  castle;
    int  epSq;
    int  halfmove;
    U64  hash;
};

// ── Board ─────────────────────────────────────────────────────────────────────
class Board {
public:
    // Bitboards: one per (color, piece-type)
    U64 pieces[12]    = {};
    U64 occ[3]        = {};   // occ[0]=white, occ[1]=black, occ[2]=all
    Piece mailbox[64] = {};   // fast piece-on-square lookup

    Color sideToMove   = WHITE;
    int   castleRights = 0;
    int   epSq         = NO_SQ;
    int   halfmoveClock= 0;
    int   fullmoveNum  = 1;
    U64   hash         = 0;

    std::vector<UndoInfo> history;

    // ── Setup ──────────────────────────────────────────────────────────────────
    void setStartPos();
    bool setFEN(const std::string& fen);
    std::string getFEN() const;

    // ── Piece access ───────────────────────────────────────────────────────────
    U64  pieceBB(Color c, PieceType pt) const { return pieces[makePiece(c,pt)]; }
    int  kingSquare(Color c) const { return lsb(pieceBB(c, KING)); }

    // ── Make / unmake ──────────────────────────────────────────────────────────
    void makeMove(Move m);
    void unmakeMove();
    void makeNullMove();
    void unmakeNullMove();

    // ── Attack / check helpers ─────────────────────────────────────────────────
    U64  attackersTo(int sq, U64 occupied) const;
    bool inCheck()   const;
    bool isAttacked(int sq, Color byColor) const;

    // ── Perft (for debugging move generation) ─────────────────────────────────
    uint64_t perft(int depth);

    // ── Debug ──────────────────────────────────────────────────────────────────
    void print() const;

private:
    void putPiece(Piece p, int sq);
    void removePiece(int sq);
    void movePiece(int from, int to);
};