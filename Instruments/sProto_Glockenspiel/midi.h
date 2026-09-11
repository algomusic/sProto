#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — ESP32 Serial2 defaults. 
MIDI16 midi(42, 47); // S3
// MIDI16 midi(35, 17); // OG I2S

int8_t midiActiveNote = -1;          // MIDI owns envelope when >= 0
int8_t midiOutNote[4] = {-1, -1, -1, -1};
unsigned long midiOutOffTime[4] = {0, 0, 0, 0};

// MIDI CC numbers for pot parameters
const byte CC_PITCH = 8;
const byte CC_RELEASE = 9;
const byte CC_VOLUME = 10;

void handleMidiNoteOff(byte pitch) {
  if (pitch == (byte)midiActiveNote && activeTrigger == 5) {
    midiActiveNote = -1;
    resumeHeldTouchPadOrRelease();
  }
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {handleMidiNoteOff(pitch); return;}  // vel=0 is note off
  // osc.setPitch(pitch);
  setVoicePitch(pitch);
  triggerNote(5);
  // env.start();
  // activeTrigger = 5;
  midiActiveNote = pitch;
  
}

void handleMidiCC(byte cc, byte value) {
  switch (cc) {
    case 8:  updatePitch(floatMap(value, 0, 127, 48, 96)); break;
    case 9:  updateModIndex(floatMap(value, 0, 127, 0.0f, 10.0f)); break;
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
