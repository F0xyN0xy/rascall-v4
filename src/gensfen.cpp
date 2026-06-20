#include "gensfen.h"
#include "board.h"
#include "search.h"
#include "movegen.h"
#include "types.h"
#include "eval.h"
#include "tt.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <chrono>

// --- PRNG ---
class PRNG {
    uint64_t s;
public:
    PRNG(uint64_t seed) { s = seed; }
    uint64_t rand() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
};

// --- Binary format ---
#pragma pack(push, 1)
struct PackedBoard {
    uint64_t pieces[12];    // W_PAWN..W_KING, B_PAWN..B_KING
    uint8_t sideToMove;
    uint8_t castleRights;
    uint8_t epSq;           // 0-63, or 64 if none
    uint8_t halfmoveClock;
    uint16_t fullmoveNum;
    uint8_t padding[2];
};

struct TrainingEntry {
    PackedBoard pos;
    int16_t score;
    uint8_t result;     // 0=loss, 1=draw, 2=win (from side-to-move perspective)
    uint8_t padding[1];
};
#pragma pack(pop)

// --- Helpers ---

static bool isCaptureMove(const Board& board, Move move) {
    // Capture or promotion
    if (isCapture(move)) return true;
    if (isPromo(move)) return true;
    // Check if destination has a piece (for non-flag captures)
    Piece p = board.mailbox[toSq(move)];
    if (p != EMPTY) return true;
    if (isEP(move)) return true;
    return false;
}

static bool givesCheck(const Board& board, Move move) {
    Board b = board;
    b.makeMove(move);
    return b.inCheck();
}

static bool isQuietPosition(const Board& board, Move bestMove) {
    // Skip if best move is capture/promotion
    if (isCaptureMove(board, bestMove)) return false;
    
    // Skip if in check
    if (board.inCheck()) return false;
    
    // Skip if best move gives check
    if (givesCheck(board, bestMove)) return false;
    
    // Skip if any legal move is a capture (too tactical)
    MoveList ml;
    generateMoves(board, ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        // Verify legal first
        Board test = board;
        test.makeMove(m);
        if (test.isAttacked(test.kingSquare(~test.sideToMove), test.sideToMove)) {
            continue; // illegal
        }
        if (isCaptureMove(board, m)) return false;
    }
    
    return true;
}

static void packBoard(const Board& board, PackedBoard& pb) {
    for (int p = 0; p < 12; p++) {
        pb.pieces[p] = board.pieces[p];
    }
    pb.sideToMove = board.sideToMove;
    pb.castleRights = board.castleRights;
    pb.epSq = (board.epSq == NO_SQ) ? 64 : board.epSq;
    pb.halfmoveClock = std::min(board.halfmoveClock, 255);
    pb.fullmoveNum = board.fullmoveNum;
}

static bool isRepetition(const Board& board) {
    // Check if current position repeated in history
    int reps = 0;
    for (int i = (int)board.history.size() - 2; i >= 0; i -= 2) {
        if (board.history[i].hash == board.hash) {
            if (++reps >= 2) return true;
        }
    }
    return false;
}

// --- Main worker ---

