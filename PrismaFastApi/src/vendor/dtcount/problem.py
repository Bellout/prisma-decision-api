"""The counting problem in Python, and a brute-force counter.

Mirrors the C++ format "dtcount-problem 1" (README.md) so problems can be
generated, adapted and checked from Python. ``brute_force_count`` is an
implementation independent of the C++ code: the differential suite holds
all of them to the same answer.
"""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass, field


@dataclass
class Variable:
    name: str
    kind: str  # "D" or "U"
    states: int


@dataclass
class Factor:
    kind: str  # "P" (zero pattern of a probability table) or "R" (restriction)
    scope: list[int]
    allowed: list[int]  # row-major over scope, last variable fastest

    def entry(self, assignment: tuple[int, ...] | list[int], states: list[int]) -> int:
        index = 0
        for v in self.scope:
            index = index * states[v] + assignment[v]
        return self.allowed[index]


@dataclass
class Problem:
    variables: list[Variable] = field(default_factory=list)
    factors: list[Factor] = field(default_factory=list)

    @property
    def states(self) -> list[int]:
        return [v.states for v in self.variables]

    def symmetric_count(self) -> int:
        return math.prod(self.states)

    def to_text(self) -> str:
        lines = ["dtcount-problem 1"]
        for v in self.variables:
            lines.append(f"var {v.name} {v.kind} {v.states}")
        for f in self.factors:
            scope = " ".join(str(v) for v in f.scope)
            bits = "".join("1" if a else "0" for a in f.allowed)
            lines.append(f"factor {f.kind} {len(f.scope)}{' ' + scope if scope else ''} {bits}")
        return "\n".join(lines) + "\n"

    @classmethod
    def from_text(cls, text: str) -> "Problem":
        problem = cls()
        lines = [ln.split() for ln in text.splitlines()]
        lines = [ln for ln in lines if ln and not ln[0].startswith("#")]
        if not lines or lines[0] != ["dtcount-problem", "1"]:
            raise ValueError("expected the header 'dtcount-problem 1'")
        for words in lines[1:]:
            if words[0] == "var":
                problem.variables.append(Variable(words[1], words[2], int(words[3])))
            elif words[0] == "factor":
                k = int(words[2])
                scope = [int(w) for w in words[3 : 3 + k]]
                problem.factors.append(Factor(words[1], scope, [int(c) for c in words[3 + k]]))
            else:
                raise ValueError(f"unknown line {' '.join(words)!r}")
        return problem

    def permuted(self, order: list[int]) -> "Problem":
        """The same problem with its variables listed in ``order``.

        ``order[i]`` is the old index of the variable placed at position i.
        The count is a property of the problem, not of the order.
        """
        new_index = {old: new for new, old in enumerate(order)}
        out = Problem([self.variables[old] for old in order], [])
        for f in self.factors:
            out.factors.append(Factor(f.kind, [new_index[v] for v in f.scope], list(f.allowed)))
        return out


def brute_force_count(problem: Problem) -> int:
    """Count complete assignments allowed by every factor, one by one."""
    states = problem.states
    count = 0
    for assignment in itertools.product(*(range(s) for s in states)):
        if all(f.entry(assignment, states) for f in problem.factors):
            count += 1
    return count
