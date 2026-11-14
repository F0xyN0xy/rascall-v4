#include "pgn_recorder.h"
#include "movegen.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

PgnRecorder GameRecorder;

namespace {
bool legalMove(Board& board, Move move) {
    MoveList moves;
    generateMoves(board, moves);
    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i] != move) continue;
        board.makeMove(move);
        const bool legal = !board.isAttacked(board.kingSquare(~board.sideToMove), board.sideToMove);
        board.unmakeMove();
        return legal;
    }
    return false;
}

bool hasLegalMove(Board& board) {
    MoveList moves;
    generateMoves(board, moves);
    for (int i = 0; i < moves.count; ++i)
        if (legalMove(board, moves.moves[i])) return true;
    return false;
}

std::string sanFor(Board& board, Move move) {
    const int from = fromSq(move), to = toSq(move);
    const Piece moving = board.mailbox[from];
    const PieceType type = typeOf(moving);
    const bool capture = isCapture(move);
    std::string san;

    if (flagOf(move) == KING_CASTLE) san = "O-O";
    else if (flagOf(move) == QUEEN_CASTLE) san = "O-O-O";
    else {
        if (type != PAWN) {
            san += " PNBRQK"[type + 1];
            bool sameFile = false, sameRank = false, ambiguous = false;
            MoveList candidates;
            generateMoves(board, candidates);
            for (int i = 0; i < candidates.count; ++i) {
                Move other = candidates.moves[i];
                if (other == move || toSq(other) != to || typeOf(board.mailbox[fromSq(other)]) != type ||
                    board.mailbox[fromSq(other)] != moving || !legalMove(board, other)) continue;
                ambiguous = true;
                if (fileOf(fromSq(other)) == fileOf(from)) sameFile = true;
                if (rankOf(fromSq(other)) == rankOf(from)) sameRank = true;
            }
            if (ambiguous) {
                if (!sameFile) san += char('a' + fileOf(from));
                else if (!sameRank) san += char('1' + rankOf(from));
                else san += squareName(from);
            }
        } else if (capture) san += char('a' + fileOf(from));

        if (capture) san += 'x';
        san += squareName(to);
        if (isPromo(move)) {
            static const char promoChars[] = " nbrq";
            san += '=';
            san += promoChars[promoType(move)];
        }
    }

    board.makeMove(move);
    const bool check = board.inCheck();
    if (check) san += hasLegalMove(board) ? "+" : "#";
    board.unmakeMove();
    return san;
}
}

void PgnRecorder::reset(const std::string& startFen) {
    startFen_ = startFen;
    result_ = "*";
    sanMoves_.clear();
}

void PgnRecorder::append(const Board& before, Move move) {
    if (move == NULL_MOVE || !legalMove(const_cast<Board&>(before), move)) return;
    Board copy = before;
    sanMoves_.push_back(sanFor(copy, move));
}

void PgnRecorder::setResult(const std::string& result) {
    if (result == "1-0" || result == "0-1" || result == "1/2-1/2" || result == "*")
        result_ = result;
    saveActive();
}

std::string PgnRecorder::makePgn() const {
    const bool standard = startFen_ == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << "[Event \"Rascall V4 game\"]\n"
        << "[Site \"Unknown\"]\n"
        << "[Date \"" << std::put_time(&tm, "%Y.%m.%d") << "\"]\n"
        << "[Round \"-\"]\n[White \"Unknown\"]\n[Black \"Unknown\"]\n";
    if (!standard) out << "[SetUp \"1\"]\n[FEN \"" << startFen_ << "\"]\n";
    out << "[Result \"" << result_ << "\"]\n\n";
    Board board;
    board.setFEN(startFen_);
    for (std::size_t i = 0; i < sanMoves_.size(); ++i) {
        if (i % 2 == 0) out << (i / 2 + 1) << ". ";
        else if (i == 1) out << "";
        out << sanMoves_[i] << ' ';
    }
    out << result_ << "\n";
    return out.str();
}

void PgnRecorder::saveActive() const {
    try {
        std::filesystem::create_directories("games/raw");
        const std::string temp = activePath_ + ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) return;
            file << makePgn();
            file.flush();
            if (!file) return;
        }
        std::error_code ec;
        std::filesystem::remove(activePath_, ec);
        std::filesystem::rename(temp, activePath_, ec);
    } catch (...) {
        // Recording must never interrupt UCI communication or engine search.
    }
}

void PgnRecorder::finish() {
    if (sanMoves_.empty()) return;
    saveActive();
    try {
        std::filesystem::create_directories("games/raw");
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::filesystem::path target = std::filesystem::path("games/raw") /
            ("rascall-" + std::to_string(stamp) + ".pgn");
        std::error_code ec;
        std::filesystem::rename(activePath_, target, ec);
        if (ec) {
            std::filesystem::copy_file(activePath_, target,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) std::filesystem::remove(activePath_, ec);
        }
    } catch (...) {
    }
    sanMoves_.clear();
    startFen_.clear();
    result_ = "*";
}
