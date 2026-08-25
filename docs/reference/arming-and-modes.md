# Arming and modes

Arming is the safety interlock between your handset and the motors. Disarmed, the tracks cannot
turn no matter what the sticks do.

## The arm switch

| Setting | Default | What it is |
|---|---|---|
| `drive.arm_src` | `ch5` | Which channel the arming switch is on. `none` disables arming. |
| `drive.arm_min` | 1700 µs | Bottom of the armed band |
| `drive.arm_max` | 2000 µs | Top of the armed band |

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

## Motor modes

`motor0.mode` and `motor1.mode` control where each motor's output comes from:

| Mode | Behaviour |
|---|---|
| `off` | The pin is detached and held low. Nothing is driven. |
| `input` | Normal driving. The motor follows its Source — the drive mixer by default. **This is the default.** |
| `armed` | Live, but following the manual `throttle_us` value rather than the sticks. |

**A board that has never been configured drives nothing**, because `Type` defaults to `none` and
an output with no type detaches its pin whatever the mode says. The firmware cannot know what is
wired there: neutral is a stop command to an ESC, but an H-bridge on a brushless-configured output
reads that same pulse as 30% duty and runs the motor. Choosing `Type` is the single step that
brings the output to life, and it starts the arm hold like any other transition out of `off`.

Once saved, settings are restored at boot, so a configured board starts driving at power-on as
usual and its ESC arms once, there — the `none` default only affects a board that has never been
set up, or one whose settings were reset by a firmware update.

`armed` is a bench-testing tool: it will spin a motor from a value typed into the app, so keep
the wheels off the ground when using it.
