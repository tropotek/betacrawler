# Arming and modes

Arming is the safety interlock between your handset and the motors. Disarmed, the tracks cannot
turn no matter what the sticks do.

## The arm switch

| Setting | Default | What it is |
|---|---|---|
| `tank_drive.arm_src` | `ch5` | Which channel the arming switch is on. `none` disables arming. |
| `tank_drive.arm_min` | 1700 µs | Bottom of the armed band |
| `tank_drive.arm_max` | 2000 µs | Top of the armed band |

The vehicle is armed while that channel sits **between** Arm Min and Arm Max. The defaults
describe a two-position switch flipped up, which is where a switch usually sits at 1700–2000 µs.

Set this on the **Modes** page, where the band is drawn as a slider with the channel's live
position marked against it — much easier than guessing microsecond values.

## What disarmed does

Both ESC outputs are held at neutral. Stick movement is read and displayed as normal, but it goes
nowhere.

## The link-loss clamp

The same neutral clamp applies whenever the radio link goes stale, independently of the arm
switch. If frames stop arriving for longer than the protocol's timeout
(`elrs.timeout_ms`, default 200 ms, or `crossfire.timeout_ms`, default 1000 ms), the outputs go
to neutral and stay there until the link comes back.

You cannot turn this off. Walking out of range stops the vehicle rather than leaving it running.

## Arm-hold

Arming does not take effect the instant the switch flips. The firmware also requires the throttle
to have been sitting at neutral for **two seconds** first.

This is what stops a vehicle lurching away because you armed with the throttle stick already
pushed forward. If you arm and nothing happens, centre the throttle and wait a couple of seconds.

## ESC modes

`esc0.mode` and `esc1.mode` control where each ESC's output comes from:

| Mode | Behaviour |
|---|---|
| `input` | Normal. The ESC follows its Source — the tank mixer by default. **This is the default.** |
| `armed` | The ESC is live but follows the manual `throttle_us` value rather than the sticks. |
| `off` | The ESC is not driven at all. |

Leave both on `input` for normal driving. The other two are bench-testing tools — `armed` in
particular will spin a motor from a value typed into the app, so keep the tracks off the ground
when using it.
