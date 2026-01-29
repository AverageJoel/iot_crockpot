#!/usr/bin/env python3
"""
IoT Crockpot Simulator - Main entry point.

A Python TUI simulator that mimics the ESP32-C3 crockpot firmware behavior.
Watches firmware source files and updates when constants change.

Usage:
    python main.py

Controls:
    o - Turn off
    w - Set to WARM
    l - Set to LOW
    h - Set to HIGH
    e - Toggle sensor error
    s - Show status
    q - Quit
"""

import sys
import threading
import time
from pathlib import Path

# Platform-specific keyboard input
if sys.platform == "win32":
    import msvcrt

    def get_key() -> str | None:
        """Get a keypress without blocking (Windows)."""
        if msvcrt.kbhit():
            ch = msvcrt.getch()
            # Handle special keys (arrows, etc.) - ignore them
            if ch in (b'\x00', b'\xe0'):
                msvcrt.getch()  # consume second byte
                return None
            return ch.decode('utf-8', errors='ignore').lower()
        return None
else:
    import select
    import tty
    import termios

    _old_settings = None

    def _setup_terminal():
        global _old_settings
        fd = sys.stdin.fileno()
        _old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)

    def _restore_terminal():
        global _old_settings
        if _old_settings:
            fd = sys.stdin.fileno()
            termios.tcsetattr(fd, termios.TCSADRAIN, _old_settings)

    def get_key() -> str | None:
        """Get a keypress without blocking (Unix/Mac)."""
        if select.select([sys.stdin], [], [], 0)[0]:
            ch = sys.stdin.read(1)
            return ch.lower()
        return None

from rich.console import Console
from rich.live import Live

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler, FileModifiedEvent
    WATCHDOG_AVAILABLE = True
except ImportError:
    WATCHDOG_AVAILABLE = False

from config_parser import ConfigParser
from crockpot_sim import CrockpotSimulator, CrockpotState
from tui import CrockpotTUI


# Find firmware directory relative to this script
SCRIPT_DIR = Path(__file__).parent
FIRMWARE_DIR = SCRIPT_DIR.parent / "firmware"


class ConfigFileHandler(FileSystemEventHandler):
    """Handles file system events for header files."""

    def __init__(self, callback):
        self.callback = callback
        self._last_reload = 0

    def on_modified(self, event):
        if isinstance(event, FileModifiedEvent):
            # Debounce - ignore events within 1 second
            now = time.time()
            if now - self._last_reload < 1.0:
                return
            self._last_reload = now

            path = Path(event.src_path)
            if path.suffix == ".h":
                self.callback(path)


