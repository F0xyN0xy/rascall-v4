#include "uci.h"
#include "movegen.h"
#include "gensfen.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <fstream>
#include <chrono>

// ── Global state ──────────────────────────────────────────────────────────────
static Board gBoard;
static std::thread searchThread;
static bool isPondering = false;
static int ponderSoftTime = 0;
static int ponderHardTime = 0;
static SearchLimits currentLimits;
static bool useBook = true;
static bool usePonder = true;

// ── Parse position ────────────────────────────────────────────────────────────
static void parsePosition(const std::string& line) {
    std::istringstream ss(line);
    std::string token;
    ss >> token; // "position"

    ss >> token;
    if (token == "startpos") {
        gBoard.setStartPos();
        ss >> token; // might be "moves"
    } else if (token == "fen") {
        std::string fen;
        while (ss >> token && token != "moves")
            fen += token + " ";
        gBoard.setFEN(fen);
    }

    while (ss >> token) {
        if (token == "moves") continue;
        Move m = strToMove(gBoard, token);
        if (m != NULL_MOVE) gBoard.makeMove(m);
    }
}

// ── FAST game phase estimate (no movegen!) ────────────────────────────────────
static int estimateGamePhaseFast(const Board& b) {
    int npm = popcount(b.occ[2] & ~b.pieceBB(WHITE, PAWN) & ~b.pieceBB(BLACK, PAWN)
                          & ~b.pieceBB(WHITE, KING) & ~b.pieceBB(BLACK, KING));
    return npm;
}

// ── FAST complexity estimate (no movegen!) ────────────────────────────────────
static int estimateComplexityFast(const Board& b) {
    int complexity = 0;
    complexity += popcount(b.occ[2]) * 2;
    complexity += popcount(b.pieceBB(WHITE, PAWN) | b.pieceBB(BLACK, PAWN));
    if (b.inCheck()) complexity += 30;
    complexity += std::abs(popcount(b.occ[WHITE]) - popcount(b.occ[BLACK])) * 3;
    return complexity;
}

// ── Calculate time allocation ─────────────────────────────────────────────────
static void calculateTimeLimits(int myTime, int myInc, int movestogo, 
                                int& softMs, int& hardMs, const Board& b) {
    if (myTime <= 0) {
        softMs = hardMs = 0;
        return;
    }

    int phase = estimateGamePhaseFast(b);
    int complexity = estimateComplexityFast(b);

    // MUCH more conservative factors
    float baseFactor;
    if (phase > 24)       baseFactor = 0.008f;   // Opening: 0.8%
    else if (phase > 12)  baseFactor = 0.015f;   // Middlegame: 1.5%
    else if (phase > 6)   baseFactor = 0.025f;   // Endgame: 2.5%
    else                  baseFactor = 0.040f;   // Late endgame: 4%

    if (movestogo > 0 && movestogo < 10) {
        baseFactor *= 0.7f;
    }

    float complexityMult = 1.0f;
    if (complexity > 50)      complexityMult = 1.2f;
    else if (complexity > 30) complexityMult = 1.1f;
    else if (complexity < 15) complexityMult = 0.8f;

    float incBonus = myInc * 0.4f;

    softMs = int(myTime * baseFactor * complexityMult + incBonus);
    hardMs = int(myTime * baseFactor * complexityMult * 1.8f + incBonus);

    // ABSOLUTE CAPS
    int maxTime = myTime / 15;
    int maxSoft = myTime / 25;

    if (phase > 24) {
        maxTime = std::min(maxTime, 2000);   // Max 2s in opening
        maxSoft = std::min(maxSoft, 1200);   // Soft: 1.2s
    }

    hardMs = std::min(hardMs, maxTime);
    softMs = std::min(softMs, maxSoft);

    hardMs = std::max(hardMs, softMs + 100);

    if (softMs < 50) softMs = 50;
    if (hardMs < 100) hardMs = 100;

    int overhead = 100;
    hardMs = std::max(50, hardMs - overhead);
    softMs = std::max(50, softMs - overhead);
}

// ── Search lifecycle and legal fallback ───────────────────────────────────────
static void stopSearch() {
    if (searchThread.joinable()) {
        Engine.stop = true;
        searchThread.join();
    }
}

static bool isLegalMove(Board& board, Move move) {
    MoveList moves;
    generateMoves(board, moves);
    for (int i = 0; i < moves.count; ++i) {
        if (moves.moves[i] != move) continue;
        board.makeMove(move);
        bool legal = !board.isAttacked(board.kingSquare(~board.sideToMove), board.sideToMove);
        board.unmakeMove();
        return legal;
    }
    return false;
}

