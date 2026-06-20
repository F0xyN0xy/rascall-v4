#include "bitboard.h"

U64 KNIGHT_ATTACKS[64];
U64 KING_ATTACKS[64];
U64 PAWN_ATTACKS[2][64];

void initAttacks() {
    for (int sq = 0; sq < 64; sq++) {
        int r = rankOf(sq), f = fileOf(sq);
        U64 b = bit(sq);

        // Knight
        U64 kn = 0;
        if (r+2<=7 && f+1<=7) kn |= bit(makeSquare(f+1,r+2));
        if (r+2<=7 && f-1>=0) kn |= bit(makeSquare(f-1,r+2));
        if (r-2>=0 && f+1<=7) kn |= bit(makeSquare(f+1,r-2));
        if (r-2>=0 && f-1>=0) kn |= bit(makeSquare(f-1,r-2));
        if (r+1<=7 && f+2<=7) kn |= bit(makeSquare(f+2,r+1));
        if (r+1<=7 && f-2>=0) kn |= bit(makeSquare(f-2,r+1));
        if (r-1>=0 && f+2<=7) kn |= bit(makeSquare(f+2,r-1));
        if (r-1>=0 && f-2>=0) kn |= bit(makeSquare(f-2,r-1));
        KNIGHT_ATTACKS[sq] = kn;

        // King
        U64 ki = 0;
        if (r+1<=7)            ki |= bit(makeSquare(f,  r+1));
        if (r-1>=0)            ki |= bit(makeSquare(f,  r-1));
        if (f+1<=7)            ki |= bit(makeSquare(f+1,r  ));
        if (f-1>=0)            ki |= bit(makeSquare(f-1,r  ));
        if (r+1<=7 && f+1<=7) ki |= bit(makeSquare(f+1,r+1));
        if (r+1<=7 && f-1>=0) ki |= bit(makeSquare(f-1,r+1));
        if (r-1>=0 && f+1<=7) ki |= bit(makeSquare(f+1,r-1));
        if (r-1>=0 && f-1>=0) ki |= bit(makeSquare(f-1,r-1));
        KING_ATTACKS[sq] = ki;

        // Pawn attacks
        PAWN_ATTACKS[WHITE][sq] = 0;
        PAWN_ATTACKS[BLACK][sq] = 0;
        if (r+1<=7) {
            if (f-1>=0) PAWN_ATTACKS[WHITE][sq] |= bit(makeSquare(f-1,r+1));
            if (f+1<=7) PAWN_ATTACKS[WHITE][sq] |= bit(makeSquare(f+1,r+1));
        }
        if (r-1>=0) {
            if (f-1>=0) PAWN_ATTACKS[BLACK][sq] |= bit(makeSquare(f-1,r-1));
            if (f+1<=7) PAWN_ATTACKS[BLACK][sq] |= bit(makeSquare(f+1,r-1));
        }
    }
}