#pragma once

// ============================================================
//  StateMachine.h  –  Machine d'état haut-niveau + sous-états RUN
//  Memphre Boat Racing – Teensy 4.1
//
//  Porté depuis Miniature/include/StateMachine.h.
//  Modif vs original : include "config.h" (minuscule, guillemets).
//
//  États haut-niveau :
//    IDLE  → attente, moteurs arrêtés, foils neutres
//    RUN   → actif, entre dans les sous-états
//    STOP  → arrêt d'urgence (perte RC ou SwA OFF)
//
//  Sous-états RUN :
//    NEUTRE   → throttle = 0
//    AVANCE   → throttle > 0
//    RECULE   → SwC ON
//    CONTROLE → throttle >= SM_SPEED_THRESHOLD_HIGH (PID foils actif)
//
//  Transitions :
//    IDLE  → RUN    : SwA ON
//    RUN   → STOP   : SwA OFF ou timeout RC
//    STOP  → IDLE   : automatique après arrêt
//    NEUTRE  → AVANCE   : throttle > 0
//    NEUTRE  → RECULE   : SwC ON
//    AVANCE  → NEUTRE   : throttle == 0
//    AVANCE  → CONTROLE : throttle >= SM_SPEED_THRESHOLD_HIGH
//    RECULE  → NEUTRE   : SwC OFF
//    CONTROLE→ AVANCE   : throttle < SM_SPEED_THRESHOLD_LOW
// ============================================================

#include <Arduino.h>
#include "config.h"

// ----------------------------------------------------------------
// Enums d'état
// ----------------------------------------------------------------
enum class TopState : uint8_t {
    IDLE  = 0,
    RUN   = 1,
    STOP  = 2
};

enum class RunState : uint8_t {
    NEUTRE   = 0,
    AVANCE   = 1,
    RECULE   = 2,
    CONTROLE = 3
};

// ----------------------------------------------------------------
// Snapshot d'entrée — rempli avant l'appel à update()
// ----------------------------------------------------------------
struct SMInputs {
    float throttle;   // [0.0, +1.0]
    float rudder;     // [-1.0, +1.0]
    bool  switchA;    // ON/OFF (armement)
    bool  switchB;    // inutilisé
    bool  switchC;    // marche arrière
    bool  rcValid;    // false = signal perdu → STOP
};

// ================================================================
//  Classe StateMachine
// ================================================================
class StateMachine {
public:
    StateMachine() = default;

    void begin();
    void update(const SMInputs& in);

    TopState getTopState() const { return _top; }
    RunState getRunState() const { return _run; }
    bool     isRunning()   const { return _top == TopState::RUN;  }
    bool     isStopped()   const { return _top == TopState::STOP; }
    bool     isControl()   const { return _run == RunState::CONTROLE; }
    bool     isReverse()   const { return _run == RunState::RECULE;   }

    void printDebug() const;

private:
    TopState _top = TopState::IDLE;
    RunState _run = RunState::NEUTRE;

    void updateTop(const SMInputs& in);
    void updateRun(const SMInputs& in);
    void onEnterStop();
    void onEnterIdle();
};
