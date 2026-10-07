"""OTA outcome logic with a fake clock and a scripted device; no sockets are opened."""

import json
from argparse import Namespace

import pytest

HOST = "10.0.0.5"


class ScriptedDevice:
    """Replaces _ota_get. Each entry is a reply text, None for a dropped probe, or a
    callable taking the clock's current time and returning either. The last entry
    repeats once the script runs out. Every probe costs `latency` seconds."""

    def __init__(self, clock, script, latency=0.5):
        self.clock, self.script, self.latency, self.calls = clock, list(script), latency, 0

    def __call__(self, host, path, passphrase, timeout=10):
        entry = self.script[min(self.calls, len(self.script) - 1)]
        self.calls += 1
        now = self.clock.now
        self.clock.advance(self.latency)
        if callable(entry):
            entry = entry(now)
        return None if entry is None else (200, entry)


def uptime(seconds):
    return json.dumps({"enabled": False, "uptime_s": seconds})


def status(state):
    return json.dumps({"status": state})


@pytest.fixture
def device(cli, clock, monkeypatch):
    def install(script, latency=0.5):
        fake = ScriptedDevice(clock, script, latency)
        monkeypatch.setattr(cli, "_ota_get", fake)
        return fake
    return install


# ── _ota_uptime ───────────────────────────────────────────────────────────────

@pytest.mark.parametrize("text,expected", [
    (uptime(12), 12.0), (uptime(3.5), 3.5), ('{"enabled": true}', None),
    ('{"uptime_s": "12"}', None), ("OTA disabled", None), ("[1]", None), ("null", None),
])
def test_ota_uptime_parsing(cli, text, expected):
    assert cli._ota_uptime(text) == expected


# ── _ota_wait_reboot ──────────────────────────────────────────────────────────

def test_short_uptime_proves_a_reboot(cli, clock, device):
    since = clock.now
    clock.advance(30)                       # upload, then the post-reply grace period
    device([uptime(4)])
    outcome, reply = cli._ota_wait_reboot(HOST, 120, since)
    assert outcome == "rebooted"
    assert reply == uptime(4)


def test_uptime_longer_than_the_push_is_not_a_reboot(cli, clock, device):
    since = clock.now
    clock.advance(30)
    device([lambda now: uptime(now - since + 3600)])
    outcome, _ = cli._ota_wait_reboot(HOST, 120, since)
    assert outcome == "stayed_up"


def test_stayed_up_waits_out_the_limit(cli, clock, device):
    since = clock.now
    clock.advance(30)
    start = clock.now
    device([uptime(99999)])
    cli._ota_wait_reboot(HOST, 120, since)
    assert clock.now - start >= cli.OTA_STAYED_UP_LIMIT


def test_firmware_without_uptime_counts_as_stayed_up(cli, clock, device):
    since = clock.now
    clock.advance(30)
    device(['{"enabled": false}'])
    assert cli._ota_wait_reboot(HOST, 120, since)[0] == "stayed_up"


def test_dropped_probes_while_rebooting_then_proof(cli, clock, device):
    since = clock.now
    clock.advance(30)
    fake = device([None, None, None, uptime(2)])
    assert cli._ota_wait_reboot(HOST, 120, since)[0] == "rebooted"
    assert fake.calls == 4


def test_old_image_answering_then_reboot_still_proves_it(cli, clock, device):
    since = clock.now
    clock.advance(30)
    device([lambda now: uptime(now - since + 600), None, uptime(1)])
    assert cli._ota_wait_reboot(HOST, 120, since)[0] == "rebooted"


def test_silent_device_times_out(cli, clock, device):
    since = clock.now
    device([None])
    assert cli._ota_wait_reboot(HOST, 20, since) == (None, None)


# ── _ota_push_image after a lost reply ────────────────────────────────────────

@pytest.fixture
def lost_reply(cli, sandbox, clock, monkeypatch):
    image = sandbox.root / "firmware.bin"
    image.write_bytes(b"\0" * 16)
    monkeypatch.setattr(cli, "_ota_request", lambda *a, **k: (None, "no reply within 20s"))

    def push(outcome, back):
        monkeypatch.setattr(cli, "_ota_wait_reboot", lambda host, timeout, since: (outcome, back))
        cli._ota_push_image(HOST, "/api/ota/firmware", "p", image, "firmware", reboot=True)
    return push


