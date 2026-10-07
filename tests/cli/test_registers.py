"""properties_from: registers (modbus/register_device) and the numeric unit audit."""

from pathlib import Path

import pytest

from helpers import device_doc

RS485 = {"rs485": {"pin_a": 44, "pin_b": 43}}
COMP = Path("register_device.yml")


def reg_node(registers, node_id="env1"):
    return {"component": "modbus/register_device", "id": node_id,
            "config": {"address": 1}, "registers": registers}


def props_error(cli, capsys, regs) -> str:
    with pytest.raises(SystemExit):
        cli.registers_to_properties(regs, "env1", COMP)
    return capsys.readouterr().err


# ── registers_to_properties ───────────────────────────────────────────────────

def test_register_becomes_a_read_only_retained_property(cli, sandbox):
    props = cli.registers_to_properties(
        [{"id": "relative-humidity", "reg": 2, "unit": "%", "description": "RH"}], "env1", COMP)
    assert props == [{"id": "relative-humidity", "name": "Relative Humidity",
                      "datatype": "float", "settable": False, "retained": True,
                      "unit": "%", "description": "RH"}]


def test_absent_unit_is_not_invented(cli, sandbox):
    props = cli.registers_to_properties([{"id": "count", "reg": 1, "datatype": "integer"}],
                                        "env1", COMP)
    assert "unit" not in props[0]


@pytest.mark.parametrize("regs", [[], None, {"id": "t"}])
def test_registers_must_be_a_non_empty_list(cli, sandbox, capsys, regs):
    assert "non-empty 'registers:' list" in props_error(cli, capsys, regs)


@pytest.mark.parametrize("reg,message", [
    ("t", "expected a mapping"),
    ({"reg": 1}, "missing required 'id'"),
    ({"id": "t"}, "missing required 'reg'"),
    ({"id": "t", "reg": "0x0001"}, "reg must be an integer"),
    ({"id": "t", "reg": 1, "type": "coil"}, "type must be 'input' or 'holding'"),
    ({"id": "t", "reg": 1, "datatype": "string"}, "datatype must be one of"),
])
def test_malformed_register_is_named(cli, sandbox, capsys, reg, message):
    assert message in props_error(cli, capsys, [reg])


def test_duplicate_register_id_is_rejected(cli, sandbox, capsys):
    err = props_error(cli, capsys, [{"id": "t", "reg": 1}, {"id": "t", "reg": 2}])
    assert "duplicate register id 't'" in err


@pytest.mark.xfail(strict=True, reason="bool is an int subclass, so `reg: true` passes the "
                   "integer check and emits `True` into the C++ descriptor table")
def test_boolean_reg_is_rejected(cli, sandbox):
    with pytest.raises(SystemExit):
        cli.registers_to_properties([{"id": "t", "reg": True}], "env1", COMP)


@pytest.mark.xfail(strict=True, raises=AttributeError,
                   reason="a numeric register id (`id: 1`) crashes with AttributeError on "
                          "rid.replace() instead of a clean error")
def test_numeric_register_id_is_reported_not_a_traceback(cli, sandbox):
    with pytest.raises(SystemExit):
        cli.registers_to_properties([{"id": 1, "reg": 1}], "env1", COMP)


# ── register-count cap ────────────────────────────────────────────────────────

def test_cap_is_read_from_the_firmware_header(cli, sandbox):
    header = (Path(cli.LIB_DIR) / "modbus" / "register_device.h").read_text(encoding="utf-8")
    assert f"#define REGDEV_MAX_REGISTERS {cli._regdev_max_registers()}" in header


def test_register_count_over_the_cap_is_rejected(cli, sandbox, capsys, monkeypatch):
    header = sandbox.root / "register_device.h"
    header.write_text("#define REGDEV_MAX_REGISTERS 2\n", encoding="utf-8")
    monkeypatch.setattr(cli, "_REGDEV_HEADER", header)
    regs = [{"id": f"r{i}", "reg": i} for i in range(2)]
    assert len(cli.registers_to_properties(regs, "env1", COMP)) == 2
    regs.append({"id": "r2", "reg": 2})
    err = props_error(cli, capsys, regs)
    assert "lists 3 registers; the firmware holds at most 2" in err


