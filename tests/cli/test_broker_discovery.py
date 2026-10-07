"""mqtt.discover: the broker service types the mDNS querier browses, in priority order."""

import pytest

from helpers import device_doc


def with_discover(value):
    doc = device_doc()
    doc["mqtt"] = {"mode": "discovery-only", "discover": value}
    return doc


def test_default_is_secure_mqtt_then_mqtt(sandbox):
    sandbox.generate(device_doc())
    assert sandbox.config_yml["mqtt"]["discover"] == ["secure-mqtt", "mqtt"]


@pytest.mark.parametrize("value", [["secure-mqtt"], ["mqtt", "secure-mqtt"], ["mqtt"]])
def test_list_is_written_in_order(sandbox, value):
    sandbox.generate(with_discover(value))
    assert sandbox.config_yml["mqtt"]["discover"] == value


@pytest.mark.parametrize("value, message", [
    (["amqp"], "'amqp' is not a broker service; valid: secure-mqtt, mqtt, mqtt-ws, mqtt-wss"),
    (["_mqtt._tcp"], "'_mqtt._tcp' is not a broker service"),
    ([1883], "1883 is not a broker service"),
    (["mqtt-ws"], "'mqtt-ws' is MQTT over WebSocket"),
    (["secure-mqtt", "mqtt-wss"], "'mqtt-wss' is MQTT over WebSocket"),
    (["mqtt", "mqtt"], "'mqtt' is listed more than once"),
    ([], "is empty"),
    ("secure-mqtt", "must be a list"),
])
def test_invalid_list_is_refused(sandbox, capsys, value, message):
    with pytest.raises(SystemExit) as exc:
        sandbox.generate(with_discover(value))
    assert exc.value.code == 1
    err = capsys.readouterr().err
    assert "ERROR: mqtt.discover in" in err
    assert "device.yml" in err
    assert message in err


def test_every_problem_is_reported(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(with_discover(["mqtt-ws", "foo", "mqtt", "mqtt"]))
    err = capsys.readouterr().err
    assert "'mqtt-ws' is MQTT over WebSocket" in err
    assert "'foo' is not a broker service" in err
    assert "'mqtt' is listed more than once" in err


def test_refused_before_anything_is_generated(sandbox):
    with pytest.raises(SystemExit):
        sandbox.generate(with_discover(["mqtt-ws"]))
    assert not (sandbox.cli.DATA_DIR / "config.yml").exists()
    assert not (sandbox.cli.SRC_DIR / "homie.cpp").exists()


def test_overlay_list_replaces_device_yml_list(sandbox):
    sandbox.generate(with_discover(["secure-mqtt", "mqtt"]),
                     overlay={"device": {"device_passphrase": "test-passphrase"},
                              "mqtt": {"discover": ["secure-mqtt"]}})
    assert sandbox.config_yml["mqtt"]["discover"] == ["secure-mqtt"]


@pytest.mark.parametrize("value, message", [
    ([], "is empty"),
    (["mqtt-wss"], "'mqtt-wss' is MQTT over WebSocket"),
])
def test_invalid_overlay_list_is_refused(sandbox, capsys, value, message):
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc(),
                         overlay={"device": {"device_passphrase": "test-passphrase"},
                                  "mqtt": {"discover": value}})
    err = capsys.readouterr().err
    assert "ERROR: mqtt.discover in config-overlay.yml" in err
    assert message in err
