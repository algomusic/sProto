// Use M16's MIDI16 class to read and send MIDI messages.

#include "MIDI16.h"

// GPIO 42 = RX, 47 = TX — Serial2 defaults for sProto with ESP32 S3 dev board
// Use 16, 17 for sProto with original ESP32 dev board
MIDI16 midi(42, 47);

// MIDI thru is disabled by default. ALL_DATA forwards every received byte;
// UNUSED_DATA forwards channel messages this synth does not handle. Realtime
// messages always pass through. MIDI channels are zero-based (0 = channel 1).
enum MidiThruMode : uint8_t { MIDI_THRU_ALL_DATA, MIDI_THRU_UNUSED_DATA };
bool midiThruEnabled = false;
MidiThruMode midiThruMode = MIDI_THRU_UNUSED_DATA;
uint8_t synthMidiChannel = 0;

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
  // MIDI16's byte-level thru also forwards SysEx and System Common messages.
  // Unused-data filtering still requires forwarding individual received events.
  const bool allDataThru = midiThruEnabled && midiThruMode == MIDI_THRU_ALL_DATA;
  midi.setMidiThru(allDataThru);
  uint8_t status;
  while ((status = midi.read()) != 0) {
    uint8_t data1 = midi.getData1();
    uint8_t data2 = midi.getData2();
    uint8_t channel = midi.getChannel();
    bool handled = false;

    if (status >= 0xF8) {
      if (!allDataThru) midi.forwardReceivedMessage();
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

    bool shouldForward = midiThruEnabled && midiThruMode == MIDI_THRU_UNUSED_DATA && !handled;
    if (shouldForward) {
      midi.forwardReceivedMessage();
    }
  }
}

// use to send MIDI note messages, pass isOn true for note on, or false for note off
void sendMidiNote(bool isOn, int chan, int midiPitch, int velocity) {
  if (isOn) {
    midi.sendNoteOn(chan, midiPitch, velocity);
  } else midi.sendNoteOff(chan, midiPitch, 0);
}
