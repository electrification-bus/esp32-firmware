"""Serial-port handoff between a supervised monitor and a flash (bin/serial_handoff.py).

Fake processes stand in for `pio device monitor`, and a plain file in tmp_path stands in
for the serial port, so no serial hardware is touched."""

import importlib.util
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def sh():
    spec = importlib.util.spec_from_file_location("serial_handoff",
                                                  REPO / "bin" / "serial_handoff.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def run_dir(tmp_path):
    d = tmp_path / ".run"
    d.mkdir()
    return d


@pytest.fixture
def port(tmp_path):
    p = tmp_path / "cu.usbserial-0001"
    p.write_text("")
    return str(p)


def dead_pid():
    proc = subprocess.Popen([sys.executable, "-c", "pass"])
    proc.wait()
    return proc.pid


@pytest.fixture
def sleeper():
    """A live process that is not a supervisor, so it never answers a request."""
    proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    yield proc
    proc.kill()
    proc.wait()


def write_breadcrumb(sh, run_dir, port, pid, env="esp32-poe-iso"):
    path = sh.state_path(run_dir, port)
    path.write_text(f"PID={pid}\nPORT={port}\nPIO_ENV={env}\nSTARTED=now\n")
    return path


def wait_for(cond, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cond():
            return True
        time.sleep(0.02)
    return False


class FakeMonitor:
    """A Supervisor running in a thread over a child that sleeps until terminated."""

    def __init__(self, sh, run_dir, port, child="import time; time.sleep(60)"):
        self.spawned = []
        self.messages = []

        def spawn():
            proc = subprocess.Popen([sys.executable, "-c", child])
            self.spawned.append(proc)
            return proc

        self.sup = sh.Supervisor(run_dir, port, spawn, env_name="esp32-poe-iso",
                                 monitor_cmd="monitor", poll=0.02, out=self.messages.append,
                                 grace=2.0, port_timeout=2.0)
        self.rc = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        self.rc = self.sup.run()

    def __enter__(self):
        self.thread.start()
        assert wait_for(lambda: len(self.spawned) == 1 and self.sup.state.exists())
        return self

    def __exit__(self, *exc):
        self.sup.stop()
        self.thread.join(10)
        for proc in self.spawned:
            if proc.poll() is None:
                proc.kill()


# ── keys and liveness ──────────────────────────────────────────────────────────

def test_port_key_merges_cu_and_tty(sh):
    assert sh.port_key("/dev/cu.usbserial-214240") == "usbserial-214240"
    assert sh.port_key("/dev/tty.usbserial-214240") == "usbserial-214240"
    assert sh.port_key("/dev/ttyUSB0") == "ttyUSB0"


def test_pid_alive(sh):
    assert sh.pid_alive(os.getpid())
    assert not sh.pid_alive(dead_pid())
    assert not sh.pid_alive(None)
    assert not sh.pid_alive("")


# ── flasher side without a cooperating monitor ─────────────────────────────────

def test_flash_without_monitor_does_not_wait(sh, run_dir, port):
    start = time.monotonic()
    with sh.flash_handoff(run_dir, port, timeout=5, out=lambda m: None) as h:
        assert h.monitor is None
    assert time.monotonic() - start < 1
    assert list(run_dir.iterdir()) == []


def test_flash_without_run_dir(sh, tmp_path, port):
    with sh.flash_handoff(tmp_path / "missing", None, timeout=5, out=lambda m: None) as h:
        assert h.monitor is None


def test_stale_monitor_breadcrumb_is_removed(sh, run_dir, port):
    path = write_breadcrumb(sh, run_dir, port, dead_pid())
    sh.released_path(run_dir, port).write_text("")
    with sh.flash_handoff(run_dir, port, timeout=5, out=lambda m: None) as h:
        assert h.monitor is None
    assert not path.exists()
    assert not sh.released_path(run_dir, port).exists()


def test_legacy_tmux_breadcrumb_is_stale(sh, run_dir, port):
    path = sh.state_path(run_dir, port)
    path.write_text(f"TMUX_PANE_ID=%3\nTMUX_PANE=ebus:1.0\nPORT={port}\n")
    assert sh.live_monitors(run_dir) == []
    assert not path.exists()


def test_unresponsive_monitor_times_out(sh, run_dir, port, sleeper):
    write_breadcrumb(sh, run_dir, port, sleeper.pid)
    start = time.monotonic()
    with pytest.raises(sh.HandoffError) as err:
        with sh.flash_handoff(run_dir, port, timeout=0.5, poll=0.02, out=lambda m: None):
            pytest.fail("flash must not start")
    assert time.monotonic() - start < 3
    assert str(sleeper.pid) in str(err.value)
    assert "EBUS_HANDOFF_TIMEOUT" in str(err.value)
    assert not sh.request_path(run_dir, port).exists()


def test_timeout_comes_from_environment(sh, run_dir, monkeypatch):
    monkeypatch.setenv("EBUS_HANDOFF_TIMEOUT", "7.5")
    assert sh.flash_handoff(run_dir, None).timeout == 7.5


def test_monitor_dying_counts_as_release(sh, run_dir, port, sleeper):
    write_breadcrumb(sh, run_dir, port, sleeper.pid)
    # kill and reap: an unreaped child is a zombie, which still counts as alive
    threading.Timer(0.2, lambda: (sleeper.kill(), sleeper.wait())).start()
    with sh.flash_handoff(run_dir, port, timeout=5, poll=0.02, out=lambda m: None) as h:
        assert h.monitor is not None
    assert not sh.request_path(run_dir, port).exists()


def test_several_monitors_without_port_is_ambiguous(sh, run_dir, tmp_path, sleeper):
    write_breadcrumb(sh, run_dir, "/dev/cu.usbserial-A", sleeper.pid)
    write_breadcrumb(sh, run_dir, "/dev/cu.usbserial-B", os.getpid())
    with pytest.raises(sh.HandoffError) as err:
        with sh.flash_handoff(run_dir, None, timeout=0.2, out=lambda m: None):
            pass
    assert "usbserial-A" in str(err.value) and "usbserial-B" in str(err.value)


def test_concurrent_flash_is_refused(sh, run_dir, port, sleeper):
    write_breadcrumb(sh, run_dir, port, os.getpid())
    sh.request_path(run_dir, port).write_text(f"PID={sleeper.pid}\n")
    with pytest.raises(sh.HandoffError, match="another flash"):
        with sh.flash_handoff(run_dir, port, timeout=0.2, out=lambda m: None):
            pass


def test_stale_request_is_reclaimed(sh, run_dir, port):
    write_breadcrumb(sh, run_dir, port, os.getpid())
    sh.request_path(run_dir, port).write_text(f"PID={dead_pid()}\n")
    sh.released_path(run_dir, port).write_text("")
    with sh.flash_handoff(run_dir, port, timeout=1, out=lambda m: None) as h:
        assert h.monitor is not None
        assert sh.read_kv(sh.request_path(run_dir, port))["PID"] == str(os.getpid())
    assert not sh.request_path(run_dir, port).exists()


# ── full round trip with a supervisor ──────────────────────────────────────────

def test_flash_pauses_and_resumes_monitor(sh, run_dir, port):
    with FakeMonitor(sh, run_dir, port) as mon:
        first = mon.spawned[0]
        # tty. spelling of the same port finds the cu. monitor
        tty_port = port.replace("cu.", "tty.")
        with sh.flash_handoff(run_dir, tty_port, timeout=5, poll=0.02,
                              out=lambda m: None) as h:
            assert h.monitor is not None
            assert first.poll() is not None          # child stopped: port closed
            assert sh.released_path(run_dir, port).exists()
            time.sleep(0.2)
            assert len(mon.spawned) == 1             # not restarted mid-flash
        assert wait_for(lambda: len(mon.spawned) == 2)
        assert not sh.released_path(run_dir, port).exists()
        assert mon.spawned[1].poll() is None
        assert any("paused" in m for m in mon.messages)
        assert any("resumed" in m for m in mon.messages)
        assert sh.state_path(run_dir, port).exists()
    assert mon.rc == 0
    assert not sh.state_path(run_dir, port).exists()


def test_port_with_no_explicit_name_targets_single_monitor(sh, run_dir, port):
    with FakeMonitor(sh, run_dir, port) as mon:
        with sh.flash_handoff(run_dir, None, timeout=5, poll=0.02, out=lambda m: None) as h:
            assert h.monitor["PORT"] == port
        assert wait_for(lambda: len(mon.spawned) == 2)


def test_two_monitors_only_named_port_pauses(sh, run_dir, tmp_path, port):
    other = tmp_path / "cu.usbmodem-0002"
    other.write_text("")
    with FakeMonitor(sh, run_dir, port) as a, FakeMonitor(sh, run_dir, str(other)) as b:
        with sh.flash_handoff(run_dir, str(other), timeout=5, poll=0.02, out=lambda m: None):
            assert b.spawned[0].poll() is not None
            assert a.spawned[0].poll() is None
        assert wait_for(lambda: len(b.spawned) == 2)
        assert len(a.spawned) == 1


def test_crashed_flasher_lets_monitor_resume(sh, run_dir, port):
    with FakeMonitor(sh, run_dir, port) as mon:
        flasher = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        sh.request_path(run_dir, port).write_text(f"PID={flasher.pid}\n")
        assert wait_for(lambda: sh.released_path(run_dir, port).exists())
        flasher.kill()
        flasher.wait()
        assert wait_for(lambda: len(mon.spawned) == 2)


def test_monitor_waits_for_port_to_return(sh, run_dir, port):
    with FakeMonitor(sh, run_dir, port) as mon:
        with sh.flash_handoff(run_dir, port, timeout=5, poll=0.02, out=lambda m: None):
            os.unlink(port)                         # board re-enumerating after reset
        time.sleep(0.3)
        assert len(mon.spawned) == 1
        Path(port).write_text("")
        assert wait_for(lambda: len(mon.spawned) == 2)


def test_child_exit_without_request_ends_supervisor(sh, run_dir, port):
    mon = FakeMonitor(sh, run_dir, port, child="import sys; sys.exit(3)")
    mon.thread.start()
    mon.thread.join(10)
    assert mon.rc == 3
    assert len(mon.spawned) == 1
    assert not sh.state_path(run_dir, port).exists()


def test_second_monitor_on_same_port_is_refused(sh, run_dir, port):
    with FakeMonitor(sh, run_dir, port):
        sup = sh.Supervisor(run_dir, port, lambda: pytest.fail("must not spawn"))
        # Same PID as the running one would be accepted, so fake a different owner.
        path = sh.state_path(run_dir, port)
        info = path.read_text()
        sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        try:
            path.write_text(info.replace(f"PID={os.getpid()}", f"PID={sleeper.pid}"))
            with pytest.raises(sh.HandoffError, match="already"):
                sup.register()
        finally:
            sleeper.kill()
            sleeper.wait()
            path.write_text(info)


def test_stale_breadcrumb_is_reclaimed_by_new_monitor(sh, run_dir, port):
    write_breadcrumb(sh, run_dir, port, dead_pid())
    sh.request_path(run_dir, port).write_text(f"PID={dead_pid()}\n")
    sup = sh.Supervisor(run_dir, port, lambda: None, env_name="esp32-s3")
    sup.register()
    info = sh.read_kv(sh.state_path(run_dir, port))
    assert info["PID"] == str(os.getpid()) and info["PIO_ENV"] == "esp32-s3"
    assert not sh.request_path(run_dir, port).exists()
    sup.unregister()
    assert not sh.state_path(run_dir, port).exists()


# ── listing and port detection ─────────────────────────────────────────────────

def test_format_monitors(sh, run_dir, port):
    assert sh.format_monitors(run_dir) == "no active serial monitors"
    write_breadcrumb(sh, run_dir, port, os.getpid())
    write_breadcrumb(sh, run_dir, "/dev/cu.gone", dead_pid())
    out = sh.format_monitors(run_dir)
    assert port in out and "watching" in out and "gone" not in out
    sh.released_path(run_dir, port).write_text("")
    assert "paused for a flash" in sh.format_monitors(run_dir)


def test_detect_serial_port(sh, tmp_path):
    fake = tmp_path / "pio"
    fake.write_text("#!/bin/sh\ncat <<'EOF'\n"
                    '[{"port": "/dev/cu.Bluetooth-Incoming-Port", "hwid": "n/a"},'
                    ' {"port": "/dev/cu.usbserial-214240", "hwid": "USB VID:PID=1A86:7523"}]\n'
                    "EOF\n")
    fake.chmod(0o755)
    if sys.platform == "win32":
        pytest.skip("shell script stand-in for pio")
    assert sh.detect_serial_port(str(fake)) == "/dev/cu.usbserial-214240"
    assert sh.detect_serial_port(str(tmp_path / "nope")) is None


# ── through the CLI: monitor_serial and _upload ────────────────────────────────

def test_cli_upload_pauses_cli_monitor(cli, sh, run_dir, port, tmp_path, monkeypatch):
    """`monitor --serial` relays and learns from its child; `push` pauses it around the
    upload and the relaunched child keeps relaying."""
    from argparse import Namespace
    counter = tmp_path / "spawns"
    child = (
        "import pathlib, sys, time\n"
        f"c = pathlib.Path({str(counter)!r})\n"
        "n = int(c.read_text()) + 1 if c.exists() else 1\n"
        "c.write_text(str(n))\n"
        "print(f'Device ID: dev-{n}', flush=True)\n"
        "print(f'Network: WiFi STA IP=10.0.0.{n}', flush=True)\n"
        "time.sleep(60) if n == 1 else sys.exit(0)\n")
    monkeypatch.setattr(cli, "RUN_DIR", run_dir)
    monkeypatch.setattr(cli, "_pio_monitor_cmd", lambda env: [sys.executable, "-c", child])
    learned = []
    monkeypatch.setattr(cli, "_host_learn", lambda key, env, ip, dev: learned.append((ip, dev)))
    result = {}
    t = threading.Thread(target=lambda: result.setdefault(
        "rc", cli.monitor_serial({}, "esp32-poe-iso", port, "key")), daemon=True)
    t.start()
    assert wait_for(lambda: learned == [("10.0.0.1", "dev-1")])

    uploads = []

    def fake_pio_run(env, targets, p=None):
        uploads.append((targets, p, cli.serial_handoff.released_path(run_dir, port).exists()))
        return 0
    monkeypatch.setattr(cli, "pio_run", fake_pio_run)
    cfg = {"device": {"environment": "esp32-poe-iso"}}
    cli._upload(cfg, Namespace(port=port, fs_only=True, erase=False, device_yml="device.yml"),
                build=False)
    assert uploads == [(["buildfs", "uploadfs"], port, True)]
    t.join(15)
    assert result["rc"] == 0
    assert ("10.0.0.2", "dev-2") in learned
    assert not cli.serial_handoff.state_path(run_dir, port).exists()
