#include "MultiControl.h"

// hardware
MultiControl pot1(32, 1); // pitch
MultiControl pot2(33, 1); // release
MultiControl pot3(34, 1); // volume
MultiControl btn(27, 2); // trigger sound
MultiControl pad1(12, 0); // play base pitch
MultiControl pad2(13, 0); // play base + 2 semitones
MultiControl pad3(14, 0); // play base + 4 semitons
MultiControl pad4(15, 0); // play base + 7 semitones

// pot values (0-1023)
int potPitch = 512;
int potRelease   = 512;
int potVol   = 1023;

// gate/trigger state — shared with midi.h and the .ino handler functions
int  activeTrigger = -1;   // -1=none, 0=button, 1-4=pads, 5=MIDI
bool prevBtnState  = false;
bool prevPadState[4] = {false, false, false, false};

void initControls() {
  pad1.calibrateTouch(50); pad1.setTouchThresholds(45, 30); pad1.setTouchDebounceReads(4);
  pad2.calibrateTouch(50); pad2.setTouchThresholds(45, 30); pad2.setTouchDebounceReads(4);
  pad3.calibrateTouch(50); pad3.setTouchThresholds(45, 30); pad3.setTouchDebounceReads(4);
  pad4.calibrateTouch(50); pad4.setTouchThresholds(45, 30); pad4.setTouchDebounceReads(4);
}

// reads pots and button only — pad reads are done one-per-ms in the main envelope tick
void readControls() {
  static uint8_t index = 0;
  switch (index) {
    case 0: { int v = pot1.readPotChanged(); if (v >= 0) potPitch = v; } break;
    case 1: { int v = pot2.readPotChanged(); if (v >= 0) potRelease   = v; } break;
    case 2: {
      int v = pot3.readPotChanged();
      if (v >= 0) {
        potVol = v;
        outputGain.setLevel(v);
      }
    } break;
    case 3: btn.readChanged(); break;
  }
  if (++index > 3) index = 0;
}

void sendPadNoteOn(int padIdx, int pitch);  // defined in midi.h

// --- handlers ---
// handleButton and handleTouchPads live here so they can reach osc, env, midi, basePitch etc.

void handleButton() {
  bool btnNow = btn.isPressed();
  if (btnNow && !prevBtnState) {
    osc.setPitch(basePitch);
    env.start();
    activeTrigger = 0;
  }
  if (!btnNow && prevBtnState && activeTrigger == 0) {
    env.startRelease();
    activeTrigger = -1;
  }
  prevBtnState = btnNow;
}

void handleTouchPads(int padIdx, bool padNow) {
  if (padNow && !prevPadState[padIdx]) {
    int pitch = basePitch + padIntervals[padIdx];
    osc.setPitch(pitch);
    env.start();
    activeTrigger = padIdx + 1;
    sendPadNoteOn(padIdx, pitch);
  }
  if (!padNow && prevPadState[padIdx] && activeTrigger == padIdx + 1) {
    env.startRelease();
    activeTrigger = -1;
  }
  prevPadState[padIdx] = padNow;
}

void readTouchPads() {
  static uint8_t padIdx = 0;
  bool padNow;
  switch (padIdx) {
    case 0: padNow = pad1.isTouched(); break;
    case 1: padNow = pad2.isTouched(); break;
    case 2: padNow = pad3.isTouched(); break;
    default: padNow = pad4.isTouched(); break;
  }
  handleTouchPads(padIdx, padNow);
  padIdx = (padIdx + 1) & 3;
}
