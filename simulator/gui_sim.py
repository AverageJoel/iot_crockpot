"""
GUI Simulator for crockpot display.

Renders the device GUI screens using Rich, simulating what would appear
on the actual small display. Mirrors the rendering logic from firmware/main/gui.c.

Display sizes to test:
- 128x128 (1.44" square)
- 240x135 (1.14" wide)
- 240x240 (1.3" square)
- 320x240 (2.8" standard)
"""

from dataclasses import dataclass
from enum import Enum, auto

from rich.align import Align
from rich.console import Console, Group, RenderableType
from rich.panel import Panel
from rich.style import Style
from rich.table import Table
from rich.text import Text

from crockpot_sim import CrockpotState, CrockpotStatus


class Screen(Enum):
    """GUI screens matching gui.h GUI_SCREEN_* enum."""
    MAIN = auto()
    SETTINGS = auto()
    WIFI = auto()
    INFO = auto()
    SCHEDULE = auto()


@dataclass
class Theme:
    """Color theme matching gui.h gui_theme_t."""
    background: str = "black"
    text: str = "white"
    text_dim: str = "bright_black"
    accent: str = "blue"
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


# Preset display configurations
DISPLAY_PRESETS = {
    "128x128": DisplayConfig(128, 128, "1.44\" Square"),
    "240x135": DisplayConfig(240, 135, "1.14\" Wide"),
    "240x240": DisplayConfig(240, 240, "1.3\" Square"),
    "320x240": DisplayConfig(320, 240, "2.8\" TFT"),
}


