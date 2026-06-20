#include "board.h"
#include "movegen.h"
#include <sstream>
#include <iostream>
#include <cstring>

// ── Zobrist globals ───────────────────────────────────────────────────────────
namespace Zobrist {
    U64 psq[12][64];
    U64 side;
    U64 castle[16];
    U64 ep[8];
}
TranspositionTable TT;

// ── Internal helpers ──────────────────────────────────────────────────────────
void Board::putPiece(Piece p, int sq) {
    mailbox[sq] = p;
    pieces[p]  |= bit(sq);
    occ[colorOf(p)] |= bit(sq);
    occ[2]          |= bit(sq);
    hash ^= Zobrist::psq[p][sq];
}

void Board::removePiece(int sq) {
    Piece p = mailbox[sq];
    if (p == EMPTY) return;
    mailbox[sq] = EMPTY;
    pieces[p]  &= ~bit(sq);
    occ[colorOf(p)] &= ~bit(sq);
    occ[2]          &= ~bit(sq);
    hash ^= Zobrist::psq[p][sq];
}

void Board::movePiece(int from, int to) {
    Piece p = mailbox[from];
    removePiece(from);
    putPiece(p, to);
}

// ── setStartPos ───────────────────────────────────────────────────────────────
void Board::setStartPos() {
    setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// ── FEN parsing ───────────────────────────────────────────────────────────────
bool Board::setFEN(const std::string& fen) {
    // Reset
    for (auto& bb : pieces) bb = 0;
    for (auto& bb : occ)    bb = 0;
    for (auto& p  : mailbox) p = EMPTY;
    hash = 0; castleRights = 0; epSq = NO_SQ;
    halfmoveClock = 0; fullmoveNum = 1;
    history.clear();

    std::istringstream ss(fen);
    std::string board, side, castle, ep;
    int hm = 0, fm = 1;
    ss >> board >> side >> castle >> ep >> hm >> fm;

    // Piece placement
    int sq = A8;
    for (char c : board) {
        if (c == '/') { sq -= 16; continue; }
        if (c >= '1' && c <= '8') { sq += c - '0'; continue; }
        const std::string chars = "PNBRQKpnbrqk";
        int idx = chars.find(c);
        if (idx == (int)std::string::npos) return false;
        putPiece(Piece(idx), sq++);
    }

    sideToMove = (side == "b") ? BLACK : WHITE;
    if (sideToMove == BLACK) hash ^= Zobrist::side;

    // Castling
    for (char c : castle) {
        if (c == 'K') castleRights |= WHITE_OO;
        if (c == 'Q') castleRights |= WHITE_OOO;
        if (c == 'k') castleRights |= BLACK_OO;
        if (c == 'q') castleRights |= BLACK_OOO;
    }
    hash ^= Zobrist::castle[castleRights];

    // En passant
    if (ep != "-") {
        epSq = parseSquare(ep);
        if (epSq != NO_SQ) hash ^= Zobrist::ep[fileOf(epSq)];
    }

    halfmoveClock = hm;
    fullmoveNum   = fm;
    return true;
}

std::string Board::getFEN() const {
    std::string fen;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            Piece p = mailbox[makeSquare(f, r)];
            if (p == EMPTY) { empty++; }
            else { if (empty) { fen += char('0'+empty); empty=0; } fen += PIECE_CH[p]; }
        }
        if (empty) fen += char('0'+empty);
        if (r > 0) fen += '/';
    }
    fen += (sideToMove == WHITE) ? " w " : " b ";
    std::string cr;
    if (castleRights & WHITE_OO)  cr += 'K';
    if (castleRights & WHITE_OOO) cr += 'Q';
    if (castleRights & BLACK_OO)  cr += 'k';
    if (castleRights & BLACK_OOO) cr += 'q';
    fen += cr.empty() ? "-" : cr;
    fen += " ";
    fen += (epSq == NO_SQ) ? "-" : squareName(epSq);
    fen += " " + std::to_string(halfmoveClock);
    fen += " " + std::to_string(fullmoveNum);
    return fen;
}

