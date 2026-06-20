#include "movegen.h"

// ── Helper: add pawn promotions ───────────────────────────────────────────────
static void addPromos(MoveList& ml, int from, int to, bool capture) {
    ml.add(makeMove(from, to, capture ? PROMO_CAPTURE_Q : PROMO_Q));
    ml.add(makeMove(from, to, capture ? PROMO_CAPTURE_R : PROMO_R));
    ml.add(makeMove(from, to, capture ? PROMO_CAPTURE_B : PROMO_B));
    ml.add(makeMove(from, to, capture ? PROMO_CAPTURE_N : PROMO_N));
}

// ── Helper: iterate a bitboard adding quiet/capture moves ─────────────────────
static void addMoves(MoveList& ml, int from, U64 quietBB, U64 capBB) {
    U64 caps = capBB;
    while (caps) { int to = lsb(caps); caps &= caps-1; ml.add(makeMove(from, to, CAPTURE)); }
    U64 qs   = quietBB;
    while (qs)   { int to = lsb(qs);   qs   &= qs-1;   ml.add(makeMove(from, to, QUIET));   }
}

// ── Generate all pseudo-legal moves ──────────────────────────────────────────
void generateMoves(const Board& b, MoveList& ml) {
    Color us   = b.sideToMove;
    Color them = ~us;
    U64 myBB   = b.occ[us];
    U64 thBB   = b.occ[them];
    U64 all    = b.occ[2];
    U64 empty  = ~all;

    // ── Pawns ─────────────────────────────────────────────────────────────────
    U64 pawns = b.pieceBB(us, PAWN);
    if (us == WHITE) {
        U64 single = (pawns << 8) & empty;
        U64 dbl    = ((single & RANK_3) << 8) & empty;
        U64 capL   = (pawns << 7) & ~FILE_H & thBB;
        U64 capR   = (pawns << 9) & ~FILE_A & thBB;
        // Quiet pushes
        U64 qq = single & ~RANK_8;
        while (qq) { int to=lsb(qq); qq&=qq-1; ml.add(makeMove(to-8,to,QUIET)); }
        // Double push
        while (dbl) { int to=lsb(dbl); dbl&=dbl-1; ml.add(makeMove(to-16,to,DOUBLE_PUSH)); }
        // Promotions
        U64 pr = single & RANK_8;
        while (pr) { int to=lsb(pr); pr&=pr-1; addPromos(ml,to-8,to,false); }
        // Captures
        while (capL) { int to=lsb(capL); capL&=capL-1;
            if (rankOf(to)==7) addPromos(ml,to-7,to,true);
            else ml.add(makeMove(to-7,to,CAPTURE)); }
        while (capR) { int to=lsb(capR); capR&=capR-1;
            if (rankOf(to)==7) addPromos(ml,to-9,to,true);
            else ml.add(makeMove(to-9,to,CAPTURE)); }
        // En passant
        if (b.epSq != NO_SQ) {
            U64 epAtk = PAWN_ATTACKS[BLACK][b.epSq] & pawns;
            while (epAtk) { int from=lsb(epAtk); epAtk&=epAtk-1;
                ml.add(makeMove(from, b.epSq, EP_CAPTURE)); }
        }
    } else {
        U64 single = (pawns >> 8) & empty;
        U64 dbl    = ((single & RANK_6) >> 8) & empty;
        U64 capL   = (pawns >> 9) & ~FILE_H & thBB;
        U64 capR   = (pawns >> 7) & ~FILE_A & thBB;
        U64 qq = single & ~RANK_1;
        while (qq) { int to=lsb(qq); qq&=qq-1; ml.add(makeMove(to+8,to,QUIET)); }
        while (dbl) { int to=lsb(dbl); dbl&=dbl-1; ml.add(makeMove(to+16,to,DOUBLE_PUSH)); }
        U64 pr = single & RANK_1;
        while (pr) { int to=lsb(pr); pr&=pr-1; addPromos(ml,to+8,to,false); }
        while (capL) { int to=lsb(capL); capL&=capL-1;
            if (rankOf(to)==0) addPromos(ml,to+9,to,true);
            else ml.add(makeMove(to+9,to,CAPTURE)); }
        while (capR) { int to=lsb(capR); capR&=capR-1;
            if (rankOf(to)==0) addPromos(ml,to+7,to,true);
            else ml.add(makeMove(to+7,to,CAPTURE)); }
        if (b.epSq != NO_SQ) {
            U64 epAtk = PAWN_ATTACKS[WHITE][b.epSq] & pawns;
            while (epAtk) { int from=lsb(epAtk); epAtk&=epAtk-1;
                ml.add(makeMove(from, b.epSq, EP_CAPTURE)); }
        }
    }

    // ── Knights ───────────────────────────────────────────────────────────────
    U64 kn = b.pieceBB(us, KNIGHT);
    while (kn) {
        int sq=lsb(kn); kn&=kn-1;
        U64 att = KNIGHT_ATTACKS[sq];
        addMoves(ml, sq, att & empty, att & thBB);
    }

    // ── Bishops ───────────────────────────────────────────────────────────────
    U64 bi = b.pieceBB(us, BISHOP);
    while (bi) {
        int sq=lsb(bi); bi&=bi-1;
        U64 att = bishopAttacks(sq, all);
        addMoves(ml, sq, att & empty, att & thBB);
    }

    // ── Rooks ─────────────────────────────────────────────────────────────────
    U64 ro = b.pieceBB(us, ROOK);
    while (ro) {
        int sq=lsb(ro); ro&=ro-1;
        U64 att = rookAttacks(sq, all);
        addMoves(ml, sq, att & empty, att & thBB);
    }

    // ── Queens ────────────────────────────────────────────────────────────────
    U64 qu = b.pieceBB(us, QUEEN);
    while (qu) {
        int sq=lsb(qu); qu&=qu-1;
        U64 att = queenAttacks(sq, all);
        addMoves(ml, sq, att & empty, att & thBB);
    }

    // ── King ──────────────────────────────────────────────────────────────────
    {
        int sq = b.kingSquare(us);
        U64 att = KING_ATTACKS[sq];
        addMoves(ml, sq, att & empty & ~myBB, att & thBB);
    }

    // ── Castling ──────────────────────────────────────────────────────────────
    if (us == WHITE) {
        if ((b.castleRights & WHITE_OO)
            && !(all & 0x60ULL)
            && !b.isAttacked(E1, BLACK)
            && !b.isAttacked(F1, BLACK)
            && !b.isAttacked(G1, BLACK))
            ml.add(makeMove(E1, G1, KING_CASTLE));

        if ((b.castleRights & WHITE_OOO)
            && !(all & 0x0EULL)
            && !b.isAttacked(E1, BLACK)
            && !b.isAttacked(D1, BLACK)
            && !b.isAttacked(C1, BLACK))
            ml.add(makeMove(E1, C1, QUEEN_CASTLE));
    } else {
        if ((b.castleRights & BLACK_OO)
            && !(all & 0x6000000000000000ULL)
            && !b.isAttacked(E8, WHITE)
            && !b.isAttacked(F8, WHITE)
            && !b.isAttacked(G8, WHITE))
            ml.add(makeMove(E8, G8, KING_CASTLE));

        if ((b.castleRights & BLACK_OOO)
            && !(all & 0x0E00000000000000ULL)
            && !b.isAttacked(E8, WHITE)
            && !b.isAttacked(D8, WHITE)
            && !b.isAttacked(C8, WHITE))
            ml.add(makeMove(E8, C8, QUEEN_CASTLE));
    }
}

