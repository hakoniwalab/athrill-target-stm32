#!/usr/bin/env python3
"""Run the shared ArduCopter x500 hover client from examples/ardupilot-hakoniwa."""
import runpy
from pathlib import Path

runpy.run_path(Path(__file__).resolve().parent.parent / "ardupilot-hakoniwa" / "fly.py", run_name="__main__")
