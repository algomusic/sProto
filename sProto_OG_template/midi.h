#include "MIDI16.h"

// GPIO 16 = RX, 17 = TX — ESP32 Serial2 defaults. 
MIDI16 midi(16, 17);

int8_t midiActiveNote = -1;          // MIDI owns envelope when >= 0
int8_t midiOutNote[4] = {-1, -1, -1, -1};
unsigned long midiOutOffTime[4] = {0, 0, 0, 0};

void handleMidiNoteOff(byte pitch) {
  if (pitch == (byte)midiActiveNote && activeTrigger == 5) {
    env.startRelease();
    activeTrigger = -1;
    midiActiveNote = -1;
  }
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {handleMidiNoteOff(pitch); return;}  // vel=0 is note off
  osc.setPitch(pitch);
  env.start();
  midiActiveNote = pitch;
  activeTrigger  = 5;
}

void handleMidiCC(byte cc, byte value) {
  switch (cc) {
    case 8:
      potPitch = map(value, 0, 127, 0, 1023);
      basePitch = map(potPitch, 0, 1023, 24, 84);
      break;
    case 9: potRelease = map(value, 0, 127, 0, 1023); break;
    case 10:
      potVol = map(value, 0, 127, 0, 1023);
      outputGain.setLevel(potVol);
      break;
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

void readPots() {
  int prevPitch = potPitch, prevRelease = potRelease, prevVol = potVol;
  readControls();
  if (potPitch != prevPitch) {
    midi.sendControlChange(0, 8, map(potPitch, 0, 1023, 0, 127));
    basePitch = map(potPitch, 0, 1023, 24, 84);
  }
  if (potRelease != prevRelease) midi.sendControlChange(0, 9, map(potRelease, 0, 1023, 0, 127));
  if (potVol != prevVol) midi.sendControlChange(0, 10, map(potVol, 0, 1023, 0, 127));
  env.setRelease(floatMap(potRelease, 0, 1023, 10.0f, 5000.0f));
}
