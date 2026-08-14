# twocrypto-pool

Standalone C++17 TwoCrypto pool SDK, compiled-policy extension point, and exact Vyper parity verification suite.

The public product is an installable, header-only C++ target `twocrypto::pool` (and alias `twocrypto_pool`). Public headers retain the include spelling `<pools/twocrypto_fx/...>` and namespace `arb::pools::twocrypto_fx`.

Python tools (`twocrypto_parity`) and executable benchmark adapters (`benchmark_harness_{i,d,f,ld}`) are private validation and parity verification utilities.

All commands below run from `twocrypto-pool/`.

## Public C++ SDK

The SDK requires C++17 and Boost.Multiprecision. Linking `twocrypto::pool` does not propagate threads, Boost.JSON, OpenSSL, executable-only compile definitions, or a concrete compiled policy to consumers.

The pool owns its complete value-semantic transaction snapshot:

```cpp
namespace tc = arb::pools::twocrypto_fx;
using Pool = tc::TwoCryptoPool<tc::uint256>;

Pool::MutableSnapshot before = pool.mutable_snapshot();
// speculative transaction
pool.restore_mutable(before);
bool cacheable = pool.quote_cache_safe();
```

`MutableSnapshot` covers every mutable pool, policy, research, and hook-metric field changed by a transaction while excluding immutable configuration. It is allocation-free. `quote_cache_safe()` is true for the native `None` policy and the macro-absent compiled passthrough; selected compiled policies deny caching by default.

Configure and install:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix "$PWD/_install"
```

Consume the install tree without this source checkout:

```cmake
find_package(twocrypto_pool CONFIG REQUIRED)
add_executable(pool_consumer main.cpp)
target_link_libraries(pool_consumer PRIVATE twocrypto::pool)
```

```sh
cmake -S /path/to/consumer -B /tmp/pool-consumer-build \
  -DCMAKE_PREFIX_PATH="$PWD/_install"
cmake --build /tmp/pool-consumer-build
```

The install exports `twocrypto_poolConfig.cmake` and the `twocrypto::pool` target. It does not install private adapters or require a source-relative include path.

## Repository-owned compiled policy extension

`PolicyKind::Compiled` is the executable-private extension point. A final executable may define:

```text
TWOCRYPTO_POLICY_HEADER="/absolute/path/to/policy.hpp"
```

That header defines `CompiledPolicy<T>` in `arb::pools::twocrypto_fx`. It may provide a fee, a price-scale target, state updates, a conservative fee floor, and optional keeper decision methods. The pool retains clamping, step limiting, LP protection, transaction rollback, and actuator authority.

Without a selected header, `pools/twocrypto_fx/policies/compiled_passthrough.hpp` returns zero for fee and target, which delegates to the native pool surfaces.

For pool-owned benchmark targets, configure the header and optional expected digest with:

```sh
cmake -S . -B build/policy \
  -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON \
  -DTWOCRYPTO_POOL_POLICY_PATH="$PWD/fixtures/test_compiled_policy.hpp" \
  -DTWOCRYPTO_POOL_POLICY_SHA256=<expected-sha256>
cmake --build build/policy --target benchmark_harness_ld
```

CMake resolves a regular file, hashes its exact bytes, rejects a supplied digest mismatch, and makes configuration depend on the file. The macro and identity fields are PRIVATE properties of final executables and never become an INTERFACE property of `twocrypto_pool`.

## Python parity package

The installed package is `twocrypto_parity`. It contains private parity and diagnostic helpers:

- `twocrypto_parity.vyper_pool_runner` executes the pinned Boa reference;
- `twocrypto_parity.cpp_pool_runner` runs already-built typed adapters;
- `twocrypto_parity.numeric_variants` compares integer and floating diagnostics;
- `twocrypto_parity.math_cases` generates deterministic exact math cases;
- `twocrypto_parity.generate_data` generates deterministic pool/action inputs;
- `twocrypto_parity.compare` compares C++ and Boa snapshots, including failures.

Vyper helpers are package resources, so installed tests do not depend on an unpackaged source directory.

```sh
uv sync --frozen --extra test
uv run --frozen --no-sync twocrypto-parity --version
```

Supported Python is 3.12. The lock pins Vyper 0.4.3, titanoboa 0.2.8, snekmate 0.1.2, and pytest 8.4.1.

## Pinned Vyper reference

The reference Vyper contract submodule is located at `reference/twocrypto-ng` and pinned to revision `2c645ca604a4a0878e08f2f1581e5c4ae1c8f8d4` from `https://github.com/curvefi/twocrypto-ng.git`.

`reference/REVISION` records the canonical upstream pin.

## Deterministic fixtures

Committed inputs under `fixtures/` are self-contained JSON:

- `pools.json`: seeded pool configurations;
- `sequences.json`: seeded time travel, exchanges, ordinary additions, donations, and removals;
- `math_cases.json`: seeded exact math cases;
- `failed_action.json`: failed-action atomicity; and
- `test_compiled_policy.hpp`: minimal deterministic compiled policy fixture.

Generate supplementary fixtures into a caller-owned directory:

```sh
uv run --frozen --no-sync python -m twocrypto_parity.generate_data \
  --seed 7 --pools 4 --trades 64 --start-ts 1700000000 \
  --out /tmp/twocrypto-fixtures
```

## Private parity targets

Enable only the adapters needed for validation:

```sh
cmake -S . -B build/parity \
  -DCMAKE_BUILD_TYPE=Release \
  -DTWOCRYPTO_POOL_BUILD_BENCHMARKS=ON \
  -DTWOCRYPTO_POOL_BUILD_MATH_ADAPTER=ON
cmake --build build/parity --target \
  benchmark_harness_i benchmark_harness_d \
  benchmark_harness_f benchmark_harness_ld stableswap_math_i
```

`benchmark_harness_i` is the exact integer adapter. The `d`, `f`, and `ld` adapters are diagnostics only. The math adapter is emitted under `build/parity/lib/` with the platform suffix.

## Tests

Build the standalone C++ snapshot/config tests:

```sh
cmake -S . -B /tmp/twocrypto-pool-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/twocrypto-pool-build -j
ctest --test-dir /tmp/twocrypto-pool-build --output-on-failure
```

Run the self-contained Python suite:

```sh
uv run --frozen --no-sync pytest -q tests
```
