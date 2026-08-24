"""A real viewport Slate HUD driven entirely by Python state."""

import unreal_engine as ue
from unreal_engine import FLinearColor, SBorder, SOverlay, STextBlock, SVerticalBox
from unreal_engine.enums import (
    EHorizontalAlignment,
    ESlateVisibility,
    EVerticalAlignment,
)
from unreal_engine.structs import SlateColor

from .utils import is_valid


PANEL_LINEAR = FLinearColor(0.015, 0.03, 0.06, 0.90)
TITLE_LINEAR = FLinearColor(0.15, 0.85, 1.0, 1.0)
PRIMARY_LINEAR = FLinearColor(0.95, 0.98, 1.0, 1.0)
SECONDARY_LINEAR = FLinearColor(0.58, 0.70, 0.80, 1.0)
SUCCESS_LINEAR = FLinearColor(0.20, 1.0, 0.55, 1.0)

# UE5.8's Slate constructors bind ColorAndOpacity as FSlateColor, while the
# mutable STextBlock.set_color_and_opacity wrapper still accepts FLinearColor.
PANEL_SLATE = SlateColor(SpecifiedColor=PANEL_LINEAR)
TITLE_SLATE = SlateColor(SpecifiedColor=TITLE_LINEAR)
PRIMARY_SLATE = SlateColor(SpecifiedColor=PRIMARY_LINEAR)
SECONDARY_SLATE = SlateColor(SpecifiedColor=SECONDARY_LINEAR)


class ThirdPersonHUD:
    """Own one Slate tree and attach it to one game viewport."""

    def __init__(self):
        self.root = None
        self.viewport = None
        self.attached = False
        self.update_count = 0
        self._last_values = None

        self.objective_text = None
        self.score_text = None
        self.status_text = None
        self.help_text = None

    def _build(self):
        title = STextBlock(
            text="UEP 0.6  |  PYTHON THIRD PERSON",
            color_and_opacity=TITLE_SLATE,
        )
        self.objective_text = STextBlock(
            text="Preparing Python gameplay...",
            color_and_opacity=PRIMARY_SLATE,
        )
        self.score_text = STextBlock(text="", color_and_opacity=SECONDARY_SLATE)
        self.status_text = STextBlock(text="", color_and_opacity=SECONDARY_SLATE)
        self.help_text = STextBlock(
            text="WASD / Left Stick  Move    Mouse / Right Stick  Look    Space / Face Button  Jump",
            color_and_opacity=PRIMARY_SLATE,
        )

        information = SVerticalBox()
        information.add_slot(title, auto_height=True, padding=(0.0, 0.0, 0.0, 8.0))
        information.add_slot(
            self.objective_text,
            auto_height=True,
            padding=(0.0, 0.0, 0.0, 4.0),
        )
        information.add_slot(self.score_text, auto_height=True)
        information.add_slot(
            self.status_text,
            auto_height=True,
            padding=(0.0, 4.0, 0.0, 0.0),
        )

        information_panel = SBorder(
            padding=(18.0, 14.0, 18.0, 14.0),
            border_background_color=PANEL_SLATE,
        )(information)
        help_panel = SBorder(
            padding=(16.0, 10.0, 16.0, 10.0),
            border_background_color=PANEL_SLATE,
        )(self.help_text)

        root = SOverlay()
        root.add_slot(
            information_panel,
            z_order=10,
            h_align=EHorizontalAlignment.HAlign_Left,
            padding=(28.0, 28.0, 0.0, 0.0),
            v_align=EVerticalAlignment.VAlign_Top,
        )
        root.add_slot(
            help_panel,
            z_order=10,
            h_align=EHorizontalAlignment.HAlign_Left,
            padding=(28.0, 0.0, 0.0, 24.0),
            v_align=EVerticalAlignment.VAlign_Bottom,
        )
        root.set_visibility(ESlateVisibility.HitTestInvisible)
        self.root = root

    def attach(self, world):
        self.detach()
        self._build()
        self.viewport = world.get_game_viewport()
        self.viewport.add_viewport_widget_content(self.root, 100)
        self.attached = True
        self.update_count = 0
        self._last_values = None
        ue.log("UEP_PYTHON_THIRD_PERSON_HUD_READY")

    def detach(self):
        was_attached = self.attached
        if self.attached and is_valid(self.viewport) and self.root is not None:
            try:
                self.viewport.remove_viewport_widget_content(self.root)
            except Exception:
                pass
        self.root = None
        self.viewport = None
        self.attached = False
        self._last_values = None
        if was_attached:
            ue.log("UEP_PYTHON_THIRD_PERSON_HUD_RELEASED")

    def update(self, progress, pawn):
        if not self.attached:
            return

        speed = float(getattr(pawn, "PythonAnimationSpeed", 0.0))
        animation = str(getattr(pawn, "PythonAnimationState", "unknown"))
        values = (
            progress.round,
            progress.round_score,
            progress.total_score,
            progress.target,
            int(progress.elapsed),
            progress.phase,
            int(progress.victory_remaining + 0.999),
            int(speed),
            animation,
        )
        if values == self._last_values:
            return
        self._last_values = values
        self.update_count += 1

        if progress.phase == "victory":
            self.objective_text.set_text(
                "ROUND COMPLETE  |  Next round in {0}".format(
                    max(0, values[6]),
                )
            )
            self.objective_text.set_color_and_opacity(SUCCESS_LINEAR)
        else:
            self.objective_text.set_text(
                "ROUND {0:02d}  |  Collect every floating Python orb".format(
                    progress.round,
                )
            )
            self.objective_text.set_color_and_opacity(PRIMARY_LINEAR)

        minutes, seconds = divmod(int(progress.elapsed), 60)
        self.score_text.set_text(
            "PICKUPS  {0}/{1}    TOTAL  {2}    TIME  {3:02d}:{4:02d}".format(
                progress.round_score,
                progress.target,
                progress.total_score,
                minutes,
                seconds,
            )
        )
        self.status_text.set_text(
            "PYTHON CHARACTER  |  speed {0:03d}  |  {1}".format(
                int(speed),
                animation.upper(),
            )
        )

    def snapshot(self):
        return {
            "attached": self.attached,
            "update_count": self.update_count,
            "slate_root": self.root is not None,
        }
