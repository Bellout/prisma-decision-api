"""Run the dtcount binary on a problem and return its JSON answer.

The binary is located through ``DTCOUNT_BIN``, else ``build/dtcount`` next
to this package. A hard timeout kills the process if it outlives its own
deadline (the third of the three layers of the 3-second rule).
"""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

from .problem import Problem

DEFAULT_BINARY = Path(__file__).resolve().parents[2] / "build" / "dtcount"


def binary() -> Path:
    path = Path(os.environ.get("DTCOUNT_BIN", DEFAULT_BINARY))
    if not path.is_file():
        raise FileNotFoundError(f"dtcount binary not found at {path}; build it (README.md)")
    return path


def run(
    problem: Problem | str,
    algo: str | None = None,
    budget_ms: float | None = None,
    max_cost: float | None = None,
    precheck: bool = True,
    kill_after_s: float = 3.5,
) -> dict:
    text = problem if isinstance(problem, str) else problem.to_text()
    argv = [str(binary())]
    if algo:
        argv += ["--algo", algo]
    if budget_ms is not None:
        argv += ["--budget-ms", str(budget_ms)]
    if max_cost is not None:
        argv += ["--max-cost", str(max_cost)]
    if not precheck:
        argv.append("--no-precheck")
    argv.append("-")
    try:
        proc = subprocess.run(argv, input=text, capture_output=True, text=True, timeout=kill_after_s)
    except subprocess.TimeoutExpired:
        return {"status": "timeout", "ep": None, "killed": True}
    answer = json.loads(proc.stdout)
    if proc.returncode != 0:
        raise ValueError(answer.get("error", proc.stderr.strip()))
    return answer
