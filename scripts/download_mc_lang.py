#!/usr/bin/env python3
"""Thin wrapper — delegates to ``scripts.vanilla.lang``.

Usage
  python scripts/download_mc_lang.py --help
"""
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from vanilla import lang

lang.main()
