# VESC Dashboard — Interface web locale

Interface web avec sliders, graphiques temps réel et console série pour contrôler un VESC via Arduino.

## Installation (une seule fois)

```bash
pip install -r requirements.txt
```

## Démarrage

```bash
python server.py
```

Puis ouvrir le navigateur à → http://127.0.0.1:5000

## Utilisation

1. Sélectionner le port COM de l'Arduino dans le dropdown
2. Choisir le baud rate (115200 par défaut)
3. Cliquer CONNECT
4. Les graphiques se mettent à jour automatiquement
5. Utiliser les sliders pour envoyer Current / RPM / Duty / BrakeCurrent
6. E-STOP envoie immédiatement Current=0

## Structure des fichiers

```
vesc_dashboard/
├── server.py        ← Serveur Python (WebSocket + Serial bridge)
├── dashboard.html   ← Interface web
├── requirements.txt ← Dépendances Python
└── README.md
```

## Format série attendu de l'Arduino

Graphiques  : >NomVariable:valeur
Console     : n'importe quel texte sans ">"
Commandes   : NomControl=valeur (envoyé vers l'Arduino)
