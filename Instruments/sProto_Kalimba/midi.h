#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — ESP32 Serial2 defaults. 
MIDI16 midi(42, 47);

int8_t lastMidiPitch = -1;
int8_t lastMidiVoice = -1;
int8_t midiOutNote[4] = {-1, -1, -1, -1};
unsigned long midiOutOffTime[4] = {0, 0, 0, 0};

// MIDI CC numbers for pot parameters
const byte CC_PITCH = 8;
const byte CC_RELEASE = 9;
const byte CC_VOLUME = 10;

void handleMidiNoteOff(byte pitch) {
  for (int voice = 0; voice < voiceCount; voice++) {
    if (voiceOwner[voice] == VOICE_MIDI && voicePitch[voice] == (int8_t)pitch) {
      restorePadVoiceIfHeld(voice);
    }
  }
  if (lastMidiPitch == (int8_t)pitch) {
    lastMidiPitch = -1;
    lastMidiVoice = -1;
  }
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {handleMidiNoteOff(pitch); return;}  // vel=0 is note off

  int voice;
  if (lastMidiVoice >= 0 && pitch == (byte)lastMidiPitch &&
      voiceOwner[lastMidiVoice] == VOICE_MIDI) {
    // Consecutive repetitions of one pitch retrigger the same resonator.
    voice = lastMidiVoice;
  } else {
    // Changed pad and MIDI pitches share one three-voice round-robin cursor.
    voice = nextRoundRobinVoice;
    nextRoundRobinVoice = (nextRoundRobinVoice + 1) % voiceCount;
  }

  float level = floatMap(velocity, 0, 127, 0.1f, 1.0f);
  exciterFilters[voice].setFreq(
      floatMap(velocity, 0, 127, exciterCutoff * 0.5f, exciterCutoff * 2.0f));
  playVoice(voice, pitch, VOICE_MIDI, level);
  lastMidiPitch = pitch;
  lastMidiVoice = voice;
}

void handleMidiCC(byte cc, byte value) {
  switch (cc) {
    case CC_PITCH:   updatePitch(floatMap(value, 0, 127, 48, 84)); break;
    case CC_RELEASE: updateExciterDampening(floatMap(value, 0, 127, 50.0f, 10000.0f)); break;
    case CC_VOLUME:  updateVolume(map(value, 0, 127, 0, 1024)); break;
  }
}

void readMidi() {
  uint8_t status;
  while ((status = midi.read()) != 0) {
    if (status == MIDI16::noteOn) handleMidiNoteOn(midi.getData1(), midi.getData2());
    else if (status == MIDI16::noteOff) handleMidiNoteOff(midi.getData1());
    else if (status == MIDI16::controlChange) handleMidiCC(midi.getData1(), midi.getData2());
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
