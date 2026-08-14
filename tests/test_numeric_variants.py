from __future__ import annotations

import platform
from pathlib import Path

import pytest

from twocrypto_parity import numeric_variants
from twocrypto_parity.cpp_pool_runner import CppPoolRunner, MODES


def test_version_reports_pinned_reference_and_python(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as exited:
        numeric_variants.main(["--version"])

    assert exited.value.code == 0
    assert capsys.readouterr().out == (
        "twocrypto-parity 0.1.0 "
        "twocrypto-ng=2c645ca604a4a0878e08f2f1581e5c4ae1c8f8d4 "
        f"python={platform.python_version()}\n"
    )


def test_harness_override_rejected_before_execution_or_output_creation(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[tuple[object, ...]] = []
    monkeypatch.setattr(numeric_variants, "run_cpp_pool", lambda *args, **kwargs: calls.append(args))
    output_dir = tmp_path / "multi-mode"

    with pytest.raises(ValueError, match=r"--harness.*exactly one"):
        numeric_variants.run_variants(
            "pools.json",
            "sequences.json",
            output_dir,
            modes=("i", "d"),
            harness=tmp_path / "benchmark_harness_custom",
        )

    assert calls == []
    assert not output_dir.exists()


def test_default_multi_mode_resolves_distinct_mode_binaries(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build_dir = tmp_path / "build"
    build_dir.mkdir()
    for mode in MODES:
        (build_dir / f"benchmark_harness_{mode}").touch()

    resolved: list[tuple[str, Path]] = []

    def fake_run_cpp_pool(mode: str, pools: object, sequences: object, output: Path, **kwargs: object) -> dict[str, object]:
        runner = CppPoolRunner(build_dir=kwargs["build"], harness=kwargs["harness"])
        harness_path = runner.harness_path(mode)
        resolved.append((mode, harness_path))
        return {"results": [], "metadata": {"harness_path": str(harness_path)}}

    monkeypatch.setattr(numeric_variants, "run_cpp_pool", fake_run_cpp_pool)
    summary = numeric_variants.run_variants(
        "pools.json",
        "sequences.json",
        tmp_path / "outputs",
        modes=MODES,
        build=build_dir,
    )

    assert [mode for mode, _ in resolved] == list(MODES)
    assert [path.name for _, path in resolved] == [f"benchmark_harness_{mode}" for mode in MODES]
    assert len({path for _, path in resolved}) == len(MODES)
    assert summary["modes"] == list(MODES)
    assert summary["outputs"] == {mode: f"cpp_{mode}_combined.json" for mode in MODES}


def test_single_mode_override_keeps_mode_label(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    harness = tmp_path / "benchmark_harness_custom"
    harness.touch()
    seen: list[tuple[str, Path | None]] = []

    def fake_run_cpp_pool(mode: str, pools: object, sequences: object, output: Path, **kwargs: object) -> dict[str, object]:
        seen.append((mode, kwargs["harness"]))
        return {"results": [], "metadata": {"harness_path": str(harness)}}

    monkeypatch.setattr(numeric_variants, "run_cpp_pool", fake_run_cpp_pool)
    summary = numeric_variants.run_variants(
        "pools.json",
        "sequences.json",
        tmp_path / "outputs",
        modes=("ld",),
        harness=harness,
    )

    assert seen == [("ld", harness)]
    assert summary["modes"] == ["ld"]
    assert summary["outputs"] == {"ld": "cpp_ld_combined.json"}
