"""
Core crockpot state machine simulator.
Mirrors the logic in firmware/main/crockpot.c
"""

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Callable, TYPE_CHECKING

from temperature_sim import TemperatureSimulator, State

if TYPE_CHECKING:
    from schedule import ScheduleManager, Schedule, ScheduleStep
    from datalog import DataLog


class CrockpotState(Enum):
    OFF = 0
    WARM = 1
    LOW = 2
    HIGH = 3


@dataclass
class CrockpotStatus:
    state: CrockpotState = CrockpotState.OFF
    temperature_f: float = 70.0
    uptime_seconds: int = 0
    wifi_connected: bool = True
    sensor_error: bool = False
    relay_main: bool = False
    relay_aux: bool = False
    # Schedule fields
    schedule_active: bool = False
    schedule_name: str = ""
    schedule_step: int = 0
    schedule_total_steps: int = 0
    schedule_step_remaining: int = 0
    schedule_step_progress: float = 0.0


class CrockpotSimulator:
    """Simulates the crockpot firmware state machine."""

    # Maximum consecutive sensor errors before safety shutoff
    MAX_SENSOR_ERRORS = 10

    def __init__(
        self,
        safety_temp_f: float = 300.0,
        control_interval_ms: int = 1000,
        on_state_change: Callable[[CrockpotState], None] | None = None,
        on_safety_shutoff: Callable[[str], None] | None = None,
        enable_schedule: bool = True,
        enable_datalog: bool = True,
    ):
        self.safety_temp_f = safety_temp_f
        self.control_interval_ms = control_interval_ms
        self.on_state_change = on_state_change
        self.on_safety_shutoff = on_safety_shutoff

        self._state = CrockpotState.OFF
        self._uptime = 0
        self._wifi_connected = True
        self._consecutive_errors = 0

        self._relay_main = False
        self._relay_aux = False

        self._temp_sim = TemperatureSimulator()

        # Schedule manager
        self._schedule_manager: "ScheduleManager | None" = None
        if enable_schedule:
            from schedule import ScheduleManager
            self._schedule_manager = ScheduleManager(
                on_state_change=self._schedule_state_change,
                on_schedule_complete=self._on_schedule_complete,
                on_step_change=self._on_step_change,
            )

        # Data logger
        self._datalog: "DataLog | None" = None
        if enable_datalog:
            from datalog import DataLog
            self._datalog = DataLog()

    @property
    def state(self) -> CrockpotState:
        return self._state

    @property
    def schedule_manager(self) -> "ScheduleManager | None":
        """Get the schedule manager instance."""
        return self._schedule_manager

    @property
    def datalog(self) -> "DataLog | None":
        """Get the data logger instance."""
        return self._datalog

    def set_state(self, state: CrockpotState) -> bool:
        """Change crockpot state and update relays."""
        old_state = self._state
        self._state = state
        self._apply_relay_state()

        if old_state != state and self.on_state_change:
            self.on_state_change(state)

        return True

    def get_status(self) -> CrockpotStatus:
        """Get complete status snapshot."""
        # Get schedule info
        schedule_active = False
        schedule_name = ""
        schedule_step = 0
        schedule_total_steps = 0
        schedule_step_remaining = 0
        schedule_step_progress = 0.0

        if self._schedule_manager and self._schedule_manager.is_active:
            schedule_active = True
            if self._schedule_manager.active_schedule:
                schedule_name = self._schedule_manager.active_schedule.name
            schedule_step = self._schedule_manager.current_step_index
            schedule_total_steps = self._schedule_manager.total_steps
            schedule_step_remaining = self._schedule_manager.step_remaining_seconds
            schedule_step_progress = self._schedule_manager.get_step_progress()

        return CrockpotStatus(
            state=self._state,
            temperature_f=self._temp_sim.get_temperature(),
            uptime_seconds=self._uptime,
            wifi_connected=self._wifi_connected,
            sensor_error=self._temp_sim.has_error(),
            relay_main=self._relay_main,
            relay_aux=self._relay_aux,
            schedule_active=schedule_active,
            schedule_name=schedule_name,
            schedule_step=schedule_step,
            schedule_total_steps=schedule_total_steps,
            schedule_step_remaining=schedule_step_remaining,
            schedule_step_progress=schedule_step_progress,
        )

    def control_loop(self) -> None:
        """
        Main control loop - call once per second.
        Mirrors crockpot_control_task() from crockpot.c:144-196.
        """
        # Update temperature simulation (equivalent to temperature_read())
        temp_state = State(self._state.value)
        temp = self._temp_sim.update(temp_state, self._relay_main, dt=1.0)
        sensor_error = self._temp_sim.has_error()

        # Update uptime (crockpot.c:164-165)
        self._uptime += 1

        # Safety check: high temperature (crockpot.c:171-176)
        # Only check if reading is valid (no sensor error)
        if not sensor_error and temp > self.safety_temp_f:
            if self._state != CrockpotState.OFF:
                self._safety_shutoff(f"Temperature {temp:.1f}F exceeds limit")

        # Safety check: persistent sensor error while heating (crockpot.c:179-188)
        # Counter only increments when error AND heating, only resets after shutoff
        # Note: Counter does NOT reset on successful read (matches firmware behavior)
        if sensor_error and self._state != CrockpotState.OFF:
            self._consecutive_errors += 1
            if self._consecutive_errors > self.MAX_SENSOR_ERRORS:
                self._safety_shutoff("Persistent sensor error")
                self._consecutive_errors = 0

        # Tick schedule manager
        if self._schedule_manager:
            self._schedule_manager.tick()

        # Tick data logger
        if self._datalog:
            # Update schedule info in datalog
            if self._schedule_manager and self._schedule_manager.is_active:
                self._datalog.set_schedule_info(
                    active=True,
                    name=self._schedule_manager.active_schedule.name if self._schedule_manager.active_schedule else "",
                    step=self._schedule_manager.current_step_index,
                )
            else:
                self._datalog.set_schedule_info(active=False)

            self._datalog.tick(self.get_status())

    def _apply_relay_state(self) -> None:
        """Set relay states based on current crockpot state.

        Relay mapping:
        - OFF:  Both relays off
        - WARM: Relay 2 (AUX) only - low heat
        - LOW:  Relay 1 (MAIN) only - medium heat
        - HIGH: Both relays on - maximum heat
        """
        if self._state == CrockpotState.OFF:
            self._relay_main = False
            self._relay_aux = False
        elif self._state == CrockpotState.WARM:
            self._relay_main = False
            self._relay_aux = True
        elif self._state == CrockpotState.LOW:
            self._relay_main = True
            self._relay_aux = False
        elif self._state == CrockpotState.HIGH:
            self._relay_main = True
            self._relay_aux = True

    def _safety_shutoff(self, reason: str) -> None:
        """Emergency shutoff - turn everything off."""
        self._relay_main = False
        self._relay_aux = False
        self._state = CrockpotState.OFF

        # Stop any active schedule
        if self._schedule_manager and self._schedule_manager.is_active:
            self._schedule_manager.stop()

        if self.on_safety_shutoff:
            self.on_safety_shutoff(reason)

    def _schedule_state_change(self, state: CrockpotState) -> None:
        """Callback for schedule-driven state changes."""
        old_state = self._state
        self._state = state
        self._apply_relay_state()

        if old_state != state and self.on_state_change:
            self.on_state_change(state)

    def _on_schedule_complete(self, name: str) -> None:
        """Callback when a schedule completes."""
        # Schedule completed - device stays in last state
        pass

    def _on_step_change(self, step_index: int, step: "ScheduleStep") -> None:
        """Callback when schedule advances to a new step."""
        pass

    # Schedule control methods
    def start_schedule(self, schedule: "Schedule") -> bool:
        """Start a cooking schedule."""
        if not self._schedule_manager:
            return False
        self._schedule_manager.start(schedule)
        return True

    def stop_schedule(self) -> None:
        """Stop the current schedule."""
        if self._schedule_manager:
            self._schedule_manager.stop()

    def inject_sensor_error(self, error: bool) -> None:
        """Inject or clear sensor error for testing."""
        self._temp_sim.inject_error(error)

    def update_config(self, safety_temp_f: float, control_interval_ms: int) -> None:
        """Update configuration from parsed header values."""
        self.safety_temp_f = safety_temp_f
        self.control_interval_ms = control_interval_ms

    def state_from_string(self, s: str) -> CrockpotState | None:
        """Parse state from string (case-insensitive)."""
        s = s.upper().strip()
        try:
            return CrockpotState[s]
        except KeyError:
            return None

    def state_to_string(self, state: CrockpotState) -> str:
        """Convert state to string."""
        return state.name
