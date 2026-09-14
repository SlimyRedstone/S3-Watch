#!/usr/bin/env python3
"""
Rewrite EEZ Studio output in ./ui/ to prefix every top-level symbol with
'RemoteNow_' so multiple apps can coexist without multiple-definition link
errors.

Run from this app's folder:
    python update_eez.py

Idempotent. Re-running is safe.
"""

import re
import sys
from pathlib import Path

APP_NAME = "RemoteNow"
UI_DIR   = Path(__file__).resolve().parent / "ui"

# Names starting with any of these are left alone (LVGL, ESP, libc, etc.).
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
    "printf", "malloc", "free", "fopen", "fclose", "fread", "fwrite",
}

# Match a top-level function definition: `<return type> <name>(<args>) {`
FUNC_RE = re.compile(r'^[\w\s\*]+?\s+(\w+)\s*\([^\)]*\)\s*\{', re.M)
# Match a top-level array variable definition: `<type> <name>[] = { ... };`
ARR_RE  = re.compile(r'^[\w\s\*]+?\s+(\w+)\s*\[\s*\]\s*=', re.M)
# Match a top-level scalar / pointer VARIABLE definition such as:
#   `static int16_t currentScreen = -1;`
#   `objects_t objects;`
#   `lv_obj_t *tick_value_change_obj = NULL;`
# Only the variable name is captured; the type token is left alone.
# Anchored at column 0 so indented (function-local) decls are ignored.
VAR_RE  = re.compile(
    r'^(?:static\s+|const\s+|extern\s+|volatile\s+)*'
    r'\w+(?:\s*\*+)?\s+(\w+)\s*(?:=[^;{}]*?)?;',
    re.M
)


def collect_names(text: str) -> set:
    names = set()
    for rx in (FUNC_RE, ARR_RE, VAR_RE):
        for m in rx.finditer(text):
            n = m.group(1)
            if n.startswith(APP_NAME + "_"): continue
            if n in SKIP_NAMES: continue
            if any(n.startswith(p) for p in SKIP_PREFIXES): continue
            names.add(n)
    return names


def main() -> int:
    if not UI_DIR.exists():
        print(f"  ! ui/ folder not found at {UI_DIR}")
        return 1

    files = sorted(list(UI_DIR.rglob("*.c")) + list(UI_DIR.rglob("*.h")))
    if not files:
        print(f"  ! no .c or .h files in {UI_DIR}")
        return 1

    # Pass 1: gather every renameable symbol.
    all_names = set()
    for f in files:
        try:
            all_names |= collect_names(f.read_text(encoding="utf-8"))
        except UnicodeDecodeError:
            pass

    if not all_names:
        print("  = no symbols to rename. (already prefixed?)")
        return 0

    # Sort longest-first so substring collisions don't truncate replacements.
    sorted_names = sorted(all_names, key=len, reverse=True)
    pattern = re.compile(r'\b(' + '|'.join(re.escape(n) for n in sorted_names) + r')\b')

    def repl(m: re.Match) -> str:
        n = m.group(1)
        return n if n.startswith(APP_NAME + "_") else f"{APP_NAME}_{n}"

    # Pass 2: substitute references everywhere in ui/.
    print(f"Renaming {len(all_names)} symbol(s) with prefix '{APP_NAME}_':")
    for n in sorted_names:
        print(f"    {n}  ->  {APP_NAME}_{n}")

    changed = 0
    for f in files:
        try:
            original = f.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        # Substitute line by line so we leave `#include` directives alone
        # (rewriting a symbol that shares a name with a header file path
        # would break the build).
        new_lines = []
        for line in original.splitlines(keepends=True):
            if line.lstrip().startswith("#include"):
                new_lines.append(line)
            else:
                new_lines.append(pattern.sub(repl, line))
        new = "".join(new_lines)
        if new != original:
            f.write_text(new, encoding="utf-8", newline="\n")
            print(f"  ~ {f.relative_to(UI_DIR.parent)}")
            changed += 1

    print(f"Done. {changed} file(s) updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
