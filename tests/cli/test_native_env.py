"""The [env:native] host unit-test env: listed but marked, never built as a board."""

from argparse import Namespace

import pytest


def test_suites_are_test_dirs_under_test_native(cli, tmp_path, monkeypatch):
    native = tmp_path / "native"
    for d in ("test_b", "test_a", "helpers"):
        (native / d).mkdir(parents=True)
    (native / "test_c.cpp").write_text("", encoding="utf-8")
    monkeypatch.setattr(cli, "NATIVE_TEST_DIR", native)
    assert cli.native_test_suites() == ["native/test_a", "native/test_b"]


def test_no_suites_when_test_native_is_missing(cli, tmp_path, monkeypatch):
    monkeypatch.setattr(cli, "NATIVE_TEST_DIR", tmp_path / "absent")
    assert cli.native_test_suites() == []


def test_repo_suites_are_found(cli):
    assert "native/test_log_stream" in cli.native_test_suites()


def test_core_suites_come_from_the_submodule(cli):
    assert {"test_homie_datatype", "test_homie_id"} <= set(cli.core_test_suites())


def test_missing_submodule_stops_generate_with_a_hint(cli, sandbox, monkeypatch, capsys):
    empty = sandbox.root / "empty-lib"
    (empty / "ebus_core").mkdir(parents=True)
    monkeypatch.setattr(cli, "LIB_DIR", empty)
    with pytest.raises(SystemExit):
        sandbox.generate({"device": {"name": "x"}, "nodes": []})
    assert "git submodule update --init" in capsys.readouterr().err


def test_envs_marks_native_as_not_a_board(cli, sandbox, capsys):
    cli.cmd_envs(Namespace(device_yml=str(sandbox.root / "device.yml")))
    lines = [l for l in capsys.readouterr().out.splitlines() if l.split()[:1] == ["native"]]
    assert len(lines) == 1
    assert "not a board" in lines[0]


def test_native_is_rejected_as_device_environment(cli):
    with pytest.raises(SystemExit):
        cli.get_environment({"device": {"environment": "native"}})
