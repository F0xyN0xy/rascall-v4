#include "eval.h"
#include "movegen.h"
#include <fstream>
#include <iostream>

EvalWeights EW;

// ── Phase calculation for tapered eval ───────────────────────────────────────
// Phase = 24 at start, 0 in pure endgame. Used to blend mg/eg scores.
static int gamePhase(const Board& b) {
    int phase = 24;
    phase -= popcount(b.pieceBB(WHITE, QUEEN) | b.pieceBB(BLACK, QUEEN)) * 4;
    phase -= popcount(b.pieceBB(WHITE, ROOK)  | b.pieceBB(BLACK, ROOK))  * 2;
    phase -= popcount(b.pieceBB(WHITE, BISHOP)| b.pieceBB(BLACK, BISHOP));
    phase -= popcount(b.pieceBB(WHITE, KNIGHT)| b.pieceBB(BLACK, KNIGHT));
    return std::max(0, phase);
}

// ── Tapered score: blend middlegame and endgame ──────────────────────────────
static int taper(int mg, int eg, int phase) {
    return (mg * phase + eg * (24 - phase)) / 24;
}

// ── Middlegame PST (from white's perspective, rank flipped for black) ─────────
static int mgPstValue(PieceType pt, Color c, int sq) {
    static const int mgPST[6][64] = {
        // Pawn: push to center, control center
        {   0,   0,   0,   0,   0,   0,   0,   0,
          -24, -24, -16, -12, -12, -16, -24, -24,
          -16, -12,  -8,  -4,  -4,  -8, -12, -16,
           -8,  -4,   4,  12,  12,   4,  -4,  -8,
            0,   4,  12,  20,  20,  12,   4,   0,
            8,  12,  20,  28,  28,  20,  12,   8,
           16,  20,  28,  36,  36,  28,  20,  16,
            0,   0,   0,   0,   0,   0,   0,   0 },
        // Knight: central outposts good, edges bad
        { -50, -40, -30, -30, -30, -30, -40, -50,
          -40, -20,   0,   4,   4,   0, -20, -40,
          -30,   4,   8,  12,  12,   8,   4, -30,
          -30,   0,  12,  18,  18,  12,   0, -30,
          -30,   4,  12,  18,  18,  12,   4, -30,
          -30,   0,   8,  12,  12,   8,   0, -30,
          -40, -20,   0,   0,   0,   0, -20, -40,
          -50, -40, -30, -30, -30, -30, -40, -50 },
        // Bishop: long diagonals good, trapped corners bad
        { -20, -10, -10, -10, -10, -10, -10, -20,
          -10,   4,   0,   0,   0,   0,   4, -10,
          -10,   8,   8,   8,   8,   8,   8, -10,
          -10,   0,   8,  12,  12,   8,   0, -10,
          -10,   4,   4,  12,  12,   4,   4, -10,
          -10,   0,   4,   8,   8,   4,   0, -10,
          -10,   0,   0,   0,   0,   0,   0, -10,
          -20, -10, -10, -10, -10, -10, -10, -20 },
        // Rook: open files, 7th rank
        {   0,   0,   0,   4,   4,   0,   0,   0,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
            4,   8,   8,   8,   8,   8,   8,   4,
            0,   0,   0,   0,   0,   0,   0,   0 },
        // Queen: central, but not too early
        { -20, -10, -10,  -4,  -4, -10, -10, -20,
          -10,   0,   4,   0,   0,   4,   0, -10,
          -10,   4,   4,   4,   4,   4,   4, -10,
           -4,   0,   4,   4,   4,   4,   0,  -4,
           -4,   0,   4,   4,   4,   4,   0,  -4,
          -10,   4,   4,   4,   4,   4,   4, -10,
          -10,   0,   4,   0,   0,   4,   0, -10,
          -20, -10, -10,  -4,  -4, -10, -10, -20 },
        // King: castled corner good, center bad in opening
        { -40, -40, -40, -40, -40, -40, -40, -40,
          -40, -40, -40, -40, -40, -40, -40, -40,
          -40, -40, -40, -40, -40, -40, -40, -40,
          -40, -40, -40, -40, -40, -40, -40, -40,
          -32, -32, -32, -32, -32, -32, -32, -32,
          -16, -16, -24, -24, -24, -24, -16, -16,
            0,   8,   4,   0,   0,   4,   8,   0,
            4,  16,   8,   0,   0,   8,  16,   4 }
    };
    int idx = (c == WHITE) ? sq : (sq ^ 56);
    return mgPST[pt][idx];
}

