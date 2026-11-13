"""device.role (and the older homie_role) and the oa3: block."""

import configparser
from pathlib import Path

import pytest

from helpers import device_doc

URL = "https://price.example.com/openadr3/3.1.0"
DEVICE_YML = Path("device.yml")


def role_of(cli, **device):
    return cli.device_role(device, DEVICE_YML)


# ── device_role ────────────────────────────────────────────────────────────────

def test_role_defaults_to_device(cli):
    assert role_of(cli) == "device"


@pytest.mark.parametrize("value", ["device", "controller", "standalone"])
def test_role_is_read(cli, value):
    assert role_of(cli, role=value) == value


@pytest.mark.parametrize("value", ["device", "controller"])
def test_homie_role_is_still_accepted(cli, value):
    assert role_of(cli, homie_role=value) == value


def test_role_and_homie_role_that_agree_are_accepted(cli):
    assert role_of(cli, role="controller", homie_role=" controller ") == "controller"


def test_role_and_homie_role_that_differ_are_refused(cli, capsys):
    with pytest.raises(SystemExit):
        role_of(cli, role="standalone", homie_role="device")
    err = capsys.readouterr().err
    assert "device.role: standalone and device.homie_role: device" in err
    assert "keep only role" in err


@pytest.mark.parametrize("device", [{"role": "gateway"}, {"homie_role": "Controller"},
                                    {"role": 1}])
def test_unknown_role_is_refused(cli, capsys, device):
    with pytest.raises(SystemExit):
        cli.device_role(device, DEVICE_YML)
    assert "device.role must be one of device, controller, standalone" in capsys.readouterr().err


# ── oa3_config ─────────────────────────────────────────────────────────────────

def oa3(**block):
    return {"oa3": {"base_url": URL, "program_name": "price", **block}}


def oa3_problems(cli, capsys, cfg) -> str:
    with pytest.raises(SystemExit):
        cli.oa3_config(cfg)
    err = capsys.readouterr().err
    assert "ERROR: device.yml oa3: is not valid:" in err
    return err


def test_no_oa3_block_is_none(cli):
    assert cli.oa3_config({}) is None
    assert cli.oa3_config({"oa3": None}) is None


def test_a_valid_block_is_returned(cli):
    cfg = oa3(program_id="p1", poll_period_s=300, max_events=4)
    assert cli.oa3_config(cfg) == cfg["oa3"]


def test_program_id_alone_is_enough(cli):
    assert cli.oa3_config({"oa3": {"base_url": URL, "program_id": "p1"}})


def test_base_url_is_required(cli, capsys):
    assert "base_url: required" in oa3_problems(cli, capsys, {"oa3": {"program_name": "p"}})


@pytest.mark.parametrize("url", ["price.example.com/x", "ftp://host/x", "https://", "https:///x"])
def test_base_url_needs_a_scheme_and_host(cli, capsys, url):
    err = oa3_problems(cli, capsys, oa3(base_url=url))
    assert "base_url: must start with https:// or http:// and a host" in err


def test_a_program_is_required(cli, capsys):
    err = oa3_problems(cli, capsys, {"oa3": {"base_url": URL}})
    assert "program_name or program_id: at least one is required" in err


def test_unknown_key_is_refused(cli, capsys):
    err = oa3_problems(cli, capsys, oa3(poll_period=60))
    assert "poll_period: unknown key (known: base_url, program_name" in err


def test_a_block_that_is_not_a_section_is_refused(cli, capsys):
    err = oa3_problems(cli, capsys, {"oa3": URL})
    assert "must be a section (a nested block), got str" in err


@pytest.mark.parametrize("key,value,message", [
    ("base_url", 5, "base_url: must be a string, got 5"),
    ("program_name", 7, "program_name: must be a string, got 7"),
    ("program_id", ["p"], "program_id: must be a string, got ['p']"),
    ("poll_period_s", "60", "poll_period_s: must be an integer, got '60'"),
    ("poll_period_s", 60.0, "poll_period_s: must be an integer, got 60.0"),
    ("poll_period_s", True, "poll_period_s: must be an integer, got True"),
    ("max_events", False, "max_events: must be an integer, got False"),
])
def test_wrong_types_are_refused(cli, capsys, key, value, message):
    assert message in oa3_problems(cli, capsys, oa3(**{key: value}))


