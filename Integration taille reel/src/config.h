#ifndef CONFIG_H
#define CONFIG_H

#define NB_CANAUX 3

//------------- BAUDRATES ------------//
#define BAUD_USB        9600     
#define BAUD_Xsens      115200   
#define BAUD_RPI        115200   
//------------------------------------//


//-------- PERIODE DES TACHES --------//
#define PERIODE_Xsens_MS            10   // 100Hz
#define PERIODE_height_control_MS   20   // 50Hz
#define PERIODE_SONAR_MS            20   // 50Hz
#define PERIODE_RPI_MS              500  // 2Hz
//------------------------------------//


//---------- STACKS FREERTOS ---------//
#define STACK_IMU       2048   // 8KB
#define STACK_FOILS     2048   // 8KB
#define STACK_SONAR     2048   // 8KB
#define STACK_RPI       2048   // 8KB
//------------------------------------//


//-------- PRIORITES FREERTOS --------//
#define PRIO_IMU        3
#define PRIO_FOILS      3
#define PRIO_SONAR      2
#define PRIO_RPI        1
//------------------------------------//


//-------------- SONARS --------------//
#define SONAR_VREF          3.3f
#define SONAR_ADC_BITS      4095.0f
#define SONAR_RESISTANCE    150.0f
#define SONAR_I_MIN_MA      4.0f
#define SONAR_I_MAX_MA      20.0f
#define SONAR_DIST_MIN_CM   20.0f
#define SONAR_DIST_MAX_CM   200.0f
#define SONAR_ALPHA         0.2f
#define SONAR_DISTANCE_REF_AVANT            70.0f
#define SONAR_DISTANCE_REF_ARRIERE_GAUCHE   70.0f
#define SONAR_DISTANCE_REF_ARRIERE_DROIT    70.0f
#define SONAR_INIT_DISTANCE 0.0f
#define SONAR_INIT_TIME     0.0f
//------------------------------------//


//-------------- SERVOS --------------//
#define SERVO_ALPHA                    0.2f
// SERVO AVANT
#define SERVO_AVANT_NEUTRAL            148
#define SERVO_AVANT_MIN                120 
#define SERVO_AVANT_MAX                163
// SERVO ARRIERE GAUCHE
#define SERVO_ARRIERE_GAUCHE_NEUTRAL   148
#define SERVO_ARRIERE_GAUCHE_MIN       120 
#define SERVO_ARRIERE_GAUCHE_MAX       163
// SERVO ARRIERE DROIT
#define SERVO_ARRIERE_DROIT_NEUTRAL    148
#define SERVO_ARRIERE_DROIT_MIN        120 
#define SERVO_ARRIERE_DROIT_MAX        163
//------------------------------------//


//--------- HAUTEUR CONTROLE ---------//
#define H_DISTANCE_REF_AVANT            70.0f
#define H_DISTANCE_REF_ARRIERE_GAUCHE   70.0f
#define H_DISTANCE_REF_ARRIERE_DROIT    70.0f

#define H_DEADBAND_ERR          1.0f
#define H_DEADBAND_DERIV        0.3f
#define H_ALPHA_DERIV           0.15f
#define H_INTEGRAL_MAX          20.0f
#define H_INTEGRAL_MIN          -20.0f

// Grande Erreur
#define H_KP_HAUT               1.6f
#define H_KI_HAUT               0.0f
#define H_KD_HAUT               1.9f
// Moyenne Erreur
#define H_KP_MID                0.6f
#define H_KI_MID                0.05f
#define H_KD_MID                0.4f
// Petite Erreur
#define H_KP_BAS                0.4f
#define H_KI_BAS                0.1f
#define H_KD_BAS                0.2f

#define H_INIT_INTEGRAL         0.0f
#define H_INIT_DERIVE           0.0f
#define H_INIT_OUTPUTS          0.0f
#define H_INIT_CMD              0.0f
#define H_INIT_TIME             0.0f
//------------------------------------//


//---------- ROULIS CONTROLE ---------//
#define R_DISTANCE_REF        70.0f
#define R_DEADBAND_ERR        1.0f
#define R_DEADBAND_DERIV      0.3f
#define R_ALPHA_DERIV         0.15f
#define R_INTEGRAL_MAX        20.0f
#define R_INTEGRAL_MIN       -20.0f
// Grande Erreur
#define R_KP_HAUT             1.6f
#define R_KI_HAUT             0.0f
#define R_KD_HAUT             1.9f
// Moyenne Erreur
#define R_KP_MID              0.6f
#define R_KI_MID              0.05f
#define R_PID_KD_MID          0.4f
// Petite Erreur
#define R_KP_BAS              0.4f
#define R_KI_BAS              0.1f
#define R_KD_BAS              0.2f
//------------------------------------//


//----------- PINS TEENSY ------------//
//#define RX1                         0
//#define TX1                         1
#define PWM_Servo_Avant             2
#define PWM_Servo_Arriere_Gauche    3
#define PWM_Servo_Arriere_Droit     4
//#define PIN_5                       5
//#define PIN_6                       6
#define RX2                         7
#define TX2                         8
//#define PIN_9                       9
//#define PIN_10                      10
//#define PIN_11                      11
//#define PIN_12                      12
#define LED                         13
#define Analog_Sonar_Avant          14
#define Analog_Sonar_Arriere_Gauche 15
#define Analog_Sonar_Arriere_Droit  16
//#define PIN_17                      17
//#define PIN_18                      18
#define Relay_Control               19
#define TX_Xsens                    20
#define RX_Xsens                    21
#define TX_Batt_72V                 22
#define RX_Batt_72V                 23
//#define PIN_24                      24
#define POT_Mats_Arriere            25
#define POT_Volant                  26
#define POT_Levier                  27
//#define PIN_28                      28
//#define PIN_29                      29
#define RX_Moteur                   30
#define TX_Moteur                   31
#define DC_DC_3V3                   32
#define I_O_Sonar_Avant             33
#define I_O_Sonar_Arriere_Gauche    34
#define I_O_Sonar_Arriere_Droit     35
//#define PIN_36                      36
//#define PIN_37                      37
#define I_Switch_ON                 38
#define I_But_Start                 39
#define I_F_R                       40
//#define PIN_41                      41
//------------------------------------//

#endif /* CONFIG_H */