// ── Endgame PST (from white's perspective) ───────────────────────────────────
static int egPstValue(PieceType pt, Color c, int sq) {
    static const int egPST[6][64] = {
        // Pawn: push to promotion
        {   0,   0,   0,   0,   0,   0,   0,   0,
           60,  60,  60,  60,  60,  60,  60,  60,
           48,  48,  48,  48,  48,  48,  48,  48,
           36,  36,  36,  36,  36,  36,  36,  36,
           24,  24,  24,  24,  24,  24,  24,  24,
           12,  12,  12,  12,  12,  12,  12,  12,
            0,   0,   0,   0,   0,   0,   0,   0,
            0,   0,   0,   0,   0,   0,   0,   0 },
        // Knight: still central, but less edge penalty
        { -50, -40, -30, -30, -30, -30, -40, -50,
          -40, -20,   0,   0,   0,   0, -20, -40,
          -30,   0,   4,   8,   8,   4,   0, -30,
          -30,   0,   8,  12,  12,   8,   0, -30,
          -30,   0,   8,  12,  12,   8,   0, -30,
          -30,   0,   4,   8,   8,   4,   0, -30,
          -40, -20,   0,   0,   0,   0, -20, -40,
          -50, -40, -30, -30, -30, -30, -40, -50 },
        // Bishop: mobility matters more
        { -20, -10, -10, -10, -10, -10, -10, -20,
          -10,   0,   0,   0,   0,   0,   0, -10,
          -10,   0,   4,   4,   4,   4,   0, -10,
          -10,   0,   4,   8,   8,   4,   0, -10,
          -10,   0,   4,   8,   8,   4,   0, -10,
          -10,   0,   4,   4,   4,   4,   0, -10,
          -10,   0,   0,   0,   0,   0,   0, -10,
          -20, -10, -10, -10, -10, -10, -10, -20 },
        // Rook: open files, king activity
        {   0,   0,   0,   0,   0,   0,   0,   0,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
           -4,   0,   0,   0,   0,   0,   0,  -4,
            8,  12,  12,  12,  12,  12,  12,   8,
            0,   0,   0,   0,   0,   0,   0,   0 },
        // Queen: central control, but avoid early development
        { -20, -10, -10,  -4,  -4, -10, -10, -20,
          -10,   0,   0,   0,   0,   0,   0, -10,
          -10,   0,   0,   0,   0,   0,   0, -10,
           -4,   0,   0,   4,   4,   0,   0,  -4,
           -4,   0,   0,   4,   4,   0,   0,  -4,
          -10,   0,   0,   0,   0,   0,   0, -10,
          -10,   0,   0,   0,   0,   0,   0, -10,
          -20, -10, -10,  -4,  -4, -10, -10, -20 },
        // King: center good in endgame, support pawns
        { -40, -32, -24, -16, -16, -24, -32, -40,
          -32, -24, -16,  -8,  -8, -16, -24, -32,
          -24, -16,  -8,   0,   0,  -8, -16, -24,
          -16,  -8,   0,   8,   8,   0,  -8, -16,
          -16,  -8,   0,   8,   8,   0,  -8, -16,
          -24, -16,  -8,   0,   0,  -8, -16, -24,
          -32, -24, -16,  -8,  -8, -16, -24, -32,
          -40, -32, -24, -16, -16, -24, -32, -40 }
    };
    int idx = (c == WHITE) ? sq : (sq ^ 56);
    return egPST[pt][idx];
}

