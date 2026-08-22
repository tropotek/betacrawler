# Parameters

Every setting the board publishes. The **Key** column is what you type in the
[Terminal](terminal-and-backup.md) and what appears in a settings backup file.

Values apply the moment you change them. They are not written to flash until you press **Save to
flash**.

## Device

| Setting | Key | Default | Range |
|---|---|---|---|
| Device Name | `device.name` | `betacrawler` | up to 31 characters |

A label for your own benefit, on the **Configuration** page and in a settings backup. Useful if
you have more than one vehicle.

## Telemetry

| Setting | Key | Default | Range |
|---|---|---|---|
| Rate (Hz) | `tlm.rate` | `10` | 1–50 |

How often the board pushes live values to the app. Display only — it has no effect on how the
vehicle drives.

## Receiver

| Setting | Key | Default | Range |
|---|---|---|---|
| Protocol | `rx.protocol` | `elrs` | `crossfire`, `elrs` |
| Source | `rx.source` | `uart` | `uart`, `sim` |
| Deadband (µs) | `rx.deadband_us` | `0` | 0–200 |

`rx.source` selects where channel data comes from. Leave it on `uart` — that is the physical
receiver. `sim` generates fake channel movement for testing without a handset.

Deadband ignores stick movement near centre. Raise it if the vehicle creeps when the sticks are
released.

### Link timeouts

| Setting | Key | Default | Range |
|---|---|---|---|
| Crossfire Timeout (ms) | `crossfire.timeout_ms` | `1000` | 100–2000 |
| ELRS Timeout (ms) | `elrs.timeout_ms` | `200` | 50–2000 |

How long the board waits without a valid frame before treating the radio link as lost and
clamping the outputs to neutral. Only the one matching your protocol applies.

## Tank Drive

| Setting | Key | Default | Range |
|---|---|---|---|
| Throttle Src | `tank_drive.throttle_src` | `ch2` | `ch1`–`ch12` |
| Steer Src | `tank_drive.steer_src` | `ch1` | `ch1`–`ch12` |
| Forward Ratio (%) | `tank_drive.forward_ratio` | `100` | 0–100 |
| Reverse Ratio (%) | `tank_drive.reverse_ratio` | `100` | 0–100 |
| Steer Ratio (%) | `tank_drive.steer_ratio` | `100` | 0–100 |
| Arm Src | `tank_drive.arm_src` | `ch5` | `none`, `ch1`–`ch12` |
| Arm Min (µs) | `tank_drive.arm_min` | `1700` | 1000–2000 |
| Arm Max (µs) | `tank_drive.arm_max` | `2000` | 1000–2000 |

The mixer. It takes throttle and steering and produces a speed for each track.

The three ratios cap authority independently — see [Tuning](../drive/tuning.md). Arming is
covered in [Arming and modes](arming-and-modes.md).

## ESC 0 and ESC 1

Both ESCs carry the same settings. `esc0` drives the left track, `esc1` the right.

| Setting | Key | Default | Range |
|---|---|---|---|
| Direction | `esc0.direction` / `esc1.direction` | `bidirectional` | `unidirectional`, `bidirectional` |
| PWM Rate (Hz) | `esc0.rate` / `esc1.rate` | `50` | `50`, `100`, `200`, `400` |
| ESC mode | `esc0.mode` / `esc1.mode` | `input` | `off`, `armed`, `input` |
| Throttle (µs) | `esc0.throttle_us` / `esc1.throttle_us` | `1500` | 1000–2000 |
| Min (µs) | `esc0.min_us` / `esc1.min_us` | `1000` | 500–1500 |
| Max (µs) | `esc0.max_us` / `esc1.max_us` | `2000` | 1500–2500 |
| Source | `esc0.src` | `drive_left` | `ch1`–`ch12`, `drive_left`, `drive_right` |
| Source | `esc1.src` | `drive_right` | `ch1`–`ch12`, `drive_left`, `drive_right` |

**Direction** must match how the ESC itself is configured in BLHeli Configurator. Bidirectional
means centre-stick is stop, above is forward, below is reverse.

**Source** is where the ESC takes its command from. `drive_left` and `drive_right` are the two
outputs of the tank mixer — that is the normal setting. Pointing an ESC at a raw channel instead
bypasses the mixer entirely.

**Throttle** is a manual output used when the mode is not `input`. **Min** and **Max** are the
ESC's calibrated endpoints; they cannot cross.

## Telemetry values

Read-only values the board reports. These are displayed, never set.

| Group | Values |
|---|---|
| System | Uptime, Clock (MHz), Free RAM (kB), Temp (°C), VDD (V), Fault, Loop (Hz), Worst Pass (µs) |
| RC Channels | CH1–CH16, in µs |
| RC Link | Link, LQ (%), RSSI (dBm), Rate (Hz), Errors, RF Rate (Hz), TX Power (mW) |
| Tank Drive | Left and Right output, in µs |
| ESC 0 | Output (µs), Armed |
| ESC 1 | Output (µs), Armed |

**Fault** reads `None` on a healthy board. **Worst Pass** is the longest single loop iteration
seen, which is the number that matters if control ever feels laggy.
