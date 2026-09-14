#!/usr/bin/env python3
"""
Scaffold a new Brookesia app, ready to host an EEZ Studio LVGL UI.

Run from the project root or from the dev/ folder:
    python dev/new_app.py
"""

import os
import re
import shutil
import sys
from pathlib import Path

# Locate project root by walking up from this script.
SCRIPT_DIR  = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
COMPONENTS_DIR = PROJECT_ROOT / "components"
MAIN_CMAKE     = PROJECT_ROOT / "main" / "CMakeLists.txt"

EEZ_FILES = [
    "template.eez-project",
    "template.eez-project-ui-state",
]


def ask_app_name() -> str:
    while True:
        name = input("App name (PascalCase, e.g. MyApp): ").strip()
        if not re.match(r"^[A-Za-z][A-Za-z0-9_]*$", name):
            print("  -> invalid, use letters/digits/underscore only.")
            continue
        return name


def write_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")
    print(f"  + {path.relative_to(PROJECT_ROOT)}")


def render_hpp(name: str) -> str:
    return f"""#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

namespace esp_brookesia::apps {{

class {name} : public systems::phone::App {{
public:
    static {name} *requestInstance(
        bool use_status_bar = true, bool use_navigation_bar = false);
    ~{name}();

    bool run()  override;
    bool back() override;
    bool close() override;

protected:
    {name}(bool use_status_bar, bool use_navigation_bar);

private:
    static {name} *_instance;
}};

}} // namespace esp_brookesia::apps
"""


def render_cpp(name: str) -> str:
    return f"""#include "{name}.hpp"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "{name}"
#include "esp_lib_utils.h"

// EEZ Studio generates C code; pull its entry point in with C linkage.
extern "C" {{
    #include "ui.h"
    #include "vars.h"
    #include "styles.h"
    #include "structs.h"
    #include "images.h"
    #include "fonts.h"
    #include "screens.h"
}}

LV_IMG_DECLARE(esp_brookesia_image_middle_app_launcher_default_112_112);

namespace esp_brookesia::apps {{

static constexpr char APP_NAME[] = "{name}";

{name} *{name}::_instance = nullptr;

{name} *{name}::requestInstance(bool use_status_bar, bool use_navigation_bar)
{{
    if (_instance == nullptr) _instance = new {name}(use_status_bar, use_navigation_bar);
    return _instance;
}}

{name}::{name}(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME,
          &esp_brookesia_image_middle_app_launcher_default_112_112,
          /*use_default_screen=*/true,
          use_status_bar,
          use_navigation_bar) {{}}

{name}::~{name}() {{ _instance = nullptr; }}

bool {name}::run()
{{
    {name}_ui_init();   // EEZ Studio entry. Builds widgets on the active screen.
    return true;
}}

bool {name}::back()
{{
    notifyCoreClosed();
    return true;
}}

bool {name}::close()
{{
    return true;
}}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, {name}, APP_NAME, []()
{{
    return std::shared_ptr<{name}>({name}::requestInstance(), []({name} *p) {{}});
}})

}} // namespace esp_brookesia::apps
"""


def render_cmakelists(name: str) -> str:
    return f"""# Run update_eez.py at configure time so EEZ-Studio source files in ui/
# are renamed with this app's prefix before they are compiled. The script
# is idempotent, so re-running on every configure is safe.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    execute_process(
        COMMAND ${{Python3_EXECUTABLE}} ${{CMAKE_CURRENT_LIST_DIR}}/update_eez.py
        WORKING_DIRECTORY ${{CMAKE_CURRENT_LIST_DIR}}
        RESULT_VARIABLE _eez_rc
        OUTPUT_QUIET
    )
    if(NOT _eez_rc EQUAL 0)
        message(WARNING "update_eez.py failed (rc=${{_eez_rc}}) at ${{CMAKE_CURRENT_LIST_DIR}}")
    endif()
else()
    message(WARNING "Python3 not found; skipping update_eez.py")
endif()
if(DEFINED CMAKE_PROJECT_NAME)    
    # Single recursive glob covers root, ui/, icon/, and any other subdir.
    # DO NOT add a second file(GLOB_RECURSE PROJ_SRCS_C ...) line — it would
    # OVERWRITE this variable, not append, and you would lose source files.
    file(GLOB_RECURSE PROJ_SRCS_C   CONFIGURE_DEPENDS ${{CMAKE_CURRENT_LIST_DIR}}/*.c)
    file(GLOB_RECURSE PROJ_SRCS_CPP CONFIGURE_DEPENDS ${{CMAKE_CURRENT_LIST_DIR}}/*.cpp)
else()
    file(GLOB_RECURSE PROJ_SRCS_C   ${{CMAKE_CURRENT_LIST_DIR}}/*.c)
    file(GLOB_RECURSE PROJ_SRCS_CPP ${{CMAKE_CURRENT_LIST_DIR}}/*.cpp)
endif()

idf_component_register(
    SRCS ${{PROJ_SRCS_C}} ${{PROJ_SRCS_CPP}}
    INCLUDE_DIRS
        ${{CMAKE_CURRENT_LIST_DIR}}
        ${{CMAKE_CURRENT_LIST_DIR}}/ui
    REQUIRES driver
    WHOLE_ARCHIVE
)
# Prefix common EEZ-Studio global symbols with this app's name so multiple
# apps each carrying their own ui.c/screens.c/actions.c can coexist without
# multiple-definition link errors.
# target_compile_definitions(${{COMPONENT_LIB}} PRIVATE
#     ui_init={name}_ui_init
#     ui_tick={name}_ui_tick
#     objects={name}_objects
#     flowState={name}_flowState
#     create_screens={name}_create_screens
#     tick_screens={name}_tick_screens
# )

set_source_files_properties(
    ${{PROJ_SRCS_CPP}}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
"""


