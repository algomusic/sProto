//. sProto PCB template using the M16 and MultiControl libraries
// a simple mono synthesizer with pot control over pitch, filter cutoff, and volume
// There are 3 files:
// MultiControl user-interface code is in the controls.h file
// MIDI code is in the midi.h file
// Audio code is in this main file

#include "M16.h"
#include "Osc.h"
#include "Bob.h"
#include "Env.h"
#include "Gain.h"

Osc osc1; // declare an instance of the oscillator class
Bob filter1; // declare an instance of the Bob filter class
Env ampEnv1; // declare an instance of the envelope class
Gain outputGain(250); // a gain instance and inital value. 0 = silent, 1024 = full level

#include "controls.h"
#include "midi.h"

void setup() {
  Serial.begin(115200);
  osc1.sawGen(); // initialise the osc's wavetable with a sawtooth waveform
  osc1.setPitch(69); // MIDI pitch, A4, 440 Hz
  filter1.setFreq(900); // filter cutoff, 900 Hz
  // ampEnv1.setSustain(0.5); // add sustain, set the envelopes' sustain level
  seti2sPins(38, 39, 40, -1); // DAC connections for sProto S3: bck, ws, data_out, data_in
  // useInternalDAC(); // For sProto with original ESP32 dev kit
  audioStart();
}

void loop() {
  readPots(); // in controls.h
  readTouchPads(); // in controls.h
  readMidi(); // in midi.h
}

void audioUpdate() {
  int32_t sample = osc1.next(); // get the next wavetable value
  sample = filter1.next(sample); // apply filter
  sample = (ampEnv1.next() * sample) >> 16; // apply envelope
  sample = outputGain.next(sample); // adjust the volume
  audioBlockWrite(sample, sample); // send to the DAC
}
