#include "MultiControl.h"
// set up MC instances for each user interface element: GPIO pin, MC type
MultiControl dial1(5, 1); MultiControl dial2(6, 1); // OG 32, 33 - S3 5, 6
MultiControl dial3(7, 1); MultiControl button(8, 2); // OG 34, 27 - S3 7, 8
MultiControl pad1(1, 0); MultiControl pad2(2, 0); // OG 12, 13 - S3 1, 2
MultiControl pad3(3, 0); MultiControl pad4(4, 0); // OG 14, 15 - S3, 3, 4

bool padActive [] = {false, false, false, false}; // track pad on/off status
bool padGateActive = false;

void readPots() {
  const int dial1Value = dial1.readPotChanged(); // oscillator pitch
  if (dial1Value >= 0) osc1.setPitch(map(dial1Value, 0, 1024, 24, 96));
  const int dial2Value = dial2.readPotChanged(); // filter cutoff
  if (dial2Value >= 0) filter1.setFreq(map(dial2Value, 0, 1024, 200, 5000));
  const int dial3Value = dial3.readPotChanged(); // volume
  if (dial3Value >= 0) outputGain.setLevel(dial3Value);
}

void readTouchPads() {
  const bool padState[4] = {
    pad1.isTouched(), pad2.isTouched(), pad3.isTouched(), pad4.isTouched()
  };
  const int padPitch[4] = {60, 62, 64, 65};
  bool anyPadActive = false;
  for (int i = 0; i < 4; ++i) {
    if (padState[i] && !padActive[i]) {
      osc1.setPitch(padPitch[i]);
      ampEnv1.start();
    }
    padActive[i] = padState[i];
    anyPadActive |= padState[i];
  }
  if (padGateActive && !anyPadActive) ampEnv1.startRelease();
  padGateActive = anyPadActive;
}