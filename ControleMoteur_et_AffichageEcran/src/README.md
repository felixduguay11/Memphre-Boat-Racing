# VescCAN_Teensy

Bibliothèque de communication VESC par CAN pour Teensy 4.1 + FlexCAN_T4.  
Remplace `vesc_can_bus_arduino` (conçue pour Arduino Mega + MCP2515).

## Fichiers

| Fichier | Rôle |
|---|---|
| `VescCAN_Teensy.h` | Header — structures, interface, déclarations |
| `VescCAN_Teensy.cpp` | Implémentation — décodage, encodage, getters |
| `vesc_can_reader_v4.ino` | Sketch principal (remplace v3) |

## Installation dans PlatformIO

Copiez `VescCAN_Teensy.h` et `VescCAN_Teensy.cpp` dans `src/`, puis dans `vesc_can_reader_v4.ino` :
```cpp
#include "VescCAN_Teensy.h"
```

## Utilisation rapide

```cpp
#include <FlexCAN_T4.h>
#include "VescCAN_Teensy.h"

// Déclaration du bus
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> rawCan1;
FlexCANAdapter<CAN1> adapterCan1(rawCan1);
VescCANBus vesc(adapterCan1);

void setup() {
  rawCan1.begin();
  rawCan1.setBaudRate(250000);
  vesc.begin();
}

void loop() {
  vesc.update();  // toujours appeler en premier dans loop()

  // Lecture
  float rpm = vesc.getERPM(10);       // eRPM de l'ESC ID=10
  float vin = vesc.getVoltageIn(10);  // Tension batterie
  float ife = vesc.getTempFET(10);    // Temp FET

  // Envoi commandes
  vesc.setDuty(10, 0.20f);            // 20% duty sur ESC ID=10
  vesc.setCurrent(10, 5.0f);          // 5 A sur ESC ID=10
  vesc.setStop(10);                    // Arrêt ESC ID=10
}
```

## Multi-ESC sur le même bus

Tous les ESCs sur le même bus CAN sont automatiquement distingués par leur ID :

```cpp
vesc.update();
// Lecture simultanée de 3 ESCs (IDs 10, 11, 12)
for (uint8_t id = 10; id <= 12; id++) {
  if (vesc.isUpdated(id)) {
    Serial.printf("ESC %u : eRPM=%ld  Vin=%.1f V\n",
                  id, vesc.getERPM(id), vesc.getVoltageIn(id));
  }
}
```

## Deux bus CAN indépendants

```cpp
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> rawCan1;
FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> rawCan2;
FlexCANAdapter<CAN1> adapter1(rawCan1);
FlexCANAdapter<CAN2> adapter2(rawCan2);
VescCANBus vesc1(adapter1);
VescCANBus vesc2(adapter2);

void loop() {
  vesc1.update();  // ESCs sur CAN1
  vesc2.update();  // ESCs sur CAN2
}
```

## Commandes disponibles

| Fonction | Description |
|---|---|
| `setDuty(id, duty)` | Duty cycle (-1.0 à +1.0) |
| `setCurrent(id, A)` | Courant moteur en ampères |
| `setCurrentBrake(id, A)` | Courant de freinage (toujours positif) |
| `setERPM(id, erpm)` | RPM électrique cible |
| `setPosition(id, deg)` | Position PID en degrés |
| `setStop(id)` | Arrêt immédiat (duty=0) |

## Données lues (Status VESC)

| Getter | Status | Description |
|---|---|---|
| `getERPM(id)` | S1 | RPM électrique |
| `getMotorCurrent(id)` | S1 | Courant moteur (A) |
| `getDutyCycle(id)` | S1 | Duty cycle (-1 à +1) |
| `getAmpHours(id)` | S2 | Ah consommées |
| `getAmpHoursChg(id)` | S2 | Ah rechargées (regen) |
| `getWattHours(id)` | S3 | Wh consommées |
| `getWattHoursChg(id)` | S3 | Wh rechargées |
| `getTempFET(id)` | S4 | Temp FETs (°C) |
| `getTempMotor(id)` | S4 | Temp moteur (°C) |
| `getCurrentIn(id)` | S4 | Courant batterie (A) |
| `getPIDPos(id)` | S4 | Position PID (°) |
| `getTachometer(id)` | S5 | Tachomètre cumulé |
| `getVoltageIn(id)` | S5 | Tension batterie (V) |
| `getRPM(id, poles)` | S1 | RPM mécanique |

## Corrections vs vesc_can_bus_arduino original

- **Suppression dépendance MCP2515/SPI** → FlexCAN_T4 natif Teensy
- **Bug WattHours** : la lib originale multipliait par 0.01, le firmware VESC divise par 10000 → corrigé
- **Signed Int16** : `(short)strtol(hex)` remplacé par lecture directe signée correcte pour les valeurs négatives (courant regen, etc.)
- **Support multi-ESC natif** : plus de classe par ESC, un seul objet bus gère tous les IDs
- **eRPM / 30 retiré** : la lib originale divisait l'eRPM par 30 sans raison claire ; on retourne l'eRPM brut (le firmware envoie directement l'eRPM)
