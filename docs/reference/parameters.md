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

## Drive

| Setting | Key | Default | Range |
|---|---|---|---|
| Drive Mode | `drive.mode` | `skid` | `skid`, `car` |
| Throttle Src | `drive.throttle_src` | `ch2` | `ch1`–`ch12` |
| Steer Src | `drive.steer_src` | `ch1` | `ch1`–`ch12` |
| Forward Ratio (%) | `drive.forward_ratio` | `100` | 0–100 |
| Reverse Ratio (%) | `drive.reverse_ratio` | `100` | 0–100 |
| Steer Ratio (%) | `drive.steer_ratio` | `100` | 0–100 |
| Arm Src | `drive.arm_src` | `ch5` | `none`, `ch1`–`ch12` |
| Arm Min (µs) | `drive.arm_min` | `1700` | 1000–2000 |
| Arm Max (µs) | `drive.arm_max` | `2000` | 1000–2000 |

The mixer. It takes throttle and steering and writes two outputs, which
`motor<N>.src` and `servo.src` then select with `drive_left` and `drive_right`.

**Drive Mode** decides what those two outputs mean:

| | `drive_left` | `drive_right` |
|---|---|---|
| `skid` | left track | right track |
| `car` | throttle | steer |

`skid` mixes throttle and steer into both sides, so the vehicle turns by driving them at
different speeds — tracks, a 4WD rover, or a 2WD skid-steer chassis. `car` keeps the two apart:
throttle drives one motor, steer drives a servo, and neither affects the other.

The three ratios cap authority independently — see [Tuning](../drive/tuning.md). Arming is
covered in [Arming and modes](arming-and-modes.md).

## Motor 0 and Motor 1

Both ESCs carry the same settings. `motor0` drives the left track, `motor1` the right.

| Setting | Key | Default | Range |
|---|---|---|---|
| Type | `motor0.type` / `motor1.type` | `brushless` | `brushless`, `brushed` |
| PWM Rate (Hz) | `motor0.rate` / `motor1.rate` | `50` | `50`, `100`, `200`, `400` |
| ESC mode | `motor0.mode` / `motor1.mode` | `input` | `off`, `armed`, `input` |
| Throttle (µs) | `motor0.throttle_us` / `motor1.throttle_us` | `1500` | 1000–2000 |
| Min (µs) | `motor0.min_us` / `motor1.min_us` | `1000` | 500–1500 |
| Max (µs) | `motor0.max_us` / `motor1.max_us` | `2000` | 1500–2500 |
| Source | `motor0.src` | `drive_left` | `ch1`–`ch12`, `drive_left`, `drive_right` |
| Source | `motor1.src` | `drive_right` | `ch1`–`ch12`, `drive_left`, `drive_right` |

**Source** is where the ESC takes its command from. `drive_left` and `drive_right` are the two
outputs of the tank mixer — that is the normal setting. Pointing an ESC at a raw channel instead
bypasses the mixer entirely.

**Throttle** is a manual output used when the mode is not `input`. **Min** and **Max** are the
ESC's calibrated endpoints; they cannot cross.

### Brushed motors (H-Bridge)

Shown only when `Type` is `brushed`.

| Setting | Key | Default | Range |
|---|---|---|---|
| Switch Freq (Hz) | `motor0.freq` / `motor1.freq` | `20000` | 1000–50000 |
| Invert | `motor0.invert` / `motor1.invert` | `normal` | `normal`, `inverted` |
| At Zero | `motor0.brake` / `motor1.brake` | `coast` | `coast`, `brake` |

**Switch Freq** is the H-bridge's PWM switching frequency — 20 kHz is above the audible range and
every DRV8833/TB6612-class driver handles it fine.

**Invert** is per motor, not shared — fixes a swapped H-bridge lead pair without rewiring.

**At Zero** decides what happens at zero command: `coast` (both H-bridge inputs low, motor spins
freely) or `brake` (both high, resisting motion).

## Servo

| Setting | Key | Default | Range |
|---|---|---|---|
| Servo | `servo.mode` | `off` | `off`, `hold`, `sweep`, `input` |
| Angle (°) | `servo.angle` | `90` | 0–180 |
| Sweep (s) | `servo.sweep_s` | `4` | 1–30 |
| Min (µs) | `servo.min_us` | `1000` | 500–1500 |
| Max (µs) | `servo.max_us` | `2000` | 1500–2500 |
| Source | `servo.src` | `ch2` | `ch1`–`ch12`, `drive_left`, `drive_right` |
| Invert | `servo.invert` | `normal` | `normal`, `reversed` |
| Trim (µs) | `servo.trim_us` | `0` | −250–250 |

**Mode** picks where the pulse comes from: `hold` parks at **Angle**, `sweep` runs back and
forth over **Sweep** seconds, `input` follows **Source**. `off` detaches the pin so the servo
relaxes and draws no holding current.

**Source** accepts a raw channel or, for a steered car, `drive_right` — the mixer's steer output.

**Invert** and **Trim** describe the linkage, not the input, so they apply in every mode. Invert
mirrors travel about the centre of Min/Max; Trim then shifts the centre. Set Invert if the wheels
turn the wrong way, then use Trim to bring them straight.

There is no arm gate on the servo. Steering keeps working whether or not the vehicle is armed,
and when the link drops — a vehicle still rolling is better steerable than not.

## Telemetry values

Read-only values the board reports. These are displayed, never set.

| Group | Values |
|---|---|
| System | Uptime, Clock (MHz), Free RAM (kB), Temp (°C), VDD (V), Fault, Loop (Hz), Worst Pass (µs) |
| RC Channels | CH1–CH16, in µs |
| RC Link | Link, LQ (%), RSSI (dBm), Rate (Hz), Errors, RF Rate (Hz), TX Power (mW) |
| Drive | Left and Right output, in µs |
| Motor 0 | Output (µs), Armed |
| Motor 1 | Output (µs), Armed |

**Fault** reads `None` on a healthy board. **Worst Pass** is the longest single loop iteration
seen, which is the number that matters if control ever feels laggy.
