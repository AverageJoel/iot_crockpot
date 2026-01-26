# Telegram Bot Setup Guide

This guide explains how to create a Telegram bot and configure it for the IoT Crockpot.

## Step 1: Create a Bot with BotFather

1. Open Telegram and search for `@BotFather`
2. Start a chat and send `/newbot`
3. Follow the prompts:
   - Enter a name for your bot (e.g., "My Crockpot")
   - Enter a username (must end in 'bot', e.g., "my_crockpot_bot")
4. BotFather will give you a **token** - save this securely!

Example token format:
```
123456789:ABCdefGHIjklMNOpqrSTUvwxYZ
```

## Step 2: Configure the Firmware

### Option A: Hardcode Token (Development Only)

Edit `firmware/main/telegram.c` and set the token:

```c
static char s_bot_token[64] = "YOUR_TOKEN_HERE";
```

**Warning:** Don't commit tokens to version control!

### Option B: Set via Serial Console (Recommended)

1. Build and flash the firmware
2. Connect via serial monitor
3. Use the configuration command (TBD)

### Option C: Store in NVS (Production)

Future implementation will support storing the token in NVS (Non-Volatile Storage) for persistence across reboots.

## Step 3: Find Your Chat ID

To receive messages, you need your Telegram chat ID:

1. Start a chat with your bot
2. Send any message (e.g., "/start")
3. Visit this URL in a browser (replace TOKEN):
   ```
   https://api.telegram.org/botTOKEN/getUpdates
   ```
4. Find `"chat":{"id":YOUR_CHAT_ID}` in the response

## Step 4: Test the Bot

Once the crockpot is running and connected to WiFi:

1. Send `/status` to your bot
2. You should receive the current status
3. Try `/help` to see available commands

## Available Commands

| Command | Description |
|---------|-------------|
| `/start` | Welcome message and status |
| `/status` | Current temperature and state |
| `/off` | Turn crockpot off |
| `/warm` | Set to warm mode |
| `/low` | Set to low mode |
| `/high` | Set to high mode |
| `/help` | List all commands |

## Security Considerations

### Bot Token Security
- Never share your bot token publicly
- Don't commit tokens to Git
- Regenerate token if compromised (via BotFather: `/revoke`)

### Access Control (Future)
Consider implementing:
- Whitelist of allowed chat IDs
- Password/PIN for control commands
- Notification-only mode for unauthorized users

## Troubleshooting

### Bot Not Responding

1. Check WiFi connection on ESP32
2. Verify token is correct
3. Check serial output for errors
4. Ensure Telegram API is accessible (not blocked)

### "Unauthorized" Errors

- Token is invalid or revoked
- Regenerate token with BotFather

### Slow Response

- Long polling timeout is 30 seconds by default
- First message after boot may take up to 30s
- Check WiFi signal strength

## Example Conversation

```
You: /status

Bot: Crockpot Status:
     State: LOW
     Temperature: 185.4 F
     Uptime: 3600 seconds
     WiFi: Connected
     Sensor: OK

You: /high

Bot: Crockpot set to HIGH

You: /status

Bot: Crockpot Status:
     State: HIGH
     Temperature: 186.2 F
     ...
```

## Advanced: Bot Commands Menu

You can set up a command menu in BotFather:

1. Message @BotFather
2. Send `/setcommands`
3. Select your bot
4. Paste this:

```
status - Show current status
off - Turn off
warm - Set to warm
low - Set to low
high - Set to high
help - Show help
```

Now users will see a menu when they type `/`.
