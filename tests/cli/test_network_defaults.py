"""wifi.enabled and the mdns keys: tri-state emission, overlay overrides, type checks,
and the notes generate prints about what the device will do."""

import pytest

from helpers import device_doc

PASS = {"device": {"device_passphrase": "test-passphrase"}}
ETHERNET_ENVS = ["esp32-poe-iso", "esp32-poe-iso-arduino2", "wesp32-poe", "waveshare-esp32-s3-poe-eth",
                 "olimex-esp32-p4-devkit"]
WIFI_AP_ENVS = ["esp32-s3-devkitc-1", "esp32-s3-devkitc-1-arduino2", "seeed_xiao_esp32s3"]


def overlay(**sections):
    return {**PASS, **sections}


@pytest.mark.parametrize("env", ETHERNET_ENVS)
def test_ethernet_envs_are_not_wifi_ap(cli, env):
    assert cli.env_is_wifi_ap(env) is False


@pytest.mark.parametrize("env", WIFI_AP_ENVS)
def test_wifi_ap_envs_are_detected(cli, env):
    assert cli.env_is_wifi_ap(env) is True


def test_commented_out_wifi_ap_flag_does_not_count(cli):
    # esp32-poe-iso-wrover carries ";-DCONFIG_WIFI_AP" in its build_flags.
    assert cli.env_is_wifi_ap("esp32-poe-iso-wrover") is False


def test_unknown_env_is_not_wifi_ap(cli):
    assert cli.env_is_wifi_ap("no-such-env") is False
    assert cli.env_is_wifi_ap(None) is False


@pytest.mark.parametrize("env", ETHERNET_ENVS + WIFI_AP_ENVS)
def test_keys_are_omitted_by_default_so_the_build_default_holds(sandbox, env):
    sandbox.generate(device_doc(environment=env))
    cfg = sandbox.config_yml
    assert "enabled" not in cfg["wifi"]
    assert "mdns" not in cfg


@pytest.mark.parametrize("value", [True, False])
def test_wifi_enabled_in_device_yml_is_emitted(sandbox, value):
    doc = device_doc()
    doc["wifi"] = {"ssid": "net", "password": "pw", "enabled": value}
    sandbox.generate(doc)
    assert sandbox.config_yml["wifi"] == {"ssid": "net", "password": "pw", "enabled": value}


@pytest.mark.parametrize("base, override", [(None, True), (None, False), (True, False),
                                            (False, True)])
def test_overlay_wifi_enabled_wins(sandbox, base, override):
    doc = device_doc()
    if base is not None:
        doc["wifi"] = {"enabled": base}
    sandbox.generate(doc, overlay=overlay(wifi={"enabled": override}))
    assert sandbox.config_yml["wifi"]["enabled"] is override


def test_overlay_mdns_enabled_false_wins(sandbox):
    doc = device_doc()
    doc["mdns"] = {"enabled": True}
    sandbox.generate(doc, overlay=overlay(mdns={"enabled": False}))
    assert sandbox.config_yml["mdns"] == {"enabled": False}


def test_overlay_mdns_advertise_added(sandbox):
    sandbox.generate(device_doc(), overlay=overlay(mdns={"advertise": False}))
    assert sandbox.config_yml["mdns"] == {"advertise": False}


@pytest.mark.parametrize("section, key, value", [
    ("wifi", "enabled", "yes please"),
    ("wifi", "enabled", 1),
    ("mdns", "enabled", "off"),
    ("mdns", "discovery", 0),
    ("mdns", "advertise", [True]),
])
def test_non_boolean_in_device_yml_is_refused(sandbox, capsys, section, key, value):
    doc = device_doc()
    doc[section] = {key: value}
    with pytest.raises(SystemExit) as exc:
        sandbox.generate(doc)
    assert exc.value.code == 1
    assert f"{section}.{key} must be true or false (got {value!r})" in capsys.readouterr().err


@pytest.mark.parametrize("section, key, value", [
    ("wifi", "enabled", "true"),
    ("mdns", "enabled", 0),
])
def test_non_boolean_in_overlay_is_refused(sandbox, capsys, section, key, value):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(), overlay=overlay(**{section: {key: value}}))
    err = capsys.readouterr().err
    assert "config-overlay.yml" in err
    assert f"{section}.{key} must be true or false" in err


def test_mdns_enabled_false_warns_about_the_hostname(sandbox, capsys):
    doc = device_doc()
    doc["mdns"] = {"enabled": False}
    sandbox.generate(doc)
    out = capsys.readouterr().out
    assert "WARNING: mdns.enabled is false: the device will not claim <device-id>.local" in out
    assert "`mdns: {advertise: false}` keeps the hostname" in out


@pytest.mark.parametrize("mdns", [None, {"advertise": False}, {"enabled": True}])
def test_no_hostname_warning_otherwise(sandbox, capsys, mdns):
    doc = device_doc()
    if mdns is not None:
        doc["mdns"] = mdns
    sandbox.generate(doc)
    assert "will not claim" not in capsys.readouterr().out


@pytest.mark.parametrize("mode", ["discovery-only", "discovery-with-fallback"])
def test_discovery_mode_wins_over_mdns_enabled_false(sandbox, capsys, mode):
    doc = device_doc()
    doc["mdns"] = {"enabled": False}
    doc["mqtt"] = {"mode": mode}
    sandbox.generate(doc)
    out = capsys.readouterr().out
    assert f"mqtt.mode {mode} needs the mDNS querier, which wins" in out
    assert "will not claim" not in out


def test_enabled_false_over_granular_true_is_reported(sandbox, capsys):
    doc = device_doc()
    doc["mdns"] = {"enabled": False, "advertise": True}
    sandbox.generate(doc)
    assert "mdns.enabled false overrides mdns.advertise true" in capsys.readouterr().out


def test_ssid_on_ethernet_build_notes_wifi_stays_off(sandbox, capsys):
    sandbox.generate(device_doc(), overlay=overlay(wifi={"ssid": "home", "password": "pw"}))
    out = capsys.readouterr().out
    assert "NOTE: wifi.ssid is set, but esp32-poe-iso is an Ethernet build" in out


def test_ssid_on_ethernet_build_with_wifi_enabled_has_no_note(sandbox, capsys):
    sandbox.generate(device_doc(),
                     overlay=overlay(wifi={"ssid": "home", "password": "pw", "enabled": True}))
    assert "NOTE: wifi.ssid" not in capsys.readouterr().out


def test_ssid_on_wifi_ap_build_has_no_note(sandbox, capsys):
    sandbox.generate(device_doc(environment="esp32-s3-devkitc-1"),
                     overlay=overlay(wifi={"ssid": "home", "password": "pw"}))
    assert "NOTE: wifi.ssid" not in capsys.readouterr().out


def test_wifi_disabled_on_wifi_ap_build_warns(sandbox, capsys):
    sandbox.generate(device_doc(environment="esp32-s3-devkitc-1"),
                     overlay=overlay(wifi={"enabled": False}))
    assert "WARNING: wifi.enabled is false on esp32-s3-devkitc-1, a WiFi-AP build" in \
        capsys.readouterr().out
