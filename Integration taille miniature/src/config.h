#ifndef CONFIG_H
#define CONFIG_H

#define NB_CANAUX 3
#define NB_SONARS 2

//------------- BAUDRATES ------------//
#define BAUD_USB        9600     
#define BAUD_Xsens      115200   
#define BAUD_RPI        115200   
//------------------------------------//

//======= SELECTION TEST UNITAIRE =======//
// Décommenter UNE seule ligne pour isoler une tâche.
// Tout commenté = mode normal (toutes les tâches).
//#define TEST_XSENS
//#define TEST_SONAR
//#define TEST_FOILS
 
#if   defined(TEST_XSENS)
  #define RUN_XSENS 1
  #define RUN_SONAR 0
  #define RUN_FOILS 0
#elif defined(TEST_SONAR)
  #define RUN_XSENS 0
  #define RUN_SONAR 1
  #define RUN_FOILS 0
#elif defined(TEST_FOILS)
  #define RUN_XSENS 0
  #define RUN_SONAR 1   // les foils ont besoin des sonars
  #define RUN_FOILS 1
#else  // mode normal
  #define RUN_XSENS 1
  #define RUN_SONAR 1
  #define RUN_FOILS 1
#endif
//========================================//


//-------- PERIODE DES TACHES --------//
#define PERIODE_Xsens_MS            10   // 100 Hz
#define PERIODE_height_control_MS   20   // 50 Hz
#define PERIODE_SONAR_MS            20   // 50 Hz
#define PERIODE_RPI_MS              500  // 2  Hz
#define PERIODE_PROP_MS             20   // 50 Hz
#define PERIODE_SM_MS               20   // 50 Hz
//------------------------------------//


//---------- STACKS FREERTOS ---------//
#define STACK_XSENS     2048   // 8KB
#define STACK_FOILS     2048   // 8KB
#define STACK_SONAR     2048   // 8KB
#define STACK_RPI       2048   // 8KB
#define STACK_PROP      2048
#define STACK_SM        2048
//------------------------------------//


//-------- PRIORITES FREERTOS --------//
#define PRIO_SM         4
#define PRIO_XSENS      3
#define PRIO_FOILS      3
#define PRIO_PROP       2
#define PRIO_SONAR      2
#define PRIO_RPI        1
//------------------------------------//


//-------------- SONARS --------------//
#define SONAR_VREF          3.3f
#define SONAR_ADC_BITS      1024.0f
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


//----------------------- SERVOS ----------------------------------//
#define SERVO_ALPHA                    0.2f
// SERVO AVANT
#define SERVO_AVANT_NEUTRAL            90
#define SERVO_AVANT_MIN                80
#define SERVO_AVANT_MAX                100
// SERVO ARRIERE GAUCHE
#define SERVO_ARRIERE_GAUCHE_NEUTRAL   90
#define SERVO_ARRIERE_GAUCHE_MIN       80
#define SERVO_ARRIERE_GAUCHE_MAX       100
// SERVO ARRIERE DROIT
#define SERVO_ARRIERE_DROIT_NEUTRAL    90
#define SERVO_ARRIERE_DROIT_MIN        80
#define SERVO_ARRIERE_DROIT_MAX        100

// Physical travel limits (tune after installation)
#define FOIL_ANGLE_MIN_DEG   25.0f       // Full dive
#define FOIL_ANGLE_MAX_DEG   165.0f      // Full lift
#define FOIL_ANGLE_NEUTRAL   90.0f       // Flat

//CALIBRATION SORTIE SERVOS FOILS (servos Miniature) 
#define FOIL_MIN_DEG        0
#define FOIL_MAX_DEG        180
#define FOIL_PULSE_MIN_US   1000
#define FOIL_PULSE_MAX_US   2000
//-------------------------------------------------------------------//


//--------- HAUTEUR CONTROLE ---------//
#define H_DISTANCE_REF_AVANT            32.5f
#define H_DISTANCE_REF_ARRIERE_GAUCHE   32.5f
#define H_DISTANCE_REF_ARRIERE_DROIT    32.5f

#define H_DEADBAND_ERR          1.0f
#define H_DEADBAND_DERIV        0.3f
#define H_INTEGRAL_MAX          20.0f
#define H_INTEGRAL_MIN          -20.0f

// Grande Erreur
#define H_GRANDE_ERREUR         10.0f
#define H_KP_HAUT               1.6f
#define H_KI_HAUT               0.0f
#define H_KD_HAUT               1.9f

// Moyenne Erreur
#define H_MOYENNE_ERREUR        5.0f
#define H_KP_MID                0.6f
#define H_KI_MID                0.05f
#define H_KD_MID                0.4f

