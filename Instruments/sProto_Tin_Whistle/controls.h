#include "MultiControl.h"

// Dial A - pitch
// Dial B - tone low-pass cutoff
// Dial C - output volume

// hardware init
MultiControl pad1(1, 0); // play base pitch
MultiControl pad2(2, 0); // play base + 2 semitones
MultiControl pad3(3, 0); // play base + 4 semitons
MultiControl pad4(4, 0); // play base + 7 semitones
MultiControl pot1(5, 1); // pitch
MultiControl pot2(6, 1); // release
MultiControl pot3(7, 1); // volume
MultiControl btn(8, 2); // trigger sound

bool prevPadState[4] = {false, false, false, false};
uint32_t padPressOrder[4] = {0, 0, 0, 0};
uint32_t nextPadPressOrder = 1;

MultiControl* const touchPads[4] = {&pad1, &pad2, &pad3, &pad4};

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
    triggerNotePitch(basePitch + padIntervals[selectedPad]);
    startEnvelopes();
    activeTrigger = selectedPad + 1;
  } else {
    pitchGlideActive = false;
    releaseEnvelopes();
    activeTrigger = -1;
  }
}

void initTouchControls() {
  for (int i = 0; i < 4; i++) {
    touchPads[i]->setTouchDebounceReads(4);
    touchPads[i]->setTouchMinHold(30);
    // Retrigger detection is unreliable when adjacent capacitive pads couple.
    touchPads[i]->setRetriggerThreshold(0);
    touchPads[i]->calibrateTouch(50);
    prevPadState[i] = false;
  }
}

// --- handlers ---
// Handlers live here so they can reach audio, midi and global variables.

void handleButton(int value) {
  if (value == 0) {
    triggerNotePitch(basePitch);
    startEnvelopes();
    activeTrigger = 0;
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

  // Read every pad on every control tick so debounce timing is predictable.
  for (int i = 0; i < 4; i++) {
    padState[i] = touchPads[i]->isTouched();
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
      triggerNotePitch(basePitch + padIntervals[selectedPad]);
      startEnvelopes();
      activeTrigger = selectedPad + 1;
    } else if (activePadReleased) {
      pitchGlideActive = false;
      releaseEnvelopes();
      activeTrigger = -1;
    }
  }

  for (int i = 0; i < 4; i++) prevPadState[i] = padState[i];

  // Only the pad that owns the voice controls its pressure-dependent timbre.
  if (activeTrigger >= 1 && activeTrigger <= 4) {
    int padIdx = activeTrigger - 1;
    int level = touchPads[padIdx]->getValue();
    toneAmpEnv.setMaxLevel(pow(level / 1024.0f, 2) * 0.3f + 0.7f);
    toneCutoffPressureScale = (level / 1024.0f * 400.0f + 500.0f) / 900.0f;
    adjustToneCutoff();
    adjustNoiseCutoff();
  }
}

void readControls() {
  readTouchControls();

  static uint8_t index = 0;
  switch (index) {
    case 0: { int v = pot1.readPotChanged();
      if (v >= 0) {
        float pitch = floatMap(v, 0, 1023, 48, 84);
        updatePitch(pitch);
        Serial.print("Pitch "); Serial.println(pitch, 2);
        midi.sendControlChange(0, 8, map(v, 0, 1023, 0, 127));
      }
    } break;
    case 1: { int v = pot2.readPotChanged();
      if (v >= 0) {
        // updateRelease(floatMap(v, 0, 1023, 10.0f, 5000.0f));
        // Pitch tracking and touch pressure are applied by adjustToneCutoff().
        baseToneCutoff = floatMap(v, 0, 1023, 500.0f, 6000.0f);
        adjustToneCutoff();
        Serial.print("Tone Cutoff "); Serial.println(baseToneCutoff);
        midi.sendControlChange(0, 9, map(v, 0, 1023, 0, 127));
      }
    } break;
    case 2: { int v = pot3.readPotChanged();
      if (v >= 0) {
        updateVolume(v);
        Serial.print("Volume "); Serial.println(v);
        midi.sendControlChange(0, 10, map(v, 0, 1023, 0, 127));
      }
    } break;
    case 3: { int v = btn.readChanged();
      if (v >= 0) handleButton(v);
    } break;
  }
  index = (index + 1) % 4;
}
