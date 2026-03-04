# Touch & Display Tuning Notes

## FT6336U Register Writes (touch_driver.c)

Three registers are written at init time after `esp_lcd_touch_new_i2c_ft5x06`:

| Register | Name | Value | Why |
|----------|------|-------|-----|
| 0x86 | CTRL | 0x00 | Keep-active mode. Default (0x01) enters low-power between touches → first tap after idle is missed. |
| 0x88 | PERIODACTIVE | 0x01 | Active scan period in units of 10ms. Default 0x06 = 60ms → only 16Hz effective rate. 0x01 = 10ms = 100Hz. |
| 0x80 | THGROUP | 0x16 (22) | Touch detection threshold. **This module shipped at 0x46 (70)** — about 3× the normal sensitivity. Lowered to 22. Increase toward 40–50 if phantom touches appear. |

> **Warning**: Do NOT confuse 0x80 (THGROUP) with PERIODACTIVE. Writing 0x80 = 0x01 sets the threshold to 1 (extremely sensitive), flooding LVGL with phantom touches and causing UI freezes.

All three values are read back and logged at boot:
```
FT6336U CTRL (0x86) = 0x00
FT6336U PERIODACTIVE (0x88) = 0x01
FT6336U THGROUP (0x80) = 0x16 (22)
```

## LVGL Indev Architecture

### Old approach (problematic)
`lvgl_port_add_touch` registers an LVGL indev whose read callback runs **inside** the LVGL task. When LVGL is blocked waiting for SPI DMA between render bands, the indev timer cannot fire:

- 40-line buffer @ 80MHz SPI: ~4ms DMA wait per band × 8 bands = **~32ms blocked per frame**
- A tap that starts and ends during a render cycle is never detected
- Effective poll rate was ~16Hz despite the 8ms timer setting

### New approach (background task + cache)
A dedicated FreeRTOS task (`touch_poll`) reads the IC every 8ms independently of LVGL. It stores the result in `s_cache` (protected by a mutex). The LVGL indev callback (`display_touch_read_cb`) reads from this cache — no I2C, returns instantly.

```
FT6336U IC ──(I2C)──► touch_poll task (8ms, priority MAX-2)
                               │
                          s_cache (mutex)
                               │
LVGL indev timer ──────► display_touch_read_cb() ──► LVGL event system
```

### Interrupt-driven upgrade — IMPLEMENTED
The FT6336U INT pin is wired to **D7 (GPIO 20)** on the XIAO. `CONFIG_CROCKPOT_TOUCH_INT=20` is set in `sdkconfig.defaults`. The poll task automatically switches from `vTaskDelayUntil(8ms)` to a GPIO ISR + semaphore, giving sub-millisecond press detection.

Confirmed in boot log:
```
I (xxx) touch_driver: Touch poll task: INT-driven (GPIO 20)
```

Note: The XIAO uses native USB Serial/JTAG (no CH340). GPIO20 and GPIO21 are free I/O — they are NOT connected to any serial bridge chip.

## Display Rendering

### SPI clock
Set to 80MHz (`LCD_SPI_CLK_HZ` in `display_driver.h`). ST7796 supports up to 80MHz. If corruption appears, reduce to 60MHz.

### Draw buffer
40 lines × 480px × 2B × 2 (double buffer) = ~75KB. Double buffer allows CPU rendering and SPI DMA to overlap. If OOM at boot, reduce to 30 lines in `sdkconfig.defaults`.

### Screen transitions
Changed from `LV_SCR_LOAD_ANIM_OVER_LEFT/RIGHT` to `LV_SCR_LOAD_ANIM_FADE_IN` (120ms).

- OVER animations render two screens side-by-side each frame (2× the pixel work)
- FADE_IN only needs to render one screen per frame, blending via opacity
- No sliding pixel artifacts visible during the transition

## Summary of Changes Made

| File | Change | Effect |
|------|--------|--------|
| `touch_driver.c` | Write CTRL=0x00, PERIODACTIVE=0x01, THGROUP=0x16 | IC scans at 100Hz, normal sensitivity |
| `touch_driver.c` | Add `touch_poll_task` + `s_cache` | Touch reads decouple from LVGL rendering |
| `touch_driver.c` | Add `touch_driver_read_cached()` | Instant cache read for LVGL callback |
| `touch_driver.c` | ISR stub for INT pin (compiled if GPIO ≥ 0) | Ready for interrupt-driven upgrade |
| `display.c` | Replace `lvgl_port_add_touch` with custom indev | Uses cached reads, avoids I2C inside LVGL task |
| `display.c` | `lvgl_port_lock(200)` instead of `lock(0)` | Timer period reliably set to 8ms |
| `display_driver.h` | SPI clock 40→80MHz | Halves DMA wait, LVGL task less blocked |
| `display.c` | `double_buffer = true` | CPU+DMA overlap, fewer render stalls |
| `sdkconfig.defaults` | Draw buffer 20→40 lines | Halves flush count per frame |
| `gui.c` | Transitions OVER_LEFT/RIGHT → FADE_IN 120ms | No slide artifacts, less render work |