def render_idf_yml() -> str:
    return """version: 0.1.0
dependencies:
  brookesia_core:
    public: true
  waveshare/esp32_s3_touch_amoled_2_06: "*"
"""


def render_ui_placeholder() -> str:
    """Stub ui.h/ui.c so the app builds before EEZ files are dropped in."""
    return """// Placeholder. Overwrite this file with the EEZ Studio export.
// Required entry point: void ui_init(void);
"""


def render_ui_h_stub(name: str) -> str:
    return """// Stub. Replace with EEZ Studio's generated ui.h.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void {name}_ui_init(void);
void {name}_ui_tick(void);
#ifdef __cplusplus
}
#endif
"""


def render_ui_c_stub(name: str) -> str:
    return f"""// Stub. Replace with EEZ Studio's generated ui.c (and friends).
#include "ui.h"
#include "lvgl.h"

void ui_init(void) {{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, "{name}: drop EEZ files into ui/");
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
}}

void ui_tick(void) {{}}
"""


def render_actions_stub() -> str:
    """EEZ may emit actions.h declaring user actions. Provide a default empty
    implementation so the app still links if you forget to add yours."""
    return """// Optional stub for EEZ-declared actions. Once the EEZ export drops in
// its own actions.c, delete this file.
"""


def render_update_eez(name: str) -> str:
    """Per-app symbol-rewrite script.

    Run AFTER copying EEZ Studio output into ui/. Walks every .c/.h there,
    collects function definitions and array variable definitions, then
    prepends `<AppName>_` to every name that needs it. Idempotent.
    """
    return f"""#!/usr/bin/env python3
\"\"\"
Rewrite EEZ Studio output in ./ui/ to prefix every top-level symbol with
'{name}_' so multiple apps can coexist without multiple-definition link
errors.

Run from this app's folder:
    python update_eez.py

Idempotent. Re-running is safe.
\"\"\"

import re
import sys
from pathlib import Path

APP_NAME = "{name}"
UI_DIR   = Path(__file__).resolve().parent / "ui"

# Names starting with any of these are left alone (LVGL, ESP, libc, etc.).
SKIP_PREFIXES = (
    "lv_", "LV_", "_lv_",
    "esp_", "ESP_", "__esp_",
    "ble_", "nimble_",
    "xpowers_", "XPowers",
    "__", "_",
)
SKIP_NAMES = {{
    "main", "if", "for", "while", "switch", "return", "do", "else",
    "void", "int", "char", "float", "double", "long", "short",
    "unsigned", "signed", "static", "const", "extern", "struct",
    "typedef", "sizeof", "true", "false", "NULL",
    "memcpy", "memset", "memcmp", "strcpy", "strcmp", "strlen", "snprintf",
    "printf", "malloc", "free", "fopen", "fclose", "fread", "fwrite",
}}

# Match a top-level function definition: `<return type> <name>(<args>) {{`
FUNC_RE = re.compile(r'^[\\w\\s\\*]+?\\s+(\\w+)\\s*\\([^\\)]*\\)\\s*\\{{', re.M)
# Match a top-level array variable definition: `<type> <name>[] = {{ ... }};`
ARR_RE  = re.compile(r'^[\\w\\s\\*]+?\\s+(\\w+)\\s*\\[\\s*\\]\\s*=', re.M)
# Match a top-level scalar / pointer VARIABLE definition such as:
#   `static int16_t currentScreen = -1;`
#   `objects_t objects;`
#   `lv_obj_t *tick_value_change_obj = NULL;`
# Only the variable name is captured; the type token is left alone.
# Anchored at column 0 so indented (function-local) decls are ignored.
VAR_RE  = re.compile(
    r'^(?:static\\s+|const\\s+|extern\\s+|volatile\\s+)*'
    r'\\w+(?:\\s*\\*+)?\\s+(\\w+)\\s*(?:=[^;{{}}]*?)?;',
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
        print(f"  ! ui/ folder not found at {{UI_DIR}}")
        return 1

    files = sorted(list(UI_DIR.rglob("*.c")) + list(UI_DIR.rglob("*.h")))
    if not files:
        print(f"  ! no .c or .h files in {{UI_DIR}}")
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
    pattern = re.compile(r'\\b(' + '|'.join(re.escape(n) for n in sorted_names) + r')\\b')

    def repl(m: re.Match) -> str:
        n = m.group(1)
        return n if n.startswith(APP_NAME + "_") else f"{{APP_NAME}}_{{n}}"

    # Pass 2: substitute references everywhere in ui/.
    print(f"Renaming {{len(all_names)}} symbol(s) with prefix '{{APP_NAME}}_':")
    for n in sorted_names:
        print(f"    {{n}}  ->  {{APP_NAME}}_{{n}}")

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
            f.write_text(new, encoding="utf-8", newline="\\n")
            print(f"  ~ {{f.relative_to(UI_DIR.parent)}}")
            changed += 1

    print(f"Done. {{changed}} file(s) updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
"""


