Import("env")
import os, glob

# Expose project include/ and lib/ to library source file compilation.
# PlatformIO's generated include path for library builds is unreliable on
# Windows paths — this script adds them explicitly via SCons.
#
# It also adds every bundled framework library's src/ dir (WiFi, Ethernet,
# Network, FS, SPI, ...) AND every lib_deps-fetched library's own root/src dir
# (ArduinoJson, YAMLDuino, ...). Libraries #include each other (e.g.
# Ethernet/ETH.h includes WiFi.h and SPI.h; YAMLDuino's ArduinoYaml.cpp
# conditionally includes ArduinoJson.h via __has_include) but PlatformIO's LDF
# does not reliably propagate those inter-library include paths when compiling
# each as a separate unit on Windows — so rather than hand-listing individual
# headers (which breaks again on the next framework restructuring — arduino-esp32
# v3.x already split networking into a separate Network lib), add every
# libraries/*/src directory (framework) and every libdeps/<env>/* + */src
# directory (lib_deps) that exists, unconditionally.
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

extra_cpppath = [
    env.subst("$PROJECT_INCLUDE_DIR"),
    env.subst("$PROJECT_DIR") + "/lib",
]

lib_srcs = []
if framework_dir and os.path.isdir(framework_dir):
    libraries_dir = os.path.join(framework_dir, "libraries")
    for src_dir in sorted(glob.glob(os.path.join(libraries_dir, "*", "src"))):
        if os.path.isdir(src_dir):
            lib_srcs.append(src_dir.replace("\\", "/"))

libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR") + "/" + env.subst("$PIOENV")
if os.path.isdir(libdeps_dir):
    for lib_dir in sorted(glob.glob(os.path.join(libdeps_dir, "*"))):
        if not os.path.isdir(lib_dir):
            continue
        # Root dir first — some libraries (e.g. ArduinoJson) keep their main
        # header at the library root, not under src/.
        lib_srcs.append(lib_dir.replace("\\", "/"))
        src_dir = os.path.join(lib_dir, "src")
        if os.path.isdir(src_dir):
            lib_srcs.append(src_dir.replace("\\", "/"))

extra_cpppath += lib_srcs
env.Append(CPPPATH=extra_cpppath)

# Belt-and-suspenders: raw -I flags survive env cloning in framework builds
env.Append(CCFLAGS=["-I" + p for p in lib_srcs])
