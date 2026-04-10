#pragma once

// ============================================================
//  Config.h  –  Central configuration for RC Hydrofoil
// ============================================================

// ----------------------------------------------------------------
// ■  PIN MAP
// ----------------------------------------------------------------

// --- SPI bus (hardware SPI, shared) ---
// MOSI → 11  |  MISO → 12  |  SCK → 13  (Teensy 4.1 default)
#define PIN_IMU_CS          10

// --- Sonar HC-SR04 ---
#define PIN_SONAR_TRIG      14 // fil blanc
#define PIN_SONAR_ECHO      15 // fil vert
// FUTURE PIN POUR UGT 14 15 16
// FUTURE PIN POUR I/O 33 34 35

//--- Sonar UGT207 ---
#define PIN_UGT_ANALOG       16 // fil a determiner
#define PIN_UGT_IO           35 // fil a determiner 

// --- ESC (PWM) ---
#define PIN_ESC_LEFT        2 // fil vert
#define PIN_ESC_RIGHT       3 // fil bleu 

// --- Servo: direction (rudder) ---
#define PIN_SERVO_RUDDER    4 // fil orange

// --- Servos: hydrofoils ---
#define PIN_FOIL_FRONT      5 // fil vert
#define PIN_FOIL_REAR_LEFT  6 // fil rouge
#define PIN_FOIL_REAR_RIGHT 7 // fil jaune

// --- FlySky receiver (PWM input, 5 channels used) ---
#define PIN_RC_CH1          20   // Direction
#define PIN_RC_CH3          22   // Throttle
#define PIN_RC_CH5          24   // Switch A
#define PIN_RC_CH6          25   // Knob VrA
#define PIN_RC_CH7          26   // Switch D


// ----------------------------------------------------------------
// ■  IMU  (MPU6500 via SPI)
// ----------------------------------------------------------------
#define IMU_SPI_SPEED       8000000UL   // 8 MHz (max 20 MHz)

// Gyro full-scale range: DPS_250 | DPS_500 | DPS_1000 | DPS_2000
#define IMU_GYRO_RANGE      GyroRange::DPS_500

// Accel full-scale range: G2 | G4 | G8 | G16
#define IMU_ACCEL_RANGE     AccelRange::G4

// Complementary filter: 0 = pure gyro, 1 = pure accel
#define IMU_ALPHA           0.02f

// Static calibration sample count (sensor must be still)
#define IMU_CAL_SAMPLES     500

// Update rate
#define IMU_UPDATE_HZ       500
#define IMU_PERIOD_US       (1000000UL / IMU_UPDATE_HZ)


// ----------------------------------------------------------------
// ■  SONAR  (HC-SR04)
// ----------------------------------------------------------------
#define SONAR_UPDATE_HZ     20           // [Hz]  max ~25 Hz reliable
#define SONAR_PERIOD_US     (1000000UL / SONAR_UPDATE_HZ)
#define SONAR_MIN_CM        2.0f         // Below = invalid
#define SONAR_MAX_CM        400.0f       // Above = invalid
#define SONAR_FILTER_ALPHA  0.25f        // Low-pass: 0=frozen, 1=raw

// ----------------------------------------------------------------
// ■  SONAR  (UGT)
// ----------------------------------------------------------------

#define UGT_ADC_RESOLUTION 10 // 10 bit de resolution (0-1023)
#define UGT_ALPHA          0.1f // facteur alpha de filtrage 
#define UGT_V_REF          3.0f // tension de reference  
#define UGT_UPDATE_HZ      10


// ----------------------------------------------------------------
// ■  ESC / MOTORS
// ----------------------------------------------------------------
#define ESC_PWM_FREQ_HZ      50          // Standard 50 Hz ESC signal
#define ESC_PULSE_MIN_US     1000        // Full reverse / disarmed
#define ESC_PULSE_NEUTRAL_US 1500        // Stop
#define ESC_PULSE_MAX_US     2000        // Full forward
#define ESC_ARM_PULSE_US     ESC_PULSE_MIN_US
#define ESC_ARM_DELAY_MS     2000        // Hold low pulse to arm


// ----------------------------------------------------------------
// ■  SERVOS
// ----------------------------------------------------------------
// Rudder (direction servo)
#define RUDDER_PWM_FREQ_HZ   50
#define RUDDER_PULSE_MIN_US  1000
#define RUDDER_PULSE_MAX_US  2000
#define RUDDER_CENTER_US     1500
#define RUDDER_MIN_DEG       0
#define RUDDER_MAX_DEG       180

// Hydrofoil servos (same spec for all three)
#define FOIL_PWM_FREQ_HZ     50
#define FOIL_PULSE_MIN_US    1000
#define FOIL_PULSE_MAX_US    2000
#define FOIL_CENTER_US       1500        // Foil flat/neutral
#define FOIL_MIN_DEG         0
#define FOIL_MAX_DEG         180

// Physical travel limits (tune after installation)
#define FOIL_ANGLE_MIN_DEG   25.0f       // Full dive
#define FOIL_ANGLE_MAX_DEG   165.0f      // Full lift
#define FOIL_ANGLE_NEUTRAL   90.0f       // Flat


// ----------------------------------------------------------------
// ■  RC RECEIVER  (FlySky, PWM — 5 channels)
// ----------------------------------------------------------------
#define RC_PULSE_MIN_US      1000
#define RC_PULSE_MAX_US      2000
#define RC_PULSE_MID_US      1500
#define RC_DEADBAND_US       30          // ±30 µs around centre
#define RC_TIMEOUT_MS        500         // No pulse → failsafe


// ----------------------------------------------------------------
// ■  STATE MACHINE
// ----------------------------------------------------------------
// Speed thresholds for Run sub-state transitions
// Expressed as fraction of max throttle (0.0 – 1.0)
#define SM_SPEED_THRESHOLD_HIGH   0.10f  // Avance → Contrôle
#define SM_SPEED_THRESHOLD_LOW    0.085f // Contrôle → Avance


// ----------------------------------------------------------------
// ■  HEIGHT CONTROLLER  (active in CONTROLE state)
// ----------------------------------------------------------------
// Height setpoint mode
// 1 = fixed (HEIGHT_SETPOINT_DEFAULT_CM)
// 0 = adjustable via knob CH6
#define HEIGHT_SETPOINT_FIXED  1

#define HEIGHT_SETPOINT_DEFAULT_CM  32.0f
#define HEIGHT_SETPOINT_MIN_CM       5.0f
#define HEIGHT_SETPOINT_MAX_CM      25.0f

#define HEIGHT_KP    4.0f
#define HEIGHT_KI    1.75f
#define HEIGHT_KD    1.5f
#define HEIGHT_I_MAX 25.0f              // Anti-windup clamp [deg]


// ----------------------------------------------------------------
// ■  ROLL CONTROLLER  (active in AVANCE & CONTROLE state)
// ----------------------------------------------------------------
#define ROLL_SETPOINT_DEG   0.0f        // Keep wings level

#define ROLL_KP    2.0f
#define ROLL_KI    0.750f
#define ROLL_KD    0.25f
#define ROLL_I_MAX 15.0f                // Anti-windup clamp [deg]


// ----------------------------------------------------------------
// ■  SERIAL DEBUG
// ----------------------------------------------------------------
#define SERIAL_BAUD      115200
#define DEBUG_PRINT_HZ   10             // [Hz]
#define DEBUG_PERIOD_MS  (1000UL / DEBUG_PRINT_HZ)