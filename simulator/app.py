#!/usr/bin/env python3
"""
IoT Crockpot Simulator - Textual App with clickable GUI.

Relay mapping:
- OFF:  Both relays off
- WARM: Relay 2 only
- LOW:  Relay 1 only
- HIGH: Both relays on
"""

import threading
import time
from pathlib import Path

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import Button, Footer, Header, Static, ProgressBar, Label
from textual.binding import Binding

from crockpot_sim import CrockpotSimulator, CrockpotState
from config_parser import ConfigParser
from schedule import PRESET_SCHEDULES, Schedule

SCRIPT_DIR = Path(__file__).parent
FIRMWARE_DIR = SCRIPT_DIR.parent / "firmware"


class CrockpotApp(App):
    """Crockpot simulator with clickable buttons."""

    CSS = """
    Screen {
        align: center middle;
        background: #1a1a2e;
    }

    #main-container {
        width: 70;
        height: 32;
        border: double white;
        background: #16213e;
        padding: 1 2;
        layout: vertical;
    }

    #title {
        width: 100%;
        text-align: center;
        text-style: bold;
        color: cyan;
        margin-bottom: 1;
    }

    #temperature {
        width: 100%;
        height: 3;
        content-align: center middle;
        text-align: center;
        text-style: bold;
        color: white;
        background: #0f0f23;
        border: round #333;
        margin-bottom: 1;
    }

    #relay-row {
        width: 100%;
        height: 3;
        align: center middle;
        margin-bottom: 1;
    }

    .relay-box {
        width: 20;
        height: 3;
        border: solid #444;
        content-align: center middle;
        text-align: center;
        margin: 0 2;
    }

    .relay-on {
        background: #2d5a27;
        border: solid green;
        color: white;
    }

    .relay-off {
        background: #333;
        border: solid #555;
        color: #666;
    }

    #button-row {
        width: 100%;
        height: 5;
        align: center middle;
        margin: 1 0;
    }

    Button {
        margin: 0 1;
        min-width: 12;
        height: 3;
    }

    #btn-off {
        background: #555;
        border: tall #777;
    }
    #btn-off:hover {
        background: #666;
    }
    #btn-off:focus {
        text-style: bold;
    }
    #btn-off.selected {
        background: #888;
        border: tall white;
        text-style: bold reverse;
    }

    #btn-warm {
        background: #554400;
        color: yellow;
        border: tall #776600;
    }
    #btn-warm:hover {
        background: #665500;
    }
    #btn-warm:focus {
        text-style: bold;
    }
    #btn-warm.selected {
        background: #aa8800;
        border: tall yellow;
        text-style: bold reverse;
        color: black;
    }

    #btn-low {
        background: #553300;
        color: orange;
        border: tall #774400;
    }
    #btn-low:hover {
        background: #664400;
    }
    #btn-low:focus {
        text-style: bold;
    }
    #btn-low.selected {
        background: darkorange;
        border: tall orange;
        text-style: bold reverse;
        color: white;
    }

    #btn-high {
        background: #550000;
        color: red;
        border: tall #770000;
    }
    #btn-high:hover {
        background: #660000;
    }
    #btn-high:focus {
        text-style: bold;
    }
    #btn-high.selected {
        background: #cc0000;
        border: tall red;
        text-style: bold reverse;
        color: white;
    }

    #status-bar {
        dock: bottom;
        width: 100%;
        height: 1;
        margin-top: 1;
    }

    #wifi-status {
        width: 50%;
    }

    #uptime {
        width: 50%;
        text-align: right;
    }

    #schedule-container {
        width: 100%;
        height: auto;
        margin-top: 1;
        padding: 0 1;
    }

    #schedule-status {
        width: 100%;
        height: 1;
        text-align: center;
        color: cyan;
    }

    #schedule-progress {
        width: 100%;
        height: 1;
        margin-top: 0;
    }

    #schedule-buttons {
        width: 100%;
        height: 3;
        align: center middle;
        margin-top: 1;
    }

    .schedule-btn {
        min-width: 14;
        height: 3;
        margin: 0 1;
    }

    #btn-schedule-stop {
        background: #553333;
        color: #ff6666;
        border: tall #774444;
    }
    #btn-schedule-stop:hover {
        background: #664444;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("e", "toggle_error", "Error"),
        Binding("o", "set_off", "Off"),
        Binding("w", "set_warm", "Warm"),
        Binding("l", "set_low", "Low"),
        Binding("h", "set_high", "High"),
        Binding("1", "schedule_1", "Slow Cook"),
        Binding("2", "schedule_2", "Quick Warm"),
        Binding("3", "schedule_3", "All Day"),
        Binding("s", "stop_schedule", "Stop Schedule"),
        Binding("x", "export_log", "Export Log"),
    ]

    def __init__(self) -> None:
        super().__init__()

        # Parse config
        config_parser = ConfigParser(FIRMWARE_DIR)
        config = config_parser.parse_all()

        # Create simulator
        self.simulator = CrockpotSimulator(
            safety_temp_f=config.get("CROCKPOT_SAFETY_TEMP_F", 300.0),
            control_interval_ms=config.get("CROCKPOT_CONTROL_INTERVAL_MS", 1000),
        )

        self._running = True
        self._control_thread = threading.Thread(target=self._control_loop, daemon=True)

    def compose(self) -> ComposeResult:
        yield Header()

        with Container(id="main-container"):
            yield Static("IoT Crockpot Controller", id="title")
            yield Static("70.0°F", id="temperature")

            with Horizontal(id="relay-row"):
                yield Static("RELAY 1\nOFF", id="relay1", classes="relay-box relay-off")
                yield Static("RELAY 2\nOFF", id="relay2", classes="relay-box relay-off")

            with Horizontal(id="button-row"):
                yield Button("OFF", id="btn-off", classes="selected")
                yield Button("WARM", id="btn-warm")
                yield Button("LOW", id="btn-low")
                yield Button("HIGH", id="btn-high")

            with Horizontal(id="status-bar"):
                yield Static("[green]WiFi[/]", id="wifi-status")
                yield Static("00:00:00", id="uptime")

            # Schedule section
            with Container(id="schedule-container"):
                yield Static("No schedule active", id="schedule-status")
                yield ProgressBar(id="schedule-progress", total=100, show_eta=False)
                with Horizontal(id="schedule-buttons"):
                    yield Button("Slow Cook", id="btn-schedule-1", classes="schedule-btn")
                    yield Button("Quick Warm", id="btn-schedule-2", classes="schedule-btn")
                    yield Button("All Day", id="btn-schedule-3", classes="schedule-btn")
                    yield Button("Stop", id="btn-schedule-stop", classes="schedule-btn")

        yield Footer()

    def on_mount(self) -> None:
        self._control_thread.start()
        self.set_interval(0.25, self._update_display)

    def _control_loop(self) -> None:
        while self._running:
            self.simulator.control_loop()
            time.sleep(1.0)

    def _update_display(self) -> None:
        status = self.simulator.get_status()

        # Update temperature
        temp_widget = self.query_one("#temperature", Static)
        temp_text = f"{status.temperature_f:.1f}°F"
        if status.sensor_error:
            temp_widget.update(f"[bold red]{temp_text}[/]\n[bold red blink]SENSOR ERROR[/]")
        elif status.temperature_f >= 300:
            temp_widget.update(f"[bold red]{temp_text}[/]")
        else:
            temp_widget.update(f"[bold white]{temp_text}[/]")

        # Update relay indicators
        relay1 = self.query_one("#relay1", Static)
        relay2 = self.query_one("#relay2", Static)

        if status.relay_main:
            relay1.update("[bold]RELAY 1[/]\n[green bold]● ON[/]")
            relay1.remove_class("relay-off")
            relay1.add_class("relay-on")
        else:
            relay1.update("RELAY 1\n[dim]○ OFF[/]")
            relay1.remove_class("relay-on")
            relay1.add_class("relay-off")

        if status.relay_aux:
            relay2.update("[bold]RELAY 2[/]\n[green bold]● ON[/]")
            relay2.remove_class("relay-off")
            relay2.add_class("relay-on")
        else:
            relay2.update("RELAY 2\n[dim]○ OFF[/]")
            relay2.remove_class("relay-on")
            relay2.add_class("relay-off")

        # Update button selection
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

        # Update status bar
        wifi = self.query_one("#wifi-status", Static)
        wifi.update("[green]WiFi ●[/]" if status.wifi_connected else "[dim]WiFi ○[/]")

        uptime_widget = self.query_one("#uptime", Static)
        h = status.uptime_seconds // 3600
        m = (status.uptime_seconds % 3600) // 60
        s = status.uptime_seconds % 60
        uptime_widget.update(f"[dim]{h:02d}:{m:02d}:{s:02d}[/]")

        # Update schedule display
        schedule_status = self.query_one("#schedule-status", Static)
        schedule_progress = self.query_one("#schedule-progress", ProgressBar)

        if status.schedule_active:
            step_num = status.schedule_step + 1
            total_steps = status.schedule_total_steps
            remaining_mins = status.schedule_step_remaining // 60
            remaining_secs = status.schedule_step_remaining % 60
            current_state = status.state.name

            if status.schedule_step_remaining > 0:
                schedule_status.update(
                    f"[cyan]{status.schedule_name}[/] - Step {step_num}/{total_steps}: "
                    f"[bold]{current_state}[/] ({remaining_mins}:{remaining_secs:02d} left)"
                )
                schedule_progress.update(progress=status.schedule_step_progress * 100)
            else:
                schedule_status.update(
                    f"[cyan]{status.schedule_name}[/] - Step {step_num}/{total_steps}: "
                    f"[bold]{current_state}[/] (indefinite)"
                )
                schedule_progress.update(progress=100)
        else:
            schedule_status.update("[dim]No schedule active - Press 1/2/3 to start[/]")
            schedule_progress.update(progress=0)

    def on_button_pressed(self, event: Button.Pressed) -> None:
        """Handle button clicks."""
        button_id = event.button.id

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
        elif button_id == "btn-schedule-1":
            self._start_schedule(0)
        elif button_id == "btn-schedule-2":
            self._start_schedule(1)
        elif button_id == "btn-schedule-3":
            self._start_schedule(2)
        elif button_id == "btn-schedule-stop":
            self.simulator.stop_schedule()

        self._update_display()

    def _start_schedule(self, index: int) -> None:
        """Start a preset schedule by index."""
        if index < len(PRESET_SCHEDULES):
            schedule = PRESET_SCHEDULES[index]
            self.simulator.start_schedule(schedule)

    def action_set_off(self) -> None:
        self.simulator.set_state(CrockpotState.OFF)

    def action_set_warm(self) -> None:
        self.simulator.set_state(CrockpotState.WARM)

    def action_set_low(self) -> None:
        self.simulator.set_state(CrockpotState.LOW)

    def action_set_high(self) -> None:
        self.simulator.set_state(CrockpotState.HIGH)

    def action_toggle_error(self) -> None:
        status = self.simulator.get_status()
        self.simulator.inject_sensor_error(not status.sensor_error)

    def action_schedule_1(self) -> None:
        self._start_schedule(0)

    def action_schedule_2(self) -> None:
        self._start_schedule(1)

    def action_schedule_3(self) -> None:
        self._start_schedule(2)

    def action_stop_schedule(self) -> None:
        self.simulator.stop_schedule()

    def action_export_log(self) -> None:
        """Export the data log to CSV."""
        if self.simulator.datalog:
            filename = self.simulator.datalog.generate_filename("csv")
            export_path = Path.home() / ".crockpot" / filename
            self.simulator.datalog.to_csv(export_path)
            self.notify(f"Log exported to {export_path}")

    def on_unmount(self) -> None:
        self._running = False


def main():
    app = CrockpotApp()
    app.run()


if __name__ == "__main__":
    main()
