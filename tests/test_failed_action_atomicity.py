from __future__ import annotations

from types import SimpleNamespace

from twocrypto_parity.vyper_pool_runner import VyperPoolRunner


class _Pool:
    def __init__(self) -> None:
        self._balances = [100, 200]
        self.internal = SimpleNamespace(_xp=lambda values, _scale: values)

    def balances(self, index: int) -> int:
        return self._balances[index]

    def admin_balances(self, _index: int) -> int:
        return 0

    def price_scale(self) -> int:
        return 10**18

    def eval(self, _expression: str) -> int:
        return 10**18

    def donation_shares(self) -> int:
        return 0

    def donation_duration(self) -> int:
        return 1

    def last_donation_release_ts(self) -> int:
        return 0

    def donation_protection_expiry_ts(self) -> int:
        return 0

    def donation_protection_period(self) -> int:
        return 1

    def D(self) -> int:
        return 300

    def virtual_price(self) -> int:
        return 1

    def xcp_profit(self) -> int:
        return 1

    def lp_xcp_profit(self) -> int:
        return 1

    def last_prices(self) -> int:
        return 1

    def totalSupply(self) -> int:
        return 100


def test_unknown_action_is_recorded_without_pool_mutation() -> None:
    runner = VyperPoolRunner.__new__(VyperPoolRunner)
    pool = _Pool()
    before = runner.take_pool_snapshot(pool)
    snapshots = runner.execute_actions(pool, (object(), object()), [{"type": "unsupported_action"}])
    after = snapshots[-1]
    assert after["action_success"] is False
    assert after["error"]
    for field, value in before.items():
        assert after[field] == value
