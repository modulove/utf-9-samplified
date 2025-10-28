/*
   Original: The Bleep Drum
   By John-Mike Reed aka Dr. Bleep
   https://bleeplabs.com/product/the-bleep-drum/

   Updated version for 2025 WGD Modular UTF-9 rerelease
   OPTIMIZED VERSION with EEPROM Save/Load
   
   == MIDI CLOCK TO MODULAR ==
   The clock output (pin 12) has dual functionality:
   - When playing (play=1): Outputs internal sequencer clock
   - When stopped (play=0): Converts incoming MIDI clock to modular clock
   
   This allows the Bleep Drum to act as a MIDI-to-CV clock converter
   when not running its internal sequencer. Perfect for syncing
   your modular system to a DAW or other MIDI clock source.
   
   Clock divider can be set in code (MIDI_CLOCK_DIVIDER):
   - 1 = 24ppqn (every MIDI clock tick)
   - 2 = 12ppqn (every 2nd tick) 
   - 3 = 8th note triplets (8ppqn)
   - 4 = 16th notes (6ppqn)
   - 6 = 8th notes (4ppqn) [DEFAULT]
   - 8 = 8th note dots (3ppqn)
   - 12 = Quarter notes (2ppqn)
   - 24 = Half notes (1ppqn)
   - 48 = Whole notes (0.5ppqn)
   
   MIDI Start/Stop can optionally control sequencer:
   Set MIDI_AUTO_START to 1 in code to enable auto start/stop
   
   == MIDI CHANNEL SELECTION (on boot) ==
   Hold buttons during power-on to select MIDI channel:
   No buttons = Channel 1
   Button 1 = Channel 2
   Button 2 = Channel 3
   Button 1+2 = Channel 4
   Button 3 = Channel 5
   Button 1+3 = Channel 6
   Button 2+3 = Channel 7
   Button 1+2+3 = Channel 8
   Button 4 = Channel 9
   Button 1+4 = Channel 10
   Button 2+4 = Channel 11
   Button 1+2+4 = Channel 12
   Button 3+4 = Channel 13
   Button 1+3+4 = Channel 14
   Button 2+3+4 = Channel 15
   Button 1+2+3+4 = Channel 16
   LED flashes indicate selected channel
   
   == CONTROLS ==
   Buttons 1-4: Trigger sounds (when SHIFT not held)
   SHIFT + Button 1-4: Load pattern from slot 1-4 into current bank
   SHIFT + REC then Button 1-4: Save current bank to slot 1-4
   SHIFT + TAP + Button 1-4: Switch to bank 1-4
   SHIFT + TAP (hold): Adjust tempo with pot
   SHIFT + PLAY: Toggle reverse mode
   SHIFT + TAP (long hold): Toggle metronome on/off
   
   == SAVE/LOAD SYSTEM ==
   - Each slot stores ONE 32-step pattern
   - Save the current bank's pattern to any slot
   - Load any slot into the current bank
   - Build tracks by saving variations across slots
   - Metronome always OFF in saved patterns
   - Global tempo (not saved per pattern)
   - Auto-loads last saved slot on boot
   
   == WORKFLOW EXAMPLE ==
   1. Create beat in Bank 1, save to Slot 1
   2. Modify it, save variation to Slot 2
   3. Switch to Bank 2, load Slot 1, modify, save to Slot 3
   4. Live: Load slots 1-4 into any bank as needed
   
   == MIDI ==
   Notes 60, 62, 64, 65: Trigger sounds
   Notes 72, 74, 76, 77: Select banks 1-4
   Note 67: Play/Stop
   Note 69: Toggle reverse
   Note 70: Toggle noise mode
   CC 70-73: Control parameters
   MIDI Clock: Converted to modular clock when stopped
*/

#include <MIDI.h>
#include <EEPROM.h>
MIDI_CREATE_DEFAULT_INSTANCE();
#include <avr/pgmspace.h>
#include "samples.h"
#include <SPI.h>
#include <Bounce2.h>

// EEPROM Memory Map (1024 bytes total)
// Bytes 0-1:   Magic number (0xBE25)
// Bytes 2-15:  Settings:
//   Byte 2: Last saved slot (0-3)
//   Byte 3: Settings byte (play, click_en, playmode, noise_mode)
//   Byte 4: Current bank (0, 31, 63, or 95)
//   Byte 5: MIDI channel (1-16)
//   Bytes 6-15: Reserved for future use
// Bytes 16-271:  Save Slot 1 - One 32-step pattern
// Bytes 272-527: Save Slot 2 - One 32-step pattern
// Bytes 528-783: Save Slot 3 - One 32-step pattern
// Bytes 784-1023: Save Slot 4 - One 32-step pattern
//
// Each slot stores one 32-step pattern:
// - 16 bytes: Packed sequences (32 steps × 4 sequences, 2 steps per byte)
// - 32 bytes: B1 frequency values (compressed from int to byte)
// - 32 bytes: B2 frequency values (compressed from int to byte)
// - 176 bytes: Reserved for future expansion

