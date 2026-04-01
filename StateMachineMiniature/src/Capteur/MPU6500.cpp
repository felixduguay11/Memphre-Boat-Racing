// ============================================================
//  IMU.cpp  –  MPU6500 driver implementation
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include "MPU6500.h"

// ----------------------------------------------------------------
// Scale factor LUTs
// ----------------------------------------------------------------
static constexpr float GYRO_SCALE_LUT[4]  = {
    131.0f,   // ±250 dps   → 131  LSB/(deg/s)
     65.5f,   // ±500 dps   →  65.5 LSB/(deg/s)
     32.8f,   // ±1000 dps  →  32.8 LSB/(deg/s)
     16.4f    // ±2000 dps  →  16.4 LSB/(deg/s)
};

static constexpr float ACCEL_SCALE_LUT[4] = {
    16384.0f, // ±2 g
     8192.0f, // ±4 g
     4096.0f, // ±8 g
     2048.0f  // ±16 g
};

// ----------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------
IMU::IMU(uint8_t csPin)
    : _csPin(csPin),
      _spiSettings(IMU_SPI_SPEED, MSBFIRST, SPI_MODE3)
{}

// ----------------------------------------------------------------
// begin()
// ----------------------------------------------------------------
bool IMU::begin(GyroRange gyroRange, AccelRange accelRange)
{
    pinMode(_csPin, OUTPUT);
    csHigh();
    SPI.begin();

    delay(100); // Let MPU6500 power up

    // Reset device
    writeRegister(MPU6500_REG::PWR_MGMT_1, 0x80);
    delay(100);

    // Wake up, select auto-select best clock source
    writeRegister(MPU6500_REG::PWR_MGMT_1, 0x01);
    delay(10);

    // Verify WHO_AM_I
    uint8_t whoAmI = readRegister(MPU6500_REG::WHO_AM_I);
    if (whoAmI != MPU6500_REG::EXPECTED_ID) {
        Serial.printf("[IMU] WHO_AM_I mismatch: got 0x%02X, expected 0x%02X\n",
                      whoAmI, MPU6500_REG::EXPECTED_ID);
        return false;
    }

    // Enable all axes
    writeRegister(MPU6500_REG::PWR_MGMT_2, 0x00);

    // Sample rate divider → 1 kHz / (1 + 0) = 1 kHz
    writeRegister(MPU6500_REG::SMPLRT_DIV, 0x00);

    // DLPF config: bandwidth 92 Hz gyro / 98 Hz accel (register CONFIG = 2)
    writeRegister(MPU6500_REG::CONFIG, 0x02);

    // Gyro range
    writeRegister(MPU6500_REG::GYRO_CONFIG, static_cast<uint8_t>(gyroRange));
    uint8_t gIdx = (static_cast<uint8_t>(gyroRange) >> 3) & 0x03;
    _gyroScale = GYRO_SCALE_LUT[gIdx];

    // Accel range
    writeRegister(MPU6500_REG::ACCEL_CONFIG, static_cast<uint8_t>(accelRange));
    uint8_t aIdx = (static_cast<uint8_t>(accelRange) >> 3) & 0x03;
    _accelScale = ACCEL_SCALE_LUT[aIdx];

    // Accel DLPF: bandwidth 99 Hz
    writeRegister(MPU6500_REG::ACCEL_CONFIG2, 0x02);

    _lastUpdate = micros();
    _ready = true;

    Serial.println("[IMU] MPU6500 initialised OK");
    return true;
}

// ----------------------------------------------------------------
// calibrate()  – keep sensor still!
// ----------------------------------------------------------------
void IMU::calibrate(uint16_t samples)
{
    Serial.println("[IMU] Calibrating – keep sensor still...");

    double sumAx = 0, sumAy = 0, sumAz = 0;
    double sumGx = 0, sumGy = 0, sumGz = 0;

    for (uint16_t i = 0; i < samples; i++) {
        readAllRegisters();
        sumAx += _raw.ax;
        sumAy += _raw.ay;
        sumAz += _raw.az;
        sumGx += _raw.gx;
        sumGy += _raw.gy;
        sumGz += _raw.gz;
        delay(2); // ~500 Hz collection rate
    }

    // Gyro bias: mean raw counts → deg/s
    _cal.gyro_bias_x = (sumGx / samples) / _gyroScale;
    _cal.gyro_bias_y = (sumGy / samples) / _gyroScale;
    _cal.gyro_bias_z = (sumGz / samples) / _gyroScale;

    // Accel bias: remove 1g on whichever axis is vertical
    _cal.accel_bias_x = (sumAx / samples) / _accelScale;
    _cal.accel_bias_y = (sumAy / samples) / _accelScale;
    // Z should read +1 g when flat – subtract 1 g from bias
    _cal.accel_bias_z = ((sumAz / samples) / _accelScale) - 1.0f;

    Serial.printf("[IMU] Cal done. Gyro bias (dps): %.4f  %.4f  %.4f\n",
                  _cal.gyro_bias_x, _cal.gyro_bias_y, _cal.gyro_bias_z);
    Serial.printf("[IMU] Accel bias (g) :  %.4f  %.4f  %.4f\n",
                  _cal.accel_bias_x, _cal.accel_bias_y, _cal.accel_bias_z);
}

