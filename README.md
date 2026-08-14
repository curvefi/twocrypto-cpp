# twocrypto-cpp

Standalone C++17 TwoCrypto pool SDK, exact-integer adapter, and pinned Vyper parity package. This repository owns the pool state machine and its policy ABI/safety envelope. It does **not** own event feeds, market scenarios, evaluator execution, optimization, plotting, or concrete experimental policy profiles; those belong to `curve-fx-arb-harness` and `curve-fx-optimization`.

The public product is the installable, header-only CMake target `twocrypto::pool` (alias `twocrypto_pool`). The Python package `twocrypto_parity` and exact harness are private validation utilities.

## Independent setup

Requirements: Python 3.12, [uv](https://docs.astral.sh/uv/), CMake 3.14+, a C++17 compiler, and Boost 1.79+ headers. Run Python and CMake setup independently; the Python environment is not needed by C++ consumers.

```sh
cd /path/to/twocrypto-cpp
uv sync --frozen --extra test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The standalone CMake build enables pool-local tests by default. A consumer should install the SDK into a separate prefix and use only the exported target:

```sh
cmake --install build --prefix "$PWD/_install"
```

```cmake
find_package(twocrypto_pool CONFIG REQUIRED)
add_executable(pool_consumer main.cpp)
target_link_libraries(pool_consumer PRIVATE twocrypto::pool)
```

```sh
cmake -S /path/to/consumer -B /tmp/twocrypto-consumer-build \
  -DCMAKE_PREFIX_PATH=/path/to/twocrypto-cpp/_install
cmake --build /tmp/twocrypto-consumer-build --parallel
```

The install tree contains `twocrypto_poolConfig.cmake`, headers, and the `twocrypto::pool` target. It does not require this source checkout, private adapters, Boost.JSON, threads, OpenSSL, or a concrete compiled policy.

## C++ API and transaction snapshots

The pool owns a value-semantic mutable transaction snapshot:

```cpp
namespace tc = arb::pools::twocrypto_fx;
using Pool = tc::TwoCryptoPool<tc::uint256>;

Pool::MutableSnapshot before = pool.mutable_snapshot();
// speculative transaction
pool.restore_mutable(before);
bool cacheable = pool.quote_cache_safe();
```

`MutableSnapshot` covers mutable pool, policy, research, and hook-metric fields changed by a transaction while excluding immutable configuration. It is allocation-free. `quote_cache_safe()` is true for the native `None` policy and macro-absent compiled passthrough; selected compiled policies deny caching by default.

## Private compiled-policy extension

`PolicyKind::Compiled` is a final-executable extension point. A final executable may define `TWOCRYPTO_POLICY_HEADER` to a regular header that provides the source contract's `ChallengeFeePolicy<T>` in `arb::pools::twocrypto_fx`. The pool retains clamping, step limiting, LP protection, rollback, and actuator authority. Without a selected header, `pools/twocrypto_fx/policies/compiled_passthrough.hpp` delegates to native surfaces.

For the private exact-integer parity harness, configure the committed deterministic policy fixture and its digest:

```sh
cmake -S . -B build/policy -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON \
  -DTWOCRYPTO_POOL_POLICY_PATH="$PWD/fixtures/test_compiled_policy.hpp" \
  -DTWOCRYPTO_POOL_POLICY_SHA256=18fdfe021e6029b4b3f744c07867cdd031024249c268501667882d556121acf8
cmake --build build/policy --target benchmark_harness_i --parallel
```

CMake resolves a regular file, hashes its exact bytes, rejects a supplied digest mismatch, and makes configuration depend on the file. Policy macros and identity fields are private to the final executable and never become an interface property of `twocrypto_pool`. Concrete orchestrator profiles are documented in that repository, not copied here.

## Python parity

Install the pinned Python environment with `uv sync --frozen --extra test`. The lockfile pins Vyper 0.4.3, titanoboa 0.2.8, snekmate 0.1.2, and pytest 8.4.1.

The package exposes one exact parity route through `twocrypto_parity.cpp_pool_runner`; it invokes an already-built `benchmark_harness_i` and never configures or writes into the source tree. The Boa adapter is used by the authority test to compare the same deterministic action sequence against the pinned reference.

```sh
cmake -S . -B build/parity -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON
cmake --build build/parity --target benchmark_harness_i --parallel
TWOCRYPTO_BUILD_ROOT="$PWD/build/parity" \
  uv run --frozen --no-sync twocrypto-parity \
  /path/to/pools.json /path/to/sequences.json --out /tmp/cpp.json
```

The high-value authority test constructs its pool and action inputs in a temporary directory, so no generated fixtures are committed. The only retained fixture is `fixtures/test_compiled_policy.hpp`, used for policy-header attestation.

The exact harness is the sole private adapter:

```sh
cmake -S . -B build/parity -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON
cmake --build build/parity --target benchmark_harness_i --parallel
```

`benchmark_harness_i` is the uint256 adapter and emits exact decimal state snapshots.

## Provenance, audit, and data posture

The Vyper reference is the `reference/twocrypto-ng` Git submodule at revision `2c645ca604a4a0878e08f2f1581e5c4ae1c8f8d4` from `https://github.com/curvefi/twocrypto-ng.git`, recorded in `reference/REVISION`. Keep this submodule revision with any parity report. A report should record the pool revision, compiler/build mode, uint256 harness identity, policy-header digest when applicable, and the explicit input paths used for the run.

This private repository does not grant redistribution rights for source, policy fixtures, or reference data. Do not publish or copy private data without maintainer authorization; treat a missing or unavailable submodule/data checkout as an access/provenance issue rather than substituting another reference.

The pool boundary is deliberately narrow: install this SDK first; the harness consumes the installed target, and the orchestrator supplies concrete profiles and all workflows. Do not use historical checkout names or source-relative include paths as runtime dependencies.
