"""Run every upstream YBTwocryptoPolicy case against Boa and the C++ port."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
from types import ModuleType
from typing import Any

import boa
import pytest

from twocrypto_parity.paths import build_root


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "reference" / "twocrypto-ng"
UPSTREAM_TEST = REFERENCE / "tests/unitary/policy/test_yb_twocrypto_policy.py"
STATE_FIELDS = ("last_update_ts", "last_prices", "fast_ema", "slow_ema", "price_scale")


def _load_upstream() -> ModuleType:
    spec = importlib.util.spec_from_file_location("_upstream_yb_policy_cases", UPSTREAM_TEST)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {UPSTREAM_TEST}")
    module = importlib.util.module_from_spec(spec)
    previous = Path.cwd()
    try:
        os.chdir(REFERENCE)
        spec.loader.exec_module(module)
    finally:
        os.chdir(previous)
    return module

UPSTREAM = _load_upstream()


def _evaluator_path() -> Path:
    configured = os.environ.get("TWOCRYPTO_YB_EVALUATOR")
    path = (
        Path(configured).expanduser().resolve()
        if configured
        else build_root() / "yb_policy_evaluator_i"
    )
    if not path.is_file():
        pytest.fail(
            "YieldBasis evaluator is missing; build target yb_policy_evaluator_i "
            "or set TWOCRYPTO_YB_EVALUATOR"
        )
    return path


class CppPolicySession:
    def __init__(self, params: tuple[int, ...]) -> None:
        self.process = subprocess.Popen(
            [str(_evaluator_path())],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.init_response = self.request(
            {"op": "init", "params": [str(value) for value in params]},
            require_ok=False,
        )

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def request(self, payload: dict[str, Any], *, require_ok: bool = True) -> dict[str, Any]:
        if self.process.stdin is None or self.process.stdout is None:
            raise RuntimeError("YieldBasis evaluator pipes are unavailable")
        self.process.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            stderr = self.process.stderr.read() if self.process.stderr is not None else ""
            raise RuntimeError(f"YieldBasis evaluator exited: {stderr}")
        response = json.loads(line)
        if require_ok and not response.get("ok"):
            raise AssertionError(f"C++ policy rejected request: {response.get('error')}")
        return response

    @staticmethod
    def _ints(value: Any) -> Any:
        if isinstance(value, list):
            return [CppPolicySession._ints(item) for item in value]
        if isinstance(value, dict):
            return {key: CppPolicySession._ints(item) for key, item in value.items()}
        return int(value) if isinstance(value, str) else value

    def query(self, op: str, **payload: Any) -> Any:
        request = {"op": op, "timestamp": str(boa.env.evm.patch.timestamp)}
        request.update(payload)
        return self._ints(self.request(request)["value"])

    def state(self) -> dict[str, int]:
        return self._ints(self.request({"op": "state"})["state"])

    def update(self, price_scale: int, price_oracle: int, last_prices: int) -> None:
        self.request(
            {
                "op": "update",
                "timestamp": str(boa.env.evm.patch.timestamp),
                "price_scale": str(price_scale),
                "price_oracle": str(price_oracle),
                "last_prices": str(last_prices),
            }
        )


class DualPolicy:
    def __init__(self, vyper_policy: Any, cpp: CppPolicySession, params: tuple[int, ...]) -> None:
        self.vyper = vyper_policy
        self.cpp = cpp
        self.params = params

    def _immutable(self, index: int, name: str) -> int:
        value = int(getattr(self.vyper, name)())
        assert value == self.params[index]
        return value

    def POOL(self) -> Any:
        return self.vyper.POOL()

    def FAST_HALF_LIFE(self) -> int:
        return self._immutable(0, "FAST_HALF_LIFE")

    def SLOW_HALF_LIFE(self) -> int:
        return self._immutable(1, "SLOW_HALF_LIFE")

    def KAPPA(self) -> int:
        return self._immutable(2, "KAPPA")

    def DEADBAND(self) -> int:
        return self._immutable(3, "DEADBAND")

    def MIN_CAP(self) -> int:
        return self._immutable(4, "MIN_CAP")

    def MAX_CAP(self) -> int:
        return self._immutable(5, "MAX_CAP")

    def _assert_state(self, state: Any | None = None) -> Any:
        actual = self.vyper.state() if state is None else state
        expected = self.cpp.state()
        assert {name: int(getattr(actual, name)) for name in STATE_FIELDS} == expected
        return actual

    def state(self) -> Any:
        return self._assert_state()

    def get_emas(self) -> list[int]:
        actual = [int(value) for value in self.vyper.get_emas()]
        assert actual == self.cpp.query("get_emas")
        return actual

    def get_price_scale(self) -> int:
        actual = int(self.vyper.get_price_scale())
        assert actual == self.cpp.query("get_price_scale")
        return actual

    def get_fee(self, xp: list[int]) -> int:
        actual = int(self.vyper.get_fee(xp))
        expected = self.cpp.query("get_fee", xp=[str(value) for value in xp])
        assert actual == expected
        return actual

    def update_pool_state(self, *args: Any, **kwargs: Any) -> Any:
        # Authentication belongs to the Vyper external boundary. The C++ policy
        # is called only by its owning pool and has no address model.
        return self.vyper.update_pool_state(*args, **kwargs)


def _deploy(
    pool: Any,
    fast_half_life: int = 3_600,
    slow_half_life: int = 7_200,
    kappa: int = 2 * UPSTREAM.PRECISION,
    deadband: int = 0,
    min_cap: int = UPSTREAM._bps(1),
    max_cap: int = UPSTREAM._bps(60),
    initialize: bool = True,
) -> DualPolicy:
    params = (fast_half_life, slow_half_life, kappa, deadband, min_cap, max_cap)
    cpp = CppPolicySession(params)
    try:
        policy = UPSTREAM.POLICY_DEPLOYER.deploy(pool.address, *params)
    except Exception:
        cpp_rejected = not cpp.init_response.get("ok")
        cpp.close()
        if not cpp_rejected:
            raise AssertionError("Vyper rejected constructor parameters accepted by C++") from None
        raise
    if not cpp.init_response.get("ok"):
        cpp.close()
        raise AssertionError(
            f"C++ rejected constructor parameters accepted by Vyper: "
            f"{cpp.init_response.get('error')}"
        )
    dual = DualPolicy(policy, cpp, params)
    if initialize:
        _update(
            dual,
            pool,
            pool.price_scale(),
            pool.last_prices(),
            pool.price_oracle(),
        )
    return dual


def _update(
    policy: DualPolicy,
    pool: Any,
    price_scale: int,
    last_prices: int,
    price_oracle: int | None = None,
) -> None:
    if price_oracle is None:
        price_oracle = last_prices
    pool.update_policy(policy.vyper, price_scale, price_oracle, last_prices)
    policy.cpp.update(price_scale, price_oracle, last_prices)
    policy._assert_state()


UPSTREAM._deploy = _deploy
UPSTREAM._update = _update


@pytest.fixture(scope="module")
def math_contract() -> Any:
    return boa.load(str(REFERENCE / "contracts/main/StableswapMath.vy"))


# Retain the upstream parametrization exactly: pytest expands these functions
# to the same 59 cases as the pinned reference suite.
for _name, _value in vars(UPSTREAM).items():
    if _name.startswith("test_") and callable(_value):
        globals()[_name] = _value

pool = UPSTREAM.pool
