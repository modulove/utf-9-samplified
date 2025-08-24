# UTF-9 Drum Computer — Enhanced Features & Documentation  

The features of the UTF-9 drum computer firmware

---

## ✨ Improvements in this Firmware  (code)

- **Data type optimization** → leaner and faster code  
- **Simplified structure** → easier to read & hack  
- **Save/Load system** → intuitive and live-ready  

---

## 💾 Save / Load System  

```
SHIFT + [1-4]          → LOAD pattern from slot
SHIFT + REC + [1-4]    → SAVE pattern to slot
```


---

## 🕹️ Complete Control Summary  

| Action | Keys |
|--------|------|
| **Trigger sounds** | Buttons 1-4 |
| **Load pattern** | SHIFT + Button 1-4 |
| **Save pattern** | SHIFT + REC → Button 1-4 |
| **Switch banks** | SHIFT + TAP + Button 1-4 |
| **Reverse mode** | SHIFT + PLAY |
| **MIDI channel select (on boot)** | Hold binary button combo |

---

## 🎹 MIDI Channel Selection  

On boot, hold button combos to select MIDI channel **1–16**.  
Channel is saved to **EEPROM** and remembered!  

---

## ⏱️ MIDI Clock → Modular Converter  

```
Sequencer Playing → Pin 12 = Internal Clock
Sequencer Stopped → Pin 12 = MIDI Clock passthrough
```

So your drum computer doubles as a **MIDI-to-CV clock bridge** when idle!  

### Config Options (in code top)  
```cpp
#define MIDI_CLOCK_DIVIDER 6  // Clock division
#define MIDI_AUTO_START 0     // Auto start/stop with MIDI
```

- Clock divisions:  
  ```
  1  = 24 ppqn (fastest)
  2  = 12 ppqn
  4  =  6 ppqn (16ths)
  6  =  4 ppqn (8ths) [default]
  12 =  2 ppqn (quarters)
  24 =  1 ppqn (halves)
  48 =  0.5 ppqn (wholes)
  ```

- Auto Start:  
  ```
  0xFA → Start
  0xFC → Stop
  0xFB → Continue
  ```

---

## 🔑 Key Features  

✔️ 4-Track step sequencer  
✔️ Reverse mode
✔️ MIDI channel select via buttons on boot
✔️ Modular-ready clock output  (TRS-Midi input)
✔️ Clock generator
✔️ **Proven beginner-friendly DIY hardware** 

---
