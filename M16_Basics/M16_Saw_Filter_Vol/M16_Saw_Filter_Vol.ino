#include "M16.h"
#include "Osc.h"
#include "Bob.h"
#include "Gain.h"

Osc osc1; // declare an instance of the oscillator class
Bob filter1;
Gain outputGain(250); // 0 = silent, 1024 = full level

void setup() {
  // put your setup code here, to run once:
  osc1.sawGen(); // initialise the osc's wavetable with a sawtooth waveform
  osc1.setPitch(69); // MIDI pitch, A4, 440 Hz
  filter1.setFreq(900); // filter cutoff, 900 Hz
  seti2sPins(38, 39, 40, -1); // DAC connections: bck, ws, data_out, data_in
  audioStart();
}

void loop() {
  // put your main code here, to run repeatedly:
  
}

void audioUpdate() {
  int32_t sample = osc1.next(); // get the next wavetable value
  sample = filter1.next(sample); // apply filter
  sample = outputGain.next(sample); // adjust the volume
  audioBlockWrite(sample, sample);
}
