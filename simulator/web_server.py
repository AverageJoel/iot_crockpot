"""
Web server interface for the crockpot simulator.
Provides REST API and simple web UI for control.
"""

import asyncio
import json
import logging
from typing import TYPE_CHECKING, Callable

from aiohttp import web

if TYPE_CHECKING:
    from crockpot_sim import CrockpotSimulator

logger = logging.getLogger(__name__)

# Simple HTML UI
HTML_PAGE = """<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>IoT Crockpot</title>
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            max-width: 400px;
            margin: 0 auto;
            padding: 20px;
            background: #1a1a2e;
            color: #eee;
            min-height: 100vh;
        }
        h1 { text-align: center; margin-bottom: 30px; }
        .status-card {
            background: #16213e;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .status-row {
            display: flex;
            justify-content: space-between;
            padding: 8px 0;
            border-bottom: 1px solid #0f3460;
        }
        .status-row:last-child { border-bottom: none; }
        .status-label { color: #888; }
        .status-value { font-weight: bold; }
        .state-OFF { color: #666; }
        .state-WARM { color: #f39c12; }
        .state-LOW { color: #e74c3c; }
        .state-HIGH { color: #c0392b; }
        .buttons {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
        }
        button {
            padding: 15px;
            font-size: 16px;
            font-weight: bold;
            border: none;
            border-radius: 8px;
            cursor: pointer;
            transition: transform 0.1s, opacity 0.1s;
        }
        button:active { transform: scale(0.95); }
        button:disabled { opacity: 0.5; cursor: not-allowed; }
        .btn-off { background: #34495e; color: white; }
        .btn-warm { background: #f39c12; color: white; }
        .btn-low { background: #e74c3c; color: white; }
        .btn-high { background: #c0392b; color: white; }
        .btn-active { box-shadow: 0 0 0 3px #3498db; }
        .error { color: #e74c3c; }
        .ok { color: #2ecc71; }
        .schedule-info {
            background: #0f3460;
            border-radius: 8px;
            padding: 10px;
            margin-top: 10px;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <h1>🍲 IoT Crockpot</h1>

    <div class="status-card" id="status">
        <div class="status-row">
            <span class="status-label">State</span>
            <span class="status-value" id="state">--</span>
        </div>
        <div class="status-row">
            <span class="status-label">Temperature</span>
            <span class="status-value" id="temp">--</span>
        </div>
        <div class="status-row">
            <span class="status-label">Uptime</span>
            <span class="status-value" id="uptime">--</span>
        </div>
        <div class="status-row">
            <span class="status-label">Sensor</span>
            <span class="status-value" id="sensor">--</span>
        </div>
        <div id="schedule-container"></div>
    </div>

    <div class="buttons">
        <button class="btn-off" onclick="setState('off')">OFF</button>
        <button class="btn-warm" onclick="setState('warm')">WARM</button>
        <button class="btn-low" onclick="setState('low')">LOW</button>
        <button class="btn-high" onclick="setState('high')">HIGH</button>
    </div>

    <script>
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();

                const stateEl = document.getElementById('state');
                stateEl.textContent = data.state;
                stateEl.className = 'status-value state-' + data.state;

                document.getElementById('temp').textContent = data.temperature_f.toFixed(1) + '°F';

                const mins = Math.floor(data.uptime_seconds / 60);
                const secs = data.uptime_seconds % 60;
                document.getElementById('uptime').textContent = mins + 'm ' + secs + 's';

                const sensorEl = document.getElementById('sensor');
                sensorEl.textContent = data.sensor_error ? 'ERROR' : 'OK';
                sensorEl.className = 'status-value ' + (data.sensor_error ? 'error' : 'ok');

                // Update button active states
                document.querySelectorAll('.buttons button').forEach(btn => {
                    btn.classList.remove('btn-active');
                });
                const activeBtn = document.querySelector('.btn-' + data.state.toLowerCase());
                if (activeBtn) activeBtn.classList.add('btn-active');

                // Schedule info
                const scheduleContainer = document.getElementById('schedule-container');
                if (data.schedule_active) {
                    scheduleContainer.innerHTML = `
                        <div class="schedule-info">
                            <strong>Schedule:</strong> ${data.schedule_name}<br>
                            Step ${data.schedule_step + 1}/${data.schedule_total_steps}
                        </div>
                    `;
                } else {
                    scheduleContainer.innerHTML = '';
                }
            } catch (e) {
                console.error('Failed to fetch status:', e);
            }
        }

        async function setState(state) {
            try {
                await fetch('/api/state/' + state, { method: 'POST' });
                await fetchStatus();
            } catch (e) {
                console.error('Failed to set state:', e);
            }
        }

        // Poll for status updates
        fetchStatus();
        setInterval(fetchStatus, 1000);
    </script>
</body>
</html>
"""


