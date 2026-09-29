# UTF-9-samplified Drum Computer  

This project is based on the [bleep drum](https://bleeplabs.com/product/the-bleep-drum/) design by Dr. Bleep.

In 2025, **Felix Wiegeand (wgdmodular)** picked it up and released this **utf-9-samplified drum machine clone** with nice upgrades that make it perfect for starters and pros.

We (Modulove) put these into the hands of **beginners at DIY synthesizer solder workshops in 2025**.

- Few dozens were built.
- Every single one worked :)
- All were **hand-soldered by beginners (even kids!)**.

It turned out to be a **perfect entry-level project**.

The new firmware adds **Additonal functionality, fixes, and performance tricks** that came out of those builds and feedback from users.

---

## Firmware Updates

**Stable firmware (recommended):** 
Fixes the **button confusion issue**, fixed bad button behavior, small optimizations.
[https://dl.modulove.de/module/utf9/](https://dl.modulove.de/module/utf9/)

**Beta firmware (optional):**
Adds the new features: save/load slots, MIDI-to-CV clock, and more.
[https://dl.modulove.de/module/utf9/beta.html](https://dl.modulove.de/module/utf9/beta.html) 

Flash tool & instructions: [https://dl.modulove.de/module/utf9/](https://dl.modulove.de/module/utf9/)

---

## Supported boards

The firmware builds from a single sketch for three targets:

| Board | Notes |
|---|---|
| Arduino Nano | ATmega328P, new bootloader (115200 baud) |
| Arduino Nano (Old Bootloader) | ATmega328P, 57600 baud |
| LGT8F328P | Nano clone, built at `clock_div=2` so the chip runs at 16 MHz |

The LGT8F328P is an AVR-compatible clone whose internal RC oscillator runs at
32 MHz. The build divides it to 16 MHz so every timing in the sketch matches
the Nano, and the sample clock is derived from `F_CPU` rather than hardcoded,
so it lands on 9803.9 Hz on both. The LGT8F has no real EEPROM - the core
emulates it in flash, where writing one byte erases and rewrites a 1 KB page -
so pattern saves are staged and written as a single burst there.

None of this costs the Nano anything: the ATmega builds are byte-for-byte the
same size as before the dual-platform change.

**The Standard drumkit is Nano-only.** The LGT8F bootloader reserves 3 KB,
leaving 29696 bytes against the Nano's 30720, and Standard's sample set builds
to 30658. The other five kits all fit.

---

## Documentation

Docs of the **new features** here:

[docs/README.md](./docs/README.md)

The BOM is the docs/ folder as well.
[docs/BOM.md](./docs/BOM.md)

Find the ibom here:
[https://dl.modulove.de/module/utf9/ibom.html](https://dl.modulove.de/module/utf9/ibom.html)

---

## Links

The original Bleep Drum 
[https://bleeplabs.com/product/the-bleep-drum/](https://bleeplabs.com/product/the-bleep-drum/).

Old Bleep Drum User Guide & Hacking Info
[https://bleeplabs.com/bleep-drum-user-guide/](https://bleeplabs.com/bleep-drum-user-guide/).

---

## 💜 Thank You


Thanks for building with us
and huge thank yoouu to Felix from WGD Modular & John-Mike Reed aka Dr. Bleep who has been designing the initial Bleep Drum.

Linus & the Modulove workshop team
