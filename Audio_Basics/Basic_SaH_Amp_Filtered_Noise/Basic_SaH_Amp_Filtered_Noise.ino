#include "M16.h"

  constexpr int filterDepth = 4;
  constexpr int sampleHoldPeriod = 100; // audio samples between new noise values
  int32_t noiseHistory[filterDepth] = {};
  int historyIndex = 0;
  int32_t heldNoise = 0;
  int samplesUntilNoiseRead = 0;
  float volume = 0.5;

  void setup() {
    seti2sPins(38, 39, 40, -1);
    audioStart();
  }

  void loop() {
  }

  void audioUpdate() {
    // Sample and hold: read a new random value only when the counter expires.
    // Between reads, keep using the previously sampled value.
    if (samplesUntilNoiseRead == 0) {
      heldNoise = random(-32000, 32001);
      samplesUntilNoiseRead = sampleHoldPeriod;
    }
    samplesUntilNoiseRead--;

    noiseHistory[historyIndex] = heldNoise;
    historyIndex = (historyIndex + 1) % filterDepth;

    int32_t sum = 0;
    for (int i = 0; i < filterDepth; i++) {
      sum += noiseHistory[i];
    }
    // average recent values
    int32_t filteredNoise = sum / filterDepth;
    // adjust the volume gain
    filteredNoise *= volume;
    audioBlockWrite(filteredNoise, filteredNoise);
  }
