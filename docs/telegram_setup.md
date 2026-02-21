# Telegram Bot Setup Guide

This guide explains how to create a Telegram bot and configure it for the IoT Crockpot.

## Step 1: Create a Bot with BotFather

1. Open Telegram and search for `@BotFather`
2. Start a chat and send `/newbot`
3. Follow the prompts:
   - Enter a name for your bot (e.g., "My Crockpot")
   - Enter a username (must end in `bot`, e.g., `my_crockpot_bot`)
4. BotFather will give you a **token** — save this securely

Example token format:
```
123456789:ABCdefGHIjklMNOpqrSTUvwxYZ
```

## Step 2: Find Your Chat ID

You need your Telegram chat ID to whitelist yourself and receive safety alerts.

1. Start a chat with your bot and send any message (e.g., `/start`)
2. Visit this URL in a browser, replacing `TOKEN`:
   ```
   https://api.telegram.org/botTOKEN/getUpdates
   ```
3. Find `"chat":{"id":YOUR_CHAT_ID}` in the JSON response

Alternatively, message `@userinfobot` on Telegram — it replies with your chat ID.

## Step 3: Configure the Firmware

Run `idf.py menuconfig` from the `firmware/` directory and navigate to:
**IoT Crockpot → Telegram**

| Setting | Description |
|---------|-------------|
| **Default Telegram bot token** | Paste your token here |
| **Allowed Telegram chat ID** | Your chat ID (leave blank to allow any) |

Build and flash. The token and chat ID are saved to NVS on first use and persist across reboots and re-flashes (unless NVS is erased).

### Updating Credentials at Runtime

Token and chat ID can be updated without re-flashing by calling `telegram_set_token()` from firmware code. NVS values take priority over Kconfig defaults.

## Step 4: Test the Bot

Once the crockpot is running and connected to WiFi:

1. Send `/status` to your bot — you should receive the current state and temperature
2. Try `/help` to see all available commands

## Available Commands

| Command | Description |
|---------|-------------|
| `/start` | Welcome message and current status |
| `/status` | Temperature, state, uptime, WiFi status |
| `/off` | Turn crockpot off |
| `/warm` | Set to warm mode |
| `/low` | Set to low mode |
| `/high` | Set to high mode |
| `/help` | List all commands |

## Automatic Safety Alerts

The bot sends an automatic alert (no command required) when a safety shutoff occurs:

- **Temperature limit exceeded**: fires when the crockpot auto-shuts off above 300°F
- **Persistent sensor error**: fires after 10 consecutive bad thermocouple readings while heating

Example alert message:
```
SAFETY SHUTOFF: Temperature 305.2 F exceeded limit. Crockpot turned OFF.
```

Alerts are sent to the **Allowed chat ID** configured in menuconfig. If no chat ID is set, alerts are silently dropped.

## Security

### Token Security
- Never share your bot token or commit it to version control
- If compromised, regenerate via BotFather: `/revoke`

### Chat ID Whitelist
Setting **Allowed Telegram chat ID** restricts the bot to only process commands from that specific user. Messages from any other chat ID are logged and ignored. Leave blank only on a private/trusted network.

## Troubleshooting

### Bot Not Responding

1. Check WiFi — serial monitor will show connection status at boot
2. Verify the token is correct (`idf.py menuconfig` or check NVS)
3. Check serial output for HTTP errors
4. Ensure `api.telegram.org` is reachable from your network

### First Response Is Slow

Long polling timeout is 30 seconds. The first message after a boot or reconnect may take up to 30s to receive a reply.

### "Unauthorized" Errors in Serial Log

Token is invalid or revoked. Regenerate with BotFather and re-configure via menuconfig.

## Example Conversation

```
You:  /status

Bot:  Crockpot Status:
      State: LOW
      Temperature: 185.4 F
      Uptime: 3600 seconds
      WiFi: Connected
      Sensor: OK

You:  /high

Bot:  Crockpot set to HIGH

--- later, safety event ---

Bot:  SAFETY SHUTOFF: Temperature 305.2 F exceeded limit. Crockpot turned OFF.
```

## BotFather Command Menu (Optional)

Set up an autocomplete menu in Telegram:

1. Message `@BotFather`, send `/setcommands`
2. Select your bot
3. Paste:

```
status - Show current status
off - Turn off
warm - Set to warm
low - Set to low
high - Set to high
help - Show help
```

Users will see this menu when they type `/` in the chat.
