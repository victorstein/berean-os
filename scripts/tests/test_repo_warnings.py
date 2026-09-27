import ntpath
import posixpath
import sys
import unittest
from pathlib import Path

# discover -s scripts/tests puts only this directory on sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from repo_warnings import is_repo_source  # noqa: E402

POSIX_PROJECT = "/work/berean-os"
WINDOWS_PROJECT = "D:\\proj"


def posix_owned(rel_path):
    return is_repo_source(posixpath.join(POSIX_PROJECT, rel_path), POSIX_PROJECT, pathmod=posixpath)


def windows_owned(abs_path):
    return is_repo_source(abs_path, WINDOWS_PROJECT, pathmod=ntpath)


class PosixPaths(unittest.TestCase):
    def test_src_and_repo_libs_are_owned(self):
        for rel_path in (
            "src/main.cpp",
            "src/activities/reader/EpubReaderActivity.cpp",
            "lib/hal/HalGPIO.cpp",
            "lib/Epub/Epub/Section.cpp",
            "lib/ProgressMapper/ProgressMapper.cpp",
        ):
            with self.subTest(rel_path=rel_path):
                self.assertTrue(posix_owned(rel_path))

    def test_vendored_and_third_party_sources_are_not_owned(self):
        for rel_path in (
            "lib/expat/xmlparse.c",
            "lib/miniz/src/miniz.c",
            "lib/uzlib/src/tinflate.c",
            "freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp",
            ".pio/libdeps/x4pro/QRCode/src/qrcode.c",
        ):
            with self.subTest(rel_path=rel_path):
                self.assertFalse(posix_owned(rel_path))

    def test_framework_outside_the_project_is_not_owned(self):
        framework_main = "/home/u/.platformio/packages/framework-arduinoespressif32/cores/esp32/main.cpp"
        self.assertFalse(is_repo_source(framework_main, POSIX_PROJECT, pathmod=posixpath))

    def test_sibling_directory_sharing_the_project_prefix_is_not_owned(self):
        self.assertFalse(is_repo_source("/work/berean-os-old/src/main.cpp", POSIX_PROJECT, pathmod=posixpath))

    def test_prefix_look_alikes(self):
        self.assertTrue(posix_owned("lib/expatfoo/x.cpp"))
        self.assertFalse(posix_owned("srcx/a.cpp"))
        self.assertFalse(posix_owned("libx/a.cpp"))

    def test_dot_dot_segments_are_normalised(self):
        self.assertFalse(posix_owned("../packages/framework-arduinoespressif32/cores/esp32/main.cpp"))
        self.assertFalse(posix_owned("src/../lib/expat/xmlparse.c"))
        self.assertTrue(posix_owned("src/../lib/hal/HalGPIO.cpp"))

    def test_file_at_the_project_root_is_not_owned(self):
        self.assertFalse(posix_owned("main.cpp"))


class WindowsPaths(unittest.TestCase):
    def test_same_drive_repo_source_is_owned(self):
        self.assertTrue(windows_owned("D:\\proj\\lib\\hal\\HalGPIO.cpp"))

    def test_forward_slashes_and_case_are_normalised(self):
        self.assertTrue(windows_owned("D:/proj/src/main.cpp"))
        self.assertTrue(windows_owned("d:\\PROJ\\src\\main.cpp"))

    def test_cross_drive_framework_is_not_owned_and_does_not_raise(self):
        framework_main = "C:\\Users\\u\\.platformio\\packages\\framework-arduinoespressif32\\cores\\esp32\\main.cpp"
        self.assertFalse(windows_owned(framework_main))

    def test_vendored_lib_is_not_owned(self):
        self.assertFalse(windows_owned("D:\\proj\\lib\\expat\\xmlparse.c"))


if __name__ == "__main__":
    unittest.main()
