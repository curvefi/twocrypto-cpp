"""Invoke an already-built typed pool parity adapter.

The runner deliberately does not configure or build CMake. A caller supplies an
existing pool build directory (or uses ``TWOCRYPTO_BUILD_ROOT``), and all result
writes go to the explicit output path.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path
from typing import Any

from .paths import build_root, core_root

MODES = ("i", "d", "f", "ld")


class CppPoolRunner:
    """Run ``benchmark_harness_{i,d,f,ld}`` without touching source/build files."""

    def __init__(
        self,
        cpp_project_path: str | os.PathLike[str] | None = None,
        *,
        build_dir: str | os.PathLike[str] | None = None,
        harness: str | os.PathLike[str] | None = None,
    ) -> None:
        self.cpp_project_path = core_root(cpp_project_path)
        self._harness_override = (
            Path(harness).expanduser().resolve() if harness is not None else None
        )
        if build_dir is not None:
            self.build_dir = Path(build_dir).expanduser().resolve()
        elif self._harness_override is not None:
            self.build_dir = self._harness_override.parent
        else:
            self.build_dir = build_root(core=self.cpp_project_path)
        if not self.build_dir.is_dir():
            raise FileNotFoundError(f"pool build directory does not exist: {self.build_dir}")

    def harness_path(self, mode: str) -> Path:
        if mode not in MODES:
            raise ValueError(f"mode must be one of {', '.join(MODES)}")
        path = self._harness_override or (self.build_dir / f"benchmark_harness_{mode}")
        if not path.is_file():
            raise FileNotFoundError(
                f"typed parity adapter is missing: {path}; build the optional parity target "
                "separately or pass --harness"
            )
        return path

    def run_benchmark(
        self,
        mode: str,
        pool_configs_file: str | os.PathLike[str],
        sequences_file: str | os.PathLike[str],
        output_file: str | os.PathLike[str],
    ) -> dict[str, Any]:
        pools = Path(pool_configs_file).expanduser().resolve()
        sequences = Path(sequences_file).expanduser().resolve()
        output = Path(output_file).expanduser().resolve()
        if not pools.is_file():
            raise FileNotFoundError(f"pool configs not found: {pools}")
        if not sequences.is_file():
            raise FileNotFoundError(f"sequences not found: {sequences}")
        if not output.parent.is_dir():
            raise FileNotFoundError(f"output directory does not exist: {output.parent}")
        harness = self.harness_path(mode)
        proc = subprocess.run(
            [str(harness), str(pools), str(sequences), str(output)],
            cwd=self.cpp_project_path,
            check=False,
            capture_output=True,
            text=True,
        )
        if proc.returncode:
            detail = proc.stderr.strip() or proc.stdout.strip()
            raise RuntimeError(f"C++ harness failed ({proc.returncode}): {detail}")
        if not output.is_file():
            raise RuntimeError(f"C++ harness did not write requested output: {output}")
        with output.open(encoding="utf-8") as stream:
            result = json.load(stream)
        metadata = result.setdefault("metadata", {})
        metadata["harness_path"] = str(harness)
        return result

    def format_json_output(self, json_file: str | os.PathLike[str]) -> None:
        """Normalize an explicitly requested result file in place."""
        output = Path(json_file).expanduser().resolve()
        with output.open(encoding="utf-8") as stream:
            payload = json.load(stream)
        with output.open("w", encoding="utf-8") as stream:
            json.dump(payload, stream, indent=2)
            stream.write("\n")


def run_cpp_pool(
    mode: str,
    pool_configs_file: str | os.PathLike[str],
    sequences_file: str | os.PathLike[str],
    output_file: str | os.PathLike[str],
    *,
    core: str | os.PathLike[str] | None = None,
    build: str | os.PathLike[str] | None = None,
    harness: str | os.PathLike[str] | None = None,
) -> dict[str, Any]:
    runner = CppPoolRunner(core, build_dir=build, harness=harness)
    result = runner.run_benchmark(mode, pool_configs_file, sequences_file, output_file)
    runner.format_json_output(output_file)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=MODES)
    parser.add_argument("pools_file", type=Path)
    parser.add_argument("sequences_file", type=Path)
    parser.add_argument("--out", required=True, type=Path, help="explicit result JSON path")
    parser.add_argument("--core", type=Path, default=None)
    parser.add_argument("--build", type=Path, default=None)
    parser.add_argument("--harness", type=Path, default=None)
    args = parser.parse_args(argv)
    run_cpp_pool(
        args.mode,
        args.pools_file,
        args.sequences_file,
        args.out,
        core=args.core,
        build=args.build,
        harness=args.harness,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
