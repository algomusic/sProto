#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — ESP32 Serial2 defaults. 
MIDI16 midi(42, 47);

int8_t midiActiveNote = -1;          // MIDI owns envelope when >= 0
int8_t midiOutNote[4] = {-1, -1, -1, -1};
unsigned long midiOutOffTime[4] = {0, 0, 0, 0};

void handleMidiNoteOff(byte pitch) {
  if (pitch == (byte)midiActiveNote && activeTrigger == 5) {
    midiActiveNote = -1;
    resumeHeldTouchPadOrRelease();
  }
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) { // vel=0 is note off
    handleMidiNoteOff(pitch); return;
  } else {
    toneAmpEnv.setMaxLevel(pow(velocity / 127.0f, 2) * 0.3f + 0.7f);
    baseToneCutoff = velocity / 127.0f * 400 + 500;
  }
  targetPitch = pitch;
  // setPlayingPitch(pitch);
  startEnvelopes();
  midiActiveNote = pitch;
  activeTrigger  = 5;
}

void handleMidiCC(byte cc, byte value) {
  switch (cc) {
    case 8:  updatePitch(floatMap(value, 0, 127, 48, 84)); break;
    case 9:  updateRelease(floatMap(value, 0, 127, 10.0f, 5000.0f)); break;
    case 10: updateVolume(map(value, 0, 127, 0, 1024)); break;
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
