#pragma once

//  Wiring (Teensy 4.1 hardware SPI – MOSI/MISO/SCK = pins 11/12/13):
//    CS   → any digital pin (IMU_CS_PIN, default 10)
//    MOSI → pin 11
//    MISO → pin 12
//    SCK  → pin 13
//    VCC  → 3.3 V
//    GND  → GND
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <Config.h>

// ----------------------------------------------------------------
// MPU6500 register map (subset)
// ----------------------------------------------------------------
namespace MPU6500_REG {
    static constexpr uint8_t SMPLRT_DIV   = 0x19;
    static constexpr uint8_t CONFIG       = 0x1A;
    static constexpr uint8_t GYRO_CONFIG  = 0x1B;
    static constexpr uint8_t ACCEL_CONFIG = 0x1C;
    static constexpr uint8_t ACCEL_CONFIG2= 0x1D;
    static constexpr uint8_t ACCEL_XOUT_H = 0x3B;
    static constexpr uint8_t TEMP_OUT_H   = 0x41;
    static constexpr uint8_t GYRO_XOUT_H  = 0x43;
    static constexpr uint8_t PWR_MGMT_1   = 0x6B;
    static constexpr uint8_t PWR_MGMT_2   = 0x6C;
    static constexpr uint8_t WHO_AM_I     = 0x75;
    static constexpr uint8_t READ_FLAG    = 0x80;
    static constexpr uint8_t EXPECTED_ID  = 0x70;  // MPU6500
}

// ----------------------------------------------------------------
// Gyro and accelerometer full-scale range options
// ----------------------------------------------------------------
enum class GyroRange : uint8_t {
    DPS_250  = 0x00,
    DPS_500  = 0x08,
    DPS_1000 = 0x10,
    DPS_2000 = 0x18
};

enum class AccelRange : uint8_t {
    G2  = 0x00,
    G4  = 0x08,
    G8  = 0x10,
    G16 = 0x18
};

// ----------------------------------------------------------------
// Raw sensor data
// ----------------------------------------------------------------
struct ImuRaw {
    int16_t ax, ay, az;   // Accelerometer raw counts
    int16_t gx, gy, gz;   // Gyroscope raw counts
    int16_t temp;          // Temperature raw counts
};

// ----------------------------------------------------------------
// Processed / calibrated data
// ----------------------------------------------------------------
struct ImuData {
    // Physical values
    float accel_x_g,  accel_y_g,  accel_z_g;   // [g]
    float gyro_x_dps, gyro_y_dps, gyro_z_dps;  // [deg/s]
    float temp_c;                                // [°C]

    // Orientation estimated by complementary filter
    float roll_deg;   // Rotation around X axis  (+right wing down)
    float pitch_deg;  // Rotation around Y axis  (+nose up)

    // Timestamp of last successful read
    uint32_t timestamp_us;
};

// ----------------------------------------------------------------
// Calibration offsets (filled by calibrate())
// ----------------------------------------------------------------
struct ImuCalibration {
    float accel_bias_x = 0.0f;
    float accel_bias_y = 0.0f;
    float accel_bias_z = 0.0f;
    float gyro_bias_x  = 0.0f;
    float gyro_bias_y  = 0.0f;
    float gyro_bias_z  = 0.0f;
};

// ================================================================
//  IMU  class
// ================================================================
class IMU {
public:
    // ----------------------------------------------------------
    // Construction
    // ----------------------------------------------------------
    explicit IMU(uint8_t csPin = PIN_IMU_CS);

    // ----------------------------------------------------------
    // Lifecycle
    // ----------------------------------------------------------

    /**
     * @brief  Initialise SPI and MPU6500.
     * @param  gyroRange   Full-scale range for gyroscope.
     * @param  accelRange  Full-scale range for accelerometer.
     * @return true on success, false if WHO_AM_I check fails.
     */
    bool begin(GyroRange  gyroRange  = GyroRange::DPS_500,
               AccelRange accelRange = AccelRange::G4);

    /**
     * @brief  Collect N samples at rest and store mean offsets.
     *         Keep the sensor perfectly still during this call.
     * @param  samples  Number of samples to average (default 500).
     */
    void calibrate(uint16_t samples = 500);

    // ----------------------------------------------------------
    // Runtime
    // ----------------------------------------------------------

    /**
     * @brief  Read sensor, apply calibration, run complementary filter.
     *         Call at a fixed rate (e.g. every 2 ms / 500 Hz).
     * @return true if data is valid, false if SPI read failed.
     */
    bool update();

    // ----------------------------------------------------------
    // Accessors
    // ----------------------------------------------------------
    const ImuData&        getData()        const { return _data; }
    const ImuRaw&         getRaw()         const { return _raw;  }
    const ImuCalibration& getCalibration() const { return _cal;  }

    float getRoll()  const { return _data.roll_deg;  }
    float getPitch() const { return _data.pitch_deg; }

    bool  isReady()  const { return _ready; }

    // ----------------------------------------------------------
    // Direct register access (advanced use)
    // ----------------------------------------------------------
    uint8_t readRegister(uint8_t reg);
    void    writeRegister(uint8_t reg, uint8_t value);

private:
    uint8_t        _csPin;
    bool           _ready       = false;
    float          _gyroScale   = 1.0f;   // LSB → deg/s
    float          _accelScale  = 1.0f;   // LSB → g
    ImuRaw         _raw         = {};
    ImuData        _data        = {};
    ImuCalibration _cal         = {};
    uint32_t       _lastUpdate  = 0;

    SPISettings    _spiSettings;

    // Internal helpers
    void    readAllRegisters();
    void    applyCalibrationAndScale();
    void    runComplementaryFilter(float dt);
    void    csLow()  { digitalWriteFast(_csPin, LOW);  }
    void    csHigh() { digitalWriteFast(_csPin, HIGH); }
};