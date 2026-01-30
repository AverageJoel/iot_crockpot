#!/usr/bin/env python3
"""
IoT Crockpot Simulator - Textual App with clickable GUI.

Screens:
- Main: Temperature display + state buttons
- Menu: Screen selection
- Schedules: Start a cooking schedule
- History: Temperature graph over time
- Settings: Configuration options
"""

import threading
import time
from collections import deque
from enum import Enum, auto
from pathlib import Path

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical, Center
from textual.widgets import Button, Footer, Header, Static, ProgressBar, Label, ListView, ListItem
from textual.binding import Binding
from textual.reactive import reactive

from crockpot_sim import CrockpotSimulator, CrockpotState
from config_parser import ConfigParser
from schedule import PRESET_SCHEDULES, Schedule

SCRIPT_DIR = Path(__file__).parent
FIRMWARE_DIR = SCRIPT_DIR.parent / "firmware"

# Sparkline characters
SPARK_CHARS = "▁▂▃▄▅▆▇█"


class AppScreen(Enum):
    MAIN = auto()
    MENU = auto()
    SCHEDULES = auto()
    HISTORY = auto()
    SETTINGS = auto()


class CrockpotApp(App):
    """Crockpot simulator with multiple screens."""

    CSS = """
    Screen {
        align: center middle;
        background: #1a1a2e;
    }

    .screen-container {
        width: 60;
        height: 24;
        border: double white;
        background: #16213e;
        padding: 1 2;
    }

    .hidden {
        display: none;
    }

    /* Title */
    .screen-title {
        width: 100%;
        text-align: center;
        text-style: bold;
        color: cyan;
        margin-bottom: 1;
    }

    /* Temperature display */
    #temperature {
        width: 100%;
        height: 3;
        content-align: center middle;
        text-align: center;
        text-style: bold;
        color: white;
        background: #0f0f23;
        border: round #333;
    }

    /* State buttons row */
    #state-buttons {
        width: 100%;
        height: 3;
        align: center middle;
        margin: 1 0;
    }

    .state-btn {
        min-width: 8;
        height: 3;
        margin: 0 1;
    }

    #btn-off { background: #444; }
    #btn-off.selected { background: #888; text-style: bold reverse; }
    #btn-warm { background: #554400; color: yellow; }
    #btn-warm.selected { background: #aa8800; text-style: bold reverse; }
    #btn-low { background: #553300; color: orange; }
    #btn-low.selected { background: darkorange; text-style: bold reverse; }
    #btn-high { background: #550000; color: red; }
    #btn-high.selected { background: #cc0000; text-style: bold reverse; }

    /* Status bar */
    #status-bar {
        width: 100%;
        height: 1;
        margin-top: 1;
    }

    /* Schedule status */
    #schedule-info {
        width: 100%;
        text-align: center;
        color: cyan;
        margin-top: 1;
    }

    /* Menu button */
    #menu-btn {
        margin-top: 1;
        width: 100%;
    }

    /* Menu screen */
    .menu-item {
        width: 100%;
        height: 3;
        margin: 1 0;
        content-align: center middle;
    }

    .menu-item:hover {
        background: #2a2a4e;
    }

    /* Schedule list */
    .schedule-item {
        width: 100%;
        height: 2;
        margin: 0;
    }

    .schedule-item:hover {
        background: #2a2a4e;
    }

    /* History graph */
    #history-graph {
        width: 100%;
        height: 3;
        background: #0f0f23;
        border: round #333;
        content-align: center middle;
        text-align: center;
    }

    #history-stats {
        width: 100%;
        text-align: center;
        margin-top: 1;
    }

    /* Settings */
    .setting-row {
        width: 100%;
        height: 2;
    }

    /* Back button */
    .back-btn {
        margin-top: 1;
    }

    /* Menu button - bottom right */
    .menu-btn {
        dock: bottom;
        width: auto;
        margin-top: 1;
    }

    .nav-row {
        width: 100%;
        height: 3;
        align: right middle;
        margin-top: 1;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("m", "show_menu", "Menu"),
        Binding("escape", "go_back", "Back"),
        Binding("o", "set_off", "Off"),
        Binding("w", "set_warm", "Warm"),
        Binding("l", "set_low", "Low"),
        Binding("h", "set_high", "High"),
        Binding("s", "stop_schedule", "Stop"),
        Binding("x", "export_log", "Export"),
    ]

    current_screen: reactive[AppScreen] = reactive(AppScreen.MAIN)

    def __init__(self) -> None:
        super().__init__()

        config_parser = ConfigParser(FIRMWARE_DIR)
        config = config_parser.parse_all()

        self.simulator = CrockpotSimulator(
            safety_temp_f=config.get("CROCKPOT_SAFETY_TEMP_F", 300.0),
            control_interval_ms=config.get("CROCKPOT_CONTROL_INTERVAL_MS", 1000),
        )

        self._running = True
        self._control_thread = threading.Thread(target=self._control_loop, daemon=True)
        self._temp_history: deque[float] = deque(maxlen=40)

    def compose(self) -> ComposeResult:
        yield Header()

        # Main screen
        with Container(id="main-screen", classes="screen-container"):
            yield Static("Crockpot", classes="screen-title")
            yield Static("70°F", id="temperature")
            with Horizontal(id="state-buttons"):
                yield Button("OFF", id="btn-off", classes="state-btn selected")
                yield Button("WARM", id="btn-warm", classes="state-btn")
                yield Button("LOW", id="btn-low", classes="state-btn")
                yield Button("HIGH", id="btn-high", classes="state-btn")
            yield Static("", id="schedule-info")
            with Horizontal(id="status-bar"):
                yield Static("[green]WiFi[/]", id="wifi-status")
                yield Static("00:00:00", id="uptime")
            with Horizontal(classes="nav-row"):
                yield Button("Menu", id="menu-btn", classes="menu-btn")

        # Menu screen
        with Container(id="menu-screen", classes="screen-container hidden"):
            yield Static("Menu", classes="screen-title")
            yield Button("Main", id="menu-main", classes="menu-item")
            yield Button("Schedules", id="menu-schedules", classes="menu-item")
            yield Button("History", id="menu-history", classes="menu-item")
            yield Button("Settings", id="menu-settings", classes="menu-item")
            with Horizontal(classes="nav-row"):
                yield Button("Back", id="menu-back", classes="menu-btn")

        # Schedules screen
        with Container(id="schedules-screen", classes="screen-container hidden"):
            yield Static("Schedules", classes="screen-title")
            yield Button("Slow Cook (H3h>L6h>W)", id="sched-1", classes="schedule-item")
            yield Button("Quick Warm (H1h>W)", id="sched-2", classes="schedule-item")
            yield Button("All Day (L8h>W)", id="sched-3", classes="schedule-item")
            yield Button("Stop Schedule", id="sched-stop", classes="schedule-item")
            with Horizontal(classes="nav-row"):
                yield Button("Menu", id="menu-btn-sched", classes="menu-btn")

        # History screen
        with Container(id="history-screen", classes="screen-container hidden"):
            yield Static("History", classes="screen-title")
            yield Static("", id="history-graph")
            yield Static("", id="history-stats")
            with Horizontal(classes="nav-row"):
                yield Button("Menu", id="menu-btn-hist", classes="menu-btn")

        # Settings screen
        with Container(id="settings-screen", classes="screen-container hidden"):
            yield Static("Settings", classes="screen-title")
            yield Static("WiFi: Connected", id="setting-wifi", classes="setting-row")
            yield Static("Temp Unit: Fahrenheit", id="setting-temp", classes="setting-row")
            yield Static("Safety Limit: 300°F", id="setting-safety", classes="setting-row")
            with Horizontal(classes="nav-row"):
                yield Button("Menu", id="menu-btn-settings", classes="menu-btn")

        yield Footer()

    def on_mount(self) -> None:
        self._control_thread.start()
        self.set_interval(0.25, self._update_display)

    def _control_loop(self) -> None:
        while self._running:
            self.simulator.control_loop()
            time.sleep(1.0)

    def watch_current_screen(self, new_screen: AppScreen) -> None:
        """React to screen changes."""
        screen_map = {
            AppScreen.MAIN: "main-screen",
            AppScreen.MENU: "menu-screen",
            AppScreen.SCHEDULES: "schedules-screen",
            AppScreen.HISTORY: "history-screen",
            AppScreen.SETTINGS: "settings-screen",
        }

        for screen, container_id in screen_map.items():
            container = self.query_one(f"#{container_id}")
            if screen == new_screen:
                container.remove_class("hidden")
            else:
                container.add_class("hidden")

    def _update_display(self) -> None:
        status = self.simulator.get_status()

        # Record temperature history
        self._temp_history.append(status.temperature_f)

        # Update main screen elements
        temp_widget = self.query_one("#temperature", Static)
        temp_text = f"{status.temperature_f:.0f}°F"
        if status.sensor_error:
            temp_widget.update(f"[bold red]{temp_text} ERROR[/]")
        elif status.temperature_f >= 300:
            temp_widget.update(f"[bold red]{temp_text}[/]")
        else:
            temp_widget.update(f"[bold white]{temp_text}[/]")

        # Update state buttons
        state_to_btn = {
            CrockpotState.OFF: "#btn-off",
            CrockpotState.WARM: "#btn-warm",
            CrockpotState.LOW: "#btn-low",
            CrockpotState.HIGH: "#btn-high",
        }
        for state, btn_id in state_to_btn.items():
            btn = self.query_one(btn_id, Button)
            if state == status.state:
                btn.add_class("selected")
            else:
                btn.remove_class("selected")

        # Update schedule info
        schedule_info = self.query_one("#schedule-info", Static)
        if status.schedule_active:
            step = status.schedule_step + 1
            total = status.schedule_total_steps
            remaining = status.schedule_step_remaining
            if remaining > 0:
                mins = remaining // 60
                schedule_info.update(f"[cyan]{status.schedule_name}[/] {step}/{total} ({mins}m)")
            else:
                schedule_info.update(f"[cyan]{status.schedule_name}[/] {step}/{total}")
        else:
            schedule_info.update("[dim]No schedule[/]")

        # Update status bar
        uptime = self.query_one("#uptime", Static)
        h = status.uptime_seconds // 3600
        m = (status.uptime_seconds % 3600) // 60
        s = status.uptime_seconds % 60
        uptime.update(f"{h:02d}:{m:02d}:{s:02d}")

        # Update history screen if visible
        if self.current_screen == AppScreen.HISTORY:
            self._update_history()

    def _update_history(self) -> None:
        """Update history graph."""
        if not self._temp_history:
            return

        temps = list(self._temp_history)
        min_t = min(temps)
        max_t = max(temps)
        current = temps[-1]

        # Build sparkline
        range_t = max(max_t - min_t, 10)
        sparkline = ""
        for t in temps:
            norm = (t - min_t) / range_t
            norm = max(0, min(0.99, norm))
            idx = int(norm * len(SPARK_CHARS))
            sparkline += SPARK_CHARS[idx]

        graph = self.query_one("#history-graph", Static)
        graph.update(f"[cyan]{sparkline}[/]")

        stats = self.query_one("#history-stats", Static)
        stats.update(f"Now: {current:.0f}°F  Min: {min_t:.0f}°F  Max: {max_t:.0f}°F")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        button_id = event.button.id

        # Main screen
        if button_id == "btn-off":
            self.simulator.stop_schedule()
            self.simulator.set_state(CrockpotState.OFF)
        elif button_id == "btn-warm":
            self.simulator.stop_schedule()
            self.simulator.set_state(CrockpotState.WARM)
        elif button_id == "btn-low":
            self.simulator.stop_schedule()
            self.simulator.set_state(CrockpotState.LOW)
        elif button_id == "btn-high":
            self.simulator.stop_schedule()
            self.simulator.set_state(CrockpotState.HIGH)
        elif button_id in ("menu-btn", "menu-btn-sched", "menu-btn-hist", "menu-btn-settings"):
            self.current_screen = AppScreen.MENU

        # Menu screen
        elif button_id in ("menu-main", "menu-back"):
            self.current_screen = AppScreen.MAIN
        elif button_id == "menu-schedules":
            self.current_screen = AppScreen.SCHEDULES
        elif button_id == "menu-history":
            self.current_screen = AppScreen.HISTORY
        elif button_id == "menu-settings":
            self.current_screen = AppScreen.SETTINGS

        # Schedules screen
        elif button_id == "sched-1":
            self._start_schedule(0)
            self.current_screen = AppScreen.MAIN
        elif button_id == "sched-2":
            self._start_schedule(1)
            self.current_screen = AppScreen.MAIN
        elif button_id == "sched-3":
            self._start_schedule(2)
            self.current_screen = AppScreen.MAIN
        elif button_id == "sched-stop":
            self.simulator.stop_schedule()


    def _start_schedule(self, index: int) -> None:
        if index < len(PRESET_SCHEDULES):
            self.simulator.start_schedule(PRESET_SCHEDULES[index])

    def action_show_menu(self) -> None:
        self.current_screen = AppScreen.MENU

    def action_go_back(self) -> None:
        if self.current_screen != AppScreen.MAIN:
            self.current_screen = AppScreen.MAIN

    def action_set_off(self) -> None:
        self.simulator.stop_schedule()
        self.simulator.set_state(CrockpotState.OFF)

    def action_set_warm(self) -> None:
        self.simulator.stop_schedule()
        self.simulator.set_state(CrockpotState.WARM)

    def action_set_low(self) -> None:
        self.simulator.stop_schedule()
        self.simulator.set_state(CrockpotState.LOW)

    def action_set_high(self) -> None:
        self.simulator.stop_schedule()
        self.simulator.set_state(CrockpotState.HIGH)

    def action_toggle_error(self) -> None:
        status = self.simulator.get_status()
        self.simulator.inject_sensor_error(not status.sensor_error)

    def action_stop_schedule(self) -> None:
        self.simulator.stop_schedule()

    def action_export_log(self) -> None:
        if self.simulator.datalog:
            filename = self.simulator.datalog.generate_filename("csv")
            export_path = Path.home() / ".crockpot" / filename
            self.simulator.datalog.to_csv(export_path)
            self.notify(f"Exported to {export_path}")

    def on_unmount(self) -> None:
        self._running = False


def main():
    app = CrockpotApp()
    app.run()


if __name__ == "__main__":
    main()
