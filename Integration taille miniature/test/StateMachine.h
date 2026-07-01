#pragma once

// ============================================================
//  StateMachine.h  –  Top-level & Run sub-state machine
//  RC Hydrofoil – Teensy 4.1
//
//  Top-level states:
//    IDLE  → attente, moteurs arrêtés, foils neutres
//    RUN   → actif, entre dans les sous-états
//    STOP  → arrêt d'urgence (perte RC ou erreur)
//
//  Run sub-states:
//    NEUTRE   → throttle = 0, foils fixes
//    AVANCE   → throttle > 0, foils fixes
//    RECULE   → SwC ON, foils fixes
//    CONTROLE → throttle >= SM_SPEED_THRESHOLD_HIGH, PID actif
//
//  Transitions:
//    IDLE  → RUN    : SwA ON
//    RUN   → STOP   : SwA OFF ou RC timeout
//    STOP  → IDLE   : automatique après arrêt complet
//
//    NEUTRE  → AVANCE   : throttle > 0
//    NEUTRE  → RECULE   : SwC ON
//    AVANCE  → NEUTRE   : throttle == 0
//    AVANCE  → CONTROLE : throttle >= SM_SPEED_THRESHOLD_HIGH
//    RECULE  → NEUTRE   : SwC OFF
//    CONTROLE→ AVANCE   : throttle < SM_SPEED_THRESHOLD_LOW
// ============================================================

#include <Arduino.h>
#include <Config.h>

// ----------------------------------------------------------------
// State enums
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
// Input snapshot — filled by main.cpp before calling update()
// ----------------------------------------------------------------
struct SMInputs {
    float throttle;   // [0.0, +1.0]
    float rudder;     // [-1.0, +1.0]
    bool  switchA;    // ON/OFF
    bool  switchB;    // unused
    bool  switchC;    // reverse
    bool  rcValid;    // false = signal lost → STOP
};

// ================================================================
//  StateMachine class
// ================================================================
class StateMachine {
public:
    StateMachine() = default;

    void begin();
    void update(const SMInputs& in);

    // Accessors
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