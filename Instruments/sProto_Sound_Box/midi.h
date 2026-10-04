/* MIDI notes:

  - 60 triggers Pad 1 — tin whistle
  - 62 triggers Pad 2 — kalimba
  - 64 triggers Pad 3 — spring
  - 65 triggers Pad 4 — glockenspiel

  MIDI CC mappings:

    CC    Control
  ━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    8    Dial A bank 0 — whistle pitch
  ─────  ─────────────────────────────────────────
    9    Dial B bank 0 — kalimba pitch
  ─────  ─────────────────────────────────────────
    10    Dial C bank 0 — spring strikes
  ─────  ─────────────────────────────────────────
    11    Shifted Dial A — BBD delay time
  ─────  ─────────────────────────────────────────
    12    Shifted Dial B — BBD level and feedback
  ─────  ─────────────────────────────────────────
    13    Shifted Dial C — reverb mix
*/

#ifndef SPROTO_SOUND_BOX_MIDI_H
#define SPROTO_SOUND_BOX_MIDI_H

#include "MIDI16.h"

// GPIO 42 = RX, GPIO 47 = TX on the sProto ESP32-S3 hardware.
MIDI16 midi(42, 47);

const byte instrumentMidiNotes[4] = {60, 62, 64, 65};
bool midiInstrumentHeld[4] = {false, false, false, false};

bool isMidiInstrumentHeld(uint8_t instrument) {
  return instrument < 4 && midiInstrumentHeld[instrument];
}

int8_t instrumentForMidiNote(byte pitch) {
  for (uint8_t instrument = 0; instrument < 4; instrument++) {
    if (instrumentMidiNotes[instrument] == pitch) return instrument;
  }
  return -1;
}

void triggerMidiInstrument(uint8_t instrument, byte velocity) {
  // Velocity 1 is 30% level and velocity 127 is 100% level.
  const float velocityLevel = 0.3f + ((velocity - 1) * (0.7f / 126.0f));
  setInstrumentMaxLevel(instrument, velocityLevel);
  if (instrument == 0) triggerWhistle();
  if (instrument == 1) triggerKalimba();
  if (instrument == 2) triggerSpring();
  if (instrument == 3) triggerGlockenspiel();
}

void releaseMidiInstrument(uint8_t instrument) {
  if (instrument == 0) releaseWhistle();
  if (instrument == 1) releaseKalimba();
  if (instrument == 2) releaseSpring();
  if (instrument == 3) releaseGlockenspiel();
}

void handleMidiNoteOff(byte pitch) {
  int8_t instrument = instrumentForMidiNote(pitch);
  if (instrument < 0) return;
  midiInstrumentHeld[instrument] = false;
  if (!isPadHeld(instrument)) releaseMidiInstrument(instrument);
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {
    handleMidiNoteOff(pitch);
    return;
  }
  int8_t instrument = instrumentForMidiNote(pitch);
  if (instrument < 0) return;
  midiInstrumentHeld[instrument] = true;
  triggerMidiInstrument(instrument, velocity);
}

void handleMidiCC(byte cc, byte value) {
  uint8_t dial;
  uint8_t bank;
  switch (cc) {
    case 8: dial = 0; bank = 0; break; // Dial A: whistle pitch
    case 9: dial = 1; bank = 0; break; // Dial B: kalimba pitch
    case 10: dial = 2; bank = 0; break; // Dial C: spring strikes
    case 11: dial = 0; bank = 1; break; // Shift+A: delay time
    case 12: dial = 1; bank = 1; break; // Shift+B: delay level/feedback
    case 13: dial = 2; bank = 1; break; // Shift+C: reverb mix
    default: return;
  }

  const int potValue = (value * 1023 + 63) / 127.0f;
  updateDialControl(dial, bank, potValue);
  syncDialBankValue(dial, bank, potValue);
}

void initMidiInput() {
  #if IS_ESP32()
    // Capture complete UART messages reliably while loop() services touch UI.
    midi.beginClockTask(1, 5);
  #endif
}

void readMidi() {
  uint8_t status;
  while ((status = midi.read()) != 0) {
    if (status == MIDI16::noteOn) {
      handleMidiNoteOn(midi.getData1(), midi.getData2());
    } else if (status == MIDI16::noteOff) {
      handleMidiNoteOff(midi.getData1());
    } else if (status == MIDI16::controlChange) {
      handleMidiCC(midi.getData1(), midi.getData2());
    }
  }
}

#endif