"""peripherals:, project components, device.yml lib_deps: and pinned library versions."""

import configparser
from pathlib import Path

import pytest
import yaml

from helpers import device_doc, relay

ENV = "esp32-poe-iso"
URL = "https://price.example.com/openadr3/3.1.0"
SPI = {"spi": {"pin_sck": 14, "pin_mosi": 2}}
ILI9341_DEPS = ["adafruit/Adafruit BusIO @ 1.17.4", "adafruit/Adafruit GFX Library @ 1.13.0",
                "adafruit/Adafruit ILI9341 @ 1.7.0"]


def lcd(pid="lcd", **config):
    return {"id": pid, "component": "spi/ili9341",
            "config": {"pin_cs": 5, "pin_dc": 15, **config}}


def peripheral_doc(peripherals, buses=SPI, **device):
    doc = device_doc(buses=buses, **device)
    doc["peripherals"] = peripherals
    return doc


def lean_ini(cli) -> configparser.RawConfigParser:
    ini = configparser.RawConfigParser()
    ini.read(cli.GENERATED_INI, encoding="utf-8")
    return ini


def lib_deps(cli) -> list:
    return [l.strip() for l in lean_ini(cli).get("common", "lib_deps").splitlines() if l.strip()]


def components(cli) -> list:
    return lean_ini(cli).get(f"env:{ENV}", "custom_ebus_components").split()


def peripherals_h(cli) -> list:
    return (cli.GEN_SRC_DIR / "peripherals.h").read_text(encoding="utf-8").splitlines()


def peripherals_cpp(cli) -> list:
    return (cli.GEN_SRC_DIR / "peripherals.cpp").read_text(encoding="utf-8").splitlines()


def refused(sandbox, capsys, doc) -> str:
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    return capsys.readouterr().err


def project_component(sandbox, rel, yml, header="#pragma once\n", cpp=""):
    """Write lib/<bus>/<name>.{h,cpp,yml} into the sandbox project."""
    base = sandbox.project / "lib" / rel
    base.parent.mkdir(parents=True, exist_ok=True)
    base.with_suffix(".h").write_text(header, encoding="utf-8")
    base.with_suffix(".cpp").write_text(cpp or f'#include <{rel}.h>\n', encoding="utf-8")
    base.with_suffix(".yml").write_text(yaml.safe_dump(yml, sort_keys=False), encoding="utf-8")


def probe_yml(kind="sensor", lib_dep="owner/Probe Lib @ 2.0.0"):
    return {"peripheral": {"kind": kind},
            "generator": {"class": "Probe", "header": "custom/probe.h",
                          "constructor": '("{id}", {config.pin})', "lib_deps": [lib_dep]}}


# ── lib_dep_key ────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("spec,key", [
    ("adafruit/Adafruit BusIO @ 1.17.4", ("adafruitbusio", "1.17.4")),
    ("adafruit/Adafruit BusIO@1.17.4", ("adafruitbusio", "1.17.4")),
    ("owner/Lib @ 1.2.3-rc.1", ("lib", "1.2.3-rc.1")),
    ("owner/Lib @ v1.2.3", ("lib", "1.2.3")),
    ("https://github.com/adafruit/Adafruit_BusIO.git#1.17.4", ("adafruitbusio", "1.17.4")),
    ("https://github.com/256dpi/arduino-mqtt.git#v2.5.3", ("arduinomqtt", "2.5.3")),
    ("https://github.com/CircuitSetup/ATM90E32.git#467858f48b1ef5610f489218bdb153b2df8556d6",
     ("atm90e32", "467858f48b1ef5610f489218bdb153b2df8556d6")),
    ("oa3=https://github.com/grid-coordination/cpp-oa3.git#v0.1.0", ("oa3", "0.1.0")),
    ("oa3=symlink:///src/cpp-oa3", ("oa3", "local /src/cpp-oa3")),
    ("symlink:///src/cpp-oa3/", ("cppoa3", "local /src/cpp-oa3/")),
    ("file:///src/My_Lib", ("mylib", "local /src/My_Lib")),
    ("Wire", ("wire", "framework")),
    (" SPI ", ("spi", "framework")),
])
def test_lib_dep_key(cli, spec, key):
    assert cli.lib_dep_key(spec) == key


