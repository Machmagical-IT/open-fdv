#!/usr/bin/env python
# SCons build script for the open-fdv GDExtension.
#
# This imports the godot-cpp build environment (which compiles the bindings and
# the static libgodot-cpp library), then builds our shared library from src/.

import os

env = SConscript("godot-cpp/SConstruct")

# Clone so our toolchain tweaks (C++20) don't disturb godot-cpp's own targets.
env = env.Clone()

# Add the extension source include path and force C++20 for our own sources
# (godot-cpp itself defaults to C++17).
env.Append(CPPPATH=["src/"])
env.Append(CXXFLAGS=["-std=c++20"])

# The shared rules core lives one level up in shared/. It is plain C, compiled
# straight into this library rather than linked separately, so the Godot build
# has no runtime dependency beyond its own .dll -- the same sources the Unity
# build compiles to fd_core.dll and calls through P/Invoke.
#
# FD_CORE_STATIC must be defined: without it fd_core.h declares the functions
# as dllimport, the compiler emits __imp_ symbols, and the link fails with every
# core function undefined. The macro says "the implementation is already in this
# binary", not "there is a DLL next door to import from".
#
# This checkout exists in two layouts, and `shared/` sits at a different
# relative path in each:
#
#   submodule of Fatal-Delivery   Fatal-Delivery/vehicle   -> ../shared
#   standalone clone              <umbrella>/open-fdv      -> ../Fatal-Delivery/shared
#
# Probe both so either layout builds. The core is closed-source, so a bare
# clone with neither path cannot build -- fail loudly below instead of globbing
# zero C files and dying at link time with every symbol undefined.
core_dirs = ["../shared", "../Fatal-Delivery/shared"]
core = next((d for d in core_dirs if os.path.isdir(d)), None)
if core is None:
    raise SCons.Errors.UserError(
        "fd_core not found (looked in %s). open-fdv needs the closed "
        "Fatal-Delivery checkout that provides shared/." % ", ".join(core_dirs)
    )

env.Append(CPPPATH=[core + "/include"])

# The public ABI header (open-fdv-dev SDK). Same two layouts:
#
#   submodule of Fatal-Delivery   Fatal-Delivery/vehicle   -> ../../open-fdv-dev
#   standalone clone              <umbrella>/open-fdv      -> ../open-fdv-dev
#
# FD_SDK_INCLUDE overrides the probe for CI or an out-of-tree SDK. Making
# open-fdv-dev a submodule of this repo would collapse this to one path; until
# then the probe keeps both layouts working.
sdk_dirs = ["../open-fdv-dev/include", "../../open-fdv-dev/include"]
if "FD_SDK_INCLUDE" in ARGUMENTS:
    sdk_dirs = [ARGUMENTS["FD_SDK_INCLUDE"]]
sdk = next((d for d in sdk_dirs if os.path.isdir(d)), None)
if sdk is None:
    raise SCons.Errors.UserError(
        "open-fdv-dev SDK not found (looked in %s). Check the SDK out beside "
        "this repo, or pass FD_SDK_INCLUDE=<path>/include." % ", ".join(sdk_dirs)
    )
env.Append(CPPPATH=[sdk])
env.Append(CFLAGS=["-std=c11"])
env.Append(CPPDEFINES=["FD_CORE_STATIC"])

# dlopen/dlsym. Modern glibc has them in libc, but older toolchains and the
# BSDs need libdl named explicitly, and naming it is harmless everywhere.
if env["platform"] in ("linux", "bsd", "freebsd", "openbsd", "netbsd"):
    env.Append(LIBS=["dl"])

sources = Glob("src/*.cpp") + Glob(core + "/src/*.c")

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "bin/libfdv.{}.{}.framework/libfdv.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "bin/libfdv{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
