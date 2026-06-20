#include "book.h"
#include "movegen.h"
#include <fstream>
#include <iostream>
#include <random>
#include <cstring>

OpeningBook Book;

// ── Convert Polyglot move to our Move format ──────────────────────────────────
// Polyglot: fromFile(3)|fromRank(3)|toFile(3)|toRank(3)|promo(3)|0
// Promo: 0=N, 1=B, 2=R, 3=Q
Move OpeningBook::polyglotToMove(uint16_t pgMove) {
    int fromFile = (pgMove >> 9) & 0x7;
    int fromRank = (pgMove >> 6) & 0x7;
    int toFile   = (pgMove >> 3) & 0x7;
    int toRank   = (pgMove >> 0) & 0x7;
    int promo    = (pgMove >> 12) & 0x7;

    int from = makeSquare(fromFile, fromRank);
    int to   = makeSquare(toFile, toRank);

    MoveFlag flag = QUIET;
    if (promo > 0) {
        // Polyglot promo: 1=N, 2=B, 3=R, 4=Q (but actually 0=N,1=B,2=R,3=Q)
        // Let's handle both cases safely
        PieceType pt;
        switch (promo) {
            case 0: pt = KNIGHT; break;
            case 1: pt = BISHOP; break;
            case 2: pt = ROOK;   break;
            case 3: pt = QUEEN;  break;
            default: pt = QUEEN; break;
        }
        if (pt == KNIGHT) flag = PROMO_N;
        else if (pt == BISHOP) flag = PROMO_B;
        else if (pt == ROOK)   flag = PROMO_R;
        else if (pt == QUEEN)  flag = PROMO_Q;
    }

    return makeMove(from, to, flag);
}

// ── Load Polyglot .bin file ───────────────────────────────────────────────────
bool OpeningBook::loadPolyglot(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "[Book] Could not open: " << path << "\n";
        return false;
    }

    entries_.clear();
    PolyglotEntry pe;
    size_t count = 0;

    while (f.read(reinterpret_cast<char*>(&pe), sizeof(pe))) {
        // Polyglot stores in big-endian, convert if needed
        // On little-endian systems (x86_64):
        auto swap64 = [](uint64_t x) -> uint64_t {
            return ((x & 0xFF00000000000000ULL) >> 56) |
                   ((x & 0x00FF000000000000ULL) >> 40) |
                   ((x & 0x0000FF0000000000ULL) >> 24) |
                   ((x & 0x000000FF00000000ULL) >> 8)  |
                   ((x & 0x00000000FF000000ULL) << 8)  |
                   ((x & 0x0000000000FF0000ULL) << 24) |
                   ((x & 0x000000000000FF00ULL) << 40) |
                   ((x & 0x00000000000000FFULL) << 56);
        };
        auto swap16 = [](uint16_t x) -> uint16_t {
            return (x >> 8) | (x << 8);
        };

        uint64_t key = swap64(pe.key);
        uint16_t move = swap16(pe.move);
        uint16_t weight = swap16(pe.weight);

        Move m = polyglotToMove(move);
        if (m != NULL_MOVE) {
            entries_[key].push_back({m, weight});
            count++;
        }
    }

    std::cout << "[Book] Loaded " << count << " entries from " << path << "\n";
    return count > 0;
}

// ── Probe book for a move ─────────────────────────────────────────────────────
Move OpeningBook::probe(const Board& b) const {
    auto it = entries_.find(b.hash);
    if (it == entries_.end() || it->second.empty())
        return NULL_MOVE;

    const auto& moves = it->second;

    // Weighted random selection
    int totalWeight = 0;
    for (const auto& bm : moves) totalWeight += bm.weight;
    if (totalWeight <= 0) return moves[0].move;

    static std::mt19937 rng(std::random_device{}());
    int pick = std::uniform_int_distribution<>(0, totalWeight - 1)(rng);

    for (const auto& bm : moves) {
        pick -= bm.weight;
        if (pick < 0) return bm.move;
    }
    return moves[0].move;
}

