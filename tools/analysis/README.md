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

Analyse all raw PGNs (adjust the path if your engine's working directory puts
the archive under `build/games/raw`):

```powershell
py tools/analysis/analyse_pgn.py .\\build\\games\\raw `
  --stockfish "$HOME\\Documents\\stockfish\\stockfish-windows-x86-64-universal.exe" `
  --depth 12 `
  --output-dir .\\build\\games\\analysed
```

Or analyse one PGN by passing its path instead of the directory. The script
writes annotated PGNs and a JSON report to the selected output directory. It
flags moves whose estimated loss is at least 100 centipawns by default.

### How move loss is calculated

For each position, the script asks Stockfish for its unrestricted best move and
score, then searches again from the **same pre-move position** with the played
move as the only allowed root move. Both scores are expressed from the mover's
perspective. Centipawn loss is the difference between those two root scores,
clamped at zero. This avoids comparing an evaluation before the move against
one from a different position and removes the sign/perspective ambiguity in the
older calculation.

Mate scores are represented separately as mate distances, not converted into
large made-up centipawn values. A forced-mate outcome that changes from a
winning mate for the mover to no win (or a forced loss) is reported as a
`mate_regression`; mate positions are not assigned a centipawn loss.

The comparison is still a depth-limited estimate. Results can change with
Stockfish version, depth, hash size, hardware and position complexity. The
played-move search adds work, so analysis takes longer than the earlier
before/after-position method. Increase depth for critical positions and verify
important examples before using them as training labels.

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
