"""Serial-port handoff between `ebus-esp32 monitor --serial` and a flash.

A USB-serial port has one owner at a time. `ebus-esp32 monitor` (USB) runs the serial
monitor as a child process under a Supervisor; a flash (`run`, `push`) wraps its upload
in flash_handoff(), which asks the supervisor to release the port, waits for it, flashes,
and lets the supervisor reopen the port. Everything is files under .run/, polled, so it
works in any terminal and on any OS; no tmux, signals, or FIFOs are involved.

Files, keyed by port_key(port):

  monitor-<key>.state     breadcrumb written by the supervisor while it runs (KEY=VALUE:
                          PID, PORT, PIO_ENV, MONITOR_CMD, STARTED, TMUX_PANE). Its PID
                          decides liveness; a breadcrumb whose PID is dead is stale and
                          is removed by whoever finds it.
  flash-<key>.request     written by the flasher (PID=...). Its presence asks the
                          supervisor to release the port; its removal (or the flasher's
                          death) tells the supervisor to reopen it.
  monitor-<key>.released  written by the supervisor once its child has exited and the
                          port is closed. The flasher waits for it.
"""

import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

POLL = 0.2                 # seconds between file checks, both sides
RELEASE_TIMEOUT = 20.0     # flasher's wait for the monitor to close the port
TERMINATE_GRACE = 3.0      # supervisor's wait after terminate() before kill()
PORT_RETURN_TIMEOUT = 10.0  # supervisor's wait for a re-enumerating USB port


class HandoffError(Exception):
    """A handoff that cannot proceed; the message is meant for the user."""


# ── keys, files, liveness ──────────────────────────────────────────────────────

def port_key(port: str) -> str:
    """Filesystem-safe key for a port. macOS exposes each device as both /dev/cu.X and
    /dev/tty.X, so the cu./tty. prefix is dropped and both spellings share one key."""
    name = os.path.basename(str(port).rstrip("/\\")) or str(port)
    for prefix in ("cu.", "tty."):
        if name.startswith(prefix):
            name = name[len(prefix):]
            break
    if sys.platform == "win32":
        name = name.upper()
    return re.sub(r"[^A-Za-z0-9._-]", "_", name)


def state_path(run_dir: Path, port: str) -> Path:
    return Path(run_dir) / f"monitor-{port_key(port)}.state"


def request_path(run_dir: Path, port: str) -> Path:
    return Path(run_dir) / f"flash-{port_key(port)}.request"


def released_path(run_dir: Path, port: str) -> Path:
    return Path(run_dir) / f"monitor-{port_key(port)}.released"


def pid_alive(pid) -> bool:
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    if sys.platform == "win32":
        # os.kill(pid, 0) on Windows terminates the process, so ask the kernel instead.
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, pid)   # QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return bool(ok) and code.value == 259                # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def read_kv(path: Path) -> dict:
    """Parse a KEY=VALUE file; {} when it is missing or unreadable."""
    out = {}
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError:
        return out
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip().strip('"')
    return out


def _write_kv(path: Path, data: dict) -> None:
    path.write_text("".join(f"{k}={v}\n" for k, v in data.items()), encoding="utf-8")


def _create_exclusive(path: Path, data: dict) -> bool:
    """Atomically create path with data; False if it already exists."""
    try:
        fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    except FileExistsError:
        return False
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write("".join(f"{k}={v}\n" for k, v in data.items()))
    return True


def _unlink(path: Path) -> None:
    try:
        Path(path).unlink()
    except FileNotFoundError:
        pass


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def live_monitors(run_dir: Path) -> list:
    """Breadcrumbs of running monitors as dicts (with "_path" added). Stale ones,
    whose PID is dead or missing (including pre-supervisor tmux breadcrumbs), are
    removed along with their .released marker."""
    run_dir = Path(run_dir)
    found = []
    for path in sorted(run_dir.glob("monitor-*.state")):
        info = read_kv(path)
        if pid_alive(info.get("PID")):
            info["_path"] = path
            found.append(info)
        else:
            _unlink(path)
            _unlink(path.with_suffix(".released"))
    return found


