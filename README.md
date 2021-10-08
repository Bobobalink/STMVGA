# STMVGA
Who says you can't bitbang VGA with 8k of RAM?

This is using an STM32F0 series microprocessor (48MHz max, 8kB of RAM, 64kB of flash) to generate a VGA signal at a stunning 80x60 resolution (the biggest frame buffer I could fit in RAM).
It does this by linking together three timers and DMA, so we actually have most of the CPU totally free to generate images! The DMA and timers are actually able to drive at 400x600, but there isn't enough RAM for a frame at that resolution. See `src/main.c` for more information.
