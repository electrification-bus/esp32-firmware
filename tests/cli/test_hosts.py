"""The learned-address cache (build/monitor-hosts.json) and host resolution."""

import json

import pytest

ENV = "esp32-poe-iso"
KEY = "device.yml#ebus-demo"


def write_cache(cli, data):
    cli.MONITOR_HOSTS.write_text(json.dumps(data), encoding="utf-8")


def resolve(cli, key=KEY, explicit=None, unique=False):
    return cli.resolve_host({}, ENV, key, explicit, unique=unique)


# ── cache key ─────────────────────────────────────────────────────────────────

def test_host_key_is_repo_relative_plus_device_name(cli, sandbox):
    path = sandbox.root / "examples" / "ssr" / "device.yml"
    assert cli._host_key({"device": {"name": "ssr"}}, path) == "examples/ssr/device.yml#ssr"


def test_host_key_outside_repo_stays_absolute(cli, sandbox, tmp_path):
    path = tmp_path / "elsewhere.yml"
    assert cli._host_key({"device": {}}, path) == f"{path.resolve().as_posix()}#"


# ── reading ───────────────────────────────────────────────────────────────────

def test_missing_cache_reads_empty(cli, sandbox):
    assert cli._hosts_load() == cli._hosts_empty()


@pytest.mark.parametrize("text", ["not json", "[1, 2]", '"str"'])
def test_corrupt_cache_reads_empty(cli, sandbox, text):
    cli.MONITOR_HOSTS.write_text(text, encoding="utf-8")
    assert cli._hosts_load() == cli._hosts_empty()


def test_oldest_format_env_to_ip_is_read_as_environments(cli, sandbox):
    write_cache(cli, {ENV: "10.0.0.5"})
    assert cli._hosts_load()["environments"] == {ENV: "10.0.0.5"}


def test_old_per_yml_format_with_device_id_becomes_a_device(cli, sandbox):
    write_cache(cli, {KEY: {"host": "10.0.0.5", "device_id": "b0b21c90f570", "environment": ENV}})
    hosts = cli._hosts_load()
    assert hosts["devices"] == {"b0b21c90f570": {"host": "10.0.0.5", "environment": ENV,
                                                 "yml": KEY, "seen": 0}}
    assert hosts["ymls"][KEY] == {"device_id": "b0b21c90f570"}


def test_old_per_yml_format_without_device_id_keeps_host_on_the_yml(cli, sandbox):
    write_cache(cli, {KEY: {"host": "10.0.0.5", "environment": ENV}})
    hosts = cli._hosts_load()
    assert hosts["devices"] == {}
    assert hosts["ymls"][KEY]["host"] == "10.0.0.5"
    assert resolve(cli)[0] == "10.0.0.5"


def test_malformed_v2_entries_are_dropped(cli, sandbox):
    write_cache(cli, {"version": 2,
                      "devices": {"good": {"host": "10.0.0.1", "yml": KEY, "seen": 5},
                                  "int-host": {"host": 7},
                                  "no-host": {"yml": KEY},
                                  "neg-seen": {"host": "10.0.0.2", "seen": -1},
                                  "bool-seen": {"host": "10.0.0.3", "seen": True},
                                  "huge-seen": {"host": "10.0.0.4", "seen": 2 ** 32},
                                  "not-a-dict": "10.0.0.5"},
                      "ymls": {KEY: {"device_id": 3}, "other#x": {"device_id": None}},
                      "environments": {ENV: "10.0.0.9", "bad": 1}})
    hosts = cli._hosts_load()
    assert list(hosts["devices"]) == ["good"]
    assert list(hosts["ymls"]) == ["other#x"]
    assert hosts["environments"] == {ENV: "10.0.0.9"}


# ── learning and resolving ────────────────────────────────────────────────────

def test_nothing_known_resolves_to_none(cli, sandbox):
    assert resolve(cli) == (None, None)


def test_learned_device_resolves_for_its_yml(cli, sandbox, clock):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    host, source = resolve(cli)
    assert host == "10.0.0.5"
    assert "device dev-a" in source
    assert resolve(cli, key="other.yml#x") == (None, None)


def test_boards_from_one_yml_keep_their_own_addresses(cli, sandbox, clock):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.6", "dev-b")
    devices = cli._hosts_load()["devices"]
    assert devices["dev-a"]["host"] == "10.0.0.5"
    assert devices["dev-b"]["host"] == "10.0.0.6"


def test_monitor_picks_the_most_recent_of_several(cli, sandbox, clock, capsys):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.6", "dev-b")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    capsys.readouterr()
    assert resolve(cli)[0] == "10.0.0.5"
    err = capsys.readouterr().err
    assert "2 devices have been learned" in err
    assert "Using the most recent, dev-a" in err


def test_ota_refuses_to_guess_between_several(cli, sandbox, clock, capsys):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.6", "dev-b")
    with pytest.raises(SystemExit):
        resolve(cli, unique=True)
    assert "more than one device could be the target" in capsys.readouterr().err


def test_explicit_device_id_resolves_to_its_address(cli, sandbox, clock):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.6", "dev-b")
    host, source = resolve(cli, explicit="dev-a", unique=True)
    assert host == "10.0.0.5"
    assert "learned for device dev-a" in source


def test_explicit_address_is_used_verbatim(cli, sandbox):
    assert resolve(cli, explicit="192.168.1.42") == ("192.168.1.42", "--host")


def test_session_without_device_id_at_a_known_address_is_that_board(cli, sandbox, clock):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.5", None)
    hosts = cli._hosts_load()
    assert hosts["ymls"][KEY] == {"device_id": "dev-a"}
    assert hosts["devices"]["dev-a"]["seen"] == int(clock.now)


def test_session_without_device_id_at_a_new_address_is_the_newest_candidate(cli, sandbox, clock,
                                                                           capsys):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.7", None)
    host, source = resolve(cli)
    assert host == "10.0.0.7"
    assert "device id not seen" in source
    assert "Using the most recent, (device id not seen)" in capsys.readouterr().err


def test_address_taken_by_another_board_drops_the_old_owner(cli, sandbox, clock):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    clock.advance(60)
    cli._host_learn("other.yml#x", ENV, "10.0.0.5", "dev-b")
    assert list(cli._hosts_load()["devices"]) == ["dev-b"]


def test_legacy_per_environment_address_is_a_last_resort(cli, sandbox):
    write_cache(cli, {ENV: "10.0.0.9"})
    host, source = resolve(cli)
    assert host == "10.0.0.9"
    assert "older CLI" in source


def test_learning_drops_the_legacy_environment_entry(cli, sandbox, clock):
    write_cache(cli, {ENV: "10.0.0.9"})
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    assert cli._hosts_load()["environments"] == {}


def test_relearning_the_same_address_is_quiet(cli, sandbox, clock, capsys):
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    assert "learned address 10.0.0.5" in capsys.readouterr().out
    clock.advance(60)
    cli._host_learn(KEY, ENV, "10.0.0.5", "dev-a")
    assert capsys.readouterr().out == ""


def test_boot_log_patterns(cli):
    assert cli._IP_LEARN_RE.search("Network: WiFi STA IP=192.168.1.42").group(1) == "192.168.1.42"
    assert cli._IP_LEARN_RE.search("Network: Ethernet IP=10.0.0.5").group(1) == "10.0.0.5"
    assert cli._ID_LEARN_RE.search("Device ID: b0b21c90f570").group(1) == "b0b21c90f570"
