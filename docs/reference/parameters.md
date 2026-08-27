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

The mixer. It takes throttle and steering and writes three outputs, which
`motor<N>.src` selects with `drive_left`, `drive_right` and `drive_steer`.

**Drive Mode** decides what the two motor outputs carry. `drive_steer` carries the steering
command in both modes, which is why a servo pointed at it works either way:

| | `drive_left` | `drive_right` | `drive_steer` |
|---|---|---|---|
| `skid` | left track | right track | steer |
| `car` | throttle | throttle | steer |

In `car` mode both motor outputs carry the same throttle, so a single motor drives whichever pin
it is wired to and a two-wheel-drive car works by wiring both.

`skid` mixes throttle and steer into both sides, so the vehicle turns by driving them at
different speeds — tracks, a 4WD rover, or a 2WD skid-steer chassis. `car` keeps the two apart:
throttle drives one motor, steer drives a servo, and neither affects the other.

The three ratios cap authority independently — see [Tuning](../drive/tuning.md). Arming is
covered in [Arming and modes](arming-and-modes.md).

## Motor 0 and Motor 1

Both ESCs carry the same settings. `motor0` drives the left side, `motor1` the right.

| Setting | Key | Default | Range |
|---|---|---|---|
| Type | `motor0.type` / `motor1.type` | `none` | `none`, `brushless`, `brushed` |
| PWM Rate (Hz) | `motor0.rate` / `motor1.rate` | `50` | `50`, `100`, `200`, `400` |
| Motor mode | `motor0.mode` / `motor1.mode` | `input` | `off`, `armed`, `input` |
| Throttle (µs) | `motor0.throttle_us` / `motor1.throttle_us` | `1500` | 1000–2000 |
| Min (µs) | `motor0.min_us` / `motor1.min_us` | `1000` | 500–1500 |
| Max (µs) | `motor0.max_us` / `motor1.max_us` | `2000` | 1500–2500 |
| Source | `motor0.src` | `drive_left` | `ch1`–`ch12`, `drive_left`, `drive_right`, `drive_steer` |
| Source | `motor1.src` | `drive_right` | `ch1`–`ch12`, `drive_left`, `drive_right`, `drive_steer` |
| Invert | `motor0.invert` / `motor1.invert` | `normal` | `normal`, `inverted` |

**Invert** reverses which way that motor turns, per motor, for either output type: an H-bridge
swaps which pin drives which lead, and an ESC has its pulse mirrored about neutral. Swapping two
motor wires is the better permanent fix — this is for a motor you cannot reach, sealed inside a
model. It reverses the signal, not the motor, so braking and the ESC's own reverse behaviour are
unaffected; if you later swap wires as well, the two cancel out.

**Source** is where the ESC takes its command from. `drive_left` and `drive_right` are the
mixer's two motor outputs — that is the normal setting. Pointing an ESC at a raw channel instead
bypasses the mixer entirely. Set from the Terminal; no page shows it.

**Throttle** is a manual output used when the mode is not `input`. **Min** and **Max** are the
ESC's calibrated endpoints; they cannot cross.

### Brushed motors (H-Bridge)

Shown only when `Type` is `brushed`.

| Setting | Key | Default | Range |
|---|---|---|---|
| Switch Freq (Hz) | `motor0.freq` / `motor1.freq` | `20000` | 1000–50000 |
| At Zero | `motor0.brake` / `motor1.brake` | `coast` | `coast`, `brake` |

**Switch Freq** is the H-bridge's PWM switching frequency — 20 kHz is above the audible range and
every DRV8833/TB6612-class driver handles it fine.

**At Zero** decides what happens at zero command: `coast` (both H-bridge inputs low, motor spins
freely) or `brake` (both high, resisting motion).

## Servo

| Setting | Key | Default | Range |
|---|---|---|---|
| Min (µs) | `servo.min_us` | `1000` | 500–1500 |
| Max (µs) | `servo.max_us` | `2000` | 1500–2500 |
| Invert | `servo.invert` | `normal` | `normal`, `reversed` |
| Trim (µs) | `servo.trim_us` | `0` | −250–250 |

There is nothing to switch on. The firmware drives the servo when **Drive Mode** is `car` and
detaches the pin when it is `skid`, so the servo relaxes and draws no holding current on a vehicle
that has none. While driven it follows the mixer's steering output, with `steer_src` and
`steer_ratio` already applied.

**Min** and **Max** are the ends of the pulse range, so they bound the travel the linkage can
reach. **Invert** and **Trim** describe the linkage rather than the input. Invert mirrors travel
about the centre of Min/Max; Trim then shifts the centre. Set Invert if the wheels turn the wrong
way, then use Trim to bring them straight.

To move the servo on the bench with no receiver bound, set `rx.source = sim` with Drive Mode on
`car`: the simulated channels sweep the steering end to end through the real mixer.

There is no arm gate on the servo: steering keeps working whether or not the vehicle is armed. If
the link drops the wheels hold where they were and the motors fail to neutral, so the vehicle
coasts to a stop along the curve it was already on.

## Telemetry values

Read-only values the board reports. These are displayed, never set.

| Group | Values |
|---|---|
| System | Uptime, Clock (MHz), Free RAM (kB), Temp (°C), VDD (V), Fault, Loop (Hz), Worst Pass (µs) |
| RC Channels | CH1–CH16, in µs |
| RC Link | Link, LQ (%), RSSI (dBm), Rate (Hz), Errors, RF Rate (Hz), TX Power (mW) |
| Drive | Left, Right and Steer output, in µs |
| Motor 0 | Output (µs), Armed |
| Motor 1 | Output (µs), Armed |

**Fault** reads `None` on a healthy board. **Worst Pass** is the longest single loop iteration
seen, which is the number that matters if control ever feels laggy.
