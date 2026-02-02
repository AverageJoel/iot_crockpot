"""
Telegram bot interface for the crockpot simulator.
Mirrors the commands from firmware/main/telegram.c
"""

import asyncio
import logging
from typing import TYPE_CHECKING, Callable

from telegram import Update
from telegram.ext import Application, CommandHandler, ContextTypes

if TYPE_CHECKING:
    from crockpot_sim import CrockpotSimulator, CrockpotState

logger = logging.getLogger(__name__)


class TelegramBot:
    """Telegram bot that controls the crockpot simulator."""

    def __init__(
        self,
        token: str,
        simulator: "CrockpotSimulator",
        on_command: Callable[[str, str], None] | None = None,
    ):
        """
        Initialize the Telegram bot.

        Args:
            token: Telegram bot token from @BotFather
            simulator: CrockpotSimulator instance to control
            on_command: Optional callback when commands are received (cmd, response)
        """
        self.token = token
        self.simulator = simulator
        self.on_command = on_command
        self.application: Application | None = None
        self._running = False

    def _build_status_message(self) -> str:
        """Build status message matching firmware format."""
        status = self.simulator.get_status()

        lines = [
            "🍲 Crockpot Status:",
            f"State: {status.state.name}",
            f"Temperature: {status.temperature_f:.1f}°F",
            f"Uptime: {status.uptime_seconds} seconds",
            f"WiFi: {'Connected' if status.wifi_connected else 'Disconnected'}",
            f"Sensor: {'ERROR ⚠️' if status.sensor_error else 'OK ✓'}",
        ]

        if status.schedule_active:
            lines.append(f"Schedule: {status.schedule_name} (Step {status.schedule_step + 1}/{status.schedule_total_steps})")

        return "\n".join(lines)

    def _build_help_message(self) -> str:
        """Build help message matching firmware format."""
        return (
            "🍲 IoT Crockpot Commands:\n"
            "/status - Show current status\n"
            "/off - Turn off\n"
            "/warm - Set to warm\n"
            "/low - Set to low\n"
            "/high - Set to high\n"
            "/help - Show this help"
        )

    async def _cmd_start(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /start command."""
        await self._cmd_status(update, context)

    async def _cmd_status(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /status command."""
        response = self._build_status_message()
        await update.message.reply_text(response)
        if self.on_command:
            self.on_command("/status", response)

    async def _cmd_off(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /off command."""
        from crockpot_sim import CrockpotState
        self.simulator.set_state(CrockpotState.OFF)
        response = "Crockpot turned OFF"
        await update.message.reply_text(response)
        if self.on_command:
            self.on_command("/off", response)

    async def _cmd_warm(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /warm command."""
        from crockpot_sim import CrockpotState
        self.simulator.set_state(CrockpotState.WARM)
        response = "Crockpot set to WARM 🔥"
        await update.message.reply_text(response)
        if self.on_command:
            self.on_command("/warm", response)

    async def _cmd_low(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /low command."""
        from crockpot_sim import CrockpotState
        self.simulator.set_state(CrockpotState.LOW)
        response = "Crockpot set to LOW 🔥🔥"
        await update.message.reply_text(response)
        if self.on_command:
            self.on_command("/low", response)

    async def _cmd_high(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /high command."""
        from crockpot_sim import CrockpotState
        self.simulator.set_state(CrockpotState.HIGH)
        response = "Crockpot set to HIGH 🔥🔥🔥"
        await update.message.reply_text(response)
        if self.on_command:
            self.on_command("/high", response)

    async def _cmd_help(self, update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
        """Handle /help command."""
        response = self._build_help_message()
        await update.message.reply_text(response)
        if self.on_command:
            self.on_command("/help", response)

    async def start(self) -> None:
        """Start the bot (async)."""
        self.application = Application.builder().token(self.token).build()

        # Register command handlers
        self.application.add_handler(CommandHandler("start", self._cmd_start))
        self.application.add_handler(CommandHandler("status", self._cmd_status))
        self.application.add_handler(CommandHandler("off", self._cmd_off))
        self.application.add_handler(CommandHandler("warm", self._cmd_warm))
        self.application.add_handler(CommandHandler("low", self._cmd_low))
        self.application.add_handler(CommandHandler("high", self._cmd_high))
        self.application.add_handler(CommandHandler("help", self._cmd_help))

        self._running = True
        logger.info("Starting Telegram bot polling...")

        # Initialize and start polling
        await self.application.initialize()
        await self.application.start()
        await self.application.updater.start_polling(drop_pending_updates=True)

    async def stop(self) -> None:
        """Stop the bot."""
        self._running = False
        if self.application:
            await self.application.updater.stop()
            await self.application.stop()
            await self.application.shutdown()
            logger.info("Telegram bot stopped")

    @property
    def is_running(self) -> bool:
        return self._running
