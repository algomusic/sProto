#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — ESP32 Serial2 defaults. 
MIDI16 midi(42, 47);

int8_t midiOutNote[4] = {-1, -1, -1, -1};
unsigned long midiOutOffTime[4] = {0, 0, 0, 0};
float velocityAmpMod = 0.7;
float velocityFilterMod = 0.4;
const float pitchBendRange = 2.0f; // semitones above and below centre

// MIDI CC numbers for pot parameters
const byte CC_MOD_WHEEL = 1;
const byte CC_PITCH = 8;
const byte CC_MORPH = 9; // wave
const byte CC_CUTOFF = 10;
const byte CC_ENV_SHAPE = 11;
const byte CC_FILTER_ENV_SHAPE = 12;
const byte CC_CHORUS_DEPTH = 13;

void initMidiInput() {
  #if IS_ESP32()
    // The background reader owns Serial2 reception and buffers complete MIDI
    // byte streams independently of touch scanning and other loop activity.
    midi.beginClockTask(1, 5);
  #endif
}

void handleMidiNoteOff(byte pitch) {
  releaseMidiVoice(pitch);
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {handleMidiNoteOff(pitch); return;}  // vel=0 is note off
  // MIDI velocity 1 maps to the configured minimum, while 127 maps to the
  // unchanged full-level voice. Each modulation amount is independently
  // adjustable from 0.0 (disabled) to 1.0 (full velocity range).
  float velocityNormal = (velocity - 1) / 126.0f;
  float ampMod = max(0.0f, min(1.0f, velocityAmpMod));
  float filterMod = max(0.0f, min(1.0f, velocityFilterMod));
  float ampLevel = 1.0f - ampMod * (1.0f - velocityNormal);
  float cutoffFrequencyScale = 1.0f - filterMod * (1.0f - velocityNormal);
  startVoice(pitch, VOICE_MIDI, pitch, ampLevel, cutoffFrequencyScale);
}

void handleMidiPitchBend(byte lsb, byte msb) {
  int bendValue = lsb | (msb << 7); // 14-bit MIDI value; centre is 8192
  int centred = bendValue - 8192;
  float normalised = centred >= 0 ? centred / 8191.0f : centred / 8192.0f;
  updatePitchBend(normalised * pitchBendRange);
}

void handleMidiCC(byte cc, byte value) {
  int potValue = map(value, 0, 127, 0, 1023);
  switch (cc) {
    case CC_MOD_WHEEL:
      updateVibratoDepth(value * (0.05f / 127.0f)); break;
    case CC_PITCH:
      updatePitchFromDial(potValue); syncPotBankValue(0, 0, potValue); break;
    case CC_MORPH:
      updateWaveMorph(potValue); syncPotBankValue(1, 0, potValue); break;
    case CC_CUTOFF:
      updateFilterCutoff(potValue); syncPotBankValue(2, 0, potValue); break;
    case CC_ENV_SHAPE:
      updateEnvelopeMorph(potValue); syncPotBankValue(1, 1, potValue); break;
    case CC_FILTER_ENV_SHAPE:
      updateFilterEnvelopeMorph(potValue); syncPotBankValue(0, 1, potValue); break;
    case CC_CHORUS_DEPTH:
      updateChorusDepth(potValue); syncPotBankValue(2, 1, potValue); break;
  }
}

void readMidi() {
  uint8_t status;
  while ((status = midi.read()) != 0) {
    if (status == MIDI16::noteOn) handleMidiNoteOn(midi.getData1(), midi.getData2());
    else if (status == MIDI16::noteOff) handleMidiNoteOff(midi.getData1());
    else if (status == MIDI16::controlChange) handleMidiCC(midi.getData1(), midi.getData2());
    else if (status == MIDI16::pitchBend) handleMidiPitchBend(
        midi.getData1(), midi.getData2());
  }
}

void sendPadNoteOn(int padIdx, int pitch) {
  if (midiOutNote[padIdx] >= 0) midi.sendNoteOff(0, midiOutNote[padIdx], 0);
  midi.sendNoteOn(0, pitch, 100);
  midiOutNote[padIdx] = (int8_t)pitch;
  midiOutOffTime[padIdx] = millis() + 200;
}

void updateMidiOut(unsigned long msNow) {
  for (int i = 0; i < 4; i++) {
    if (midiOutNote[i] >= 0 && (long)(msNow - midiOutOffTime[i]) >= 0) {
      midi.sendNoteOff(0, midiOutNote[i], 0);
      midiOutNote[i] = -1;
    }
  }
}