def _live_request(run_dir: Path, port: str):
    """The flash request for port as a dict, or None if absent or its flasher died."""
    path = request_path(run_dir, port)
    if not path.exists():
        return None
    info = read_kv(path)
    if pid_alive(info.get("PID")):
        return info
    return None


# ── port detection ─────────────────────────────────────────────────────────────

def detect_serial_port(pio: str, env: dict = None, cwd=None):
    """The first USB serial port `pio device list` reports, or None. The monitor pins
    this port so a resume after a flash reopens the same board."""
    import json
    try:
        out = subprocess.run([pio, "device", "list", "--serial", "--json-output"],
                             capture_output=True, text=True, env=env, cwd=cwd,
                             timeout=30).stdout
        ports = json.loads(out or "[]")
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    for entry in ports:
        if "VID:PID" in str(entry.get("hwid", "")).upper() and entry.get("port"):
            return entry["port"]
    return None


# ── flasher side ───────────────────────────────────────────────────────────────

class flash_handoff:
    """Context manager wrapped around every USB upload. With no live monitor on the
    port it does nothing. Otherwise it asks the monitor to release the port, waits up
    to `timeout` seconds for it, and on exit lets the monitor resume.

    With port=None (pio auto-detects the upload port) it acts on the single live
    monitor; several live monitors are ambiguous and raise HandoffError."""

    def __init__(self, run_dir: Path, port: str = None, timeout: float = None,
                 poll: float = POLL, out=None):
        self.run_dir = Path(run_dir)
        self.port = port
        if timeout is None:
            timeout = float(os.environ.get("EBUS_HANDOFF_TIMEOUT", RELEASE_TIMEOUT))
        self.timeout = timeout
        self.poll = poll
        self.out = out or (lambda msg: print(msg, file=sys.stderr, flush=True))
        self.monitor = None   # breadcrumb of the monitor paused, if any
        self._request = None

    def _target(self):
        monitors = live_monitors(self.run_dir) if self.run_dir.is_dir() else []
        if self.port:
            key = port_key(self.port)
            for m in monitors:
                if port_key(m.get("PORT", "")) == key:
                    return m
            return None
        if len(monitors) > 1:
            lines = "\n".join(f"  --port {m.get('PORT')}  (env {m.get('PIO_ENV')}, "
                              f"pid {m.get('PID')})" for m in monitors)
            raise HandoffError(f"{len(monitors)} serial monitors are running; name the "
                               f"port to flash:\n{lines}")
        return monitors[0] if monitors else None

    def __enter__(self):
        mon = self._target()
        if mon is None:
            return self
        port = mon["PORT"]
        req = request_path(self.run_dir, port)
        me = {"PID": os.getpid(), "PORT": port, "STARTED": _now()}
        if not _create_exclusive(req, me):
            other = _live_request(self.run_dir, port)
            if other and str(other.get("PID")) != str(os.getpid()):
                raise HandoffError(f"another flash (pid {other.get('PID')}) is already "
                                   f"using {port}")
            _write_kv(req, me)   # stale request left by a flasher that died
        self._request = req
        self.monitor = mon
        self.out(f"(pausing the serial monitor on {port}, pid {mon['PID']})")
        try:
            return self._wait_released(mon)
        except BaseException:
            _unlink(req)   # e.g. Ctrl-C while waiting: let the monitor resume
            self._request = None
            raise

    def _wait_released(self, mon: dict):
        port = mon["PORT"]
        released = released_path(self.run_dir, port)
        deadline = time.monotonic() + self.timeout
        while True:
            if released.exists() or not pid_alive(mon["PID"]):
                return self
            if time.monotonic() >= deadline:
                self.monitor = None
                raise HandoffError(
                    f"the serial monitor on {port} (pid {mon['PID']}) did not release the "
                    f"port within {self.timeout:g}s.\n"
                    f"Quit it (Ctrl-C in its terminal) or `kill {mon['PID']}`, then flash "
                    f"again. EBUS_HANDOFF_TIMEOUT=<seconds> waits longer.")
            time.sleep(self.poll)

    start = __enter__

    def finish(self) -> None:
        self.__exit__(None, None, None)

    def __exit__(self, *exc):
        if self._request is not None:
            _unlink(self._request)
            if pid_alive(self.monitor["PID"]):
                self.out(f"(resuming the serial monitor on {self.monitor['PORT']})")
        return False