#define EEPROM_MAGIC 0xBE25  // 2 bytes
#define EEPROM_SETTINGS_ADDR 2  // Start of settings (uses bytes 2-15)
#define EEPROM_BANK_START 16    // Start of bank storage
#define EEPROM_BANK_SIZE 256    // 256 bytes per save slot (4 slots total)

// Buttons
#define BTN1_PIN 4
#define BTN2_PIN 2
#define BTN3_PIN 7
#define BTN4_PIN 19

Bounce debouncerRed = Bounce();
Bounce debouncerGreen = Bounce();
Bounce debouncerBlue = Bounce();
Bounce debouncerYellow = Bounce();

// Control pins
#define play_pin  3
#define rec_pin   8
#define tap_pin   18
#define shift_pin 17

// MIDI Clock Settings
#define MIDI_CLOCK_DIVIDER 6    // 24 PPQN to 4 PPQN (6:1 ratio)
#define MIDI_AUTO_START 1       // 1=Auto start/stop with MIDI, 0=Manual only

uint32_t cm;
const unsigned long dds_tune = 4294967296 / 9800;  // 2^32 / measured dds freq

// Optimized variables - keeping audio-critical ones as int
int sample_holder1;  // Keep as int for audio quality
uint16_t eee;
byte erase_latch, shift, banko = 0;
uint16_t pot1, pot2, pot3 = 200, pot4 = 200;
int8_t kick_sample, snare_sample, hat_sample, noise_sample, bass_sample;
int8_t B2_freq_sample, B1_freq_sample;
uint32_t accumulator, accumulator2, accumulator3, accumulator4, accumulator5;
uint32_t accu_freq_1, accu_freq_2;

// Sequences - keep original size for patterns
byte B2_sequence[128];
byte B3_sequence[128];
byte B1_sequence[128];
byte B4_sequence[128];
int B2_freq_sequence[128];  // KEEP AS INT for audio quality
int B1_freq_sequence[128];  // KEEP AS INT for audio quality

int kf, pf, kfe;  // KEEP AS INT for proper frequency
uint16_t index, index2, index3, index4, index5;
uint16_t index_freq_1, index_freq_2;
uint16_t indexr, index2r, index3r, index4r, index2vr, index4vr;
byte noise_mode = 1;
byte loopstep = 0, loopstepf = 0;
byte recordbutton, prevrecordbutton, record, prevloopstep;
byte revbutton, prevrevbutton;
uint32_t prev;
byte playmode = 1, play = 0;
byte playbutton, pplaybutton;
byte B4_trigger, B4_latch, B4_loop_trigger, B1_trigger, B1_latch;
byte B1_loop_trigger, B4_seq_trigger, B3_seq_trigger;
byte B1_seq_trigger, B3_latch, B2_trigger, B2_loop_trigger, B3_loop_trigger;
byte B2_latch, B3_trigger, B2_seq_trigger;
byte B2_seq_latch, B1_seq_latch, precordbutton;
byte recordmode = 1;
byte tapbutton, ptapbutton;
uint16_t taptempo = 1000;
uint16_t ratepot;
byte trigger_out_latch;
byte button1, button2, button3, button4, tapb;
byte pbutton1, pbutton2, pbutton3, pbutton4, ptapb;
byte tiggertempo, midistep, pmidistep, miditempo;
byte bf1, bf2, bf3, bf4, bft;
uint16_t midicc3 = 128, midicc4 = 157;
uint16_t midicc1, midicc2;
byte midi_note_check;
byte prevshift, shift_latch;
byte t;
uint16_t tapbank[2];  // Reduced from 4 to 2
uint16_t recordoffsettimer, offsetamount, taptempof;
int click_pitch;  // Use int for proper pitch values
byte click_amp, click_wait;
int sample_out_temp;  // Keep as int for proper audio mixing
byte sample_out;
uint32_t dds_time;
byte bft_latch;
uint16_t raw1, raw2;
uint32_t log1, log2;
byte click_play, click_en = 1;
uint16_t shift_time;
byte shift_time_latch;

// MIDI Clock to Modular Clock Configuration
// MIDI sends 24 clocks per quarter note (24ppqn)
// Change this value to set clock division when sequencer is stopped:
// 1 = 24ppqn (every tick), 3 = 8ppqn (triplets), 6 = 4ppqn (8th notes)
// 12 = 2ppqn (quarter notes), 24 = 1ppqn (half notes), etc.
#define MIDI_CLOCK_DIVIDER 6  // Default: 8th notes (4 pulses per quarter note)

// Set to 1 to enable auto start/stop with MIDI Start/Stop messages
#define MIDI_AUTO_START 0  // 0=disabled, 1=enabled

