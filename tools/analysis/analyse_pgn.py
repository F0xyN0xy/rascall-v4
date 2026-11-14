#!/usr/bin/env python3
"""Analyse recorded Rascall PGNs with a local Stockfish-compatible UCI engine."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import chess
import chess.engine
import chess.pgn


def score_cp(info: dict[str, Any], pov: chess.Color) -> int | None:
    score = info.get("score")
    if score is None:
        return None
    return score.pov(pov).score(mate_score=100000)


def analyse_game(
    game: chess.pgn.Game,
    engine: chess.engine.SimpleEngine,
    limit: chess.engine.Limit,
    threshold: int,
) -> tuple[chess.pgn.Game, list[dict[str, Any]]]:
    annotated = chess.pgn.Game()
    annotated.headers.update(game.headers)
    source_board = game.board()
    out_board = annotated.board()
    mistakes: list[dict[str, Any]] = []

    for ply, move in enumerate(game.mainline_moves(), start=1):
        if move not in source_board.legal_moves:
            annotated.headers["AnalysisError"] = f"Illegal move at ply {ply}"
            break

        mover = source_board.turn
        before = source_board.fen()
        before_info = engine.analyse(source_board, limit)
        best_pv = before_info.get("pv", [])
        best_move = best_pv[0] if best_pv else None
        best_text = source_board.san(best_move) if best_move and best_move in source_board.legal_moves else (
            best_move.uci() if best_move else "unavailable"
        )
        before_score = score_cp(before_info, mover)

        source_board.push(move)
        after_info = engine.analyse(source_board, limit)
        after_score = score_cp(after_info, mover)
        loss = (
            max(0, before_score - after_score)
            if before_score is not None and after_score is not None
            else None
        )

        node = annotated.end().add_variation(move)
        node.comment = (
            f"Stockfish best before move: {best_text}; "
            f"eval after move (mover POV): {after_score}; "
            f"estimated loss: {loss if loss is not None else 'unknown'} cp"
        )

        if loss is not None and loss >= threshold:
            mistakes.append({
                "ply": ply,
                "side": "white" if mover else "black",
                "fen_before": before,
                "played": move.uci(),
                "best_move": best_move.uci() if best_move else None,
                "best_move_san": best_text,
                "eval_before_cp": before_score,
                "eval_after_cp": after_score,
                "loss_cp": loss,
            })
        out_board.push(move)

    annotated.headers["Annotator"] = "Stockfish post-game analysis"
    return annotated, mistakes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pgn", type=Path, help="PGN file or directory containing PGNs")
    parser.add_argument("--stockfish", type=Path, required=True, help="Path to Stockfish executable")
    parser.add_argument("--depth", type=int, default=12, help="Analysis depth per position")
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
        "games": [],
    }

    with chess.engine.SimpleEngine.popen_uci(str(args.stockfish), timeout=30) as engine:
        supported = {}
        if "Hash" in engine.options:
            supported["Hash"] = args.hash
        if supported:
            engine.configure(supported)

        for source in files:
            with source.open("r", encoding="utf-8", errors="replace") as handle:
                game_index = 0
                while (game := chess.pgn.read_game(handle)) is not None:
                    game_index += 1
                    if game.errors:
                        report["games"].append({
                            "source": str(source), "game": game_index,
                            "error": "PGN parsing errors", "details": [str(e) for e in game.errors],
                        })
                        continue
                    annotated, mistakes = analyse_game(
                        game, engine, chess.engine.Limit(depth=args.depth), args.mistake_cp
                    )
                    output = args.output_dir / f"{source.stem}-{game_index:03d}-analysed.pgn"
                    with output.open("w", encoding="utf-8") as out:
                        exporter = chess.pgn.FileExporter(out)
                        annotated.accept(exporter)
                        out.write("\n")
                    report["games"].append({
                        "source": str(source),
                        "game": game_index,
                        "white": game.headers.get("White", "?"),
                        "black": game.headers.get("Black", "?"),
                        "result": game.headers.get("Result", "*"),
                        "analysed_pgn": str(output),
                        "mistake_count": len(mistakes),
                        "mistakes": mistakes,
                    })

    report_path = args.output_dir / f"analysis-report-{stamp}.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Analysed {len(report['games'])} game(s).")
    print(f"Report: {report_path}")
    print(f"Annotated PGNs: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
