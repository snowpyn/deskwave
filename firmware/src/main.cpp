#include <Arduino.h>

#include "deskwave_version.h"

void setup() {
    Serial.begin(115200);
    Serial.printf("DeskWave %s\n", deskwave::kVersion);
}

void loop() {
    delay(1000);
}
