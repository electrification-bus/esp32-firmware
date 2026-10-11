"""Projects: a device.yml in a directory of its own, built into its .ebus/."""

import configparser

import pytest

from helpers import device_doc, relay


def hand_coded_doc():
    doc = device_doc()
    del doc["nodes"]
    return doc


def test_project_paths_accepts_a_directory_or_its_device_yml(cli, tmp_path):
    assert cli.project_paths(str(tmp_path)) == (tmp_path, tmp_path / "device.yml")
    other = tmp_path / "other.yml"
    assert cli.project_paths(str(other)) == (tmp_path, other)


def test_use_project_points_every_output_into_the_project(cli, tmp_path, monkeypatch):
    for name in ("PROJECT_DIR", "OUT_DIR", "GEN_SRC_DIR", "GEN_DATA_DIR", "GENERATED_INI",
                 "PIO_BUILD_DIR", "CONFIG_OVERLAY"):
        monkeypatch.setattr(cli, name, getattr(cli, name))
    monkeypatch.setattr(cli, "CONFIG_OVERLAY_REPO", tmp_path / "repo-overlay.yml")
    cli.use_project(tmp_path)
    out = tmp_path.resolve() / ".ebus"
    assert (cli.GEN_SRC_DIR, cli.GEN_DATA_DIR, cli.GENERATED_INI, cli.PIO_BUILD_DIR) == \
        (out / "src", out / "data", out / "platformio.ini", out / "build")
    assert cli.CONFIG_OVERLAY == tmp_path.resolve() / "config-overlay.yml"


def test_repo_root_overlay_is_used_only_when_the_project_has_none(cli, tmp_path, monkeypatch):
    for name in ("PROJECT_DIR", "OUT_DIR", "GEN_SRC_DIR", "GEN_DATA_DIR", "GENERATED_INI",
                 "PIO_BUILD_DIR", "CONFIG_OVERLAY"):
        monkeypatch.setattr(cli, name, getattr(cli, name))
    repo_overlay = tmp_path / "repo-overlay.yml"
    repo_overlay.write_text("device: {}\n")
    monkeypatch.setattr(cli, "CONFIG_OVERLAY_REPO", repo_overlay)
    project = tmp_path / "p"
    project.mkdir()
    cli.use_project(project)
    assert cli.CONFIG_OVERLAY == repo_overlay
    (project / "config-overlay.yml").write_text("device: {}\n")
    cli.use_project(project)
    assert cli.CONFIG_OVERLAY == project.resolve() / "config-overlay.yml"


def test_the_repo_itself_is_not_a_project(cli, sandbox, monkeypatch, capsys):
    monkeypatch.setattr(cli, "PROJECT_DIR", sandbox.root)
    with pytest.raises(SystemExit):
        sandbox.generate(device_doc([relay()]))
    assert "not a project" in capsys.readouterr().err


def test_sources_are_linked_and_homie_cpp_generated(cli, sandbox):
    (sandbox.root / "src" / "main.cpp").write_text("// main\n")
    (sandbox.root / "src" / "node").mkdir()
    (sandbox.root / "src" / "homie.cpp").write_text("// stale, from an older clone\n")
    (sandbox.root / "src" / "idf_component.yml").write_text("dependencies: {}\n")
    (sandbox.project / "src").mkdir()
    sandbox.generate(device_doc([relay()]))
    staged = cli.GEN_SRC_DIR
    assert (staged / "main.cpp").resolve() == (sandbox.root / "src" / "main.cpp").resolve()
    assert (staged / "node").resolve() == (sandbox.root / "src" / "node").resolve()
    assert (staged / "project").resolve() == (sandbox.project / "src").resolve()
    assert not (staged / "idf_component.yml").exists()
    assert "DigitalOutput relay1" in sandbox.homie_cpp


def test_a_removed_project_src_is_unlinked(cli, sandbox):
    (sandbox.project / "src").mkdir()
    sandbox.generate(device_doc([relay()]))
    assert (cli.GEN_SRC_DIR / "project").exists()
    (sandbox.project / "src").rmdir()
    sandbox.generate(device_doc([relay()]))
    assert not (cli.GEN_SRC_DIR / "project").is_symlink()


def test_data_is_the_repo_data_with_the_project_data_over_it(cli, sandbox, capsys):
    repo_data, project_data = sandbox.root / "data", sandbox.project / "data"
    (repo_data / "html").mkdir()
    (repo_data / "html" / "config.html").write_text("repo page")
    (repo_data / "openapi.yml").write_text("repo api")
    (repo_data / "config.yml").write_text("password: from-an-older-clone")
    (repo_data / "notes.txt~").write_text("editor backup")
    project_data.mkdir()
    (project_data / "openapi.yml").write_text("project api")
    (project_data / "extra.txt").write_text("project file")
    (project_data / "homie.yml").write_text("hand-maintained")
    sandbox.generate(device_doc([relay()]))
    staged = cli.GEN_DATA_DIR
    assert (staged / "html" / "config.html").read_text() == "repo page"
    assert (staged / "openapi.yml").read_text() == "project api"
    assert (staged / "extra.txt").read_text() == "project file"
    assert not (staged / "notes.txt~").exists()
    assert "from-an-older-clone" not in (staged / "config.yml").read_text()
    assert sandbox.homie_yml["nodes"][0]["id"] == "relay1"
    assert "homie.yml is not used" in capsys.readouterr().out


def test_a_hand_coded_project_generates_no_device_code(cli, sandbox):
    (sandbox.project / "src").mkdir()
    sandbox.generate(device_doc([relay()]))
    assert (cli.GEN_SRC_DIR / "homie.cpp").exists()
    sandbox.generate(hand_coded_doc())
    assert not (cli.GEN_SRC_DIR / "homie.cpp").exists()
    assert sandbox.homie_yml["nodes"] == []
    assert sandbox.config_yml["device"]["name"] == "test-device"


def test_a_hand_coded_project_needs_its_src(sandbox, capsys):
    with pytest.raises(SystemExit):
        sandbox.generate(hand_coded_doc())
    assert "does not exist" in capsys.readouterr().err


def test_components_the_project_includes_are_built(cli, sandbox):
    src = sandbox.project / "src"
    src.mkdir()
    (src / "device.cpp").write_text('#include <homie.h>\n#include <i2c/bme280.h>\n')
    sandbox.generate(hand_coded_doc())
    ini = configparser.RawConfigParser()
    ini.read(cli.GENERATED_INI)
    assert "i2c/bme280" in ini.get("env:esp32-poe-iso", "custom_ebus_components").split()
    bme280 = cli.load_yaml_file(cli.LIB_DIR / "i2c" / "bme280.yml")
    for dep in bme280["generator"]["lib_deps"]:
        assert dep in ini.get("common", "lib_deps")


def test_generated_ini_builds_the_project(cli, sandbox):
    path = sandbox.generate(device_doc([relay()]))
    ini = configparser.RawConfigParser()
    ini.read(cli.GENERATED_INI)
    assert ini.get("platformio", "src_dir") == str(cli.GEN_SRC_DIR)
    assert ini.get("platformio", "data_dir") == str(cli.GEN_DATA_DIR)
    assert ini.get("platformio", "build_dir") == str(cli.PIO_BUILD_DIR)
    assert ini.get("env:esp32-poe-iso", "custom_ebus_device_yml") == str(path.resolve())
    assert (cli.OUT_DIR / ".gitignore").read_text().splitlines()[-1] == "*"
