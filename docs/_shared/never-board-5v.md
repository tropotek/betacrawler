!!! warning "Never power a motor driver from the board's 5 V pin"

    A motor under load — and a stalled one especially — draws far more current than the board's
    5 V rail can supply. The board browns out, USB drops, and the app shows a disconnect, so it
    looks like a software or cable problem rather than the electrical one it is.

    Motor drivers take their power from the pack, directly or through a distribution board. The
    board's own 5 V comes from USB or from a BEC.

    If your ESC has its own BEC lead — the red wire on the servo connector — check whether
    anything else is already feeding the board's 5 V before connecting it. Two supplies driving
    the same rail is its own problem.
