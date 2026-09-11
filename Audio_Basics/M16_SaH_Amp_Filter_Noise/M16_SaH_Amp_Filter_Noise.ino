// Sample-and-hold, amplified and filtered noise using M16 classes.

#include "M16.h"
#include "Osc.h"
#include "EMA.h"
#include "Gain.h"

WaveTable noiseTable;
Osc sampleAndHold;
EMA noiseFilter(0.25f);
Gain outputGain(512);

constexpr int sampleHoldPeriod = 100; // audio samples per held noise value

void setup() {
  noiseTable.noiseGen();
  sampleAndHold.setTable(noiseTable);
  sampleAndHold.setSandH(true);
  sampleAndHold.setFreq((float)SAMPLE_RATE / sampleHoldPeriod);

  seti2sPins(38, 39, 40, -1); // BCK, WS, DOUT, DIN (unused)
  audioStart();
}

void loop() {
}

void audioUpdate() {
  int32_t noise = sampleAndHold.next();
  int32_t filteredNoise = noiseFilter.next(noise);
  int32_t output = outputGain.next(filteredNoise);
  audioBlockWrite(output, output);
}
