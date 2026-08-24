"""Pure-Python state for the collectible round loop."""


class DemoProgress:
    """Own scoring and round transitions without depending on Unreal objects."""

    def __init__(self, target, round_complete_seconds):
        if target <= 0:
            raise ValueError("target must be positive")
        if round_complete_seconds <= 0.0:
            raise ValueError("round_complete_seconds must be positive")

        self.target = int(target)
        self.round_complete_seconds = float(round_complete_seconds)
        self.round = 1
        self.round_score = 0
        self.total_score = 0
        self.elapsed = 0.0
        self.phase = "playing"
        self.victory_remaining = 0.0

    def collect(self):
        """Record one pickup and return True exactly when the round completes."""
        if self.phase != "playing" or self.round_score >= self.target:
            return False

        self.round_score += 1
        self.total_score += 1
        if self.round_score == self.target:
            self.phase = "victory"
            self.victory_remaining = self.round_complete_seconds
            return True
        return False

    def tick(self, delta_seconds):
        """Advance timers and return True when the next round should begin."""
        delta_seconds = max(0.0, float(delta_seconds))
        if self.phase == "playing":
            self.elapsed += delta_seconds
            return False

        self.victory_remaining = max(0.0, self.victory_remaining - delta_seconds)
        return self.victory_remaining <= 0.0

    def begin_next_round(self):
        if self.phase != "victory" or self.victory_remaining > 0.0:
            return False

        self.round += 1
        self.round_score = 0
        self.elapsed = 0.0
        self.phase = "playing"
        self.victory_remaining = 0.0
        return True

    def snapshot(self):
        return {
            "round": self.round,
            "round_score": self.round_score,
            "total_score": self.total_score,
            "target": self.target,
            "elapsed": round(self.elapsed, 3),
            "phase": self.phase,
            "victory_remaining": round(self.victory_remaining, 3),
        }
