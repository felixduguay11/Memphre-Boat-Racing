#ifndef CONFIG_H
#define CONFIG_H

#define NB_CANAUX 3

//=========== OPTION FOILS ===========//
// 1 = foils installés : Task_LectureSonar + Task_foils_Control
//     créées, état CONTROLE atteignable.
// 0 = sans foils : Sonar et Foils ne sont PAS créées, l'état
//     CONTROLE est inatteignable. Xsens reste actif (logging
//     GPS / attitude envoyé au Raspberry Pi).
#define FOILS_ACTIFS    0
//====================================//

//------------- BAUDRATES ------------//
#define BAUD_USB        9600     
#define BAUD_Xsens      115200   
#define BAUD_RPI        115200   
//------------------------------------//


//-------- PERIODE DES TACHES --------//
#define PERIODE_Xsens_MS            10   // 100Hz
#define PERIODE_height_control_MS   20   // 50Hz
#define PERIODE_SONAR_MS            20   // 50Hz
#define PERIODE_RPI_MS              50   // 20Hz — télémétrie JSON (Real_V1 : TLM_MS)
#define PERIODE_PILOTE_MS           20   // 50Hz
#define PERIODE_PROP_MS             20   // 50Hz — envoi consigne VESC (Real_V1 : 20 ms)
#define PERIODE_ESC_MS              5    // 200Hz — vidage du bus CAN (16 mailboxes RX étendues)
#define PERIODE_WD_MS               100  // 10Hz
#define FSM_TIMEOUT_MS              100  // sans événement pendant ce délai → pilote considéré absent
//------------------------------------//


//---------- STACKS FREERTOS ---------//
#define STACK_IMU       2048   // 8KB
#define STACK_FOILS     2048   // 8KB
#define STACK_SONAR     2048   // 8KB
#define STACK_RPI       2048   // 8KB
#define STACK_PILOTE    2048   // 8KB
#define STACK_FSM       2048   // 8KB
#define STACK_PROP      2048   // 8KB
#define STACK_ESC       2048   // 8KB
#define STACK_WD        2048   // 8KB
//------------------------------------//


//-------- PRIORITES FREERTOS --------//
// (priorités d'Architecture2.png)
#define PRIO_WD         5
#define PRIO_FSM        4
#define PRIO_PROP       3
#define PRIO_FOILS      3
#define PRIO_IMU        2
#define PRIO_SONAR      2
#define PRIO_PILOTE     2
#define PRIO_ESC        2
#define PRIO_RPI        1
//------------------------------------//


//-------------- SONARS --------------//
#define SONAR_VREF          3.3f
#define SONAR_ADC_BITS      1023.0f   // ADC 10 bits (voir ADC_RESOLUTION)
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
#define SONAR_IO_VALIDE     1         // niveau de la sortie TOR (PNP) quand le sonar est valide
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


//---------------- ADC ---------------//
// 10 bits partout (sonars + levier), comme Real_V1
#define ADC_RESOLUTION      10
//------------------------------------//


//------ PILOTE : SWITCHS + LEVIER ---//
// (repris de Real_V1)
// Switchs câblés en PULL-UP : switch fermé = LOW. Débranché ou fil
// coupé = HIGH = état le plus sûr. Donc « actif » demande un LOW.
#define SWITCH_ACTIF        0      // LOW
// I_But_Start : indique si les ESC/moteurs ont été démarrés.
// Logs du 30/09 : HIGH = ESC démarrés. Avec ESC_DEMARRES_ACTIF = 1,
// la pin est en INPUT_PULLDOWN → fil débranché = « non démarrés »
// = DC-DC coupé (état sûr).
#define ESC_DEMARRES_ACTIF  1      // HIGH

// Anti-rebond (filtrage numérique) de TOUS les switchs/boutons :
// I_Switch_ON, I_F_R, Relay_Control, I_But_Start.
// Un changement d'état n'est accepté que s'il est vu N cycles
// Task_Pilote (PERIODE_PILOTE_MS) de suite.
#define SWITCH_ON_CYCLES    2      // 2 × 20 ms = 40 ms ON stable pour passer à ON (RUN)
#define SWITCH_OFF_CYCLES   5      // 5 × 20 ms = 100 ms OFF stable pour couper

