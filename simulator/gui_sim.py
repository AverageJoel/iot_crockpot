"""
GUI Simulator for crockpot display.

Renders the device GUI screens using Rich, simulating what would appear
on the actual small display. Mirrors the rendering logic from firmware/main/gui.c.

Screens:
- MAIN: Temperature display + state buttons (OFF/WARM/LOW/HIGH)
- SCHEDULE_SELECT: Choose from available schedules to run
- SCHEDULE_BUILDER: Create custom cooking schedules
- HISTORY: Temperature and state graph over time
- SETTINGS: WiFi and display configuration
"""

from dataclasses import dataclass, field
from enum import Enum, auto
from collections import deque
from typing import TYPE_CHECKING

from rich.align import Align
from rich.console import Console, Group, RenderableType
from rich.panel import Panel
from rich.style import Style
from rich.table import Table
from rich.text import Text

from crockpot_sim import CrockpotState, CrockpotStatus

if TYPE_CHECKING:
    from schedule import Schedule, ScheduleStep


class Screen(Enum):
    """GUI screens."""
    MAIN = auto()
    SCHEDULE_SELECT = auto()
    SCHEDULE_BUILDER = auto()
    HISTORY = auto()
    SETTINGS = auto()


# Screen cycle order for navigation
SCREEN_ORDER = [
    Screen.MAIN,
    Screen.SCHEDULE_SELECT,
    Screen.SCHEDULE_BUILDER,
    Screen.HISTORY,
    Screen.SETTINGS,
]


@dataclass
class Theme:
    """Color theme."""
    background: str = "black"
    text: str = "white"
    text_dim: str = "bright_black"
    accent: str = "cyan"
    state_off: str = "bright_black"
    state_warm: str = "yellow"
    state_low: str = "dark_orange"
    state_high: str = "red"
    error: str = "red"
    success: str = "green"


@dataclass
class DisplayConfig:
    """Simulated display configuration."""
    width: int = 320
    height: int = 240
    name: str = "320x240 TFT"


DISPLAY_PRESETS = {
    "128x128": DisplayConfig(128, 128, "1.44\" Square"),
    "240x135": DisplayConfig(240, 135, "1.14\" Wide"),
    "240x240": DisplayConfig(240, 240, "1.3\" Square"),
    "320x240": DisplayConfig(320, 240, "2.8\" TFT"),
}


# Sparkline characters for temperature graph
SPARK_CHARS = "_.,-~=+*#"


@dataclass
class HistoryEntry:
    """Single entry in temperature history."""
    temperature_f: float
    state: CrockpotState
    timestamp: int


