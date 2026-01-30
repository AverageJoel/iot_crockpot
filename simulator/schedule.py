"""
Schedule management for programmable cooking programs.

Supports multi-step cooking schedules like "HIGH 3h -> LOW 6h -> WARM".
"""

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Callable
import json

from crockpot_sim import CrockpotState


@dataclass
class ScheduleStep:
    """A single step in a cooking schedule."""
    state: CrockpotState
    duration_seconds: int  # 0 = indefinite (final step only)

    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "state": self.state.name,
            "duration_seconds": self.duration_seconds,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "ScheduleStep":
        """Create from dictionary."""
        return cls(
            state=CrockpotState[data["state"]],
            duration_seconds=data["duration_seconds"],
        )


@dataclass
class Schedule:
    """A complete cooking schedule with multiple steps."""
    name: str
    steps: list[ScheduleStep] = field(default_factory=list)
    repeat: bool = False

    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "name": self.name,
            "steps": [step.to_dict() for step in self.steps],
            "repeat": self.repeat,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "Schedule":
        """Create from dictionary."""
        return cls(
            name=data["name"],
            steps=[ScheduleStep.from_dict(s) for s in data["steps"]],
            repeat=data.get("repeat", False),
        )

    @property
    def total_duration_seconds(self) -> int:
        """Total duration of all steps (excludes indefinite final step)."""
        return sum(s.duration_seconds for s in self.steps)

    def format_duration(self, seconds: int) -> str:
        """Format duration as human-readable string."""
        if seconds == 0:
            return "indefinite"
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        if hours > 0 and minutes > 0:
            return f"{hours}h {minutes}m"
        elif hours > 0:
            return f"{hours}h"
        else:
            return f"{minutes}m"


# Preset schedules
PRESET_SCHEDULES = [
    Schedule(
        name="Slow Cook",
        steps=[
            ScheduleStep(CrockpotState.HIGH, 3 * 3600),   # HIGH 3h
            ScheduleStep(CrockpotState.LOW, 6 * 3600),    # LOW 6h
            ScheduleStep(CrockpotState.WARM, 0),          # WARM indefinite
        ],
    ),
    Schedule(
        name="Quick Warm",
        steps=[
            ScheduleStep(CrockpotState.HIGH, 1 * 3600),   # HIGH 1h
            ScheduleStep(CrockpotState.WARM, 0),          # WARM indefinite
        ],
    ),
    Schedule(
        name="All Day",
        steps=[
            ScheduleStep(CrockpotState.LOW, 8 * 3600),    # LOW 8h
            ScheduleStep(CrockpotState.WARM, 0),          # WARM indefinite
        ],
    ),
]