# ── monitor side ───────────────────────────────────────────────────────────────

def _port_present(port: str) -> bool:
    if sys.platform == "win32" or not str(port).startswith("/"):
        return True   # COMn and URLs cannot be checked as paths
    return os.path.exists(port)


class Supervisor:
    """Runs the serial monitor as a child process and hands the port to a flash.

    spawn() starts the child and returns its Popen. drain(proc) consumes its output
    (or simply waits) and returns once the child has exited. While the child runs, a
    watcher thread polls for a flash request and terminates the child when one
    appears. The supervisor then marks the port released, waits for the request to
    go away (or for its flasher to die), and starts the child again with the same
    command. A child that exits with no request pending ends the supervisor."""

    def __init__(self, run_dir: Path, port: str, spawn, drain=None, *, env_name: str = "",
                 monitor_cmd: str = "", poll: float = POLL, out=None,
                 grace: float = TERMINATE_GRACE, port_timeout: float = PORT_RETURN_TIMEOUT):
        self.run_dir = Path(run_dir)
        self.port = port
        self.spawn = spawn
        self.drain = drain or (lambda proc: proc.wait())
        self.env_name = env_name
        self.monitor_cmd = monitor_cmd
        self.poll = poll
        self.out = out or (lambda msg: print(msg, flush=True))
        self.grace = grace
        self.port_timeout = port_timeout
        self.state = state_path(self.run_dir, port)
        self.released = released_path(self.run_dir, port)
        self._proc = None
        self._stop = threading.Event()
        self._tty = None
        self._paused = False

    # -- breadcrumb --

    def register(self) -> None:
        self.run_dir.mkdir(parents=True, exist_ok=True)
        info = {"PID": os.getpid(), "PORT": self.port, "PIO_ENV": self.env_name,
                "MONITOR_CMD": f'"{self.monitor_cmd}"', "STARTED": _now()}
        if os.environ.get("TMUX_PANE"):
            info["TMUX_PANE"] = os.environ["TMUX_PANE"]
        if not _create_exclusive(self.state, info):
            other = read_kv(self.state)
            if pid_alive(other.get("PID")) and str(other.get("PID")) != str(os.getpid()):
                raise HandoffError(f"a serial monitor (pid {other.get('PID')}) is already "
                                   f"watching {self.port}")
            _write_kv(self.state, info)   # stale breadcrumb from a crashed monitor
        _unlink(self.released)
        if self.request_path().exists() and _live_request(self.run_dir, self.port) is None:
            _unlink(self.request_path())

    def unregister(self) -> None:
        if str(read_kv(self.state).get("PID")) == str(os.getpid()):
            _unlink(self.state)
        _unlink(self.released)

    def request_path(self) -> Path:
        return request_path(self.run_dir, self.port)

    def flash_requested(self) -> bool:
        return _live_request(self.run_dir, self.port) is not None

    # -- terminal state --
    # The child (pio's miniterm) puts the terminal into raw mode and only restores it
    # on a clean exit. A terminated child leaves it raw, and the next child would save
    # raw as "original", so the supervisor restores the attributes it saw at startup.

    def _save_tty(self) -> None:
        if sys.platform == "win32":
            return
        try:
            import termios
            if sys.stdin.isatty():
                self._tty = termios.tcgetattr(sys.stdin.fileno())
        except (ImportError, OSError, ValueError):
            self._tty = None

    def _restore_tty(self) -> None:
        if self._tty is None:
            return
        try:
            import termios
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, self._tty)
        except (ImportError, OSError, ValueError):
            pass

    # -- child control --

    def _terminate(self, proc) -> None:
        if proc.poll() is not None:
            return
        proc.terminate()
        try:
            proc.wait(timeout=self.grace)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    def _watch(self, proc, done: threading.Event) -> None:
        while not done.wait(self.poll):
            if self._stop.is_set():
                self._terminate(proc)
                return
            if self.flash_requested():
                self._paused = True
                self._terminate(proc)
                return

    def stop(self) -> None:
        """End the supervisor from another thread or a signal handler. Only signals the
        child: waiting on it here could deadlock against the main thread's own wait,
        so the watcher thread finishes the job."""
        self._stop.set()
        proc = self._proc
        if proc is not None:
            try:
                proc.terminate()
            except OSError:
                pass

    def _wait_for_resume(self) -> bool:
        """Block while the flash runs. False if the supervisor was stopped."""
        while self.flash_requested():
            if self._stop.wait(self.poll):
                return False
        _unlink(self.released)
        deadline = time.monotonic() + self.port_timeout
        while not _port_present(self.port) and time.monotonic() < deadline:
            if self._stop.wait(self.poll):
                return False
        return True

    def _install_signals(self):
        """Turn SIGTERM/SIGHUP (terminal closed) into a clean stop so the breadcrumb
        is removed. Only possible from the main thread."""
        import signal
        saved = {}
        if threading.current_thread() is not threading.main_thread():
            return saved
        for name in ("SIGTERM", "SIGHUP"):
            sig = getattr(signal, name, None)
            if sig is None:
                continue
            try:
                saved[sig] = signal.signal(sig, lambda *_: self.stop())
            except (OSError, ValueError):
                pass
        return saved

    def run(self) -> int:
        self.register()
        self._save_tty()
        saved = self._install_signals()
        rc = 0
        try:
            while not self._stop.is_set():
                self._paused = False
                proc = self.spawn()
                self._proc = proc
                done = threading.Event()
                watcher = threading.Thread(target=self._watch, args=(proc, done), daemon=True)
                watcher.start()
                try:
                    self.drain(proc)
                except KeyboardInterrupt:
                    self._stop.set()
                finally:
                    self._terminate(proc)
                    done.set()
                    watcher.join()
                    self._proc = None
                    self._restore_tty()
                rc = proc.returncode or 0
                if self._stop.is_set():
                    break
                if not (self._paused or self.flash_requested()):
                    break   # the user quit the monitor, or it failed on its own
                self.released.write_text(f"PID={os.getpid()}\n", encoding="utf-8")
                self.out(f"\n--- monitor paused: a flash is using {self.port} ---")
                if not self._wait_for_resume():
                    break
                self.out(f"--- monitor resumed on {self.port} ---")
        except KeyboardInterrupt:
            pass
        finally:
            import signal
            for sig, handler in saved.items():
                signal.signal(sig, handler)
            self._restore_tty()
            self.unregister()
        return 0 if self._stop.is_set() else rc


# ── `ebus-esp32 monitors` ──────────────────────────────────────────────────────

def format_monitors(run_dir: Path) -> str:
    monitors = live_monitors(run_dir) if Path(run_dir).is_dir() else []
    if not monitors:
        return "no active serial monitors"
    blocks = []
    for i, m in enumerate(monitors, 1):
        port = m.get("PORT", "")
        status = "paused for a flash" if released_path(run_dir, port).exists() else "watching"
        lines = [f"{i}) {port}",
                 f"   env:     {m.get('PIO_ENV', '')}",
                 f"   pid:     {m.get('PID')}",
                 f"   status:  {status}",
                 f"   started: {m.get('STARTED', '')}"]
        if m.get("TMUX_PANE"):
            lines.append(f"   tmux:    {m['TMUX_PANE']}")
        blocks.append("\n".join(lines))
    return "\n\n".join(blocks)
