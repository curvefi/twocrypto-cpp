"""Seeded Stableswap math fixtures and optional C ABI/Boa comparison."""

from __future__ import annotations

import argparse
import ctypes
import json
import platform
import time
from pathlib import Path
from typing import Any, Callable

from .paths import contract_path


def generate_math_cases(seed: int = 0, realistic: int = 100) -> list[dict[str, str]]:
    """Return deterministic realistic and edge cases without writing files."""
    import random

    rng = random.Random(seed)
    cases = [
        {
            "A": str(rng.randint(10, 20_000) * 10_000),
            "gamma": str(rng.randint(10**10, 10**16)),
            "x0": str(rng.randint(10**18, 10**24)),
            "x1": str(rng.randint(10**18, 10**24)),
        }
        for _ in range(realistic)
    ]
    edge_values = ("1000000000000000000", "2000000000000000000", "1000000000000000000000000000")
    cases.extend(
        {"A": "10000000", "gamma": "145000000000000", "x0": x0, "x1": x1}
        for x0 in edge_values[:2]
        for x1 in edge_values[:2]
    )
    return cases


def write_math_cases(path: str | Path, *, seed: int = 0, realistic: int = 100) -> None:
    output = Path(path).expanduser().resolve()
    if not output.parent.is_dir():
        raise FileNotFoundError(f"output directory does not exist: {output.parent}")
    payload = {"seed": seed, "cases": generate_math_cases(seed, realistic)}
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


class CppMathBenchmark:
    """Call the existing C ABI; the library path must be supplied and present."""

    def __init__(self, library: str | Path) -> None:
        self.lib = ctypes.CDLL(str(Path(library).expanduser().resolve()))
        self.lib.newton_D.argtypes = [ctypes.c_char_p] * 4
        self.lib.newton_D.restype = ctypes.POINTER(ctypes.c_char)
        self.lib.get_y.argtypes = [ctypes.c_char_p] * 5 + [ctypes.c_int]
        self.lib.get_y.restype = ctypes.POINTER(ctypes.c_char_p)
        self.lib.get_p.argtypes = [ctypes.c_char_p] * 4
        self.lib.get_p.restype = ctypes.POINTER(ctypes.c_char)
        self.lib.free_string.argtypes = [ctypes.c_void_p]
        self.lib.free_string_array.argtypes = [ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]

    def newton_D(self, A: str, gamma: str, x0: str, x1: str) -> str:
        result = self.lib.newton_D(A.encode(), gamma.encode(), x0.encode(), x1.encode())
        value = ctypes.cast(result, ctypes.c_char_p).value.decode()
        self.lib.free_string(result)
        return value

    def get_y(self, A: str, gamma: str, x0: str, x1: str, D: str, i: int) -> str:
        result = self.lib.get_y(A.encode(), gamma.encode(), x0.encode(), x1.encode(), D.encode(), i)
        value = result[0].decode()
        self.lib.free_string_array(result, 2)
        return value

    def get_p(self, x0: str, x1: str, D: str, A: str) -> str:
        result = self.lib.get_p(x0.encode(), x1.encode(), D.encode(), A.encode())
        value = ctypes.cast(result, ctypes.c_char_p).value.decode()
        self.lib.free_string(result)
        return value


class VyperMathBenchmark:
    """Deploy StableswapMath from the pinned reference checkout."""

    def __init__(self, *, reference: str | Path | None = None, core: str | Path | None = None) -> None:
        import boa

        self.contract = boa.load(str(contract_path("contracts/main/StableswapMath.vy", reference=reference, core=core)))

    def newton_D(self, A: str, gamma: str, x0: str, x1: str) -> str:
        return str(self.contract.newton_D(int(A), int(gamma), [int(x0), int(x1)]))

    def get_y(self, A: str, gamma: str, x0: str, x1: str, D: str, i: int) -> str:
        return str(self.contract.get_y(int(A), int(gamma), [int(x0), int(x1)], int(D), i)[0])

    def get_p(self, x0: str, x1: str, D: str, A: str) -> str:
        return str(self.contract.get_p([int(x0), int(x1)], int(D), [int(A), 145000000000000]))


def compare_math(cases: list[dict[str, str]], cpp: CppMathBenchmark, vyper: VyperMathBenchmark) -> dict[str, Any]:
    results: dict[str, Any] = {name: {"matches": 0, "total": len(cases), "rows": []} for name in ("newton_D", "get_y", "get_p")}
    for case in cases:
        A, gamma, x0, x1 = case["A"], case["gamma"], case["x0"], case["x1"]
        D_v = vyper.newton_D(A, gamma, x0, x1)
        values = {
            "newton_D": (cpp.newton_D(A, gamma, x0, x1), D_v),
            "get_y": (cpp.get_y(A, gamma, x0, x1, D_v, 0), vyper.get_y(A, gamma, x0, x1, D_v, 0)),
            "get_p": (cpp.get_p(x0, x1, D_v, A), vyper.get_p(x0, x1, D_v, A)),
        }
        for name, (cpp_value, vyper_value) in values.items():
            match = cpp_value == vyper_value
            results[name]["matches"] += int(match)
            results[name]["rows"].append({"case": case, "cpp": cpp_value, "vyper": vyper_value, "match": match})
    return results


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path, help="explicit JSON output")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--library", type=Path, default=None, help="optional existing C ABI library")
    parser.add_argument("--reference", type=Path, default=None)
    parser.add_argument("--core", type=Path, default=None)
    args = parser.parse_args(argv)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    cases = generate_math_cases(args.seed, args.count)
    payload: dict[str, Any] = {"seed": args.seed, "cases": cases}
    if args.library:
        if not args.library.is_file():
            raise FileNotFoundError(args.library)
        started = time.perf_counter()
        payload["comparisons"] = compare_math(cases, CppMathBenchmark(args.library), VyperMathBenchmark(reference=args.reference, core=args.core))
        payload["elapsed_s"] = time.perf_counter() - started
    args.out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