// Save/Load
byte save_mode = 0;  // 0=off, 1=waiting for button
byte last_saved_slot = 0;  // Remember which slot was last saved
byte midi_channel = 1;  // MIDI channel 1-16

// MIDI Clock to Modular Clock
byte midi_clock_counter = 0;
uint32_t midi_clock_out_time = 0;
byte midi_clock_out_latch = 0;

// Function declarations
void handleSaveLoad();
void saveToSlot(byte slot);
void loadFromSlot(byte slot);
void saveSettings();
void loadSettings();
byte checkEEPROM();
void flashConfirm();
byte midi_note_on();
void BUTTONS();
void RECORD();

// ISR variables
int8_t sine_sample;  // Declare here
byte index_sine;
uint32_t acc_sine;
uint32_t trig_out_time, prevtap;

void setup() {
  cli();

  // Outputs
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(16, OUTPUT);
  digitalWrite(16, HIGH);

  // Inputs with pull-ups
  pinMode(play_pin, INPUT_PULLUP);
  pinMode(rec_pin, INPUT_PULLUP);
  pinMode(tap_pin, INPUT_PULLUP);
  pinMode(shift_pin, INPUT_PULLUP);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BTN3_PIN, INPUT_PULLUP);
  pinMode(BTN4_PIN, INPUT_PULLUP);

  debouncerGreen.attach(BTN3_PIN);
  debouncerGreen.interval(2);
  debouncerYellow.attach(BTN4_PIN);
  debouncerYellow.interval(2);
  debouncerBlue.attach(BTN2_PIN);
  debouncerBlue.interval(2);
  debouncerRed.attach(BTN1_PIN);
  debouncerRed.interval(2);

  delay(100);
  
  // MIDI Channel Selection on boot
  // Hold buttons during power-on to select MIDI channel 1-16
  // Binary encoding using 4 buttons: BTN1=1, BTN2=2, BTN3=4, BTN4=8
  // Channel = sum of pressed buttons + 1 (0 = channel 1, 15 = channel 16)
  
  midi_channel = 1;  // Default if no buttons pressed and no EEPROM data
  byte channel_select = 0;
  byte any_button_pressed = 0;
  
  if (digitalRead(BTN1_PIN) == 0) { channel_select += 1; any_button_pressed = 1; }
  if (digitalRead(BTN2_PIN) == 0) { channel_select += 2; any_button_pressed = 1; }
  if (digitalRead(BTN3_PIN) == 0) { channel_select += 4; any_button_pressed = 1; }
  if (digitalRead(BTN4_PIN) == 0) { channel_select += 8; any_button_pressed = 1; }
  
  // If buttons were pressed, set new channel
  if (any_button_pressed) {
    midi_channel = channel_select + 1;  // MIDI channels are 1-16, not 0-15
    
    // Visual feedback for channel selection
    // Quick flash = tens digit, slow flashes = ones digit
    if (midi_channel >= 10) {
      digitalWrite(13, HIGH);
      delay(300);
      digitalWrite(13, LOW);
      delay(200);
    }
    
    byte ones = midi_channel % 10;
    if (ones == 0) ones = 10;  // Show 10 flashes for 10
    
    for (byte i = 0; i < ones; i++) {
      digitalWrite(13, HIGH);
      delay(100);
      digitalWrite(13, LOW);
      delay(100);
    }
    delay(500);
  }
  // Otherwise midi_channel will be loaded from EEPROM later
  
  MIDI.begin(midi_channel);
  MIDI.turnThruOff();

  SPI.begin();
  SPI.setBitOrder(MSBFIRST);

  // Timer2 ISR
  TIMSK2 = (1 << OCIE2A);
  OCR2A = 50;
  TCCR2A = (1 << WGM21);
  TCCR2B = (1 << CS21) | (1 << CS20);
  TCCR0B = B0000001;
  TCCR1B = B0000001;

  sei();

  noise_mode = (digitalRead(shift_pin) == 0) ? 1 : 0;
  
  // Load saved data (including MIDI channel if not set by buttons)
  if (checkEEPROM()) {
    if (!any_button_pressed) {
      // No buttons pressed, load saved channel
      loadSettings();  // This loads banko position and MIDI channel
      // Re-initialize MIDI with loaded channel
      MIDI.begin(midi_channel);
      MIDI.turnThruOff();
    } else {
      // Buttons were pressed, keep the selected channel
      loadSettings();  // Load other settings
      // midi_channel already set from button presses
      saveSettings();  // Save the new channel selection
    }
    byte last_slot = EEPROM.read(EEPROM_SETTINGS_ADDR);
    if (last_slot < 4) {
      loadFromSlot(last_slot);  // Load last saved pattern into current bank
    }
  }
}