@pytest.mark.parametrize("spec", [
    "owner/Lib",
    "owner/Lib @ ^1.0.0",
    "owner/Lib @ ~1.0.0",
    "owner/Lib @ 1.x",
    "owner/Lib @ <2",
    "owner/Lib @ 1.0.0 - 2.0.0",
    "https://github.com/owner/Lib.git",
    "https://github.com/owner/Lib.git#",
])
def test_unpinned_entries_have_no_version(cli, spec):
    assert cli.lib_dep_key(spec)[1] is None


@pytest.mark.parametrize("spec", ["owner/Lib @ >=1.0.0", "owner/Lib @ <=2.0.0"])
def test_comparison_ranges_have_no_version(cli, spec):
    assert cli.lib_dep_key(spec)[1] is None


def test_a_tag_with_v_names_the_release(cli):
    assert cli.lib_dep_key("owner/Lib @ v1.2.3") == cli.lib_dep_key("owner/Lib @ 1.2.3")
    assert (cli.lib_dep_key("https://github.com/owner/Lib.git#v1.2.3")
            == cli.lib_dep_key("owner/Lib @ 1.2.3"))


# ── check_lib_deps ─────────────────────────────────────────────────────────────

def lib_deps_problems(cli, capsys, deps, sources=None) -> str:
    with pytest.raises(SystemExit):
        cli.check_lib_deps(deps, sources or {})
    err = capsys.readouterr().err
    assert "ERROR: lib_deps:" in err
    return err


def test_pinned_entries_are_accepted(cli):
    cli.check_lib_deps(["Wire", "adafruit/Adafruit BusIO @ 1.17.4",
                        "oa3=symlink:///src/cpp-oa3",
                        "https://github.com/256dpi/arduino-mqtt.git#v2.5.3"], {})


def test_an_unpinned_entry_is_refused(cli, capsys):
    err = lib_deps_problems(cli, capsys, ["owner/Lib @ ^1.0.0"],
                            {"owner/Lib @ ^1.0.0": "device.yml lib_deps:"})
    assert "'owner/Lib @ ^1.0.0' (device.yml lib_deps:) names no exact version" in err


def test_an_unattributed_entry_is_named_as_the_build(cli, capsys):
    assert "'owner/Lib' (the build) names no exact version" in \
        lib_deps_problems(cli, capsys, ["owner/Lib"])


@pytest.mark.parametrize("git", ["https://github.com/adafruit/Adafruit_BusIO.git#1.17.4",
                                 "https://github.com/adafruit/Adafruit_BusIO.git#v1.17.4"])
def test_registry_and_git_at_one_version_are_accepted(cli, git):
    cli.check_lib_deps(["adafruit/Adafruit BusIO @ 1.17.4", git], {})


def test_registry_and_git_at_two_versions_are_refused(cli, capsys):
    reg, git = "adafruit/Adafruit BusIO @ 1.17.4", "https://github.com/adafruit/Adafruit_BusIO.git#1.16.0"
    err = lib_deps_problems(cli, capsys, [reg, git],
                            {reg: "component spi/ili9341", git: "device.yml lib_deps:"})
    assert (f"'{git}' (device.yml lib_deps:) and '{reg}' (component spi/ili9341) are the same "
            f"library at different versions") in err


# ── every repo component pins its libraries ────────────────────────────────────

REPO = Path(__file__).resolve().parents[2]
COMPONENT_YMLS = sorted(REPO.glob("lib/*/*.yml")) + sorted(REPO.glob("examples/*/lib/*/*.yml"))


def component_deps():
    for path in COMPONENT_YMLS:
        gen = (yaml.safe_load(path.read_text(encoding="utf-8")) or {}).get("generator") or {}
        for dep in gen.get("lib_deps") or []:
            yield pytest.param(dep, id=f"{path.relative_to(REPO)}:{dep}")


@pytest.mark.parametrize("dep", list(component_deps()))
def test_every_component_lib_dep_is_pinned(cli, dep):
    assert cli.lib_dep_key(dep)[1] is not None


# ── device.yml lib_deps: ───────────────────────────────────────────────────────

def test_project_lib_deps_join_the_build(cli, sandbox):
    doc = device_doc(nodes=[relay()])
    doc["lib_deps"] = [" owner/App Lib @ 3.1.0 ", "Wire"]
    sandbox.generate(doc)
    deps = lib_deps(cli)
    assert "owner/App Lib @ 3.1.0" in deps
    assert "Wire" in deps


