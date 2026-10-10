"""logic/link and controller links: what generate checks in their references."""

import pytest

from helpers import device_doc


def link(link_id, source, target, **extra):
    return {"component": "logic/link", "id": link_id,
            "config": {"source": source, "target": target, **extra}}


def led():
    return {"component": "gpio/digital_output", "id": "led",
            "config": {"pin": 14, "active_low": False, "initial_state": False}}


def test_two_links_sharing_a_remote_source_are_refused(sandbox, capsys):
    doc = device_doc([led(),
                      link("a", "b0b21c90f570/env/temperature", "led/state"),
                      link("b", "B0B21C90F570/env/Temperature, led/state", "aa/x/y")])
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    err = capsys.readouterr().err
    assert "remote source 'b0b21c90f570/env/temperature' is used in links 'a' and 'b'" in err


def test_one_link_naming_a_remote_source_twice_is_refused(sandbox, capsys):
    doc = device_doc([led(), link("a", "dev/env/t, dev/env/t", "led/state", format="%1 %2")])
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    assert "is used twice in its source:" in capsys.readouterr().err


def test_links_with_distinct_remote_sources_generate(sandbox):
    sandbox.generate(device_doc([led(),
                                 link("a", "dev/env/t", "led/state"),
                                 link("b", "dev/env/h, led/state", "other/x/y")]))


def controller_doc(*links):
    return {"device": {"name": "c", "environment": "esp32-poe-iso-wrover",
                       "homie_role": "controller"},
            "links": list(links)}


def refused(sandbox, capsys, doc) -> str:
    with pytest.raises(SystemExit):
        sandbox.generate(doc)
    return capsys.readouterr().err


@pytest.mark.parametrize("source,message", [
    ("led", "must be <node>/<property> or <device-id>/<node>/<property>"),
    ("a/b/c/d", "must be <node>/<property>"),
    ("a//b", "must be <node>/<property>"),
    ("led/state,", "'' must be"),
    ("*/env/t", "uses *, which only a controller build resolves"),
    ("dev/env-*/t", "uses *, which only a controller build resolves"),
    ("dev/env/t*", "has * in the property"),
    ("!!!/state", "with no Homie id characters"),
    ("a/b, c/d, e/f, g/h", "at most 3 references (got 4)"),
    ("n/" + "p" * 32, "of 32 chars; the firmware holds at most 31 (HOMIE_PROPERTY_ID_MAX"),
    ("n" * 32 + "/p", "of 32 chars; the firmware holds at most 31 (HOMIE_NODE_ID_MAX"),
    ("d" * 64 + "/n/p", "of 64 chars; the firmware holds at most 63 (HOMIE_DEVICE_ID_MAX"),
])
def test_device_build_link_sources_are_validated(sandbox, capsys, source, message):
    err = refused(sandbox, capsys, device_doc([led(), link("l", source, "led/state")]))
    assert "ERROR: link 'l' source:" in err
    assert message in err


def test_a_remote_topic_over_the_limit_is_refused(sandbox, capsys):
    # 7 + 1 + 63 + 1 + 31 + 1 + 31 = 135 > 127, every part within its own limit.
    ref = f"{'d' * 63}/{'n' * 31}/{'p' * 31}"
    err = refused(sandbox, capsys, device_doc([led(), link("l", ref, "led/state")]))
    assert "is 135 chars; the firmware holds at most 127 (HOMIE_TOPIC_MAX" in err


def test_a_local_target_topic_counts_the_device_id_and_set(sandbox, capsys):
    # homie/5/<12-char chip id>/<31>/<31>/set is 7+1+12+1+31+1+31+4 = 88: fits.
    node = {"component": "gpio/digital_output", "id": "n" * 31,
            "config": {"pin": 14, "active_low": False, "initial_state": False}}
    sandbox.generate(device_doc([node, link("l", f"{'n' * 31}/state", f"{'n' * 31}/state")]))


def test_target_must_be_one_reference(sandbox, capsys):
    err = refused(sandbox, capsys, device_doc([led(), link("l", "led/state", "led/state, a/b")]))
    assert "link 'l' target: must be one reference" in err


def test_controller_links_need_three_parts(sandbox, capsys):
    err = refused(sandbox, capsys, controller_doc(
        {"id": "l", "source": "env/temperature", "target": "d/disp/one-line"}))
    assert "controller link 'l' source: 'env/temperature' must be <device-id>/<node>/<property>" in err


def test_controller_links_may_use_star_but_not_in_the_property(sandbox, capsys):
    sandbox.generate(controller_doc({"id": "ok", "source": "*/environment-*/air-pressure",
                                     "target": "*/display-gme12864/line-two"}))
    err = refused(sandbox, capsys, controller_doc(
        {"id": "l", "source": "*/env/air-*", "target": "d/disp/one-line"}))
    assert "has * in the property" in err


def test_controller_link_malformed_pattern(sandbox, capsys):
    err = refused(sandbox, capsys, controller_doc(
        {"id": "l", "source": "*/environment_*/t", "target": "d/disp/one-line"}))
    assert "malformed pattern 'environment_*'" in err


def test_controller_links_may_share_a_source(sandbox):
    sandbox.generate(controller_doc(
        {"id": "a", "source": "dev/env/t", "target": "d/disp/one-line"},
        {"id": "b", "source": "dev/env/t", "target": "d/disp/line-two"}))