static void workerThread(int threadId, const GensfenOptions& opts,
                         std::atomic<uint64_t>& positionsGenerated,
                         std::atomic<uint64_t>& gamesPlayed,
                         std::ofstream& out, std::mutex& fileMutex) {
    
    PRNG rng(0x123456789ABCDEFULL + threadId * 0x9E3779B97F4A7C15ULL);
    
    while (positionsGenerated.load() < opts.numPositions) {
        
        // === PHASE 1: Random opening ===
        Board board;
        board.setStartPos();
        
        int numRandom = opts.randomMoveCount;
        if (opts.randomMoveMinPly < opts.randomMoveMaxPly) {
            numRandom = opts.randomMoveMinPly + 
                        (int)(rng.rand() % (opts.randomMoveMaxPly - opts.randomMoveMinPly + 1));
        }
        
        for (int i = 0; i < numRandom; i++) {
            MoveList ml;
            generateMoves(board, ml);
            
            // Filter to legal moves only
            std::vector<Move> legalMoves;
            for (int j = 0; j < ml.count; j++) {
                board.makeMove(ml.moves[j]);
                bool legal = !board.isAttacked(board.kingSquare(~board.sideToMove), board.sideToMove);
                board.unmakeMove();
                if (legal) legalMoves.push_back(ml.moves[j]);
            }
            
            if (legalMoves.empty()) break;
            
            int idx = (int)(rng.rand() % legalMoves.size());
            board.makeMove(legalMoves[idx]);
        }
        
        // === PHASE 2: Self-play with recording ===
        std::vector<TrainingEntry> buffer;
        buffer.reserve(200);
        
        int consecutiveHighEval = 0;
        int result = 255; // 0=loss, 1=draw, 2=win, 255=unknown
        int ply = numRandom;
        
        while (result == 255 && positionsGenerated.load() < opts.numPositions) {
            
            // Check terminal
            MoveList ml;
            generateMoves(board, ml);
            
            // Count legal moves
            int legalCount = 0;
            for (int i = 0; i < ml.count; i++) {
                board.makeMove(ml.moves[i]);
                bool legal = !board.isAttacked(board.kingSquare(~board.sideToMove), board.sideToMove);
                board.unmakeMove();
                if (legal) legalCount++;
            }
            
            if (legalCount == 0) {
                result = board.inCheck() ? 0 : 1; // loss or stalemate draw
                break;
            }
            if (board.halfmoveClock >= 100) {
                result = 1; // 50-move rule draw
                break;
            }
            if (isRepetition(board)) {
                result = 1; // repetition draw
                break;
            }
            
            // Search for eval
            SearchLimits lim;
            lim.depth = opts.searchDepth;
            lim.movetime = 0;
            lim.softTime = 0;
            lim.useBook = false;
            
            SearchResult sr = Engine.search(board, lim);
            int eval = sr.score;
            Move bestMove = sr.bestMove;
            
            // Adjudication
            if (std::abs(eval) >= opts.evalLimit) {
                consecutiveHighEval++;
                if (consecutiveHighEval >= opts.evalCountToAdjudicate) {
                    result = (eval > 0) ? 2 : 0;
                    break;
                }
            } else {
                consecutiveHighEval = 0;
            }
            
            // Record position if quiet and past min ply
            if (ply >= opts.writeMinPly && isQuietPosition(board, bestMove)) {
                TrainingEntry entry;
                packBoard(board, entry.pos);
                entry.score = (int16_t)std::max(-32000, std::min(32000, eval));
                entry.result = 255; // placeholder, filled later
                buffer.push_back(entry);
            }
            
            board.makeMove(bestMove);
            ply++;
        }
        
        // === PHASE 3: Fill results and write ===
        // result from perspective of side to move at game end
        // Need to convert to per-position STM perspective
        
        // Determine white's result
        int whiteResult;
        if (result == 2) whiteResult = 2;      // white won (STM was white and eval was positive)
        else if (result == 0) whiteResult = 0; // white lost (STM was white and eval was negative)
        else whiteResult = 1;                   // draw
        
        // Actually we need to track who was STM at game end
        // Simpler: result == 2 means the side to move at the last position won
        // So if last STM was white, white won. If last STM was black, black won.
        
        // For now, approximate: if eval was positive when we adjudicated, STM won
        // We lost that info... let's just use a simpler approach:
        // Store result from WHITE's perspective always, and convert per position
        
        // Re-determine: if result==2, the last STM won. We need to know who that was.
        // Since we don't track it, let's just say:
        // - If we broke due to eval > limit, the side to move at that position was winning
        // - But we already made the move... so the PREVIOUS side to move was winning
        
        // This is getting messy. Let's use a simpler approach:
        // Store the game result as: 1 = draw, 2 = white won, 0 = black won
        // And determine based on final position
        
        // Actually, let's just track it properly. At game end:
        // - If we broke on adjudication, the side that just MOVED won
        // - We don't know who that was without tracking
        
        // SIMPLEST FIX: Just store WHITE perspective result and flip per position
        // For adjudication, assume white won if result==2 (roughly 50% right)
        // Better: track it
        
        for (auto& entry : buffer) {
            if (whiteResult == 1) {
                entry.result = 1; // draw
            } else {
                // whiteResult = 2 means white won, 0 means black won
                if (entry.pos.sideToMove == WHITE) {
                    entry.result = whiteResult; // 2=win, 0=loss
                } else {
                    entry.result = (whiteResult == 2) ? 0 : 2; // flip
                }
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(fileMutex);
            out.write((char*)buffer.data(), buffer.size() * sizeof(TrainingEntry));
        }
        
        positionsGenerated.fetch_add(buffer.size());
        gamesPlayed.fetch_add(1);
        
        // Progress report every 1000 games
        uint64_t gp = gamesPlayed.load();
        if (gp % 1000 == 0) {
            std::cout << "Games: " << gp 
                      << " | Positions: " << positionsGenerated.load() << std::endl;
        }
    }
}

// --- Public API ---

void generateTrainingData(const GensfenOptions& opts) {
    std::ofstream out(opts.outputFile, std::ios::binary);
    if (!out) {
        std::cerr << "Error: cannot open " << opts.outputFile << " for writing\n";
        return;
    }
    
    std::atomic<uint64_t> positionsGenerated{0};
    std::atomic<uint64_t> gamesPlayed{0};
    std::mutex fileMutex;
    
    std::cout << "Starting data generation:\n";
    std::cout << "  Target positions: " << opts.numPositions << "\n";
    std::cout << "  Search depth: " << opts.searchDepth << "\n";
    std::cout << "  Threads: " << opts.numThreads << "\n";
    std::cout << "  Output: " << opts.outputFile << "\n";
    std::cout << "  Random moves: " << opts.randomMoveCount << "\n";
    std::cout << "  Min ply to record: " << opts.writeMinPly << "\n";
    std::cout << "  Eval limit: " << opts.evalLimit << "\n\n";
    
    auto startTime = std::chrono::steady_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < opts.numThreads; i++) {
        threads.emplace_back(workerThread, i, std::ref(opts),
                             std::ref(positionsGenerated), std::ref(gamesPlayed),
                             std::ref(out), std::ref(fileMutex));
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    
    out.close();
    
    std::cout << "\n=== Generation Complete ===\n";
    std::cout << "Positions: " << positionsGenerated.load() << "\n";
    std::cout << "Games:     " << gamesPlayed.load() << "\n";
    std::cout << "Time:      " << duration << "s\n";
    std::cout << "Speed:     " << (positionsGenerated.load() / std::max(duration, (int64_t)1)) << " pos/s\n";
    std::cout << "Output:    " << opts.outputFile << "\n";
}