@pytest.mark.parametrize("value", ["owner/App Lib @ 3.1.0", {"a": "b"}, [5], ["ok/Lib @ 1.0.0", ""],
                                   [["owner/Lib @ 1.0.0"]]])
def test_project_lib_deps_must_be_a_list_of_strings(cli, sandbox, capsys, value):
    doc = device_doc(nodes=[relay()])
    doc["lib_deps"] = value
    assert "device.yml lib_deps: must be a list of PlatformIO library specs" in \
        refused(sandbox, capsys, doc)


def test_unpinned_project_lib_dep_is_refused(cli, sandbox, capsys):
    doc = device_doc(nodes=[relay()])
    doc["lib_deps"] = ["owner/App Lib"]
    assert "'owner/App Lib' (device.yml lib_deps:) names no exact version" in \
        refused(sandbox, capsys, doc)


def test_project_lib_dep_conflicting_with_a_component_is_refused(cli, sandbox, capsys):
    doc = peripheral_doc([lcd()])
    doc["lib_deps"] = ["adafruit/Adafruit BusIO @ 1.17.3"]
    err = refused(sandbox, capsys, doc)
    assert ("'adafruit/Adafruit BusIO @ 1.17.3' (device.yml lib_deps:) and "
            "'adafruit/Adafruit BusIO @ 1.17.4' (component spi/ili9341) are the same library "
            "at different versions") in err


def test_project_lib_dep_at_the_components_version_is_accepted(cli, sandbox):
    doc = peripheral_doc([lcd()])
    doc["lib_deps"] = ["https://github.com/adafruit/Adafruit_BusIO.git#v1.17.4"]
    sandbox.generate(doc)
    assert "adafruit/Adafruit BusIO @ 1.17.4" in lib_deps(cli)


# ── project components ─────────────────────────────────────────────────────────

def test_project_component_is_found(cli, sandbox):
    project_component(sandbox, "custom/probe", probe_yml())
    assert cli.find_component_yml("custom/probe") == sandbox.project / "lib" / "custom" / "probe.yml"
    assert cli.find_component_yml("probe") == sandbox.project / "lib" / "custom" / "probe.yml"
    assert cli.find_component_yml("i2c/bme280") == cli.LIB_DIR / "i2c" / "bme280.yml"


def test_project_component_is_staged_and_built(cli, sandbox):
    project_component(sandbox, "custom/probe", probe_yml())
    sandbox.generate(peripheral_doc([{"id": "probe", "component": "custom/probe",
                                      "config": {"pin": 4}}]))
    staged = cli.GEN_LIB_DIR / "custom"
    assert sorted(p.name for p in staged.iterdir()) == ["library.json", "probe.cpp", "probe.h",
                                                       "probe.yml"]
    library = yaml.safe_load((staged / "library.json").read_text(encoding="utf-8"))
    assert library["name"] == "project-custom"
    assert library["build"]["extraScript"] == str(cli.ROOT / "scripts" / "lib_src_filter.py")
    deps = lib_deps(cli)
    assert f"project-custom=symlink://{staged}" in deps
    assert "owner/Probe Lib @ 2.0.0" in deps
    assert "custom/probe" in components(cli)
    assert "Probe probe(\"probe\", 4);" in peripherals_cpp(cli)
    assert "#include <custom/probe.h>" in peripherals_h(cli)


def test_project_lib_without_components_leaves_no_staged_lib(cli, sandbox):
    sandbox.generate(device_doc(nodes=[relay()]))
    assert not cli.GEN_LIB_DIR.exists()
    assert not any(d.startswith("project-") for d in lib_deps(cli))


def test_project_component_named_like_a_repo_one_is_refused(cli, sandbox, capsys):
    project_component(sandbox, "i2c/bme280", probe_yml())
    err = refused(sandbox, capsys, device_doc(nodes=[relay()]))
    assert "has component(s) named like the firmware's own: i2c/bme280. Rename the project's." in err


def test_project_src_including_a_repo_component_pulls_it_in(cli, sandbox):
    src = sandbox.project / "src"
    src.mkdir()
    (src / "app.cpp").write_text("#include <Arduino.h>\n#include <i2c/bme280.h>\n",
                                 encoding="utf-8")
    sandbox.generate(device_doc(nodes=[relay()]))
    assert "i2c/bme280" in components(cli)
    assert "https://github.com/adafruit/Adafruit_BME280_Library.git#2.3.0" in lib_deps(cli)