class GUISimulator:
    """
    Simulates the crockpot GUI screens.

    Renders using Rich to approximate what the actual display would show.
    Use keyboard to navigate and interact.
    """

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

        # Config
        self.show_celsius: bool = False
        self.show_wifi: bool = True

        # Cached status
        self._status: CrockpotStatus | None = None

    def set_screen(self, screen: Screen) -> None:
        """Switch to a different screen."""
        self.previous_screen = self.current_screen
        self.current_screen = screen

    def go_back(self) -> None:
        """Return to previous screen."""
        self.current_screen = self.previous_screen
        self.previous_screen = Screen.MAIN

    def show_message(self, msg: str, is_error: bool = False) -> None:
        """Show temporary message overlay."""
        self.message = msg
        self.message_is_error = is_error

    def dismiss_message(self) -> None:
        """Clear message overlay."""
        self.message = ""
        self.message_is_error = False

    def update_status(self, status: CrockpotStatus) -> None:
        """Update cached status for rendering."""
        self._status = status

    def _get_state_style(self, state: CrockpotState) -> Style:
        """Get Rich style for crockpot state."""
        colors = {
            CrockpotState.OFF: self.theme.state_off,
            CrockpotState.WARM: self.theme.state_warm,
            CrockpotState.LOW: self.theme.state_low,
            CrockpotState.HIGH: self.theme.state_high,
        }
        return Style(color=colors.get(state, self.theme.text), bold=True)

    def _format_uptime(self, seconds: int) -> str:
        """Format uptime as HH:MM:SS or Xd HH:MM."""
        if seconds < 86400:  # Less than 1 day
            h = seconds // 3600
            m = (seconds % 3600) // 60
            s = seconds % 60
            return f"{h:02d}:{m:02d}:{s:02d}"
        else:
            d = seconds // 86400
            h = (seconds % 86400) // 3600
            m = (seconds % 3600) // 60
            return f"{d}d {h:02d}:{m:02d}"

    def _format_temp(self, temp_f: float) -> str:
        """Format temperature with unit."""
        if self.show_celsius:
            temp_c = (temp_f - 32.0) * 5.0 / 9.0
            return f"{temp_c:.1f}C"
        return f"{temp_f:.1f}F"

    # =========================================================================
    # Screen Renderers
    # =========================================================================

    def _render_state_buttons(self, current_state: CrockpotState) -> Text:
        """Render the state selection buttons."""
        buttons = Text()

        states = [
            (CrockpotState.OFF, "OFF", self.theme.state_off),
            (CrockpotState.WARM, "WARM", self.theme.state_warm),
            (CrockpotState.LOW, "LOW", self.theme.state_low),
            (CrockpotState.HIGH, "HIGH", self.theme.state_high),
        ]

        for i, (state, label, color) in enumerate(states):
            if i > 0:
                buttons.append("  ")  # Spacing between buttons

            if state == current_state:
                # Selected state - filled/highlighted
                buttons.append(f"[{label}]", style=Style(color="white", bgcolor=color, bold=True))
            else:
                # Unselected state - outline style
                buttons.append(f" {label} ", style=Style(color=color))

        return buttons

    def _render_main_screen(self) -> RenderableType:
        """
        Render main status screen.
        Mirrors render_main_screen() from gui.c.
        """
        if not self._status:
            return Text("No status", style=self.theme.text_dim)

        status = self._status
        state_style = self._get_state_style(status.state)

        # Build the screen content
        content = Text()

        # Temperature - large and centered
        temp_str = self._format_temp(status.temperature_f)
        temp_style = self.theme.error if status.temperature_f >= 300 else self.theme.text
        content.append("\n")
        content.append(temp_str, style=Style(color=temp_style, bold=True))
        content.append("\n")

        # Sensor error indicator
        if status.sensor_error:
            content.append("\n")
            content.append("SENSOR ERROR", style=Style(color=self.theme.error, bold=True))

        # Build the centered main content
        main_content = Align.center(content)

        # State selection buttons
        state_buttons = Align.center(self._render_state_buttons(status.state))

        # Status bar at bottom
        status_bar = Table.grid(expand=True)
        status_bar.add_column(justify="left")
        status_bar.add_column(justify="right")

        # WiFi status
        if self.show_wifi:
            wifi_text = Text("WiFi", style=self.theme.success if status.wifi_connected else self.theme.text_dim)
        else:
            wifi_text = Text("")

        # Uptime
        uptime_text = Text(self._format_uptime(status.uptime_seconds), style=self.theme.text_dim)

        status_bar.add_row(wifi_text, uptime_text)

        return Group(
            main_content,
            Text(""),  # Spacer
            state_buttons,
            Text(""),  # Spacer
            status_bar,
        )

    def _render_settings_screen(self) -> RenderableType:
        """Render settings menu screen."""
        content = Text()
        content.append("Settings\n", style=Style(color=self.theme.accent, bold=True))
        content.append("\n")
        content.append("Temperature: ", style=self.theme.text)
        content.append("F" if not self.show_celsius else "C", style=self.theme.accent)
        content.append("\n")
        content.append("WiFi status: ", style=self.theme.text)
        content.append("Show" if self.show_wifi else "Hide", style=self.theme.accent)
        content.append("\n\n")
        content.append("(Not fully implemented)", style=self.theme.text_dim)

        return Group(
            Align.center(content),
            Text(""),
            Align.center(Text("[Press BACK to return]", style=self.theme.text_dim)),
        )

    def _render_wifi_screen(self) -> RenderableType:
        """Render WiFi status screen."""
        if not self._status:
            return Text("No status", style=self.theme.text_dim)

        status = self._status
        content = Text()
        content.append("WiFi\n", style=Style(color=self.theme.accent, bold=True))
        content.append("\n")

        if status.wifi_connected:
            content.append("Connected", style=self.theme.success)
            content.append("\n\n")
            content.append("SSID: ", style=self.theme.text_dim)
            content.append("CrockNet", style=self.theme.text)  # Simulated
            content.append("\n")
            content.append("IP: ", style=self.theme.text_dim)
            content.append("192.168.1.42", style=self.theme.text)  # Simulated
        else:
            content.append("Disconnected", style=self.theme.error)
            content.append("\n\n")
            content.append("Reconnecting...", style=self.theme.text_dim)

        return Group(
            Align.center(content),
            Text(""),
            Align.center(Text("[Press BACK to return]", style=self.theme.text_dim)),
        )

    def _render_info_screen(self) -> RenderableType:
        """Render device info screen."""
        if not self._status:
            return Text("No status", style=self.theme.text_dim)

        status = self._status
        content = Text()
        content.append("Device Info\n", style=Style(color=self.theme.accent, bold=True))
        content.append("\n")

        # Info rows
        content.append("Uptime: ", style=self.theme.text_dim)
        content.append(self._format_uptime(status.uptime_seconds), style=self.theme.text)
        content.append("\n")

        content.append("Version: ", style=self.theme.text_dim)
        content.append("v1.0.0", style=self.theme.text)
        content.append("\n")

        content.append("Display: ", style=self.theme.text_dim)
        content.append(f"{self.display.width}x{self.display.height}", style=self.theme.text)
        content.append("\n")

        content.append("Build: ", style=self.theme.text_dim)
        content.append("Simulator", style=self.theme.text)

        return Group(
            Align.center(content),
            Text(""),
            Align.center(Text("[Press BACK to return]", style=self.theme.text_dim)),
        )

    def _render_schedule_screen(self) -> RenderableType:
        """Render schedule status screen."""
        if not self._status:
            return Text("No status", style=self.theme.text_dim)

        status = self._status
        content = Text()
        content.append("Schedule\n", style=Style(color=self.theme.accent, bold=True))
        content.append("\n")

        if status.schedule_active:
            # Schedule name
            content.append("Active: ", style=self.theme.text_dim)
            content.append(status.schedule_name, style=Style(color="cyan", bold=True))
            content.append("\n\n")

            # Current step
            step_num = status.schedule_step + 1
            total_steps = status.schedule_total_steps
            content.append("Step: ", style=self.theme.text_dim)
            content.append(f"{step_num} / {total_steps}", style=self.theme.text)
            content.append("\n")

            # Current state
            state_color = self._get_state_style(status.state).color
            content.append("State: ", style=self.theme.text_dim)
            content.append(status.state.name, style=Style(color=state_color, bold=True))
            content.append("\n")

            # Time remaining
            if status.schedule_step_remaining > 0:
                mins = status.schedule_step_remaining // 60
                secs = status.schedule_step_remaining % 60
                content.append("Remaining: ", style=self.theme.text_dim)
                content.append(f"{mins}:{secs:02d}", style=self.theme.text)
            else:
                content.append("Remaining: ", style=self.theme.text_dim)
                content.append("indefinite", style=self.theme.text_dim)
            content.append("\n\n")

            # Progress bar (ASCII)
            progress = int(status.schedule_step_progress * 20)
            bar = "[" + "=" * progress + "-" * (20 - progress) + "]"
            content.append(bar, style="cyan")
        else:
            content.append("No schedule active\n", style=self.theme.text_dim)
            content.append("\n")
            content.append("Presets:\n", style=self.theme.text)
            content.append("1. Slow Cook\n", style=self.theme.text_dim)
            content.append("2. Quick Warm\n", style=self.theme.text_dim)
            content.append("3. All Day\n", style=self.theme.text_dim)

        return Group(
            Align.center(content),
            Text(""),
            Align.center(Text("[Press BACK to return]", style=self.theme.text_dim)),
        )

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
    # Main Render
    # =========================================================================

    def render(self) -> Panel:
        """
        Render the complete simulated display.

        Returns a Rich Panel representing the device screen.
        """
        # Select screen renderer
        screen_content: RenderableType
        screen_renderers = {
            Screen.MAIN: self._render_main_screen,
            Screen.SETTINGS: self._render_settings_screen,
            Screen.WIFI: self._render_wifi_screen,
            Screen.INFO: self._render_info_screen,
            Screen.SCHEDULE: self._render_schedule_screen,
        }

        renderer = screen_renderers.get(self.current_screen, self._render_main_screen)
        screen_content = renderer()

        # Add message overlay if present
        overlay = self._render_message_overlay()
        if overlay:
            screen_content = Group(screen_content, Text(""), overlay)

        # Wrap in panel to simulate display border
        # Calculate panel width based on display aspect ratio
        # TUI characters are roughly 2:1 aspect, so adjust
        panel_width = min(50, max(30, self.display.width // 8))

        return Panel(
            screen_content,
            title=f"[{self.display.name}]",
            subtitle=f"[dim]{self.current_screen.name}[/]",
            width=panel_width,
            height=16,  # Fixed height for consistency
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