// DC-DC (pin DC_DC_3V3 = 32) :
//   I_But_Start (filtré) OFF                         → DC-DC ON
//   I_But_Start (filtré) ON depuis PRECHARGE_MS       → DC-DC OFF
//   (pendant les PRECHARGE_MS premières ms de ON, le DC-DC reste ON)
//   I_But_Start retombe à OFF                         → DC-DC ON tout de suite
#define DC_DC_ACTIF         1      // niveau de sortie = DC-DC alimenté (HIGH)
#define PRECHARGE_MS        3000

// Levier : bornes réelles MESURÉES de l'ADC (10 bits).
// Relevé au banc : repos = 230, butée = 730.
// Valeurs PAR DÉFAUT : remplacées par la calibration sauvegardée
// en EEPROM si elle existe (voir CALIBRATION DU LEVIER).
#define LEVIER_RAW_MIN      300    // valeur au repos (levier relâché)
#define LEVIER_RAW_MAX      700    // valeur à fond
#define LEVIER_DEADBAND     30     // sous ce delta au-dessus du MIN -> 0

// Calibration du levier (Task_Pilote), lancée depuis l'UI du Pi
// (bouton « Calibrer levier ») :
//   1) switch ON à OFF (IDLE), sinon la demande est refusée
//   2) « Commencer »  → {"cmd":"calib"}       : la LED 13 clignote
//   3) bouger le levier butée à butée, le remettre au repos
//   4) « Terminer »   → {"cmd":"calib_fin"}   : min/max sauvés en
//      EEPROM si la course >= CALIB_PLAGE_MIN et que le levier est
//      revenu au repos (LED fixe 2 s = OK)
//      « Annuler »    → {"cmd":"calib_annule"}
//   Passer le switch ON pendant la calibration l'annule.
#define CALIB_LEVIER_ACTIVE 1
#define CALIB_PLAGE_MIN     200    // course min (raw) pour accepter
#define CALIB_MARGE_MAX     10     // retiré du max mesuré → 1023 atteignable
#define CALIB_RETOUR_REPOS  40     // en fin de calib, raw <= min + ça
#define CALIB_TIMEOUT_MS    60000  // jamais terminée → abandon

// Commande secrète (sans écran tactile) : en IDLE (switch ON à OFF),
// basculer le switch F/R CALIB_SECRET_NB fois (passages à actif) en
// moins de CALIB_SECRET_FENETRE_MS → calibration AUTOMATIQUE :
//   la LED 13 clignote, bouger le levier butée à butée pendant
//   CALIB_AUTO_DUREE_MS, et le laisser AU REPOS à la fin.
//   Fin automatique → sauvegarde si OK (LED fixe 2 s).
//   Passer le switch ON l'annule.
#define CALIB_SECRET_NB         5
#define CALIB_SECRET_FENETRE_MS 4000
#define CALIB_AUTO_DUREE_MS     10000
#define CALIB_EEPROM_ADDR   0      // adresse de la structure en EEPROM

// Armement : tant que le levier n'a pas été vu au neutre une fois
// depuis le démarrage (ou depuis un retour en IDLE / ARRET), aucune
// consigne n'est envoyée.
// ARMEMENT_REQUIS  1 = protection active (recommandé sur l'eau)
//                  0 = désactivée, le levier répond tout de suite
#define ARMEMENT_REQUIS     1
#define LEVIER_RAW_ARME     330    // raw sous lequel le levier est « au neutre » (MIN + DEADBAND)
// NB : appliqué en RELATIF au min calibré :
//      neutre si raw <= min + (LEVIER_RAW_ARME - LEVIER_RAW_MIN)
//------------------------------------//


//----- PROPULSION : VESC (CAN3) -----//
// (repris de Real_V1)
#define VESC_ID_A           10
#define VESC_ID_B           11
#define NB_VESC             2
#define BAUD_CAN            250000   // VESC : 250 kbps par défaut
#define VESC_TIMEOUT_MS     500      // VESC muet au-delà → considéré mort

// Hélices contrarotatives : les deux doivent tourner en sens
// OPPOSÉS. Si le bateau recule quand le levier demande l'avant,
// inverse les DEUX lignes. Jamais la même valeur.
#define VESC_A_INVERSE      1
#define VESC_B_INVERSE      1

