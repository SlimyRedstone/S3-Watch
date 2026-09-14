#!/usr/bin/env python3
"""
Project-wide EEZ Studio symbol prefixer.

Walks every components/<App>/ui/ folder, derives the prefix from the
component folder name, and rewrites every top-level function and array
variable definition (and all references to them) inside the .c/.h files
so they cannot collide with the same names used by other apps.

Idempotent. Safe to run on every configure.

Run from anywhere, but typically from the project's main CMakeLists.txt:
    python dev/prefix_eez.py
"""

import re
import sys
from pathlib import Path

SCRIPT_DIR     = Path(__file__).resolve().parent
PROJECT_ROOT   = SCRIPT_DIR.parent
COMPONENTS_DIR = PROJECT_ROOT / "components"

# Names starting with any of these are left alone.
SKIP_PREFIXES = (
    "lv_", "LV_", "_lv_",
    "esp_", "ESP_", "__esp_",
    "ble_", "nimble_",
    "xpowers_", "XPowers",
    "__", "_",
)

SKIP_NAMES = {
    "main", "if", "for", "while", "switch", "return", "do", "else",
    "void", "int", "char", "float", "double", "long", "short",
    "unsigned", "signed", "static", "const", "extern", "struct",
    "typedef", "sizeof", "true", "false", "NULL",
    "memcpy", "memset", "memcmp", "strcpy", "strcmp", "strlen", "snprintf",
    "printf", "malloc", "free", "fopen", "fclose", "fread", "fwrite", "include"
}

# `<return type> <name>(<args>) {`  — top-level function definition
FUNC_RE = re.compile(r'^[\w\s\*]+?\s+(\w+)\s*\([^\)]*\)\s*\{', re.M)
# `<type> <name>[] = `              — top-level array variable definition.
ARR_RE  = re.compile(r'^[\w\s\*]+?\s+(\w+)\s*\[(\s|\d)*\]\s*=', re.M)
# `<type> <name>;` or `<type> <name> = ...;` — top-level scalar / pointer
# VARIABLE definition. Only the variable name is captured; the type token is
# left alone. Anchored at column 0 so indented (function-local) decls are
# ignored. Excludes function declarations (no `(` between name and `;`).
VAR_RE  = re.compile(
    r'^(?:static\s+|const\s+|extern\s+|volatile\s+)*'
    r'\w+(?:\s*\*+)?\s+\*?(\w+)\s*(?:=[^;{}]*?)?;',
    re.M
)


def collect_names(text: str, prefix: str) -> set:
    names = set()
    for rx in (FUNC_RE, VAR_RE, ARR_RE):
        for m in rx.finditer(text):
            n = m.group(1)
            if n.startswith(prefix + "_") : continue
            if n in SKIP_NAMES: continue
            if any(n.startswith(p) for p in SKIP_PREFIXES): continue
            names.add(n)
    return names


def process_app(ui_dir: Path, prefix: str) -> int:
    files = sorted(list(ui_dir.rglob("*.c")) + list(ui_dir.rglob("*.h")))
    if not files:
        return 0

    all_names = set()
    for f in files:
        try:
            all_names |= collect_names(f.read_text(encoding="utf-8"), prefix)
        except UnicodeDecodeError:
            continue
    if not all_names:
        return 0

    sorted_names = sorted(all_names, key=len, reverse=True)
    pat = re.compile(r'\b(' + '|'.join(re.escape(n) for n in sorted_names) + r')\b')

    def repl(m: re.Match) -> str:
        n = m.group(1)
        return n if n.startswith(prefix + "_") else f"{prefix}_{n}"

    changed_files = 0
    for f in files:
        try:
            original = f.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        # Substitute line by line so we can leave `#include` directives alone
        # (rewriting a symbol that happens to share a name with a header file
        # would break the build).
        new_lines = []
        for line in original.splitlines(keepends=True):
            if line.lstrip().startswith("#include"):
                new_lines.append(line)
            else:
                new_lines.append(pat.sub(repl, line))
        new = "".join(new_lines)
        if new != original:
            f.write_text(new, encoding="utf-8", newline="\n")
            changed_files += 1

    if changed_files:
        print(f"  [{prefix}] +{len(all_names)} symbol(s) prefixed in {changed_files} file(s)")
    return changed_files


def main() -> int:
    if not COMPONENTS_DIR.is_dir():
        print(f"  ! components folder not found: {COMPONENTS_DIR}")
        return 1

    print(f"Scanning {COMPONENTS_DIR.relative_to(PROJECT_ROOT)} for ui/ subfolders ...")
    total = 0
    for comp in sorted(COMPONENTS_DIR.iterdir()):
        if not comp.is_dir():
            continue
        ui_dir = comp / "ui"
        if not ui_dir.is_dir():
            continue
        total += process_app(ui_dir, comp.name)

    if total == 0:
        print("  = nothing to do (already prefixed or no EEZ files yet).")
    else:
        print(f"Done. {total} file(s) updated across all components.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