static Move firstLegalMove(Board b) {
    MoveList moves;
    generateMoves(b, moves);
    for (int i = 0; i < moves.count; ++i) {
        if (isLegalMove(b, moves.moves[i])) return moves.moves[i];
    }
    return NULL_MOVE;
}

// Obtain a legal predicted reply from the transposition table's child position.
// UCI's "bestmove X ponder Y" means Y is the opponent's predicted response.
static Move findPonderReply(Board board, Move bestMove) {
    if (bestMove == NULL_MOVE || !isLegalMove(board, bestMove)) return NULL_MOVE;

    board.makeMove(bestMove);
    TTEntry* entry = TT.probe(board.hash);
    if (!entry || entry->move == NULL_MOVE) {
        board.unmakeMove();
        return NULL_MOVE;
    }

    Move reply = entry->move;
    bool legal = isLegalMove(board, reply);
    board.unmakeMove();
    return legal ? reply : NULL_MOVE;
}

// ── Start search thread ───────────────────────────────────────────────────────
static void startSearch(const SearchLimits& lim) {
    stopSearch();
    Engine.stop = false;
    currentLimits = lim;

    // Snapshot the position before launching the worker. The UCI thread may
    // receive another command while the search is running.
    Board searchBoard = gBoard;
    searchThread = std::thread([lim, searchBoard]() mutable {
        SearchResult result = Engine.search(searchBoard, lim);
        Move bestMove = result.bestMove;

        // A stop during the first iteration can leave the search without a PV.
        // UCI still requires a bestmove response, so return a legal fallback.
        if (bestMove == NULL_MOVE) bestMove = firstLegalMove(searchBoard);

        if (bestMove == NULL_MOVE) {
            std::cout << "bestmove 0000" << std::endl;
        } else {
            Move reply = findPonderReply(searchBoard, bestMove);
            std::cout << "bestmove " << moveToStr(bestMove);
            if (reply != NULL_MOVE) std::cout << " ponder " << moveToStr(reply);
            std::cout << std::endl;
        }
    });
}

// ── Parse go command ──────────────────────────────────────────────────────────
static void parseGo(const std::string& line) {
    stopSearch();
    std::istringstream ss(line);
    std::string token;
    ss >> token; // "go"

    SearchLimits lim;
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
    bool doPonder = false;
    bool depthSet = false;

    while (ss >> token) {
        if (token == "depth")     { ss >> lim.depth; depthSet = true; }
        if (token == "movetime")  { ss >> lim.movetime; }
        if (token == "infinite")  { lim.infinite = true; }
        if (token == "wtime")     { ss >> wtime; }
        if (token == "btime")     { ss >> btime; }
        if (token == "winc")      { ss >> winc; }
        if (token == "binc")      { ss >> binc; }
        if (token == "movestogo") { ss >> movestogo; }
        if (token == "ponder")    { doPonder = true; }
    }

    doPonder = doPonder && usePonder;

    // === INSTANT BOOK MOVE — NO SEARCH THREAD AT ALL ===
    if (useBook && !doPonder && !lim.infinite && lim.movetime == 0 && !depthSet) {
        Move bookMove = Book.probe(gBoard);
        if (bookMove != NULL_MOVE) {
            std::cout << "info depth 1 score cp 0 nodes 1 time 0 pv " 
                      << moveToStr(bookMove) << std::endl;
            std::cout << "bestmove " << moveToStr(bookMove) << std::endl;
            std::cout.flush();
            return; // DONE — no thread started, no delay
        }
    }
    // ===

    // Time management
    if (!lim.movetime && !lim.infinite && !depthSet) {
        int myTime = (gBoard.sideToMove == WHITE) ? wtime : btime;
        int myInc  = (gBoard.sideToMove == WHITE) ? winc  : binc;

        if (myTime > 0) {
            int softMs, hardMs;
            calculateTimeLimits(myTime, myInc, movestogo, softMs, hardMs, gBoard);
            lim.softTime = softMs;
            lim.movetime = hardMs;
        }
    }

    if (doPonder) {
        isPondering = true;
        lim.ponder = true;
        lim.infinite = true;
        lim.depth = MAX_PLY;

        // Keep the clock budget calculated from the go command. It starts
        // counting only when ponderhit arrives, not while the engine thinks.
        ponderSoftTime = lim.softTime;
        ponderHardTime = lim.movetime;
        lim.movetime = 0;
        lim.softTime = 0;
    } else {
        isPondering = false;
        ponderSoftTime = 0;
        ponderHardTime = 0;
    }

    startSearch(lim);
}

