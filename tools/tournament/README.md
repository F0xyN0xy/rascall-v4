# UCI Tournament Runner

This tool runs round-robin games between local UCI chess engines. It is intended for repeatable strength comparisons and collecting positions for later analysis; it does **not** automatically train or improve an engine.

## Requirements

- Python 3.10+
- A working UCI executable for each engine
- Rascall V4 built in Release mode
- Stockfish 19 (optional, but recommended as an external reference)

From the repository root in PowerShell:

\`\`\`powershell
py -m pip install -r tools/tournament/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
\`\`\`

## Configure Stockfish

The runner reads the Stockfish executable from the \`STOCKFISH_PATH\` environment variable. This keeps machine-specific paths out of the repository.

For the Stockfish 19 installation used on the development machine:

\`\`\`powershell
$env:STOCKFISH_PATH = "$HOME\Documents\stockfish\stockfish-windows-x86-64-universal.exe"
\`\`\`

If that file is not in that location, set the variable to the actual executable path.

Rascall V4 is discovered automatically in \`build\Release\rascall-v4.exe\`, \`build\rascall-v4.exe\`, or the repository root.

## Add Rascall V1, V2 and V3

The runner accepts additional executables without requiring them to be copied into this repository. Pass each one as a \`NAME=PATH\` argument:

\`\`\`powershell
py tools/tournament/run_tournament.py \`
  --engine "Rascall V1=C:\path\to\rascall-v1.exe" \`
  --engine "Rascall V2=C:\path\to\rascall-v2.exe" \`
  --engine "Rascall V3=C:\path\to\rascall-v3.exe"
\`\`\`

Replace those example paths with the actual executable locations. You can also store paths in a local JSON file and pass it with \`--engine-config\`:

\`\`\`json
{
  "Rascall V1": "C:\\path\\to\\rascall-v1.exe",
  "Rascall V2": "C:\\path\\to\\rascall-v2.exe",
  "Rascall V3": "C:\\path\\to\\rascall-v3.exe"
}
\`\`\`

Then run with \`--engine-config .\my-engines.json\`. Do not commit a machine-specific config file.

List the engines the runner can currently find:

\`\`\`powershell
py tools/tournament/run_tournament.py --list-engines
\`\`\`

## First test: small tournament

Start with a small run before launching hundreds of games:

\`\`\`powershell
py tools/tournament/run_tournament.py \`
  --games-per-pair 4 \`
  --tc 60 \`
  --inc 1 \`
  --seed 20261112
\`\`\`

The default tournament settings are 20 games per pairing at 180+5. Each pairing alternates colors. For an even number of games, both engines play the same number of games as White and Black.

## Opening positions

Without an opening PGN, the runner uses a seeded random legal opening sequence (8 plies by default). This gives reproducible variety, but random legal openings are not a substitute for a curated opening suite.

To use positions from a PGN collection:

\`\`\`powershell
py tools/tournament/run_tournament.py --opening-pgn .\openings.pgn --opening-plies 12
\`\`\`

The runner samples positions from the early part of each PGN game, up to the requested ply. Keep the opening collection fixed when comparing engine versions.

## Output

Every run creates timestamped files under \`tournament-results\`:

- \`tournament-*.pgn\`: complete games with move comments containing reported depth, evaluation and principal variation.
- \`results-*.csv\`: wins, draws, losses, points and score percentage per engine.
- \`moves-*.jsonl\`: one record per move, including the pre-move FEN, move, depth, evaluation and PV. This is useful for later position analysis.

The runner disables supported opening-book and pondering options where possible, so the opening positions come from the tournament setup and the games are comparable.

## Important testing notes

- Use the same time control, opening PGN, seed and hardware conditions for comparisons.
- Alternate colors and use enough games; a four-game test is only a smoke test, not a reliable strength estimate.
- Keep a separate set of positions/openings for final validation so tuning does not overfit the test suite.
- Stockfish is a reference opponent, not ground truth for every position. Analyze disagreements and tactical errors rather than blindly copying its moves.
- The move evaluations are the engines' own reported scores. Different engines can use different evaluation scales, so do not compare raw centipawn numbers as if they were calibrated identically.
- The runner does not modify engine source, weights or evaluation parameters.
