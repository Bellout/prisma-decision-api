# Vendored: dtcount (eqx_prisma `scripts/dtcount`, commit 84dc474)

A copy, not the source. The endpoint-count procedure lives in eqx_prisma
`scripts/dtcount/` (feature F019) and is copied here by its `vendor.sh`.
Do not edit these files here: change them there and re-vendor.

- `problem.py`, `adapter.py`, `runner.py`: the problem model, the
  adapter from this API's compiled influence diagram, and the runner.
- `cpp/`: the C++ sources, compiled into the FastAPI image
  (`fastApi.Dockerfile`) as `/usr/local/bin/dtcount`.
