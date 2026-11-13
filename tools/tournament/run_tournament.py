#!/usr/bin/env python3
"""Round-robin UCI tournament runner for Rascall and other UCI engines."""

from __future__ import annotations

import argparse
import csv
import json
import os
import random
import sys
import time
from collections import defaultdict
from collections.abc import Mapping
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    import chess
    import chess.engine
    import chess.pgn
except ImportError:
    raise SystemExit(
        "Missing dependency. Install it with: "
        "py -m pip install -r tools/tournament/requirements.txt"
    )

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_STOCKFISH = Path(os.environ["STOCKFISH_PATH"]).expanduser() if os.environ.get("STOCKFISH_PATH") else None
DEFAULT_V4_CANDIDATES = (
    ROOT / "build" / "Release" / "rascall-v4.exe",
    ROOT / "build" / "rascall-v4.exe",
    ROOT / "rascall-v4.exe",
)


def discover_engines() -> dict[str, Path]:
    engines: dict[str, Path] = {}
    for candidate in DEFAULT_V4_CANDIDATES:
        if candidate.is_file():
            engines["Rascall V4"] = candidate
            break
    if DEFAULT_STOCKFISH and DEFAULT_STOCKFISH.is_file():
        engines["Stockfish 19"] = DEFAULT_STOCKFISH
    return engines


