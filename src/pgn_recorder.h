#pragma once
#include "board.h"
#include <string>
#include <vector>

class PgnRecorder {
public:
    void reset(const std::string& startFen);
    void append(const Board& before, Move move);
    void setResult(const std::string& result);
    void saveActive() const;
    void finish();

private:
    std::string startFen_;
    std::string result_ = "*";
    std::vector<std::string> sanMoves_;
    std::string activePath_ = "games/active-game.pgn";
    std::string makePgn() const;
};

extern PgnRecorder GameRecorder;