// ── Pawn structure ─────────────────────────────────────────────────────────────
static int evalPawns(const Board& b, Color c, int phase) {
    int score = 0;
    U64 pawns = b.pieceBB(c, PAWN);
    U64 allPawns = b.pieceBB(WHITE, PAWN) | b.pieceBB(BLACK, PAWN);
    U64 ep = pawns;

    while (ep) {
        int sq = lsb(ep); ep &= ep - 1;
        int f = fileOf(sq);
        int r = rankOf(sq);

        // Doubled pawns (penalty)
        U64 fm = fileMask(f);
        if (popcount(pawns & fm) > 1) {
            int mgPen = -10;
            int egPen = -20;
            score += taper(mgPen, egPen, phase);
        }

        // Isolated pawns (no friendly pawns on adjacent files)
        U64 adjFiles = 0;
        if (f > 0) adjFiles |= fileMask(f - 1);
        if (f < 7) adjFiles |= fileMask(f + 1);
        if (!(pawns & adjFiles)) {
            int mgIso = -12;
            int egIso = -16;
            score += taper(mgIso, egIso, phase);
        }

        // Passed pawns (no enemy pawns ahead)
        U64 ahead = 0;
        if (c == WHITE) {
            for (int rr = r + 1; rr <= 7; rr++) {
                if (f > 0) ahead |= bit(makeSquare(f - 1, rr));
                ahead |= bit(makeSquare(f, rr));
                if (f < 7) ahead |= bit(makeSquare(f + 1, rr));
            }
            if (!(b.pieceBB(BLACK, PAWN) & ahead)) {
                int mgBonus = 0;
                int egBonus = EW.passedPawnBonus[r];
                score += taper(mgBonus, egBonus, phase);
            }
        } else {
            for (int rr = r - 1; rr >= 0; rr--) {
                if (f > 0) ahead |= bit(makeSquare(f - 1, rr));
                ahead |= bit(makeSquare(f, rr));
                if (f < 7) ahead |= bit(makeSquare(f + 1, rr));
            }
            if (!(b.pieceBB(WHITE, PAWN) & ahead)) {
                int mgBonus = 0;
                int egBonus = EW.passedPawnBonus[7 - r];
                score += taper(mgBonus, egBonus, phase);
            }
        }

        // Backward pawns (can't be supported by friendly pawns)
        if (c == WHITE) {
            if (r > 0 && !(pawns & (fileMask(f) & ~(ALL << (r * 8))))) {
                score += taper(-8, -12, phase);
            }
        } else {
            if (r < 7 && !(pawns & (fileMask(f) & (ALL << ((r + 1) * 8))))) {
                score += taper(-8, -12, phase);
            }
        }
    }

    return score;
}