# ── optional bus keys ──────────────────────────────────────────────────────────

CTOR = '({bus.spi.pin_sck}, {bus.spi.pin_miso|-1}, {bus.spi.pin_mosi})'


def test_optional_bus_keys_are_not_required(cli):
    assert cli.required_bus_keys(CTOR) == {("spi", "pin_sck"), ("spi", "pin_mosi")}


def test_optional_bus_key_takes_its_default_when_absent(cli):
    assert cli.fmt_ctor(CTOR, "lcd", {}, None, SPI) == "(14, -1, 2)"


def test_optional_bus_key_takes_the_bus_value_when_present(cli):
    buses = {"spi": {**SPI["spi"], "pin_miso": 12}}
    assert cli.fmt_ctor(CTOR, "lcd", {}, None, buses) == "(14, 12, 2)"


def test_present_bus_keys_include_optional_keys_only_when_set(cli):
    assert cli.present_bus_keys(CTOR, SPI) == {("spi", "pin_sck"), ("spi", "pin_mosi")}
    buses = {"spi": {**SPI["spi"], "pin_miso": 12}}
    assert cli.present_bus_keys(CTOR, buses) == {("spi", "pin_sck"), ("spi", "pin_miso"),
                                                 ("spi", "pin_mosi")}
    assert cli.present_bus_keys(CTOR, {"spi": {**SPI["spi"], "pin_miso": None}}) == \
        {("spi", "pin_sck"), ("spi", "pin_mosi")}


def test_set_optional_bus_pin_joins_the_gpio_check(cli, sandbox, capsys):
    doc = peripheral_doc([lcd()], buses={"spi": {**SPI["spi"], "pin_miso": 15}})
    err = refused(sandbox, capsys, doc)
    assert "GPIO 15:" in err
    assert "buses.spi.pin_miso" in err
    assert "peripheral 'lcd' config.pin_dc" in err


# ── peripherals: ───────────────────────────────────────────────────────────────

def test_peripheral_emission(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd()]))
    head = peripherals_h(cli)
    assert head[1:4] == ["#pragma once", "", "#include <peripheral/peripheral.h>"]
    assert "#include <spi/ili9341.h>" in head
    assert head[-1] == "extern Ili9341 lcd;"
    cpp = peripherals_cpp(cli)
    assert '#include "peripherals.h"' in cpp
    assert 'Ili9341 lcd("lcd", 14, -1, 2, 5, 15, 1, 40000000);' in cpp
    assert "Peripheral* const peripheral_table[] = {&lcd};" in cpp
    assert cpp[-1] == "const size_t peripheral_count = 1;"


def test_two_peripherals_are_tabled_in_order(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd("lcd-b", pin_cs=4, pin_dc=16), lcd("lcd-a")]))
    cpp = peripherals_cpp(cli)
    assert "Peripheral* const peripheral_table[] = {&lcd_b, &lcd_a};" in cpp
    assert cpp[-1] == "const size_t peripheral_count = 2;"
    assert peripherals_h(cli).count("#include <spi/ili9341.h>") == 1


def test_peripherals_files_are_written_without_peripherals(cli, sandbox):
    sandbox.generate(device_doc(nodes=[relay()]))
    head = peripherals_h(cli)
    assert "#include <peripheral/peripheral.h>" in head
    assert not any(l.startswith("extern") for l in head)
    cpp = peripherals_cpp(cli)
    assert "Peripheral* const peripheral_table[] = {nullptr};" in cpp
    assert cpp[-1] == "const size_t peripheral_count = 0;"


def test_config_values_override_config_defaults(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd(rotation=3, spi_hz=30000000)]))
    assert 'Ili9341 lcd("lcd", 14, -1, 2, 5, 15, 3, 30000000);' in peripherals_cpp(cli)


def test_peripheral_brings_its_libraries_and_component(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd()]))
    deps = lib_deps(cli)
    for dep in ILI9341_DEPS:
        assert deps.count(dep) == 1
    assert "spi/ili9341" in components(cli)


