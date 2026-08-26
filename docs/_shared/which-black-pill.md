## Which Black Pill

WeAct sells the same board with either an **STM32F411CE** or an **STM32F401CE** on it. Betacrawler
supports both, and ships a firmware image for each. The pinout, the wiring and every setting on
this page are identical — the F401 is the smaller chip (96 KB of RAM at 84 MHz against the F411's
128 KB at 100 MHz), and nothing here needs the difference.

What you cannot do is flash one chip's image onto the other. The two have different memory maps,
and an F411 image on an F401 hard-faults before USB even comes up: the board goes dark and never
appears to your computer at all.

So check which one you have before the first flash. The chip's own marking is the only reliable
answer — read the top line on the square chip in the middle of the board, `STM32F411CEU6` or
`STM32F401CEU6`, with a magnifier if you need one. Boards sold as F411 that turn out to be
populated with an F401 are common enough to be worth ruling out; the silkscreen and the listing
are not evidence.

Once the board is running Betacrawler, the **Help** page reports which chip its firmware was built
for, and the **Firmware** page offers the matching image by default.