// ── King safety (tapered: less important in endgame) ────────────────────────
static int evalKingSafety(const Board& b, Color c, int phase) {
    int sq = b.kingSquare(c);
    int r = rankOf(sq), f = fileOf(sq);
    U64 pawns = b.pieceBB(c, PAWN);
    int mgScore = 0, egScore = 0;

    // Pawn shield (pawns in front of king)
    for (int df = -1; df <= 1; df++) {
        int ff = f + df;
        if (ff < 0 || ff > 7) continue;
        for (int dr = 1; dr <= 2; dr++) {
            int rr = (c == WHITE) ? r + dr : r - dr;
            if (rr < 0 || rr > 7) continue;
            if (pawns & bit(makeSquare(ff, rr))) mgScore += 12;
        }
    }

    // Open files against king (penalty)
    for (int df = -1; df <= 1; df++) {
        int ff = f + df;
        if (ff < 0 || ff > 7) continue;
        U64 filePawns = pawns & fileMask(ff);
        if (!filePawns) mgScore -= 20;
    }

    // Enemy pieces attacking king zone
    Color them = ~c;
    U64 kingZone = KING_ATTACKS[sq] | bit(sq);
    // Extend king zone one rank outward
    if (c == WHITE) kingZone |= (kingZone << 8) & ALL;
    else kingZone |= (kingZone >> 8) & ALL;

    int attackCount = 0;
    int attackWeight = 0;

    U64 kn = b.pieceBB(them, KNIGHT);
    while (kn) {
        int sq2 = lsb(kn); kn &= kn - 1;
        if (KNIGHT_ATTACKS[sq2] & kingZone) {
            attackCount++;
            attackWeight += 4;
        }
    }

    U64 bi = b.pieceBB(them, BISHOP);
    while (bi) {
        int sq2 = lsb(bi); bi &= bi - 1;
        if (bishopAttacks(sq2, b.occ[2]) & kingZone) {
            attackCount++;
            attackWeight += 3;
        }
    }

    U64 ro = b.pieceBB(them, ROOK);
    while (ro) {
        int sq2 = lsb(ro); ro &= ro - 1;
        if (rookAttacks(sq2, b.occ[2]) & kingZone) {
            attackCount++;
            attackWeight += 5;
        }
    }

    U64 qu = b.pieceBB(them, QUEEN);
    while (qu) {
        int sq2 = lsb(qu); qu &= qu - 1;
        if (queenAttacks(sq2, b.occ[2]) & kingZone) {
            attackCount++;
            attackWeight += 6;
        }
    }

    // King danger formula: more attackers = exponentially worse
    if (attackCount > 0) {
        int danger = attackWeight * (attackCount + 1) / 2;
        mgScore -= danger * 3;
    }

    // Pawn storm (enemy pawns pushing toward our king)
    U64 enemyPawns = b.pieceBB(them, PAWN);
    for (int df = -1; df <= 1; df++) {
        int ff = f + df;
        if (ff < 0 || ff > 7) continue;
        U64 stormPawns;
        if (c == WHITE) stormPawns = enemyPawns & fileMask(ff) & (ALL << ((r + 1) * 8));
        else stormPawns = enemyPawns & fileMask(ff) & ~(ALL << (r * 8));
        int count = popcount(stormPawns);
        mgScore -= count * 8;
    }

    return taper(mgScore, egScore, phase);
}

// ── Mobility ─────────────────────────────────────────────────────────────────
static int evalMobility(const Board& b, Color c, int phase) {
    int mgScore = 0, egScore = 0;
    U64 all = b.occ[2];
    U64 notUs = ~b.occ[c];

    U64 kn = b.pieceBB(c, KNIGHT);
    while (kn) {
        int sq = lsb(kn); kn &= kn - 1;
        int n = popcount(KNIGHT_ATTACKS[sq] & notUs);
        mgScore += n * 2;
        egScore += n * 2;
    }

    U64 bi = b.pieceBB(c, BISHOP);
    while (bi) {
        int sq = lsb(bi); bi &= bi - 1;
        int n = popcount(bishopAttacks(sq, all) & notUs);
        mgScore += n * 2;
        egScore += n * 3;
    }

    U64 ro = b.pieceBB(c, ROOK);
    while (ro) {
        int sq = lsb(ro); ro &= ro - 1;
        int n = popcount(rookAttacks(sq, all) & notUs);
        mgScore += n * 3;
        egScore += n * 4;
    }

    U64 qu = b.pieceBB(c, QUEEN);
    while (qu) {
        int sq = lsb(qu); qu &= qu - 1;
        int n = popcount(queenAttacks(sq, all) & notUs);
        mgScore += n * 1;
        egScore += n * 2;
    }

    return taper(mgScore, egScore, phase);
}

