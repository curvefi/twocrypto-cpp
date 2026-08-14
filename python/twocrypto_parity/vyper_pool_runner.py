"""Boa/Titanoboa reference runner for exact TwoCrypto state snapshots."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

import boa
from boa import interpret

from .paths import reference_root

# Boa's compiler cache is controlled by the caller and never defaults into the
# source checkout.  ``BOA_DISABLE_CACHE=1`` remains useful for hermetic runs.
if cache_dir := os.getenv("BOA_CACHE_DIR"):
    interpret.set_cache_dir(cache_dir)
elif os.getenv("BOA_DISABLE_CACHE") == "1":
    interpret.disable_cache()


class VyperPoolRunner:
    """Deploy reference contracts and return initial/post-action snapshots."""

    def __init__(
        self,
        contracts_path: str | os.PathLike[str] | None = None,
        *,
        reference: str | os.PathLike[str] | None = None,
        core: str | os.PathLike[str] | None = None,
    ) -> None:
        self.contracts_path = (
            Path(contracts_path).expanduser().resolve()
            if contracts_path is not None
            else reference_root(reference, core=core)
        )
        if not self.contracts_path.is_dir():
            raise FileNotFoundError(f"reference contracts do not exist: {self.contracts_path}")
        self.factory: Any = None
        self.math_contract: Any = None
        self.views_contract: Any = None
        self.amm_implementation: Any = None
        self.owner: Any = None

    def _contract(self, relative: str) -> Path:
        path = self.contracts_path / relative
        if not path.is_file():
            raise FileNotFoundError(f"reference contract does not exist: {path}")
        return path

    def deploy_infrastructure(self) -> None:
        self.math_contract = boa.load(str(self._contract("contracts/main/StableswapMath.vy")))
        self.views_contract = boa.load(str(self._contract("contracts/main/TwocryptoView.vy")))

        pool_path = self._contract("contracts/main/Twocrypto.vy")
        pool_code = pool_path.read_text(encoding="utf-8")
        pool_code = pool_code.replace(
            "MATH = Math(empty(address))",
            f"MATH = Math({self.math_contract.address})",
            1,
        )
        pool_code = pool_code.replace(
            "VIEW = Views(empty(address))",
            f"VIEW = Views({self.views_contract.address})",
            1,
        )
        self.amm_implementation = boa.loads_partial(pool_code).deploy_as_blueprint()

        self.factory = boa.load(str(self._contract("contracts/main/TwocryptoFactory.vy")))
        fee_receiver = boa.env.generate_address()
        self.owner = boa.env.generate_address()
        self.factory.initialise_ownership(fee_receiver, self.owner)
        with boa.env.prank(self.owner):
            self.factory.set_pool_implementation(self.amm_implementation, 0)
            self.factory.set_views_implementation(self.views_contract.address)
            self.factory.set_math_implementation(self.math_contract.address)

    def deploy_mock_tokens(self) -> tuple[Any, Any]:
        mock_path = Path(__file__).with_name("vyper_pool") / "mock_erc20.vy"
        token0 = boa.load(str(mock_path), "Token0", "TK0")
        token1 = boa.load(str(mock_path), "Token1", "TK1")
        deployer = boa.env.eoa
        supply = 10**9 * 10**18
        token0.mint(deployer, supply)
        token1.mint(deployer, supply)
        return token0, token1

    @staticmethod
    def _policy_kind(params: dict[str, Any]) -> str:
        policy = params.get("policy", {"kind": "none"})
        if isinstance(policy, str):
            return policy
        if isinstance(policy, dict):
            return str(policy.get("kind", "none"))
        return "none"

    def _deploy_policy(self, kind: str, pool: Any) -> Any:
        if kind in ("", "none"):
            return None
        if kind == "twocrypto_policy":
            return boa.load(str(self._contract("contracts/main/TwocryptoPolicy.vy")), pool.address)
        if kind == "zero_stub":
            return boa.load(str(self._contract("tests/mocks/ZeroStubPolicy.vy")))
        if kind in ("oracle_x2", "price_oracle_x2", "oracle_x2_sequential_fee"):
            policy_path = Path(__file__).with_name("vyper_pool") / "oracle_x2_sequential_fee_policy.vy"
            return boa.load(str(policy_path), pool.address)
        raise ValueError(f"unsupported policy kind: {kind}")

    def configure_pool(self, pool: Any, params: dict[str, Any]) -> None:
        reserved_profit_fraction = int(params.get("reserved_profit_fraction", 5 * 10**9))
        admin_fee = int(params.get("admin_fee", 5 * 10**9))
        policy_kind = self._policy_kind(params)
        policy = self._deploy_policy(policy_kind, pool)
        zero = "0x0000000000000000000000000000000000000000"
        with boa.env.prank(self.owner):
            pool.set_fee_parameters(reserved_profit_fraction, admin_fee)
            if policy is not None:
                pool.set_policy_contract(policy)
            elif policy_kind in ("", "none"):
                pool.set_policy_contract(zero)

    def deploy_pool(self, params: dict[str, Any], token0: Any, token1: Any) -> Any:
        pool_address = self.factory.deploy_pool(
            "Test Pool",
            "TEST",
            [token0.address, token1.address],
            0,
            int(params["A"]),
            int(params["gamma"]),
            int(params["mid_fee"]),
            int(params["out_fee"]),
            int(params["fee_gamma"]),
            int(params["adjustment_step_min"]),
            int(params["adjustment_step_max"]),
            int(params["ma_time"]),
            int(params["initial_price"]),
        )
        pool = boa.load_partial(str(self._contract("contracts/main/Twocrypto.vy"))).at(pool_address)
        self.configure_pool(pool, params)
        return pool

    @staticmethod
    def add_initial_liquidity(pool: Any, tokens: tuple[Any, Any], amounts: list[str]) -> None:
        token0, token1 = tokens
        user = boa.env.eoa
        token0.mint(user, int(amounts[0]))
        token1.mint(user, int(amounts[1]))
        with boa.env.prank(user):
            token0.approve(pool.address, 2**256 - 1)
            token1.approve(pool.address, 2**256 - 1)
            pool.add_liquidity([int(amounts[0]), int(amounts[1])], 0)

    @staticmethod
    def take_pool_snapshot(pool: Any) -> dict[str, Any]:
        balances = [pool.balances(0), pool.balances(1)]
        price_scale = pool.price_scale()
        cached_price_oracle = pool.eval("self.cached_price_oracle")
        xp = pool.internal._xp([balances[0], balances[1]], price_scale)
        donation_shares = pool.donation_shares()
        donation_duration = pool.donation_duration()
        last_release = pool.last_donation_release_ts()
        protection_expiry = pool.donation_protection_expiry_ts()
        protection_period = pool.donation_protection_period()
        timestamp = boa.env.timestamp
        if donation_shares == 0:
            unlocked = 0
        else:
            elapsed = timestamp - last_release
            unlocked = min(donation_shares, donation_shares * elapsed // donation_duration)
            protection_factor = 0
            if protection_expiry > timestamp:
                protection_factor = min(
                    (protection_expiry - timestamp) * 10**18 // protection_period,
                    10**18,
                )
            unlocked = unlocked * (10**18 - protection_factor) // 10**18
        return {
            "balances": [str(value) for value in balances],
            "admin_balances": [str(pool.admin_balances(0)), str(pool.admin_balances(1))],
            "xp": [str(xp[0]), str(xp[1])],
            "D": str(pool.D()),
            "virtual_price": str(pool.virtual_price()),
            "xcp_profit": str(pool.xcp_profit()),
            "lp_xcp_profit": str(pool.lp_xcp_profit()),
            "price_scale": str(price_scale),
            "price_oracle": str(cached_price_oracle),
            "last_prices": str(pool.last_prices()),
            "totalSupply": str(pool.totalSupply()),
            "timestamp": boa.env.timestamp,
            "donation_shares": str(donation_shares),
            "donation_shares_unlocked": str(unlocked),
            "donation_protection_expiry_ts": str(protection_expiry),
            "last_donation_release_ts": str(last_release),
        }

    def execute_actions(
        self, pool: Any, tokens: tuple[Any, Any], actions: list[dict[str, Any]]
    ) -> list[dict[str, Any]]:
        token0, token1 = tokens
        user = boa.env.eoa
        snapshots = [self.take_pool_snapshot(pool)]
        for action in actions:
            if action.get("time_delta", 0) > 0:
                boa.env.timestamp = boa.env.timestamp + int(action["time_delta"])
            success = True
            error: str | None = None
            try:
                kind = action["type"]
                if kind == "exchange":
                    token = token0 if action["i"] == 0 else token1
                    token.mint(user, int(action["dx"]))
                    with boa.env.prank(user):
                        token.approve(pool.address, 2**256 - 1)
                        pool.exchange(action["i"], action["j"], int(action["dx"]), 0)
                elif kind == "add_liquidity":
                    amounts = action["amounts"]
                    token0.mint(user, int(amounts[0]))
                    token1.mint(user, int(amounts[1]))
                    donation = bool(action.get("donation", False))
                    with boa.env.prank(user):
                        token0.approve(pool.address, 2**256 - 1)
                        token1.approve(pool.address, 2**256 - 1)
                        if donation:
                            pool.add_liquidity(
                                [int(amounts[0]), int(amounts[1])],
                                0,
                                "0x0000000000000000000000000000000000000000",
                                True,
                            )
                        else:
                            pool.add_liquidity([int(amounts[0]), int(amounts[1])], 0, user, False)
                elif kind == "time_travel":
                    seconds = int(action.get("seconds", 0))
                    if seconds > 0:
                        boa.env.timestamp = boa.env.timestamp + seconds
                    elif "timestamp" in action:
                        boa.env.timestamp = int(action["timestamp"])
                elif kind == "remove_liquidity":
                    minimum = action.get("min_amounts", [0, 0])
                    with boa.env.prank(user):
                        pool.remove_liquidity(
                            int(action["amount"]),
                            [int(minimum[0]), int(minimum[1])],
                            user,
                        )
                else:
                    raise ValueError(f"unknown action type: {kind}")
            except Exception as exc:  # snapshots intentionally record failed actions
                success = False
                error = str(exc)
            snapshot = self.take_pool_snapshot(pool)
            snapshot["action_success"] = success
            if error:
                snapshot["error"] = error
            snapshots.append(snapshot)
        return snapshots

    def run_benchmark(
        self,
        pool_configs_file: str | os.PathLike[str],
        sequences_file: str | os.PathLike[str],
        pool_names: list[str] | None = None,
    ) -> dict[str, Any]:
        pools_all = json.loads(Path(pool_configs_file).read_text(encoding="utf-8"))["pools"]
        pools = [p for p in pools_all if not pool_names or p.get("name") in set(pool_names)]
        sequences = json.loads(Path(sequences_file).read_text(encoding="utf-8"))["sequences"]
        if not sequences:
            raise RuntimeError("No sequences found in sequences.json")
        sequence = sequences[0]
        self.deploy_infrastructure()
        results = []
        for config in pools:
            token0, token1 = self.deploy_mock_tokens()
            boa.env.timestamp = int(sequence.get("start_timestamp", boa.env.timestamp))
            pool = self.deploy_pool(config, token0, token1)
            self.add_initial_liquidity(pool, (token0, token1), config["initial_liquidity"])
            states = self.execute_actions(pool, (token0, token1), sequence["actions"])
            results.append(
                {
                    "pool_config": config["name"],
                    "sequence": sequence["name"],
                    "result": {
                        "success": all(s.get("action_success", True) for s in states[1:]),
                        "states": states,
                    },
                }
            )
        return {"results": results}


def run_vyper_pool(
    pool_configs_file: str | os.PathLike[str],
    sequences_file: str | os.PathLike[str],
    output_file: str | os.PathLike[str],
    pool_names: list[str] | None = None,
    *,
    reference: str | os.PathLike[str] | None = None,
    core: str | os.PathLike[str] | None = None,
) -> dict[str, Any]:
    output = Path(output_file).expanduser().resolve()
    if not output.parent.is_dir():
        raise FileNotFoundError(f"output directory does not exist: {output.parent}")
    result = VyperPoolRunner(reference=reference, core=core).run_benchmark(
        pool_configs_file, sequences_file, pool_names
    )
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pools_file", type=Path)
    parser.add_argument("sequences_file", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--pools", default=None, help="comma-separated pool names")
    parser.add_argument("--reference", type=Path, default=None)
    parser.add_argument("--core", type=Path, default=None)
    args = parser.parse_args(argv)
    selected = [item for item in args.pools.split(",") if item] if args.pools else None
    run_vyper_pool(
        args.pools_file,
        args.sequences_file,
        args.out,
        selected,
        reference=args.reference,
        core=args.core,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
