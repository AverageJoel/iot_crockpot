"""
Remote control manager for the crockpot simulator.
Coordinates Telegram bot and web server in a background asyncio thread.
"""

import asyncio
import logging
import os
import threading
from pathlib import Path
from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from crockpot_sim import CrockpotSimulator

logger = logging.getLogger(__name__)


def load_env_file(path: Path) -> dict[str, str]:
    """Load environment variables from a .env file."""
    env_vars = {}
    if path.exists():
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, _, value = line.partition("=")
                    # Remove quotes if present
                    value = value.strip().strip('"').strip("'")
                    env_vars[key.strip()] = value
    return env_vars


class RemoteControlManager:
    """
    Manages remote control interfaces (Telegram, Web) in a background thread.
    """

    def __init__(
        self,
        simulator: "CrockpotSimulator",
        on_message: Callable[[str], None] | None = None,
        web_port: int = 8080,
    ):
        """
        Initialize the remote control manager.

        Args:
            simulator: CrockpotSimulator instance to control
            on_message: Callback for log messages to display in TUI
            web_port: Port for web server (default: 8080)
        """
        self.simulator = simulator
        self.on_message = on_message
        self.web_port = web_port

        self._thread: threading.Thread | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._telegram_bot = None
        self._web_server = None
        self._running = False

        # Load .env file if present
        env_path = Path(__file__).parent / ".env"
        env_vars = load_env_file(env_path)
        for key, value in env_vars.items():
            if key not in os.environ:
                os.environ[key] = value

        # Get Telegram token from environment
        self.telegram_token = os.environ.get("TELEGRAM_BOT_TOKEN", "")

    def _log(self, message: str) -> None:
        """Log a message to TUI and logger."""
        logger.info(message)
        if self.on_message:
            self.on_message(message)

    def _on_telegram_command(self, command: str, response: str) -> None:
        """Callback when Telegram command is received."""
        self._log(f"[cyan]Telegram[/] {command}")

    def _on_web_command(self, endpoint: str, response: str) -> None:
        """Callback when web command is received."""
        self._log(f"[green]Web[/] {endpoint}")

    async def _run_services(self) -> None:
        """Run all remote control services."""
        tasks = []

        # Start web server
        try:
            from web_server import WebServer

            self._web_server = WebServer(
                simulator=self.simulator,
                port=self.web_port,
                on_command=self._on_web_command,
            )
            await self._web_server.start()
            self._log(f"[green]Web server[/] http://localhost:{self.web_port}")
        except Exception as e:
            self._log(f"[red]Web server failed:[/] {e}")

        # Start Telegram bot if token is configured
        if self.telegram_token:
            try:
                from telegram_bot import TelegramBot

                self._telegram_bot = TelegramBot(
                    token=self.telegram_token,
                    simulator=self.simulator,
                    on_command=self._on_telegram_command,
                )
                await self._telegram_bot.start()
                self._log("[cyan]Telegram bot[/] connected")
            except Exception as e:
                self._log(f"[red]Telegram bot failed:[/] {e}")
        else:
            self._log("[yellow]Telegram[/] not configured (set TELEGRAM_BOT_TOKEN)")

        # Keep running until stopped
        try:
            while self._running:
                await asyncio.sleep(0.5)
        except asyncio.CancelledError:
            pass

        # Cleanup
        if self._telegram_bot:
            await self._telegram_bot.stop()
        if self._web_server:
            await self._web_server.stop()

    def _thread_main(self) -> None:
        """Main function for the background thread."""
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)

        try:
            self._loop.run_until_complete(self._run_services())
        except Exception as e:
            logger.exception(f"Remote control error: {e}")
        finally:
            self._loop.close()

    def start(self) -> None:
        """Start remote control services in a background thread."""
        if self._running:
            return

        self._running = True
        self._thread = threading.Thread(target=self._thread_main, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """Stop all remote control services."""
        self._running = False

        if self._loop:
            # Signal the async loop to stop
            self._loop.call_soon_threadsafe(self._loop.stop)

        if self._thread:
            self._thread.join(timeout=2)

    @property
    def web_url(self) -> str | None:
        """Get the web server URL if running."""
        if self._web_server and self._web_server.is_running:
            return self._web_server.url
        return None

    @property
    def telegram_connected(self) -> bool:
        """Check if Telegram bot is connected."""
        return self._telegram_bot is not None and self._telegram_bot.is_running
