#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <MadgwickAHRS.h>

// ===== PID =====
float distance_ref = 30;
float prev_target;
float error = 0;
float prevError = 0;
float integral = 0;
float derivative = 0;

// Gains (seront modifiés par fuzzy)
float Kp = 1.0;
float Ki = 0.0;
float Kd = 0.3;

// ===== Temps =====
unsigned long lastTime = 0;
float dt = 0.00; 

// ===== Servo =====
Servo myservo;
float cmd;
float servo_cmd = 65;
const int servoNeutral = 65;
const int servoMin = 50;
const int servoMax = 80;

// ===== Sonar UGT207 =====
const int sonarPin = A0;
int raw;
float voltage;
float distance;
float distance_filt;
const float alpha = 0.2;

/*
// ===== Sonar HC-SR04 =====
float duration;
float distance;
const int trigPin = 6;
const int echoPin = 7;

// ===== IMU =====
const int MPU = 0x68;
float AccX, AccY, AccZ;
Madgwick filter;

// =======================================================
// ================= Lecture Sonar =======================
// =======================================================
void GetDistance(){
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = (duration*0.0343)/2;
}

// =======================================================
// =================== Lecture IMU =======================
// =======================================================
void lectureIMU(){
  // ---------- LECTURE ACC ----------
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 6, true);

  AccX = (Wire.read() << 8 | Wire.read()) / 16384.0;
  AccY = (Wire.read() << 8 | Wire.read()) / 16384.0;
  AccZ = (Wire.read() << 8 | Wire.read()) / 16384.0;

  // ---------- LECTURE GYRO ----------
  
  Wire.beginTransmission(MPU);
  Wire.write(0x43);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 6, true);

  GyroX = (Wire.read() << 8 | Wire.read()) / 131.0;
  GyroY = (Wire.read() << 8 | Wire.read()) / 131.0;
  GyroZ = (Wire.read() << 8 | Wire.read()) / 131.0;
  
*/
// =======================================================
// ================= Lecture Sonar UGT207 =======================
// =======================================================
void readUGT207() {

  raw = analogRead(sonarPin);
  voltage = raw * (5.0 / 1023.0);

  distance      = ((voltage - 0.97) / 4.0) * 200.0 + 20.0;
  distance_filt = alpha * distance + (1 - alpha) * distance_filt;

  distance_filt = constrain(distance_filt, 20.0, 220.0);

  Serial.print("Tensipon: ");
  Serial.print(voltage);
  Serial.print(" V");
  Serial.print('\t');

  Serial.print("Distance: ");
  Serial.print(distance_filt);
  Serial.println(" cm");

  delay(50);
}
// =======================================================
// ===================== Fuzzy ===========================
// =======================================================
void fuzzyGainTuning(float e, float de) {
  float absE = abs(e);
  //float absDE = abs(de);

  if (absE > 10) {          // grosse erreur
      Kp = 2.5;
      Ki = 0.0;
      Kd = 1.5;
  }
  else if (absE > 5) {      // erreur moyenne
      Kp = 1.5;
      Ki = 0.02;
      Kd = 0.8;
  }
  else {                    // proche consigne
      Kp = 0.6;
      Ki = 0.05;
      Kd = 0.3;
  }
}

// =======================================================
// ===================== Setup ===========================
// =======================================================
void setup() {
  Serial.begin(9600);
  Wire.begin();

  /*
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  Wire.beginTransmission(MPU);
  Wire.write(0x6B);                  
  Wire.write(0x00);                  
  Wire.endTransmission(true);
  filter.begin(100);
  */

  myservo.attach(9);
  myservo.write(servoNeutral);

  lastTime = millis();     
  delay(20);
}

// =======================================================
// ======================== LOOP =========================
// =======================================================
void loop() {
  // ---------- TEMPS ----------
  unsigned long now = millis();
  dt = (now - lastTime) / 1000.0;
  if (dt <= 0) return;
  lastTime = now;

  // ---------- LECTURE IMU -----------
  //lectureIMU();

  // ---------- LECTURE SONAR ----------
  //GetDistance();
  readUGT207();

  // ---------- ERREUR ----------
  error = distance_ref - distance_filt;

  // ---------- DEADBAND ----------
  if (abs(error) < 1) error = 0;

  // ---------- INTEGRALE ----------
  integral += error * dt;
  integral = constrain(integral, -20, 20);

  // ---------- DERIVEE ----------
  float derivative = -(distance_filt - prev_target) / dt;
  prev_target = distance_filt;
  
  // ---------- FUZZY ----------
  fuzzyGainTuning(error, derivative);

  // ---------- PID ----------
  float output = Kp * error + Ki * integral + Kd * derivative; 
  
  // ---------- SERVO (limitation vitesse) ----------
  servo_cmd = servoNeutral - output;
  cmd = servo_cmd;
  cmd = constrain(cmd, servoMin, servoMax);
  myservo.write(cmd);

  // ---------- DEBUG ----------
  Serial.print("Distance: ");
  Serial.print(distance_filt);
  Serial.print(" | Servo: ");
  Serial.print(cmd);
  Serial.print(" | Kp: ");
  Serial.print(Kp);
  Serial.print(" | Kd: ");
  Serial.print(Kd);
  Serial.print(" | derive: ");
  Serial.println(derivative);
}

// =======================================================
// ====== MAIN (pour PlatformIO / Arduino Mega) ==========
// =======================================================
int main() {
    init();       
    setup();
    while (true) {
        loop();
    }
}