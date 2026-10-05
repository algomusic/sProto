#include "M16.h"

// const float step = 2.0f * PI * 440.0f / SAMPLE_RATE; // for sine wave

void setup() {
  // put your setup code here, to run once:
  seti2sPins(38, 39, 40, -1); // bck, ws, data_out, data_in
  audioStart();
}

void loop() {
  // put your main code here, to run repeatedly:
  
}

void audioUpdate() {
  int32_t sample = random(-32000, 32001);
  // int32_t sample = 16000.0f * sinf(step * audioFrameCount()); // sine wave
  audioBlockWrite(sample, sample);
}
