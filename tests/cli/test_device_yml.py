"""device.yml loading and generate-time validation."""

from argparse import Namespace

import pytest

from helpers import device_doc, relay


def generate_error(sandbox, capsys, doc, **kw) -> str:
    with pytest.raises(SystemExit) as exc:
        sandbox.generate(doc, **kw)
    assert exc.value.code == 1
    return capsys.readouterr().err


# ── loading ───────────────────────────────────────────────────────────────────

def test_missing_device_yml_exits(cli, tmp_path, capsys):
    with pytest.raises(SystemExit):
        cli.load_device_yml(tmp_path / "nope.yml")
    assert "not found" in capsys.readouterr().err


def test_malformed_yaml_reports_line_and_column(cli, tmp_path, capsys):
    path = tmp_path / "device.yml"
    path.write_text("device:\n  name: [unclosed\n", encoding="utf-8")
    with pytest.raises(SystemExit):
        cli.load_device_yml(path)
    err = capsys.readouterr().err
    assert "is not valid YAML" in err
    assert "at line" in err and "column" in err


@pytest.mark.parametrize("text,shape", [("- a\n- b\n", "list"), ("", "an empty document")])
def test_top_level_must_be_a_mapping(cli, tmp_path, capsys, text, shape):
    path = tmp_path / "device.yml"
    path.write_text(text, encoding="utf-8")
    with pytest.raises(SystemExit):
        cli.load_device_yml(path)
    assert f"got {shape}" in capsys.readouterr().err


def test_get_environment_requires_device_environment(cli, capsys):
    assert cli.get_environment({"device": {"environment": "esp32-poe-iso"}}) == "esp32-poe-iso"
    with pytest.raises(SystemExit):
        cli.get_environment({"device": {}})
    assert "missing device.environment" in capsys.readouterr().err


def test_log_port_defaults_to_23(cli):
    assert cli.get_log_port({}) == 23
    assert cli.get_log_port({"log": None}) == 23
    assert cli.get_log_port({"log": {"tcp_port": "2323"}}) == 2323


def test_log_port_rejects_non_integer(cli, capsys):
    with pytest.raises(SystemExit):
        cli.get_log_port({"log": {"tcp_port": "telnet"}})
    assert "log.tcp_port must be an integer" in capsys.readouterr().err


# ── device: block ─────────────────────────────────────────────────────────────

def test_legacy_device_board_is_rejected(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc(board="esp32-poe-iso"))
    assert "device.board" in err and "device.environment" in err


def test_device_id_must_be_a_string_template(sandbox, capsys):
    # `id: {chip_id}` is a YAML flow mapping, not a template string.
    doc = "device:\n  name: d\n  environment: esp32-poe-iso\n  id: {chip_id}\nnodes: []\n"
    err = generate_error(sandbox, capsys, doc)
    assert "device.id must be a string template" in err


# ── nodes: entries and component resolution ───────────────────────────────────

def test_nodes_must_be_a_list(sandbox, capsys):
    doc = device_doc()
    doc["nodes"] = {"component": "gpio/digital_output"}
    assert "nodes: value isn't a list" in generate_error(sandbox, capsys, doc)


def test_node_entry_requires_component(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([{"id": "x"}]))
    assert "missing required 'component'" in err


def test_unknown_bus_qualified_component_names_expected_path(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([{"component": "gpio/nonesuch"}]))
    assert "does not match any component file" in err
    assert "lib/gpio/nonesuch.yml" in err


def test_unknown_bare_component_says_every_bus_was_searched(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([{"component": "nonesuch"}]))
    assert "lib/<bus>/nonesuch.yml" in err


def test_bare_component_name_resolves_by_searching_buses(cli, sandbox):
    assert cli.find_component_yml("digital_output") == cli.LIB_DIR / "gpio" / "digital_output.yml"
    assert cli.find_component_yml("gpio/digital_output") == cli.LIB_DIR / "gpio" / "digital_output.yml"
    assert cli.find_component_yml("i2c/digital_output") is None


def test_child_device_requires_id(sandbox, capsys):
    doc = device_doc()
    doc["children"] = [{"name": "no id"}]
    assert "child device entry missing required 'id'" in generate_error(sandbox, capsys, doc)


# ── config: keys ──────────────────────────────────────────────────────────────

def test_missing_config_key_is_reported(sandbox, capsys):
    node = {"component": "gpio/digital_output", "id": "r",
            "config": {"pin": 14, "active_low": False}}
    err = generate_error(sandbox, capsys, device_doc([node]))
    assert "missing required key(s): initial_state" in err


def test_typoed_config_key_reports_both_missing_and_unrecognized(sandbox, capsys):
    node = {"component": "gpio/digital_output", "id": "r",
            "config": {"pinn": 14, "active_low": False, "initial_state": False}}
    err = generate_error(sandbox, capsys, device_doc([node]))
    assert "missing required key(s): pin" in err
    assert "unrecognized key(s): pinn" in err


def test_config_key_with_no_value_is_its_own_error(sandbox, capsys):
    doc = ("device:\n  name: d\n  environment: esp32-poe-iso\n"
           "nodes:\n  - component: gpio/digital_output\n    id: r\n"
           "    config:\n      pin:\n      active_low: false\n      initial_state: false\n")
    err = generate_error(sandbox, capsys, doc)
    assert "present but with no value: pin" in err


def test_bus_pin_in_node_config_points_at_buses_block(sandbox, capsys):
    node = {"component": "i2c/bme280", "id": "env",
            "config": {"address": "0x76", "pin_sda": 13, "pin_scl": 16}}
    err = generate_error(sandbox, capsys, device_doc([node], buses={"i2c": {"pin_sda": 13, "pin_scl": 16}}))
    assert "unrecognized key(s): pin_scl, pin_sda" in err
    assert "declared once in the top-level buses: block" in err


