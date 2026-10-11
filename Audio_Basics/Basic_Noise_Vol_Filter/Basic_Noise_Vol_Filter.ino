#include "M16.h"

int32_t previousSample = 0; // store previous sample

void setup() {
  // put your setup code here, to run once:
  seti2sPins(38, 39, 40, -1); // DAC connections: bck, ws, data_out, data_in
  audioStart();
}

void loop() {
  // put your main code here, to run repeatedly:
  
}

void audioUpdate() {
  int32_t sample = random(-32000, 32001); // noise
  sample = (previousSample + sample) / 2; // average
  previousSample = sample;
  sample = sample * 0.25; // volume
  audioBlockWrite(sample, sample);
}
