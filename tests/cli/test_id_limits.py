"""Id and topic lengths: generate refuses what the firmware's buffers would truncate."""

import pytest

from helpers import device_doc, relay

RS485 = {"rs485": {"pin_a": 44, "pin_b": 43}}
I2C = {"i2c": {"pin_sda": 13, "pin_scl": 16}}

# With the default id: ${chip_id}, the root id is 12 chars and a child's is 13 + its own.
ROOT_LEN = 12


def generate_error(sandbox, capsys, doc, overlay=None) -> str:
    with pytest.raises(SystemExit):
        sandbox.generate(doc, overlay=overlay)
    return capsys.readouterr().err


def with_child(cid, nodes=None, buses=None, children=None):
    doc = device_doc([], buses=buses)
    child = {"id": cid, "nodes": nodes or []}
    if children:
        child["children"] = children
    doc["children"] = [child]
    return doc


def reg_node(node_id, reg_id):
    return {"component": "modbus/register_device", "id": node_id, "config": {"address": 1},
            "registers": [{"id": reg_id, "reg": 1, "unit": ""}]}


def bme280(node_id):
    return {"component": "i2c/bme280", "id": node_id, "config": {"address": "0x76"}}


# ── limits and helpers ────────────────────────────────────────────────────────

def test_limits_are_read_from_the_firmware_sources(cli):
    limits = cli.homie_limits()
    assert (limits["HOMIE_DEVICE_ID_MAX"], limits["HOMIE_NODE_ID_MAX"],
            limits["HOMIE_PROPERTY_ID_MAX"], limits["HOMIE_TOPIC_MAX"]) == (63, 31, 31, 127)
    assert limits["HOMIE_PROPERTY_TOPIC_MAX"] == 127 - len("/$target")
    assert (limits["id_prefix"], limits["id_template"], limits["device_name"]) == (31, 95, 31)


@pytest.mark.parametrize("raw,expected", [
    ("env-sht20-1", "env-sht20-1"),
    ("SHT20", "sht20"),
    ("a_b.c d\te", "a-b-c-d-e"),
    ("a!b@c$d/e#f+g", "abcdefg"),
    ("café", "caf"),
    ("a__b - .c", "a-b-c"),
    ("--_a-b-_ ", "a-b"),
    ("!!!", ""),
])
def test_sanitize_matches_the_firmware(cli, raw, expected):
    assert cli.sanitize_homie_id(raw) == expected


@pytest.mark.parametrize("template,prefix,name,expected", [
    ("", "", "x", "000000000000"),
    ("${chip_id}", "", "x", "000000000000"),
    ("bess-${chip_id}", "", "x", "bess-000000000000"),
    ("${name}-${chip_id_short}", "", "My Panel", "my-panel-000000"),
    ("bess-${chip_id}", "Lab 1", "x", "lab-1-bess-000000000000"),
    ("${name}", "", "n" * 40, "n" * 31),   # device_name[32] keeps 31
])
def test_root_id_is_resolved_as_on_the_device(cli, template, prefix, name, expected):
    assert cli.runtime_root_id(template, prefix, name, cli.homie_limits()) == expected


# ── device ids ────────────────────────────────────────────────────────────────

def test_child_id_at_the_device_id_limit_passes(sandbox):
    sandbox.generate(with_child("c" * (63 - ROOT_LEN - 1)))
    assert "char childId_" + "c" * 50 + "[HOMIE_DEVICE_ID_MAX + 1];" in sandbox.homie_cpp


def test_child_id_one_over_the_device_id_limit_fails(sandbox, capsys):
    cid = "c" * (63 - ROOT_LEN)
    err = generate_error(sandbox, capsys, with_child(cid))
    assert f"device id '000000000000-{cid}' (child '{cid}'" in err
    assert "is 64 chars; the firmware holds at most 63 (HOMIE_DEVICE_ID_MAX" in err
    assert "${chip_id} is counted as its 12 hex chars" in err
    assert not (sandbox.cli.SRC_DIR / "homie.cpp").exists()


