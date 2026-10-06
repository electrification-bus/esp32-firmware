Import("env")
import os
import sys
import subprocess
from pathlib import Path

# PlatformIO pre-build hook: regenerate src/homie.cpp + data/homie.yml + data/config.yml
# from device.yml before every build. This makes device.yml the single source of truth —
# the generated files are gitignored and never committed (see .gitignore).
#
# We shell out to the `ebus-esp32` CLI rather than importing it: the CLI is an
# extension-less, hyphenated script (not an importable module), and running it as a
# subprocess with PlatformIO's own interpreter ($PYTHONEXE, which ships pyyaml) keeps a
# single generation implementation. A bare `pio run` therefore regenerates too, so a
# fresh clone (with no generated files) builds correctly.

project_dir = Path(env["PROJECT_DIR"])
cli = project_dir / "ebus-esp32"

# Which device.yml to generate from. `./ebus-esp32 <cmd> path/to/device.yml` exports
# EBUS_DEVICE_YML (absolute) so this hook regenerates from the file the CLI was given;
# without it, the hook would rebuild ./device.yml over the CLI's own generate. A bare
# `pio run` can set it too; a relative path is taken from the project root.
override = os.environ.get("EBUS_DEVICE_YML")
device_yml = project_dir / (override or "device.yml")

# Hand-coded mode: if there's no device.yml, the project maintains src/homie.cpp
# and data/*.yml by hand (see doc/HandCodedDevice.md), so the generator stands
# aside instead of failing the build. An explicit EBUS_DEVICE_YML that does not
# exist is a mistake, not hand-coded mode.
if not device_yml.exists():
    if override:
        print(f"generate_homie: EBUS_DEVICE_YML={override} not found", file=sys.stderr)
        env.Exit(1)
    print("generate_homie: no device.yml — hand-coded mode, skipping generation")
else:
    print(f"generate_homie: regenerating Homie sources from {device_yml}")
    result = subprocess.run([sys.executable, str(cli), "generate", str(device_yml)],
                            cwd=str(project_dir))
    if result.returncode != 0:
        print("generate_homie: generation FAILED — see errors above", file=sys.stderr)
        env.Exit(result.returncode)
