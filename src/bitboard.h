#pragma once
#include "types.h"

// ── Bit helpers ───────────────────────────────────────────────────────────────
inline int  popcount(U64 b)       { return __builtin_popcountll(b); }
inline int  lsb(U64 b)           { return __builtin_ctzll(b); }
inline int  msb(U64 b)           { return 63 ^ __builtin_clzll(b); }
inline U64  popLSB(U64& b)       { U64 t = b & -b; b &= b-1; return t; }
inline U64  bit(int sq)          { return 1ULL << sq; }

// Named bitboard constants
constexpr U64 FILE_A = 0x0101010101010101ULL;
constexpr U64 FILE_B = FILE_A << 1;
constexpr U64 FILE_G = FILE_A << 6;
constexpr U64 FILE_H = FILE_A << 7;
constexpr U64 RANK_1 = 0xFFULL;
constexpr U64 RANK_2 = RANK_1 << 8;
constexpr U64 RANK_3 = RANK_1 << 16;
constexpr U64 RANK_4 = RANK_1 << 24;
constexpr U64 RANK_5 = RANK_1 << 32;
constexpr U64 RANK_6 = RANK_1 << 40;
constexpr U64 RANK_7 = RANK_1 << 48;
constexpr U64 RANK_8 = RANK_1 << 56;
constexpr U64 ALL    = ~0ULL;

// ── Pre-computed attack tables ────────────────────────────────────────────────
extern U64 KNIGHT_ATTACKS[64];
extern U64 KING_ATTACKS[64];
extern U64 PAWN_ATTACKS[2][64];   // [color][sq]

void initAttacks();                 // call once at startup

// ── Sliding piece attacks (on-the-fly, occupancy aware) ───────────────────────
inline U64 rookAttacks(int sq, U64 occ) {
    U64 attacks = 0;
    int r = rankOf(sq), f = fileOf(sq);
    // North
    for (int rr = r+1; rr <= 7; rr++) {
        U64 b = bit(makeSquare(f, rr));
        attacks |= b;
        if (occ & b) break;
    }
    // South
    for (int rr = r-1; rr >= 0; rr--) {
        U64 b = bit(makeSquare(f, rr));
        attacks |= b;
        if (occ & b) break;
    }
    // East
    for (int ff = f+1; ff <= 7; ff++) {
        U64 b = bit(makeSquare(ff, r));
        attacks |= b;
        if (occ & b) break;
    }
    // West
    for (int ff = f-1; ff >= 0; ff--) {
        U64 b = bit(makeSquare(ff, r));
        attacks |= b;
        if (occ & b) break;
    }
    return attacks;
}

inline U64 bishopAttacks(int sq, U64 occ) {
    U64 attacks = 0;
    int r = rankOf(sq), f = fileOf(sq);
    for (int rr=r+1, ff=f+1; rr<=7 && ff<=7; rr++, ff++) {
        U64 b = bit(makeSquare(ff, rr)); attacks |= b; if (occ & b) break; }
    for (int rr=r+1, ff=f-1; rr<=7 && ff>=0; rr++, ff--) {
        U64 b = bit(makeSquare(ff, rr)); attacks |= b; if (occ & b) break; }
    for (int rr=r-1, ff=f+1; rr>=0 && ff<=7; rr--, ff++) {
        U64 b = bit(makeSquare(ff, rr)); attacks |= b; if (occ & b) break; }
    for (int rr=r-1, ff=f-1; rr>=0 && ff>=0; rr--, ff--) {
        U64 b = bit(makeSquare(ff, rr)); attacks |= b; if (occ & b) break; }
    return attacks;
}

inline U64 queenAttacks(int sq, U64 occ) {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}