def test_peripheral_in_a_standalone_build(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd()], role="standalone"))
    assert 'Ili9341 lcd("lcd", 14, -1, 2, 5, 15, 1, 40000000);' in peripherals_cpp(cli)
    assert "adafruit/Adafruit ILI9341 @ 1.7.0" in lib_deps(cli)
    assert "spi/ili9341" in components(cli)


@pytest.mark.parametrize("pid", ["LCD", "lcd_1", "-lcd"])
def test_peripheral_id_must_be_a_homie_id(cli, sandbox, capsys, pid):
    err = refused(sandbox, capsys, peripheral_doc([lcd(pid)]))
    assert f"device.yml peripheral id '{pid}' must be a Homie id" in err


@pytest.mark.parametrize("entry", [{"component": "spi/ili9341"}, {"id": "lcd"}, "lcd"])
def test_peripheral_needs_an_id_and_a_component(cli, sandbox, capsys, entry):
    assert "device.yml peripherals: entry needs an id and a component" in \
        refused(sandbox, capsys, peripheral_doc([entry]))


def test_peripherals_must_be_a_list(cli, sandbox, capsys):
    assert "device.yml peripherals: must be a list of entries" in \
        refused(sandbox, capsys, peripheral_doc(lcd()))


def test_unknown_peripheral_component_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([{"id": "lcd", "component": "spi/nope"}]))
    assert "peripheral 'lcd' references component 'spi/nope', which matches no" in err


def test_two_peripherals_with_one_id_are_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd(), lcd(pin_cs=4, pin_dc=16)]))
    assert "id 'lcd' is used more than once in peripherals: and nodes:" in err


def test_a_node_with_a_peripherals_id_is_refused(cli, sandbox, capsys):
    doc = peripheral_doc([lcd()])
    doc["nodes"] = [relay("lcd", pin=4)]
    err = refused(sandbox, capsys, doc)
    assert ("node id 'lcd' is used by more than one node — first by component 'spi/ili9341', "
            "again by component 'gpio/digital_output'") in err


def test_a_node_component_under_peripherals_is_refused(cli, sandbox, capsys):
    doc = peripheral_doc([{"id": "out", "component": "gpio/digital_output",
                           "config": {"pin": 14, "active_low": False, "initial_state": False}}])
    err = refused(sandbox, capsys, doc)
    assert "peripheral 'out': component 'gpio/digital_output' is not a peripheral" in err
    assert "list it under nodes:" in err


def test_a_peripheral_component_under_nodes_is_refused(cli, sandbox, capsys):
    doc = device_doc(nodes=[lcd()], buses=SPI)
    err = refused(sandbox, capsys, doc)
    assert "node 'lcd': component 'spi/ili9341' is a peripheral with no Homie node; list it under peripherals:" in err


def test_missing_peripheral_config_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys,
                  peripheral_doc([{"id": "lcd", "component": "spi/ili9341",
                                   "config": {"pin_cs": 5}}]))
    assert "peripheral 'lcd' (component 'spi/ili9341') config: block problem" in err
    assert "missing required key(s): pin_dc" in err


def test_missing_bus_key_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd()], buses={"spi": {"pin_sck": 14}}))
    assert "buses.spi.pin_mosi is missing" in err


def test_peripheral_pin_clashing_with_a_bus_pin_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd(pin_cs=14)]))
    assert "GPIO 14:" in err
    assert "buses.spi.pin_sck" in err
    assert "peripheral 'lcd' config.pin_cs" in err


def test_peripheral_pin_clashing_with_a_node_pin_is_refused(cli, sandbox, capsys):
    doc = peripheral_doc([lcd()])
    doc["nodes"] = [relay(pin=5)]
    err = refused(sandbox, capsys, doc)
    assert "GPIO 5:" in err
    assert "peripheral 'lcd' config.pin_cs" in err
    assert "node 'relay1' config.pin" in err


# ── source-closure lib_deps ────────────────────────────────────────────────────

def test_component_reached_through_an_include_brings_its_libraries(cli, sandbox):
    yml = probe_yml(kind="display")
    yml["generator"].update({"class": "Screen", "header": "custom/screen.h"})
    project_component(sandbox, "custom/screen", yml,
                      header="#pragma once\n#include <spi/ili9341.h>\n")
    sandbox.generate(peripheral_doc([{"id": "screen", "component": "custom/screen",
                                      "config": {"pin": 4}}]))
    assert {"custom/screen", "spi/ili9341"} <= set(components(cli))
    deps = lib_deps(cli)
    for dep in ILI9341_DEPS:
        assert deps.count(dep) == 1


