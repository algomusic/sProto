#include "MultiControl.h"

// sProto ESP32 Dev Module control pins
MultiControl dial1(5, 1); // GPIO, Potentiometer
MultiControl dial2(6, 1);
MultiControl dial3(7, 1);
MultiControl button(8, 2); // GPIO, Button

MultiControl pad1(1, 0); // GPIO, Touch
MultiControl pad2(2, 0);
MultiControl pad3(3, 0);
MultiControl pad4(4, 0);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("sProto MultiControl basics");
}

void loop() {
  const int dial1Value = dial1.readPot();
  const int dial2Value = dial2.readPot();
  const int dial3Value = dial3.readPot();

  Serial.print("Dials: "); Serial.print(dial1Value);
  Serial.print("  "); Serial.print(dial2Value);
  Serial.print("  "); Serial.print(dial3Value);

  Serial.print(" | Button: "); Serial.print(button.isPressed() ? "pressed" : "released");

  Serial.print(" | Pads: ");
  Serial.print(pad1.isTouched() ? "1" : "0");
  Serial.print(pad2.isTouched() ? "1" : "0");
  Serial.print(pad3.isTouched() ? "1" : "0");
  Serial.println(pad4.isTouched() ? "1" : "0");

  delay(100);
}
