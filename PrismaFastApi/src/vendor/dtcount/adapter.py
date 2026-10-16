"""Prisma's compiled influence diagram → a dtcount problem.

The only module that knows a decision tool's model. It reads the JSON the
Prisma API sends its solver service for every structure call (``issues``,
``discrete_probabilities``, ``discrete_utilities``, after the .NET API has
compiled the restriction tables) and the tree's variable set (the partial
order), and produces the 0/1 factors of STUDY.md §3:

- for each uncertainty in the tree, the zero pattern of its probability
  table (an explicit probability of exactly 0 is "cannot occur");
- for each utility table, the combinations whose utility is at or below
  -1e10 (the restriction penalty is -1e100; Prisma's tree view prunes at
  -1e10), and likewise an option's or outcome's own utility.

Restricted uncertainty rows (zeroed probabilities) and total restrictions
(the N/A state, probability 1 or an allowed option only under the
restricting state) arrive already compiled into those tables. A missing
or null probability is *not* treated as a structural zero: it is
reported as a warning and the combination stays possible.

Input is plain JSON (dicts and lists, as serialised by the DTOs); no
Prisma code is imported.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from .problem import Factor, Problem, Variable

PENALTY = -1e10


class AdapterError(ValueError):
    """The model cannot be expressed as a counting problem."""


@dataclass
class Report:
    variables: int = 0
    probability_factors: int = 0
    restriction_factors: int = 0
    warnings: list[str] = field(default_factory=list)

    def as_dict(self) -> dict:
        return {
            "variables": self.variables,
            "probability_factors": self.probability_factors,
            "restriction_factors": self.restriction_factors,
            "warnings": self.warnings,
        }


def _states(issue: dict) -> list[dict]:
    if issue.get("type") == "Decision":
        return list((issue.get("decision") or {}).get("options") or [])
    return list((issue.get("uncertainty") or {}).get("outcomes") or [])


def problem_from_prisma(model: dict, tree_issue_ids: list[str]) -> tuple[Problem, Report]:
    issues = {str(i["id"]): i for i in model.get("issues", [])}
    report = Report()
    problem = Problem()
    state_of: dict[str, tuple[int, int]] = {}  # state id -> (variable, index)

    for issue_id in tree_issue_ids:
        issue = issues.get(str(issue_id))
        if issue is None:
            raise AdapterError(f"tree issue {issue_id} is not in the model")
        kind = {"Decision": "D", "Uncertainty": "U"}.get(issue.get("type"))
        if kind is None:
            continue
        states = _states(issue)
        if not states:
            raise AdapterError(f"{issue.get('type')} {issue.get('name')!r} has no states")
        v = len(problem.variables)
        problem.variables.append(Variable(str(issue["id"]), kind, len(states)))
        for k, s in enumerate(states):
            state_of[str(s["id"])] = (v, k)
            if s.get("utility") is not None and s["utility"] <= PENALTY:
                problem.factors.append(
                    Factor("R", [v], [0 if j == k else 1 for j in range(len(states))])
                )
                report.restriction_factors += 1
    report.variables = len(problem.variables)
    sizes = problem.states

    def key(cell: dict) -> list[tuple[int, int]] | None:
        ids = [str(x) for x in (cell.get("parent_outcome_ids") or [])] + [
            str(x) for x in (cell.get("parent_option_ids") or [])
        ]
        out = []
        for sid in ids:
            if sid not in state_of:
                return None
            out.append(state_of[sid])
        return out

    def mark(scope: list[int], allowed: list[int], fixed: dict[int, int]) -> None:
        # Forbid every entry consistent with `fixed` (scope variables it
        # leaves out range over all their states).
        free = [v for v in scope if v not in fixed]
        def rec(i: int, assignment: dict[int, int]) -> None:
            if i == len(free):
                index = 0
                for v in scope:
                    index = index * sizes[v] + assignment[v]
                allowed[index] = 0
                return
            for s in range(sizes[free[i]]):
                assignment[free[i]] = s
                rec(i + 1, assignment)
        rec(0, dict(fixed))

    # Probability tables: zero patterns.
    for v, var in enumerate(problem.variables):
        if var.kind != "U":
            continue
        issue = issues[var.name]
        unc_id = str((issue.get("uncertainty") or {}).get("id"))
        cells = [c for c in model.get("discrete_probabilities", []) if str(c.get("uncertainty_id")) == unc_id]
        if not cells:
            report.warnings.append(f"uncertainty {issue.get('name')!r} has no probability table")
            continue
        keys = [key(c) for c in cells]
        if any(k is None for k in keys):
            report.warnings.append(
                f"uncertainty {issue.get('name')!r}: a probability refers to a state outside the tree; table skipped"
            )
            continue
        parents = sorted({p for k in keys for p, _ in k})
        scope = parents + [v]
        allowed = [1] * math.prod(sizes[x] for x in scope)
        nulls = 0
        for cell, k in zip(cells, keys):
            outcome = state_of.get(str(cell.get("outcome_id")))
            if outcome is None or outcome[0] != v:
                report.warnings.append(f"uncertainty {issue.get('name')!r}: a cell names an unknown outcome")
                continue
            p = cell.get("probability")
            if p is None or (isinstance(p, float) and math.isnan(p)):
                nulls += 1
                continue
            if p == 0:
                fixed = dict(k)
                fixed[v] = outcome[1]
                mark(scope, allowed, fixed)
        if nulls:
            report.warnings.append(f"uncertainty {issue.get('name')!r}: {nulls} probabilities are empty")
        if 0 in allowed:
            problem.factors.append(Factor("P", scope, allowed))
            report.probability_factors += 1

    # Utility tables: penalties are forbidden combinations.
    by_utility: dict[str, list[dict]] = {}
    for cell in model.get("discrete_utilities", []):
        by_utility.setdefault(str(cell.get("utility_id")), []).append(cell)
    for utility_id, cells in by_utility.items():
        forbidden = [c for c in cells if c.get("utility_value") is not None and c["utility_value"] <= PENALTY]
        if not forbidden:
            continue
        keys = [key(c) for c in forbidden]
        if any(k is None for k in keys):
            report.warnings.append(
                f"utility table {utility_id}: a penalty refers to a state outside the tree; skipped"
            )
            continue
        scope = sorted({p for k in keys for p, _ in k})
        if not scope:
            raise AdapterError(f"utility table {utility_id} forbids everything unconditionally")
        allowed = [1] * math.prod(sizes[x] for x in scope)
        for k in keys:
            mark(scope, allowed, dict(k))
        problem.factors.append(Factor("R", scope, allowed))
        report.restriction_factors += 1

    return problem, report