// ── Mode de commande des VESC ──
// MODE_ERPM    : boucle de vitesse du VESC (ancien comportement)
// MODE_DUTY    : levier → duty cycle (tension). Pas de boucle de
//                vitesse ; vitesse bornée hors de l'eau.
// MODE_COURANT : levier → courant moteur (couple). Plus doux au
//                départ. ⚠ hélice hors de l'eau = le moteur monte
//                jusqu'au duty max : limiter « Max ERPM » dans VESC
//                Tool (voir recommandations mcconf).
// Dans tous les modes, le VESC applique ses propres limites
// (mcconf : Motor Current Max 147.87 A, Max Duty 0.95,
// Battery Current Max 250 A). Le plafond vers 12 km/h vient de
// « Motor Current Max » : ça se change dans VESC Tool, pas ici.
#define MODE_ERPM           0
#define MODE_DUTY           1
#define MODE_COURANT        2
#define MODE_COMMANDE       MODE_COURANT

// eRPM (unité interne : eRPM)
#define RAMP_STEP           80       // changement max d'eRPM par cycle (20 ms)
#define ERPM_MIN_UTILE      1800
#define ERPM_MAX_FORWARD    24000
#define ERPM_MAX_REVERSE    4000     // magnitude ; le signe est mis dans le code

// Duty (unité interne : ‰, 1000 = 100 %). Repère : 1800 eRPM ≈ 5 %,
// 24000 eRPM ≈ 66 % (λ = 0.0105 Wb, 18S). Max VESC = 950.
#define DUTY_RAMP_STEP      5        // ‰ par cycle (20 ms) = 25 %/s
#define DUTY_MIN_UTILE      50
#define DUTY_MAX_FORWARD    950
#define DUTY_MAX_REVERSE    110
#define DUTY_MAX_SURCHAUFFE 500

// Courant moteur (unité interne : 0,1 A). Max VESC = 1478 (147.87 A).
// Si tu montes « Motor Current Max » dans VESC Tool, monte aussi
// COURANT_MAX_FORWARD (le VESC plafonne de toute façon au sien).
#define COURANT_RAMP_STEP      30    // 3 A par cycle (20 ms) = 150 A/s
#define COURANT_MIN_UTILE      50    // 5 A
#define COURANT_MAX_FORWARD    3000  // 300 A
#define COURANT_MAX_REVERSE    500   // 50 A
#define COURANT_MAX_SURCHAUFFE 800   // 80 A

#define PROP_CMD_TIMEOUT_MS 200      // commande FSM plus vieille → consigne 0

// Limite de température — mêmes valeurs que config.py côté Pi
// (T_FET_MAX, T_MOT_MAX, TEMP_HYST). Surchauffe → consigne
// plafonnée à ERPM_MAX_SURCHAUFFE, retour quand toutes les temp.
// sont sous (limite - TEMP_HYST).
#define LIMITE_TEMP_ACTIVE  0        // 0 = surveillance désactivée
#define T_FET_MAX           9999.0f    // °C
#define T_MOT_MAX           9999.0f    // °C
#define TEMP_HYST           3.0f     // °C
#define ERPM_MAX_SURCHAUFFE 18000     // plafond de consigne en surchauffe

// ── Consigne générique selon MODE_COMMANDE (ne pas modifier) ──
#if MODE_COMMANDE == MODE_ERPM
  #define CMD_NOM            "ERPM"
  #define CMD_RAMP_STEP      RAMP_STEP
  #define CMD_MIN_UTILE      ERPM_MIN_UTILE
  #define CMD_MAX_FORWARD    ERPM_MAX_FORWARD
  #define CMD_MAX_REVERSE    ERPM_MAX_REVERSE
  #define CMD_MAX_SURCHAUFFE ERPM_MAX_SURCHAUFFE
#elif MODE_COMMANDE == MODE_DUTY
  #define CMD_NOM            "DUTY"
  #define CMD_RAMP_STEP      DUTY_RAMP_STEP
  #define CMD_MIN_UTILE      DUTY_MIN_UTILE
  #define CMD_MAX_FORWARD    DUTY_MAX_FORWARD
  #define CMD_MAX_REVERSE    DUTY_MAX_REVERSE
  #define CMD_MAX_SURCHAUFFE DUTY_MAX_SURCHAUFFE
#elif MODE_COMMANDE == MODE_COURANT
  #define CMD_NOM            "COURANT"
  #define CMD_RAMP_STEP      COURANT_RAMP_STEP
  #define CMD_MIN_UTILE      COURANT_MIN_UTILE
  #define CMD_MAX_FORWARD    COURANT_MAX_FORWARD
  #define CMD_MAX_REVERSE    COURANT_MAX_REVERSE
  #define CMD_MAX_SURCHAUFFE COURANT_MAX_SURCHAUFFE
