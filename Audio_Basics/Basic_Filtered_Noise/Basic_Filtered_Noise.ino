#include "M16.h"

  constexpr int filterDepth = 4;
  int32_t noiseHistory[filterDepth] = {};
  int historyIndex = 0;

  void setup() {
    seti2sPins(38, 39, 40, -1);
    audioStart();
  }

  void loop() {
  }

  void audioUpdate() {
    int32_t noise = random(-32000, 32001);

    noiseHistory[historyIndex] = noise;
    historyIndex = (historyIndex + 1) % filterDepth;

    int32_t sum = 0;
    for (int i = 0; i < filterDepth; i++) {
      sum += noiseHistory[i];
    }
    // average recent values
    int32_t filteredNoise = sum / filterDepth;
    audioBlockWrite(filteredNoise, filteredNoise);
  }