ISR(TIMER2_COMPA_vect) {
  dds_time++;
  OCR2A = 50;

  prevloopstep = loopstep;

  if (recordmode == 1 && miditempo == 0 && tiggertempo == 0) {
    if (dds_time - prev > taptempof) {
      prev = dds_time;
      if (++loopstep > 31) loopstep = 0;
    }
  }

  if (shift == 1 && bft == 1) {
    bft = 0;
    bft_latch = 0;
    tiggertempo = 0;
    t = !t;
    tapbank[t] = (dds_time - prevtap) >> 2;
    taptempo = (tapbank[0] + tapbank[1]) >> 1;
    prevtap = dds_time;
  }

  taptempof = taptempo;
  recordoffsettimer = dds_time - prev;
  //offsetamount = taptempof - (taptempof >> 2);
  offsetamount = taptempof >> 1;  // 50% threshold (divide by 2)



  if (recordoffsettimer > offsetamount) {
    loopstepf = loopstep + 1;
    if (loopstepf > 31) loopstepf = 0;
  }

  // Click pitch based on position
  if (loopstep % 4 == 0) click_pitch = 220;
  if (loopstep % 8 == 0) click_pitch = 293;
  if (loopstep == 0) click_pitch = 440;

  if (play == 1) {
    click_play = 1;
    B4_loop_trigger = B4_sequence[loopstep + banko];
    B1_loop_trigger = B1_sequence[loopstep + banko];
    B2_loop_trigger = B2_sequence[loopstep + banko];
    B3_loop_trigger = B3_sequence[loopstep + banko];
  } else {
    loopstep = 31;
    prev = 0;
    click_play = 0;
    B4_loop_trigger = B1_loop_trigger = B2_loop_trigger = B3_loop_trigger = 0;
  }

  // Step trigger out
  if (loopstep != prevloopstep) {
    digitalWrite(12, 1);
    trig_out_time = dds_time;
    trigger_out_latch = 1;
  }
  if (dds_time - trig_out_time > 500 && trigger_out_latch == 1) {
    trigger_out_latch = 0;
    digitalWrite(12, 0);
  }

  B3_seq_trigger = (loopstep != prevloopstep && B3_loop_trigger == 1);
  B2_seq_trigger = (loopstep != prevloopstep && B2_loop_trigger == 1);
  B4_seq_trigger = (loopstep != prevloopstep && B4_loop_trigger == 1);
  B1_seq_trigger = (loopstep != prevloopstep && B1_loop_trigger == 1);

  // Trigger handling
  if (B3_trigger || B3_seq_trigger) { index3 = 0; accumulator3 = 0; B3_latch = 1; }
  if (B4_trigger || B4_seq_trigger) { index4 = 0; accumulator4 = 0; B4_latch = 1; }
  if (B1_trigger) { index = 0; accumulator = 0; B1_latch = 1; }
  if (B1_seq_trigger) { index_freq_1 = 0; accu_freq_1 = 0; B1_seq_latch = 1; }
  if (B2_seq_trigger) { index_freq_2 = 0; accu_freq_2 = 0; B2_seq_latch = 1; }
  if (B2_trigger) { index2 = 0; accumulator2 = 0; B2_latch = 1; }

  if (loopstep % 4 == 0 && prevloopstep % 4 != 0) click_amp = 64;

  // Sine sample for click with proper calculation
  sine_sample = (((pgm_read_byte(&sine_table[index_sine]) - 127) * click_amp) >> 8) * click_play * click_en;

  // Sample generation
  if (playmode == 0) { // reverse
    snare_sample = pgm_read_byte(&snare_table[indexr]) - 127;
    kick_sample = pgm_read_byte(&kick_table[index2r]) - 127;
    hat_sample = pgm_read_byte(&tick_table[index3r]) - 127;
    bass_sample = pgm_read_byte(&bass_table[index4r]) - 127;
    B1_freq_sample = pgm_read_byte(&tick_table[index2vr]) - 127;
    B2_freq_sample = pgm_read_byte(&bass_table[index4vr]) - 127;
  } else {
    snare_sample = pgm_read_byte(&snare_table[index3]) - 127;
    kick_sample = pgm_read_byte(&kick_table[index4]) - 127;
    hat_sample = pgm_read_byte(&tick_table[index]) - 127;
    bass_sample = pgm_read_byte(&bass_table[index2]) - 127;
    B1_freq_sample = pgm_read_byte(&tick_table[index_freq_1]) - 127;
    B2_freq_sample = pgm_read_byte(&bass_table[index_freq_2]) - 127;
    noise_sample = pgm_read_byte(&sine_table[index5]) - 127;
  }

  // Mix samples with original algorithm for quality
  int sample_sum = snare_sample + kick_sample + hat_sample + bass_sample + B1_freq_sample + B2_freq_sample;
  
  if (noise_mode == 1) {
    byte sine_noise = ((sine_sample) | (noise_sample >> 2) * click_amp >> 2) >> 3;
    if (click_play == 0 || click_en == 0 || click_amp < 4) sine_noise = 0;

    sample_holder1 = ((sample_sum ^ (noise_sample >> 1)) >> 1) + 127;
    if (B1_latch == 0 && B2_latch == 0 && B3_latch == 0 && B4_latch == 0) {
      sample_out_temp = 127 + sine_noise;
    } else {
      sample_out_temp = sample_holder1 + sine_noise;
    }
  } else {
    sample_out_temp = ((sample_sum + sine_sample) >> 1) + 127;
  }

  // Original fold distortion for proper sound
  if (sample_out_temp > 255) sample_out_temp -= (sample_out_temp - 255) << 1;
  if (sample_out_temp < 0) sample_out_temp += sample_out_temp * -2;
  
  sample_out = sample_out_temp;

  // DAC output
  uint16_t dac_out = (0 << 15) | (1 << 14) | (1 << 13) | (1 << 12) | (sample_out << 4);
  digitalWrite(10, LOW);
  SPI.transfer(dac_out >> 8);
  SPI.transfer(dac_out & 255);
  digitalWrite(10, HIGH);

  // Click oscillator
  acc_sine += click_pitch << 2;
  index_sine = (dds_tune * acc_sine) >> (32 - 8);

  if (++click_wait > 4) {
    click_wait = 0;
    if (click_amp > 3) click_amp--;
    else click_amp = 0;
  }

  // Reverse mode indices
  if (playmode == 0) {
    indexr = (index3 - snare_length) * -1;
    index2r = (index4 - kick_length) * -1;
    index3r = (index - tick_length) * -1;
    index4r = (index2 - bass_length) * -1;
    index2vr = (index_freq_1 - tick_length) * -1;
    index4vr = (index_freq_2 - bass_length) * -1;
  }

  // Sample playback
  if (B1_latch) {
    accumulator += (midicc1 > 4) ? midicc1 : pot1;
    index = accumulator >> 6;
    if (index > tick_length) { index = 0; accumulator = 0; B1_latch = 0; }
  }

  if (B2_latch) {
    accumulator2 += (midicc2 > 4) ? midicc2 : pot2;
    index2 = accumulator2 >> 6;
    if (index2 > bass_length) { index2 = 0; accumulator2 = 0; B2_latch = 0; }
  }

  if (B3_latch) {
    accumulator3 += midicc3;
    index3 = accumulator3 >> 6;
    if (index3 > snare_length) { index3 = 0; accumulator3 = 0; B3_latch = 0; }
  }

  if (B4_latch) {
    accumulator4 += midicc4;
    index4 = accumulator4 >> 6;
    if (index4 > kick_length) { index4 = 0; accumulator4 = 0; B4_latch = 0; }
  }

  // Frequency sequences
  accu_freq_1 += kf;
  index_freq_1 = accu_freq_1 >> 6;
  if (B1_seq_trigger == 1) { kf = B1_freq_sequence[loopstep + banko]; kfe = kf; }
  if (index_freq_1 > tick_length) { kf = 0; index_freq_1 = 0; accu_freq_1 = 0; B1_seq_latch = 0; }

  accu_freq_2 += pf;
  index_freq_2 = accu_freq_2 >> 6;
  if (B2_seq_trigger == 1) pf = B2_freq_sequence[(tiggertempo ? loopstep : loopstepf) + banko];
  if (index_freq_2 > bass_length) { pf = 0; index_freq_2 = 0; accu_freq_2 = 0; B2_seq_latch = 0; }

  if (noise_mode == 1) {
    accumulator5 += pot3;
    index5 = accumulator5 >> 6;
    if (index5 > pot4) { index5 = 0; accumulator5 = 0; }
  }
}

