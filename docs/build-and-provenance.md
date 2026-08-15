# Pool build, provenance, and ownership

## Build products

`twocrypto-cpp` is the first build in the three-repository workflow. Its supported public artifact is the header-only CMake install tree:

```sh
cd /path/to/twocrypto-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix "$PWD/_install"
```

The exported package is discovered with `find_package(twocrypto_pool CONFIG REQUIRED)` and exposes `twocrypto::pool`. C++ consumers use C++17 and Boost headers. The install tree is independent of this source checkout.

Python parity is a separate environment:

```sh
uv sync --frozen --extra test
uv run --frozen --no-sync twocrypto-parity --help
```

Do not make the harness or orchestrator depend on the pool's Python environment. They consume the installed CMake target and their own lockfiles.

The pool's exact parity harness accepts `TWOCRYPTO_POOL_POLICY_PATH` and optionally `TWOCRYPTO_POOL_POLICY_SHA256`. The path must name a regular file. CMake computes SHA-256 over the exact bytes and fails configuration if a supplied digest differs:

```sh
POLICY=/path/to/policy.hpp
DIGEST=$(shasum -a 256 "$POLICY" | cut -d ' ' -f 1)
cmake -S . -B build/policy -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON \
  -DTWOCRYPTO_POOL_POLICY_PATH="$POLICY" \
  -DTWOCRYPTO_POOL_POLICY_SHA256="$DIGEST"
cmake --build build/policy --target benchmark_harness_i --parallel
```

The policy macro is private to the final executable. The public target remains a native/passthrough SDK and does not encode a repository policy. The orchestrator's `policies/` headers are concrete workflow inputs; reference them as an explicit sibling build prerequisite when compiling the harness, not as a pool-owned policy catalog.

## Reference and audit record

`reference/twocrypto-ng` tracks `invariant-change` and is pinned to revision `2457f36093568252601dd92a299f09517d93b3ba` from `https://github.com/curvefi/twocrypto-ng.git`. `reference/REVISION` is the human-readable branch and revision attestation record; retain its value with parity results. A useful audit bundle records:

- pool Git revision and `reference/REVISION`;
- compiler ID/version, CMake build type, uint256 target name, and harness identity;
- policy path, policy ID/ABI, and exact SHA-256 when a policy is selected; and
- the installed prefix consumed by downstream builds.


The repository is private and its source, policy-fixture, and reference-data redistribution and license posture are not inferred by this documentation. Obtain maintainer authorization before sharing source, policy fixtures, or reference material. Never replace an inaccessible pinned reference with an unpinned checkout.

- **Pool:** state machine, policy ABI/safety envelope, exact uint256 adapter, parity package, pinned reference, and policy attestation.
- **Harness:** one simulation loop, feed parsing, metrics/traces, compiled-policy final binary, and evaluator protocol.
- **Orchestrator:** manifests/artifacts, data provenance, concrete profiles, grids, optimization, execution, scoring, replay, and plots.

Dependency direction is one way: harness consumes an installed `twocrypto::pool`; orchestrator invokes the harness evaluator. The pool must not acquire event-walk, optimizer, cluster, or plotting responsibilities.