class WebServer:
    """Web server that controls the crockpot simulator."""

    def __init__(
        self,
        simulator: "CrockpotSimulator",
        host: str = "0.0.0.0",
        port: int = 8080,
        on_command: Callable[[str, str], None] | None = None,
    ):
        """
        Initialize the web server.

        Args:
            simulator: CrockpotSimulator instance to control
            host: Host to bind to (default: 0.0.0.0)
            port: Port to bind to (default: 8080)
            on_command: Optional callback when commands are received (endpoint, response)
        """
        self.simulator = simulator
        self.host = host
        self.port = port
        self.on_command = on_command
        self.app = web.Application()
        self.runner: web.AppRunner | None = None
        self._running = False

        # Set up routes
        self.app.router.add_get("/", self._handle_index)
        self.app.router.add_get("/api/status", self._handle_status)
        self.app.router.add_post("/api/state/{state}", self._handle_set_state)
        self.app.router.add_get("/api/help", self._handle_help)

    async def _handle_index(self, request: web.Request) -> web.Response:
        """Serve the web UI."""
        return web.Response(text=HTML_PAGE, content_type="text/html")

    async def _handle_status(self, request: web.Request) -> web.Response:
        """Return current status as JSON."""
        status = self.simulator.get_status()
        data = {
            "state": status.state.name,
            "temperature_f": status.temperature_f,
            "uptime_seconds": status.uptime_seconds,
            "wifi_connected": status.wifi_connected,
            "sensor_error": status.sensor_error,
            "relay_main": status.relay_main,
            "relay_aux": status.relay_aux,
            "schedule_active": status.schedule_active,
            "schedule_name": status.schedule_name,
            "schedule_step": status.schedule_step,
            "schedule_total_steps": status.schedule_total_steps,
        }
        return web.json_response(data)

    async def _handle_set_state(self, request: web.Request) -> web.Response:
        """Set crockpot state."""
        state_str = request.match_info["state"].upper()
        state = self.simulator.state_from_string(state_str)

        if state is None:
            return web.json_response(
                {"error": f"Invalid state: {state_str}"},
                status=400
            )

        self.simulator.set_state(state)
        response = f"Crockpot set to {state.name}"

        if self.on_command:
            self.on_command(f"/api/state/{state_str.lower()}", response)

        return web.json_response({"success": True, "state": state.name})

    async def _handle_help(self, request: web.Request) -> web.Response:
        """Return API help."""
        help_text = {
            "endpoints": {
                "GET /": "Web UI",
                "GET /api/status": "Get current status",
                "POST /api/state/{off|warm|low|high}": "Set state",
                "GET /api/help": "This help message",
            }
        }
        return web.json_response(help_text)

    async def start(self) -> None:
        """Start the web server."""
        self.runner = web.AppRunner(self.app)
        await self.runner.setup()
        site = web.TCPSite(self.runner, self.host, self.port)
        await site.start()
        self._running = True
        logger.info(f"Web server started at http://{self.host}:{self.port}")

    async def stop(self) -> None:
        """Stop the web server."""
        self._running = False
        if self.runner:
            await self.runner.cleanup()
            logger.info("Web server stopped")

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def url(self) -> str:
        """Get the server URL."""
        host = "localhost" if self.host == "0.0.0.0" else self.host
        return f"http://{host}:{self.port}"