void loop() {
  cm = millis();
  
  midi_note_check = midi_note_on();

  pbutton1 = button1; pbutton2 = button2; pbutton3 = button3; pbutton4 = button4;

  debouncerRed.update();
  debouncerBlue.update();
  debouncerGreen.update();
  debouncerYellow.update();

  button1 = debouncerRed.read();
  button2 = debouncerBlue.read();
  button3 = debouncerGreen.read();
  button4 = debouncerYellow.read();

  tapb = digitalRead(tap_pin);

  bf1 = (button1 == 0 && pbutton1 == 1);
  bf2 = (button2 == 0 && pbutton2 == 1);
  bf3 = (button3 == 0 && pbutton3 == 1);
  bf4 = (button4 == 0 && pbutton4 == 1);

  if (tapb == 0 && ptapbutton == 1 && !bft_latch) { bft = 1; bft_latch = 1; }
  else { bft = 0; bft_latch = 0; }

  // MIDI controls
  if (midi_note_check == 67) {
    play = !play;
    midi_clock_counter = 0;  // Reset MIDI clock counter
    digitalWrite(12, LOW);
  }
  if (midi_note_check == 69) playmode = !playmode;
  if (midi_note_check == 70) { shift_latch = 1; noise_mode = !noise_mode; }
  
  // MIDI bank select (consistent with button mapping)
  if (midi_note_check == 72) banko = 0;   // Bank 1
  if (midi_note_check == 74) banko = 31;  // Bank 2
  if (midi_note_check == 76) banko = 63;  // Bank 3
  if (midi_note_check == 77) banko = 95;  // Bank 4

  ptapbutton = tapb;
  pmidistep = midistep;

  BUTTONS();
  RECORD();
  handleSaveLoad();

  // Read pots
  raw1 = analogRead(0);
  log1 = (uint32_t)raw1 * raw1;
  raw2 = analogRead(1);
  log2 = (uint32_t)raw2 * raw2;

  if (noise_mode == 0) {
    pot1 = (log1 >> 11) + 2;
    pot2 = (log2 >> 11) + 42;
  } else {
    if (shift_latch == 0) {
      pot1 = (log1 >> 11) + 1;
      pot2 = (log2 >> 12) + 1;
    } else {
      pot3 = (log1 >> 6) + 1;
      pot4 = (log2 >> 8) + 1;
    }
  }
}