@pytest.mark.parametrize("key,value,message", [
    ("poll_period_s", 9, "poll_period_s: must be 10..715827, got 9"),
    ("poll_period_s", 715_828, "poll_period_s: must be 10..715827, got 715828"),
    ("max_events", 0, "max_events: must be 1..64, got 0"),
    ("max_events", 65, "max_events: must be 1..64, got 65"),
])
def test_out_of_range_values_are_refused(cli, capsys, key, value, message):
    assert message in oa3_problems(cli, capsys, oa3(**{key: value}))


@pytest.mark.parametrize("key,value", [("poll_period_s", 10), ("poll_period_s", 715_827),
                                       ("max_events", 1), ("max_events", 64)])
def test_range_bounds_are_accepted(cli, key, value):
    assert cli.oa3_config(oa3(**{key: value}))


def test_every_problem_is_reported_at_once(cli, capsys):
    err = oa3_problems(cli, capsys, {"oa3": {"max_events": 0, "extra": 1}})
    for message in ("extra: unknown key", "base_url: required", "program_name or program_id",
                    "max_events: must be 1..64"):
        assert message in err


# ── oa3_lib_dep ────────────────────────────────────────────────────────────────

def test_lib_dep_defaults_to_the_pinned_library(cli, monkeypatch):
    monkeypatch.delenv("EBUS_CPP_OA3", raising=False)
    assert cli.oa3_lib_dep() == cli.OA3_LIB_DEP
    assert cli.OA3_LIB_DEP.startswith("oa3=")


def test_blank_override_is_ignored(cli, monkeypatch):
    monkeypatch.setenv("EBUS_CPP_OA3", "  ")
    assert cli.oa3_lib_dep() == cli.OA3_LIB_DEP


@pytest.mark.parametrize("value,dep", [
    ("symlink:///src/cpp-oa3", "oa3=symlink:///src/cpp-oa3"),
    (" oa3=symlink:///src/cpp-oa3 ", "oa3=symlink:///src/cpp-oa3"),
])
def test_override_is_named_oa3(cli, monkeypatch, value, dep):
    monkeypatch.setenv("EBUS_CPP_OA3", value)
    assert cli.oa3_lib_dep() == dep


# ── env_is_classic_esp32 ───────────────────────────────────────────────────────

@pytest.mark.parametrize("env,classic", [
    ("esp32-poe-iso", True),
    ("esp32-poe-iso-wrover", True),
    ("waveshare-esp32-s3-poe-eth", False),
    ("olimex-esp32-p4-devkit", False),
    ("seeed_xiao_esp32s3", False),
    ("no-such-env", False),
    (None, False),
])
def test_classic_esp32_envs(cli, sandbox, env, classic):
    assert cli.env_is_classic_esp32(env) is classic


# ── generate ───────────────────────────────────────────────────────────────────

def env_build_flags(cli, env="esp32-poe-iso") -> list:
    ini = configparser.RawConfigParser()
    ini.read(cli.GENERATED_INI, encoding="utf-8")
    return ini.get(f"env:{env}", "build_flags").split()


def lib_deps(cli) -> list:
    ini = configparser.RawConfigParser()
    ini.read(cli.GENERATED_INI, encoding="utf-8")
    return [l.strip() for l in ini.get("common", "lib_deps").splitlines() if l.strip()]


def test_standalone_generate(cli, sandbox, capsys, monkeypatch):
    monkeypatch.delenv("EBUS_CPP_OA3", raising=False)
    sandbox.generate(device_doc(role="standalone"))
    flags = env_build_flags(cli)
    assert "-DEBUS_ROLE_STANDALONE" in flags
    assert not {"-DENABLE_CONTROLLER", "-DEBUS_OA3"} & set(flags)
    assert not any(f.startswith("-DOA3_MAX_EVENTS") for f in flags)
    assert cli.OA3_LIB_DEP not in lib_deps(cli)
    config = sandbox.config_yml
    assert config["device"]["role"] == "standalone"
    assert "homie_role" not in config["device"]
    assert "oa3" not in config
    assert "role: standalone with no oa3:" in capsys.readouterr().out


