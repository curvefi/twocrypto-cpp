"""Invoke the already-built exact uint256 pool parity harness."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
from typing import Any

from .paths import build_root, core_root


def _resolve_harness(
    *,
    core: Path,
    build: str | os.PathLike[str] | None,
    harness: str | os.PathLike[str] | None,
) -> Path:
    if harness is not None:
        path = Path(harness).expanduser().resolve()
    else:
        build_dir = (
            Path(build).expanduser().resolve()
            if build is not None
            else build_root(core=core)
        )
        if not build_dir.is_dir():
            raise FileNotFoundError(f"pool build directory does not exist: {build_dir}")
        path = build_dir / "benchmark_harness_i"
    if not path.is_file():
        raise FileNotFoundError(
            f"exact parity harness is missing: {path}; build it separately or pass --harness"
        )
    return path


def run_cpp_pool(
    pool_configs_file: str | os.PathLike[str],
    sequences_file: str | os.PathLike[str],
    output_file: str | os.PathLike[str],
    *,
    core: str | os.PathLike[str] | None = None,
    build: str | os.PathLike[str] | None = None,
    harness: str | os.PathLike[str] | None = None,
) -> dict[str, Any]:
    project = core_root(core)
    executable = _resolve_harness(core=project, build=build, harness=harness)
    pools = Path(pool_configs_file).expanduser().resolve()
    sequences = Path(sequences_file).expanduser().resolve()
    output = Path(output_file).expanduser().resolve()
    if not pools.is_file():
        raise FileNotFoundError(f"pool configs not found: {pools}")
    if not sequences.is_file():
        raise FileNotFoundError(f"sequences not found: {sequences}")
    if not output.parent.is_dir():
        raise FileNotFoundError(f"output directory does not exist: {output.parent}")

    proc = subprocess.run(
        [str(executable), str(pools), str(sequences), str(output)],
        cwd=project,
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
        return json.load(stream)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pools_file", type=Path)
    parser.add_argument("sequences_file", type=Path)
    parser.add_argument("--out", required=True, type=Path, help="explicit result JSON path")
    parser.add_argument("--core", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--harness", type=Path)
    args = parser.parse_args(argv)
    run_cpp_pool(
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