void RECORD() {
  pplaybutton = playbutton;
  playbutton = digitalRead(play_pin);
  if (pplaybutton == 1 && playbutton == 0 && shift == 1 && recordbutton == 1) {
    play = !play;
    midi_clock_counter = 0;
    digitalWrite(12, LOW);
  }

  precordbutton = recordbutton;
  recordbutton = digitalRead(rec_pin);
  if (recordbutton == 0 && precordbutton == 1) {
    record = !record;
    play = 1;
    erase_latch = 1;
    eee = 0;
  }
  if (recordbutton == 1 && precordbutton == 0) {
    erase_latch = 0;
  }

  if (play == 0) record = 0;

  // Hold REC + PLAY to erase
  if (recordbutton == 0 && playbutton == 0 && erase_latch == 1) {
    if (++eee >= 800) {
      eee = 0;
      erase_latch = 0;
      play = 1;
      record = 0;
      for (byte j = 0; j < 32; j++) {
        B1_sequence[j + banko] = 0;
        B2_sequence[j + banko] = 0;
        B4_sequence[j + banko] = 0;
        B3_sequence[j + banko] = 0;
      }
    }
  }

  if (record == 1) {
    // FIX: Capture loopstepf atomically to prevent race condition
    byte record_step;
    cli();  // Disable interrupts
    record_step = loopstepf;
    sei();  // Re-enable interrupts
    
    // Now use the captured value consistently
    if (B1_trigger) {
      B1_sequence[record_step + banko] = 1;
      B1_freq_sequence[record_step + banko] = pot1;
    }
    if (B2_trigger) {
      B2_sequence[record_step + banko] = 1;
      B2_freq_sequence[record_step + banko] = pot2;
    }
    if (B4_trigger) B4_sequence[record_step + banko] = 1;
    if (B3_trigger) B3_sequence[record_step + banko] = 1;
  }
}

void BUTTONS() {
  prevshift = shift;
  shift = digitalRead(shift_pin);

  if (shift == 0 && prevshift == 1) {
    shift_latch = !shift_latch;
    shift_time = 0;
    shift_time_latch = 1;
  }

  if (shift == 0 && tapb == 1 && shift_time_latch == 1) {
    if (++shift_time > 800) {
      click_en = !click_en;
      shift_time = 0;
      shift_time_latch = 0;
    }
  }

  // Bank select with SHIFT + TAP + button (doesn't conflict with load)
  if (shift == 0 && tapb == 0 && recordbutton == 1) {
    if (button1 == 0) banko = 0;   // Bank 1
    if (button2 == 0) banko = 31;  // Bank 2 
    if (button3 == 0) banko = 63;  // Bank 3
    if (button4 == 0) banko = 95;  // Bank 4
  }

  // Tempo adjustment when holding SHIFT + TAP (no buttons)
  if (shift == 0 && tapb == 0 && recordbutton == 1) {
    play = 1;
    ratepot = analogRead(1);
    taptempo = ((ratepot - 1024) * -1) << 2;
  }

  // Reverse mode with SHIFT + PLAY
  if (shift == 0 && recordbutton == 1 && save_mode == 0) {
    revbutton = digitalRead(play_pin);
    if (revbutton == 0 && prevrevbutton == 1) playmode = !playmode;
    prevrevbutton = revbutton;
  }

  // Triggers (when not holding SHIFT, and not in save mode)
  if (shift == 1 && save_mode == 0) {
    B1_trigger = (bf1 || midi_note_check == 60);
    B4_trigger = (bf4 || midi_note_check == 65);
    B2_trigger = (bf2 || midi_note_check == 62);
    B3_trigger = (bf3 || midi_note_check == 64);
  } else {
    // Clear triggers when in save mode or holding shift
    B1_trigger = 0;
    B2_trigger = 0;
    B3_trigger = 0;
    B4_trigger = 0;
  }
}

