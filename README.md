# twocrypto-cpp

Standalone C++17 TwoCrypto pool checkout, exact-integer adapter, and pinned Vyper parity fixtures. This repository owns the pool state machine and its policy ABI/safety envelope. It does **not** own event feeds, market scenarios, evaluator execution, optimization, plotting, or concrete experimental policy profiles; those belong to `curve-fx-arb-harness` and `curve-fx-optimization`.

The checkout provides the header-only CMake target `twocrypto::pool` (alias `twocrypto_pool`). The `twocrypto_parity` environment and exact harness are private validation utilities used by the sibling checkouts.

## Repository split

- [`twocrypto-cpp`](https://github.com/curvefi/twocrypto-cpp) — C++ Twocrypto pool implementation and Vyper parity; no market simulation or experiment orchestration.
- [`fx-arb-harness`](https://github.com/curvefi/fx-arb-harness) — C++ arbitrage simulation and evaluator protocol; owns market-event execution and raw metrics.
- [`fx-optimization`](https://github.com/curvefi/fx-optimization) — cluster orchestration, parameter grids, scoring, result storage, robustness analysis, heatmaps, and replay.

## Checkout-local setup

Requirements: Python 3.12, [uv](https://docs.astral.sh/uv/), CMake 3.14+, a C++17 compiler, and Boost 1.79+ headers. Run Python and CMake setup independently; Python is only needed for parity utilities.

```sh
cd /path/to/twocrypto-cpp
uv sync --frozen --extra test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The standalone CMake build enables pool-local tests by default. The `cmake --install` step below writes a disposable checkout-local prefix for the sibling harness; it is not a system installation or a published SDK. Policy implementations remain private parity fixtures and are not installed. Without a selected compiled policy, the staged target exposes only the native pool.

```sh
cmake --install build --prefix "$PWD/_install"
```

An installed consumer needs only Boost and the exported pool target:

```cmake
find_package(twocrypto_pool CONFIG REQUIRED)
add_executable(pool_consumer main.cpp)
target_link_libraries(pool_consumer PRIVATE twocrypto::pool)
```

Use `-DCMAKE_PREFIX_PATH=/path/to/twocrypto-cpp/_install` when configuring the
consumer. The package config contains no checkout revision, dirty-state, or
concrete-policy metadata and has no source-relative dependencies.

## C++ API and transaction snapshots

The pool owns a value-semantic mutable transaction snapshot:

```cpp
namespace tc = arb::pools::twocrypto_fx;
using Pool = tc::TwoCryptoPool<tc::uint256>;

Pool::MutableSnapshot before = pool.mutable_snapshot();
// speculative transaction
pool.restore_mutable(before);
bool native_fee = pool.uses_native_fee_model();
```

`MutableSnapshot` covers mutable pool and compiled-policy state changed by a transaction while excluding immutable configuration. It is allocation-free. `uses_native_fee_model()` is true for native pools and for compiled policies that explicitly declare zero-return native-fee fallback.

## Private compiled-policy extension

`PolicyKind::Compiled` is a final-executable extension point. The harness (or
another private executable) may define `TWOCRYPTO_POLICY_HEADER` to a regular
header that provides the source contract's `ChallengeFeePolicy<T>` in
`arb::pools::twocrypto_fx`. The pool retains clamping, step limiting, LP
protection, rollback, and actuator authority. Without a selected header,
`compiled` is unavailable; use `PolicyKind::None` for the native pool.
Concrete policy selection is intentionally owned by that executable, not by
the installed pool package. The pool checkout exposes
`TWOCRYPTO_PARITY_POLICY_PATH` only for private parity test/benchmark targets;
that value is never exported with `twocrypto::pool`.

The checked-in `include/pools/twocrypto_fx/policies/yieldbasis.hpp` is the exact
`uint256` translation of the pinned `YBTwocryptoPolicy.vy`. Build the private
policy evaluator and pool harness, then run the 59 upstream policy cases, exact
native uint parity, and the pool-integrated state comparison:

```sh
cmake -S . -B build/yb-parity -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_TESTS=ON \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON \
  -DTWOCRYPTO_PARITY_POLICY_PATH="$PWD/include/pools/twocrypto_fx/policies/yieldbasis.hpp"
cmake --build build/yb-parity \
  --target yb_policy_evaluator_i benchmark_harness_i --parallel
TWOCRYPTO_YB_EVALUATOR="$PWD/build/yb-parity/yb_policy_evaluator_i" \
  TWOCRYPTO_HARNESS_I="$PWD/build/yb-parity/benchmark_harness_i" \
  uv run --frozen --no-sync pytest -q -o addopts='' \
  tests/test_yb_policy_parity.py \
  tests/test_boa_parity_fxswap_ext_fee.py
```

The installed pool package does not select or hash a concrete policy. Policy
macros and any evaluator identity fields remain private to parity executables.

## Python parity

Sync the pinned Python environment with `uv sync --frozen --extra test`. The lockfile pins Vyper 0.4.3, titanoboa 0.2.8, snekmate 0.1.2, and pytest 8.4.1.

The checkout exposes one exact parity route through `twocrypto_parity.cpp_pool_runner`; it invokes an already-built `benchmark_harness_i` and never configures or writes into the source tree. The Boa adapter is used by the authority test to compare the same deterministic action sequence against the pinned reference.
The action domain has one persistent LP caller. Per-account sender/receiver
fields fail closed; snapshots compare that caller's LP balance as well as pool
state.

```sh
cmake -S . -B build/parity -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON
cmake --build build/parity --target benchmark_harness_i --parallel
TWOCRYPTO_BUILD_ROOT="$PWD/build/parity" \
  uv run --frozen --no-sync twocrypto-parity \
  /path/to/pools.json /path/to/sequences.json --out /tmp/cpp.json
```

`benchmark_harness_i` emits exact decimal state snapshots. Authority tests
create all pool and action inputs in temporary directories.

## Provenance, audit, and data posture

The Vyper authority is the `reference/twocrypto-ng` Git submodule. Its tracked
gitlink commit is the revision record; `.gitmodules` records the upstream URL
and `invariant-change` branch. Build provenance belongs to the final evaluator
that selects and compiles the pool policy; this pool package does not emit
placeholder revision or compiler identities.

This private repository does not grant redistribution rights for its source or pinned reference. Obtain maintainer authorization before sharing either; never substitute an unpinned checkout for an unavailable reference.

The pool boundary is deliberately narrow: build this checkout first; the harness consumes its staged target, and the orchestrator supplies concrete profiles and all workflows. Do not use historical checkout names or source-relative include paths as runtime dependencies.
