# WiFi

**Type:** hardware module (`firmware/src/hardware/wifi/`)

Network connectivity. How this module reaches WiFi depends on the MCU family — the STM32
reference board needs a companion chip; an ESP32 board has WiFi on-die.

## STM32 boards: ESP-01 companion chip (default / reference)

On `blackpill_f411ce`, WiFi is an ESP-01 (ESP8266) module running its stock AT firmware, talking
over `USART2`:

```c
#define WIFI_RX_PIN  PA3
#define WIFI_TX_PIN  PA2
#define WIFI_BAUD    115200
```

These pins are deliberately not shared with the RX module's `USART1` pins, so a future board can
enable both WiFi and an RC receiver at once. `CH_PD`, `GPIO0`, `GPIO2` and `RST` on the ESP-01 are
pulled high locally on the module's own board (10k to 3V3) and don't connect to the STM32 at all.

The module speaks the ESP8266's AT command set (`proto_at.cpp`) over that serial link — from the
STM32's point of view, WiFi is just another UART peripheral it drives with text commands.

## ESP32 boards: native on-die WiFi

`esp32_wroom32` has no companion chip and no AT command layer — it drives the ESP32's own WiFi
radio directly (`wifi_esp32_driver.cpp`), through Espressif's own WiFi API rather than a serial
protocol.

**Scanning is blocking, not async, and this is deliberate.** An earlier async scan implementation
was unreliable in practice; the driver now blocks during a scan rather than risk a scan that never
completes or reports stale results. If you're porting WiFi to a new ESP32-family board, don't
"fix" this back to async without first reproducing why the async path was dropped.

## Turning it off

Set `FEATURE_WIFI 0` in the board header and reflash.
