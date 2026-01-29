"""
Realistic temperature simulation for the crockpot simulator.
"""

import random
from enum import Enum


class State(Enum):
    OFF = 0
    WARM = 1
    LOW = 2
    HIGH = 3


# Target temperatures for each state (Fahrenheit)
TARGET_TEMPS: dict[State, float] = {
    State.OFF: 70.0,   # Room temperature
    State.WARM: 150.0,
    State.LOW: 200.0,
    State.HIGH: 300.0,
}

# Heating rate in degrees F per second (when relay is ON)
HEATING_RATE = 2.0

# Cooling coefficient (exponential decay towards ambient)
COOLING_COEFF = 0.02

# Noise amplitude (random variation)
NOISE_AMPLITUDE = 0.5

# Room/ambient temperature
ROOM_TEMP = 70.0


class TemperatureSimulator:
    """Simulates realistic crockpot temperature behavior."""

    def __init__(self, initial_temp: float = ROOM_TEMP):
        self.temperature = initial_temp
        self.sensor_error = False
        self._error_injected = False

    def update(self, state: State, relay_on: bool, dt: float = 1.0) -> float:
        """
        Update temperature based on current state and relay.

        Args:
            state: Current crockpot state
            relay_on: Whether main relay is currently on
            dt: Time step in seconds

        Returns:
            Current temperature in Fahrenheit
        """
        if self._error_injected:
            self.sensor_error = True
            return self.temperature

        self.sensor_error = False
        target = TARGET_TEMPS.get(state, ROOM_TEMP)

        if relay_on and self.temperature < target:
            # Heating: linear rise towards target
            self.temperature += HEATING_RATE * dt
            # Don't overshoot target
            self.temperature = min(self.temperature, target + 10)
        else:
            # Cooling: exponential decay towards room temp
            diff = self.temperature - ROOM_TEMP
            self.temperature -= diff * COOLING_COEFF * dt

        # Add noise
        self.temperature += random.uniform(-NOISE_AMPLITUDE, NOISE_AMPLITUDE)

        # Clamp to reasonable bounds
        self.temperature = max(ROOM_TEMP - 10, min(400, self.temperature))

        return self.temperature

    def inject_error(self, error: bool) -> None:
        """Inject or clear a sensor error condition."""
        self._error_injected = error

    def get_temperature(self) -> float:
        """Get current temperature."""
        return self.temperature

    def has_error(self) -> bool:
        """Check if sensor is in error state."""
        return self.sensor_error
