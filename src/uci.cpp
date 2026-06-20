#include "uci.h"
#include "movegen.h"
#include "gensfen.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <fstream>
#include <chrono>

// ── Stream stats (optional, for OBS overlay) ──────────────────────────────────
void writeStreamStats(const Board& b, const SearchResult& sr, int elapsedMs) {
    static int moveCounter = 0;
    moveCounter++;

    std::ofstream f("C:/Users/foxyn/Documents/Coding/Streaming/engine_stats.json");
    if (!f) return;

    f << "{\n";
    f << "  \"engine\": \"Rascall v4\",\n";
    f << "  \"depth\": " << sr.depth << ",\n";
    f << "  \"nodes\": " << sr.nodes << ",\n";
    f << "  \"nps\": " << (elapsedMs > 0 ? (int)(sr.nodes * 1000.0 / elapsedMs) : 0) << ",\n";
    if (sr.mateIn != 0) {
        f << "  \"mate\": " << sr.mateIn << ",\n";
        f << "  \"eval\": \"Mate in " << std::abs(sr.mateIn) << "\",\n";
    } else {
        f << "  \"eval\": " << sr.score << ",\n";
    }
    f << "  \"time\": " << elapsedMs << ",\n";
    f << "  \"pv\": \"" << moveToStr(sr.bestMove) << "\",\n";
    f << "  \"moveNumber\": " << moveCounter << "\n";
    f << "}\n";
}

// ── Global state ──────────────────────────────────────────────────────────────
static Board gBoard;
static std::thread searchThread;
static Move ponderMove = NULL_MOVE;
static bool isPondering = false;
static SearchLimits currentLimits;
static bool gUseBook = true;  // can be toggled via setoption
// Store clock at the time of the last "go ponder" so ponderhit can use it
static int gPonderWtime = 0, gPonderBtime = 0, gPonderWinc = 0, gPonderBinc = 0, gPonderMovestogo = 0;

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

    // Piece count
    complexity += popcount(b.occ[2]) * 2;

    // Pawns
    complexity += popcount(b.pieceBB(WHITE, PAWN) | b.pieceBB(BLACK, PAWN));

    // Checks
    if (b.inCheck()) complexity += 30;

    // Material imbalance
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

    int complexity = estimateComplexityFast(b);

    // --- Base allocation ---
    // Estimate moves remaining. Stockfish-style: assume ~50 moves left in opening,
    // scaling down as pieces come off. movestogo overrides if provided.
    float movesLeft;
    if (movestogo > 0) {
        movesLeft = (float)movestogo;
    } else {
        int phase = estimateGamePhaseFast(b);
        // phase ranges from ~28 (opening, all pieces) to 0 (bare kings)
        // Map to expected moves left: opening ~40, midgame ~30, endgame ~20
        if      (phase > 22) movesLeft = 40.0f;
        else if (phase > 14) movesLeft = 35.0f;
        else if (phase > 7)  movesLeft = 28.0f;
        else                 movesLeft = 18.0f;
    }

    // Usable time = current clock + expected future increments
    // Use 80% of the projected total to leave a safety buffer
    float usableTime = myTime + myInc * std::min(movesLeft - 1.0f, 30.0f);
    float baseMs = usableTime / movesLeft * 0.85f;

    // --- Complexity scaling ---
    float complexityMult = 1.0f;
    if      (complexity > 60) complexityMult = 1.35f;
    else if (complexity > 40) complexityMult = 1.15f;
    else if (complexity < 15) complexityMult = 0.80f;

    // --- movestogo urgency: give MORE time when close to time control ---
    if (movestogo > 0 && movestogo <= 5) {
        complexityMult *= 1.4f;   // urgent — spend more per move
    } else if (movestogo > 0 && movestogo <= 10) {
        complexityMult *= 1.15f;
    }

    softMs = int(baseMs * complexityMult);

    // Hard limit: allow up to 4x soft, but never more than 1/4 of remaining clock
    // (Stockfish uses roughly 3-5x the soft limit as hard limit)
    hardMs = int(softMs * 3.5f);
    hardMs = std::min(hardMs, myTime / 4);
    hardMs = std::max(hardMs, softMs + 50);

    // Safety floors
    if (softMs < 20)  softMs = 20;
    if (hardMs < 50)  hardMs = 50;

    // Never exceed half the clock no matter what
    hardMs = std::min(hardMs, myTime / 2);
    softMs = std::min(softMs, hardMs - 20);
    if (softMs < 20) softMs = 20;
}

