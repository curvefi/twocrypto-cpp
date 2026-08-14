from __future__ import annotations

import tomllib
from importlib import resources
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_vyper_helpers_are_declared_as_package_data() -> None:
    config = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    setuptools = config["tool"]["setuptools"]
    assert setuptools["include-package-data"] is True
    package_data = setuptools["package-data"]
    assert package_data["twocrypto_parity.vyper_pool"] == ["*.vy"]

    package = resources.files("twocrypto_parity.vyper_pool")
    for helper in ("mock_erc20.vy", "oracle_x2_sequential_fee_policy.vy"):
        assert package.joinpath(helper).is_file(), helper
