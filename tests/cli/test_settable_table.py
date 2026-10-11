"""settable_table_capacity(): the settable subscriber table sized from device.yml."""

import re

import pytest
import yaml

from conftest import REPO
from helpers import device_doc, relay


def capacity(cpp: str) -> int:
    m = re.search(r"uint16_t settable_table_capacity\(\) \{\s*return (\d+);", cpp)
    assert m, "settable_table_capacity() not emitted"
    return int(m.group(1))


def load(rel: str) -> dict:
    return yaml.safe_load((REPO / rel).read_text(encoding="utf-8"))


@pytest.mark.parametrize("settables,expected", [(0, 8), (3, 8), (4, 9), (8, 13), (100, 105)])
def test_capacity_is_count_plus_state_watch_plus_margin_with_a_floor(cli, settables, expected):
    assert cli.settable_table_capacity(settables) == expected


def test_demo_example(sandbox):
    sandbox.generate(load("examples/demo/device.yml"))
    assert "// 1 settable property + 1 $state watch" in sandbox.homie_cpp
    assert capacity(sandbox.homie_cpp) == 8


def test_device_with_no_settables_gets_the_floor(sandbox):
    regs = [{"id": "t", "reg": 1, "unit": "C"}, {"id": "h", "reg": 2, "unit": "%"}]
    node = {"component": "modbus/register_device", "id": "env1",
            "config": {"address": 1}, "registers": regs}
    sandbox.generate(device_doc([node], buses={"rs485": {"pin_a": 44, "pin_b": 43}}))
    assert "// 0 settable properties" in sandbox.homie_cpp
    assert capacity(sandbox.homie_cpp) == 8


def test_bridge_bess_example_with_a_read_only_child(sandbox):
    sandbox.generate(load("examples/bridge-bess/device.yml"))
    assert "// 0 settable properties" in sandbox.homie_cpp
    assert capacity(sandbox.homie_cpp) == 8


def test_bms_example_counts_every_settable_in_its_component(cli, sandbox, monkeypatch):
    # The example's component lives in examples/bms/, outside the lib/<bus>/ search path.
    real = cli.find_component_yml
    monkeypatch.setattr(cli, "find_component_yml",
                        lambda c: REPO / "examples/bms/serial_bms.yml" if c == "ble/serial_bms"
                        else real(c))
    sandbox.generate(load("examples/bms/device.yml"))
    assert "// 8 settable properties" in sandbox.homie_cpp
    assert capacity(sandbox.homie_cpp) == 13


def test_settables_on_nested_child_devices_are_counted(sandbox):
    grandchild = {"id": "g", "nodes": [relay("r6", 5)]}
    child = {"id": "c", "nodes": [relay("r4", 15), relay("r5", 16)], "children": [grandchild]}
    doc = device_doc([relay("r1", 2), relay("r2", 4), relay("r3", 14)])
    doc["children"] = [child]
    sandbox.generate(doc)
    assert "// 6 settable properties" in sandbox.homie_cpp
    assert capacity(sandbox.homie_cpp) == 11


def test_device_build_link_remote_sources_take_table_slots(sandbox):
    links = [{"component": "logic/link", "id": f"l{i}",
              "config": {"source": f"dev{i}/env/t, dev{i}/env/h, r1/state", "target": "r1/state"}}
             for i in range(3)]
    sandbox.generate(device_doc([relay("r1", 14)] + links))
    assert "// 1 settable property + 6 link source watches + 1 $state watch" in sandbox.homie_cpp
    assert capacity(sandbox.homie_cpp) == 12


def test_controller_links_take_no_table_slots(sandbox):
    doc = {"device": {"name": "c", "environment": "esp32-poe-iso-wrover",
                      "homie_role": "controller"},
           "links": [{"id": "l", "source": "dev/env/t", "target": "disp/d/one-line"}]}
    sandbox.generate(doc)
    assert "// 0 settable properties + 1 $state watch" in sandbox.homie_cpp
