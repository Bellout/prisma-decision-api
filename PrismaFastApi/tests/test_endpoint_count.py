"""Endpoint-count service (eqx_prisma F019): adapter + external dtcount binary.

Runs inside the FastAPI image, where the binary is built
(DTCOUNT_BIN=/usr/local/bin/dtcount):

    docker run --rm -v $PWD/PrismaFastApi/tests:/code/tests <fastapi image> \
        sh -c "pip install -q --user pytest && cd /code && python -m pytest -q tests"

The model is a small one in the API's JSON shape, counted by hand:
Test? (yes|no) -> Result (pos|neg|N/A) -> Drill (drill|skip); Result is
totally restricted by "no" (only N/A), drilling is forbidden after "neg"
(-1e100). Endpoints: yes-pos 2 + yes-neg 1 + no-N/A 2 = 5 of 12.
"""

import asyncio
import types
import uuid

import pytest

from src.services.endpoint_count_service import EndpointCountService, EndpointCounterUnavailable


class Dumped:
    def __init__(self, data):
        self.data = data

    def model_dump(self, mode="json"):
        return self.data


def issue(iid, kind, states):
    sub = {"Decision": "decision", "Uncertainty": "uncertainty"}[kind]
    key = {"Decision": "options", "Uncertainty": "outcomes"}[kind]
    return {"id": iid, "name": iid, "type": kind,
            sub: {"id": f"{iid}-{sub}", key: [{"id": s, "name": s, "utility": 0.0} for s in states]}}


def model(options_of_test=("yes", "no")):
    issues = [issue("test", "Decision", list(options_of_test)),
              issue("result", "Uncertainty", ["pos", "neg", "na"]),
              issue("drill", "Decision", ["drill", "skip"])]
    probs = []
    for parent, row in (("yes", {"pos": 0.6, "neg": 0.4, "na": 0.0}), ("no", {"pos": 0.0, "neg": 0.0, "na": 1.0})):
        for outcome, p in row.items():
            probs.append({"uncertainty_id": "result-uncertainty", "outcome_id": outcome, "probability": p,
                          "parent_option_ids": [parent], "parent_outcome_ids": []})
    utils = [{"utility_id": "restricted-0", "utility_value": -1e100 if (o, d) == ("neg", "drill") else 0.0,
              "parent_outcome_ids": [o], "parent_option_ids": [d]}
             for o in ("pos", "neg", "na") for d in ("drill", "skip")]
    return [Dumped(i) for i in issues], [Dumped(p) for p in probs], [Dumped(u) for u in utils]


class FixedOrder:
    async def create_partial_order_from_dtos(self, *args, **kwargs):
        return types.SimpleNamespace(issue_ids=["test", "result", "drill"])


def count(**kw):
    issues, probs, utils = model(**kw.pop("model", {}))
    service = EndpointCountService(FixedOrder())
    return asyncio.run(service.count(uuid.uuid4(), issues, [], probs, utils, **kw))


def test_counts_the_hand_model():
    answer = count()
    assert answer["status"] == "ok" and answer["ep"] == "5" and answer["ep_sym"] == "12"
    assert answer["adapter"]["probability_factors"] == 1 and answer["adapter"]["restriction_factors"] == 1


def test_debug_input_returns_the_problem_instead():
    answer = count(debug_input=True)
    assert answer["problem"].startswith("dtcount-problem 1")


def test_a_model_that_cannot_be_counted_is_reported_as_invalid():
    answer = count(model={"options_of_test": ()})
    assert answer["status"] == "invalid" and answer["ep"] is None


def test_a_missing_binary_is_unavailable(monkeypatch):
    monkeypatch.setenv("DTCOUNT_BIN", "/nonexistent/dtcount")
    with pytest.raises(EndpointCounterUnavailable):
        count()
