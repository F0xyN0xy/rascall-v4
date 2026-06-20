#pragma once
#include "types.h"
#include "board.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

struct PolyglotEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};

struct BookMove {
    Move move;
    int  weight;
};

class OpeningBook {
public:
    bool loadPolyglot(const std::string& path);
    Move probe(const Board& b) const;
    bool hasMoves(const Board& b) const;
    void clear();
    size_t size() const { return entries_.size(); }

    // Built-in repertoire (public so uci.cpp can call it)
    void initBuiltinRepertoire();

private:
    std::unordered_map<uint64_t, std::vector<BookMove>> entries_;
    static Move polyglotToMove(uint16_t pgMove);
};

extern OpeningBook Book;