// ── Start search thread ───────────────────────────────────────────────────────
static void startSearch(const SearchLimits& lim) {
    if (searchThread.joinable()) {
        Engine.stop = true;
        searchThread.join();
    }
    Engine.stop = false;

    auto searchStart = std::chrono::steady_clock::now();
    currentLimits = lim;

    searchThread = std::thread([lim, searchStart]() {
        Board b = gBoard;
        SearchResult result = Engine.search(b, lim);

        auto searchEnd = std::chrono::steady_clock::now();
        int elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(searchEnd - searchStart).count();

        writeStreamStats(b, result, elapsedMs);

        std::cout << "bestmove " << moveToStr(result.bestMove);
        if (ponderMove != NULL_MOVE && !lim.ponder) {
            std::cout << " ponder " << moveToStr(ponderMove);
        }
        std::cout << "\n";
        std::cout.flush();
    });
}

// ── Parse go command ──────────────────────────────────────────────────────────
static void parseGo(const std::string& line) {
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

    // === INSTANT BOOK MOVE — no thread, no search ===
    if (gUseBook && !doPonder && !lim.infinite && lim.movetime == 0) {
        Move bookMove = Book.probe(gBoard);
        if (bookMove != NULL_MOVE) {
            std::cout << "info depth 1 score cp 0 nodes 1 time 0 pv " 
                      << moveToStr(bookMove) << " bookmove" << std::endl;
            std::cout << "bestmove " << moveToStr(bookMove) << std::endl;
            std::cout.flush();
            return;
        }
    }

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
        lim.movetime = 0;
        lim.softTime = 0;

        // Save clock values so ponderhit can compute proper time limits
        gPonderWtime = wtime; gPonderBtime = btime;
        gPonderWinc  = winc;  gPonderBinc  = binc;
        gPonderMovestogo = movestogo;

        TTEntry* tte = TT.probe(gBoard.hash);
        if (tte && tte->move != NULL_MOVE) {
            ponderMove = tte->move;
        }
    } else {
        isPondering = false;
        ponderMove = NULL_MOVE;
    }

    lim.useBook = gUseBook;
    startSearch(lim);
}

// ── Main UCI loop ─────────────────────────────────────────────────────────────
void uciLoop() {
    EW.load("weights.bin");

    if (!Book.loadPolyglot("book.bin")) {
        std::cout << "info string No external book, using built-in repertoire\n";
        Book.initBuiltinRepertoire();
    }

    gBoard.setStartPos();
    std::string line;

    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            std::cout << "id name Rascall v4\n";
            std::cout << "id author FoxyNet475\n";
            std::cout << "option name Hash type spin default 64 min 1 max 1024\n";
            std::cout << "option name Depth type spin default 0 min 0 max 64\n";
            std::cout << "option name BookFile type string default book.bin\n";
            std::cout << "option name UseBook type check default true\n";
            std::cout << "option name Ponder type check default true\n";
            std::cout << "uciok\n";
            std::cout.flush();
        }
        else if (cmd == "isready") {
            std::cout << "readyok\n";
            std::cout.flush();
        }
        else if (cmd == "ucinewgame") {
            TT.clear();
            gBoard.setStartPos();
            isPondering = false;
            ponderMove = NULL_MOVE;
        }
        else if (cmd == "setoption") {
            std::string name, val;
            ss >> name; // "name"
            ss >> name; // actual name
            ss >> val;  // "value"
            ss >> val;  // actual value
            if (name == "Hash") {
                int mb = std::stoi(val);
                TT.resize(mb);
            }
            else if (name == "UseBook") {
                gUseBook = (val == "true");
            }
            else if (name == "BookFile") {
                if (!Book.loadPolyglot(val)) {
                    Book.initBuiltinRepertoire();
                }
            }
            else if (name == "Depth") {
                int d = std::stoi(val);
                if (d > 0) currentLimits.depth = d;
            }
        }
        else if (cmd == "position") {
            parsePosition(line);
        }
        else if (cmd == "go") {
            parseGo(line);
        }
        else if (cmd == "stop") {
            Engine.stop = true;
            isPondering = false;
            if (searchThread.joinable()) searchThread.join();
        }
        else if (cmd == "ponderhit") {
            if (isPondering) {
                isPondering = false;

                // Use the clock that was sent with the original "go ponder" command
                int myTime = (gBoard.sideToMove == WHITE) ? gPonderWtime : gPonderBtime;
                int myInc  = (gBoard.sideToMove == WHITE) ? gPonderWinc  : gPonderBinc;
                if (myTime <= 0) myTime = 5000; // fallback

                int softMs, hardMs;
                calculateTimeLimits(myTime, myInc, gPonderMovestogo, softMs, hardMs, gBoard);

                Engine.setTimeLimits(softMs, hardMs);
            }
        }
        else if (cmd == "quit") {
            Engine.stop = true;
            if (searchThread.joinable()) searchThread.join();
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
                      << "  time: " << ms << "ms\n";
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
            std::cout << "Static eval: " << evaluate(gBoard) << " cp\n";
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