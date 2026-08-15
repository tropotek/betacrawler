# Tuning

Once it drives, these are the settings that change how it drives. All of them are on the
**Controller** page unless noted, and all apply the instant you change them — press **Save to
flash** when you are happy.

## Speed and steering limits

Three independent caps, each a percentage of full authority, all defaulting to 100:

| Setting | Range | What it does |
|---|---|---|
| Forward Ratio | 0–100% | Caps forward speed |
| Reverse Ratio | 0–100% | Caps reverse speed |
| Steer Ratio | 0–100% | Caps how much steering can differ the two tracks |

They are deliberately independent. Capping forward speed leaves your pivot-on-the-spot alone,
because a pivot happens at zero throttle. Capping steering leaves straight-line speed alone.

A heavy chassis with fast motors is usually much easier to drive with Forward and Reverse pulled
down to 60–70% and Steer lower still.

## Deadband

**Channel Deadband** (`rx.deadband_us`, 0–200 µs, default 0) ignores small stick movements around
centre.

Raise it if the vehicle creeps when the sticks are centred and released. Start around 10–20 µs
and go up only as far as you need — deadband is dead stick travel, so more is not better.

## ESC frame rate

**PWM Rate** (`esc0.rate` / `esc1.rate`) sets how often each ESC is sent a new command: 50, 100,
200 or 400 Hz. The default is 50 Hz, which every analog ESC accepts.

A BLHeli-S ESC handles 400 Hz and cuts up to 20 ms of delay between stick and motor. Raise it if
the vehicle feels sluggish to respond. Set both ESCs the same.

## ESC calibration

**Min** and **Max** (`esc0.min_us` / `esc0.max_us`, defaults 1000 and 2000 µs) are the endpoints
the ESC was calibrated to. Change them only to match an ESC that expects a different range. The
two bounds cannot cross.

!!! warning "Do not use Min and Max to limit speed"

    It is tempting, and it does not do what you want. The calibration range also sets where
    neutral sits, so narrowing it moves your stop point and the vehicle will creep or refuse to
    reverse.

    Use Forward Ratio and Reverse Ratio instead. That is what they are for.

## Telemetry rate

**UI Telemetry Rate** (`tlm.rate`, 1–50 Hz, default 10) on the **Configuration** page controls
how often the board pushes live values to the app. It affects the display only, never how the
vehicle drives.
