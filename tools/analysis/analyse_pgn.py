#!/usr/bin/env python3
"""Analyse recorded Rascall PGNs with a local Stockfish-compatible UCI engine.

Loss is measured by comparing Stockfish's unrestricted root score with a
search restricted to the move actually played. Mate scores remain mate scores;
they are never converted to arbitrary centipawn values.
"""
from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import chess
import chess.engine
import chess.pgn


def score_record(info: dict[str, Any], pov: chess.Color) -> dict[str, int | None] | None:
    """Return mover-relative centipawns or mate distance, never both."""
    raw = info.get("score")
    if raw is None:
        return None
    score = raw.pov(pov)
    mate = score.mate()
    return {"cp": None, "mate": mate} if mate is not None else {"cp": score.score(), "mate": None}


def compare_scores(
    best: dict[str, int | None] | None,
    played: dict[str, int | None] | None,
) -> tuple[int | None, bool]:
    """Return centipawn regret and a separate forced-mate regression flag."""
    if best is None or played is None:
        return None, False
    best_cp, played_cp = best.get("cp"), played.get("cp")
    if best_cp is not None and played_cp is not None:
        return max(0, int(best_cp) - int(played_cp)), False

    best_mate, played_mate = best.get("mate"), played.get("mate")
    regression = (
        best_mate is not None and best_mate > 0
        and (played_mate is None or played_mate <= 0)
    ) or (
        best_mate is None and played_mate is not None and played_mate < 0
    )
    return None, regression


def display_score(score: dict[str, int | None] | None) -> str:
    if score is None:
        return "unavailable"
    if score.get("mate") is not None:
        return f"mate({score['mate']})"
    return f"{score.get('cp')} cp"


def analyse_game(
    game: chess.pgn.Game,
    engine: chess.engine.SimpleEngine,
    limit: chess.engine.Limit,
    threshold: int,
) -> tuple[chess.pgn.Game, list[dict[str, Any]]]:
    annotated = chess.pgn.Game()
    annotated.headers.update(game.headers)
    board = game.board()
    mistakes: list[dict[str, Any]] = []

    for ply, move in enumerate(game.mainline_moves(), start=1):
        if move not in board.legal_moves:
            annotated.headers["AnalysisError"] = f"Illegal move at ply {ply}"
            break

        mover = board.turn
        fen_before = board.fen()
        best_info = engine.analyse(board, limit)
        pv = best_info.get("pv", [])
        best_move = pv[0] if pv else None
        best_san = board.san(best_move) if best_move in board.legal_moves else (
            best_move.uci() if best_move else "unavailable"
        )
        best_score = score_record(best_info, mover)

        # Both evaluations are rooted in the same pre-move position and use
        # the same depth. If the played move is already best, reuse its score.
        played_info = best_info if move == best_move else engine.analyse(
            board, limit, root_moves=[move]
        )
        played_score = score_record(played_info, mover)
        loss_cp, mate_regression = compare_scores(best_score, played_score)

        node = annotated.end().add_variation(move)
        node.comment = (
            f"Stockfish best: {best_san}; best root eval (mover POV): "
            f"{display_score(best_score)}; played-move root eval: "
            f"{display_score(played_score)}; estimated loss: "
            f"{loss_cp if loss_cp is not None else 'not comparable'} cp"
            + ("; forced-mate outcome worsened" if mate_regression else "")
        )

        if (loss_cp is not None and loss_cp >= threshold) or mate_regression:
            mistakes.append({
                "ply": ply,
                "side": "white" if mover else "black",
                "fen_before": fen_before,
                "played": move.uci(),
                "best_move": best_move.uci() if best_move else None,
                "best_move_san": best_san,
                "best_score": best_score,
                "played_score": played_score,
                "loss_cp": loss_cp,
                "mate_regression": mate_regression,
            })
        board.push(move)

    annotated.headers["Annotator"] = "Stockfish post-game analysis"
    return annotated, mistakes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pgn", type=Path, help="PGN file or directory containing PGNs")
    parser.add_argument("--stockfish", type=Path, required=True, help="Path to Stockfish executable")
    parser.add_argument("--depth", type=int, default=12, help="Analysis depth per candidate move")
    parser.add_argument("--mistake-cp", type=int, default=100, help="Report moves losing at least this many centipawns")
    parser.add_argument("--hash", type=int, default=512, help="Stockfish hash size in MB")
    parser.add_argument("--output-dir", type=Path, default=Path("games/analysed"))
    args = parser.parse_args()

    if not args.stockfish.is_file():
        parser.error(f"Stockfish executable not found: {args.stockfish}")
    if args.depth < 1 or args.mistake_cp < 1 or args.hash < 1:
        parser.error("depth, mistake-cp and hash must be positive")

    files = sorted(args.pgn.glob("*.pgn")) if args.pgn.is_dir() else [args.pgn]
    files = [path for path in files if path.is_file()]
    if not files:
        parser.error("No PGN files found")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    report: dict[str, Any] = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "engine": str(args.stockfish),
        "depth": args.depth,
        "mistake_threshold_cp": args.mistake_cp,
        "scoring": "same-root best-vs-played comparison; mate scores separate from cp",
        "games": [],
    }

    with chess.engine.SimpleEngine.popen_uci(str(args.stockfish), timeout=30) as engine:
        if "Hash" in engine.options:
            engine.configure({"Hash": args.hash})

        for source in files:
            with source.open("r", encoding="utf-8", errors="replace") as handle:
                game_index = 0
                while (game := chess.pgn.read_game(handle)) is not None:
                    game_index += 1
                    if game.errors:
                        report["games"].append({
                            "source": str(source), "game": game_index,
                            "error": "PGN parsing errors",
                            "details": [str(error) for error in game.errors],
                        })
                        continue

                    annotated, mistakes = analyse_game(
                        game, engine, chess.engine.Limit(depth=args.depth), args.mistake_cp
                    )
                    output = args.output_dir / f"{source.stem}-{game_index:03d}-analysed.pgn"
                    with output.open("w", encoding="utf-8") as out:
                        annotated.accept(chess.pgn.FileExporter(out))
                        out.write("\n")

                    report["games"].append({
                        "source": str(source), "game": game_index,
                        "white": game.headers.get("White", "?"),
                        "black": game.headers.get("Black", "?"),
                        "result": game.headers.get("Result", "*"),
                        "analysed_pgn": str(output),
                        "mistake_count": len(mistakes), "mistakes": mistakes,
                    })

    report_path = args.output_dir / f"analysis-report-{stamp}.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Analysed {len(report['games'])} game(s).")
    print(f"Report: {report_path}")
    print(f"Annotated PGNs: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
