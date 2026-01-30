"""
Data logging for temperature and state history.

Captures status every LOG_INTERVAL_SECONDS for display and export.
"""

from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING
import csv
import json
import time

if TYPE_CHECKING:
    from crockpot_sim import CrockpotStatus, CrockpotState


# Configuration
LOG_INTERVAL_SECONDS = 60   # Log every 60 seconds
MAX_LOG_ENTRIES = 1440      # 24 hours of history at 60s intervals


@dataclass
class LogEntry:
    """A single log entry capturing system state."""
    timestamp: int          # Uptime seconds when logged
    temperature_f: float
    state: "CrockpotState"
    relay_main: bool
    relay_aux: bool
    schedule_active: bool = False
    schedule_name: str = ""
    schedule_step: int = 0

    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization."""
        return {
            "timestamp": self.timestamp,
            "temperature_f": self.temperature_f,
            "state": self.state.name,
            "relay_main": self.relay_main,
            "relay_aux": self.relay_aux,
            "schedule_active": self.schedule_active,
            "schedule_name": self.schedule_name,
            "schedule_step": self.schedule_step,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "LogEntry":
        """Create from dictionary."""
        from crockpot_sim import CrockpotState
        return cls(
            timestamp=data["timestamp"],
            temperature_f=data["temperature_f"],
            state=CrockpotState[data["state"]],
            relay_main=data["relay_main"],
            relay_aux=data["relay_aux"],
            schedule_active=data.get("schedule_active", False),
            schedule_name=data.get("schedule_name", ""),
            schedule_step=data.get("schedule_step", 0),
        )


class DataLog:
    """
    Ring buffer for temperature and state logging.

    Call tick() every second from the control loop.
    Logs are captured every LOG_INTERVAL_SECONDS.
    """

    def __init__(
        self,
        log_interval: int = LOG_INTERVAL_SECONDS,
        max_entries: int = MAX_LOG_ENTRIES,
    ):
        self._log_interval = log_interval
        self._max_entries = max_entries
        self._entries: deque[LogEntry] = deque(maxlen=max_entries)
        self._seconds_since_last_log = 0
        self._schedule_active = False
        self._schedule_name = ""
        self._schedule_step = 0

    @property
    def entries(self) -> list[LogEntry]:
        """Get all log entries."""
        return list(self._entries)

    @property
    def entry_count(self) -> int:
        """Number of entries in the log."""
        return len(self._entries)

    @property
    def log_interval(self) -> int:
        """Seconds between log entries."""
        return self._log_interval

    def set_schedule_info(
        self,
        active: bool,
        name: str = "",
        step: int = 0,
    ) -> None:
        """Update schedule info for logging."""
        self._schedule_active = active
        self._schedule_name = name
        self._schedule_step = step

    def tick(self, status: "CrockpotStatus") -> bool:
        """
        Update the log with current status.

        Call this every second. Returns True if a new entry was logged.
        """
        self._seconds_since_last_log += 1

        if self._seconds_since_last_log >= self._log_interval:
            self._seconds_since_last_log = 0
            self._log_entry(status)
            return True

        return False

    def force_log(self, status: "CrockpotStatus") -> None:
        """Force an immediate log entry."""
        self._log_entry(status)
        self._seconds_since_last_log = 0

    def _log_entry(self, status: "CrockpotStatus") -> None:
        """Create and store a log entry."""
        entry = LogEntry(
            timestamp=status.uptime_seconds,
            temperature_f=status.temperature_f,
            state=status.state,
            relay_main=status.relay_main,
            relay_aux=status.relay_aux,
            schedule_active=self._schedule_active,
            schedule_name=self._schedule_name,
            schedule_step=self._schedule_step,
        )
        self._entries.append(entry)

    def get_recent(self, count: int) -> list[LogEntry]:
        """Get the most recent N entries."""
        entries = list(self._entries)
        return entries[-count:] if count < len(entries) else entries

    def get_temperature_history(self, count: int | None = None) -> list[float]:
        """Get temperature values from recent entries."""
        entries = self.get_recent(count) if count else list(self._entries)
        return [e.temperature_f for e in entries]

    def clear(self) -> None:
        """Clear all log entries."""
        self._entries.clear()
        self._seconds_since_last_log = 0

    def to_csv(self, path: Path) -> None:
        """Export log to CSV file."""
        path.parent.mkdir(parents=True, exist_ok=True)

        with open(path, "w", newline="") as f:
            writer = csv.writer(f)

            # Header
            writer.writerow([
                "timestamp",
                "temperature_f",
                "state",
                "relay_main",
                "relay_aux",
                "schedule_active",
                "schedule_name",
                "schedule_step",
            ])

            # Data rows
            for entry in self._entries:
                writer.writerow([
                    entry.timestamp,
                    f"{entry.temperature_f:.1f}",
                    entry.state.name,
                    1 if entry.relay_main else 0,
                    1 if entry.relay_aux else 0,
                    1 if entry.schedule_active else 0,
                    entry.schedule_name,
                    entry.schedule_step,
                ])

    def to_json(self, path: Path) -> None:
        """Export log to JSON file."""
        path.parent.mkdir(parents=True, exist_ok=True)

        data = {
            "log_interval_seconds": self._log_interval,
            "entry_count": len(self._entries),
            "entries": [e.to_dict() for e in self._entries],
        }

        with open(path, "w") as f:
            json.dump(data, f, indent=2)

    def from_json(self, path: Path) -> bool:
        """Load log from JSON file. Returns True on success."""
        if not path.exists():
            return False

        try:
            with open(path, "r") as f:
                data = json.load(f)

            entries = [LogEntry.from_dict(e) for e in data.get("entries", [])]
            self._entries = deque(entries, maxlen=self._max_entries)
            return True
        except (json.JSONDecodeError, KeyError, ValueError):
            return False

    def get_stats(self) -> dict:
        """Get statistics from the log."""
        if not self._entries:
            return {
                "min_temp": 0.0,
                "max_temp": 0.0,
                "avg_temp": 0.0,
                "duration_seconds": 0,
                "entry_count": 0,
            }

        temps = [e.temperature_f for e in self._entries]
        first_ts = self._entries[0].timestamp
        last_ts = self._entries[-1].timestamp

        return {
            "min_temp": min(temps),
            "max_temp": max(temps),
            "avg_temp": sum(temps) / len(temps),
            "duration_seconds": last_ts - first_ts,
            "entry_count": len(self._entries),
        }

    def generate_filename(self, extension: str = "csv") -> str:
        """Generate a timestamped filename for export."""
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        return f"crockpot_log_{timestamp}.{extension}"
