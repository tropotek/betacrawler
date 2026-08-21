# Troubleshooting

Symptoms in roughly the order you are likely to hit them.

## The port does not appear in the dropdown

Most often the cable. Plenty of USB cables carry power only, and a charge-only cable makes the
board look dead while the LED blinks away happily.

Check the LED first: if it is blinking once a second, the board is fine and the problem is
between it and your computer. Try another cable, then another port.

A board running Betacrawler is labelled **(STM32)** in the dropdown.

## The board connects, then drops out when the motors run

The ESCs are being powered from the board's 5V pin. Under load they pull far more current than
that rail can supply, the board browns out, and USB drops.

It looks like a software or cable fault, which is what makes it confusing. It is not — run the
ESCs off the power distribution board's battery pads, and keep a common ground with the board.

## No channels move on the Controller page

Work through these in order:

1. **The receiver pad is still in PWM mode.** This is the usual answer. A receiver's pads ship as
   PWM servo outputs; one has to be reassigned to CRSF in the receiver's own menu. Until then
   nothing reaches the board at all, with no error to say so.
2. **The receiver is not bound** to your handset.
3. **The protocol does not match** — check `rx.protocol` is `elrs` or `crossfire` to suit your
   receiver.
4. **TX and RX are the wrong way round.** The receiver's *TX* pad goes to the board's PB7
   (not PA10 &mdash; see the wiring guide for why), and its *RX* pad to PA9.

## Channels move but the tracks do not

The vehicle is not armed. Check on the **Modes** page that the arm channel's live marker sits
inside the highlighted band, and that `tank_drive.arm_src` names the channel your switch is
actually on.

If the switch looks right, centre the throttle and wait two seconds — arming also requires the
throttle to have been at neutral for that long before it takes effect.

## One track runs backwards

Swap any two of the three motor wires on that ESC, or reverse that motor's direction in BLHeli
Configurator. Either works.

## The two tracks are swapped left-for-right

No need to rewire. Swap `esc0.src` and `esc1.src` between `drive_left` and `drive_right`.

## It will not reverse

The ESC is not in bidirectional mode. Set it in BLHeli Configurator — the firmware already
expects bidirectional and cannot make an ESC reverse that is not configured for it.

## It creeps with the sticks centred

Raise `rx.deadband_us`. Start around 10–20 µs.

If it creeps in one direction only and a lot of deadband is needed, the ESC's calibration is
likely off-centre; recheck **Min** and **Max**.

## Settings vanish after a power cycle

They were never saved. Changes apply to the running board immediately but live in RAM until you
press **Save to flash**.

## The board is unresponsive and will not appear for a firmware update

Reach the built-in bootloader by hand: hold **BOOT0**, tap **NRST**, release **BOOT0**. The
**Firmware** page stays available even with nothing connected, which is exactly when you need it.

One thing to know: every STM32F4 in bootloader mode identifies itself the same way, so a board in
that state cannot be told apart from any other. Only have the one you are flashing plugged in.

## My receiver is not ELRS or Crossfire

It will not work. The firmware speaks CRSF only, in those two flavours. Anything else — PWM, PPM,
SBUS, IBUS — means modifying the firmware yourself.
