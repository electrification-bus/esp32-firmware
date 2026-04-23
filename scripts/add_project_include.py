Import("env")
# Expose project include/ and lib/ to library source file compilation.
# PlatformIO's generated include path for library builds is unreliable on
# Windows paths — this script adds them explicitly via SCons.
# Note: src/ (project) compilation gets lib/ via -I build_flag in platformio.ini.
env.Append(CPPPATH=[
    env.subst("$PROJECT_INCLUDE_DIR"),
    env.subst("$PROJECT_DIR") + "/lib",
])