// ----------------------------------------------------------------
// update()
// ----------------------------------------------------------------
bool IMU::update()
{
    if (!_ready) return false;

    uint32_t now = micros();
    float dt = (now - _lastUpdate) * 1e-6f;  // seconds
    _lastUpdate = now;

    // Guard against absurd dt (first call, timer wrap-around)
    if (dt <= 0.0f || dt > 0.5f) dt = 0.002f;

    readAllRegisters();
    applyCalibrationAndScale();
    runComplementaryFilter(dt);

    _data.timestamp_us = now;
    return true;
}

// ----------------------------------------------------------------
// readAllRegisters()  – burst-read 14 bytes starting at ACCEL_XOUT_H
// ----------------------------------------------------------------
void IMU::readAllRegisters()
{
    uint8_t buf[14];

    SPI.beginTransaction(_spiSettings);
    csLow();
    SPI.transfer(MPU6500_REG::ACCEL_XOUT_H | MPU6500_REG::READ_FLAG);
    for (uint8_t i = 0; i < 14; i++) {
        buf[i] = SPI.transfer(0x00);
    }
    csHigh();
    SPI.endTransaction();

    _raw.ax   = (int16_t)((buf[0]  << 8) | buf[1]);
    _raw.ay   = (int16_t)((buf[2]  << 8) | buf[3]);
    _raw.az   = (int16_t)((buf[4]  << 8) | buf[5]);
    _raw.temp = (int16_t)((buf[6]  << 8) | buf[7]);
    _raw.gx   = (int16_t)((buf[8]  << 8) | buf[9]);
    _raw.gy   = (int16_t)((buf[10] << 8) | buf[11]);
    _raw.gz   = (int16_t)((buf[12] << 8) | buf[13]);
}

// ----------------------------------------------------------------
// applyCalibrationAndScale()
// ----------------------------------------------------------------
void IMU::applyCalibrationAndScale()
{
    _data.accel_x_g  = (_raw.ax / _accelScale) - _cal.accel_bias_x;
    _data.accel_y_g  = (_raw.ay / _accelScale) - _cal.accel_bias_y;
    _data.accel_z_g  = (_raw.az / _accelScale) - _cal.accel_bias_z;

    _data.gyro_x_dps = (_raw.gx / _gyroScale)  - _cal.gyro_bias_x;
    _data.gyro_y_dps = (_raw.gy / _gyroScale)  - _cal.gyro_bias_y;
    _data.gyro_z_dps = (_raw.gz / _gyroScale)  - _cal.gyro_bias_z;

    // MPU6500 temp formula from datasheet
    _data.temp_c = (static_cast<float>(_raw.temp) / 321.0f) + 21.0f;
}

// ----------------------------------------------------------------
// runComplementaryFilter()
// ----------------------------------------------------------------
void IMU::runComplementaryFilter(float dt)
{
    // Accelerometer-derived angles (noisy but drift-free)
    float accel_roll  = atan2f(_data.accel_y_g, _data.accel_z_g) * RAD_TO_DEG;
    float accel_pitch = atan2f(-_data.accel_x_g,
                                sqrtf(_data.accel_y_g * _data.accel_y_g +
                                      _data.accel_z_g * _data.accel_z_g)) * RAD_TO_DEG;

    // Gyroscope integration (accurate short-term, drifts long-term)
    _data.roll_deg  += _data.gyro_x_dps * dt;
    _data.pitch_deg += _data.gyro_y_dps * dt;

    // Fuse: trust gyro heavily, correct slow drift with accel
    _data.roll_deg  = (1.0f - IMU_ALPHA) * _data.roll_deg  + IMU_ALPHA * accel_roll;
    _data.pitch_deg = (1.0f - IMU_ALPHA) * _data.pitch_deg + IMU_ALPHA * accel_pitch;
}

// ----------------------------------------------------------------
// readRegister() / writeRegister()
// ----------------------------------------------------------------
uint8_t IMU::readRegister(uint8_t reg)
{
    SPI.beginTransaction(_spiSettings);
    csLow();
    SPI.transfer(reg | MPU6500_REG::READ_FLAG);
    uint8_t val = SPI.transfer(0x00);
    csHigh();
    SPI.endTransaction();
    return val;
}

void IMU::writeRegister(uint8_t reg, uint8_t value)
{
    SPI.beginTransaction(_spiSettings);
    csLow();
    SPI.transfer(reg & ~MPU6500_REG::READ_FLAG);
    SPI.transfer(value);
    csHigh();
    SPI.endTransaction();
}

void IMU::printDebug() const
{
    Serial.printf("[IMU] Roll: %6.1f deg  Pitch: %6.1f deg  Temp: %.1f C\n",
    _data.roll_deg, _data.pitch_deg, _data.temp_c);
}