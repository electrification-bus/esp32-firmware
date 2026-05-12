Import("env")
import os, glob

# Expose project include/ and lib/ to library source file compilation.
# PlatformIO's generated include path for library builds is unreliable on
# Windows paths — this script adds them explicitly via SCons.
#
# Also add WiFi/src so ETH.h can find WiFi.h — Arduino ESP32 ETH.h
# includes WiFi.h but PlatformIO's LDF does not propagate this dependency
# when compiling the Ethernet library as a separate unit.
#
# NOTE: $FRAMEWORK_DIR is set by the framework builder SConscript which runs
# AFTER pre: extra_scripts. Fall back to searching the PlatformIO packages dir.

framework_dir = env.subst("$FRAMEWORK_DIR")

if not framework_dir or not os.path.isdir(framework_dir):
    pio_home = os.environ.get(
        "PLATFORMIO_HOME_DIR",
        os.path.join(os.path.expanduser("~"), ".platformio")
    )
    pkg_dir = os.path.join(pio_home, "packages")
    matches = glob.glob(os.path.join(pkg_dir, "framework-arduinoespressif32*"))
    # Prefer the stable release (no @src hash, and not the -libs archive which
    # has no libraries/ folder). The @src version may have a restructured WiFi.h
    # that doesn't include WiFiClient.h.
    stable = [m for m in matches if '@' not in os.path.basename(m)
              and not os.path.basename(m).endswith("-libs")]
    if stable:
        framework_dir = sorted(stable)[-1]
    elif matches:
        framework_dir = sorted(matches)[-1]

# Locate WiFi.h — check both old (v2.x) and merged (v3.x) locations
wifi_src = None
for candidate in ["libraries/WiFi/src", "libraries/Ethernet/src"]:
    path = os.path.join(framework_dir, candidate).replace("\\", "/")
    if os.path.isfile(os.path.join(path, "WiFi.h")):
        wifi_src = path
        break

# Locate Network.h — arduino-esp32 v3.x split networking base into Network lib
network_src = None
network_path = os.path.join(framework_dir, "libraries/Network/src").replace("\\", "/")
if os.path.isfile(os.path.join(network_path, "Network.h")):
    network_src = network_path

extra_cpppath = [
    env.subst("$PROJECT_INCLUDE_DIR"),
    env.subst("$PROJECT_DIR") + "/lib",
]
if wifi_src:
    extra_cpppath.append(wifi_src)
if network_src:
    extra_cpppath.append(network_src)

env.Append(CPPPATH=extra_cpppath)

# Belt-and-suspenders: raw -I flag survives env cloning in framework builds
if wifi_src:
    env.Append(CCFLAGS=["-I" + wifi_src])
if network_src:
    env.Append(CCFLAGS=["-I" + network_src])
