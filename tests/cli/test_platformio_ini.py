"""The lean .ebus/platformio.ini: lib_deps pruning and per-component source filtering."""

import configparser

import pytest

from helpers import device_doc, relay

ENV = "esp32-poe-iso"
I2C = {"i2c": {"pin_sda": 13, "pin_scl": 16}}


def lean_ini(cli) -> configparser.RawConfigParser:
    ini = configparser.RawConfigParser()
    ini.read(cli.GENERATED_INI, encoding="utf-8")
    return ini


def lib_deps(ini) -> list:
    return [l.strip() for l in ini.get("common", "lib_deps").splitlines() if l.strip()]


def component_lib_deps(cli, component) -> list:
    return cli.load_yaml_file(cli.LIB_DIR / f"{component}.yml")["generator"]["lib_deps"]


def test_catalog_holds_every_component_lib_dep(cli, sandbox):
    catalog = cli.collect_optional_lib_catalog()
    for comp in ("i2c/bme280", "ble/gatt_device", "spi/atm90e32_energy_meter"):
        assert set(component_lib_deps(cli, comp)) <= catalog


def test_unused_optional_libraries_are_left_out(cli, sandbox):
    sandbox.generate(device_doc([relay()]))
    deps = lib_deps(lean_ini(cli))
    assert not set(deps) & cli.collect_optional_lib_catalog()
    assert "bblanchon/ArduinoJson @ 7.4.3" in deps


def test_used_component_libraries_are_added(cli, sandbox):
    sandbox.generate(device_doc([{"component": "i2c/bme280", "id": "env",
                                  "config": {"address": "0x76"}}], buses=I2C))
    deps = lib_deps(lean_ini(cli))
    for dep in component_lib_deps(cli, "i2c/bme280"):
        assert deps.count(dep) == 1
    for dep in component_lib_deps(cli, "ble/gatt_device"):
        if dep not in component_lib_deps(cli, "i2c/bme280"):
            assert dep not in deps


def test_lean_ini_keeps_only_the_selected_env(cli, sandbox):
    sandbox.generate(device_doc([relay()]))
    envs = [s for s in lean_ini(cli).sections() if s.startswith("env:")]
    assert envs == [f"env:{ENV}"]


def test_lean_ini_builds_into_the_cli_tree(cli, sandbox):
    sandbox.generate(device_doc([relay()]))
    ini = lean_ini(cli)
    assert ini.get("platformio", "build_dir") == str(cli.PIO_BUILD_DIR)
    assert ini.get("platformio", "libdeps_dir") == str(cli.PIO_LIBDEPS_DIR)


def test_component_filter_lists_used_components(cli, sandbox):
    sandbox.generate(device_doc([relay()]))
    assert lean_ini(cli).get(f"env:{ENV}", "custom_ebus_components") == "gpio/digital_output"


def test_source_closure_follows_includes_to_other_components(cli, sandbox):
    closure = cli.component_source_closure(["modbus/sht20"])
    assert closure == sorted(set(closure))
    assert {"modbus/sht20", "modbus/register_device"} <= set(closure)
    assert "i2c/bme280" not in closure


def test_unknown_environment_writes_no_lean_ini(cli, sandbox):
    sandbox.generate(device_doc([relay()], environment="no-such-env"))
    assert not cli.GENERATED_INI.exists()


def test_changed_lib_deps_remove_the_stale_env_build_dir(cli, sandbox, capsys):
    sandbox.generate(device_doc([relay()]))
    stale = cli.PIO_BUILD_DIR / ENV
    stale.mkdir(parents=True)
    sandbox.generate(device_doc([{"component": "i2c/bme280", "id": "env",
                                  "config": {"address": "0x76"}}], buses=I2C))
    assert not stale.exists()
    assert "lib_deps changed" in capsys.readouterr().out


def test_unchanged_lib_deps_keep_the_env_build_dir(cli, sandbox):
    sandbox.generate(device_doc([relay()]))
    kept = cli.PIO_BUILD_DIR / ENV
    kept.mkdir(parents=True)
    sandbox.generate(device_doc([relay("other", 15)]))
    assert kept.exists()


@pytest.mark.parametrize("env", ["esp32-poe-iso", "esp32-s3-devkitc-1-arduino2"])
def test_monitor_speed_comes_from_the_tracked_ini(cli, sandbox, env):
    assert cli.get_monitor_speed(env) == 115200


def test_monitor_speed_falls_back_to_env_section_then_default(cli, sandbox, monkeypatch):
    ini = sandbox.root / "pio.ini"
    ini.write_text("[env]\nmonitor_speed = 74880\n[env:a]\nmonitor_speed = 921600\n"
                   "[env:b]\nboard = x\n[env:c]\nmonitor_speed = fast\n", encoding="utf-8")
    monkeypatch.setattr(cli, "PLATFORMIO_INI", ini)
    assert cli.get_monitor_speed("a") == 921600
    assert cli.get_monitor_speed("b") == 74880
    assert cli.get_monitor_speed("c", default=9600) == 9600


def test_local_ini_overrides_tracked_ini(cli, sandbox, monkeypatch):
    ini = sandbox.root / "pio.ini"
    ini.write_text("[env:a]\nmonitor_speed = 115200\n", encoding="utf-8")
    (sandbox.root / "platformio_local.ini").write_text("[env:a]\nmonitor_speed = 9600\n",
                                                      encoding="utf-8")
    monkeypatch.setattr(cli, "PLATFORMIO_INI", ini)
    assert cli.get_monitor_speed("a") == 9600
