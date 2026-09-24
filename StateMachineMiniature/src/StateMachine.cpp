// ============================================================
//  StateMachine.cpp  –  Top-level & Run sub-state machine
//  RC Hydrofoil – Teensy 4.1
// ============================================================

#include <StateMachine.h>

// ----------------------------------------------------------------
// begin()
// ----------------------------------------------------------------
void StateMachine::begin()
{
    _top = TopState::IDLE;
    _run = RunState::NEUTRE;
    Serial.println("[SM] Initialised — IDLE");
}

// ----------------------------------------------------------------
// update()  –  call at main loop rate (50 Hz)
// ----------------------------------------------------------------
void StateMachine::update(const SMInputs& in)
{
    updateTop(in);
    if (_top == TopState::RUN) {
        updateRun(in);
    }
}

// ----------------------------------------------------------------
// updateTop()
// ----------------------------------------------------------------
void StateMachine::updateTop(const SMInputs& in)
{
    switch (_top) {

        case TopState::IDLE:
            if (in.switchA && in.rcValid) {
                _top = TopState::RUN;
                _run = RunState::NEUTRE;
                Serial.println("[SM] IDLE → RUN");
            }
            break;

        case TopState::RUN:
            if (!in.rcValid) {
                Serial.println("[SM] RC lost → STOP");
                _top = TopState::STOP;
                onEnterStop();
            } else if (!in.switchA) {
                Serial.println("[SM] SwA OFF → STOP");
                _top = TopState::STOP;
                onEnterStop();
            }
            break;

        case TopState::STOP:
            // Return to IDLE automatically after stop
            onEnterIdle();
            _top = TopState::IDLE;
            Serial.println("[SM] STOP → IDLE");
            break;
    }
}

// ----------------------------------------------------------------
// updateRun()
// ----------------------------------------------------------------
void StateMachine::updateRun(const SMInputs& in)
{
    switch (_run) {

        case RunState::NEUTRE:
            if (in.switchC) {
                _run = RunState::RECULE;
                Serial.println("[SM] NEUTRE → RECULE");
            } else if (in.throttle > 0.0f) {
                _run = RunState::AVANCE;
                Serial.println("[SM] NEUTRE → AVANCE");
            }
            break;

        case RunState::AVANCE:
            if (in.throttle <= 0.0f) {
                _run = RunState::NEUTRE;
                Serial.println("[SM] AVANCE → NEUTRE");
            } else if (in.throttle >= SM_SPEED_THRESHOLD_HIGH) {
                _run = RunState::CONTROLE;
                Serial.println("[SM] AVANCE → CONTROLE");
            }
            break;

        case RunState::RECULE:
            if (!in.switchC) {
                _run = RunState::NEUTRE;
                Serial.println("[SM] RECULE → NEUTRE");
            }
            break;

        case RunState::CONTROLE:
            if (in.throttle < SM_SPEED_THRESHOLD_LOW) {
                _run = RunState::AVANCE;
                Serial.println("[SM] CONTROLE → AVANCE");
            }
            break;
    }
}

// ----------------------------------------------------------------
// onEnterStop()  –  actions immédiates à l'entrée en STOP
// ----------------------------------------------------------------
void StateMachine::onEnterStop()
{
    // Moteurs et foils seront gérés par main.cpp selon l'état
    _run = RunState::NEUTRE;
}

// ----------------------------------------------------------------
// onEnterIdle()
// ----------------------------------------------------------------
void StateMachine::onEnterIdle()
{
    _run = RunState::NEUTRE;
}

// ----------------------------------------------------------------
// printDebug()
// ----------------------------------------------------------------
void StateMachine::printDebug() const
{
    const char* topStr[] = { "IDLE", "RUN", "STOP" };
    const char* runStr[] = { "NEUTRE", "AVANCE", "RECULE", "CONTROLE" };

    Serial.printf("[SM] Top: %-6s  Run: %s\n",
                  topStr[(uint8_t)_top],
                  runStr[(uint8_t)_run]);
}