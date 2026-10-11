Import("env", "pio_lib_builder")
import os

# Library extraScript (build.extraScript in lib/<bus>/library.json) for the
# component libraries. Each lib/<bus>/ folder is ONE PlatformIO library, so without
# this every component .cpp in it compiles as soon as any one of them is used, and a
# component whose third-party library is not in lib_deps (because device.yml does not
# use it) fails on its #include: i2c/gme12864 alone died on Adafruit_BME280.h.
#
# `ebus-esp32 generate` writes the components the build needs into .ebus/platformio.ini
# as custom_ebus_components (bus/name pairs, space-separated, including components
# reached through another component's headers). Here the source filter drops
# <name>.cpp/.c of every other lib/<bus>/<name>.yml component in this library. Files
# that belong to no component (i2c_bus.cpp, spi_bus.cpp, ble_client.cpp) always build.
#
# The tracked platformio/platformio.ini does not set the option, and then the filter is
# left alone: every source compiles, as before, against its full lib_deps list.
#
# PlatformIO resets the library's SRC_FILTER just before running this script, and a
# srcFilter key in library.json would take precedence over it, so do not add one.

used = env.GetProjectOption("custom_ebus_components", None)
if used is not None:
    lib_dir = pio_lib_builder.path
    bus = os.path.basename(lib_dir)
    keep = set()
    for comp in used.split():
        comp_bus, _, name = comp.partition("/")
        if comp_bus == bus:
            keep.add(name)
    src_filter = ["+<*>", "-<.git%s>" % os.sep, "-<.svn%s>" % os.sep]
    for entry in sorted(os.listdir(lib_dir)):
        stem, ext = os.path.splitext(entry)
        if ext == ".yml" and stem not in keep:
            src_filter += ["-<%s.cpp>" % stem, "-<%s.c>" % stem]
    env.Replace(SRC_FILTER=" ".join(src_filter))
