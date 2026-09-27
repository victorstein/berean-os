"""
PlatformIO pre-build script: -Wall for this repo's own sources only.

-Wall is added per source through a build middleware, not build_flags:
build_flags also reaches freeink-sdk, the libdeps and the framework, and the
only way to quiet their warnings there is global -Wno-* flags, which would
switch the same checks off for our code. The framework's -Wno-sign-compare
still wins over -Wall, so -Wsign-compare stays off.

FreeInkUI's include directory becomes a system directory because
FreeInkUIIcon.h trips -Wcomment inside our own translation units, and
freeink-sdk is a submodule this repo does not edit.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import sys

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
sys.path.insert(0, os.path.join(PROJECT_DIR, "scripts"))

from repo_warnings import is_repo_source  # noqa: E402

FREEINK_UI_INCLUDE = os.path.join(PROJECT_DIR, "freeink-sdk", "libs", "ui", "FreeInkUI", "include")


def add_wall_to_repo_sources(builder_env, node):
    if not is_repo_source(node.srcnode().get_abspath(), PROJECT_DIR):
        return node
    return builder_env.Object(node, CCFLAGS=builder_env["CCFLAGS"] + ["-Wall"])


env.Append(CCFLAGS=["-isystem", FREEINK_UI_INCLUDE])  # noqa: F821
env.AddBuildMiddleware(add_wall_to_repo_sources)  # noqa: F821
