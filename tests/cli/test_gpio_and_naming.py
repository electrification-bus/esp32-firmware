"""GPIO conflict detection, constructor templating and C identifier naming."""

import pytest

from helpers import device_doc, relay


# ── check_gpio_conflicts ──────────────────────────────────────────────────────

def test_distinct_gpios_pass(cli):
    cli.check_gpio_conflicts({"buses.i2c.pin_sda": 13, "buses.i2c.pin_scl": 16,
                              "node 'r' config.pin": 14})


def test_same_gpio_twice_names_both_users(cli, capsys):
    with pytest.raises(SystemExit):
        cli.check_gpio_conflicts({"buses.i2c.pin_sda": 13, "node 'r' config.pin": 13})
    err = capsys.readouterr().err
    assert "GPIO 13: buses.i2c.pin_sda, node 'r' config.pin" in err


@pytest.mark.parametrize("value", [-1, True, "13", None])
def test_negative_bool_and_non_int_pins_are_ignored(cli, value):
    cli.check_gpio_conflicts({"a": value, "b": value, "c": 13})


def test_node_pin_clashing_with_bus_pin_fails_generate(sandbox, capsys):
    doc = device_doc([{"component": "i2c/bme280", "id": "env", "config": {"address": "0x76"}},
                      relay("r", 13)],
                     buses={"i2c": {"pin_sda": 13, "pin_scl": 16}})
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    assert "GPIO 13: buses.i2c.pin_sda, node 'r' config.pin" in capsys.readouterr().err


def test_two_nodes_on_one_bus_share_its_pins(sandbox):
    # Distinct addresses: these are two devices on ONE bus, which is what the test is
    # about, and the address is how the master tells them apart.
    doc = device_doc([{"component": "modbus/sht20", "id": "t1",
                       "config": {"address": 1}},
                      {"component": "modbus/eletechsup32", "id": "v1",
                       "config": {"address": 4}}],
                     buses={"rs485": {"pin_a": 44, "pin_b": 43}})
    sandbox.generate(doc)


def test_two_relays_on_one_gpio_fail_generate(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc([relay("a", 14), relay("b", 14)]))
    assert "GPIO 14: node 'a' config.pin, node 'b' config.pin" in capsys.readouterr().err


def test_unused_bus_pins_are_not_checked(sandbox):
    # buses.i2c is declared but no node uses it, so its pin 14 does not clash.
    sandbox.generate(device_doc([relay("r", 14)], buses={"i2c": {"pin_sda": 14, "pin_scl": 16}}))


# ── constructor templating ────────────────────────────────────────────────────

def test_required_keys_are_read_from_the_template(cli):
    tmpl = '("{id}", {bus.i2c.pin_sda}, {bus.i2c.pin_scl}, {config.address}, {config.pin})'
    assert cli.required_config_keys(tmpl) == {"address", "pin"}
    assert cli.required_bus_keys(tmpl) == {("i2c", "pin_sda"), ("i2c", "pin_scl")}


@pytest.mark.parametrize("value,literal", [(True, "true"), (False, "false"), (15, "15"),
                                           ("0x76", "0x76"), (0.5, "0.5")])
def test_cpp_literal(cli, value, literal):
    assert cli._cpp_literal(value) == literal


def test_fmt_ctor_substitutes_id_config_and_bus(cli):
    out = cli.fmt_ctor('("{id}", {bus.i2c.pin_sda}, {config.on})', "env-1",
                       {"on": True}, buses={"i2c": {"pin_sda": 13}})
    assert out == '("env-1", 13, true)'


def test_fmt_ctor_names_the_register_table_after_a_c_safe_id(cli):
    out = cli.fmt_ctor("({registers}, {registers_count})", "env-1", {}, registers=[{}, {}, {}])
    assert out == "(_regs_env_1, 3)"


@pytest.mark.parametrize("raw,ident", [("pump-contactor", "pump_contactor"), ("a.b c", "a_b_c"),
                                       ("ok_1", "ok_1"), (7, "7")])
def test_cvar_maps_non_identifier_characters_to_underscore(cli, raw, ident):
    assert cli._cvar_global(raw) == ident