// ── Generate captures only (for quiescence search) ───────────────────────────
void generateCaptures(const Board& b, MoveList& ml) {
    Color us   = b.sideToMove;
    Color them = ~us;
    U64 thBB   = b.occ[them];
    U64 all    = b.occ[2];
    U64 empty  = ~all;

    // Pawn captures + promotions
    U64 pawns = b.pieceBB(us, PAWN);
    if (us == WHITE) {
        U64 single = (pawns << 8) & empty & RANK_8;
        while (single) { int to=lsb(single); single&=single-1; addPromos(ml,to-8,to,false); }
        U64 cL = (pawns << 7) & ~FILE_H & thBB;
        U64 cR = (pawns << 9) & ~FILE_A & thBB;
        while (cL) { int to=lsb(cL); cL&=cL-1;
            if (rankOf(to)==7) addPromos(ml,to-7,to,true); else ml.add(makeMove(to-7,to,CAPTURE)); }
        while (cR) { int to=lsb(cR); cR&=cR-1;
            if (rankOf(to)==7) addPromos(ml,to-9,to,true); else ml.add(makeMove(to-9,to,CAPTURE)); }
        if (b.epSq != NO_SQ) {
            U64 epA = PAWN_ATTACKS[BLACK][b.epSq] & pawns;
            while (epA) { int from=lsb(epA); epA&=epA-1; ml.add(makeMove(from,b.epSq,EP_CAPTURE)); }
        }
    } else {
        U64 single = (pawns >> 8) & empty & RANK_1;
        while (single) { int to=lsb(single); single&=single-1; addPromos(ml,to+8,to,false); }
        U64 cL = (pawns >> 9) & ~FILE_H & thBB;
        U64 cR = (pawns >> 7) & ~FILE_A & thBB;
        while (cL) { int to=lsb(cL); cL&=cL-1;
            if (rankOf(to)==0) addPromos(ml,to+9,to,true); else ml.add(makeMove(to+9,to,CAPTURE)); }
        while (cR) { int to=lsb(cR); cR&=cR-1;
            if (rankOf(to)==0) addPromos(ml,to+7,to,true); else ml.add(makeMove(to+7,to,CAPTURE)); }
        if (b.epSq != NO_SQ) {
            U64 epA = PAWN_ATTACKS[WHITE][b.epSq] & pawns;
            while (epA) { int from=lsb(epA); epA&=epA-1; ml.add(makeMove(from,b.epSq,EP_CAPTURE)); }
        }
    }

    // Other pieces - captures only
    U64 kn = b.pieceBB(us, KNIGHT);
    while (kn) { int sq=lsb(kn); kn&=kn-1; U64 a=KNIGHT_ATTACKS[sq]&thBB; addMoves(ml,sq,0,a); }
    U64 bi = b.pieceBB(us, BISHOP);
    while (bi) { int sq=lsb(bi); bi&=bi-1; U64 a=bishopAttacks(sq,all)&thBB; addMoves(ml,sq,0,a); }
    U64 ro = b.pieceBB(us, ROOK);
    while (ro) { int sq=lsb(ro); ro&=ro-1; U64 a=rookAttacks(sq,all)&thBB;   addMoves(ml,sq,0,a); }
    U64 qu = b.pieceBB(us, QUEEN);
    while (qu) { int sq=lsb(qu); qu&=qu-1; U64 a=queenAttacks(sq,all)&thBB;  addMoves(ml,sq,0,a); }
    int ksq = b.kingSquare(us);
    U64 a = KING_ATTACKS[ksq] & thBB;
    addMoves(ml, ksq, 0, a);
}

// ── Move string helpers ────────────────────────────────────────────────────────
std::string moveToStr(Move m) {
    if (m == NULL_MOVE) return "0000";
    std::string s = squareName(fromSq(m)) + squareName(toSq(m));
    if (isPromo(m)) {
        const char* pt = "nbrq";
        s += pt[promoType(m) - KNIGHT];
    }
    return s;
}

Move strToMove(Board& b, const std::string& s) {
    if (s.size() < 4) return NULL_MOVE;
    int from = parseSquare(s.substr(0,2));
    int to   = parseSquare(s.substr(2,2));
    MoveList ml; generateMoves(b, ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (fromSq(m)==from && toSq(m)==to) {
            if (isPromo(m)) {
                if (s.size() < 5) continue;
                char pc = s[4];
                PieceType pt = promoType(m);
                if (pc=='n'&&pt==KNIGHT) return m;
                if (pc=='b'&&pt==BISHOP) return m;
                if (pc=='r'&&pt==ROOK)   return m;
                if (pc=='q'&&pt==QUEEN)  return m;
            } else return m;
        }
    }
    return NULL_MOVE;
}