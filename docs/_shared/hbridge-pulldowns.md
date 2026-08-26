!!! warning "Fit pulldowns on the H-bridge inputs"

    Put a 10k resistor from each H-bridge input to ground — four in total for two motors.

    The STM32's pins are floating whenever the firmware is not driving them: during a firmware
    update, between pressing NRST and the firmware booting, and from the moment the pack is
    connected until the board has started. An H-bridge reads a floating input as undefined, so
    without pulldowns the motor can run during any of those windows. With them, floating means
    off. Check whether your module already has them before adding your own.
