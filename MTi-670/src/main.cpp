#include <Arduino.h>
#include "MTi670.h"

// Serial1 = RX pin 0, TX pin 1 on Teensy 4.1
MTi670 imu(Serial1, 115200);

void setup() {
    Serial.begin(115200);

    delay(10000);
    Serial.println("[Setup] Starting MTi-670...");
    imu.begin();
    Serial.println("[Setup] Done.");

}

void loop() {

    if (imu.update()) {
          imu.printData();

          // ── Use values directly ───────────────────────────────────────────────
          imu.attitude().roll;
          imu.attitude().pitch;
          imu.attitude().yaw;

          imu.position().lat;
          imu.position().lon;

          imu.velocity().speed; // horizontal speed (m/s)
        //   imu.velocity().vx / vy / vz
      }
}
// #include <Arduino.h>

// void setup() {
//     Serial.begin(115200);
//     Serial1.begin(115200);
//     delay(2000);
//     Serial.println("Listening...");
// }

// void loop() {
//     while (Serial1.available()) {
//         uint8_t b = Serial1.read();
//         Serial.printf("0x%02X ", b);
//     }
// }