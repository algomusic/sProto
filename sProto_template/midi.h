#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — Serial2 defaults for sProto with ESP32 S3 dev board
// Use 16, 17 for sProto with original ESP32 dev board
MIDI16 midi(42, 47);

void handleMidiNoteOff(byte pitch) {
  ampEnv1.startRelease();
}

void handleMidiNoteOn(byte pitch, byte velocity) {
  if (velocity == 0) {handleMidiNoteOff(pitch); return;}  // vel=0 is note off
  osc1.setPitch(pitch);
  ampEnv1.start();
}

void handleMidiCC(byte cc, byte value) {
  switch (cc) {
    case 8: // cc number
      osc1.setPitch(map(value, 0, 1024, 24, 96)); break;
    case 9:
      filter1.setFreq(map(value, 0, 1024, 200, 5000)); break;
    case 10:
      outputGain.setLevel(value); break;
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

// use to send MIDI note messages, isOn true for note on, false for note off
void sendMIDI(bool isOn, int chan, int midiPitch, int velocity) {
  if (isOn) {
    midi.sendNoteOn(chan, midiPitch, velocity);
  } else midi.sendNoteOff(chan, midiPitch, 0);
}