#else
  #error "MODE_COMMANDE invalide"
#endif
//------------------------------------//


//----------- QUALITÉ DU BUS CAN -----//
// Statistiques publiées dans la télémétrie (clé "can") :
// trames reçues / rejetées (mal formées, perdues) et erreurs bus.
#define CAN_STATS_FENETRE_MS 1000    // fenêtre du pourcentage « récent »
//------------------------------------//


//------------ FILTRE GPS ------------//
// Rejet des vitesses / positions aberrantes de l'Xsens (paquets
// corrompus). Après GPS_REJETS_RESYNC rejets consécutifs, la
// nouvelle valeur est acceptée (vrai saut, ex. premier fix).
#define XSENS_RX_BUFFER       1024    // octets ajoutés au buffer RX de Serial5
#define GPS_VITESSE_MAX_KMH   80.0f   // au-delà : rejet
#define GPS_ACCEL_MAX_KMH_S   25.0f   // variation max plausible (km/h par seconde)
#define GPS_SAUT_MIN_KMH      2.0f    // tolérance fixe (bruit)
#define GPS_POS_SAUT_MAX_M    50.0f   // saut de position max entre deux trames
#define GPS_REJETS_RESYNC     20      // rejets consécutifs → re-synchronisation
//------------------------------------//


//--------- MACHINE D'ETAT -----------//
// Logique IDLE / RUN (NEUTRAL, FORWARD, REVERSE) = Real_V1.
// Ajouts : ARRET (erreur critique du watchdog) et CONTROLE.
// FORWARD → CONTROLE si : FOILS_ACTIFS, switch Relay_Control actif,
//   I/O des sonars valides, vitesse GPS >= SM_VITESSE_CONTROLE_KMH.
// CONTROLE → FORWARD si une condition tombe, ou vitesse
//   < SM_VITESSE_CONTROLE_KMH - SM_VITESSE_HYST_KMH.
#define SM_VITESSE_CONTROLE_KMH  12.0f
#define SM_VITESSE_HYST_KMH      1.0f
#define FSM_QUEUE_LEN            8
//------------------------------------//


//------------- WATCHDOG -------------//
#define WD_TIMEOUT_CRITIQUE_MS   300   // Pilote, FSM, Propulsion, ESC → ARRET
#define WD_TIMEOUT_CAPTEUR_MS    1000  // Xsens, Sonar, Foils, RPi → pas de CONTROLE
#define XSENS_DONNEES_TIMEOUT_MS 200   // aucun paquet Xsens depuis → données périmées
#define WD_STACK_PERIODE         10    // mesure des stacks tous les N cycles (1 s)
// Watchdog matériel (RTWDOG) : reboot du Teensy si Task_Watchdog
// elle-même ne tourne plus (scheduler figé, crash...).
#define WD_MATERIEL_ACTIF        1
#define WD_MATERIEL_TIMEOUT_S    1.0f  // 0.03 à 2.0 s
//------------------------------------//


//---------- LIAISON RASPBERRY PI ----//
// USB (Serial = /dev/ttyACM0 côté Pi), format JSON de Real_V1.
#define HB_MS               500   // battement de cœur hors streaming
#define PI_LINE_MAX         96
#define RPI_JSON_MAX        2048
#define DEBUG_PRINT         0     // texte lisible en plus du JSON (app Pi fermée)
#define DEBUG_LEVIER        0     // 1 = imprime le raw ADC pour calibrer
#define PRINT_MS            1000
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
#define LED                         13   // SORTIE : clignote pendant la calibration du levier
#define Analog_Sonar_Avant          14
#define Analog_Sonar_Arriere_Gauche 15
#define Analog_Sonar_Arriere_Droit  16
//#define PIN_17                      17
//#define PIN_18                      18
#define Relay_Control               19   // ENTRÉE : switch d'activation du contrôle des foils
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
#define DC_DC_3V3                   32   // SORTIE : DC-DC (ON si I_But_Start OFF, OFF 3 s après I_But_Start ON)
#define I_O_Sonar_Avant             33
#define I_O_Sonar_Arriere_Gauche    34
#define I_O_Sonar_Arriere_Droit     35
//#define PIN_36                      36
//#define PIN_37                      37
#define I_Switch_ON                 38
#define I_But_Start                 39   // ENTRÉE : ESC/moteurs démarrés
#define I_F_R                       40
//#define PIN_41                      41
//------------------------------------//

#endif /* CONFIG_H */