class SimulatorApp:
    """Main application coordinating simulator, TUI, and file watcher."""

    def __init__(self):
        self.console = Console()
        self.running = False
        self._config_version = 0

        # Parse initial config
        self.config_parser = ConfigParser(FIRMWARE_DIR)
        config = self.config_parser.parse_all()

        # Create simulator
        self.simulator = CrockpotSimulator(
            safety_temp_f=config.get("CROCKPOT_SAFETY_TEMP_F", 300.0),
            control_interval_ms=config.get("CROCKPOT_CONTROL_INTERVAL_MS", 1000),
            on_state_change=self._on_state_change,
            on_safety_shutoff=self._on_safety_shutoff,
        )

        # Create TUI
        self.tui = CrockpotTUI(self.simulator)

        # File watcher
        self.observer = None

    def _on_state_change(self, state: CrockpotState) -> None:
        """Callback when state changes."""
        self.tui.add_message(f"State changed to {state.name}")

    def _on_safety_shutoff(self, reason: str) -> None:
        """Callback when safety shutoff triggers."""
        self.tui.add_message(f"[red bold]SAFETY SHUTOFF: {reason}[/]")

    def _on_config_reload(self, path: Path) -> None:
        """Callback when config file changes."""
        config = self.config_parser.parse_all()
        self.simulator.update_config(
            safety_temp_f=config.get("CROCKPOT_SAFETY_TEMP_F", 300.0),
            control_interval_ms=config.get("CROCKPOT_CONTROL_INTERVAL_MS", 1000),
        )
        self._config_version += 1
        self.tui.notify_config_reload(self._config_version)

    def _setup_file_watcher(self) -> None:
        """Set up file system watcher for header files."""
        if not WATCHDOG_AVAILABLE:
            self.tui.add_message("[yellow]watchdog not installed - file watching disabled[/]")
            return

        if not FIRMWARE_DIR.exists():
            self.tui.add_message(f"[yellow]Firmware dir not found: {FIRMWARE_DIR}[/]")
            return

        handler = ConfigFileHandler(self._on_config_reload)
        self.observer = Observer()

        # Watch the main directory
        watch_dir = FIRMWARE_DIR / "main"
        if watch_dir.exists():
            self.observer.schedule(handler, str(watch_dir), recursive=False)
            self.tui.add_message(f"Watching {watch_dir} for changes")

        self.observer.start()

    def _control_loop_thread(self) -> None:
        """Background thread running the control loop."""
        while self.running:
            self.simulator.control_loop()
            time.sleep(1.0)

    def _handle_key(self, key: str) -> bool:
        """Handle a keypress. Returns False to quit."""
        if key == 'q':
            return False
        elif key == 'o':
            self.simulator.set_state(CrockpotState.OFF)
            self.tui.add_message("Set state to OFF")
        elif key == 'w':
            self.simulator.set_state(CrockpotState.WARM)
            self.tui.add_message("Set state to WARM")
        elif key == 'l':
            self.simulator.set_state(CrockpotState.LOW)
            self.tui.add_message("Set state to LOW")
        elif key == 'h':
            self.simulator.set_state(CrockpotState.HIGH)
            self.tui.add_message("Set state to HIGH")
        elif key == 'e':
            status = self.simulator.get_status()
            new_error = not status.sensor_error
            self.simulator.inject_sensor_error(new_error)
            state = "injected" if new_error else "cleared"
            self.tui.add_message(f"Sensor error {state}")
        elif key == 's':
            status = self.simulator.get_status()
            self.tui.add_message(
                f"State: {status.state.name}, "
                f"Temp: {status.temperature_f:.1f} F, "
                f"Relay: {'ON' if status.relay_main else 'OFF'}"
            )
        return True

    def run(self) -> None:
        """Run the simulator application."""
        self.running = True

        # Set up terminal for non-blocking input (Unix only)
        if sys.platform != "win32":
            _setup_terminal()

        # Set up file watcher
        self._setup_file_watcher()

        # Start control loop thread
        control_thread = threading.Thread(target=self._control_loop_thread, daemon=True)
        control_thread.start()

        try:
            with Live(self.tui.render(), refresh_per_second=4, console=self.console) as live:
                self.tui.add_message("Press: [bold]o[/]=OFF [bold]w[/]=WARM [bold]l[/]=LOW [bold]h[/]=HIGH [bold]e[/]=error [bold]q[/]=quit")

                while self.running:
                    # Check for keypress
                    key = get_key()
                    if key:
                        if not self._handle_key(key):
                            break

                    # Update display
                    live.update(self.tui.render())
                    time.sleep(0.1)

        except KeyboardInterrupt:
            pass
        finally:
            self.running = False

            # Restore terminal (Unix only)
            if sys.platform != "win32":
                _restore_terminal()

            if self.observer:
                self.observer.stop()
                self.observer.join(timeout=1)

        self.console.print("\n[dim]Simulator stopped.[/]")


def main():
    """Entry point."""
    # Check for Rich library
    try:
        import rich
    except ImportError:
        print("Error: 'rich' library required. Install with:")
        print("  pip install rich")
        sys.exit(1)

    app = SimulatorApp()
    app.run()


if __name__ == "__main__":
    main()