// ── Attack helpers ────────────────────────────────────────────────────────────
U64 Board::attackersTo(int sq, U64 occupied) const {
    return (PAWN_ATTACKS[BLACK][sq]  & pieceBB(WHITE, PAWN))
         | (PAWN_ATTACKS[WHITE][sq]  & pieceBB(BLACK, PAWN))
         | (KNIGHT_ATTACKS[sq]       & (pieceBB(WHITE,KNIGHT)|pieceBB(BLACK,KNIGHT)))
         | (KING_ATTACKS[sq]         & (pieceBB(WHITE,KING)  |pieceBB(BLACK,KING)))
         | (bishopAttacks(sq, occupied) & (pieceBB(WHITE,BISHOP)|pieceBB(BLACK,BISHOP)
                                         |pieceBB(WHITE,QUEEN) |pieceBB(BLACK,QUEEN)))
         | (rookAttacks(sq, occupied)   & (pieceBB(WHITE,ROOK)  |pieceBB(BLACK,ROOK)
                                         |pieceBB(WHITE,QUEEN) |pieceBB(BLACK,QUEEN)));
}

bool Board::isAttacked(int sq, Color by) const {
    if (PAWN_ATTACKS[~by][sq]   & pieceBB(by, PAWN))   return true;
    if (KNIGHT_ATTACKS[sq]      & pieceBB(by, KNIGHT)) return true;
    if (KING_ATTACKS[sq]        & pieceBB(by, KING))    return true;
    if (bishopAttacks(sq,occ[2])& (pieceBB(by,BISHOP)|pieceBB(by,QUEEN))) return true;
    if (rookAttacks(sq,occ[2])  & (pieceBB(by,ROOK)  |pieceBB(by,QUEEN))) return true;
    return false;
}

bool Board::inCheck() const {
    return isAttacked(kingSquare(sideToMove), ~sideToMove);
}

// ── Make move ─────────────────────────────────────────────────────────────────
void Board::makeMove(Move m) {
    UndoInfo ui;
    ui.move     = m;
    ui.castle   = castleRights;
    ui.epSq     = epSq;
    ui.halfmove = halfmoveClock;
    ui.hash     = hash;
    ui.captured = EMPTY;

    // Remove old ep/castle from hash
    if (epSq != NO_SQ)   hash ^= Zobrist::ep[fileOf(epSq)];
    hash ^= Zobrist::castle[castleRights];

    epSq = NO_SQ;
    halfmoveClock++;

    int from = fromSq(m), to = toSq(m);
    MoveFlag flag = flagOf(m);
    Color us = sideToMove, them = ~us;

    // Handle capture
    if (isCapture(m) && !isEP(m)) {
        ui.captured = mailbox[to];
        removePiece(to);
        halfmoveClock = 0;
    }

    // En passant capture
    if (isEP(m)) {
        int capSq = makeSquare(fileOf(to), rankOf(from));
        ui.captured = mailbox[capSq];
        removePiece(capSq);
        halfmoveClock = 0;
    }

    // Move piece
    Piece moving = mailbox[from];
    if (typeOf(moving) == PAWN) halfmoveClock = 0;

    if (isPromo(m)) {
        removePiece(from);
        putPiece(makePiece(us, promoType(m)), to);
    } else {
        movePiece(from, to);
    }

    // Castling rook
    if (flag == KING_CASTLE) {
        int rookFrom = makeSquare(7, rankOf(from));
        int rookTo   = makeSquare(5, rankOf(from));
        movePiece(rookFrom, rookTo);
    } else if (flag == QUEEN_CASTLE) {
        int rookFrom = makeSquare(0, rankOf(from));
        int rookTo   = makeSquare(3, rankOf(from));
        movePiece(rookFrom, rookTo);
    }

    // Double pawn push sets ep square
    if (flag == DOUBLE_PUSH) {
        epSq = makeSquare(fileOf(from), rankOf(from) + (us == WHITE ? 1 : -1));
        hash ^= Zobrist::ep[fileOf(epSq)];
    }

    // Update castling rights
    static const int castleMask[64] = {
        ~WHITE_OOO,15,15,15,~(WHITE_OO|WHITE_OOO),15,15,~WHITE_OO,
        15,15,15,15,15,15,15,15,
        15,15,15,15,15,15,15,15,
        15,15,15,15,15,15,15,15,
        15,15,15,15,15,15,15,15,
        15,15,15,15,15,15,15,15,
        15,15,15,15,15,15,15,15,
        ~BLACK_OOO,15,15,15,~(BLACK_OO|BLACK_OOO),15,15,~BLACK_OO
    };
    castleRights &= castleMask[from] & castleMask[to];
    hash ^= Zobrist::castle[castleRights];

    if (sideToMove == BLACK) fullmoveNum++;
    sideToMove = them;
    hash ^= Zobrist::side;

    history.push_back(ui);
}

