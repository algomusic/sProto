#include "M16.h"

void setup() {
  // put your setup code here, to run once:
  seti2sPins(38, 39, 40, -1); // bck, ws, data_out, data_in
  audioStart();
}

void loop() {
  // put your main code here, to run repeatedly:
  
}

void audioUpdate() {
  int32_t noise = random(-32000, 32001);
  audioBlockWrite(noise, noise);
}
