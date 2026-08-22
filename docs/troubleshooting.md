# Troubleshooting

Symptoms in roughly the order you are likely to hit them.

## The app will not open, or says the browser is unsupported

The configurator needs a Chromium-based browser — Chrome, Edge, Brave or Opera. Firefox and
Safari have no Web Serial API and cannot talk to a board at all.

If you are running your own copy of the app rather than the hosted one, it also has to be served
from `localhost` or over HTTPS. A page served from a LAN address over plain HTTP loads, but the
browser withholds the USB APIs and **Connect** does nothing useful.

## The board does not appear in the browser's device picker

Most often the cable. Plenty of USB cables carry power only, and a charge-only cable makes the
board look dead while the LED blinks away happily.

Check the LED first: if it is blinking once a second, the board is fine and the problem is
between it and your computer. Try another cable, then another port.

The board shows up as an STMicroelectronics virtual COM port — `/dev/ttyACM0` on Linux, a `COM`
port on Windows. If it is listed but connecting fails, something else probably has the port open:
a serial monitor, or another tab running the app.

## The board went dark after its first flash and never appears

You flashed the wrong chip's image. An F411 build on an F401 hard-faults before USB comes up, so
the board neither enumerates nor blinks — see
[Which Black Pill](build/what-you-need.md#which-black-pill).

Read the marking on the chip, then flash the matching build. You do not need the ST-Link back for
this: hold **BOOT0**, tap **NRST**, release **BOOT0**, and flash the right image from the
**Firmware** page.

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
that state cannot be told apart from any other. Only have the one you are flashing plugged in, and
pick the image for your own chip yourself — the app cannot recommend one for a board it has not
spoken to.

## A board in bootloader mode is invisible on Windows

Windows binds it to ST's own DfuSe driver, which a browser cannot use. Run
[Zadig](https://zadig.akeo.ie/) once against the `STM32 BOOTLOADER` device and replace that driver
with **WinUSB**. Linux and macOS need nothing.

## My receiver is not ELRS or Crossfire

It will not work. The firmware speaks CRSF only, in those two flavours. Anything else — PWM, PPM,
SBUS, IBUS — means modifying the firmware yourself.
