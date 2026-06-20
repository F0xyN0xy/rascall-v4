#ifndef GENSFEN_H
#define GENSFEN_H

#include <cstdint>
#include <string>

struct GensfenOptions {
    int searchDepth = 8;
    uint64_t numPositions = 10000000;
    std::string outputFile = "trainingdata.bin";
    int randomMoveCount = 7;
    int randomMoveMinPly = 1;
    int randomMoveMaxPly = 24;
    int evalLimit = 3000;
    int evalCountToAdjudicate = 4;
    int writeMinPly = 16;
    int numThreads = 1;
};

void generateTrainingData(const GensfenOptions& opts);

#endif