def patch_main_cmake(name: str) -> bool:
    if not MAIN_CMAKE.exists():
        print(f"  ! main CMakeLists not found at {MAIN_CMAKE}, skipping patch")
        return False
    text = MAIN_CMAKE.read_text(encoding="utf-8")
    if re.search(rf"^\s*{re.escape(name)}\s*$", text, re.M):
        print(f"  = {MAIN_CMAKE.relative_to(PROJECT_ROOT)} already lists {name}")
        return True
    # Insert before the closing ) of REQUIRES block.
    pattern = re.compile(r"(REQUIRES\b[^\)]*?)(\n\s*\))", re.S)
    m = pattern.search(text)
    if not m:
        print(f"  ! could not find REQUIRES block in {MAIN_CMAKE}")
        return False
    insertion = m.group(1).rstrip() + f"\n        {name}" + m.group(2)
    new_text = text[:m.start()] + insertion + text[m.end():]
    MAIN_CMAKE.write_text(new_text, encoding="utf-8", newline="\n")
    print(f"  ~ patched {MAIN_CMAKE.relative_to(PROJECT_ROOT)} (+ {name})")
    return True


def main() -> int:
    print(f"Project root: {PROJECT_ROOT}")
    name = ask_app_name()
    app_dir = COMPONENTS_DIR / name
    if app_dir.exists():
        print(f"  ! {app_dir} already exists, aborting.")
        return 1

    print(f"Creating component {app_dir.relative_to(PROJECT_ROOT)} ...")
    write_file(app_dir / f"{name}.hpp",          render_hpp(name))
    write_file(app_dir / f"{name}.cpp",          render_cpp(name))
    write_file(app_dir / "CMakeLists.txt",       render_cmakelists(name))
    write_file(app_dir / "idf_component.yml",    render_idf_yml())

    # ui/ subfolder for EEZ Studio output, with build-time stubs so the
    # project compiles before the user drops the real files in.
    write_file(app_dir / "ui" / "ui.h",          render_ui_h_stub(name))
    write_file(app_dir / "ui" / "ui.c",          render_ui_c_stub(name))
    write_file(app_dir / "ui" / "README.txt",    render_ui_placeholder())

    # Per-app EEZ symbol-prefix script. Run after dropping EEZ files in ui/.
    write_file(app_dir / "update_eez.py",        render_update_eez(name))


    for f in EEZ_FILES:
        src = SCRIPT_DIR / f
        dest = app_dir / f.replace("template", name)
        shutil.copyfile(src, dest)

    print("Patching main/CMakeLists.txt ...")
    patch_main_cmake(name)

    print()
    print("Done. Next steps:")
    print(f"  1. Export your project from EEZ Studio (LVGL target).")
    print(f"  2. Copy the generated files (ui.h, ui.c, screens.*, actions.*,")
    print(f"     images.*, vars.*, styles.*, fonts.*) into")
    print(f"     {app_dir / 'ui'} (overwrite the stub).")
    print(f"  3. Implement any user actions EEZ declared in actions.h.")
    print(f"  4. idf.py fullclean && idf.py build")
    return 0


if __name__ == "__main__":
    sys.exit(main())
