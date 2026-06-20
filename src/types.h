#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <cassert>
#include <cmath>

using U64 = uint64_t;
using Move = uint32_t;

// ── Constants ────────────────────────────────────────────────────────────────
constexpr int NO_SQ        = 64;
constexpr int INF          = 1'000'000;
constexpr int MATE_SCORE   = 900'000;
constexpr int MATE_DIST_MAX  = 64;       // Max plies to mate we track
constexpr int MAX_PLY      = 64;
constexpr int MAX_MOVES    = 256;
const Move    NULL_MOVE    = 0;

// ── Mate score helpers ─────────────────────────────────────────────────────
inline bool isMateScore(int score) {
    return std::abs(score) >= MATE_SCORE - MATE_DIST_MAX;
}

// Convert internal score to "mate in N" (positive = we win, negative = we lose)
// score = MATE_SCORE - ply_distance, where ply_distance = ply + (mateIn-1)*2
inline int scoreToMateIn(int score, int ply) {
    if (score > 0 && score >= MATE_SCORE - MATE_DIST_MAX) {
        return (MATE_SCORE - score + 1) / 2;
    }
    if (score < 0 && score <= -MATE_SCORE + MATE_DIST_MAX) {
        return -((MATE_SCORE + score) / 2);
    }
    return 0;
}

// Convert "mate in N" to internal score at given ply
inline int mateInToScore(int mateIn, int ply) {
    if (mateIn > 0) return MATE_SCORE - ply - (mateIn - 1) * 2;
    if (mateIn < 0) return -MATE_SCORE + ply + (-mateIn - 1) * 2;
    return 0;
}

// ── Colors ───────────────────────────────────────────────────────────────────
enum Color : int { WHITE = 0, BLACK = 1 };
inline Color operator~(Color c) { return Color(c ^ 1); }

// ── Piece types ──────────────────────────────────────────────────────────────
enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE_TYPE };

// ── Colored pieces  (W_PAWN=0 … W_KING=5, B_PAWN=6 … B_KING=11, EMPTY=12) ─
enum Piece : int {
    W_PAWN=0, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN=6, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    EMPTY=12
};
inline PieceType typeOf(Piece p)                 { return PieceType(p % 6); }
inline Color     colorOf(Piece p)                { return Color(p / 6); }
inline Piece     makePiece(Color c, PieceType t) { return Piece(c * 6 + t); }

// ── Move encoding  from(6)|to(6)|flags(4) ────────────────────────────────────
enum MoveFlag : int {
    QUIET=0, DOUBLE_PUSH=1, KING_CASTLE=2, QUEEN_CASTLE=3,
    CAPTURE=4, EP_CAPTURE=5,
    PROMO_N=8,  PROMO_B=9,  PROMO_R=10, PROMO_Q=11,
    PROMO_CAPTURE_N=12, PROMO_CAPTURE_B=13,
    PROMO_CAPTURE_R=14, PROMO_CAPTURE_Q=15
};
inline Move     makeMove(int from, int to, MoveFlag f = QUIET) {
    return (f << 12) | (to << 6) | from;
}
inline int      fromSq(Move m)   { return  m & 0x3F; }
inline int      toSq(Move m)     { return (m >> 6) & 0x3F; }
inline MoveFlag flagOf(Move m)   { return MoveFlag((m >> 12) & 0xF); }
inline bool     isCapture(Move m){ return flagOf(m) & 4; }
inline bool     isPromo(Move m)  { return flagOf(m) >= PROMO_N; }
inline bool     isEP(Move m)     { return flagOf(m) == EP_CAPTURE; }
inline bool     isCastle(Move m) {
    MoveFlag f = flagOf(m);
    return f == KING_CASTLE || f == QUEEN_CASTLE;
}
inline PieceType promoType(Move m) {
    MoveFlag f = flagOf(m);
    if (f >= PROMO_CAPTURE_N) return PieceType(KNIGHT + (f - PROMO_CAPTURE_N));
    if (f >= PROMO_N)         return PieceType(KNIGHT + (f - PROMO_N));
    return NO_PIECE_TYPE;
}

// ── Named squares ─────────────────────────────────────────────────────────────
enum Square : int {
    A1=0,B1,C1,D1,E1,F1,G1,H1,
    A2,B2,C2,D2,E2,F2,G2,H2,
    A3,B3,C3,D3,E3,F3,G3,H3,
    A4,B4,C4,D4,E4,F4,G4,H4,
    A5,B5,C5,D5,E5,F5,G5,H5,
    A6,B6,C6,D6,E6,F6,G6,H6,
    A7,B7,C7,D7,E7,F7,G7,H7,
    A8,B8,C8,D8,E8,F8,G8,H8
};

// ── Square helpers ────────────────────────────────────────────────────────────
inline int fileOf(int sq)              { return sq & 7; }
inline int rankOf(int sq)              { return sq >> 3; }
inline int makeSquare(int file, int rank){ return rank * 8 + file; }

inline int parseSquare(const std::string& s) {
    if (s.size() < 2) return NO_SQ;
    int f = s[0] - 'a', r = s[1] - '1';
    if (f < 0 || f > 7 || r < 0 || r > 7) return NO_SQ;
    return makeSquare(f, r);
}
inline std::string squareName(int sq) {
    if (sq == NO_SQ) return "-";
    char buf[3] = { char('a' + fileOf(sq)), char('1' + rankOf(sq)), 0 };
    return buf;
}

// ── Castling rights ───────────────────────────────────────────────────────────
constexpr int WHITE_OO  = 1;
constexpr int WHITE_OOO = 2;
constexpr int WHITE_CASTLE = WHITE_OO | WHITE_OOO;
constexpr int BLACK_OO  = 4;
constexpr int BLACK_OOO = 8;
constexpr int BLACK_CASTLE = BLACK_OO | BLACK_OOO;

// ── Piece chars ───────────────────────────────────────────────────────────────
const char PIECE_CH[13] = { 'P','N','B','R','Q','K','p','n','b','r','q','k','.' };