def test_standalone_oa3_generate(cli, sandbox, monkeypatch):
    monkeypatch.delenv("EBUS_CPP_OA3", raising=False)
    doc = device_doc(role="standalone")
    doc.update(oa3(program_id="p1", poll_period_s=600, max_events=8))
    sandbox.generate(doc)
    flags = env_build_flags(cli)
    assert {"-DEBUS_ROLE_STANDALONE", "-DEBUS_OA3", "-DOA3_MAX_EVENTS=8"} <= set(flags)
    assert lib_deps(cli).count(cli.OA3_LIB_DEP) == 1
    assert sandbox.config_yml["oa3"] == {"base_url": URL, "program_name": "price",
                                         "program_id": "p1", "poll_period_s": 600}


def test_device_oa3_generate_uses_the_default_max_events(cli, sandbox, monkeypatch):
    monkeypatch.setenv("EBUS_CPP_OA3", "symlink:///src/cpp-oa3")
    doc = device_doc()
    doc.update(oa3())
    sandbox.generate(doc)
    flags = env_build_flags(cli)
    assert "-DEBUS_OA3" in flags
    assert f"-DOA3_MAX_EVENTS={cli.OA3_MAX_EVENTS_DEFAULT}" in flags
    assert "-DEBUS_ROLE_STANDALONE" not in flags
    assert "oa3=symlink:///src/cpp-oa3" in lib_deps(cli)
    assert sandbox.config_yml["device"]["role"] == "device"
    assert sandbox.config_yml["oa3"] == {"base_url": URL, "program_name": "price"}


def test_controller_from_homie_role_generates_the_controller_flag(cli, sandbox):
    sandbox.generate({"device": {"name": "c", "environment": "esp32-poe-iso",
                                 "homie_role": "controller"}})
    flags = env_build_flags(cli)
    assert "-DENABLE_CONTROLLER" in flags
    assert "-DEBUS_ROLE_STANDALONE" not in flags
    assert sandbox.config_yml["device"]["role"] == "controller"


@pytest.mark.parametrize("section", ["nodes", "children", "links"])
def test_standalone_refuses_homie_sections(cli, sandbox, capsys, section):
    doc = {"device": {"name": "s", "environment": "esp32-poe-iso", "role": "standalone"},
           section: [{"component": "gpio/digital_output", "id": "x",
                      "config": {"pin": 14, "active_low": False, "initial_state": False}}]
           if section == "nodes" else [{"id": "x"}]}
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    assert f"role: standalone has no Homie device, so it takes no {section}:" in \
        capsys.readouterr().err


def test_oa3_with_a_tls_broker_on_an_esp32_warns(cli, sandbox, capsys):
    doc = device_doc()
    doc["mqtt"] = {"mode": "configured-only", "host": "broker.example.com", "port": 8883}
    doc.update(oa3())
    sandbox.generate(doc)
    assert "oa3: with a TLS broker holds two TLS sessions" in capsys.readouterr().out


def test_oa3_on_standalone_does_not_warn_about_tls(cli, sandbox, capsys):
    doc = device_doc(role="standalone")
    doc["mqtt"] = {"mode": "configured-only", "host": "broker.example.com", "port": 8883}
    doc.update(oa3())
    sandbox.generate(doc)
    assert "two TLS sessions" not in capsys.readouterr().out


@pytest.mark.parametrize("key,most", [("base_url", 255), ("program_name", 63),
                                      ("program_id", 63)])
def test_values_longer_than_the_firmware_buffers_are_refused(cli, capsys, key, most):
    value = (URL + "/" + "x" * most) if key == "base_url" else "x" * (most + 1)
    assert f"{key}: at most {most} characters" in oa3_problems(cli, capsys, oa3(**{key: value}))


@pytest.mark.parametrize("key", ["role", "homie_role"])
def test_overlay_cannot_set_the_role(cli, sandbox, capsys, key):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(), overlay={"device": {"device_passphrase": "test-passphrase",
                                                           key: "controller"}})
    assert f"device.{key}: decides what firmware is built" in capsys.readouterr().err


@pytest.mark.parametrize("env,max_events,warns", [
    ("esp32-poe-iso", 2, False),
    ("esp32-poe-iso", 4, True),
    ("esp32-poe-iso-wrover", 8, False),
])
def test_storage_warning_without_psram(cli, sandbox, capsys, env, max_events, warns):
    doc = device_doc(role="standalone")
    doc["device"]["environment"] = env
    doc.update(oa3(max_events=max_events))
    sandbox.generate(doc)
    assert ("has no PSRAM" in capsys.readouterr().out) == warns
