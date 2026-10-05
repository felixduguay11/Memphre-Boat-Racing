#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Point d'entree.

    python3 run.py                    # UART reel sur /dev/serial0
    python3 run.py --sim --windowed   # test sans Teensy
    python3 run.py --port /dev/ttyAMA0
"""

import sys
from ui.app import main

if __name__ == "__main__":
    sys.exit(main())
