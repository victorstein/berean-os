"""Which build sources belong to this repo, for scripts/enable_repo_warnings.py.

No SCons import, so scripts/tests can exercise the rule directly.
"""

import os

# Vendored upstream code under lib/: each library.json declares the upstream
# release (expat 2.7.3, miniz 11.3.2, uzlib 2.9.8), so their warnings are
# upstream's to fix. This is the only exclusion list inside lib/.
VENDORED_LIBS = ("expat", "miniz", "uzlib")


def is_repo_source(abs_path, project_dir, pathmod=os.path):
    source = pathmod.normcase(pathmod.normpath(abs_path))
    project = pathmod.normcase(pathmod.normpath(project_dir))
    # relpath raises on Windows across drives, and framework sources under
    # ~/.platformio can sit on a different drive from the clone.
    if pathmod.splitdrive(source)[0] != pathmod.splitdrive(project)[0]:
        return False
    try:
        if pathmod.commonpath([source, project]) != project:
            return False
    except ValueError:
        return False
    parts = pathmod.relpath(source, project).split(pathmod.sep)
    if len(parts) < 2:
        return False
    if parts[0] == "src":
        return True
    return parts[0] == "lib" and parts[1] not in VENDORED_LIBS
