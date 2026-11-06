#!/usr/bin/env python3
"""Run the shared PX4 x500 hover client from examples/px4-hakoniwa."""
import runpy
from pathlib import Path

runpy.run_path(Path(__file__).resolve().parent.parent / "px4-hakoniwa" / "fly.py", run_name="__main__")
