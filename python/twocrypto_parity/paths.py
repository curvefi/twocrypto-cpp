"""Repository path resolution with explicit, non-mutating overrides."""

from __future__ import annotations

import os
from pathlib import Path


def _env_path(name: str) -> Path | None:
    value = os.environ.get(name)
    return Path(value).expanduser().resolve() if value else None


def core_root(override: str | os.PathLike[str] | None = None) -> Path:
    """Return the pool repository root without creating or modifying it."""
    path = (
        Path(override).expanduser().resolve()
        if override is not None
        else _env_path("TWOCRYPTO_POOL_ROOT")
    )
    if path is None:
        # package is python/twocrypto_parity inside the pool repository
        path = Path(__file__).resolve().parents[2]
    if not path.is_dir():
        raise FileNotFoundError(f"pool repository root does not exist: {path}")
    return path


def reference_root(
    override: str | os.PathLike[str] | None = None,
    *,
    core: str | os.PathLike[str] | None = None,
) -> Path:
    path = (
        Path(override).expanduser().resolve()
        if override is not None
        else _env_path("TWOCRYPTO_REFERENCE_ROOT")
    )
    if path is None:
        path = core_root(core) / "reference" / "twocrypto-ng"
    if not path.is_dir():
        raise FileNotFoundError(f"twocrypto-ng reference does not exist: {path}")
    return path


def build_root(override: str | os.PathLike[str] | None = None, *, core: str | os.PathLike[str] | None = None) -> Path:
    path = (
        Path(override).expanduser().resolve()
        if override is not None
        else _env_path("TWOCRYPTO_BUILD_ROOT")
    )
    if path is None:
        path = core_root(core) / "build"
    if not path.is_dir():
        raise FileNotFoundError(
            f"pool build directory does not exist: {path}; build the pool separately "
            "or pass an explicit override"
        )
    return path