// Petite Erreur
#define H_KP_BAS                0.4f
#define H_KI_BAS                0.1f
#define H_KD_BAS                0.2f
//------------------------------------//


//----------- PITCH CONTROLE -------//
#define P_REF_DEG             0.0f
#define P_DEADBAND_ERR        2.0f   
#define P_DEADBAND_DERIV      0.3f    
#define P_INTEGRAL_MIN       -20.0f
#define P_INTEGRAL_MAX        20.0f
 
// Grande erreur (>30°)
#define P_GRANDE_ERREUR       30.0f
#define P_KP_HAUT             2.0f
#define P_KI_HAUT             0.0f
#define P_KD_HAUT             1.0f

// Moyenne erreur (>20°)
#define P_MOYENNE_ERREUR      20.0f
#define P_KP_MID              1.0f
#define P_KI_MID              0.05f
#define P_KD_MID              0.5f

// Petite erreur (<20°)
#define P_KP_BAS              0.5f
#define P_KI_BAS              0.1f
#define P_KD_BAS              0.2f
//------------------------------------//


//---------- ROLL CONTROLE ---------//
#define R_REF_DEG             0.0f    // A changer par la consigne du volant !!!
#define R_DEADBAND_ERR        1.0f
#define R_DEADBAND_DERIV      0.3f
#define R_INTEGRAL_MAX        20.0f
#define R_INTEGRAL_MIN       -20.0f

// Grande Erreur
#define R_GRANDE_ERREUR       20.0f
#define R_KP_HAUT             1.6f
#define R_KI_HAUT             0.0f
#define R_KD_HAUT             1.9f

// Moyenne Erreur
#define R_MOYENNE_ERREUR      10.0f
#define R_KP_MID              0.6f
#define R_KI_MID              0.05f
#define R_KD_MID              0.4f

// Petite Erreur
#define R_KP_BAS              0.4f
#define R_KI_BAS              0.1f
#define R_KD_BAS              0.2f
//------------------------------------//

//-------------- RÉCEPTEUR FLYSKY (PWM, 5 canaux) -------------------//
#define RC_PULSE_MIN_US   1000
#define RC_PULSE_MAX_US   2000
#define RC_PULSE_MID_US   1500
#define RC_DEADBAND_US    30            // ±30 µs autour du centre
#define RC_TIMEOUT_MS     500           // pas d'impulsion => failsafe
//-------------------------------------------------------------------//


//------------------- ESC (2 moteurs de propulsion) ----------------//
#define ESC_PWM_FREQ_HZ       50
#define ESC_PULSE_MIN_US      1000      // pleine marche arrière / désarmé
#define ESC_PULSE_NEUTRAL_US  1500      // stop
#define ESC_PULSE_MAX_US      2000      // pleine marche avant
#define ESC_ARM_PULSE_US      ESC_PULSE_MIN_US
#define ESC_ARM_DELAY_MS      2000      // maintien du pulse bas pour armer
//-------------------------------------------------------------------//


//------------------- SERVO DIRECTION (rudder) ---------------------//
#define RUDDER_PULSE_MIN_US 1000
#define RUDDER_PULSE_MAX_US 2000
#define RUDDER_CENTER_US    1500
//-------------------------------------------------------------------//


//---------------- SEUILS MACHINE D'ÉTAT (fraction throttle) -------//
#define SM_SPEED_THRESHOLD_HIGH  0.10f   // AVANCE   -> CONTROLE
#define SM_SPEED_THRESHOLD_LOW   0.085f  // CONTROLE -> AVANCE

// Sécurité foils : 0 = foils neutres (bring-up moteurs seuls) ;
//                  1 = PID foils actif en CONTROLE (si tu ajoutes le gating)
#define MINIATURE_FOILS_ENABLED  1
//-------------------------------------------------------------------//



//----------- PINS TEENSY ------------//
#define PIN_RC_CH1                  20   // Direction (rudder)
#define PIN_RC_CH3                  22   // Throttle
#define PIN_RC_CH5                  24   // Switch A (armement)
#define PIN_RC_CH6                  25   // Knob VrA (hauteur)
#define PIN_RC_CH7                  26   // Switch C (marche AR)
#define PIN_ESC_LEFT                2   
#define PIN_ESC_RIGHT               3   
#define PIN_SERVO_RUDDER            4    

//#define RX1                         0
//#define TX1                         1
#define PWM_Servo_Avant             5
#define PWM_Servo_Arriere_Gauche    6
#define PWM_Servo_Arriere_Droit     7
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
//#define Analog_Sonar_Arriere_Droit  16
//#define PIN_17                      17
//#define PIN_18                      18
#define Relay_Control               19
#define TX_Xsens                    17
#define RX_Xsens                    16
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

