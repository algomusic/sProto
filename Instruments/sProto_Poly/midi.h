#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — ESP32 Serial2 defaults. 
MIDI16 midi(42, 47);

int8_t midiChannel = 1;
int8_t midiOutNote[4] = {-1, -1, -1, -1};
unsigned long midiOutOffTime[4] = {0, 0, 0, 0};
const float pitchBendRange = 2.0f; // semitones above and below centre
// Set to MIDI_THRU_ALL for complete byte thru, or MIDI_THRU_UNUSED
// to pass only events this synth does not handle. The default leaves thru off.
uint8_t midiThruMode = MIDI_THRU_OFF;

// MIDI CC numbers for pot parameters
const byte CC_MOD_WHEEL = 1;
const byte CC_PITCH = 8;
const byte CC_MORPH = 9; // waveform selection
const byte CC_CUTOFF = 10;
const byte CC_ENV_SHAPE = 12;
const byte CC_FILTER_ENV_SHAPE = 13;
const byte CC_CHORUS_DEPTH = 14;

void initMidiInput() {
  midi.setMidiThruMode(midiThruMode);
  // Reserve the local voice pool for this synth. MIDI16 will forward note
  // events beyond this limit and remember their note-off routing, avoiding
  // voice stealing when MIDI thru is enabled.
  if (midiThruMode != MIDI_THRU_OFF) {
    midi.setExtendPoly(voices);
    midi.setExtendChannel((uint8_t)max(1, min(16, (int)midiChannel)));
    midi.setMidiExtend(true);
  }
  #if IS_ESP32()      
    midi.beginClockTask(1, 5);
  #endif
}

void handleMidiNoteOff(byte pitch) {
  releaseMidiVoice(pitch, midi.getChannel());
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {handleMidiNoteOff(pitch); return;}  // vel=0 is note off
  startVoice(pitch, MIDI_SOURCE, pitch, velocity, midi.getChannel());
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
      updateVibratoDepth(value * (1.0f / 127.0f)); break;
    case CC_PITCH:
      updatePitchFromDial(potValue); syncPotBankValue(0, 0, potValue); break;
    case CC_MORPH:
      updateWaveformSelection(potValue); syncPotBankValue(1, 0, potValue); break;
    case CC_CUTOFF:
      updateFilterCutoff(potValue); syncPotBankValue(2, 0, potValue); break;
    case CC_ENV_SHAPE:
      updateEnvelopeMorph(potValue); syncPotBankValue(0, 1, potValue); break;
    case CC_FILTER_ENV_SHAPE:
      updateFilterEnvelopeMorph(potValue); syncPotBankValue(1, 1, potValue); break;
    case CC_CHORUS_DEPTH:
      updateChorusDepth(potValue); syncPotBankValue(2, 1, potValue); break;
  }
}

void readMidi() {
  uint8_t status;
  while ((status = midi.read()) != 0) {
    bool handled = false;
    const uint8_t selectedChannel =
        (uint8_t)max(1, min(16, (int)midiChannel)) - 1;
    if (midi.getChannel() == selectedChannel) {
      if (status == MIDI16::noteOn) {
        handleMidiNoteOn(midi.getData1(), midi.getData2());
        handled = true;
      } else if (status == MIDI16::noteOff) {
        handleMidiNoteOff(midi.getData1());
        handled = true;
      } else if (status == MIDI16::controlChange) {
        const byte cc = midi.getData1();
        if (cc == CC_MOD_WHEEL || cc == CC_PITCH || cc == CC_MORPH ||
            cc == CC_CUTOFF || cc == CC_ENV_SHAPE ||
            cc == CC_FILTER_ENV_SHAPE || cc == CC_CHORUS_DEPTH) {
          handleMidiCC(cc, midi.getData2());
          handled = true;
        }
      } else if (status == MIDI16::pitchBend) {
        handleMidiPitchBend(midi.getData1(), midi.getData2());
        handled = true;
      }
    }

    // ALL_DATA is forwarded by MIDI16 byte-for-byte. UNUSED_DATA forwards
    // channel events here only when the synth did not act on them; system
    // common, SysEx and real-time handling stays inside MIDI16.
    if (!handled) midi.forwardUnhandledMessage();
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
