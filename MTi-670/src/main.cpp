#include <Arduino.h>
#include "MTi670.h"

// Serial1 = RX pin 0, TX pin 1 on Teensy 4.1
MTi670 imu(Serial8, 115200);

void setup() {
    Serial.begin(115200);
    // No delay — listen for WakeUp immediately
    imu.begin(5000);
    Serial.println("[Setup] Done.");
}

void loop() {
    if (imu.update()) {
        imu.printData();
    }
    
    // // Debug temporaire
    // static uint32_t last = 0;
    // if (millis() - last > 1000) {
    //     Serial.printf("att:%d pos:%d vel:%d\n", 
    //         imu.attitude().valid,
    //         imu.position().valid, 
    //         imu.velocity().valid);
    //     last = millis();
    // }
}
// #include <Arduino.h>

// void setup() {
//     Serial.begin(115200);
//     Serial8.begin(115200);
//     Serial.println("Listening...");
// }

// void loop() {
//     while (Serial8.available()) {
//         Serial.printf("0x%02X ", Serial8.read());
//     }
// }