bool OpeningBook::hasMoves(const Board& b) const {
    auto it = entries_.find(b.hash);
    return it != entries_.end() && !it->second.empty();
}

void OpeningBook::clear() {
    entries_.clear();
}

// ── Built-in repertoire (used if no external book) ────────────────────────────
// A small but solid repertoire for both colors
void OpeningBook::initBuiltinRepertoire() {
    auto addLine = [this](const std::vector<std::string>& moves, int maxMoves = 6) {
        Board b; b.setStartPos();
        int count = 0;
        for (const auto& ms : moves) {
            if (count >= maxMoves) break;
            MoveList ml; generateMoves(b, ml);
            Move m = NULL_MOVE;
            for (int i = 0; i < ml.count; i++) {
                if (moveToStr(ml.moves[i]) == ms) { m = ml.moves[i]; break; }
            }
            if (m == NULL_MOVE) break;
            
            // Add this move to book for CURRENT position
            entries_[b.hash].push_back({m, 100});
            b.makeMove(m);
            count++;
        }
    };

    // White openings - deeper lines (6-8 moves)
    addLine({"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"}, 8);  // Ruy Lopez
    addLine({"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "d2d3", "f8c5"}, 8);  // Italian
    addLine({"e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "e5d4", "f3d4", "g8f6"}, 8);  // Scotch
    addLine({"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6"}, 8);  // Sicilian Najdorf
    addLine({"e2e4", "c7c5", "g1f3", "e7e6", "d2d4", "c5d4", "f3d4", "b8c6"}, 8);  // Sicilian Classical
    addLine({"e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "c1g5", "f8e7"}, 8);  // French
    addLine({"e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "d5e4", "c3e4", "b8d7"}, 8);  // Caro-Kann
    addLine({"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c1g5", "f8e7"}, 8);  // Queen's Gambit
    addLine({"d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "f8g7", "e2e4", "d7d6"}, 8);  // King's Indian
    addLine({"d2d4", "g8f6", "c2c4", "e7e6", "g1f3", "b4", "b1c3", "c5"}, 8);       // Nimzo-Indian
    addLine({"c2c4", "e7e5", "b1c3", "b8c6", "g1f3", "g8f6", "e2e4", "f8b4"}, 8);   // English
    addLine({"g1f3", "d7d5", "g2g3", "g8f6", "f1g2", "c7c6", "d2d3", "f8g7"}, 8);   // King's Indian Attack

    // Black openings - responses to e4
    addLine({"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6"}, 8);  // Sicilian
    addLine({"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"}, 8);  // Ruy Lopez defense
    addLine({"e2e4", "e7e6", "d2d4", "d7d5", "b1d2", "g8f6", "e5e4", "f6e4"}, 8);  // French
    addLine({"e2e4", "c7c6", "d2d4", "d7d5", "e4e5", "c8f5", "g1f3", "e7e6"}, 8);  // Caro-Kann
    addLine({"e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5a5", "d2d4", "g8f6"}, 8);  // Scandinavian
    addLine({"e2e4", "g7g6", "d2d4", "f8g7", "b1c3", "d7d6", "g1f3", "g8f6"}, 8);  // Pirc
    addLine({"e2e4", "d7d6", "d2d4", "g8f6", "b1c3", "g7g6", "f1e2", "f8g7"}, 8);  // Pirc alternate

    // Black openings - responses to d4
    addLine({"d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "f8g7", "e2e4", "d7d6"}, 8);  // King's Indian
    addLine({"d2d4", "g8f6", "c2c4", "e7e6", "g1f3", "b4", "b1d2", "d5"}, 8);       // Nimzo-Indian
    addLine({"d2d4", "d7d5", "c2c4", "c7c6", "g1f3", "g8f6", "b1c3", "e7e6"}, 8);  // Slav
    addLine({"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c1g5", "f8e7"}, 8);  // QGD
    addLine({"d2d4", "g8f6", "g1f3", "e7e6", "c1f4", "d7d5", "e2e3", "f8d6"}, 8);  // London vs KID setup

    std::cout << "[Book] Built-in repertoire loaded: " << entries_.size() << " positions\n";
}