# Automatic game recording and Stockfish review

Rascall's UCI process records the move history it receives and keeps an auto-saved
PGN snapshot at `games/active-game.pgn`. On `ucinewgame` or `quit`, the
snapshot is moved into `games/raw/`. If the GUI does not report a result, the
PGN result remains `*` (unknown). This is deliberate: the engine must not guess
whether a game was won, lost or drawn.

The recorder works from the UCI `position` history, so it can capture games
played through local GUIs, tournament tools and online UCI adapters, provided
the adapter sends the complete move list for each position. The recorder writes
relative to the process working directory. For Lichess, set the bot's engine
working directory to the Rascall build folder if you want the `games/` folder
next to the executable.

## Build

From the repository root in PowerShell:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Stockfish post-game analysis

Install the Python dependency:

```powershell
py -m pip install -r tools/analysis/requirements.txt
```

Analyse all raw PGNs:

```powershell
py tools/analysis/analyse_pgn.py .\games\raw `
  --stockfish "$HOME\Documents\stockfish\stockfish-windows-x86-64-universal.exe" `
  --depth 12
```

Or analyse one PGN by passing its path instead of the directory. The script
writes annotated PGNs and a JSON report to `games/analysed/`. It flags moves
whose estimated loss is at least 100 centipawns by default. This is a diagnostic
signal, not a perfect classification: results depend on Stockfish depth, time,
hardware and position complexity.

## Notes and limitations

- UCI has no standard game-result command. Unknown results stay as `*`.
- The tournament runner already writes its own complete PGNs and results; those
  are still the authoritative source for tournament outcomes.
- The recorder stores SAN move text and the starting FEN. It does not infer
  player names, event names or time controls unless a future adapter supplies
  them.
- Stockfish analysis does not automatically change Rascall's source code or
  weights. Review recurring mistakes first, then turn selected positions into
  regression tests and validate any tuning in fresh engine matches.
- PGNs are saved locally. Do not commit personal game archives or Lichess tokens.
