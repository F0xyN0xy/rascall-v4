#include "eval.h"
#include "movegen.h"
#include <fstream>
#include <iostream>

EvalWeights EW;

// ── PST lookup (always from white's perspective, rank flipped for black) ───────
static int pstValue(PieceType pt, Color c, int sq) {
    // Flip square for black (mirror rank)
    int idx = (c == WHITE) ? sq : (sq ^ 56);
    return EW.pst[pt][idx];
}

// ── Material ──────────────────────────────────────────────────────────────────
int staticMaterial(const Board& b, Color c) {
    int val = 0;
    for (int pt = PAWN; pt <= QUEEN; pt++)
        val += popcount(b.pieceBB(c, PieceType(pt))) * EW.material[pt];
    return val;
}

// ── Pawn structure helpers ────────────────────────────────────────────────────
static U64 fileMask(int file) { return 0x0101010101010101ULL << file; }

static int evalPawns(const Board& b, Color c) {
    int score  = 0;
    U64 pawns  = b.pieceBB(c, PAWN);
    U64 ep     = pawns;
    while (ep) {
        int sq  = lsb(ep); ep &= ep-1;
        int f   = fileOf(sq);
        int r   = rankOf(sq);

        // Doubled pawns
        U64 fm = fileMask(f);
        if (popcount(pawns & fm) > 1) score += EW.doubledPawnPenalty / 2;

        // Isolated pawns (no friendly pawns on adjacent files)
        U64 adjFiles = 0;
        if (f > 0) adjFiles |= fileMask(f-1);
        if (f < 7) adjFiles |= fileMask(f+1);
        if (!(pawns & adjFiles)) score += EW.isolatedPawnPenalty;

        // Passed pawns
        U64 ahead = 0;
        if (c == WHITE) {
            for (int rr = r+1; rr <= 7; rr++) {
                if (f > 0) ahead |= bit(makeSquare(f-1, rr));
                ahead |= bit(makeSquare(f, rr));
                if (f < 7) ahead |= bit(makeSquare(f+1, rr));
            }
            if (!(b.pieceBB(BLACK, PAWN) & ahead))
                score += EW.passedPawnBonus[r];
        } else {
            for (int rr = r-1; rr >= 0; rr--) {
                if (f > 0) ahead |= bit(makeSquare(f-1, rr));
                ahead |= bit(makeSquare(f, rr));
                if (f < 7) ahead |= bit(makeSquare(f+1, rr));
            }
            if (!(b.pieceBB(WHITE, PAWN) & ahead))
                score += EW.passedPawnBonus[7-r];
        }
    }
    return score;
}

// ── King safety ───────────────────────────────────────────────────────────────
static int evalKingSafety(const Board& b, Color c) {
    int   sq   = b.kingSquare(c);
    int   r    = rankOf(sq), f = fileOf(sq);
    U64   pawns= b.pieceBB(c, PAWN);
    int   score= 0;
    // Count friendly pawns shielding the king
    for (int df = -1; df <= 1; df++) {
        int ff = f + df;
        if (ff < 0 || ff > 7) continue;
        int shieldRank = (c == WHITE) ? r+1 : r-1;
        if (shieldRank >= 0 && shieldRank <= 7)
            if (pawns & bit(makeSquare(ff, shieldRank)))
                score += EW.kingShieldBonus;
    }
    return score;
}

// ── Mobility ─────────────────────────────────────────────────────────────────
static int evalMobility(const Board& b, Color c) {
    int score = 0;
    U64 all   = b.occ[2];
    U64 notUs = ~b.occ[c];

    U64 kn = b.pieceBB(c, KNIGHT);
    while (kn) { int sq=lsb(kn); kn&=kn-1;
        score += popcount(KNIGHT_ATTACKS[sq] & notUs) * EW.mobilityBonus[KNIGHT]; }

    U64 bi = b.pieceBB(c, BISHOP);
    while (bi) { int sq=lsb(bi); bi&=bi-1;
        score += popcount(bishopAttacks(sq,all) & notUs) * EW.mobilityBonus[BISHOP]; }

    U64 ro = b.pieceBB(c, ROOK);
    while (ro) { int sq=lsb(ro); ro&=ro-1;
        score += popcount(rookAttacks(sq,all) & notUs) * EW.mobilityBonus[ROOK]; }

    U64 qu = b.pieceBB(c, QUEEN);
    while (qu) { int sq=lsb(qu); qu&=qu-1;
        score += popcount(queenAttacks(sq,all) & notUs) * EW.mobilityBonus[QUEEN]; }

    return score;
}

// ── Main evaluate ─────────────────────────────────────────────────────────────
int evaluate(const Board& b) {
    int score = 0;
    Color us = b.sideToMove;

    for (Color c : {WHITE, BLACK}) {
        int sign = (c == us) ? 1 : -1;
        // Material + PST
        for (int pt = PAWN; pt <= KING; pt++) {
            U64 bb = b.pieceBB(c, PieceType(pt));
            int mat = popcount(bb) * EW.material[pt];
            int pst = 0;
            U64 tmp = bb;
            while (tmp) { int sq=lsb(tmp); tmp&=tmp-1; pst += pstValue(PieceType(pt),c,sq); }
            score += sign * (mat + pst);
        }
        score += sign * evalPawns(b, c);
        score += sign * evalMobility(b, c);
        score += sign * evalKingSafety(b, c);
    }
    return score;
}

// ── Weight persistence ────────────────────────────────────────────────────────
void EvalWeights::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(this), sizeof(*this));
}

void EvalWeights::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (f) f.read(reinterpret_cast<char*>(this), sizeof(*this));
}