"""
Core crockpot state machine simulator.
Mirrors the logic in firmware/main/crockpot.c
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Callable

from temperature_sim import TemperatureSimulator, State


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

    @property
    def state(self) -> CrockpotState:
        return self._state

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
        return CrockpotStatus(
            state=self._state,
            temperature_f=self._temp_sim.get_temperature(),
            uptime_seconds=self._uptime,
            wifi_connected=self._wifi_connected,
            sensor_error=self._temp_sim.has_error(),
            relay_main=self._relay_main,
            relay_aux=self._relay_aux,
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

    def _apply_relay_state(self) -> None:
        """Set relay states based on current crockpot state.

        Mirrors relay_apply_state() from relay.c:
        - OFF: MAIN off
        - WARM/LOW/HIGH: MAIN on (all heating states are identical)
        - AUX is not used (reserved for future PWM/multi-relay)
        """
        if self._state == CrockpotState.OFF:
            self._relay_main = False
        else:
            # WARM, LOW, HIGH all turn main relay on
            self._relay_main = True
        # AUX is never used in current firmware
        self._relay_aux = False

    def _safety_shutoff(self, reason: str) -> None:
        """Emergency shutoff - turn everything off."""
        self._relay_main = False
        self._relay_aux = False
        self._state = CrockpotState.OFF

        if self.on_safety_shutoff:
            self.on_safety_shutoff(reason)

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
