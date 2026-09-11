// Pot-controlled sample-and-hold, amplified and filtered noise.

#include "M16.h"
#include "Osc.h"
#include "EMA.h"
#include "Gain.h"
#include "MultiControl.h"

WaveTable noiseTable;
Osc sampleAndHold;
EMA noiseFilter;
Gain outputGain(512);

MultiControl sampleHoldPot(5, 1);
MultiControl filterAmountPot(6, 1);
MultiControl volumePot(7, 1);

int sampleHoldPeriod = 100;   // 1-200 audio samples
float noiseFilterAmnt = 0.25f; // 0.0-1.0 EMA coefficient
int volume = 512;             // 0-1024

void setSampleHoldPeriodFromPot(int potValue) {
  sampleHoldPeriod = 1 + ((potValue * 199) / 1022);
  sampleAndHold.setFreq((float)SAMPLE_RATE / sampleHoldPeriod);
}

void setFilterAmountFromPot(int potValue) {
  noiseFilterAmnt = potValue * (1.0f / 1022.0f);
  noiseFilter.setCoefficient((int)(noiseFilterAmnt * 1024.0f));
}

void setVolumeFromPot(int potValue) {
  volume = (potValue * 1024) / 1022;
  outputGain.setLevel(volume);
}

void setup() {
  sampleHoldPot.setPotHysteresis(4);
  filterAmountPot.setPotHysteresis(4);
  volumePot.setPotHysteresis(4);

  int value = sampleHoldPot.readPot();
  if (value >= 0) setSampleHoldPeriodFromPot(value);
  value = filterAmountPot.readPot();
  if (value >= 0) setFilterAmountFromPot(value);
  value = volumePot.readPot();
  if (value >= 0) setVolumeFromPot(value);

  noiseTable.noiseGen();
  sampleAndHold.setTable(noiseTable);
  sampleAndHold.setSandH(true);
  sampleAndHold.setFreq((float)SAMPLE_RATE / sampleHoldPeriod);

  seti2sPins(38, 39, 40, -1); // BCK, WS, DOUT, DIN (unused)
  audioStart();
}

void loop() {
  int value = sampleHoldPot.readPotChanged();
  if (value >= 0) setSampleHoldPeriodFromPot(value);

  value = filterAmountPot.readPotChanged();
  if (value >= 0) setFilterAmountFromPot(value);

  value = volumePot.readPotChanged();
  if (value >= 0) setVolumeFromPot(value);

  delay(4);
}

void audioUpdate() {
  int32_t noise = sampleAndHold.next();
  int32_t filteredNoise = noiseFilter.next(noise);
  int32_t output = outputGain.next(filteredNoise);
  audioBlockWrite(output, output);
}
