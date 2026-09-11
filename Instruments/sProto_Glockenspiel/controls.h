#include "MultiControl.h"

// hardware init
const uint8_t padPins[4] = {1, 2, 3, 4}; // S3
// const uint8_t padPins[4] = {13, 14, 15, 16}; // OG I2S
MultiControl pads[4];
MultiControl pots[3] = {MultiControl(5, 1), MultiControl(6, 1), MultiControl(7, 1)}; // S3
// MultiControl pots[3] = {MultiControl(32, 1), MultiControl(33, 1), MultiControl(34, 1)}; // OG I2S
MultiControl btn(8, 2); // S3
// MultiControl btn(27, 2); // OD I2S

bool prevPadState[4] = {false, false, false, false};
uint32_t padPressOrder[4] = {0, 0, 0, 0};
uint32_t nextPadPressOrder = 1;

int newestHeldPad() {
  int selectedPad = -1;
  uint32_t newestOrder = 0;
  for (int i = 0; i < 4; i++) {
    if (prevPadState[i] && padPressOrder[i] >= newestOrder) {
      newestOrder = padPressOrder[i];
      selectedPad = i;
    }
  }
  return selectedPad;
}

void resumeHeldTouchPadOrRelease() {
  int selectedPad = newestHeldPad();
  if (selectedPad >= 0) {
    setVoicePitch(basePitch + padIntervals[selectedPad]);
    triggerNote(selectedPad + 1);

  } else {
    releaseNote();;
  }
}

void initTouchControls() {
  for (int i = 0; i < 4; i++) {
    pads[i].setPin(padPins[i]);
    pads[i].setControl(0); // 0 = capacitive touch
    pads[i].setTouchDebounceReads(4);
    pads[i].setTouchMinHold(30);
    // Adjacent capacitive pads can produce false dip/rise retrigger patterns.
    pads[i].setRetriggerThreshold(0); // S3
    pads[i].calibrateTouch(50);
    prevPadState[i] = false;
  }
}

// --- handlers ---
// Handlers live here so they can reach audio, midi and global variables.

void handleButton(int value) {
  if (value == 0) {
    setVoicePitch(basePitch);
    triggerNote(0);
    Serial.println("Button pressed");
  }
  else if (activeTrigger == 0) {
    resumeHeldTouchPadOrRelease();
    Serial.println("Button released");
  }
}

void readTouchControls() {
  bool padState[4];
  bool padPressed = false;

  // Poll every pad on every control tick so debounce timing stays predictable.
  for (int i = 0; i < 4; i++) {
    padState[i] = pads[i].isTouched();
    if (padState[i] && !prevPadState[i]) {
      padPressOrder[i] = nextPadPressOrder++;
      padPressed = true;
      sendPadNoteOn(i, basePitch + padIntervals[i]);
    }
  }

  int selectedPad = -1;
  uint32_t newestOrder = 0;
  for (int i = 0; i < 4; i++) {
    if (padState[i] && padPressOrder[i] >= newestOrder) {
      newestOrder = padPressOrder[i];
      selectedPad = i;
    }
  }

  bool activePadReleased = activeTrigger >= 1 && activeTrigger <= 4 &&
                           !padState[activeTrigger - 1];
  if (padPressed || activePadReleased) {
    if (selectedPad >= 0) {
      setVoicePitch(basePitch + padIntervals[selectedPad]);
      triggerNote(selectedPad + 1);
    } else if (activePadReleased) {
      releaseNote();
    }
  }

  for (int i = 0; i < 4; i++) prevPadState[i] = padState[i];
}

void readControls() {
  readTouchControls();

  static uint8_t index = 0;
  if (index < 3) {
    int v = pots[index].readPotChanged();
    if (v >= 0) {
      switch (index) {
        case 0: updatePitch(map(v, 0, 1023, 48, 96)); break;
        case 1: updateModIndex(floatMap(v, 0, 1023, 0.0f, 10.0f)); break;
        case 2: updateVolume(map(v, 0, 1023, 0, 1024)); break;
      }
      midi.sendControlChange(0, CC_PITCH + index, map(v, 0, 1023, 0, 127));
    }
  } else {
    int v = btn.readChanged();
    if (v >= 0) handleButton(v);
  }
  index = (index + 1) % 4;
}
