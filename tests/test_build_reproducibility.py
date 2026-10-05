import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
INI = (ROOT / "platformio.ini").read_text(encoding="utf-8-sig")
WORKFLOW = (ROOT / ".github" / "workflows" / "firmware-ci.yml").read_text(
    encoding="utf-8-sig"
)


class BuildReproducibilityTests(unittest.TestCase):
    def test_pioarduino_release_is_pinned(self):
        self.assertIn(
            "releases/download/55.03.312/platform-espressif32.zip",
            INI,
        )
        self.assertNotIn("releases/download/stable/", INI)

    def test_ci_platformio_version_is_pinned(self):
        self.assertIn("platformio==6.2.0", WORKFLOW)
        self.assertNotIn("pip install --upgrade platformio", WORKFLOW)


if __name__ == "__main__":
    unittest.main()
