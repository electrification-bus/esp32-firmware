"""Fixtures for the ./ebus-esp32 CLI tests.

The CLI is one extensionless script, loaded here once as a module. Tests that touch the
filesystem run against `sandbox`: a throwaway repo root whose lib/ and platformio/ link
to the real ones, and a project directory beside it, with every path constant the CLI
writes through pointed into them.
"""

import importlib.machinery
import importlib.util
import sys
import time as _real_time
from argparse import Namespace
from pathlib import Path

import pytest
import yaml

REPO = Path(__file__).resolve().parents[2]
CLI_PATH = REPO / "ebus-esp32"


@pytest.fixture(scope="session")
def cli():
    loader = importlib.machinery.SourceFileLoader("ebus_esp32", str(CLI_PATH))
    spec = importlib.util.spec_from_loader("ebus_esp32", loader)
    module = importlib.util.module_from_spec(spec)
    # No .pyc for the script: it would land in a __pycache__/ at the repo root.
    saved, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = saved
    return module


class Sandbox:
    def __init__(self, cli, root: Path, project: Path):
        self.cli = cli
        self.root = root
        self.project = project

    def write_device(self, doc, name="device.yml") -> Path:
        path = self.project / name
        text = doc if isinstance(doc, str) else yaml.safe_dump(doc, sort_keys=False)
        path.write_text(text, encoding="utf-8")
        return path

    def write_overlay(self, doc) -> Path:
        path = self.cli.CONFIG_OVERLAY
        text = doc if isinstance(doc, str) else yaml.safe_dump(doc, sort_keys=False)
        path.write_text(text, encoding="utf-8")
        return path

    def generate(self, doc, overlay=None):
        """Run the generator on `doc`. Unless an overlay is given, one supplying a valid
        passphrase is written so tests of other rules get past the passphrase check."""
        if overlay is None:
            overlay = {"device": {"device_passphrase": "test-passphrase"}}
        if overlay is not False:
            self.write_overlay(overlay)
        path = self.write_device(doc)
        self.cli._generate(Namespace(device_yml=str(path)))
        return path

    @property
    def homie_cpp(self) -> str:
        return (self.cli.GEN_SRC_DIR / "homie.cpp").read_text(encoding="utf-8")

    @property
    def config_yml(self) -> dict:
        return yaml.safe_load((self.cli.GEN_DATA_DIR / "config.yml").read_text(encoding="utf-8"))

    @property
    def homie_yml(self) -> dict:
        return yaml.safe_load((self.cli.GEN_DATA_DIR / "homie.yml").read_text(encoding="utf-8"))


@pytest.fixture
def sandbox(cli, tmp_path, monkeypatch):
    root = tmp_path / "repo"
    root.mkdir()
    (root / "lib").symlink_to(REPO / "lib", target_is_directory=True)
    (root / "platformio").symlink_to(REPO / "platformio", target_is_directory=True)
    for d in ("src", "data", "build"):
        (root / d).mkdir()
    build = root / "build"
    project = tmp_path / "project"
    project.mkdir()
    out = project / ".ebus"
    patches = {
        "ROOT": root,
        "LIB_DIR": root / "lib",
        "SRC_DIR": root / "src",
        "DATA_DIR": root / "data",
        "INCLUDE_DIR": root / "include",
        "BUILD_DIR": build,
        "PLATFORMIO_INI": root / "platformio" / "platformio.ini",
        "PIO_LIBDEPS_DIR": root / ".pio" / "cli" / "libdeps",
        "PROJECT_DIR": project,
        "OUT_DIR": out,
        "GEN_SRC_DIR": out / "src",
        "GEN_DATA_DIR": out / "data",
        "GENERATED_INI": out / "platformio.ini",
        "PIO_BUILD_DIR": out / "build",
        "CONFIG_OVERLAY": project / "config-overlay.yml",
        "CONFIG_OVERLAY_REPO": root / "config-overlay.yml",
        "CONFIG_OVERLAY_LEGACY": tmp_path / "home" / ".config" / "eBus" / "config.yml",
        "MONITOR_HOSTS": build / "monitor-hosts.json",
        "_REGDEV_HEADER": root / "lib" / "modbus" / "register_device.h",
    }
    for name, value in patches.items():
        monkeypatch.setattr(cli, name, value)
    monkeypatch.delenv("EBUS_GENERATED", raising=False)
    return Sandbox(cli, root, project)


class FakeClock:
    """Stands in for the CLI's `time` module: time() reads a counter that only sleep()
    (or advance()) moves. Anything else falls through to the real module."""

    def __init__(self, start: float = 1_000_000.0):
        self.now = start

    def time(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds

    def advance(self, seconds: float) -> None:
        self.now += seconds

    def __getattr__(self, name):
        return getattr(_real_time, name)


@pytest.fixture
def clock(cli, monkeypatch):
    fake = FakeClock()
    monkeypatch.setattr(cli, "time", fake)
    return fake