// ── Unmake move ───────────────────────────────────────────────────────────────
void Board::unmakeMove() {
    UndoInfo ui = history.back();
    history.pop_back();

    sideToMove     = ~sideToMove;
    castleRights   = ui.castle;
    epSq           = ui.epSq;
    halfmoveClock  = ui.halfmove;
    hash           = ui.hash;
    if (sideToMove == BLACK) fullmoveNum--;

    int from = fromSq(ui.move), to = toSq(ui.move);
    MoveFlag flag = flagOf(ui.move);
    Color us = sideToMove;

    // Undo promotion
    if (isPromo(ui.move)) {
        removePiece(to);
        putPiece(makePiece(us, PAWN), from);
    } else {
        movePiece(to, from);
    }

    // Restore capture
    if (isCapture(ui.move) && !isEP(ui.move)) {
        putPiece(ui.captured, to);
    }
    if (isEP(ui.move)) {
        int capSq = makeSquare(fileOf(to), rankOf(from));
        putPiece(ui.captured, capSq);
    }

    // Undo castling rook
    if (flag == KING_CASTLE) {
        movePiece(makeSquare(5, rankOf(from)), makeSquare(7, rankOf(from)));
    } else if (flag == QUEEN_CASTLE) {
        movePiece(makeSquare(3, rankOf(from)), makeSquare(0, rankOf(from)));
    }
}

// ── Null move ─────────────────────────────────────────────────────────────────
void Board::makeNullMove() {
    UndoInfo ui = {};
    ui.epSq     = epSq;
    ui.castle   = castleRights;
    ui.halfmove = halfmoveClock;
    ui.hash     = hash;
    ui.move     = NULL_MOVE;
    if (epSq != NO_SQ) { hash ^= Zobrist::ep[fileOf(epSq)]; epSq = NO_SQ; }
    hash ^= Zobrist::side;
    sideToMove = ~sideToMove;
    halfmoveClock++;
    history.push_back(ui);
}

void Board::unmakeNullMove() {
    UndoInfo ui = history.back();
    history.pop_back();
    sideToMove    = ~sideToMove;
    epSq          = ui.epSq;
    castleRights  = ui.castle;
    halfmoveClock = ui.halfmove;
    hash          = ui.hash;
}

// ── Perft ─────────────────────────────────────────────────────────────────────
uint64_t Board::perft(int depth) {
    if (depth == 0) return 1;
    MoveList ml;
    generateMoves(*this, ml);
    uint64_t nodes = 0;
    for (int i = 0; i < ml.count; i++) {
        makeMove(ml.moves[i]);
        if (!isAttacked(kingSquare(~sideToMove), sideToMove))
            nodes += perft(depth - 1);
        unmakeMove();
    }
    return nodes;
}

// ── Print board ───────────────────────────────────────────────────────────────
void Board::print() const {
    std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; r--) {
        std::cout << (r+1) << " |";
        for (int f = 0; f < 8; f++)
            std::cout << " " << PIECE_CH[mailbox[makeSquare(f,r)]] << " |";
        std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    }
    std::cout << "    a   b   c   d   e   f   g   h\n";
    std::cout << "FEN: " << getFEN() << "\n\n";
}