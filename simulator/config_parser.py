"""
Parse C header files to extract #define constants for the simulator.
"""

import re
from pathlib import Path
from typing import Any


class ConfigParser:
    """Parses C header files to extract constants."""

    def __init__(self, firmware_path: Path):
        self.firmware_path = firmware_path
        self.constants: dict[str, Any] = {}
        self._load_defaults()

    def _load_defaults(self) -> None:
        """Set default values in case parsing fails."""
        self.constants = {
            "CROCKPOT_SAFETY_TEMP_F": 300.0,
            "CROCKPOT_CONTROL_INTERVAL_MS": 1000,
            "RELAY_MAIN_GPIO": 4,
            "RELAY_AUX_GPIO": 5,
            "RELAY_ACTIVE_HIGH": 1,
        }

    def parse_all(self) -> dict[str, Any]:
        """Parse all relevant header files and return constants."""
        self._load_defaults()

        crockpot_h = self.firmware_path / "main" / "crockpot.h"
        relay_h = self.firmware_path / "main" / "relay.h"

        if crockpot_h.exists():
            self._parse_file(crockpot_h)
        if relay_h.exists():
            self._parse_file(relay_h)

        return self.constants

    def _parse_file(self, filepath: Path) -> None:
        """Parse a single header file for #define statements."""
        try:
            content = filepath.read_text(encoding="utf-8")
        except Exception:
            return

        # Match #define NAME VALUE patterns
        # Handles: integers, floats (with f suffix), hex values
        pattern = r"#define\s+(\w+)\s+([^\s/]+)"

        for match in re.finditer(pattern, content):
            name = match.group(1)
            value_str = match.group(2).strip()

            # Skip include guards and non-value defines
            if name.endswith("_H") or not value_str:
                continue

            value = self._parse_value(value_str)
            if value is not None:
                self.constants[name] = value

    def _parse_value(self, value_str: str) -> int | float | None:
        """Parse a C constant value to Python type."""
        # Remove trailing 'f' for float literals
        if value_str.endswith("f"):
            value_str = value_str[:-1]

        # Try float first (handles decimals)
        try:
            if "." in value_str:
                return float(value_str)
        except ValueError:
            pass

        # Try integer (handles hex, octal, decimal)
        try:
            if value_str.startswith("0x") or value_str.startswith("0X"):
                return int(value_str, 16)
            elif value_str.startswith("0") and len(value_str) > 1 and value_str[1:].isdigit():
                return int(value_str, 8)
            else:
                return int(value_str)
        except ValueError:
            pass

        return None

    def get(self, name: str, default: Any = None) -> Any:
        """Get a constant value by name."""
        return self.constants.get(name, default)


def watch_paths(firmware_path: Path) -> list[Path]:
    """Return list of paths to watch for changes."""
    return [
        firmware_path / "main" / "crockpot.h",
        firmware_path / "main" / "relay.h",
    ]