// ── Piece threats / hanging pieces ───────────────────────────────────────────
static int evalThreats(const Board& b, Color c, int phase) {
    int mgScore = 0, egScore = 0;
    Color them = ~c;
    U64 all = b.occ[2];

    for (int pt = KNIGHT; pt <= QUEEN; pt++) {
        U64 ourPieces = b.pieceBB(c, PieceType(pt));
        while (ourPieces) {
            int sq = lsb(ourPieces); ourPieces &= ourPieces - 1;
            U64 attacks;
            if (pt == KNIGHT) attacks = KNIGHT_ATTACKS[sq];
            else if (pt == BISHOP) attacks = bishopAttacks(sq, all);
            else if (pt == ROOK) attacks = rookAttacks(sq, all);
            else if (pt == QUEEN) attacks = queenAttacks(sq, all);
            else continue;

            for (int ept = PAWN; ept <= QUEEN; ept++) {
                if (ept <= pt) continue; // Only attack more valuable pieces
                U64 victims = b.pieceBB(them, PieceType(ept)) & attacks;
                if (!victims) continue;

                // Check if victim is defended (simplified)
                bool defended = false;
                U64 enemyAttacks = 0;
                for (int dpt = PAWN; dpt <= QUEEN; dpt++) {
                    U64 dp = b.pieceBB(them, PieceType(dpt));
                    while (dp) {
                        int dsq = lsb(dp); dp &= dp - 1;
                        U64 datt;
                        if (dpt == PAWN) datt = PAWN_ATTACKS[them][dsq];
                        else if (dpt == KNIGHT) datt = KNIGHT_ATTACKS[dsq];
                        else if (dpt == BISHOP) datt = bishopAttacks(dsq, all);
                        else if (dpt == ROOK) datt = rookAttacks(dsq, all);
                        else if (dpt == QUEEN) datt = queenAttacks(dsq, all);
                        else continue;
                        if (datt & victims) { defended = true; break; }
                    }
                    if (defended) break;
                }

                if (!defended) {
                    int value = EW.material[ept] - EW.material[pt];
                    mgScore += value / 5;
                    egScore += value / 4;
                }
            }
        }
    }

    return taper(mgScore, egScore, phase);
}

// ── Space bonus (center control) ─────────────────────────────────────────────
static int evalSpace(const Board& b, Color c, int phase) {
    if (phase < 12) return 0; // Only in middlegame

    int mgScore = 0;
    U64 center = (bit(D4) | bit(E4) | bit(D5) | bit(E5));
    U64 extendedCenter = (bit(C3) | bit(D3) | bit(E3) | bit(F3) |
                          bit(C4) | bit(F4) |
                          bit(C5) | bit(F5) |
                          bit(C6) | bit(D6) | bit(E6) | bit(F6));

    for (int pt = PAWN; pt <= QUEEN; pt++) {
        U64 bb = b.pieceBB(c, PieceType(pt));
        mgScore += popcount(bb & center) * 3;
        mgScore += popcount(bb & extendedCenter) * 1;
    }

    return taper(mgScore, 0, phase);
}

// ── Main evaluate ─────────────────────────────────────────────────────────────
int evaluate(const Board& b) {
    int phase = gamePhase(b);

    int mg = 0, eg = 0;
    Color us = b.sideToMove;

    // Material and PST (tapered)
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == us) ? 1 : -1;
        for (int pt = PAWN; pt <= KING; pt++) {
            U64 bb = b.pieceBB(c, PieceType(pt));
            int mat = popcount(bb) * EW.material[pt];
            mg += sign * mat;
            eg += sign * mat;

            U64 tmp = bb;
            while (tmp) {
                int sq = lsb(tmp); tmp &= tmp - 1;
                mg += sign * mgPstValue(PieceType(pt), c, sq);
                eg += sign * egPstValue(PieceType(pt), c, sq);
            }
        }
    }

    int score = taper(mg, eg, phase);

    // Add positional components
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == us) ? 1 : -1;
        score += sign * evalPawns(b, c, phase);
        score += sign * evalKingSafety(b, c, phase);
        score += sign * evalMobility(b, c, phase);
        score += sign * evalThreats(b, c, phase);
        score += sign * evalSpace(b, c, phase);
    }

    // Tempo bonus (side to move is slightly better)
    score += taper(15, 5, phase);

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