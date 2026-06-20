Import("env")
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

print("generate_homie: regenerating Homie sources from device.yml")
result = subprocess.run([sys.executable, str(cli), "generate"], cwd=str(project_dir))
if result.returncode != 0:
    print("generate_homie: generation FAILED — see errors above", file=sys.stderr)
    env.Exit(result.returncode)