def parse_engine_argument(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("Engine must use NAME=PATH format.")
    name, raw_path = value.split("=", 1)
    if not name.strip() or not raw_path.strip():
        raise argparse.ArgumentTypeError("Both engine name and path are required.")
    return name.strip(), Path(raw_path.strip()).expanduser()


def load_engine_config(path: Path) -> dict[str, Path]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"Could not read engine config {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit("Engine config must be a JSON object mapping names to paths.")
    result: dict[str, Path] = {}
    for name, raw_path in data.items():
        if not isinstance(name, str) or not isinstance(raw_path, str):
            raise SystemExit("Every engine config entry must be a string name and path.")
        if raw_path.strip():
            result[name] = Path(raw_path).expanduser()
    return result


def load_opening_from_pgn(path: Path, rng: random.Random, max_plies: int) -> chess.Board:
    games: list[chess.pgn.Game] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        while (game := chess.pgn.read_game(handle)) is not None:
            if game.errors:
                continue
            board = game.board()
            positions = [board.copy()]
            for move in game.mainline_moves():
                if move not in board.legal_moves:
                    break
                board.push(move)
                positions.append(board.copy())
                if board.is_game_over():
                    break
            usable = positions[: min(len(positions), max_plies + 1)]
            usable = [pos for pos in usable if not pos.is_game_over()]
            if usable:
                games.append(chess.pgn.Game.from_board(rng.choice(usable)))
    if not games:
        raise SystemExit(f"No usable opening positions found in {path}.")
    return rng.choice(games).board()


def random_opening(rng: random.Random, plies: int) -> chess.Board:
    board = chess.Board()
    for _ in range(plies):
        if board.is_game_over():
            break
        moves = list(board.legal_moves)
        if not moves:
            break
        board.push(rng.choice(moves))
    return board


def make_opening(args: argparse.Namespace, rng: random.Random) -> chess.Board:
    if args.opening_pgn:
        return load_opening_from_pgn(args.opening_pgn, rng, args.opening_plies)
    return random_opening(rng, args.opening_plies)


def configure_engine(engine: chess.engine.SimpleEngine, hash_mb: int) -> None:
    supported: dict[str, Any] = {}
    for option, value in (
        ("UseBook", False),
        ("OwnBook", False),
        ("Hash", hash_mb),
    ):
        if option in engine.options:
            supported[option] = value
    if supported:
        engine.configure(supported)


def score_text(info: Mapping[str, Any], pov: chess.Color) -> str:
    score = info.get("score")
    if score is None:
        return "unknown"
    value = score.pov(pov)
    if value.is_mate():
        mate = value.mate()
        return f"mate {mate:+d}" if mate is not None else "mate"
    cp = value.score()
    return f"{cp / 100:+.2f}" if cp is not None else "unknown"


def pv_text(info: Mapping[str, Any]) -> str:
    pv = info.get("pv", [])
    return " ".join(move.uci() for move in pv[:12])


def open_engine(name: str, path: Path, hash_mb: int) -> chess.engine.SimpleEngine:
    if not path.is_file():
        raise SystemExit(f"Engine executable not found: {name}: {path}")
    try:
        resolved = path.resolve()
        working_directory = ROOT if ROOT == resolved or ROOT in resolved.parents else path.parent
        engine = chess.engine.SimpleEngine.popen_uci(
            [str(path)], cwd=str(working_directory), timeout=10.0
        )
        configure_engine(engine, hash_mb)
        return engine
    except Exception as exc:
        raise SystemExit(f"Could not start UCI engine {name} ({path}): {exc}") from exc


def play_game(
    white_name: str,
    black_name: str,
    white_engine: chess.engine.SimpleEngine,
    black_engine: chess.engine.SimpleEngine,
    board: chess.Board,
    args: argparse.Namespace,
    game_number: int,
    pgn_handle: Any,
    moves_handle: Any,
) -> str:
    game = chess.pgn.Game.from_board(board)
    game.headers["Event"] = "Rascall Automated UCI Tournament"
    game.headers["Site"] = "Local"
    game.headers["Date"] = datetime.now().strftime("%Y.%m.%d")
    game.headers["Round"] = str(game_number)
    game.headers["White"] = white_name
    game.headers["Black"] = black_name
    game.headers["TimeControl"] = f"{args.tc}+{args.inc}"
    game.headers["OpeningPlies"] = str(len(board.move_stack))
    game.headers["Termination"] = "unterminated"

    engines = {chess.WHITE: white_engine, chess.BLACK: black_engine}
    names = {chess.WHITE: white_name, chess.BLACK: black_name}
    start_time = time.monotonic()
    clocks = {chess.WHITE: args.tc, chess.BLACK: args.tc}
    ply = 0

    while not board.is_game_over(claim_draw=True) and ply < args.max_plies:
        mover = board.turn
        engine = engines[mover]
        before_fen = board.fen()
        try:
            move_started = time.monotonic()
            result = engine.play(
                board,
                chess.engine.Limit(
                    white_clock=clocks[chess.WHITE],
                    black_clock=clocks[chess.BLACK],
                    white_inc=args.inc,
                    black_inc=args.inc,
                ),
                info=chess.engine.INFO_ALL,
            )
            elapsed = time.monotonic() - move_started
            clocks[mover] = max(0.0, clocks[mover] - elapsed + args.inc)
        except Exception as exc:
            raise RuntimeError(
                f"{names[mover]} failed on ply {ply + 1} ({before_fen}): {exc}"
            ) from exc

        if result.move is None or result.move not in board.legal_moves:
            raise RuntimeError(f"{names[mover]} returned no legal move on ply {ply + 1}.")

        san = board.san(result.move)
        info = result.info or {}
        board.push(result.move)
        node = game.end().add_variation(result.move)
        depth = info.get("depth", "")
        score = score_text(info, mover)
        pv = pv_text(info)
        node.comment = f"depth {depth} | eval {score} | pv {pv}".strip()
        moves_handle.write(
            json.dumps(
                {
                    "game": game_number,
                    "white": white_name,
                    "black": black_name,
                    "ply": ply + 1,
                    "side": "white" if mover else "black",
                    "engine": names[mover],
                    "fen": before_fen,
                    "move": result.move.uci(),
                    "san": san,
                    "depth": depth,
                    "score_from_mover_pov": score,
                    "pv": pv,
                },
                ensure_ascii=False,
            )
            + "\n"
        )
        moves_handle.flush()
        ply += 1

    if board.is_game_over(claim_draw=True):
        game.headers["Result"] = board.result(claim_draw=True)
        outcome = board.outcome(claim_draw=True)
        game.headers["Termination"] = outcome.termination.name if outcome else "unknown"
    else:
        game.headers["Result"] = "1/2-1/2"
        game.headers["Termination"] = f"move-limit-{args.max_plies}"

    game.headers["PlyCount"] = str(ply)
    game.headers["GameDuration"] = f"{time.monotonic() - start_time:.1f}s"
    exporter = chess.pgn.FileExporter(pgn_handle)
    game.accept(exporter)
    pgn_handle.write("\n")
    pgn_handle.flush()
    moves_handle.flush()
    return game.headers["Result"]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run reproducible round-robin matches between UCI chess engines."
    )
    parser.add_argument("--engine-config", type=Path, help="JSON object of engine names to executable paths.")
    parser.add_argument("--engine", action="append", type=parse_engine_argument, default=[], metavar="NAME=PATH")
    parser.add_argument("--games-per-pair", type=int, default=20, help="Games per matchup; even values balance colors.")
    parser.add_argument("--tc", type=float, default=180.0, help="Base clock in seconds (default: 180).")
    parser.add_argument("--inc", type=float, default=5.0, help="Increment in seconds (default: 5).")
    parser.add_argument("--hash", type=int, default=128, dest="hash_mb", help="Hash size in MB when supported.")
    parser.add_argument("--max-plies", type=int, default=300, help="Stop exceptionally long games after this many plies.")
    parser.add_argument("--opening-plies", type=int, default=8, help="Random legal opening plies when no PGN is supplied.")
    parser.add_argument("--opening-pgn", type=Path, help="PGN file used as a source of opening positions.")
    parser.add_argument("--seed", type=int, default=20261112, help="Random seed for reproducibility.")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tournament-results")
    parser.add_argument("--list-engines", action="store_true", help="List engines found from defaults/config and exit.")
    args = parser.parse_args()

    if args.games_per_pair < 1 or args.tc <= 0 or args.inc < 0:
        parser.error("games-per-pair and tc must be positive; inc cannot be negative.")
    if args.hash_mb < 1 or args.max_plies < 1 or args.opening_plies < 0:
        parser.error("hash and max-plies must be positive; opening-plies cannot be negative.")
    if args.opening_pgn and not args.opening_pgn.is_file():
        parser.error(f"Opening PGN not found: {args.opening_pgn}")

    engines = discover_engines()
    if args.engine_config:
        engines.update(load_engine_config(args.engine_config))
    for name, path in args.engine:
        engines[name] = path

    if args.list_engines:
        if not engines:
            print("No engines found. Supply --engine NAME=PATH or --engine-config.")
        for name, path in engines.items():
            print(f"{name}: {path} {'[found]' if path.is_file() else '[missing]'}")
        return 0

    if len(engines) < 2:
        print("Detected engines:")
        if engines:
            for name, path in engines.items():
                print(f"  {name}: {path}")
        else:
            print("  None")
        print("\nRascall V4 was not found in the default build locations, and Stockfish was not found.")
        print("Set STOCKFISH_PATH in this PowerShell session and pass --engine for any executable")
        print("whose location differs from the runner's defaults. Use --list-engines to diagnose.")
        return 2
    missing = [(name, path) for name, path in engines.items() if not path.is_file()]
    if missing:
        for name, path in missing:
            print(f"Missing engine: {name}: {path}", file=sys.stderr)
        return 2

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    pgn_path = args.output_dir / f"tournament-{stamp}.pgn"
    csv_path = args.output_dir / f"results-{stamp}.csv"
    moves_path = args.output_dir / f"moves-{stamp}.jsonl"
    rng = random.Random(args.seed)
    names = list(engines)
    stats: dict[str, dict[str, float]] = defaultdict(lambda: {"wins": 0, "draws": 0, "losses": 0, "points": 0})
    rows: list[dict[str, Any]] = []
    total_games = len(names) * (len(names) - 1) // 2 * args.games_per_pair
    completed = 0

    print(f"Engines: {', '.join(names)}")
    print(f"Games: {total_games} | Time control: {args.tc}+{args.inc}")
    print(f"Seed: {args.seed}")
    print(f"PGN: {pgn_path}")

    try:
        with pgn_path.open("w", encoding="utf-8") as pgn_handle, moves_path.open("w", encoding="utf-8") as moves_handle:
            for left_index in range(len(names)):
                for right_index in range(left_index + 1, len(names)):
                    left_name, right_name = names[left_index], names[right_index]
                    print(f"\nMatch: {left_name} vs {right_name}")
                    left_engine = open_engine(left_name, engines[left_name], args.hash_mb)
                    try:
                        right_engine = open_engine(right_name, engines[right_name], args.hash_mb)
                        try:
                            for game_in_pair in range(args.games_per_pair):
                                left_is_white = game_in_pair % 2 == 0
                                white_name, black_name = (
                                    (left_name, right_name) if left_is_white else (right_name, left_name)
                                )
                                white_engine, black_engine = (
                                    (left_engine, right_engine) if left_is_white else (right_engine, left_engine)
                                )
                                board = make_opening(args, rng)
                                completed += 1
                                result = play_game(
                                    white_name, black_name, white_engine, black_engine,
                                    board, args, completed, pgn_handle, moves_handle
                                )
                                if result == "1-0":
                                    stats[white_name]["wins"] += 1
                                    stats[black_name]["losses"] += 1
                                    stats[white_name]["points"] += 1
                                elif result == "0-1":
                                    stats[black_name]["wins"] += 1
                                    stats[white_name]["losses"] += 1
                                    stats[black_name]["points"] += 1
                                else:
                                    stats[white_name]["draws"] += 1
                                    stats[black_name]["draws"] += 1
                                    stats[white_name]["points"] += 0.5
                                    stats[black_name]["points"] += 0.5
                                rows.append({
                                    "game": completed, "white": white_name, "black": black_name,
                                    "result": result, "termination": "see PGN",
                                })
                                print(f"  Game {completed}/{total_games}: {white_name} - {black_name} {result}")
                        finally:
                            right_engine.quit()
                    finally:
                        left_engine.quit()
    except KeyboardInterrupt:
        print("\nTournament interrupted. Completed games have been saved.")
    except Exception as exc:
        print(f"\nTournament stopped after {completed} completed games: {exc}", file=sys.stderr)

    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["engine", "games", "wins", "draws", "losses", "points", "score_percent"])
        writer.writeheader()
        for name in names:
            item = stats[name]
            games = int(item["wins"] + item["draws"] + item["losses"])
            pct = (item["points"] / games * 100) if games else 0
            writer.writerow({
                "engine": name, "games": games, "wins": int(item["wins"]),
                "draws": int(item["draws"]), "losses": int(item["losses"]),
                "points": item["points"], "score_percent": round(pct, 1),
            })

    print("\nStandings:")
    for name in sorted(names, key=lambda n: stats[n]["points"], reverse=True):
        item = stats[name]
        games = int(item["wins"] + item["draws"] + item["losses"])
        pct = item["points"] / games * 100 if games else 0
        print(f"  {name:20} {item['points']:5.1f}/{games:<3}  W-D-L {int(item['wins'])}-{int(item['draws'])}-{int(item['losses'])}  {pct:5.1f}%")
    print(f"\nSaved PGN:   {pgn_path}")
    print(f"Saved CSV:   {csv_path}")
    print(f"Saved moves: {moves_path}")
    return 0 if completed == total_games else 1


if __name__ == "__main__":
    raise SystemExit(main())
