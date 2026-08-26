!!! danger "Set Type to `brushed`, never `brushless`, with an H-bridge wired"

    An ESC's idle command is a 1500 µs pulse in a 5000 µs frame. Into an H-bridge that is a 30%
    duty cycle, so both motors run at a third throttle with no receiver and no arming. Set `Type`
    to `brushed` on the Configuration page and press **Save to flash** before connecting the drive
    pack.

    `Type` ships as `none`, which detaches the pin entirely, so a board you have not configured
    drives nothing — but `brushless` on an H-bridge build is the one setting that turns a motor on
    its own.