def test_config_block_must_be_a_mapping(sandbox, capsys):
    node = {"component": "gpio/digital_output", "id": "r", "config": [14]}
    assert "config: block" in generate_error(sandbox, capsys, device_doc([node]))


# ── duplicate ids ─────────────────────────────────────────────────────────────

def test_duplicate_explicit_node_ids_are_rejected(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([relay("r", 14), relay("r", 15)]))
    assert "node id 'r' is used by more than one node" in err
    assert "defaulted from the component" not in err


def test_duplicate_defaulted_node_ids_suggest_an_explicit_id(sandbox, capsys):
    a = {"component": "sim/battery_sim"}
    err = generate_error(sandbox, capsys, device_doc([a, dict(a)]))
    assert "is used by more than one node" in err
    assert "add an explicit 'id:'" in err


def test_node_ids_are_unique_across_child_devices(sandbox, capsys):
    doc = device_doc([relay("r", 14)])
    doc["children"] = [{"id": "kid", "nodes": [relay("r", 15)]}]
    assert "node id 'r' is used by more than one node" in generate_error(sandbox, capsys, doc)


def test_duplicate_child_device_ids_are_rejected(sandbox):
    doc = device_doc()
    doc["children"] = [{"id": "kid"}, {"id": "kid"}]
    with pytest.raises(SystemExit):
        sandbox.generate(doc)


def test_ids_colliding_as_cpp_names_are_rejected(sandbox):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc([relay("pump-1", 14), relay("pump_1", 15)]))


# ── buses: block ──────────────────────────────────────────────────────────────

def bme280(node_id="env"):
    return {"component": "i2c/bme280", "id": node_id, "config": {"address": "0x76"}}


def test_undeclared_bus_is_reported(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([bme280()]))
    assert "buses.i2c is not declared" in err


def test_missing_bus_key_is_reported(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([bme280()], buses={"i2c": {"pin_sda": 13}}))
    assert "buses.i2c.pin_scl is missing" in err


def test_bus_key_with_no_value_is_reported(sandbox, capsys):
    err = generate_error(sandbox, capsys,
                         device_doc([bme280()], buses={"i2c": {"pin_sda": 13, "pin_scl": None}}))
    assert "buses.i2c.pin_scl has no value" in err


def test_buses_block_must_be_a_mapping(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc([bme280()], buses=[13, 16]))
    assert "buses: value isn't a mapping" in err


def test_bus_pins_are_substituted_into_the_constructor(sandbox):
    sandbox.generate(device_doc([bme280()], buses={"i2c": {"pin_sda": 13, "pin_scl": 16}}))
    assert 'BME280 env("env", 13, 16, 0x76);' in sandbox.homie_cpp


# ── generated output ──────────────────────────────────────────────────────────

def test_generate_writes_cpp_homie_and_config(sandbox):
    sandbox.generate(device_doc([relay("relay1", 14)]))
    cpp = sandbox.homie_cpp
    assert "#include <gpio/digital_output.h>" in cpp
    assert 'DigitalOutput relay1("relay1", 14, false, false);' in cpp
    assert "relay1.setup();" in cpp and "relay1.loop();" in cpp
    assert [n["id"] for n in sandbox.homie_yml["nodes"]] == ["relay1"]
    assert sandbox.config_yml["device"]["name"] == "test-device"


def test_hyphenated_node_id_becomes_a_cpp_identifier(sandbox):
    sandbox.generate(device_doc([relay("pump-contactor", 14)]))
    cpp = sandbox.homie_cpp
    assert 'DigitalOutput pump_contactor("pump-contactor", 14' in cpp
    assert "static Property _prop_pump_contactor_state;" in cpp
    assert '_node->addProperty(&_prop_pump_contactor_state, "state"' in cpp


def test_child_device_nodes_are_built_in_cpp_not_homie_yml(sandbox):
    doc = device_doc([relay("r1", 14)])
    doc["children"] = [{"id": "kid", "name": "Kid", "nodes": [relay("r2", 15)]}]
    sandbox.generate(doc)
    cpp = sandbox.homie_cpp
    assert "static Device dev_kid;" in cpp
    assert "r2.setDevice(&dev_kid);" in cpp
    assert [n["id"] for n in sandbox.homie_yml["nodes"]] == ["r1"]


def test_mdns_section_is_omitted_when_device_yml_is_silent(sandbox):
    sandbox.generate(device_doc())
    assert "mdns" not in sandbox.config_yml


def test_mdns_keys_present_in_device_yml_are_emitted_as_bools(sandbox):
    doc = device_doc()
    doc["mdns"] = {"enabled": False, "advertise": 1}
    sandbox.generate(doc)
    assert sandbox.config_yml["mdns"] == {"enabled": False, "advertise": True}


def test_device_type_wins_over_type(sandbox):
    sandbox.generate(device_doc(type="generic", device_type="ebus-meter"))
    assert sandbox.config_yml["device"]["device_type"] == "ebus-meter"


def test_empty_broker_with_configured_only_warns(sandbox, capsys):
    sandbox.generate(device_doc())
    assert "mqtt.host is empty" in capsys.readouterr().out


def test_cmd_generate_quiets_stdout_when_already_generated(cli, sandbox, capsys, monkeypatch):
    path = sandbox.write_device(device_doc())
    sandbox.write_overlay({"device": {"device_passphrase": "p4ss"}})
    monkeypatch.setenv("EBUS_GENERATED", "1")
    cli.cmd_generate(Namespace(device_yml=str(path)))
    assert capsys.readouterr().out == ""
    assert (cli.SRC_DIR / "homie.cpp").exists()
