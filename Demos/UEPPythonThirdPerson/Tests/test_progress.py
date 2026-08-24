"""Host-side tests for the Unreal-independent Third Person round state."""

from pathlib import Path
import sys
import unittest


SCRIPTS_ROOT = (
    Path(__file__).resolve().parents[1] / "Overlay" / "Content" / "Scripts"
)
sys.path.insert(0, str(SCRIPTS_ROOT))

from uep_third_person.state import DemoProgress  # noqa: E402


class DemoProgressTests(unittest.TestCase):
    def test_rejects_invalid_configuration(self):
        with self.assertRaises(ValueError):
            DemoProgress(0, 3.0)
        with self.assertRaises(ValueError):
            DemoProgress(6, 0.0)

    def test_collects_exact_target_and_enters_victory(self):
        progress = DemoProgress(3, 2.0)

        self.assertFalse(progress.collect())
        self.assertFalse(progress.collect())
        self.assertTrue(progress.collect())
        self.assertEqual(progress.round_score, 3)
        self.assertEqual(progress.total_score, 3)
        self.assertEqual(progress.phase, "victory")

        self.assertFalse(progress.collect())
        self.assertEqual(progress.total_score, 3)

    def test_victory_countdown_starts_clean_next_round(self):
        progress = DemoProgress(1, 1.0)
        self.assertTrue(progress.collect())

        self.assertFalse(progress.tick(0.4))
        self.assertTrue(progress.tick(0.6))
        self.assertTrue(progress.begin_next_round())
        self.assertEqual(progress.round, 2)
        self.assertEqual(progress.round_score, 0)
        self.assertEqual(progress.total_score, 1)
        self.assertEqual(progress.phase, "playing")

    def test_play_timer_ignores_negative_delta(self):
        progress = DemoProgress(2, 1.0)
        progress.tick(-10.0)
        progress.tick(0.25)
        self.assertEqual(progress.snapshot()["elapsed"], 0.25)


if __name__ == "__main__":
    unittest.main()
