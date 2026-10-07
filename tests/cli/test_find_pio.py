"""Locating PlatformIO: a penv whose interpreter is gone must be skipped."""

import sys
from pathlib import Path

import pytest


def script(tmp_path, first_line: bytes, name="pio") -> Path:
    path = tmp_path / name
    path.write_bytes(first_line + b"\nprint('hi')\n")
    return path


def test_no_shebang_counts_as_present(cli, tmp_path):
    assert cli._interpreter_present(script(tmp_path, b"MZ binary"))


def test_existing_interpreter_is_present(cli, tmp_path):
    assert cli._interpreter_present(script(tmp_path, b"#!" + sys.executable.encode()))


def test_interpreter_with_arguments_is_present(cli, tmp_path):
    assert cli._interpreter_present(script(tmp_path, b"#!" + sys.executable.encode() + b" -u"))


def test_missing_interpreter_is_absent(cli, tmp_path):
    assert not cli._interpreter_present(script(tmp_path, b"#!/nonexistent/penv/bin/python3.11"))


def test_unreadable_script_is_absent(cli, tmp_path):
    assert not cli._interpreter_present(tmp_path / "missing")


@pytest.fixture
def home(cli, tmp_path, monkeypatch):
    fake_home = tmp_path / "home"
    (fake_home / ".platformio" / "penv" / "bin").mkdir(parents=True)
    monkeypatch.setattr(cli.Path, "home", classmethod(lambda cls: fake_home))
    return fake_home


def test_find_pio_prefers_a_working_penv(cli, home, monkeypatch):
    pio = script(home / ".platformio" / "penv" / "bin", b"#!" + sys.executable.encode())
    monkeypatch.setattr(cli.shutil, "which", lambda name: "/usr/bin/pio")
    assert cli.find_pio() == str(pio)


def test_find_pio_skips_a_stale_penv_for_the_path(cli, home, monkeypatch):
    script(home / ".platformio" / "penv" / "bin", b"#!/gone/python3.9")
    monkeypatch.setattr(cli.shutil, "which", lambda name: "/usr/bin/pio")
    assert cli.find_pio() == "pio"


def test_find_pio_exits_when_nothing_is_installed(cli, home, monkeypatch, capsys):
    monkeypatch.setattr(cli.shutil, "which", lambda name: None)
    with pytest.raises(SystemExit):
        cli.find_pio()
    assert "PlatformIO not found" in capsys.readouterr().err