class ScheduleManager:
    """
    Manages schedule execution.

    Call tick() every second from the control loop.
    """

    # Default path for custom schedules
    DEFAULT_SCHEDULE_PATH = Path.home() / ".crockpot" / "schedules.json"

    def __init__(
        self,
        on_state_change: Callable[[CrockpotState], None] | None = None,
        on_schedule_complete: Callable[[str], None] | None = None,
        on_step_change: Callable[[int, ScheduleStep], None] | None = None,
        schedule_path: Path | None = None,
    ):
        self._on_state_change = on_state_change
        self._on_schedule_complete = on_schedule_complete
        self._on_step_change = on_step_change
        self._schedule_path = schedule_path or self.DEFAULT_SCHEDULE_PATH

        # Current schedule state
        self._active_schedule: Schedule | None = None
        self._current_step_index: int = 0
        self._step_elapsed_seconds: int = 0

        # Available schedules (presets + custom)
        self._custom_schedules: list[Schedule] = []
        self._load_custom_schedules()

    @property
    def is_active(self) -> bool:
        """Whether a schedule is currently running."""
        return self._active_schedule is not None

    @property
    def active_schedule(self) -> Schedule | None:
        """The currently running schedule."""
        return self._active_schedule

    @property
    def current_step_index(self) -> int:
        """Index of the current step (0-based)."""
        return self._current_step_index

    @property
    def current_step(self) -> ScheduleStep | None:
        """The current step being executed."""
        if self._active_schedule and self._current_step_index < len(self._active_schedule.steps):
            return self._active_schedule.steps[self._current_step_index]
        return None

    @property
    def step_elapsed_seconds(self) -> int:
        """Seconds elapsed in current step."""
        return self._step_elapsed_seconds

    @property
    def step_remaining_seconds(self) -> int:
        """Seconds remaining in current step (0 if indefinite)."""
        step = self.current_step
        if step and step.duration_seconds > 0:
            return max(0, step.duration_seconds - self._step_elapsed_seconds)
        return 0

    @property
    def total_steps(self) -> int:
        """Total number of steps in active schedule."""
        if self._active_schedule:
            return len(self._active_schedule.steps)
        return 0

    @property
    def all_schedules(self) -> list[Schedule]:
        """All available schedules (presets + custom)."""
        return PRESET_SCHEDULES + self._custom_schedules

    def start(self, schedule: Schedule) -> None:
        """Start executing a schedule."""
        if not schedule.steps:
            return

        self._active_schedule = schedule
        self._current_step_index = 0
        self._step_elapsed_seconds = 0

        # Apply first step's state
        first_step = schedule.steps[0]
        if self._on_state_change:
            self._on_state_change(first_step.state)
        if self._on_step_change:
            self._on_step_change(0, first_step)

    def stop(self) -> None:
        """Stop the current schedule."""
        self._active_schedule = None
        self._current_step_index = 0
        self._step_elapsed_seconds = 0

    def tick(self) -> None:
        """
        Advance the schedule by one second.

        Call this every second from the control loop.
        """
        if not self._active_schedule:
            return

        step = self.current_step
        if not step:
            return

        self._step_elapsed_seconds += 1

        # Check if step is complete (duration > 0 means timed step)
        if step.duration_seconds > 0 and self._step_elapsed_seconds >= step.duration_seconds:
            self._advance_step()

    def _advance_step(self) -> None:
        """Advance to the next step in the schedule."""
        if not self._active_schedule:
            return

        next_index = self._current_step_index + 1

        # Check if we've reached the end
        if next_index >= len(self._active_schedule.steps):
            if self._active_schedule.repeat:
                # Restart from beginning
                next_index = 0
            else:
                # Schedule complete
                schedule_name = self._active_schedule.name
                self._active_schedule = None
                self._current_step_index = 0
                self._step_elapsed_seconds = 0
                if self._on_schedule_complete:
                    self._on_schedule_complete(schedule_name)
                return

        # Move to next step
        self._current_step_index = next_index
        self._step_elapsed_seconds = 0

        next_step = self._active_schedule.steps[next_index]
        if self._on_state_change:
            self._on_state_change(next_step.state)
        if self._on_step_change:
            self._on_step_change(next_index, next_step)

    def add_custom_schedule(self, schedule: Schedule) -> None:
        """Add a custom schedule and save to disk."""
        # Check if a schedule with this name already exists
        for i, s in enumerate(self._custom_schedules):
            if s.name == schedule.name:
                self._custom_schedules[i] = schedule
                self._save_custom_schedules()
                return

        self._custom_schedules.append(schedule)
        self._save_custom_schedules()

    def remove_custom_schedule(self, name: str) -> bool:
        """Remove a custom schedule by name."""
        for i, s in enumerate(self._custom_schedules):
            if s.name == name:
                del self._custom_schedules[i]
                self._save_custom_schedules()
                return True
        return False

    def get_schedule_by_name(self, name: str) -> Schedule | None:
        """Find a schedule by name."""
        for schedule in self.all_schedules:
            if schedule.name == name:
                return schedule
        return None

    def _load_custom_schedules(self) -> None:
        """Load custom schedules from JSON file."""
        if not self._schedule_path.exists():
            return

        try:
            with open(self._schedule_path, "r") as f:
                data = json.load(f)
            self._custom_schedules = [Schedule.from_dict(s) for s in data.get("schedules", [])]
        except (json.JSONDecodeError, KeyError, ValueError):
            # Invalid file, start fresh
            self._custom_schedules = []

    def _save_custom_schedules(self) -> None:
        """Save custom schedules to JSON file."""
        # Ensure directory exists
        self._schedule_path.parent.mkdir(parents=True, exist_ok=True)

        data = {
            "schedules": [s.to_dict() for s in self._custom_schedules],
        }

        with open(self._schedule_path, "w") as f:
            json.dump(data, f, indent=2)

    def format_status(self) -> str:
        """Format current schedule status as string."""
        if not self._active_schedule:
            return "No schedule"

        step = self.current_step
        if not step:
            return "No schedule"

        step_num = self._current_step_index + 1
        total_steps = len(self._active_schedule.steps)
        state_name = step.state.name

        if step.duration_seconds > 0:
            remaining = self.step_remaining_seconds
            mins = remaining // 60
            secs = remaining % 60
            return f"{self._active_schedule.name} - Step {step_num}/{total_steps}: {state_name} ({mins}:{secs:02d} left)"
        else:
            return f"{self._active_schedule.name} - Step {step_num}/{total_steps}: {state_name} (indefinite)"

    def get_step_progress(self) -> float:
        """Get progress through current step (0.0 to 1.0)."""
        step = self.current_step
        if not step or step.duration_seconds == 0:
            return 0.0
        return min(1.0, self._step_elapsed_seconds / step.duration_seconds)
