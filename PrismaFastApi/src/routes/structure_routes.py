import uuid
import asyncio
from typing import Optional
from fastapi import APIRouter, Depends
from fastapi.responses import JSONResponse
from src.project_lock_manager import ProjectQueueManager
from src.dtos.edge_dtos import EdgeOutgoingDto
from src.dtos.issue_dtos import IssueOutgoingDto
from src.dtos.discrete_probability_dtos import DiscreteProbabilityOutgoingDto
from src.dtos.discrete_utility_dtos import DiscreteUtilityOutgoingDto
from src.services.structure_service import StructureService
from src.services.endpoint_count_service import EndpointCountService, EndpointCounterUnavailable
from src.dtos.discrete_utility_dtos import DiscreteUtilityOutgoingDto
from src.dtos.discrete_probability_dtos import DiscreteProbabilityOutgoingDto
from src.dependencies import (
    get_project_lock_manager,
    get_structure_service,
)
from src.domain.influence_diagram import InfluenceDiagramDOT
from src.dtos.decision_tree_dtos import DecisionTreeDto, PartialOrderDto, TreeNodeDto2


router = APIRouter(tags=["structure"])


@router.post("/structure/{project_id}/influence_diagram")
async def get_influence_diagram_from_dtos(
    issues: list[IssueOutgoingDto],
    edges: list[EdgeOutgoingDto],
) -> tuple[list[IssueOutgoingDto], list[EdgeOutgoingDto]]:
    influence_diagram = await asyncio.to_thread(lambda: InfluenceDiagramDOT(edges, issues))
    await asyncio.to_thread(influence_diagram.validate_diagram)
    return influence_diagram.issues, influence_diagram.edges


@router.post("/structure/{project_id}/partial_order")
async def get_partial_order_from_dtos(
    project_id: uuid.UUID,
    issues: list[IssueOutgoingDto],
    edges: list[EdgeOutgoingDto],
    structure_service: StructureService = Depends(get_structure_service),
) -> Optional[PartialOrderDto]:
    return await structure_service.create_partial_order_from_dtos(project_id, issues, edges, discrete_probabilities=[], discrete_utilities=[])


@router.post("/structure/{project_id}/endpoint_count")
async def get_endpoint_count_from_dtos(
    project_id: uuid.UUID,
    issues: list[IssueOutgoingDto],
    edges: list[EdgeOutgoingDto],
    discrete_probabilities: list[DiscreteProbabilityOutgoingDto] = [],
    discrete_utilities: list[DiscreteUtilityOutgoingDto] = [],
    debug: Optional[str] = None,
    structure_service: StructureService = Depends(get_structure_service),
):
    """Endpoints of the decision tree after structural pruning (eqx_prisma F019).

    200 for every counting outcome (status ok / too_large / timeout / invalid);
    503 when the external counter is unavailable. `debug=input` returns the
    counting problem instead of the count.
    """
    service = EndpointCountService(structure_service)
    try:
        return await service.count(
            project_id, issues, edges, discrete_probabilities, discrete_utilities,
            debug_input=debug == "input",
        )
    except EndpointCounterUnavailable as exc:
        return JSONResponse(status_code=503, content={"message": f"endpoint counter unavailable: {exc}"})


@router.post("/structure/{project_id}/decision_tree/v2")
async def build_decision_tree_from_dtos(
    project_id: uuid.UUID,
    issues: list[IssueOutgoingDto],
    edges: list[EdgeOutgoingDto],
    discrete_probabilities: list[DiscreteProbabilityOutgoingDto] = [],
    discrete_utilities: list[DiscreteUtilityOutgoingDto] = [],
    structure_service: StructureService = Depends(get_structure_service),
) -> Optional[DecisionTreeDto]:
    return await structure_service.create_decision_tree_from_dtos(project_id, issues, edges, discrete_probabilities=discrete_probabilities, discrete_utilities=discrete_utilities)


@router.post("/structure/{project_id}/decision_tree/v3")
async def build_decision_tree_from_dtos_optimal(
    project_id: uuid.UUID,
    issues: list[IssueOutgoingDto],
    edges: list[EdgeOutgoingDto],
    discrete_probabilities: list[DiscreteProbabilityOutgoingDto] = [],
    discrete_utilities: list[DiscreteUtilityOutgoingDto] = [],
    structure_service: StructureService = Depends(get_structure_service),
    lock_manager: ProjectQueueManager = Depends(get_project_lock_manager),
) -> Optional[TreeNodeDto2]:
    async with lock_manager.acquire_project_lock(project_id):
        return structure_service.create_decision_tree_from_dtos_optimal(
            project_id, issues, edges, discrete_probabilities=discrete_probabilities, discrete_utilities=discrete_utilities
        )
        
@router.post("/structure/{project_id}/partial_decision_tree/v3")
async def build_partial_decision_tree_from_dtos_optimal(
    project_id: uuid.UUID, issues: list[IssueOutgoingDto], edges: list[EdgeOutgoingDto],
    discrete_probabilities: list[DiscreteProbabilityOutgoingDto] = [],
    discrete_utilities: list[DiscreteUtilityOutgoingDto] = [],
    paths: list[list[uuid.UUID]] = [],
    structure_service: StructureService = Depends(get_structure_service),
    lock_manager: ProjectQueueManager = Depends(get_project_lock_manager),
) -> Optional[TreeNodeDto2]:
    async with lock_manager.acquire_project_lock(project_id):
        return await structure_service.create_partial_decision_tree_from_dtos_optimal(project_id, issues, edges, discrete_probabilities=discrete_probabilities, discrete_utilities=discrete_utilities, paths=paths)
        