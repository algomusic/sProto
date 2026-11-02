#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — Serial2 defaults for sProto with ESP32 S3 dev board
// Use 16, 17 for sProto with original ESP32 dev board
MIDI16 midi(42, 47);

// MIDI thru is disabled by default. Select MIDI_THRU_ALL_DATA to forward every
// channel message, or MIDI_THRU_UNUSED_DATA to forward messages this synth does
// not handle. MIDI channels are zero-based (0 = MIDI channel 1).
enum MidiThruMode : uint8_t { MIDI_THRU_ALL_DATA, MIDI_THRU_UNUSED_DATA };
bool midiThruEnabled = false;
MidiThruMode midiThruMode = MIDI_THRU_UNUSED_DATA;
uint8_t synthMidiChannel = 0;

void forwardMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
  // Realtime messages are single-byte messages and must always pass through.
  if (status >= 0xF8) {
    midi.writeByte(status);
    return;
  }
  // MIDI16 currently exposes complete channel messages through read().
  midi.writeByte(status);
  midi.writeByte(data1);
  uint8_t type = status & 0xF0;
  if (type != MIDI16::programChange && type != MIDI16::channelAfterTouch)
    midi.writeByte(data2);
}

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
    uint8_t data1 = midi.getData1();
    uint8_t data2 = midi.getData2();
    uint8_t channel = midi.getChannel();
    bool handled = false;

    if (status >= 0xF8) {
      forwardMidiMessage(status, data1, data2);
      continue;
    }

    if (channel == synthMidiChannel) {
      if (status == MIDI16::noteOn) {
        handleMidiNoteOn(data1, data2);
        handled = true;
      } else if (status == MIDI16::noteOff) {
        handleMidiNoteOff(data1);
        handled = true;
      } else if (status == MIDI16::controlChange) {
        // Only the CCs implemented by this synth count as handled.
        if (data1 >= 8 && data1 <= 10) {
          handleMidiCC(data1, data2);
          handled = true;
        }
      }
    }

    bool shouldForward = midiThruEnabled && (midiThruMode == MIDI_THRU_ALL_DATA || !handled);
    if (shouldForward) {
      uint8_t fullStatus = (status < 0xF0) ? (status | channel) : status;
      forwardMidiMessage(fullStatus, data1, data2);
    }
  }
}

// use to send MIDI note messages, pass isOn true for note on, or false for note off
void sendMidiNote(bool isOn, int chan, int midiPitch, int velocity) {
  if (isOn) {
    midi.sendNoteOn(chan, midiPitch, velocity);
  } else midi.sendNoteOff(chan, midiPitch, 0);
}