def test_closure_library_conflict_names_the_reached_component(cli, sandbox, capsys):
    yml = probe_yml(kind="display", lib_dep="adafruit/Adafruit GFX Library @ 1.12.0")
    yml["generator"].update({"class": "Screen", "header": "custom/screen.h"})
    project_component(sandbox, "custom/screen", yml,
                      header="#pragma once\n#include <spi/ili9341.h>\n")
    err = refused(sandbox, capsys,
                  peripheral_doc([{"id": "screen", "component": "custom/screen",
                                   "config": {"pin": 4}}]))
    assert ("'adafruit/Adafruit GFX Library @ 1.13.0' (component spi/ili9341) and "
            "'adafruit/Adafruit GFX Library @ 1.12.0' (component custom/screen)") in err


# ── oa3.display ────────────────────────────────────────────────────────────────

def oa3_doc(display, peripherals, **device):
    doc = peripheral_doc(peripherals, **device)
    doc["oa3"] = {"base_url": URL, "program_name": "price", "display": display}
    return doc


@pytest.mark.parametrize("role", ["device", "standalone"])
def test_oa3_display_is_copied_into_config(cli, sandbox, role):
    sandbox.generate(oa3_doc("lcd", [lcd()], role=role))
    assert sandbox.config_yml["oa3"] == {"base_url": URL, "program_name": "price",
                                         "display": "lcd"}


def test_oa3_display_naming_no_peripheral_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, oa3_doc("lcd", []))
    assert "oa3.display 'lcd' names no display under peripherals: (none listed)" in err


def test_oa3_display_naming_another_id_lists_the_displays(cli, sandbox, capsys):
    err = refused(sandbox, capsys, oa3_doc("screen", [lcd()]))
    assert "oa3.display 'screen' names no display under peripherals: (lcd)" in err


def test_oa3_display_naming_a_node_is_refused(cli, sandbox, capsys):
    doc = oa3_doc("relay1", [])
    doc["nodes"] = [relay()]
    assert "oa3.display 'relay1' names no display" in refused(sandbox, capsys, doc)


def test_oa3_display_naming_a_non_display_peripheral_is_refused(cli, sandbox, capsys):
    project_component(sandbox, "custom/probe", probe_yml())
    err = refused(sandbox, capsys,
                  oa3_doc("probe", [{"id": "probe", "component": "custom/probe",
                                     "config": {"pin": 4}}]))
    assert "oa3.display 'probe' names no display under peripherals: (none listed)" in err


@pytest.mark.parametrize("spec", ["https://github.com/o/Lib.git#main",
                                  "https://github.com/o/Lib.git#master"])
def test_a_branch_ref_names_no_version(cli, spec):
    assert cli.lib_dep_key(spec)[1] is None


def test_project_lib_dep_conflicting_with_the_firmware_base_is_refused(cli, sandbox, capsys):
    doc = device_doc()
    doc["lib_deps"] = ["bblanchon/ArduinoJson @ 7.0.0"]
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    err = capsys.readouterr().err
    assert "bblanchon/ArduinoJson @ 7.0.0" in err and "the firmware's platformio.ini" in err


def test_long_peripheral_id_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd("l" * 32)]))
    assert "longer than 31 characters" in err


def test_missing_peripheral_bus_key_names_the_peripheral(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd()], buses={"spi": {"pin_sck": 14}}))
    assert "peripheral 'lcd'" in err


# ── touch (i2c/ns2009) and peripheral references ──────────────────────────────

SPI_I2C = {"spi": {"pin_sck": 14, "pin_mosi": 2}, "i2c": {"pin_sda": 13, "pin_scl": 33}}


def touch(display="lcd", **config):
    return {"id": "touch", "component": "i2c/ns2009", "config": {"display": display, **config}}


def test_touch_over_a_display_is_emitted_with_olimex_defaults(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd(), touch()], buses=SPI_I2C, role="standalone"))
    cpp = (cli.GEN_SRC_DIR / "peripherals.cpp").read_text()
    assert ('Ns2009 touch("touch", 13, 33, 72, "lcd", 200, 3850, 400, 3920, false, false, '
            'true, 20, 150);') in cpp
    assert "Peripheral* const peripheral_table[] = {&lcd, &touch};" in cpp
    assert "extern Ns2009 touch;" in (cli.GEN_SRC_DIR / "peripherals.h").read_text()


