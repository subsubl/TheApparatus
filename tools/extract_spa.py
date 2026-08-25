#!/usr/bin/env python3
"""
Extracts the inline SPA from src/WebConsole.cpp (between R"rawliteral(
and )rawliteral") into tools/gui-test/gui.html for Playwright testing.
The served GUI is byte-identical to what the ESP32 ships.
"""
import re
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
src = (root / "src" / "WebConsole.cpp").read_text(encoding="utf-8")

m = re.search(r'R"HTML\((.*?)\)HTML"', src, re.DOTALL)
if not m:
    raise SystemExit("FATAL: rawliteral block not found in WebConsole.cpp")

out = root / "tools" / "gui-test" / "gui.html"
out.write_text(m.group(1), encoding="utf-8")
print(f"extracted {len(m.group(1))} bytes -> {out}")