void handleSaveLoad() {
  // SHIFT + REC = Enter save mode (saves current bank pattern)
  if (shift == 0 && recordbutton == 0 && playbutton == 1) {
    save_mode = 1;
    // Visual feedback - quick flash
    play = !play;
    delay(50);
    play = !play;
  }
  
  // Exit save mode if shift released
  if (shift == 1) {
    save_mode = 0;
  }
  
  // Direct load: SHIFT + Button (no PLAY needed)
  if (shift == 0 && recordbutton == 1 && playbutton == 1) {
    if (bf1) { loadFromSlot(0); flashConfirm(); }
    if (bf2) { loadFromSlot(1); flashConfirm(); }
    if (bf3) { loadFromSlot(2); flashConfirm(); }
    if (bf4) { loadFromSlot(3); flashConfirm(); }
  }
  
  // In save mode - wait for button 1-4 to save current pattern
  if (save_mode == 1) {
    if (bf1) { saveToSlot(0); save_mode = 0; flashConfirm(); }
    if (bf2) { saveToSlot(1); save_mode = 0; flashConfirm(); }
    if (bf3) { saveToSlot(2); save_mode = 0; flashConfirm(); }
    if (bf4) { saveToSlot(3); save_mode = 0; flashConfirm(); }
  }
}

void flashConfirm() {
  byte p = play;
  for (byte i = 0; i < 3; i++) {
    play = !play;
    delay(80);
  }
  play = p;
}

void saveToSlot(byte slot) {
  if (slot > 3) return;
  
  uint16_t addr = EEPROM_BANK_START + (slot * EEPROM_BANK_SIZE);
  
  // Save only the CURRENT 32-step pattern (based on banko)
  // Pack 2 steps into 1 byte (4 bits per step for 4 sequences)
  for (byte i = 0; i < 16; i++) {  // 16 bytes for 32 steps
    byte packed = 0;
    byte idx = (i * 2) + banko;  // Use banko offset to get current bank
    
    // Pack 2 steps into 1 byte
    if (B1_sequence[idx]) packed |= 0x01;
    if (B2_sequence[idx]) packed |= 0x02;
    if (B3_sequence[idx]) packed |= 0x04;
    if (B4_sequence[idx]) packed |= 0x08;
    if (B1_sequence[idx + 1]) packed |= 0x10;
    if (B2_sequence[idx + 1]) packed |= 0x20;
    if (B3_sequence[idx + 1]) packed |= 0x40;
    if (B4_sequence[idx + 1]) packed |= 0x80;
    
    EEPROM.update(addr + i, packed);
  }
  
  // Save B1 frequency sequences for current bank (32 values)
  for (byte i = 0; i < 32; i++) {
    byte freq1 = constrain(B1_freq_sequence[i + banko] >> 2, 0, 255);
    EEPROM.update(addr + 16 + i, freq1);
  }
  
  // Save B2 frequency sequences for current bank (32 values)
  for (byte i = 0; i < 32; i++) {
    byte freq2 = constrain(B2_freq_sequence[i + banko] >> 2, 0, 255);
    EEPROM.update(addr + 48 + i, freq2);
  }
  
  // Save global settings (including current bank position)
  saveSettings();
  EEPROM.update(EEPROM_SETTINGS_ADDR, slot);  // Remember last saved slot
  last_saved_slot = slot;
}

void loadFromSlot(byte slot) {
  if (slot > 3) return;
  
  uint16_t addr = EEPROM_BANK_START + (slot * EEPROM_BANK_SIZE);
  
  // Clear only the current bank's sequences before loading
  for (byte i = 0; i < 32; i++) {
    B1_sequence[i + banko] = 0;
    B2_sequence[i + banko] = 0;
    B3_sequence[i + banko] = 0;
    B4_sequence[i + banko] = 0;
    B1_freq_sequence[i + banko] = 0;
    B2_freq_sequence[i + banko] = 0;
  }
  
  // Load the 32-step pattern into the CURRENT bank position
  for (byte i = 0; i < 16; i++) {
    byte packed = EEPROM.read(addr + i);
    byte idx = (i * 2) + banko;  // Load into current bank
    
    B1_sequence[idx] = (packed & 0x01) ? 1 : 0;
    B2_sequence[idx] = (packed & 0x02) ? 1 : 0;
    B3_sequence[idx] = (packed & 0x04) ? 1 : 0;
    B4_sequence[idx] = (packed & 0x08) ? 1 : 0;
    B1_sequence[idx + 1] = (packed & 0x10) ? 1 : 0;
    B2_sequence[idx + 1] = (packed & 0x20) ? 1 : 0;
    B3_sequence[idx + 1] = (packed & 0x40) ? 1 : 0;
    B4_sequence[idx + 1] = (packed & 0x80) ? 1 : 0;
  }
  
  // Load B1 frequency sequences into current bank
  for (byte i = 0; i < 32; i++) {
    B1_freq_sequence[i + banko] = EEPROM.read(addr + 16 + i) << 2;
  }
  
  // Load B2 frequency sequences into current bank
  for (byte i = 0; i < 32; i++) {
    B2_freq_sequence[i + banko] = EEPROM.read(addr + 48 + i) << 2;
  }
  
  // Note: Settings are loaded separately, not here
  // This allows loading a pattern without changing global settings
}

