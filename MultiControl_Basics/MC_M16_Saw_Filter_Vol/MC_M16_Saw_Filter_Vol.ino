#include "M16.h"
#include "Osc.h"
#include "Bob.h"
#include "Gain.h"

Osc osc1; // declare an instance of the oscillator class
Bob filter1;
Gain outputGain(250); // 0 = silent, 1024 = full level

#include "MultiControl.h"

MultiControl dial1(5, 1); MultiControl dial2(6, 1);
MultiControl dial3(7, 1); MultiControl button(8, 2);
MultiControl pad1(1, 0); MultiControl pad2(2, 0);
MultiControl pad3(3, 0); MultiControl pad4(4, 0);

void setup() {
  osc1.sawGen(); // initialise the osc's wavetable with a sawtooth waveform
  osc1.setPitch(69); // MIDI pitch, A4, 440 Hz
  filter1.setFreq(900); // filter cutoff, 900 Hz
  seti2sPins(38, 39, 40, -1); // DAC connections: bck, ws, data_out, data_in
  audioStart();
}

void loop() {
  const int dial1Value = dial1.readPotChanged(); // oscillator pitch
  if (dial1Value >= 0) osc1.setPitch(map(dial1Value, 0, 1024, 24, 96));
  const int dial2Value = dial2.readPotChanged(); // filter cutoff
  if (dial2Value >= 0) filter1.setFreq(map(dial2Value, 0, 1024, 200, 5000));
  const int dial3Value = dial3.readPotChanged(); // volume
  if (dial3Value >= 0) outputGain.setLevel(dial3Value);

  if (pad1.isTouched()) osc1.setPitch(60);
  if (pad2.isTouched()) osc1.setPitch(62);
  if (pad3.isTouched()) osc1.setPitch(64);
  if (pad4.isTouched()) osc1.setPitch(65);
}

void audioUpdate() {
  int32_t sample = osc1.next(); // get the next wavetable value
  sample = filter1.next(sample); // apply filter
  sample = outputGain.next(sample); // adjust the volume
  audioBlockWrite(sample, sample);
}