class GUISimulator:
    """Simulates the crockpot GUI screens."""

    HISTORY_SIZE = 60  # Number of history points to display

    def __init__(
        self,
        display: DisplayConfig | None = None,
        theme: Theme | None = None,
    ):
        self.display = display or DisplayConfig()
        self.theme = theme or Theme()
        self.current_screen = Screen.MAIN
        self.previous_screen = Screen.MAIN

        # Message overlay
        self.message: str = ""
        self.message_is_error: bool = False

        # Settings
        self.show_celsius: bool = False
        self.wifi_ssid: str = "CrockNet"
        self.wifi_connected: bool = True

        # Cached status
        self._status: CrockpotStatus | None = None

        # Temperature history for graph
        self._temp_history: deque[HistoryEntry] = deque(maxlen=self.HISTORY_SIZE)

        # Schedule select state
        self._schedule_list: list["Schedule"] = []
        self._schedule_index: int = 0

        # Schedule builder state
        self._builder_steps: list[tuple[CrockpotState, int]] = []  # (state, duration_seconds)
        self._builder_cursor: int = 0  # 0=state, 1=hours, 2=minutes
        self._builder_state: CrockpotState = CrockpotState.HIGH
        self._builder_hours: int = 1
        self._builder_minutes: int = 0

        # Settings menu state
        self._settings_index: int = 0

    # =========================================================================
    # Navigation
    # =========================================================================

    def set_screen(self, screen: Screen) -> None:
        """Switch to a different screen."""
        self.previous_screen = self.current_screen
        self.current_screen = screen

    def next_screen(self) -> None:
        """Cycle to next screen."""
        idx = SCREEN_ORDER.index(self.current_screen)
        self.current_screen = SCREEN_ORDER[(idx + 1) % len(SCREEN_ORDER)]

    def prev_screen(self) -> None:
        """Cycle to previous screen."""
        idx = SCREEN_ORDER.index(self.current_screen)
        self.current_screen = SCREEN_ORDER[(idx - 1) % len(SCREEN_ORDER)]

    def go_back(self) -> None:
        """Return to previous screen."""
        self.current_screen = self.previous_screen
        self.previous_screen = Screen.MAIN

    # =========================================================================
    # State Updates
    # =========================================================================

    def update_status(self, status: CrockpotStatus) -> None:
        """Update cached status and record history."""
        self._status = status
        self.wifi_connected = status.wifi_connected

        # Record temperature history
        entry = HistoryEntry(
            temperature_f=status.temperature_f,
            state=status.state,
            timestamp=status.uptime_seconds,
        )
        self._temp_history.append(entry)

    def set_schedule_list(self, schedules: list["Schedule"]) -> None:
        """Set available schedules for selection screen."""
        self._schedule_list = schedules
        if self._schedule_index >= len(schedules):
            self._schedule_index = 0

    def show_message(self, msg: str, is_error: bool = False) -> None:
        """Show temporary message overlay."""
        self.message = msg
        self.message_is_error = is_error

    def dismiss_message(self) -> None:
        """Clear message overlay."""
        self.message = ""

    # =========================================================================
    # Helpers
    # =========================================================================

    def _get_state_color(self, state: CrockpotState) -> str:
        """Get color for a crockpot state."""
        colors = {
            CrockpotState.OFF: self.theme.state_off,
            CrockpotState.WARM: self.theme.state_warm,
            CrockpotState.LOW: self.theme.state_low,
            CrockpotState.HIGH: self.theme.state_high,
        }
        return colors.get(state, self.theme.text)

    def _format_temp(self, temp_f: float) -> str:
        """Format temperature with unit."""
        if self.show_celsius:
            temp_c = (temp_f - 32.0) * 5.0 / 9.0
            return f"{temp_c:.0f}C"
        return f"{temp_f:.0f}F"

    def _format_duration(self, seconds: int) -> str:
        """Format duration as Xh Ym."""
        if seconds == 0:
            return "hold"
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        if hours > 0 and minutes > 0:
            return f"{hours}h{minutes}m"
        elif hours > 0:
            return f"{hours}h"
        else:
            return f"{minutes}m"

    # =========================================================================
    # Screen Renderers
    # =========================================================================

    def _render_main_screen(self) -> RenderableType:
        """Render main screen with temperature and state buttons."""
        if not self._status:
            return Text("No status", style=self.theme.text_dim)

        status = self._status
        lines = []

        # Temperature (large)
        temp_str = self._format_temp(status.temperature_f)
        temp_color = self.theme.error if status.temperature_f >= 300 else self.theme.text
        lines.append(Text(temp_str, style=Style(color=temp_color, bold=True)))

        # Sensor error
        if status.sensor_error:
            lines.append(Text("SENSOR ERROR", style=Style(color=self.theme.error, bold=True)))

        # Schedule indicator
        if status.schedule_active:
            remaining = status.schedule_step_remaining
            if remaining > 0:
                mins = remaining // 60
                lines.append(Text(f"{status.schedule_name} ({mins}m)", style=self.theme.accent))
            else:
                lines.append(Text(f"{status.schedule_name}", style=self.theme.accent))

        lines.append(Text(""))  # Spacer

        # State buttons (compact)
        buttons = Text()
        for state in [CrockpotState.OFF, CrockpotState.WARM, CrockpotState.LOW, CrockpotState.HIGH]:
            color = self._get_state_color(state)
            label = state.name[0]  # O, W, L, H
            if state == status.state:
                buttons.append(f"[{label}]", style=Style(color="white", bgcolor=color, bold=True))
            else:
                buttons.append(f" {label} ", style=Style(color=color))
        lines.append(buttons)

        return Align.center(Group(*lines))

    def _render_schedule_select_screen(self) -> RenderableType:
        """Render schedule selection screen."""
        lines = []
        lines.append(Text("Select Schedule", style=Style(color=self.theme.accent, bold=True)))
        lines.append(Text(""))

        if not self._schedule_list:
            lines.append(Text("No schedules available", style=self.theme.text_dim))
        else:
            for i, schedule in enumerate(self._schedule_list):
                prefix = ">" if i == self._schedule_index else " "
                style = "bold" if i == self._schedule_index else ""

                # Show schedule name and summary
                steps_summary = " > ".join(
                    f"{s.state.name[0]}{self._format_duration(s.duration_seconds)}"
                    for s in schedule.steps[:3]
                )
                if len(schedule.steps) > 3:
                    steps_summary += "..."

                lines.append(Text(f"{prefix} {schedule.name}", style=style))
                lines.append(Text(f"   {steps_summary}", style=self.theme.text_dim))

        lines.append(Text(""))
        lines.append(Text("[UP/DOWN] select  [ENTER] start", style=self.theme.text_dim))

        return Align.center(Group(*lines))

    def _render_schedule_builder_screen(self) -> RenderableType:
        """Render schedule builder screen."""
        lines = []
        lines.append(Text("Build Schedule", style=Style(color=self.theme.accent, bold=True)))
        lines.append(Text(""))

        # Current steps
        if self._builder_steps:
            steps_text = " > ".join(
                f"{s.name[0]}{self._format_duration(d)}"
                for s, d in self._builder_steps
            )
            lines.append(Text(steps_text, style=self.theme.text))
        else:
            lines.append(Text("(no steps yet)", style=self.theme.text_dim))

        lines.append(Text(""))

        # Current input
        state_color = self._get_state_color(self._builder_state)
        state_style = Style(color=state_color, bold=True, reverse=self._builder_cursor == 0)
        hours_style = Style(bold=True, reverse=self._builder_cursor == 1)
        mins_style = Style(bold=True, reverse=self._builder_cursor == 2)

        input_line = Text("Add: ")
        input_line.append(self._builder_state.name, style=state_style)
        input_line.append(" ")
        input_line.append(f"{self._builder_hours}h", style=hours_style)
        input_line.append(f"{self._builder_minutes:02d}m", style=mins_style)
        lines.append(input_line)

        lines.append(Text(""))
        lines.append(Text("[</>] adjust  [ENTER] add  [S] save", style=self.theme.text_dim))

        return Align.center(Group(*lines))

    def _render_history_screen(self) -> RenderableType:
        """Render temperature history graph."""
        lines = []
        lines.append(Text("Temperature History", style=Style(color=self.theme.accent, bold=True)))
        lines.append(Text(""))

        if not self._temp_history:
            lines.append(Text("No data yet", style=self.theme.text_dim))
        else:
            # Temperature range
            temps = [e.temperature_f for e in self._temp_history]
            min_t = min(temps)
            max_t = max(temps)
            current_t = temps[-1]

            # Stats line
            lines.append(Text(f"Now: {current_t:.0f}F  Min: {min_t:.0f}F  Max: {max_t:.0f}F", style=self.theme.text_dim))
            lines.append(Text(""))

            # Sparkline graph
            range_t = max(max_t - min_t, 10)  # Minimum range of 10F
            sparkline = ""
            for entry in self._temp_history:
                norm = (entry.temperature_f - min_t) / range_t
                norm = max(0, min(1, norm))
                idx = int(norm * (len(SPARK_CHARS) - 1))
                sparkline += SPARK_CHARS[idx]

            lines.append(Text(sparkline, style=self.theme.accent))

            # State timeline
            state_line = ""
            for entry in self._temp_history:
                state_line += entry.state.name[0]  # O, W, L, H
            lines.append(Text(state_line, style=self.theme.text_dim))

        return Align.center(Group(*lines))

    def _render_settings_screen(self) -> RenderableType:
        """Render settings screen."""
        lines = []
        lines.append(Text("Settings", style=Style(color=self.theme.accent, bold=True)))
        lines.append(Text(""))

        settings = [
            ("WiFi SSID", self.wifi_ssid),
            ("WiFi Status", "Connected" if self.wifi_connected else "Disconnected"),
            ("Temperature", "Celsius" if self.show_celsius else "Fahrenheit"),
            ("Display", self.display.name),
        ]

        for i, (label, value) in enumerate(settings):
            prefix = ">" if i == self._settings_index else " "
            style = "bold" if i == self._settings_index else ""
            value_style = self.theme.success if "Connected" in value else self.theme.text

            line = Text(f"{prefix} {label}: ")
            line.append(value, style=value_style)
            lines.append(line)

        lines.append(Text(""))
        lines.append(Text("[UP/DOWN] select  [ENTER] edit", style=self.theme.text_dim))

        return Align.center(Group(*lines))

    def _render_message_overlay(self) -> RenderableType | None:
        """Render message overlay if active."""
        if not self.message:
            return None

        style = self.theme.error if self.message_is_error else self.theme.accent
        return Panel(
            Align.center(Text(self.message, style="white bold")),
            style=style,
            padding=(0, 2),
        )

    # =========================================================================
    # Input Handling
    # =========================================================================

    def handle_up(self) -> None:
        """Handle UP input."""
        if self.current_screen == Screen.SCHEDULE_SELECT:
            if self._schedule_list:
                self._schedule_index = (self._schedule_index - 1) % len(self._schedule_list)
        elif self.current_screen == Screen.SCHEDULE_BUILDER:
            if self._builder_cursor == 0:
                states = list(CrockpotState)
                idx = states.index(self._builder_state)
                self._builder_state = states[(idx - 1) % len(states)]
            elif self._builder_cursor == 1:
                self._builder_hours = min(24, self._builder_hours + 1)
            elif self._builder_cursor == 2:
                self._builder_minutes = (self._builder_minutes + 15) % 60
        elif self.current_screen == Screen.SETTINGS:
            self._settings_index = max(0, self._settings_index - 1)

    def handle_down(self) -> None:
        """Handle DOWN input."""
        if self.current_screen == Screen.SCHEDULE_SELECT:
            if self._schedule_list:
                self._schedule_index = (self._schedule_index + 1) % len(self._schedule_list)
        elif self.current_screen == Screen.SCHEDULE_BUILDER:
            if self._builder_cursor == 0:
                states = list(CrockpotState)
                idx = states.index(self._builder_state)
                self._builder_state = states[(idx + 1) % len(states)]
            elif self._builder_cursor == 1:
                self._builder_hours = max(0, self._builder_hours - 1)
            elif self._builder_cursor == 2:
                self._builder_minutes = (self._builder_minutes - 15) % 60
        elif self.current_screen == Screen.SETTINGS:
            self._settings_index = min(3, self._settings_index + 1)

    def handle_left(self) -> None:
        """Handle LEFT input."""
        if self.current_screen == Screen.SCHEDULE_BUILDER:
            self._builder_cursor = max(0, self._builder_cursor - 1)

    def handle_right(self) -> None:
        """Handle RIGHT input."""
        if self.current_screen == Screen.SCHEDULE_BUILDER:
            self._builder_cursor = min(2, self._builder_cursor + 1)

    def handle_enter(self) -> "Schedule | None":
        """Handle ENTER input. Returns schedule if one was selected."""
        if self.current_screen == Screen.SCHEDULE_SELECT:
            if self._schedule_list:
                return self._schedule_list[self._schedule_index]
        elif self.current_screen == Screen.SCHEDULE_BUILDER:
            # Add current step
            duration = self._builder_hours * 3600 + self._builder_minutes * 60
            self._builder_steps.append((self._builder_state, duration))
            # Reset for next step
            self._builder_hours = 1
            self._builder_minutes = 0
        elif self.current_screen == Screen.SETTINGS:
            if self._settings_index == 2:  # Temperature unit
                self.show_celsius = not self.show_celsius
        return None

    def get_built_schedule(self) -> "Schedule | None":
        """Get the schedule from builder and clear it."""
        if not self._builder_steps:
            return None

        from schedule import Schedule, ScheduleStep
        steps = [
            ScheduleStep(state=state, duration_seconds=duration)
            for state, duration in self._builder_steps
        ]
        schedule = Schedule(name="Custom", steps=steps)
        self._builder_steps = []
        return schedule

    def clear_builder(self) -> None:
        """Clear the schedule builder."""
        self._builder_steps = []
        self._builder_cursor = 0
        self._builder_state = CrockpotState.HIGH
        self._builder_hours = 1
        self._builder_minutes = 0

    # =========================================================================
    # Main Render
    # =========================================================================

    def render(self) -> Panel:
        """Render the complete simulated display."""
        screen_renderers = {
            Screen.MAIN: self._render_main_screen,
            Screen.SCHEDULE_SELECT: self._render_schedule_select_screen,
            Screen.SCHEDULE_BUILDER: self._render_schedule_builder_screen,
            Screen.HISTORY: self._render_history_screen,
            Screen.SETTINGS: self._render_settings_screen,
        }

        renderer = screen_renderers.get(self.current_screen, self._render_main_screen)
        screen_content = renderer()

        # Add message overlay if present
        overlay = self._render_message_overlay()
        if overlay:
            screen_content = Group(screen_content, Text(""), overlay)

        # Panel sizing
        panel_width = min(45, max(30, self.display.width // 8))

        return Panel(
            screen_content,
            title=f"[{self.display.name}]",
            subtitle=f"[dim]{self.current_screen.name}[/]",
            width=panel_width,
            height=14,
            style=f"on {self.theme.background}",
            border_style=self.theme.text_dim,
        )


class DarkTheme(Theme):
    """Dark theme (default)."""
    pass


class LightTheme(Theme):
    """Light theme for outdoor visibility."""
    background: str = "white"
    text: str = "black"
    text_dim: str = "bright_black"
    state_off: str = "grey50"