void saveSettings() {
  // Always save with metronome OFF
  byte settings = (play & 1) | (0 << 1) | ((playmode & 1) << 2) | ((noise_mode & 1) << 3);
  EEPROM.update(EEPROM_SETTINGS_ADDR + 1, settings);  // Byte 3
  EEPROM.update(EEPROM_SETTINGS_ADDR + 2, banko);     // Byte 4: Current bank
  EEPROM.update(EEPROM_SETTINGS_ADDR + 3, midi_channel); // Byte 5: MIDI channel
}

void loadSettings() {
  byte settings = EEPROM.read(EEPROM_SETTINGS_ADDR + 1);  // Byte 3
  play = settings & 1;
  // Don't load click_en - let user control it
  playmode = (settings >> 2) & 1;
  noise_mode = (settings >> 3) & 1;
  
  // Load saved bank position
  byte saved_bank = EEPROM.read(EEPROM_SETTINGS_ADDR + 2);  // Byte 4
  if (saved_bank == 0 || saved_bank == 31 || saved_bank == 63 || saved_bank == 95) {
    banko = saved_bank;
  }
  
  // Load saved MIDI channel
  byte saved_channel = EEPROM.read(EEPROM_SETTINGS_ADDR + 3);  // Byte 5
  if (saved_channel >= 1 && saved_channel <= 16) {
    midi_channel = saved_channel;
  }
}

byte checkEEPROM() {
  uint16_t magic = EEPROM.read(0) | (EEPROM.read(1) << 8);
  if (magic != EEPROM_MAGIC) {
    // Initialize EEPROM
    EEPROM.update(0, EEPROM_MAGIC & 0xFF);
    EEPROM.update(1, EEPROM_MAGIC >> 8);
    EEPROM.update(EEPROM_SETTINGS_ADDR, 0);  // Byte 2: Last slot = 0
    EEPROM.update(EEPROM_SETTINGS_ADDR + 3, 1);  // Byte 5: Default MIDI channel = 1
    saveSettings();
    // Clear all save slots
    for (uint16_t i = EEPROM_BANK_START; i < 1024; i++) {
      EEPROM.update(i, 0);
    }
    return 0;
  }
  return 1;
}

byte midi_note_on() {
  byte note = 0;
  if (MIDI.read()) {
    byte type = MIDI.getType();
    
    // Handle MIDI System Realtime messages first (they don't have channels)
    // These work regardless of selected MIDI channel
    if (type >= 0xF8) {  // System Realtime messages (0xF8-0xFF)
      if (type == 0xF8) {  // MIDI Clock tick
        if (play == 0) {  // Only process clock when sequencer is stopped
          midi_clock_counter++;
          if (midi_clock_counter >= MIDI_CLOCK_DIVIDER) {
            midi_clock_counter = 0;
            // Output clock pulse
            digitalWrite(12, HIGH);
            midi_clock_out_time = dds_time;
            midi_clock_out_latch = 1;
          }
        }
      }
      else if (type == 0xFA) {  // MIDI Start
        midi_clock_counter = 0;
        #if MIDI_AUTO_START == 1
        play = 1;  // Auto-start sequencer on MIDI Start
        #endif
      }
      else if (type == 0xFC) {  // MIDI Stop
        midi_clock_counter = 0;
        digitalWrite(12, LOW);
        #if MIDI_AUTO_START == 1
        play = 0;  // Auto-stop sequencer on MIDI Stop
        #endif
      }
      else if (type == 0xFB) {  // MIDI Continue
        // Continue from where we left off
        #if MIDI_AUTO_START == 1
        play = 1;  // Auto-continue sequencer
        #endif
      }
      return 0;  // System messages don't return notes
    }
    
    // For channel messages, check if it's on our selected channel
    if (MIDI.getChannel() != midi_channel) return 0;
    
    // Handle channel-specific messages
    if (type == 0x90) {  // Note On
      note = MIDI.getData1();
      if (MIDI.getData2() == 0) note = 0;  // Velocity 0 = Note Off
    } else if (type == 0x80) {  // Note Off
      note = 0;
    } else if (type == 0xB0) {  // Control Change
      byte cc = MIDI.getData1();
      byte val = MIDI.getData2();
      if (cc == 70) midicc1 = (val << 2) + 3;
      if (cc == 71) midicc2 = (val << 2) + 3;
      if (cc == 72) midicc3 = val << 2;
      if (cc == 73) midicc4 = val << 2;
    }
  }
  return note;
}
