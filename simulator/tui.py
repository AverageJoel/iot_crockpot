"""
Text UI for the crockpot simulator using Rich library.
"""

import time
from collections import deque
from threading import Lock

from rich.console import Console, Group
from rich.layout import Layout
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from crockpot_sim import CrockpotSimulator, CrockpotState, CrockpotStatus


# Sparkline characters for temperature history
# Use ASCII-compatible characters for Windows console compatibility
# All characters are visible (no spaces)
SPARKLINE_CHARS = "._-=+*#@"


class CrockpotTUI:
    """Terminal UI for the crockpot simulator."""

    HISTORY_SIZE = 60  # 60 seconds of history

    def __init__(self, simulator: CrockpotSimulator):
        self.simulator = simulator
        self.console = Console()
        self.messages: deque[str] = deque(maxlen=10)
        self.temp_history: deque[float] = deque(maxlen=self.HISTORY_SIZE)
        self.running = False
        self._lock = Lock()
        self._config_version = 0

    def add_message(self, msg: str) -> None:
        """Add a message to the log."""
        with self._lock:
            timestamp = time.strftime("%H:%M:%S")
            self.messages.append(f"[dim]{timestamp}[/] {msg}")

    def record_temperature(self, temp: float) -> None:
        """Record temperature for history sparkline."""
        with self._lock:
            self.temp_history.append(temp)

    def notify_config_reload(self, version: int) -> None:
        """Notify that config was reloaded."""
        self._config_version = version
        self.add_message("[yellow]Config reloaded from headers[/]")

    def _make_sparkline(self) -> str:
        """Generate sparkline from temperature history."""
        if not self.temp_history:
            return ""

        temps = list(self.temp_history)
        # Normalize to 0-8 range (sparkline chars)
        min_t = 70.0   # Room temp
        max_t = 320.0  # Above safety temp

        def normalize(t: float) -> int:
            norm = (t - min_t) / (max_t - min_t)
            norm = max(0, min(1, norm))
            return int(norm * (len(SPARKLINE_CHARS) - 1))

        return "".join(SPARKLINE_CHARS[normalize(t)] for t in temps)

    def _format_uptime(self, seconds: int) -> str:
        """Format uptime as HH:MM:SS."""
        h = seconds // 3600
        m = (seconds % 3600) // 60
        s = seconds % 60
        return f"{h:02d}:{m:02d}:{s:02d}"

    def _state_color(self, state: CrockpotState) -> str:
        """Get color for state display."""
        colors = {
            CrockpotState.OFF: "dim white",
            CrockpotState.WARM: "yellow",
            CrockpotState.LOW: "orange1",
            CrockpotState.HIGH: "red",
        }
        return colors.get(state, "white")

    def _render_status(self, status: CrockpotStatus) -> Panel:
        """Render the status panel."""
        table = Table.grid(padding=(0, 2))
        table.add_column(style="bold", width=16)
        table.add_column()

        # State
        state_color = self._state_color(status.state)
        table.add_row(
            "State:",
            Text(f"[{status.state.name}]", style=state_color)
        )

        # Temperature
        temp_str = f"{status.temperature_f:.1f} F"
        temp_style = "red" if status.temperature_f >= self.simulator.safety_temp_f else "white"
        table.add_row("Temperature:", Text(temp_str, style=temp_style))

        # History sparkline
        sparkline = self._make_sparkline()
        table.add_row("History:", Text(sparkline, style="cyan"))

        # Relays (use ASCII-compatible indicators)
        main_icon = "[*]" if status.relay_main else "[ ]"
        main_text = "ON" if status.relay_main else "OFF"
        main_style = "green" if status.relay_main else "dim"
        table.add_row("Relay Main:", Text(f"{main_icon} {main_text}", style=main_style))

        aux_icon = "[*]" if status.relay_aux else "[ ]"
        aux_text = "ON" if status.relay_aux else "OFF"
        aux_style = "green" if status.relay_aux else "dim"
        table.add_row("Relay Aux:", Text(f"{aux_icon} {aux_text}", style=aux_style))

        # Uptime
        table.add_row("Uptime:", self._format_uptime(status.uptime_seconds))

        # WiFi
        wifi_text = "Connected" if status.wifi_connected else "Disconnected"
        wifi_style = "green" if status.wifi_connected else "red"
        table.add_row("WiFi:", Text(wifi_text, style=wifi_style))

        # Sensor
        sensor_text = "ERROR" if status.sensor_error else "OK"
        sensor_style = "red bold" if status.sensor_error else "green"
        table.add_row("Sensor:", Text(sensor_text, style=sensor_style))

        return Panel(
            table,
            title="[bold]IoT Crockpot Simulator[/]",
            border_style="blue",
        )

    def _render_commands(self) -> Panel:
        """Render the commands panel."""
        commands = Text()
        commands.append("Keys: ", style="bold")
        commands.append("[o]", style="bold dim white")
        commands.append("ff  ", style="dim white")
        commands.append("[w]", style="bold yellow")
        commands.append("arm  ", style="yellow")
        commands.append("[l]", style="bold orange1")
        commands.append("ow  ", style="orange1")
        commands.append("[h]", style="bold red")
        commands.append("igh  ", style="red")
        commands.append("[e]", style="bold magenta")
        commands.append("rror  ", style="magenta")
        commands.append("[q]", style="bold")
        commands.append("uit", style="dim")

        return Panel(commands, border_style="dim")

    def _render_messages(self) -> Panel:
        """Render the message log panel."""
        with self._lock:
            msgs = list(self.messages)

        if not msgs:
            content = Text("Ready for commands...", style="dim")
        else:
            content = Text("\n".join(msgs[-5:]))

        return Panel(content, title="Log", border_style="dim")

    def render(self) -> Group:
        """Render the complete TUI."""
        status = self.simulator.get_status()
        self.record_temperature(status.temperature_f)

        return Group(
            self._render_status(status),
            self._render_commands(),
            self._render_messages(),
        )

    def handle_command(self, cmd: str) -> bool:
        """
        Handle a user command.
        Returns False if should quit.
        """
        cmd = cmd.strip().lower()

        if cmd in ("/quit", "/q", "q"):
            return False

        if cmd in ("/off", "o"):
            self.simulator.set_state(CrockpotState.OFF)
            self.add_message("Set state to OFF")

        elif cmd in ("/warm", "w"):
            self.simulator.set_state(CrockpotState.WARM)
            self.add_message("Set state to WARM")

        elif cmd in ("/low", "l"):
            self.simulator.set_state(CrockpotState.LOW)
            self.add_message("Set state to LOW")

        elif cmd in ("/high", "h"):
            self.simulator.set_state(CrockpotState.HIGH)
            self.add_message("Set state to HIGH")

        elif cmd in ("/error", "e"):
            status = self.simulator.get_status()
            new_error = not status.sensor_error
            self.simulator.inject_sensor_error(new_error)
            state = "injected" if new_error else "cleared"
            self.add_message(f"Sensor error {state}")

        elif cmd in ("/status", "s"):
            status = self.simulator.get_status()
            self.add_message(
                f"State: {status.state.name}, "
                f"Temp: {status.temperature_f:.1f} F, "
                f"Relay: {'ON' if status.relay_main else 'OFF'}"
            )

        elif cmd:
            self.add_message(f"[red]Unknown command: {cmd}[/]")

        return True
