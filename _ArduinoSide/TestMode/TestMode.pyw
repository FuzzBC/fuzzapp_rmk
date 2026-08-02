"""TestMode.pyw - FuZz Diffuser / SmartTV command console (Python/Tkinter variant).

Run with pythonw.exe (double-click works if .pyw is associated) for no
console window, or `python TestMode.pyw` to also see a console behind it.
Every command uses a real socket directly - no subprocess spawning - so
sends are effectively instant. A fresh TestMode-<date>.log is written next
to this file every run.
"""

import os
import sys

try:
    import ctypes
    ctypes.windll.shcore.SetProcessDpiAwareness(1)
except Exception:
    pass

_BASE_DIR = os.path.dirname(os.path.abspath(__file__))
_RES_DIR = os.path.join(_BASE_DIR, '.res')
if _RES_DIR not in sys.path:
    sys.path.insert(0, _RES_DIR)

import gui

if __name__ == '__main__':
    gui.run(_BASE_DIR)
