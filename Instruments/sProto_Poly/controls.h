#include "MultiControl.h"

// hardware init
const uint8_t padPins[4] = {1, 2, 3, 4};
MultiControl pads[4];
MultiControl pots[3] = {MultiControl(5, 1), MultiControl(6, 1), MultiControl(7, 1)};
MultiControl btn(8, 2);

bool prevPadState[4] = {false, false, false, false};

void syncPotBankValue(uint8_t dial, uint8_t bank, int value) {
  if (dial < 3 && bank < 2) pots[dial].setBankValue(bank, value);
}

void initTouchControls() {
  for (int i = 0; i < 4; i++) {
    pads[i].setPin(padPins[i]);
    pads[i].setControl(0); // 0 = capacitive touch
    pads[i].setTouchDebounceReads(4);
    pads[i].setTouchMinHold(30);
    // Adjacent capacitive pads can produce false dip/rise retrigger patterns.
    pads[i].setRetriggerThreshold(0);
    pads[i].calibrateTouch(50);
    prevPadState[i] = false;
  }

  // Each physical dial has an unshifted bank and a button-held bank. Seed the
  // banks with the parameter defaults so bank changes use soft takeover rather
  // than making either parameter jump to the current physical pot position.
  for (int i = 0; i < 3; i++) pots[i].initBanks(2);
  pots[0].setBankValue(0, 512);                         // root pitch: C4
  pots[1].setBankValue(0, 767);                         // sawtooth
  pots[2].setBankValue(0, (int)(baseFilterCutoff * 1023.0f + 0.5f));
  pots[0].setBankValue(1, 512);                         // filter envelope shape
  pots[1].setBankValue(1, 512);                         // amplitude envelope shape
  pots[2].setBankValue(1, 0);                           // chorus depth
  for (int i = 0; i < 3; i++) pots[i].setBank(0);
}

// --- handlers ---
// Handlers live here so they can reach audio, midi and global variables.

void handleButton(int value) {
  if (value == 0) {
    Serial.println("Button pressed");
  }
  else {
    Serial.println("Button released");
  }
}

void readTouchControls() {
  bool padState[4];
  // Poll every pad on every control tick so debounce timing stays predictable.
  for (int i = 0; i < 4; i++) {
    padState[i] = pads[i].isTouched();
    if (padState[i] && !prevPadState[i]) {
      startVoice(basePitch + padIntervals[i], VOICE_PAD, i);
      sendPadNoteOn(i, basePitch + padIntervals[i]);
    } else if (!padState[i] && prevPadState[i]) {
      releasePadVoice(i);
    }
  }

  for (int i = 0; i < 4; i++) prevPadState[i] = padState[i];
}

void readControls() {
  readTouchControls();

  const uint8_t requestedBank = btn.isPressed() ? 1 : 0;
  if (pots[0].getBank() != requestedBank) {
    for (int i = 0; i < 3; i++) pots[i].setBank(requestedBank);
  }

  static uint8_t index = 0;
  if (index < 3) {
    int v = pots[index].readPotChanged();
    if (v >= 0) {
      switch (index) {
        case 0:
          if (pots[index].getBank() == 1) {
            updateFilterEnvelopeMorph(v);
            btn.notifyHoldAction();
            midi.sendControlChange(0, CC_FILTER_ENV_SHAPE, map(v, 0, 1023, 0, 127));
          } else {
            updatePitchFromDial(v);
            midi.sendControlChange(0, CC_PITCH, map(v, 0, 1023, 0, 127));
          }
          break;
        case 1:
          if (pots[index].getBank() == 1) {
            updateEnvelopeMorph(v);
            btn.notifyHoldAction();
            midi.sendControlChange(0, CC_ENV_SHAPE, map(v, 0, 1023, 0, 127));
          } else {
            updateWaveMorph(v);
            midi.sendControlChange(0, CC_MORPH, map(v, 0, 1023, 0, 127));
          }
          break;
        case 2:
          if (pots[index].getBank() == 1) {
            updateChorusDepth(v);
            btn.notifyHoldAction();
            midi.sendControlChange(0, CC_CHORUS_DEPTH, map(v, 0, 1023, 0, 127));
          } else {
            updateFilterCutoff(v);
            midi.sendControlChange(0, CC_CUTOFF, map(v, 0, 1023, 0, 127));
          }
          break;
      }
    }
  } else {
    int v = btn.readChanged();
    if (v >= 0) handleButton(v);
  }
  index = (index + 1) % 4;
}
