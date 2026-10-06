Import("env")
import os, platform

# Build the LittleFS image in Python on Apple Silicon, for the -arduino2 envs only.
#
# espressif32@7.0.1 packs data/ with platformio/tool-mklittlefs@1.203.210628, whose macOS
# build is x86_64 only: on an arm64 Mac without Rosetta, `buildfs` dies with "Bad CPU type
# in executable". Every macOS build of tool-mklittlefs in the PlatformIO registry
# (platformio, tasmota, earlephilhower) is x86_64, so there is no native version to pin.
# The default (arduino3) envs are unaffected: the pioarduino platform already builds the
# image with littlefs-python, which it installs into PlatformIO's own Python. This script
# reuses that module.
#
# The image must mount on arduino-esp32 2.0.17, whose esp_littlefs 1.14.1 bundles littlefs
# 2.9: it accepts on-disk version 2.1 and any superblock name_max up to 255. name_max is
# 64 to match the default (arduino3) envs' images and 2.0.17's CONFIG_LITTLEFS_OBJ_NAME_LEN.
# Block size and count come from the platform (FS_BLOCK, FS_SIZE), exactly as they reach
# mklittlefs.
#
# A post: script, because the platform's main.py binds the mklittlefs command to the
# image target; this swaps that target's action afterward. Every other host keeps the
# platform's mklittlefs untouched.

FS_NAME_MAX = 64
FS_DISK_VERSION = 0x00020001     # LFS_DISK_VERSION of littlefs 2.9
# Same skip list as mklittlefs, which matches on the bare name at any depth.
IGNORED_NAMES = (".DS_Store", ".git", ".gitignore", ".gitmodules")


def _add_tree(fs, src_dir, fs_dir):
    for name in sorted(os.listdir(src_dir)):
        if name in IGNORED_NAMES:
            continue
        src_path = os.path.join(src_dir, name)
        fs_path = fs_dir + "/" + name if fs_dir else name
        if os.path.isdir(src_path):
            fs.makedirs(fs_path, exist_ok=True)
            _add_tree(fs, src_path, fs_path)
        elif os.path.isfile(src_path):
            print(fs_path)
            with open(src_path, "rb") as f, fs.open(fs_path, "wb") as dest:
                dest.write(f.read())
        else:
            continue
        # 't' is the mtime attribute esp_littlefs reads (CONFIG_LITTLEFS_USE_MTIME);
        # 32-bit seconds, the width of time_t on IDF 4.4 and what mklittlefs 3.x wrote.
        mtime = int(os.stat(src_path).st_mtime) & 0xFFFFFFFF
        fs.setattr(fs_path, "t", mtime.to_bytes(4, "little"))


def _build_image(target, source, env):
    from littlefs import LittleFS
    block_size = int(env["FS_BLOCK"])
    fs = LittleFS(
        block_size=block_size,
        block_count=int(env["FS_SIZE"]) // block_size,
        read_size=1,
        prog_size=1,
        cache_size=block_size,
        lookahead_size=32,
        block_cycles=500,
        name_max=FS_NAME_MAX,
        disk_version=FS_DISK_VERSION,
        mount=False,
    )
    fs.format()
    fs.mount()
    _add_tree(fs, str(source[0]), "")
    fs.unmount()
    with open(str(target[0]), "wb") as f:
        f.write(fs.context.buffer)
    return 0


def _use_python_builder():
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        return False
    if env.BoardConfig().get("build.filesystem", "spiffs") != "littlefs":
        return False
    try:
        import littlefs  # noqa: F401
    except ImportError:
        print("littlefs_image.py: littlefs-python is not installed in PlatformIO's Python, "
              "so buildfs falls back to the x86_64 mklittlefs, which needs Rosetta "
              "(softwareupdate --install-rosetta). Building any default env once "
              "installs littlefs-python.")
        return False
    return True


if _use_python_builder():
    image = env.File(os.path.join(env.subst("$BUILD_DIR"),
                                  env.subst("${ESP32_FS_IMAGE_NAME}.bin")))
    # The node only has a builder when buildfs/uploadfs was requested; uploadfs with
    # nobuild reuses an existing image and never packs anything.
    if image.has_builder():
        # Action() because under `pio run -v` VerboseAction returns the bare function.
        image.get_executor().set_action_list(Action(env.VerboseAction(
            _build_image, "Building FS image from '$SOURCES' directory to $TARGET")))