def test_touch_listed_before_its_display_is_accepted(cli, sandbox):
    sandbox.generate(peripheral_doc([touch(), lcd()], buses=SPI_I2C, role="standalone"))


def test_touch_naming_no_display_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd(), touch("screen")], buses=SPI_I2C))
    assert "peripheral 'touch': config.display 'screen' names no display" in err
    assert "(lcd)" in err


def test_touch_naming_a_non_display_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd(), touch("touch")], buses=SPI_I2C))
    assert "config.display 'touch' names no display" in err


def test_touch_calibration_can_be_set(cli, sandbox):
    sandbox.generate(peripheral_doc([lcd(), touch(x_min=100, invert_y=False, swap_xy=True)],
                                    buses=SPI_I2C))
    cpp = (cli.GEN_SRC_DIR / "peripherals.cpp").read_text()
    assert '"lcd", 100, 3850, 400, 3920, true, false, false, 20, 150);' in cpp


# ── a Homie node over a peripheral (i2c/bme280) ───────────────────────────────

I2C = {"i2c": {"pin_sda": 13, "pin_scl": 16}}


def env(pid="env", **config):
    return {"id": pid, "component": "i2c/bme280", "config": {"address": 0x77, **config}}


def homie_cpp_text(cli):
    return (cli.GEN_SRC_DIR / "homie.cpp").read_text()


def peripherals_cpp_text(cli):
    return (cli.GEN_SRC_DIR / "peripherals.cpp").read_text()


def test_bme280_node_shorthand_creates_driver_and_node(cli, sandbox):
    sandbox.generate(device_doc([{"component": "i2c/bme280", "id": "environment",
                                  "config": {"address": 0x77}}], buses=I2C))
    assert 'Bme280 environment_peripheral("environment", 13, 16, 119, 10000);' in \
        peripherals_cpp_text(cli)
    assert "Peripheral* const peripheral_table[] = {&environment_peripheral};" in \
        peripherals_cpp_text(cli)
    assert 'Bme280Node environment("environment", environment_peripheral);' in homie_cpp_text(cli)
    assert '#include "peripherals.h"' in homie_cpp_text(cli)
    assert '"temperature", "Temperature", "float", "°C"' in homie_cpp_text(cli)


def test_bme280_peripheral_published_by_a_node(cli, sandbox):
    doc = peripheral_doc([env(interval_ms=5000)], buses=I2C,
                         nodes=[{"id": "environment", "peripheral": "env"}])
    sandbox.generate(doc)
    assert 'Bme280 env("env", 13, 16, 119, 5000);' in peripherals_cpp_text(cli)
    assert "extern Bme280 env;" in (cli.GEN_SRC_DIR / "peripherals.h").read_text()
    assert 'Bme280Node environment("environment", env);' in homie_cpp_text(cli)


def test_bme280_peripheral_without_a_node_publishes_nothing(cli, sandbox):
    sandbox.generate(peripheral_doc([env()], buses=I2C, role="standalone"))
    assert 'Bme280 env("env", 13, 16, 119, 10000);' in peripherals_cpp_text(cli)
    assert "Bme280Node" not in homie_cpp_text(cli)


def test_node_naming_an_unknown_peripheral_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([env()], buses=I2C,
                                                  nodes=[{"id": "environment",
                                                          "peripheral": "envx"}]))
    assert "names peripheral 'envx', which is not under peripherals:" in err


def test_node_naming_a_peripheral_takes_no_config(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([env()], buses=I2C,
                                                  nodes=[{"id": "environment", "peripheral": "env",
                                                          "config": {"address": 0x76}}]))
    assert "takes no component: or config: of its own" in err


def test_node_over_a_peripheral_without_a_node_class_is_refused(cli, sandbox, capsys):
    err = refused(sandbox, capsys, peripheral_doc([lcd()], nodes=[{"id": "screen",
                                                                   "peripheral": "lcd"}]))
    assert "is a peripheral with no Homie node" in err


def test_bme280_shorthand_still_checks_its_config(cli, sandbox, capsys):
    err = refused(sandbox, capsys, device_doc([{"component": "i2c/bme280", "id": "environment"}],
                                              buses=I2C))
    assert "missing required key(s): address" in err