def test_lost_reply_then_reboot_is_success(lost_reply, capsys):
    lost_reply("rebooted", uptime(3))
    assert "the device restarted after the upload began" in capsys.readouterr().out


def test_lost_reply_and_stayed_up_is_unconfirmed(lost_reply, capsys):
    with pytest.raises(SystemExit):
        lost_reply("stayed_up", uptime(9999))
    assert "firmware UNCONFIRMED" in capsys.readouterr().err


def test_lost_reply_and_silence_is_failure(lost_reply, capsys):
    with pytest.raises(SystemExit):
        lost_reply(None, None)
    assert "firmware FAILED" in capsys.readouterr().err


def test_http_error_reply_fails(cli, sandbox, monkeypatch, capsys):
    image = sandbox.root / "littlefs.bin"
    image.write_bytes(b"\0")
    monkeypatch.setattr(cli, "_ota_request", lambda *a, **k: (403, "OTA not enabled"))
    with pytest.raises(SystemExit):
        cli._ota_push_image(HOST, "/api/ota/filesystem", "p", image, "filesystem", reboot=False)
    assert "HTTP 403 OTA not enabled" in capsys.readouterr().err


def test_staged_image_does_not_accept_a_lost_reply(cli, sandbox, monkeypatch):
    image = sandbox.root / "littlefs.bin"
    image.write_bytes(b"\0")
    seen = {}

    def fake_request(host, url, passphrase, payload, ctype, label, reply_lost_ok=False, **kw):
        seen.update(url=url, reply_lost_ok=reply_lost_ok)
        return 200, "ok"
    monkeypatch.setattr(cli, "_ota_request", fake_request)
    cli._ota_push_image(HOST, "/api/ota/filesystem", "p", image, "filesystem", reboot=False)
    assert seen == {"url": "/api/ota/filesystem?reboot=0", "reply_lost_ok": False}


def test_missing_images_are_reported_before_contacting_the_device(cli, sandbox, capsys):
    fw, fs = sandbox.root / "firmware.bin", sandbox.root / "littlefs.bin"
    with pytest.raises(SystemExit):
        cli._require_images(Namespace(fs_only=False), fw, fs)
    err = capsys.readouterr().err
    assert "no firmware image" in err and "no filesystem image" in err
    fs.write_bytes(b"\0")
    cli._require_images(Namespace(fs_only=True), fw, fs)


# ── _pull_watch ───────────────────────────────────────────────────────────────

def test_pull_complete_returns_zero(cli, device, capsys):
    device([status("downloading"), status("downloading"), status("complete")])
    assert cli._pull_watch(HOST, "p", 300) == 0
    out = capsys.readouterr().out
    assert out.count("device: downloading") == 1
    assert "device: complete" in out


@pytest.mark.parametrize("state", ["host_not_found", "file_not_found", "http_error",
                                   "write_error", "download_incomplete"])
def test_pull_error_state_fails_with_its_explanation(cli, device, capsys, state):
    device([status(state)])
    assert cli._pull_watch(HOST, "p", 300) == 1
    assert cli._PULL_ERRORS[state] in capsys.readouterr().err


def test_pull_lost_contact_then_back_is_unconfirmed(cli, device, capsys):
    device([status("downloading"), None, None, '{"enabled": false}'])
    assert cli._pull_watch(HOST, "p", 300) == 1
    assert "OTA pull UNCONFIRMED" in capsys.readouterr().err


def test_pull_lost_contact_for_good_fails(cli, device, capsys):
    device([status("downloading"), None])
    assert cli._pull_watch(HOST, "p", 300) == 1
    assert "lost contact with the device" in capsys.readouterr().err


def test_pull_that_never_finishes_gives_up(cli, device, capsys):
    device([status("downloading")])
    assert cli._pull_watch(HOST, "p", 30) == 1
    assert "Gave up after 30s" in capsys.readouterr().err


def test_pull_non_json_reply_is_shown_as_the_state(cli, device, capsys):
    device(["busy", status("complete")])
    assert cli._pull_watch(HOST, "p", 300) == 0
    assert "device: busy" in capsys.readouterr().out
