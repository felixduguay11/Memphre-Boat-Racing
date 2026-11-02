# -*- coding: utf-8 -*-
"""lancer_dernier_log.py - bouton Run de PyCharm, sans arguments.

Analyse le log le plus recent de ~/memphre_logs (ou celui donne en
argument) avec analyse_logs.py. Les graphiques sortent dans
<session>_graphs/ a cote du log.
"""
import os
import sys

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)

try:
    import numpy, matplotlib  # noqa: F401
except ImportError:
    sys.exit("numpy ou matplotlib manque. Dans le terminal de PyCharm :\n"
             "    %s -m pip install numpy matplotlib" % sys.executable)

import glob
import analyse_logs

if len(sys.argv) == 1:
    # Le plus recent selon le nom (session_NNNN_AAAAMMJJ-HHMMSS) : les dates
    # de modification changent quand les logs sont copies du Pi.
    dossier = os.path.join(os.path.expanduser("~"), "memphre_logs")
    logs = sorted(glob.glob(os.path.join(dossier, "session_*.jsonl")),
                  key=lambda p: (os.path.basename(p)[8:12], os.path.basename(p)))
    if not logs:
        sys.exit("aucun session_*.jsonl dans " + dossier)
    print("Log :", logs[-1])
    sys.argv.append(logs[-1])
analyse_logs.main()