def test_real_cap_rejects_one_more_than_the_header_allows(cli, sandbox, capsys):
    cap = cli._regdev_max_registers()
    regs = [{"id": f"r{i}", "reg": i} for i in range(cap + 1)]
    assert f"the firmware holds at most {cap}" in props_error(cli, capsys, regs)


def test_missing_cap_define_exits(cli, sandbox, capsys, monkeypatch):
    header = sandbox.root / "register_device.h"
    header.write_text("// nothing here\n", encoding="utf-8")
    monkeypatch.setattr(cli, "_REGDEV_HEADER", header)
    with pytest.raises(SystemExit):
        cli._regdev_max_registers()
    assert "REGDEV_MAX_REGISTERS not found" in capsys.readouterr().err


# ── descriptor table and generated output ─────────────────────────────────────

def test_desc_table_encodes_each_register(cli):
    lines = cli.registers_to_desc_table(
        [{"id": "temperature", "reg": 1, "scale": 0.1, "signed": True},
         {"id": "setpoint", "reg": 0x10, "type": "holding", "datatype": "integer"}], "env-1")
    assert lines == [
        "static const ModbusRegisterDesc _regs_env_1[] = {",
        '    { "temperature", 1, false, true, 0.1f, REGDEV_FLOAT },',
        '    { "setpoint", 16, true, false, 1.0f, REGDEV_INTEGER },',
        "};",
    ]


def test_generate_emits_table_and_properties_for_register_device(sandbox):
    regs = [{"id": "temperature", "reg": 1, "scale": 0.1, "unit": "°C"},
            {"id": "relative-humidity", "reg": 2, "scale": 0.1, "unit": "%"}]
    sandbox.generate(device_doc([reg_node(regs)], buses=RS485))
    cpp = sandbox.homie_cpp
    assert "static const ModbusRegisterDesc _regs_env1[] = {" in cpp
    assert 'ModbusRegisterDevice env1("env1", get_serial1(), 44, 43, 1, _regs_env1, 2);' in cpp
    props = sandbox.homie_yml["nodes"][0]["properties"]
    assert [p["id"] for p in props] == ["temperature", "relative-humidity"]


def test_registers_on_another_component_are_ignored_with_a_warning(sandbox, capsys):
    node = {"component": "modbus/sht20", "id": "t1", "registers": [{"id": "x", "reg": 1}]}
    sandbox.generate(device_doc([node], buses=RS485))
    assert "the list is IGNORED" in capsys.readouterr().err


# ── unit audit ────────────────────────────────────────────────────────────────

def test_numeric_property_without_unit_warns(sandbox, capsys):
    sandbox.generate(device_doc([reg_node([{"id": "count", "reg": 1, "datatype": "integer"}])],
                                buses=RS485))
    assert "numeric property 'env1/count' has no 'unit'" in capsys.readouterr().err


def test_empty_unit_declares_dimensionless_and_silences_the_warning(sandbox, capsys):
    sandbox.generate(device_doc([reg_node([{"id": "pf", "reg": 1, "unit": ""}])], buses=RS485))
    assert "has no 'unit'" not in capsys.readouterr().err


def test_boolean_property_needs_no_unit(sandbox, capsys):
    sandbox.generate(device_doc([reg_node([{"id": "alarm", "reg": 1, "datatype": "boolean"}])],
                                buses=RS485))
    assert "has no 'unit'" not in capsys.readouterr().err


def test_shipped_components_declare_units_on_numeric_properties(sandbox, capsys):
    doc = device_doc([{"component": "modbus/sht20", "id": "t1"},
                      {"component": "sim/battery_sim", "id": "bat"}], buses=RS485)
    sandbox.generate(doc)
    assert "has no 'unit'" not in capsys.readouterr().err