// ── Main UCI loop ─────────────────────────────────────────────────────────────
void uciLoop() {
    EW.load("weights.bin");

    if (!Book.loadPolyglot("book.bin")) {
        std::cerr << "info string No external book, using built-in repertoire\n";
        Book.initBuiltinRepertoire();
    }

    gBoard.setStartPos();
    std::string line;

    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            std::cout << "id name Rascall v4" << std::endl;
            std::cout << "id author FoxyNet475" << std::endl;
            std::cout << "option name Hash type spin default 64 min 1 max 1024" << std::endl;
            std::cout << "option name Depth type spin default 0 min 0 max 64" << std::endl;
            std::cout << "option name BookFile type string default book.bin" << std::endl;
            std::cout << "option name UseBook type check default true" << std::endl;
            std::cout << "option name Ponder type check default true" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (cmd == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (cmd == "ucinewgame") {
            stopSearch();
            TT.clear();
            gBoard.setStartPos();
            isPondering = false;
            ponderSoftTime = 0;
            ponderHardTime = 0;
        }
        else if (cmd == "setoption") {
            stopSearch();
            std::string name, val;
            ss >> name; // "name"
            ss >> name; // actual name
            ss >> val;  // "value"
            ss >> val;  // actual value
            if (name == "Hash") {
                int mb = std::stoi(val);
                TT.resize(mb);
            }
            else if (name == "BookFile") {
                if (!Book.loadPolyglot(val)) {
                    Book.initBuiltinRepertoire();
                }
            }
            else if (name == "UseBook") {
                useBook = (val == "true");
            }
            else if (name == "Ponder") {
                usePonder = (val == "true");
            }
            else if (name == "Depth") {
                int d = std::stoi(val);
                if (d > 0) currentLimits.depth = d;
            }
        }
        else if (cmd == "position") {
            stopSearch();
            parsePosition(line);
        }
        else if (cmd == "go") {
            parseGo(line);
        }
        else if (cmd == "stop") {
            isPondering = false;
            stopSearch();
        }
        else if (cmd == "ponderhit") {
            if (isPondering) {
                isPondering = false;
                Engine.setTimeLimits(ponderSoftTime, ponderHardTime);
            }
        }
        else if (cmd == "quit") {
            isPondering = false;
            stopSearch();
            break;
        }
        else if (cmd == "perft") {
            int depth = 5;
            ss >> depth;
            gBoard.print();
            auto t0 = std::chrono::steady_clock::now();
            uint64_t nodes = gBoard.perft(depth);
            auto t1 = std::chrono::steady_clock::now();
            int ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
            std::cout << "Perft(" << depth << ") = " << nodes
                      << "  time: " << ms << "ms";
        }
        else if (cmd == "display") {
            gBoard.print();
        }
        else if (cmd == "train") {
            int iters = 50, depth = 4, games = 10;
            ss >> iters >> depth >> games;
            Trainer.cfg.iterations = iters;
            Trainer.cfg.selfPlayDepth = depth;
            Trainer.cfg.gamesPerIter = games;
            Engine.stop = false;
            Trainer.train(iters);
        }
        else if (cmd == "eval") {
            gBoard.print();
            std::cout << "Static eval: " << evaluate(gBoard) << " cp";
        }
        else if (cmd == "gensfen") {
            GensfenOptions opts;
            std::string value;

            while (ss >> cmd) {
                if (cmd == "depth") {
                    ss >> value; opts.searchDepth = std::stoi(value);
                }
                else if (cmd == "loop") {
                    ss >> value; opts.numPositions = std::stoull(value);
                }
                else if (cmd == "output_file_name") {
                    ss >> opts.outputFile;
                }
                else if (cmd == "random_move_count") {
                    ss >> value; opts.randomMoveCount = std::stoi(value);
                }
                else if (cmd == "random_move_minply") {
                    ss >> value; opts.randomMoveMinPly = std::stoi(value);
                }
                else if (cmd == "random_move_maxply") {
                    ss >> value; opts.randomMoveMaxPly = std::stoi(value);
                }
                else if (cmd == "eval_limit") {
                    ss >> value; opts.evalLimit = std::stoi(value);
                }
                else if (cmd == "eval_count") {
                    ss >> value; opts.evalCountToAdjudicate = std::stoi(value);
                }
                else if (cmd == "write_minply") {
                    ss >> value; opts.writeMinPly = std::stoi(value);
                }
                else if (cmd == "threads") {
                    ss >> value; opts.numThreads = std::stoi(value);
                }
            }

            generateTrainingData(opts);
        }
    }
}