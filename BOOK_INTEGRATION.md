
## How to integrate the opening book into your engine

### 1. Copy files
- Put `book.h` and `book.cpp` into your `src/` folder

### 2. Update CMakeLists.txt
Add `src/book.cpp` to the SOURCES list:

```cmake
set(SOURCES
    src/main.cpp
    src/bitboard.cpp
    src/board.cpp
    src/movegen.cpp
    src/eval.cpp
    src/search.cpp
    src/uci.cpp
    src/rl.cpp
    src/book.cpp        # <-- ADD THIS
)
```

### 3. Update search.h
Add the book include at the top:
```cpp
#include "book.h"
```

### 4. Update search.cpp — modify Searcher::search()

At the very beginning of the `search()` function, before iterative deepening:

```cpp
SearchResult Searcher::search(Board& b, const SearchLimits& lim) {
    startTime_ = std::chrono::steady_clock::now();
    timeLimit_ = lim.movetime;
    nodes_     = 0;
    stop       = false;
    std::memset(killers_, 0, sizeof(killers_));
    std::memset(history_, 0, sizeof(history_));

    // ── Opening book probe ──────────────────────────────────────────────
    Move bookMove = Book.probe(b);
    if (bookMove != NULL_MOVE) {
        SearchResult res;
        res.bestMove = bookMove;
        res.score    = 0;
        res.depth    = 1;
        res.nodes    = 1;
        std::cout << "info depth 1 score cp 0 nodes 1 pv " 
                  << moveToStr(bookMove) << " bookmove\n";
        return res;
    }
    // ────────────────────────────────────────────────────────────────────

    SearchResult result;
    // ... rest of function unchanged
```

### 5. Update uci.cpp — initialize the book

In `uciLoop()`, after loading weights, initialize the book:

```cpp
void uciLoop() {
    // Load weights if available
    EW.load("weights.bin");

    // Initialize opening book (try external first, fallback to built-in)
    if (!Book.loadPolyglot("book.bin")) {
        Book.initBuiltinRepertoire();
    }

    gBoard.setStartPos();
    // ... rest unchanged
```

Also update the `ucinewgame` handler to NOT clear the book:
```cpp
else if (cmd == "ucinewgame") {
    TT.clear();
    // Book.clear();  // <-- DON'T clear book between games!
    gBoard.setStartPos();
}
```

### 6. Rebuild
```bash
cmake --build build -j4
```

## How to get a bigger opening book

### Option A: Download a Polyglot book
- **Lichess Opening Explorer**: https://lichess.org/openings
- **Computer Chess Wiki**: https://www.chessprogramming.org/Opening_Book
- **Stockfish book**: Many sites host `codekiddy.bin` or `varied.bin`

Place the `.bin` file next to your `.exe` and name it `book.bin`.

### Option B: Generate from your own PGN games
Use `pgn-extract` to create a Polyglot book:
```bash
# Install pgn-extract
# Then:
pgn-extract -Wepd your_games.pgn > positions.epd
# Or use a polyglot book generator tool
```

### Option C: Expand the built-in repertoire
Edit `book.cpp` → `initBuiltinRepertoire()` and add more lines.
Each `addLine({"e2e4", "e7e5", ...})` adds a full opening line.

## Expected behavior
- First ~6-10 moves: Instant response (0ms), no search
- After book is exhausted: Normal search kicks in
- UCI output shows `bookmove` so GUIs know it came from book
