"""Endpoint count of the decision tree after structural pruning.

Hands the compiled influence diagram to the external dtcount procedure
(vendored in src/vendor/dtcount from eqx_prisma, built into the image as
/usr/local/bin/dtcount) and returns its answer for display. Read-only and
self-contained: nothing else in this service uses it, and it changes no
model, table, solve or tree.
"""

from __future__ import annotations

import asyncio
import uuid
from typing import Any

from src.dtos.discrete_probability_dtos import DiscreteProbabilityOutgoingDto
from src.dtos.discrete_utility_dtos import DiscreteUtilityOutgoingDto
from src.dtos.edge_dtos import EdgeOutgoingDto
from src.dtos.issue_dtos import IssueOutgoingDto
from src.services.structure_service import StructureService
from src.vendor.dtcount.adapter import AdapterError, problem_from_prisma
from src.vendor.dtcount.runner import run


class EndpointCounterUnavailable(Exception):
    """The dtcount binary is missing or did not answer."""


class EndpointCountService:
    def __init__(self, structure_service: StructureService | None = None) -> None:
        self.structure_service = structure_service or StructureService()

    async def count(
        self,
        project_id: uuid.UUID,
        issues: list[IssueOutgoingDto],
        edges: list[EdgeOutgoingDto],
        discrete_probabilities: list[DiscreteProbabilityOutgoingDto],
        discrete_utilities: list[DiscreteUtilityOutgoingDto],
        debug_input: bool = False,
    ) -> dict[str, Any]:
        # The tree's variables are the partial order's, as the views use it.
        partial_order = await self.structure_service.create_partial_order_from_dtos(
            project_id, issues, edges, discrete_probabilities=[], discrete_utilities=[]
        )
        tree_ids = [str(i) for i in (partial_order.issue_ids if partial_order else []) if i is not None]
        model = {
            "issues": [i.model_dump(mode="json") for i in issues],
            "discrete_probabilities": [p.model_dump(mode="json") for p in discrete_probabilities],
            "discrete_utilities": [u.model_dump(mode="json") for u in discrete_utilities],
        }
        try:
            problem, report = problem_from_prisma(model, tree_ids)
        except AdapterError as exc:
            return {"status": "invalid", "reason": str(exc), "ep": None}
        if debug_input:
            return {"problem": problem.to_text(), "adapter": report.as_dict()}
        try:
            answer = await asyncio.to_thread(run, problem)
        except (FileNotFoundError, ValueError, OSError) as exc:
            raise EndpointCounterUnavailable(str(exc)) from exc
        answer["adapter"] = report.as_dict()
        return answer
