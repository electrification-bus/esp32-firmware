Import("env")
import os
import sys
import subprocess
from pathlib import Path

# PlatformIO pre-build hook: regenerate the project's .ebus/src/homie.cpp and
# .ebus/data/{homie,config}.yml from its device.yml before every build, so device.yml stays
# the single source of truth.
#
# We shell out to the `ebus-esp32` CLI rather than importing it: the CLI is an
# extension-less, hyphenated script (not an importable module), and running it as a
# subprocess with PlatformIO's own interpreter ($PYTHONEXE, which ships pyyaml) keeps a
# single generation implementation.

project_dir = Path(env["PROJECT_DIR"])
cli = project_dir / "ebus-esp32"

# Which device.yml to generate from: the generated .ebus/platformio.ini names it, and
# `ebus-esp32` also exports EBUS_DEVICE_YML.
device_yml = (env.GetProjectOption("custom_ebus_device_yml", "")
              or os.environ.get("EBUS_DEVICE_YML", ""))
if not device_yml:
    print("generate_homie: no project to build. Build through the CLI, which generates the "
          "PlatformIO config for a project:\n  ebus-esp32 build path/to/my-device",
          file=sys.stderr)
    env.Exit(1)
if not Path(device_yml).exists():
    print(f"generate_homie: {device_yml} not found", file=sys.stderr)
    env.Exit(1)
print(f"generate_homie: regenerating Homie sources from {device_yml}")
result = subprocess.run([sys.executable, str(cli), "generate", device_yml],
                        cwd=str(project_dir))
if result.returncode != 0:
    print("generate_homie: generation FAILED — see errors above", file=sys.stderr)
    env.Exit(result.returncode)
