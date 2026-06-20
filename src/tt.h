#pragma once
#include "types.h"
#include <random>
#include <cstring>

// ── Zobrist keys ──────────────────────────────────────────────────────────────
namespace Zobrist {
    extern U64 psq[12][64];   // piece × square
    extern U64 side;           // black to move
    extern U64 castle[16];     // castle rights
    extern U64 ep[8];          // en-passant file

    inline void init() {
        std::mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
        for (auto& a : psq)  for (auto& v : a) v = rng();
        side = rng();
        for (auto& v : castle) v = rng();
        for (auto& v : ep)     v = rng();
    }
}

// ── Transposition table entry ─────────────────────────────────────────────────
enum TTFlag : uint8_t { TT_NONE=0, TT_EXACT, TT_LOWER, TT_UPPER };

struct TTEntry {
    U64     key   = 0;
    int     score = 0;       // Internal score (mate scores encoded)
    Move    move  = NULL_MOVE;
    int8_t  depth = 0;
    TTFlag  flag  = TT_NONE;
    int8_t  mateIn = 0;      // 0 = no mate, +N = mate in N plies for us, -N = mated in N
    uint8_t age   = 0;       // For replacement strategy
};

// ── Transposition table (power-of-2 size) ─────────────────────────────────────
class TranspositionTable {
public:
    static constexpr size_t DEFAULT_MB = 64;

    void resize(size_t mb = DEFAULT_MB) {
        size_t bytes = mb * 1024 * 1024;
        count_ = bytes / sizeof(TTEntry);
        table_.assign(count_, TTEntry{});
        generation_ = 0;
    }

    void clear() { 
        std::fill(table_.begin(), table_.end(), TTEntry{}); 
        generation_ = 0;
    }

    void newSearch() { generation_++; }

    void store(U64 key, int score, Move move, int depth, TTFlag flag, int mateIn = 0) {
        size_t idx = key % count_;
        TTEntry& e = table_[idx];

        // Replacement strategy: replace if empty, same depth or deeper, or old generation
        bool replace = (e.key == 0) || 
                       (e.depth <= depth) || 
                       (e.age != generation_);

        if (replace) {
            e = { key, score, move, (int8_t)depth, flag, (int8_t)mateIn, generation_ };
        }
    }

    TTEntry* probe(U64 key) {
        TTEntry* e = &table_[key % count_];
        return e->key == key ? e : nullptr;
    }

    size_t hashfull() const {
        // Approximate: count non-empty entries in first 1000 buckets
        size_t filled = 0;
        size_t sample = std::min(count_, size_t(1000));
        for (size_t i = 0; i < sample; i++) {
            if (table_[i].key != 0) filled++;
        }
        return filled * 1000 / sample;
    }

private:
    std::vector<TTEntry> table_;
    size_t count_ = 0;
    uint8_t generation_ = 0;
};

extern TranspositionTable TT;