def test_nested_child_id_includes_every_ancestor(sandbox, capsys):
    # root (12) + "-a" + "-" + grandchild
    ok = 63 - ROOT_LEN - 3
    sandbox.generate(with_child("a", children=[{"id": "g" * ok}]))
    err = generate_error(sandbox, capsys, with_child("a", children=[{"id": "g" * (ok + 1)}]))
    assert f"device id '000000000000-a-{'g' * (ok + 1)}'" in err


def test_overlay_id_prefix_counts_toward_child_ids(sandbox, capsys):
    overlay = {"device": {"device_passphrase": "test-passphrase", "id_prefix": "lab1"}}
    ok = 63 - len("lab1-000000000000-")
    sandbox.generate(with_child("c" * ok), overlay=overlay)
    err = generate_error(sandbox, capsys, with_child("c" * (ok + 1)), overlay=overlay)
    assert "device id 'lab1-000000000000-" in err


def test_root_id_over_the_limit_fails(sandbox, capsys):
    err = generate_error(sandbox, capsys, device_doc(id="r" * 52 + "-${chip_id}"))
    assert "(the root device: device.id_prefix + device.id, resolved) is 65 chars" in err


def test_id_prefix_longer_than_its_buffer_fails(sandbox, capsys):
    overlay = {"device": {"device_passphrase": "test-passphrase", "id_prefix": "p" * 32}}
    err = generate_error(sandbox, capsys, device_doc(), overlay=overlay)
    assert "device.id_prefix '" + "p" * 32 + "' is 32 chars; the firmware keeps at most 31" in err


# ── node and property ids ─────────────────────────────────────────────────────

def test_node_id_at_the_limit_passes_and_one_over_fails(sandbox, capsys):
    sandbox.generate(device_doc([relay("n" * 31)]))
    err = generate_error(sandbox, capsys, device_doc([relay("n" * 32)]))
    assert f"node id '{'n' * 32}' (root device) is 32 chars; the firmware holds at most 31" in err


def test_node_id_is_measured_after_sanitizing(sandbox):
    sandbox.generate(device_doc([relay("Pump" + "_" * 40 + "Contactor")]))


def test_register_id_is_a_property_id(sandbox, capsys):
    sandbox.generate(device_doc([reg_node("m", "r" * 31)], buses=RS485))
    err = generate_error(sandbox, capsys, device_doc([reg_node("m", "r" * 32)], buses=RS485))
    assert f"property id '{'r' * 32}' (node 'm', root device) is 32 chars" in err


# ── property topics ───────────────────────────────────────────────────────────

# A child id of 50 makes the device id 63; "homie/5/" + 63 + "/" + node + "/" + property.
CHILD50 = "c" * 50
TOPIC_FIXED = len("homie/5/") + 63 + 2


def test_register_property_topic_at_the_limit_passes(sandbox):
    node = "m" * (119 - TOPIC_FIXED - 31)
    sandbox.generate(with_child(CHILD50, [reg_node(node, "r" * 31)], buses=RS485))


def test_register_property_topic_one_over_the_limit_fails(sandbox, capsys):
    node = "m" * (119 - TOPIC_FIXED - 31 + 1)
    err = generate_error(sandbox, capsys,
                         with_child(CHILD50, [reg_node(node, "r" * 31)], buses=RS485))
    topic = f"homie/5/000000000000-{CHILD50}/{node}/{'r' * 31}"
    assert f"property topic '{topic}' (node '{node}', child '{CHILD50}') is 120 chars" in err
    assert "at most 119 (HOMIE_TOPIC_MAX 127 in lib/ebus_core/include/homie/homie_limits.h, " \
           "less 8 for '/$target')" in err
    assert "id '" not in err   # every id is within its own limit


def test_component_property_topic_is_checked(sandbox, capsys):
    # bme280's longest property id is relative-humidity (17)
    node = "e" * (119 - TOPIC_FIXED - 17)
    sandbox.generate(with_child(CHILD50, [bme280(node)], buses=I2C))
    err = generate_error(sandbox, capsys, with_child(CHILD50, [bme280(node + "e")], buses=I2C))
    assert f"/{node}e/relative-humidity' (node '{node}e', child '{CHILD50}') is 120 chars" in err
