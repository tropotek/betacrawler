!!! warning "The module's SLEEP pin must be jumpered to VCC"

    Some DRV8833 breakouts label this pin `SLEEP`, others truncate it on their own printed pinout
    table to something like `EEP` — same pin either way. Powered but with SLEEP left floating, the
    module's power LED lights but nothing drives OUT1-4, which is easy to mistake for a wiring
    fault elsewhere.
