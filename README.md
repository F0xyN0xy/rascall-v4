# ♜ Rascall v4

A UCI-compatible chess engine written in C++ with a custom evaluation function, alpha-beta search with modern enhancements, and support for NNUE training data generation.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

---

## Features

| Feature | Description |
|---------|-------------|
| **Search** | Iterative deepening alpha-beta with aspiration windows, null-move pruning, late move reduction (LMR), quiescence search |
| **Evaluation** | Tapered hand-tuned evaluation with material, piece-square tables, pawn structure, mobility, and king safety |
| **Transposition Table** | 64MB+ Zobrist-hash based TT with age-based replacement |
| **Move Ordering** | MVV-LVA, killer moves, history heuristic, TT move priority |
| **Opening Book** | Polyglot `.bin` support + built-in repertoire |
| **Time Management** | Adaptive soft/hard limits with game phase and complexity estimation |
| **Training** | `gensfen` command for self-play data generation (Stockfish-compatible pipeline) |
| **UCI Protocol** | Full compatibility with Cute Chess, Arena, Lichess-bot, and other GUIs |

---

## Building

### Prerequisites
- C++17 compatible compiler (GCC, Clang, MSVC)
- CMake 3.10+ (optional) or Make

### Windows (MinGW / MSYS2)
```bash
# Using Make
make

# Or with CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Linux / macOS
```bash
make

# Or CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build Options
```bash
# Debug build (assertions, no optimizations)
make DEBUG=1

# Release build with optimizations
make RELEASE=1

# Specify target architecture
make ARCH=native   # Optimized for your CPU
make ARCH=avx2     # AVX2 instructions
make ARCH=ssse3    # SSSE3 (older CPUs)
```

---

## Usage

### Interactive UCI Mode
```bash
./rascall
uci
isready
go depth 15
# ... engine thinks ...
bestmove e2e4
quit
```

### With a GUI (Cute Chess, Arena, etc.)
1. Open your GUI's engine configuration
2. Add `rascall.exe` (Windows) or `./rascall` (Linux/macOS)
3. Set UCI protocol
4. Configure hash size (default: 64MB)

### Lichess Bot
```bash
# Install python-chess and lichess-bot
pip install chess lichess-bot

# Configure config.yml with your API token
# Set engine path to ./rascall
python lichess-bot.py
```

---

## UCI Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `Hash` | spin | 64 | Transposition table size in MB (1-1024) |
| `Depth` | spin | 0 | Maximum search depth (0 = unlimited) |
| `BookFile` | string | `book.bin` | Path to Polyglot opening book |
| `Ponder` | check | true | Enable pondering |

---

## Training Data Generation

Rascall v4 can generate training data for NNUE (Neural Network) evaluation:

```bash
./rascall
uci
isready
gensfen depth 8 loop 10000000 output_file_name training.bin random_move_count 7 write_minply 16 eval_limit 3000 threads 4
quit
```

### Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `depth` | 8 | Search depth for position evaluation |
| `loop` | 10M | Number of positions to generate |
| `output_file_name` | `trainingdata.bin` | Output file path |
| `random_move_count` | 7 | Random moves at start for diversity |
| `write_minply` | 16 | Minimum ply to record (skip opening) |
| `eval_limit` | 3000 | Adjudicate if |eval| > limit for 4 plies |
| `threads` | 1 | Parallel generation threads |

### Binary Format
The output uses a packed binary format compatible with Stockfish's training pipeline:
- 12 piece bitboards (white/black × P/N/B/R/Q/K)
- Side to move, castling rights, en passant, halfmove clock
- Evaluation score (int16, centipawns from STM perspective)
- Game result (0=loss, 1=draw, 2=win from STM perspective)

---

## Architecture

```
Rascall v4
├── Board Representation
│   ├── 12 bitboards (piece types × colors)
│   ├── Mailbox array for fast piece lookup
│   ├── Zobrist hashing for TT and repetition
│   └── Incremental make/unmake with undo stack
│
├── Search
│   ├── Iterative Deepening (1..MAX_PLY)
│   ├── Aspiration Windows (±50cp, widening on fail)
│   ├── Alpha-Beta with PV tracking
│   ├── Null Move Pruning (R=2+depth/4)
│   ├── Late Move Reduction (LMR)
│   ├── Quiescence Search (captures + promotions only)
│   └── Transposition Table (exact/lower/upper bounds)
│
├── Evaluation
│   ├── Material balance
│   ├── Piece-Square Tables (tapered, perspective-aware)
│   ├── Pawn structure (doubled, isolated, passed)
│   ├── Mobility (per piece type)
│   └── King safety (pawn shield)
│
└── Training (gensfen)
    ├── Self-play with random opening moves
    ├── Quiet position filtering (no checks/captures)
    ├── Early adjudication (decisive positions)
    └── Multi-threaded generation
```

---

## Performance

| Hardware | Depth | Nodes/second | Typical search time |
|----------|-------|-------------|---------------------|
| AMD Ryzen 5 3600 | 15 | ~2.5M nps | 3-8s (blitz) |
| Intel i7-12700H | 18 | ~4.0M nps | 5-12s (rapid) |
| Apple M1 | 16 | ~3.0M nps | 4-10s (rapid) |

*Performance varies by position complexity and time control.*

---

## Elo Estimates

| Version | Estimated Elo | Notes |
|---------|--------------|-------|
| v1 | ~1200 | Basic alpha-beta |
| v2 | ~1500 | Added TT, null move |
| v3 | ~1800 | LMR, aspiration windows |
| **v4** | **~2000-2200** | Improved eval, book, time management |

*Estimates based on self-play and limited testing against Stockfish levels.*

---

## File Structure

```
rascall-v4/
├── src/
│   ├── main.cpp          # Entry point, CLI args
│   ├── uci.cpp/h         # UCI protocol handler
│   ├── search.cpp/h      # Alpha-beta search engine
│   ├── eval.cpp/h        # Evaluation function & weights
│   ├── board.cpp/h       # Board representation & move make/unmake
│   ├── movegen.cpp/h     # Legal move generation
│   ├── bitboard.cpp/h    # Bitboard operations & attack tables
│   ├── tt.h              # Transposition table & Zobrist keys
│   ├── book.cpp/h        # Opening book (Polyglot + built-in)
│   ├── gensfen.cpp/h     # Training data generation
│   ├── rl.cpp/h          # Reinforcement learning (SPSA tuning)
│   └── types.h           # Core types, enums, move encoding
├── CMakeLists.txt        # CMake build configuration
├── Makefile              # Alternative Make build
├── README.md             # This file
├── LICENSE               # MIT License
└── assets/
    └── rascall.ico       # Windows executable icon
```

---

## Contributing

Contributions are welcome! Areas for improvement:

- [ ] NNUE evaluation network
- [ ] Syzygy tablebase support
- [ ] Multi-threaded search (SMP)
- [ ] Better time management with node-based allocation
- [ ] Futility pruning and reverse futility
- [ ] Singular extension
- [ ] Counter-move and follow-up history

Please open an issue or pull request on GitHub.

---

## Acknowledgments

- **Stockfish** — For the open-source NNUE training pipeline and endless inspiration
- **Chess Programming Wiki** — For comprehensive resources on engine development
- **Lichess** — For the excellent API and bot hosting platform
- **Rustic Chess** (Marcel Vanthoor) — For clear explanations of engine architecture

---

## License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for details.

```
MIT License

Copyright (c) 2026 FoxyNet475

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---