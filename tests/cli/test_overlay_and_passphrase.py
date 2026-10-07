"""config-overlay.yml merging, the passphrase rule and OTA passphrase precedence."""

from argparse import Namespace

import pytest

from helpers import device_doc


# ── _deep_merge ───────────────────────────────────────────────────────────────

def test_deep_merge_overrides_leaves_and_counts_them(cli):
    base = {"wifi": {"ssid": "a", "password": "b"}, "mqtt": {"port": 1883}}
    applied = cli._deep_merge(base, {"wifi": {"ssid": "home"}, "mqtt": {"port": 8883}})
    assert applied == 2
    assert base == {"wifi": {"ssid": "home", "password": "b"}, "mqtt": {"port": 8883}}


@pytest.mark.parametrize("blank", ["", None])
def test_deep_merge_blank_values_fall_back(cli, blank):
    base = {"wifi": {"ssid": "from-device-yml"}}
    assert cli._deep_merge(base, {"wifi": {"ssid": blank}}) == 0
    assert base["wifi"]["ssid"] == "from-device-yml"


def test_deep_merge_keeps_false_and_zero(cli):
    base = {"mdns": {"enabled": True}, "log": {"tcp_port": 23}}
    cli._deep_merge(base, {"mdns": {"enabled": False}, "log": {"tcp_port": 0}})
    assert base == {"mdns": {"enabled": False}, "log": {"tcp_port": 0}}


def test_deep_merge_creates_missing_sections(cli):
    base = {}
    cli._deep_merge(base, {"mdns": {"advertise": True}})
    assert base == {"mdns": {"advertise": True}}


# ── overlay applied by generate ───────────────────────────────────────────────

def test_overlay_values_win_in_generated_config(sandbox):
    doc = device_doc()
    doc["wifi"] = {"ssid": "tracked", "password": "tracked-pw"}
    sandbox.generate(doc, overlay={"device": {"device_passphrase": "s3cret"},
                                   "wifi": {"ssid": "home", "password": ""},
                                   "mqtt": {"host": "broker.lan"}})
    cfg = sandbox.config_yml
    assert cfg["wifi"] == {"ssid": "home", "password": "tracked-pw"}
    assert cfg["mqtt"]["host"] == "broker.lan"
    assert cfg["device"]["device_passphrase"] == "s3cret"


def test_flat_legacy_overlay_key_is_rejected_with_section_hint(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(), overlay={"device": {"device_passphrase": "p"},
                                                "wifi_ssid": "home"})
    err = capsys.readouterr().err
    assert "wifi_ssid: not an overlay section" in err
    assert "wifi:\n      ssid: ..." in err


def test_overlay_cannot_override_nodes(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(), overlay={"device": {"device_passphrase": "p"},
                                                "nodes": []})
    err = capsys.readouterr().err
    assert "nodes: not an overlay section" in err


def test_overlay_section_must_be_a_mapping(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(), overlay={"device": {"device_passphrase": "p"},
                                                "wifi": "home"})
    assert "wifi: must be a section" in capsys.readouterr().err


@pytest.mark.parametrize("key,hint", [
    ("mqtt_host", "mqtt:\n      host: ..."),
    ("device_name", "device:\n      name: ..."),
    ("device_id", "device:\n      id: ..."),
    ("firmware_url", "device:\n      firmware_url: ..."),
    ("device_passphrase", "device:\n      device_passphrase: ..."),
    ("verbosity", ""),
])
def test_flat_key_hint(cli, key, hint):
    assert cli._flat_key_hint(key) == hint


# ── passphrase rule ───────────────────────────────────────────────────────────

@pytest.mark.parametrize("value", [None, "", "   ", "changeme", " ChangeMe "])
def test_empty_or_placeholder_passphrase_refused(sandbox, capsys, value):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(device_passphrase=value), overlay=False)
    assert "device.device_passphrase is" in capsys.readouterr().err


def test_device_yml_passphrase_accepted_without_overlay(sandbox):
    sandbox.generate(device_doc(device_passphrase="from-device-yml"), overlay=False)
    assert sandbox.config_yml["device"]["device_passphrase"] == "from-device-yml"


def test_overlay_passphrase_replaces_placeholder(sandbox):
    sandbox.generate(device_doc(device_passphrase="changeme"),
                     overlay={"device": {"device_passphrase": "real-one"}})


def test_blank_overlay_passphrase_falls_back_to_device_yml(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(device_passphrase="changeme"),
                         overlay={"device": {"device_passphrase": ""}})
    assert "the placeholder 'changeme'" in capsys.readouterr().err


def test_overlay_placeholder_is_refused_even_over_a_good_device_yml(sandbox):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(device_passphrase="good"),
                         overlay={"device": {"device_passphrase": "changeme"}})


def test_numeric_passphrase_is_read_as_text(cli, sandbox):
    assert cli._configured_passphrase({"device": {"device_passphrase": 1234}}) == "1234"


# ── OTA passphrase precedence ─────────────────────────────────────────────────

def cfg_with(passphrase):
    return {"device": {"device_passphrase": passphrase}}


def test_ota_passphrase_flag_wins(cli, sandbox):
    sandbox.write_overlay({"device": {"device_passphrase": "overlay"}})
    args = Namespace(passphrase="flag")
    assert cli._ota_passphrase(cfg_with("device-yml"), args) == "flag"


def test_ota_passphrase_overlay_beats_device_yml(cli, sandbox):
    sandbox.write_overlay({"device": {"device_passphrase": "overlay"}})
    assert cli._ota_passphrase(cfg_with("device-yml"), Namespace(passphrase=None)) == "overlay"


def test_ota_passphrase_falls_back_to_device_yml(cli, sandbox):
    assert cli._ota_passphrase(cfg_with("device-yml"), Namespace(passphrase=None)) == "device-yml"


def test_ota_passphrase_missing_everywhere_exits(cli, sandbox, capsys):
    with pytest.raises(SystemExit):
        cli._ota_passphrase({"device": {}}, Namespace())
    assert "no device_passphrase" in capsys.readouterr().err
