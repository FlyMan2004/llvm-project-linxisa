#!/usr/bin/env python3
"""Compare Linx static/dynamic footprint between baseline and candidate builds.

This utility reports per-binary and geomean deltas for:
  1) static .text size (from llvm-size -A)
  2) optional retired-instruction counts (from an emulator command template)
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


@dataclass
class BinaryMetrics:
    relpath: str
    baseline_text: int
    candidate_text: int
    baseline_retired: Optional[int]
    candidate_retired: Optional[int]

    @property
    def text_delta_bytes(self) -> int:
        return self.candidate_text - self.baseline_text

    @property
    def text_delta_pct(self) -> float:
        return pct_delta(self.baseline_text, self.candidate_text)

    @property
    def retired_delta(self) -> Optional[int]:
        if self.baseline_retired is None or self.candidate_retired is None:
            return None
        return self.candidate_retired - self.baseline_retired

    @property
    def retired_delta_pct(self) -> Optional[float]:
        if self.baseline_retired is None or self.candidate_retired is None:
            return None
        return pct_delta(self.baseline_retired, self.candidate_retired)


def pct_delta(base: int, new: int) -> float:
    if base == 0:
        return 0.0 if new == 0 else float("inf")
    return (float(new - base) / float(base)) * 100.0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compare Linx .text and retired-instruction footprints."
    )
    p.add_argument("--baseline-dir", required=True, help="Baseline binaries root")
    p.add_argument("--candidate-dir", required=True, help="Candidate binaries root")
    p.add_argument(
        "--binary-list",
        help="File containing relative binary paths (one per line, '#' comments allowed)",
    )
    p.add_argument(
        "--glob",
        action="append",
        dest="globs",
        default=[],
        help="Relative glob to select binaries when --binary-list is omitted (repeatable)",
    )
    p.add_argument(
        "--llvm-size",
        default="llvm-size",
        help="Path to llvm-size (default: %(default)s)",
    )
    p.add_argument(
        "--size-section",
        default=".text",
        help="Section name to compare (default: %(default)s)",
    )
    p.add_argument(
        "--emulator-cmd-template",
        help=(
            "Optional shell command template for dynamic metric. Use '{bin}' as "
            "placeholder for the binary path."
        ),
    )
    p.add_argument(
        "--retired-regex",
        default=r"(?i)retired(?:[_ ]instructions?)?\s*[:=]\s*([0-9]+)",
        help="Regex with one integer capture group for retired count",
    )
    p.add_argument(
        "--json-out",
        help="Write full report JSON to this path",
    )
    p.add_argument(
        "--csv-out",
        help="Write per-binary CSV to this path",
    )
    p.add_argument(
        "--target-text-reduction-pct",
        type=float,
        default=None,
        help="Fail if geomean .text reduction is below this threshold",
    )
    p.add_argument(
        "--max-retired-regression-pct",
        type=float,
        default=None,
        help="Fail if geomean retired regression exceeds this threshold",
    )
    p.add_argument(
        "--verbose",
        action="store_true",
        help="Print each measured command",
    )
    return p.parse_args(argv)


def run(cmd: Sequence[str], verbose: bool = False) -> str:
    if verbose:
        print("+", " ".join(shlex.quote(tok) for tok in cmd), file=sys.stderr)
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc.stdout


def run_shell(cmd: str, verbose: bool = False) -> str:
    if verbose:
        print("+", cmd, file=sys.stderr)
    proc = subprocess.run(
        cmd,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {cmd}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc.stdout + "\n" + proc.stderr


def discover_binaries(
    baseline_dir: Path, candidate_dir: Path, list_file: Optional[Path], globs: List[str]
) -> List[str]:
    if list_file:
        relpaths: List[str] = []
        for line in list_file.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            relpaths.append(line)
        return sorted(set(relpaths))

    patterns = globs or ["**/*"]
    relset = set()
    for pattern in patterns:
        for bp in baseline_dir.glob(pattern):
            if not bp.is_file():
                continue
            rel = bp.relative_to(baseline_dir).as_posix()
            cp = candidate_dir / rel
            if cp.is_file():
                relset.add(rel)
    return sorted(relset)


def read_section_size(llvm_size: str, section: str, binary: Path, verbose: bool) -> int:
    out = run([llvm_size, "-A", str(binary)], verbose=verbose)
    for line in out.splitlines():
        cols = line.split()
        if len(cols) < 2:
            continue
        if cols[0] != section:
            continue
        try:
            return int(cols[1], 10)
        except ValueError as e:
            raise RuntimeError(f"invalid size value for {binary}: {line}") from e
    raise RuntimeError(f"section {section!r} not found in {binary}")


def read_retired_count(
    template: str, regex: re.Pattern[str], binary: Path, verbose: bool
) -> int:
    quoted_bin = shlex.quote(str(binary))
    cmd = template.format(bin=quoted_bin)
    out = run_shell(cmd, verbose=verbose)
    m = regex.search(out)
    if not m:
        raise RuntimeError(
            f"retired count regex did not match command output for {binary}\n"
            f"regex: {regex.pattern}\n"
            f"output:\n{out}"
        )
    return int(m.group(1), 10)


def geomean_ratio(pairs: Sequence[Tuple[int, int]]) -> float:
    if not pairs:
        return 1.0
    logs = []
    for base, cand in pairs:
        if base <= 0 or cand <= 0:
            continue
        logs.append(math.log(float(cand) / float(base)))
    if not logs:
        return 1.0
    return math.exp(sum(logs) / float(len(logs)))


def write_csv(path: Path, rows: Sequence[BinaryMetrics]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "binary",
                "baseline_text",
                "candidate_text",
                "text_delta_bytes",
                "text_delta_pct",
                "baseline_retired",
                "candidate_retired",
                "retired_delta",
                "retired_delta_pct",
            ]
        )
        for r in rows:
            w.writerow(
                [
                    r.relpath,
                    r.baseline_text,
                    r.candidate_text,
                    r.text_delta_bytes,
                    f"{r.text_delta_pct:.4f}",
                    "" if r.baseline_retired is None else r.baseline_retired,
                    "" if r.candidate_retired is None else r.candidate_retired,
                    "" if r.retired_delta is None else r.retired_delta,
                    ""
                    if r.retired_delta_pct is None
                    else f"{r.retired_delta_pct:.4f}",
                ]
            )


def write_json(
    path: Path,
    rows: Sequence[BinaryMetrics],
    summary: Dict[str, float],
    args: argparse.Namespace,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "baseline_dir": str(Path(args.baseline_dir).resolve()),
        "candidate_dir": str(Path(args.candidate_dir).resolve()),
        "section": args.size_section,
        "metrics": [
            {
                "binary": r.relpath,
                "baseline_text": r.baseline_text,
                "candidate_text": r.candidate_text,
                "text_delta_bytes": r.text_delta_bytes,
                "text_delta_pct": r.text_delta_pct,
                "baseline_retired": r.baseline_retired,
                "candidate_retired": r.candidate_retired,
                "retired_delta": r.retired_delta,
                "retired_delta_pct": r.retired_delta_pct,
            }
            for r in rows
        ],
        "summary": summary,
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    baseline_dir = Path(args.baseline_dir).resolve()
    candidate_dir = Path(args.candidate_dir).resolve()
    list_file = Path(args.binary_list).resolve() if args.binary_list else None
    retired_re = re.compile(args.retired_regex)

    relpaths = discover_binaries(baseline_dir, candidate_dir, list_file, args.globs)
    if not relpaths:
        print("no matching binaries found", file=sys.stderr)
        return 2

    rows: List[BinaryMetrics] = []
    for rel in relpaths:
        bp = baseline_dir / rel
        cp = candidate_dir / rel
        if not bp.is_file() or not cp.is_file():
            raise RuntimeError(f"missing baseline/candidate pair for: {rel}")

        b_text = read_section_size(args.llvm_size, args.size_section, bp, args.verbose)
        c_text = read_section_size(args.llvm_size, args.size_section, cp, args.verbose)

        b_ret = None
        c_ret = None
        if args.emulator_cmd_template:
            b_ret = read_retired_count(
                args.emulator_cmd_template, retired_re, bp, args.verbose
            )
            c_ret = read_retired_count(
                args.emulator_cmd_template, retired_re, cp, args.verbose
            )

        rows.append(
            BinaryMetrics(
                relpath=rel,
                baseline_text=b_text,
                candidate_text=c_text,
                baseline_retired=b_ret,
                candidate_retired=c_ret,
            )
        )

    text_pairs = [(r.baseline_text, r.candidate_text) for r in rows]
    retired_pairs = [
        (r.baseline_retired, r.candidate_retired)
        for r in rows
        if r.baseline_retired is not None and r.candidate_retired is not None
    ]
    retired_pairs_int = [(int(b), int(c)) for b, c in retired_pairs]

    text_ratio = geomean_ratio(text_pairs)
    text_reduction_pct = (1.0 - text_ratio) * 100.0

    retired_ratio = geomean_ratio(retired_pairs_int) if retired_pairs_int else 1.0
    retired_regression_pct = (retired_ratio - 1.0) * 100.0

    summary = {
        "binaries": len(rows),
        "text_geomean_ratio": text_ratio,
        "text_geomean_reduction_pct": text_reduction_pct,
        "retired_geomean_ratio": retired_ratio,
        "retired_geomean_regression_pct": retired_regression_pct,
    }

    text_change_pct = (text_ratio - 1.0) * 100.0
    print(f"Compared {len(rows)} binaries")
    print(f".text geomean ratio: {text_ratio:.6f} ({text_change_pct:+.3f}% change)")
    if retired_pairs_int:
        print(
            "retired geomean ratio: "
            f"{retired_ratio:.6f} ({retired_regression_pct:+.3f}% regression)"
        )
    else:
        print("retired geomean ratio: n/a (no emulator command template provided)")

    if args.csv_out:
        write_csv(Path(args.csv_out), rows)
        print(f"Wrote CSV: {args.csv_out}")
    if args.json_out:
        write_json(Path(args.json_out), rows, summary, args)
        print(f"Wrote JSON: {args.json_out}")

    exit_code = 0
    if args.target_text_reduction_pct is not None:
        if text_reduction_pct < args.target_text_reduction_pct:
            print(
                "ERROR: text reduction target not met: "
                f"got {text_reduction_pct:.3f}% < target {args.target_text_reduction_pct:.3f}%",
                file=sys.stderr,
            )
            exit_code = 1

    if args.max_retired_regression_pct is not None and retired_pairs_int:
        if retired_regression_pct > args.max_retired_regression_pct:
            print(
                "ERROR: retired regression threshold exceeded: "
                f"got {retired_regression_pct:.3f}% > max {args.max_retired_regression_pct:.3f}%",
                file=sys.stderr,
            )
            exit_code = 1

    return exit_code


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
