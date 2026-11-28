#!/usr/bin/env python3
"""Build everything that is derived from the design sources in this directory.

    python3 mobile-ui/design/build.py            # regenerate the UI pack
    python3 mobile-ui/design/build.py --check    # validate only: specs, tokens, textures, strings, unused icons
    python3 mobile-ui/design/build.py --check --game DIR
                                                 # and no script name of ours is a name of the game in DIR
    python3 mobile-ui/design/build.py --engine-root DIR
                                                 # another engine tree than engine/ (the UI string keys of its
                                                 # screens are checked); --no-engine-check skips that check explicitly

    python3 mobile-ui/design/build.py --script-header FILE
                                                 # only write the capability header of the interface scripts

Sources:  tokens.json, icons.json, capabilities.json, scripts.json, components/*/{spec.json,template.rml,style.rcss},
          screens/<name>/{spec.json,template.rml,style.rcss}, base.rcss, overlay.rml, screen.rml, hud.rml, hud-layout.json,
          gallery/*.rml, fonts/, textures/ (baked from textures-src/ by bake_textures.py), images/
Outputs:  <repo>/build/ui-pack/        the runtime pack the application ships as assets/ui
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
PROGRAM = HERE.parent / "PROGRAM"  # the interface scripts the engine attaches to the game's program
DEFAULT_PACK = REPO / "build" / "ui-pack"
REF = re.compile(r"@\{([A-Za-z][A-Za-z0-9_.]*)(?:/([0-9.]+))?(?:\|([a-z]+))?\}")


class DesignError(Exception):
    pass


def load_tokens():
    tokens = json.loads((HERE / "tokens.json").read_text(encoding="utf-8"))
    tokens.pop("$comment", None)
    seen = {}
    for name, value in tokens["color"].items():
        if not re.fullmatch(r"#[0-9a-f]{6}", value):
            raise DesignError(f"tokens.json: color.{name} must be lower-case #rrggbb, got {value}")
        if value in seen:
            raise DesignError(f"tokens.json: color.{name} repeats the value of color.{seen[value]}; one colour, one token")
        seen[value] = name
    return tokens


def rgb(value):
    return int(value[1:3], 16), int(value[3:5], 16), int(value[5:7], 16)


def lookup(tree, path, where):
    node = tree
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            raise DesignError(f"{where}: unknown token @{{{path}}}")
        node = node[part]
    return node


def substitute(text, tokens, target, where, extra=None):
    """Replaces @{group.name}, @{color.name/alpha}, @{color.name|gray} and @{…|json}. target: 'rcss'."""
    tree = dict(tokens)
    if extra:
        tree.update(extra)

    def replace(match):
        path, alpha, mode = match.group(1), match.group(2), match.group(3)
        value = lookup(tree, path, where)
        if mode == "json":
            return json.dumps(value, ensure_ascii=False)
        if mode == "gray":  # baked filter: grayscale(1) of the colour (Rec. 709 luma, as RmlUi's filter computes it)
            if not path.startswith("color."):
                raise DesignError(f"{where}: |gray takes a colour, not @{{{path}}}")
            r, g, b = rgb(value)
            luma = round(0.2126 * r + 0.7152 * g + 0.0722 * b)
            value = f"#{luma:02x}{luma:02x}{luma:02x}"
        elif mode is not None:
            raise DesignError(f"{where}: unknown token mode |{mode}")
        if path.startswith("color."):
            if alpha is None:
                return value
            r, g, b = rgb(value)
            if target == "rcss":  # RCSS takes the alpha as 0-255
                return f"rgba({r},{g},{b},{round(float(alpha) * 255)})"
            return f"rgba({r},{g},{b},{alpha})"
        if alpha is not None or isinstance(value, (dict, list)):
            raise DesignError(f"{where}: @{{{match.group(0)[2:-1]}}} is not a plain value")
        if isinstance(value, float) and value == int(value):
            value = int(value)
        return str(value)

    return REF.sub(replace, text)


# --------------------------------------------------------------------------- textures


def load_bake_specs():
    specs = json.loads((HERE / "textures-src" / "bake.json").read_text(encoding="utf-8"))
    specs.pop("$comment", None)
    return specs


def bake_digest(name, spec, tokens):
    """What a baked texture is made of: its source picture, its spec and the colours it uses."""
    digest = hashlib.sha256((HERE / "textures-src" / name).read_bytes())
    colours = {token: tokens["color"][token] for token, _ in spec["shade"]}
    digest.update(json.dumps([spec, colours], sort_keys=True).encode())
    return digest.hexdigest()


def check_baked_textures(tokens):
    """textures/ holds pictures baked by bake_textures.py (Pillow); fail when one is stale."""
    recorded_path = HERE / "textures" / "baked.json"
    recorded = json.loads(recorded_path.read_text(encoding="utf-8")) if recorded_path.exists() else {}
    for name, spec in load_bake_specs().items():
        if recorded.get(name) != bake_digest(name, spec, tokens) or not (HERE / "textures" / name).exists():
            raise DesignError(f"textures/{name} is stale (textures-src/{name} or bake.json changed): run mobile-ui/design/bake_textures.py")


IMAGE_REF = re.compile(r"images/[A-Za-z0-9_.-]+")


def check_images():
    """images/ holds the pack's own PNG pictures, shipped as they are: every one is named by a component or
    screen source and every name there is a file, so a typo never draws an empty box."""
    shipped = {f"images/{path.name}" for path in (HERE / "images").iterdir()}
    named = set()
    for path in sorted(HERE.glob("components/*/*")) + sorted(HERE.glob("screens/*/*")):
        named.update(IMAGE_REF.findall(path.read_text(encoding="utf-8")))
    problems = [f"{name} is not a .png" for name in sorted(shipped) if not name.endswith(".png")]
    problems += [f"{name} is named by a source but missing" for name in sorted(named - shipped)]
    problems += [f"{name} is not used by any component or screen" for name in sorted(shipped - named)]
    if problems:
        raise DesignError("; ".join(problems))


# --------------------------------------------------------------------------- icons


def icon_symbols():
    """The one icon source: icons.json (monochrome symbols with an optional accent layer, colour tiles)."""
    icons = json.loads((HERE / "icons.json").read_text(encoding="utf-8"))
    shared = [
        '<clipPath id="shade"><path d="M0 0Z"></path></clipPath>',
        '<clipPath id="nb"><circle cx="24" cy="24" r="20.5"></circle></clipPath>',
        '<mask id="mk-br" maskUnits="userSpaceOnUse" x="0" y="0" width="48" height="48"><rect width="48" height="48" fill="#fff"></rect>'
        '<circle cx="37" cy="37" r="14" fill="#000"></circle></mask>',
    ]
    symbols = []
    for ident, icon in icons["monochrome"].items():
        symbols.append((ident, "".join(icon["els"]), "acc" in icon))
        if "acc" in icon:
            symbols.append((ident + "-acc", "".join(icon["acc"]), False))
    for ident, els in icons["colour"].items():
        symbols.append((ident, "".join(els), False))
    return shared, symbols


def build_icons():
    shared, symbols = icon_symbols()
    wrap = lambda ident, body: f'<symbol id="{ident}" viewBox="0 0 48 48">{body}</symbol>'
    defs = ['<svg aria-hidden="true" width="0" height="0" style="position: absolute">', "<defs>", *shared]
    defs += [wrap(ident, body) for ident, body, _ in symbols]
    defs += ["</defs>", "</svg>"]
    ids = {ident for ident, _, _ in symbols}
    pack = {"defs": "".join(shared), "symbols": {}}
    for ident, body, accent in symbols:
        refs = set(re.findall(r'href="#([^"]+)"', body)) | set(re.findall(r"url\(#([^)]+)\)", body))
        entry = {"svg": wrap(ident, body)}
        deps = sorted(ref for ref in refs if ref in ids and ref != ident)
        if deps:
            entry["deps"] = deps
        if accent:
            entry["acc"] = True
        pack["symbols"][ident] = entry
    return pack


# ------------------------------------------------------------------------- strings

STRING_KEY_IN_TEMPLATE = re.compile(r"""\btr\(\s*'([^']+)'\s*\)""")
STRING_KEY_IN_CODE = re.compile(r"""\b(?:Text|StringKey)\(\s*"([^"]+)"\s*\)""")
# The engine outside its UI library raises alerts by key (platform/android_ui_sink.hpp ui::alert).
STRING_KEY_IN_SCRIPT = re.compile(r"""\bUiText\(\s*"([^"]+)"\s*\)""")
STRING_KEY_IN_ALERT = re.compile(r"""\bui::alert\(\s*"([^"]+)"\s*\)""")


def load_strings(engine_root, check_engine=True):
    """UI strings: strings/<lang>.json, one flat {key: text} table per shipped language.
    Every language must define exactly the same keys, and every key a template (tr('key'))
    or the engine's UI code (Text("key") / StringKey("key") under src/libs/ui/src, ui::alert("key")
    anywhere in the engine) names must exist:
    anything else is a build error, never a missing text at run time. Missing engine sources are an
    error too, unless check_engine is False (--no-engine-check)."""
    tables = {path.stem: json.loads(path.read_text(encoding="utf-8")) for path in sorted((HERE / "strings").glob("*.json"))}
    if not tables:
        raise DesignError("strings/: no language tables")
    problems = []
    keys = set().union(*(table.keys() for table in tables.values()))
    for language, table in tables.items():
        for key in sorted(keys - table.keys()):
            problems.append(f"strings/{language}.json: key '{key}' is missing")
        for key, text in table.items():
            if not isinstance(text, str) or not text:
                problems.append(f"strings/{language}.json: '{key}' must be a non-empty string")
    used = {}
    for path in sorted(HERE.glob("components/*/template.rml")) + sorted(HERE.glob("screens/*/template.rml")) + sorted(HERE.glob("gallery/*.rml")):
        for key in STRING_KEY_IN_TEMPLATE.findall(path.read_text(encoding="utf-8")):
            used.setdefault(key, str(path.relative_to(HERE)))
    for path in sorted(PROGRAM.rglob("*.c")):
        for key in STRING_KEY_IN_SCRIPT.findall(path.read_text(encoding="utf-8")):
            used.setdefault(key, str(path.relative_to(REPO)))
    sources = engine_root / "src/libs/ui/src"
    if not check_engine:
        print("design build: UI string keys used by the engine screens are NOT checked (--no-engine-check)", file=sys.stderr)
    elif not sources.is_dir():
        raise DesignError(f"no engine sources at {sources}: pass --engine-root DIR (the engine tree), "
                          "or --no-engine-check to skip checking the engine's UI string keys")
    else:
        for path in sorted(sources.rglob("*.cpp")):
            for key in STRING_KEY_IN_CODE.findall(path.read_text(encoding="utf-8")):
                used.setdefault(key, f"engine {path.relative_to(engine_root)}")
        for path in sorted(p for part in ("libs", "apps") for suffix in ("*.cpp", "*.hpp") for p in (engine_root / "src" / part).rglob(suffix)):
            for key in STRING_KEY_IN_ALERT.findall(path.read_text(encoding="utf-8", errors="replace")):
                used.setdefault(key, f"engine {path.relative_to(engine_root)}")
    for key, where in sorted(used.items()):
        if key not in keys:
            problems.append(f"{where}: UI string '{key}' is not defined in strings/")
    if problems:
        raise DesignError("\n".join(problems))
    return tables


def check_hud_contract(engine_root, check_engine=True):
    """Every element an engine HUD scene may show (the engine's src/libs/ui/src/hud/layout-elements.json,
    which its HUD host checks hold the scenes to) must be in that scene of hud-layout.json: a missing
    one is a build error, never the error toast of a device («the layout has no element …»)."""
    if not check_engine:
        print("design build: the engine HUD scenes' elements are NOT checked against hud-layout.json (--no-engine-check)", file=sys.stderr)
        return
    contract = engine_root / "src/libs/ui/src/hud/layout-elements.json"
    if not contract.is_file():
        raise DesignError(f"no {contract}: the engine tree lists no HUD layout elements (pass the right --engine-root)")
    declared = json.loads(contract.read_text(encoding="utf-8"))["scenes"]
    scenes = json.loads((HERE / "hud-layout.json").read_text(encoding="utf-8"))["scenes"]

    def ids(elements):
        for element in elements:
            yield element.get("id")
            yield from ids(element.get("children", []))

    problems = []
    for scene, wanted in declared.items():
        if scene not in scenes:
            problems.append(f"hud-layout.json: no scene '{scene}' (the engine shows it; {contract.name})")
            continue
        present = set(ids(scenes[scene]["elements"]))
        for element in wanted:
            if element not in present:
                problems.append(f"hud-layout.json: scene '{scene}' has no element '{element}' (the engine shows it; {contract.name})")
    if problems:
        raise DesignError("\n".join(problems))


# ------------------------------------------------------------------ source checks

COLOUR_LITERAL = re.compile(r"#[0-9a-fA-F]{3,8}\b|rgba?\(")
COLOUR_ESCAPE = "colour-literal:"  # `/* colour-literal: why */` on the same line allows one


def check_colour_literals():
    """Colours in component and screen sources, base rules, the overlay, gallery pages and the HUD
    layout are token references only; a raw value is a build error."""
    sources = sorted(HERE.glob("components/*/*")) + sorted(HERE.glob("screens/*/*"))
    sources += [HERE / "base.rcss", HERE / "overlay.rml", HERE / "screen.rml", HERE / "hud.rml", HERE / "hud-layout.json"]
    sources += sorted(HERE.glob("gallery/*.rml"))
    problems = []
    for path in sources:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if COLOUR_LITERAL.search(line) and COLOUR_ESCAPE not in line:
                problems.append(f"{path.relative_to(HERE)}:{number}: raw colour {COLOUR_LITERAL.search(line).group(0)!r}; "
                                "use @{color.name} (add a token to tokens.json if none fits)")
    if problems:
        raise DesignError("\n".join(problems))


GENERIC_ATTRIBUTES = {"id", "class", "style", "props", "slot", "rmlui-layout-boundary"}  # engine components.cpp IsGenericAttribute
HUD_BEHAVIORS = {"hold", "swipe"}  # behaviours a HUD layout element may add (engine behaviors.cpp)
ATTRIBUTE = re.compile(r"""\s+([a-zA-Z_][-a-zA-Z0-9_:]*)\??\s*=\s*("[^"]*"|'[^']*')""")  # name?= is conditional
TAG = re.compile(r"<(c-[a-z0-9-]+)")


def check_references(components, icon_ids):
    """Every component tag, prop, enum value and icon id named in the gallery pages, the
    component templates and the HUD layout must exist: a typo is a build error, never a
    silently ignored attribute or an empty picture."""
    specs = {spec["tag"]: spec for spec in components}
    problems = []

    def kebab(name):
        return re.sub(r"[A-Z]", lambda m: "-" + m.group(0).lower(), name)

    def icons_in(value, where):
        if isinstance(value, dict):
            for key, item in value.items():
                if key in ("icon", "emptyIcon") and isinstance(item, str) and item and "{{" not in item and item not in icon_ids:
                    problems.append(f"{where}: unknown icon '{item}'")
                elif key == "flag" and isinstance(item, str) and f"nation-{item}" not in icon_ids:
                    problems.append(f"{where}: unknown flag icon 'nation-{item}'")
                icons_in(item, where)
        elif isinstance(value, list):
            for item in value:
                icons_in(item, where)

    def check_props(tag, props, where, enums=True):
        spec = specs[tag]
        for name, value in props.items():
            prop = spec["props"].get(name)
            if prop is None:
                problems.append(f"{where}: <{tag}> has no prop '{name}'")
                continue
            if enums and prop.get("type") == "enum" and isinstance(value, (str, int)) and "{{" not in str(value) \
                    and value not in prop["options"] and str(value) not in [str(o) for o in prop["options"]]:
                problems.append(f"{where}: <{tag}> {name}={value!r} is not one of {prop['options']}")
            if name in ("icon", "emptyIcon", "action") and isinstance(value, str) and value and "{{" not in value \
                    and value not in icon_ids and (name != "action" or tag == "c-list-row"):
                problems.append(f"{where}: unknown icon '{value}'")
        icons_in(props, where)

    def check_markup(path):
        text = path.read_text(encoding="utf-8")
        where = str(path.relative_to(HERE))
        for match in TAG.finditer(text):
            tag = match.group(1)
            line = text.count("\n", 0, match.start()) + 1
            if tag not in specs:
                problems.append(f"{where}:{line}: unknown component <{tag}>")
                continue
            props, at = {}, match.end()
            by_attribute = {kebab(name): name for name in specs[tag]["props"]}
            while True:
                attribute = ATTRIBUTE.match(text, at)
                if not attribute:
                    break
                at = attribute.end()
                name, value = attribute.group(1), attribute.group(2)[1:-1]
                if name in GENERIC_ATTRIBUTES or name.startswith("data-"):
                    if name == "props" and "{{" not in value:
                        try:
                            props.update(json.loads(value.replace("&#39;", "'")))
                        except json.JSONDecodeError as error:
                            problems.append(f"{where}:{line}: <{tag}> props is not JSON ({error})")
                    continue
                if name not in by_attribute:
                    problems.append(f"{where}:{line}: <{tag}> has no prop '{name}'")
                    continue
                prop = by_attribute[name]
                if specs[tag]["props"][prop].get("type") == "json" and "{{" not in value:
                    try:
                        value = json.loads(value)
                    except json.JSONDecodeError as error:
                        problems.append(f"{where}:{line}: <{tag}> {name} is not JSON ({error})")
                        continue
                props[prop] = value
            check_props(tag, props, f"{where}:{line}")
        for match in re.finditer(r'<c-icon\b[^>]*\bicon="([^"{]+)"', text):
            if match.group(1) not in icon_ids:
                problems.append(f"{where}: unknown icon '{match.group(1)}'")

    for path in (sorted(HERE.glob("gallery/*.rml")) + sorted(HERE.glob("components/*/template.rml")) +
                 sorted(HERE.glob("screens/*/template.rml"))):
        check_markup(path)
    for spec in components:
        icons_in(spec.get("constants", {}), f"components/{spec['_folder']}/spec.json")

    layout = json.loads((HERE / "hud-layout.json").read_text(encoding="utf-8"))

    def check_element(element, where):
        tag = element.get("component")
        if tag not in specs:
            problems.append(f"{where}: unknown component {tag!r}")
            return
        check_props(tag, element.get("props", {}), where + " props")
        check_props(tag, element.get("sample", {}), where + " sample")
        if element.get("behavior", "hold") not in HUD_BEHAVIORS:
            problems.append(f"{where}: behavior must be one of {sorted(HUD_BEHAVIORS)}, got {element.get('behavior')!r}")
        if not isinstance(element.get("data", {}), dict):
            problems.append(f"{where}: data must be an object of data-* attribute values")
        for child in element.get("children", []):
            check_element(child, f"{where}/{child.get('id')}")

    for scene, data in layout["scenes"].items():
        for element in data["elements"]:
            check_element(element, f"hud-layout.json: {scene}/{element.get('id')}")
    if problems:
        raise DesignError("\n".join(dict.fromkeys(problems)))


# Icons whose ids are built at run time from a game value, by prefix: where the prefix is put together.
RUNTIME_ICON_PREFIXES = {
    "perk-": "design/scripts.json: the character binding's perkIcons, by the perk id",
    "suit-": "screens/pieces/template.rml: suit-{{p.suit}}",
    "c-sky-": "components/compass/template.rml: 'c-sky-' + weather",
    "nation-": "the flags: \"nation-\" + the nation (engine screens/maps.cpp, hud/spyglass.cpp, …)",
}
QUOTED = re.compile(r"""["']([A-Za-z0-9_-]+)["']""")


def check_unused_icons(icon_ids, engine_root, check_engine=True):
    """Every icon of icons.json is drawn by something outside the gallery: a quoted id or name (the id without
    "i-") in a component, a screen, the HUD layout, the touch interface's scripts and their list or the engine's UI
    code, or an id the code builds from one of RUNTIME_ICON_PREFIXES. A gallery page alone does not keep an icon in the
    pack."""
    if not check_engine:
        print("design build: unused icons are NOT checked (--no-engine-check)", file=sys.stderr)
        return
    design = [*HERE.glob("components/*/*"), *HERE.glob("screens/*/*"),
              *(HERE / name for name in ("hud.rml", "overlay.rml", "screen.rml", "hud-layout.json", "base.rcss",
                                         "scripts.json")),
              *(HERE.parent / "PROGRAM/interface/touch").rglob("*.c")]
    engine = [p for part in ("ui/src", "xinterface/src") for p in (engine_root / "src/libs" / part).rglob("*")
              if p.suffix in (".cpp", ".h", ".hpp", ".inc", ".json")]
    texts = [path.read_text(encoding="utf-8", errors="replace") for path in sorted(design) + sorted(engine) if path.is_file()]
    quoted = {token for text in texts for token in QUOTED.findall(text)}
    everything = "\n".join(texts)
    problems = [f"RUNTIME_ICON_PREFIXES: no source builds '{prefix}' icons any more ({where})"
                for prefix, where in RUNTIME_ICON_PREFIXES.items() if prefix not in everything]
    for ident in sorted(icon_ids):
        name = ident[2:] if ident.startswith("i-") else ident
        if ident in quoted or name in quoted or any(name.startswith(prefix) for prefix in RUNTIME_ICON_PREFIXES):
            continue
        problems.append(f"icons.json: '{ident}' is drawn by nothing outside the gallery: remove it")
    if problems:
        raise DesignError("\n".join(problems))


# ---------------------------------------------------------------------- components


def load_components(tokens, only=None, root="components"):
    """Specs of components/, or of screens/: the <s-…> screens and screen parts that C++ fills."""
    components = []
    for folder in sorted(path for path in (HERE / root).iterdir() if path.is_dir()):
        if only and folder.name not in only:
            continue
        where = f"{root}/{folder.name}"
        spec_text = substitute((folder / "spec.json").read_text(encoding="utf-8"), tokens, "rcss", where + "/spec.json")
        spec = json.loads(spec_text)
        for key in ("tag", "props"):
            if key not in spec:
                raise DesignError(f"{where}/spec.json lacks '{key}'")
        for name, prop in spec["props"].items():
            if prop.get("type", "text") not in ("text", "bool", "int", "number", "enum", "json"):
                raise DesignError(f"{where}: prop {name} has unknown type {prop.get('type')}")
            if prop.get("type") == "enum" and not prop.get("options"):
                raise DesignError(f"{where}: enum prop {name} lacks options")
        spec["template"] = substitute((folder / "template.rml").read_text(encoding="utf-8"), tokens, "rcss", where + "/template.rml",
                                      {"spec": spec.get("constants", {})})
        style = folder / "style.rcss"
        spec["_style"] = substitute(style.read_text(encoding="utf-8"), tokens, "rcss", where + "/style.rcss",
                                    {"spec": spec.get("constants", {})}) if style.exists() else ""
        spec["_folder"] = folder.name
        components.append(spec)
    return components


# ------------------------------------------------------------------------ interface scripts

CAPABILITY_ID = re.compile(r"^[a-z][a-z0-9_]*$")


def load_capabilities():
    """capabilities.json: {id: what it is}, the one list of the touch interface's capability ids."""
    capabilities = json.loads((HERE / "capabilities.json").read_text(encoding="utf-8"))
    for ident, text in capabilities.items():
        if not CAPABILITY_ID.match(ident) or not isinstance(text, str) or not text:
            raise DesignError(f"capabilities.json: '{ident}' needs an id of [a-z0-9_] and a description")
    return capabilities


def capability_header(capabilities):
    """The capability ids for the scripts (PROGRAM/interface/touch/capabilities.h, generated when packaged)."""
    lines = ["// Generated by mobile-ui/design/build.py from capabilities.json: the touch interface's capability ids.", ""]
    lines += [f'#define TOUCH_CAP_{ident.upper()} "{ident}"' for ident in sorted(capabilities)]
    return "\n".join(lines) + "\n"


TOUCH = "interface/touch/"
# The layers of the interface's scripts, in the order they are attached; what each may call is SHARED_LAYERS below.
LAYERS = ("core", "lib", "contracts", "lineages", "screens", "hud", "games")
SHARED_LAYERS = {"core", "lib", "contracts", "screens", "hud"}
# The language's built-ins the shared layers call. Short on purpose: a shared script that needs another one is a
# decision, taken by adding it here.
SHARED_BUILTINS = {"GetEventData", "DeleteAttribute", "CheckAttribute", "makearef", "sti", "stf", "makeint",
                   "makefloat", "CopyAttributes", "GetAttributeName", "GetAttributeValue", "GetAttributesNum",
                   "GetAttributeN", "abs", "strlen", "strcut", "findSubStr", "Sprintf"}
GAME_NAMES = re.compile(r"\b(teho|gpk|coas|cl|caribbean legend|to each his own|sea dogs|city of abandoned ships|"
                        r"корсары|каждому сво[её])\b", re.IGNORECASE)
SCRIPT_TYPE = r"(?:void|int|float|string|object|aref|ref|bool)"
KEYWORDS = {"if", "while", "for", "switch", "return", "else", "case", "native", "extern"}


def script_code(text):
    """A script without its comments, and its string literals apart (both are kept out of the code)."""
    code, strings, i = [], [], 0
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"', re.DOTALL)
    for match in pattern.finditer(text):
        code.append(text[i:match.start()])
        if match.group().startswith('"'):
            strings.append(match.group()[1:-1])
            code.append('""')
        else:
            code.append("\n" * match.group().count("\n"))  # lines kept: names are reported by line
        i = match.end()
    code.append(text[i:])
    return re.sub(r"(?m)^[ \t]*#.*$", "", "".join(code)), strings  # directives are not code


def declared(code):
    """(name, offset) of every name a declaration of `code` declares: each declarator of `string a = f(x), b;`, of a
    parameter list and of a range-for."""
    names = []
    for match in re.finditer(r"\b" + SCRIPT_TYPE + r"\s+&?([A-Za-z_]\w*)", code):
        names.append((match.group(1), match.start(1)))
        depth, i = 0, match.end()
        while i < len(code) and code[i] not in ";{}":
            char = code[i]
            if char in "([":
                depth += 1
            elif char in ")]":
                depth -= 1
                if depth < 0:
                    break
            elif char == "," and depth == 0:
                following = re.match(r"\s*(?:" + SCRIPT_TYPE + r"\s+)?&?([A-Za-z_]\w*)", code[i + 1:])
                if following:
                    names.append((following.group(1), i + 1 + following.start(1)))
            i += 1
    return names


def top_level(code):
    """`code` with the text inside braces blanked, its lines and offsets kept: the declarations at a script's top."""
    out, depth = [], 0
    for char in code:
        if char == "{":
            depth += 1
            out.append("{" if depth == 1 else " ")
        elif char == "}":
            depth -= 1
            out.append(" ")
        else:
            out.append(char if depth == 0 or char == "\n" else " ")
    return "".join(out)


TOP_FUNCTION = re.compile(r"(?:native\s+|extern\s+)?" + SCRIPT_TYPE + r"\s+([A-Za-z_]\w*)\s*\(([^)]*)\)")
TOP_GLOBAL = re.compile(r"(?m)(?:^|(?<=;))[ \t]*" + SCRIPT_TYPE + r"\s+[A-Za-z_]\w*\s*(?:\[[^\]]*\])?\s*(?:[=,][^;]*)?;")


def script_definitions(code):
    """Top-level names of a script: {name: arguments} of functions (natives, externs, bodies) and {global: offset}."""
    functions, globals_ = {}, {}
    top = top_level(code)
    for match in TOP_FUNCTION.finditer(top):
        arguments = [part for part in match.group(2).split(",") if part.strip()]
        functions[match.group(1)] = len(arguments)
    top = re.sub(r"\([^)]*\)", lambda match: re.sub(r"[^\n]", " ", match.group()), top)  # parameter lists
    for statement in TOP_GLOBAL.finditer(top):
        for name, offset in declared(statement.group()):
            globals_.setdefault(name, statement.start() + offset)
    return functions, globals_


def game_names(program):
    """{lower name: {kind: (name, file, line)}} of the functions, globals and defines of every script file of a game's
    program directory, the first of each name and kind."""
    found = {}
    files = sorted(path for path in program.rglob("*") if path.suffix.lower() in (".c", ".h") and path.is_file())
    for path in files:
        text = path.read_text("cp1251", errors="replace")
        where = path.relative_to(program)
        names = [("define", match.group(1), match.start(1))
                 for match in re.finditer(r"(?m)^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)", text)]
        code = script_code(text)[0]
        globals_ = script_definitions(code)[1]
        top = top_level(code)
        names += [("function", match.group(1), match.start(1)) for match in TOP_FUNCTION.finditer(top)]
        names += [("global", name, offset) for name, offset in globals_.items()]
        for kind, name, offset in names:
            source = text if kind == "define" else code
            found.setdefault(name.lower(), {}).setdefault(kind, (name, where, source.count("\n", 0, offset) + 1))
    if not files:
        raise DesignError(f"--game: no script files below {program}")
    return found


def check_script_names(games):
    """The names of the interface scripts against every script file of the games, whichever segments of a game are
    loaded when the scripts compile: a function or global of ours is no function, global or define of a game (names are
    case-insensitive and program-wide), and a parameter or local of ours is no function of a game. A local or parameter
    named like a game's global or define is the local, in the game's compiler as in ours."""
    listed = json.loads((HERE / "scripts.json").read_text(encoding="utf-8"))
    ours = []
    for segment in listed["segments"]:
        code = script_code((PROGRAM / segment["file"]).read_text("utf-8"))[0]
        globals_ = script_definitions(code)[1]
        top = top_level(code)
        program_wide = {match.group(1).lower(): match.start(1) for match in TOP_FUNCTION.finditer(top)}
        program_wide.update({name.lower(): offset for name, offset in globals_.items()})
        seen = set()
        for name, offset in sorted(declared(code) + [(match.group(1), match.start(1)) for match in
                                                     TOP_FUNCTION.finditer(top)], key=lambda item: item[1]):
            if name.lower() not in seen:
                seen.add(name.lower())
                kinds = ("function", "global", "define") if program_wide.get(name.lower()) == offset else ("function",)
                ours.append((segment["file"], code.count("\n", 0, offset) + 1, name, kinds))
    problems = []
    for game in games:
        program = next((path for path in game.iterdir() if path.is_dir() and path.name.lower() == "program"), None)
        if program is None:
            raise DesignError(f"--game {game}: no program directory")
        names = game_names(program)
        for file, line, name, kinds in ours:
            for kind, (theirs, where, their_line) in names.get(name.lower(), {}).items():
                if kind not in kinds:
                    continue
                problems.append(f"{file}:{line}: '{name}' is the {kind} {theirs} of {game.name}/{program.name}/"
                                f"{where.as_posix()}:{their_line}")
    if problems:
        raise DesignError("\n".join(problems))


def camel(stem):
    return "".join(part[:1].upper() + part[1:] for part in stem.split("_"))


def script_layer(file):
    """The layer of a listed script: its directory below interface/touch/. The game hooks written before this structure
    (interface/android_*.c) count as a lineage; they are not under the naming rules until they move below touch/."""
    if not file.startswith(TOUCH):
        return "lineages", None
    parts = file[len(TOUCH):].split("/")
    if len(parts) < 2 or parts[0] not in LAYERS:
        raise DesignError(f"scripts.json {file}: not in a layer directory below {TOUCH} ({', '.join(LAYERS)})")
    return parts[0], parts


def script_prefixes(parts):
    """The prefixes a script's own names take, by its place."""
    layer, stem = parts[0], Path(parts[-1]).stem
    if layer == "core":
        return ("Ui",)
    if layer == "lib":
        return ("TouchLib_",)
    if layer == "contracts":
        return (f"TouchAdp{camel(stem)}_",)
    if layer == "lineages":
        own = {"legacy": "TouchLeg", "cl": "TouchCl"}.get(parts[1])
        if own is None or len(parts) != 3:
            raise DesignError(f"lineage {'/'.join(parts)}: lineages are legacy/<family>.c or cl/<family>.c "
                              "(lib.c: what the lineage's adapters share)")
        return (f"TouchAdp{camel(stem)}_", f"{own}{camel(stem)}_", f"{own}{camel(stem)}")
    if layer == "hud":
        return (f"TouchHud{camel(stem)}_", f"TouchHud{camel(stem)}")
    return (f"Touch{camel(stem)}_", f"Touch{camel(stem)}")


def lint_script(file, parts, text, uses, natives, contracts):
    """The rules of one script below interface/touch/ (SCRIPTS layering): its names, its calls, no game names, no
    UiModel outside the adapters, no writes into objects that are not its own."""
    problems = []
    code, strings = script_code(text)
    functions, globals_ = script_definitions(code)
    prefixes = script_prefixes(parts)
    layer = parts[0]
    for name in sorted(set(functions) | set(globals_)):
        own = name in prefixes or name.startswith(tuple(p for p in prefixes if p.endswith("_") or layer == "core"))
        if not own:
            problems.append(f"{file}: '{name}' does not start with {' or '.join(prefixes)}")
    if layer == "contracts" and not all(re.search(r"\bextern\s+" + SCRIPT_TYPE + r"\s+" + name + r"\b", code)
                                        for name in functions):
        problems.append(f"{file}: a contract declares extern functions only")
    if layer == "lineages":
        family = Path(parts[-1]).stem
        for name, arguments in functions.items():
            if name.startswith("TouchAdp") and contracts.get(family, {}).get(name) != arguments:
                problems.append(f"{file}: {name}/{arguments} is not declared so in contracts/{family}.c")
    for word in GAME_NAMES.findall(code + "\n" + "\n".join(strings)):
        problems.append(f"{file}: names a game ('{word}'); shared code and adapters are chosen by requirements")
    if layer in SHARED_LAYERS - {"core"} and re.search(r"\bUiScreens\b|\.model\b", code):
        problems.append(f"{file}: reads the model; only lineages/ and games/ read the game's widgets")
    if layer in SHARED_LAYERS:
        allowed = set(functions) | natives | SHARED_BUILTINS
        for family in uses:
            allowed |= set(contracts.get(family, {}))
        for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", code):
            name = match.group(1)
            if name in KEYWORDS or name in allowed or name.startswith("TouchLib_"):
                continue
            if re.match(SCRIPT_TYPE + r"$", name):
                continue
            problems.append(f"{file}: calls {name}; a shared layer calls natives, TouchLib_*, the contracts it uses "
                            "and the language's built-ins only")
    locals_ = {name for name, _ in declared(code)}
    code = re.sub(r"=\s*\{[^{}]*\}", "= 0", code)  # an object literal's fields are its own
    for match in re.finditer(r"(?:^|[;{}])\s*([A-Za-z_]\w*)\s*(?:[.\[][^;=]*)?=(?!=)", code):
        root = match.group(1)
        if root not in locals_ and not root.startswith("Touch"):
            problems.append(f"{file}: writes into {root}, which is not its own")
    for match in re.finditer(r"\b(?:DeleteAttribute|CopyAttributes)\s*\(\s*&?\s*([A-Za-z_]\w*)", code):
        if match.group(1) not in locals_ and not match.group(1).startswith("Touch"):
            problems.append(f"{file}: writes into {match.group(1)}, which is not its own")
    return problems, set(functions) | set(globals_)


def object_arguments(texts):
    """Calls of our functions and natives that pass an object (declared `object`) bare to an `aref` parameter: the call
    compiles, and the callee reads and writes another object than the one named. {file: text} -> problems."""
    codes = {file: script_code(text)[0] for file, text in texts.items()}
    params, globals_ = {}, set()
    for code in codes.values():
        top = top_level(code)
        for match in TOP_FUNCTION.finditer(top):
            params[match.group(1).lower()] = [re.match(r"\s*aref\b", part) is not None
                                              for part in match.group(2).split(",")]
        globals_ |= set(re.findall(r"\bobject\s+([A-Za-z_]\w*)", top))
    problems = []
    for file, code in codes.items():
        top = top_level(code)
        for function in TOP_FUNCTION.finditer(top):
            body_start = top.find("{", function.end())
            if body_start < 0 or top[function.end():body_start].strip():
                continue  # a declaration without a body
            depth, end = 0, body_start
            while True:
                depth += {"{": 1, "}": -1}.get(code[end], 0)
                if depth == 0:
                    break
                end += 1
            scope = code[function.start():end]
            objects = set(re.findall(r"\bobject\s+([A-Za-z_]\w*)", scope))
            others = {name for name, _ in declared(scope)} - objects
            named = (globals_ - others) | objects
            for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", scope):
                kinds = params.get(match.group(1).lower())
                if not kinds or match.start() == function.start(1) - function.start():
                    continue  # not ours, or the function's own name
                depth, i, start, arguments = 1, match.end(), match.end(), []
                while i < len(scope) and depth:
                    if scope[i] in "([":
                        depth += 1
                    elif scope[i] in ")]":
                        depth -= 1
                    if (scope[i] == "," and depth == 1) or depth == 0:
                        arguments.append(scope[start:i].strip())
                        start = i + 1
                    i += 1
                for k, argument in enumerate(arguments):
                    if k < len(kinds) and kinds[k] and argument in named:
                        line = code.count("\n", 0, function.start() + match.start()) + 1
                        problems.append(f"{file}:{line}: {match.group(1)} takes the object {argument} without '&' "
                                        "(it would read and write another object)")
    return problems


def check_scripts(capabilities, screens):
    """scripts.json: the segments the engine attaches to the game's program, in order (the engine reads their
    requirements, contracts and screens), and the rules of the scripts below interface/touch/. Every listed file
    exists; every script there is listed; the layers come in order and only core and lib are core; every contract used
    has its header and a provider before the user; adapters bind, screens name a capability and a pack screen."""
    listed = json.loads((HERE / "scripts.json").read_text(encoding="utf-8"))
    tags = {spec["tag"] for spec in screens}
    problems, provided, bound, names = [], {}, {}, {}
    files = [segment["file"] for segment in listed["segments"]]
    on_disk = {str(path.relative_to(PROGRAM)) for path in (PROGRAM / TOUCH).rglob("*.c")}
    problems += [f"scripts.json: {file} is not listed (or is dead)" for file in sorted(on_disk - set(files))]
    natives, contracts, rank = set(), {}, 0
    for path in sorted((PROGRAM / TOUCH / "core").glob("*.c")):
        natives |= set(re.findall(r"\bnative\s+" + SCRIPT_TYPE + r"\s+(Ui\w*)\s*\(", path.read_text("utf-8")))
    for path in sorted((PROGRAM / TOUCH / "contracts").glob("*.c")):
        contracts[path.stem] = script_definitions(script_code(path.read_text("utf-8"))[0])[0]
    for segment in listed["segments"]:
        file = segment["file"]
        where = f"scripts.json {file}"
        if not (PROGRAM / file).is_file():
            problems.append(f"{where}: no such file under PROGRAM/")
            continue
        layer, parts = script_layer(file)
        if LAYERS.index(layer) < rank:
            problems.append(f"{where}: {layer} comes before {LAYERS[rank]} (order: {', '.join(LAYERS)})")
        rank = max(rank, LAYERS.index(layer))
        if segment.get("core") and layer not in ("core", "lib"):
            problems.append(f"{where}: only core/ and lib/ are core")
        for contract in segment.get("provides", []):
            family = contract.removeprefix("adapter:")
            if layer != "lineages" or parts is None or Path(file).stem != family:
                problems.append(f"{where}: provides {contract}, which only lineages/<lineage>/{family}.c do")
            elif f"{TOUCH}contracts/{family}.c" not in files[:files.index(file)]:
                problems.append(f"{where}: provides {contract} before its contracts/{family}.c is listed")
            provided.setdefault(contract, file)
        uses = [contract.removeprefix("adapter:") for contract in segment.get("uses", [])]
        for contract in segment.get("uses", []):
            if contract not in provided:
                problems.append(f"{where}: uses {contract}, which no segment listed before it provides")
        for contract in segment.get("optional", []):
            family = contract.removeprefix("adapter:")
            if f"{TOUCH}contracts/{family}.c" not in files[:files.index(file)]:
                problems.append(f"{where}: optional {contract} before its contracts/{family}.c is listed")
        for binding in segment.get("binds", []):
            if not segment.get("provides"):
                problems.append(f"{where}: binds {binding['capability']} but provides no contract")
            if binding["capability"] not in capabilities:
                problems.append(f"{where}: capability '{binding['capability']}' is not in capabilities.json")
            bound.setdefault(binding["capability"], set()).update(segment.get("provides", []))
        for screen in segment.get("screens", []):
            if screen["capability"] not in capabilities:
                problems.append(f"{where}: capability '{screen['capability']}' is not in capabilities.json")
            if screen["component"] not in tags:
                problems.append(f"{where}: no pack screen <{screen['component']}>")
            if "interface" in screen and layer != "games":
                problems.append(f"{where}: only a game's own screen binds its interface itself; others use an adapter")
            if "interface" not in screen and not bound.get(screen["capability"], set()) & set(segment.get("uses", [])):
                problems.append(f"{where}: screen {screen['capability']} has no binding by a contract it uses")
        if parts is not None:
            found, defined = lint_script(file, parts, (PROGRAM / file).read_text("utf-8"), uses, natives, contracts)
            problems += found
            for name in defined:
                key = name.lower()
                if key in names and not (name.startswith("TouchAdp") and layer == "lineages") and \
                        not (layer == "contracts" or names[key].startswith(f"{TOUCH}contracts/")):
                    problems.append(f"{where}: '{name}' is defined in {names[key]} too")
                names.setdefault(key, file)
    problems += object_arguments({file: (PROGRAM / file).read_text("utf-8") for file in files
                                  if file.startswith(TOUCH) and (PROGRAM / file).is_file()})
    if problems:
        raise DesignError("\n".join(problems))


# ------------------------------------------------------------------------ HUD layout


def hud_length(value, where):
    """A length of a HUD element (a size, or an edge of a stretched axis): dp, or '<n>%' of the safe area."""
    if isinstance(value, str):
        if not re.fullmatch(r"\d+(\.\d+)?%", value):
            raise DesignError(f"{where} must be a number of dp or '<n>%', got {value!r}")
        return value
    return f"{value:g}dp"


def hud_rcss(element, where):
    """The position style of one HUD element inside the safe area of c-scene."""
    h, _, v = element.get("anchor", "").partition("-")
    if h not in ("left", "right", "center", "stretch") or v not in ("top", "bottom", "center", "stretch"):
        raise DesignError(f"{where}: anchor must be <left|right|center|stretch>-<top|bottom|center|stretch>, got {element.get('anchor')!r}")
    parts = ["position: absolute;"]
    shifts = []
    for side, axis, size, shift in ((h, ("left", "right"), "width", "X"), (v, ("top", "bottom"), "height", "Y")):
        if side == "stretch":  # both edges: the box grows and shrinks with the screen
            if axis[0] not in element or axis[1] not in element or size in element:
                raise DesignError(f"{where}: a stretched axis needs '{axis[0]}' and '{axis[1]}' and no '{size}'")
            parts.append(f"{axis[0]}: {hud_length(element[axis[0]], f'{where}: {axis[0]}')}; {axis[1]}: {hud_length(element[axis[1]], f'{where}: {axis[1]}')};")
        elif side == "center":
            offset = element.get(shift.lower(), 0)
            parts.append(f"{axis[0]}: 50%;")
            if size in element:  # a fixed box is centred exactly; otherwise by its own size
                parts.append(f"margin-{axis[0]}: {offset - element[size] / 2:g}dp;")
            else:
                shifts.append(f"translate{shift}(-50%)")
                if offset:
                    parts.append(f"margin-{axis[0]}: {offset:g}dp;")
        else:
            if side not in element:
                raise DesignError(f"{where}: anchor {element['anchor']} needs the offset '{side}'")
            parts.append(f"{side}: {element[side]:g}dp;")
    for size, css in (("width", "width"), ("height", "height"), ("maxWidth", "max-width"), ("maxHeight", "max-height")):
        if size in element:
            parts.append(f"{css}: {hud_length(element[size], f'{where}: {size}')};")
    if shifts:
        parts.append(f"transform: {' '.join(shifts)};")
    return " ".join(parts)


def hud_mirrored(element, notice_spec):
    """The element for «Под левую руку» (the layout mirrored left to right): the anchor's side and the
    left/right offsets swap, a centred offset changes sign; and the props that name a side follow
    (MIRROR_PROPS): a plate's alignment, a self-placed notice's side margins, a floating stick's rest point.
    Returns (element with the mirrored placement, the props that change)."""
    mirrored = dict(element)
    h, _, v = element.get("anchor", "").partition("-")
    if h in ("left", "right"):
        mirrored["anchor"] = ("right" if h == "left" else "left") + "-" + v
    for side in ("left", "right"):
        mirrored.pop(side, None)
    if "left" in element:
        mirrored["right"] = element["left"]
    if "right" in element:
        mirrored["left"] = element["right"]
    if h == "center" and element.get("x"):
        mirrored["x"] = -element["x"]
    props = element.get("props", {})
    changed = {}
    component = element["component"]
    if component == "c-hud-plate":
        align = props.get("align", "left")
        if align in ("left", "right"):
            changed["align"] = "right" if align == "left" else "left"
    if component == "c-notice" and props.get("place") == "hud":
        left, right = notice_spec["safe"][props.get("scene", "land")]
        changed["safeLeft"], changed["safeRight"] = props.get("safeRight", right), props.get("safeLeft", left)
    if component == "c-stick" and props.get("floating"):
        changed["mirrored"] = True  # its rest point counts from the zone's right edge
    return mirrored, changed


# Screens the HUD overlap check lays every scene out on (dp): the narrowest phone we support,
# a tall flagship phone (3200x1440 at 3.75), the design reference and a tablet.
HUD_CHECK_SCREENS = ((640, 360), (780, 360), (853, 384), (915, 412), (1280, 800))


def hud_rect(element, width, height, where, notice_spec=None):
    """The box (x0, y0, x1, y1) in dp that an element can take at its biggest on a width × height
    safe area: its own box, the declared `extent` of a box sized by its content, and a party list
    (`party`: max items, item size, gaps) wrapped as the plate wraps it."""
    def length(value, full):
        return float(value[:-1]) * full / 100 if isinstance(value, str) else float(value)

    extent = element.get("extent", {})
    if element["component"] == "c-notice" and element.get("props", {}).get("place") == "hud":
        # self-placed: the notice spec's offset and side margins of its scene
        scene = element["props"].get("scene", "land")
        left, right = notice_spec["safe"][scene]
        if "height" not in extent:
            raise DesignError(f"{where}: a placed notice needs extent.height for the overlap check")
        # under a top-centre plate the lines start lower (props.offset, as C-Notice's template)
        top = element["props"].get("offset", notice_spec["offsetHud"])
        return (left, top, width - right, top + extent["height"])
    h, _, v = element["anchor"].partition("-")

    def axis(side, size, start, end, full, cap):
        if side == "stretch":
            lo = length(element[start], full)
            return lo, full - length(element[end], full)
        span = element.get(size, extent.get(size))
        if span is None:
            raise DesignError(f"{where}: no {size} (nor extent.{size}) for the overlap check")
        span = length(span, full)
        if cap in element:
            span = min(span, length(element[cap], full))
        if side == "center":
            centre = full / 2 + element.get("x" if size == "width" else "y", 0)
            return centre - span / 2, centre + span / 2
        if side == start:
            lo = length(element[start], full)
            return lo, lo + span
        hi = full - length(element[end], full)
        return hi - span, hi

    x0, x1 = axis(h, "width", "left", "right", width, "maxWidth")
    y0, y1 = axis(v, "height", "top", "bottom", height, "maxHeight")
    party = element.get("party")
    if party:
        # a column wraps into further columns below the plate's height (a row into further rows)
        count, (item_w, item_h), (gap_w, gap_h) = party["max"], party["item"], party["gap"]
        if element.get("props", {}).get("direction", "column") == "column":
            per = max(1, int((y1 - y0 + gap_h) // (item_h + gap_h)))
            lines = -(-count // per)
            x1 = x0 + lines * item_w + (lines - 1) * gap_w
            y1 = y0 + min(count, per) * (item_h + gap_h) - gap_h
        else:
            per = max(1, int((x1 - x0 + gap_w) // (item_w + gap_w)))
            lines = -(-count // per)
            y1 = y0 + lines * item_h + (lines - 1) * gap_h
            x1 = x0 + min(count, per) * (item_w + gap_w) - gap_w
    return (x0, y0, x1, y1)


def floating_rest(element, zone, rest_specs):
    """The box a floating stick (props.floating) shows at rest in its zone: restLeft / restBottom
    from the zone's bottom left corner, `size` wide, as high (the helm: its capsule's height)."""
    props = element["props"]
    size = props["size"]
    height = rest_specs["c-stick"]["helmHeight"] if props.get("kind") == "helm" else size
    x0, y1 = zone[0] + props.get("restLeft", 0), zone[3] - props.get("restBottom", 0)
    return (x0, y1 - height, x0 + size, y1)


def hud_rest_specs(tokens):
    """The component constants floating_rest measures a stick at rest by."""
    return {"c-stick": json.loads(substitute((HERE / "components/stick/spec.json").read_text(encoding="utf-8"), tokens, "rcss",
                                             "components/stick/spec.json"))["constants"]}


def check_hud_overlap(layout, notice_spec, rest_specs):
    """The overlaps of HUD elements on the screens of HUD_CHECK_SCREENS, as text; the rules are in
    README.md, "HUD layout and the safe area"."""
    areas = {name: area for name, area in layout.get("areas", {}).items() if not name.startswith("$")}
    found = []
    for width, height in HUD_CHECK_SCREENS:
        for name, scene in layout["scenes"].items():
            boxes = []
            for element in scene["elements"]:
                if element.get("overlay"):
                    continue
                where = f"hud-layout.json: {name}/{element.get('id')}"
                placed = dict(element)
                if "area" in element:
                    area = areas[element["area"]]
                    for side in area["anchor"].split("-"):
                        placed[side] = area[side] + element[side]
                boxes.append((element, hud_rect(placed, width, height, where, notice_spec)))
            # a band that follows a list begins right of its last column; under its minimum width
            # it drops under the top row (sea.cpp FollowFleet, notice spec "band")
            rects = {e["id"]: r for e, r in boxes}
            for k, (element, r) in enumerate(boxes):
                after = rects[element["follows"]][2] + 8 if element.get("follows") in rects else 0
                r = (max(r[0], after), r[1], r[2], r[3])
                band = notice_spec.get("band", {}).get(element.get("props", {}).get("scene", "")) if element["component"] == "c-notice" else None
                if band and r[2] - r[0] < band["min"]:
                    drop = band["dropped"]
                    r = (max(drop["left"], after), drop["top"], width - drop["right"], drop["top"] + r[3] - r[1])
                boxes[k] = (element, r)
            def meet(p, q):
                return p[0] < q[2] and q[0] < p[2] and p[1] < q[3] and q[1] < p[3]

            ids = [e["id"] for e, _ in boxes]
            for i, (a, ra) in enumerate(boxes):
                floating = a.get("props", {}).get("floating")
                if floating:
                    rest = floating_rest(a, ra, rest_specs)
                    if rest[0] < ra[0] or rest[1] < ra[1] or rest[2] > ra[2] or rest[3] > ra[3]:
                        found.append(f"{width}x{height} {name}: the {a['id']} control at rest leaves its zone")
                for over in a.get("under", []):
                    if over not in ids[i + 1:]:
                        found.append(f"{width}x{height} {name}: {a['id']} lies under {over}, which is not listed after it")
                zone = floating and not a.get("clear")
                for b, rb in boxes[i + 1:]:
                    if b["id"] in a.get("replaces", []) or a["id"] in b.get("replaces", []):
                        continue
                    if zone and a["id"] in b.get("offWith", []):
                        rb = floating_rest(b, rb, rest_specs)  # its zone is off while this one is shown
                    if zone or b["id"] in a.get("under", []):
                        # event lines come and go over a resting (dimmed) stick; controls and plates stay
                        if b["component"] != "c-notice" and meet(floating_rest(a, ra, rest_specs), rb):
                            found.append(f"{width}x{height} {name}: the {a['id']} control at rest overlaps {b['id']}")
                    elif meet(ra, rb):
                        found.append(f"{width}x{height} {name}: {a['id']} overlaps {b['id']}")
    return found


def build_hud(tokens):
    """hud-layout.json for the pack (with the ready `rcss` of every element) and one
    generated gallery page per scene, laid out from the same data."""
    layout = json.loads(substitute((HERE / "hud-layout.json").read_text(encoding="utf-8"), tokens, "rcss", "hud-layout.json"))
    notice = json.loads(substitute((HERE / "components/notice/spec.json").read_text(encoding="utf-8"), tokens, "rcss", "components/notice/spec.json"))
    overlaps = check_hud_overlap(layout, notice["constants"], hud_rest_specs(tokens))
    if overlaps:
        raise DesignError("HUD elements overlap (hud-layout.json; with every party list at its maximum):\n  " + "\n  ".join(overlaps))
    pages = {}

    def tag(element, style):
        props = dict(element.get("props", {}), **element.get("sample", {}))
        attrs = f' id="{element["id"]}"' if "id" in element else ""
        if props:
            attrs += " props='" + json.dumps(props, ensure_ascii=False).replace("'", "&#39;") + "'"
        if "behavior" in element:
            attrs += f' data-behavior="{element["behavior"]}"'
        for key, value in element.get("data", {}).items():
            attrs += f' data-{key}="{value}"'
        if style:
            attrs += f' style="{style}"'
        inner = "".join(tag(child, "width: 100%;") for child in element.get("children", []))
        return f'<{element["component"]}{attrs}>{inner}</{element["component"]}>'

    areas = {name: area for name, area in layout.get("areas", {}).items() if not name.startswith("$")}

    def placed(element, where):
        """An element in a shared area: its offsets are from the area's corner, and the area's
        own offsets (one source for every scene that uses it) are added."""
        if "area" not in element:
            return element
        area = areas.get(element["area"])
        if area is None:
            raise DesignError(f"{where}: unknown area {element['area']!r}")
        if element.get("anchor") != area.get("anchor") or any(side in area["anchor"] for side in ("stretch", "center")):
            raise DesignError(f"{where}: an element of area {element['area']!r} takes its anchor {area.get('anchor')!r} (left|right-top|bottom)")
        result = dict(element)
        for side in area["anchor"].split("-"):
            if side not in element:
                raise DesignError(f"{where}: anchor {element['anchor']} needs the offset '{side}'")
            result[side] = area[side] + element[side]
        # Inside the area: the scenes that share it fill the same place.
        h, _, v = area["anchor"].partition("-")
        for side, size in ((h, "width"), (v, "height")):
            if not isinstance(element.get(size), (int, float)) or element[side] < 0 or element[side] + element[size] > area[size]:
                raise DesignError(f"{where}: does not lie inside area {element['area']!r} ({size} {area[size]})")
        return result

    for name, scene in layout["scenes"].items():
        lines, mirrored_lines = [], []
        for element in scene["elements"]:
            where = f"hud-layout.json: {name}/{element.get('id')}"
            selfplaced = element["component"] == "c-notice" and element.get("props", {}).get("place") == "hud"
            mirror, mirror_props = hud_mirrored(placed(element, where), notice["constants"])
            element["rcss"] = "" if selfplaced else hud_rcss(placed(element, where), where)
            # «Под левую руку»: the runtime takes these instead (UI host, Host::SetDisplayOptions)
            element["rcssMirrored"] = "" if selfplaced else hud_rcss(mirror, where + " (mirrored)")
            element["propsMirrored"] = mirror_props
            # A top status the toasts must not cover (the cabin captain, the boarding bars): while it is
            # shown, the host's toast stack starts below it (toastClear: its bottom from the safe top, dp).
            for key in ("clear", "under", "offWith"):
                element.pop(key, None)  # the overlap check's alone
            if element.pop("toastsBelow", False):
                height = element.get("height", element.get("extent", {}).get("height"))
                if not element.get("anchor", "").endswith("-top") or not isinstance(element.get("top"), (int, float)) or not isinstance(height, (int, float)):
                    raise DesignError(f"{where}: toastsBelow needs a top anchor, a numeric top and a height (or extent height)")
                element["toastClear"] = element["top"] + height
            if element.get("gallery", True):
                lines.append("  " + tag(element, element["rcss"]))
                mirrored_lines.append("  " + tag(dict(element, props=dict(element.get("props", {}), **mirror_props)), element["rcssMirrored"]))
        for suffix, body in (("", lines), ("-mirrored", mirrored_lines)):
            pages[f"gallery/hud-{name}{suffix}.rml"] = (
                "<rml>\n<head>\n"
                f"<title>HUD: {name}{' (left-handed)' if suffix else ''}</title>\n"
                '<link type="text/rcss" href="../kit.rcss"/>\n'
                "</head>\n<body>\n"
                f"<!-- Generated by build.py from hud-layout.json (scene {name}). -->\n"
                f'<c-scene kind="{scene.get("backdrop", "land")}">\n' + "\n".join(body) + "\n</c-scene>\n</body>\n</rml>\n")
    return json.dumps(layout, ensure_ascii=False, indent=1), pages


# ---------------------------------------------------------------------------- pack


def build_pack(tokens, components, icon_pack, strings, only=None):
    files = {}
    for language, table in strings.items():
        files[f"strings/{language}.json"] = json.dumps(table, ensure_ascii=False, indent=1, sort_keys=True)
    styles = [substitute((HERE / "base.rcss").read_text(encoding="utf-8"), tokens, "rcss", "base.rcss")]
    entries = []
    for spec in components:
        if spec["_style"]:
            styles.append(f"/* ---- {spec['tag']} ---- */\n{spec['_style']}")
        entries.append({key: value for key, value in spec.items() if not key.startswith("_") and key != "doc"})
        for prop in entries[-1]["props"].values():
            prop.pop("doc", None)
    files["kit.rcss"] = "\n".join(styles)
    files["components.json"] = json.dumps({"components": entries}, ensure_ascii=False, indent=1)
    files["icons.json"] = json.dumps(icon_pack, ensure_ascii=False)
    files["scripts.json"] = (HERE / "scripts.json").read_text(encoding="utf-8")
    files["hud-layout.json"], hud_pages = build_hud(tokens)
    for document in ("overlay.rml", "screen.rml", "hud.rml"):
        files[document] = substitute((HERE / document).read_text(encoding="utf-8"), tokens, "rcss", document)
    documents = []
    for source in sorted((HERE / "gallery").glob("*.rml")):
        if only and source.stem not in only:
            continue
        files[f"gallery/{source.name}"] = substitute(source.read_text(encoding="utf-8"), tokens, "rcss", f"gallery/{source.name}")
        documents.append(f"gallery/{source.name}")
    if not only:
        files.update(hud_pages)
        documents += sorted(hud_pages)
    fonts = tokens["font"]["bodyFiles"] + tokens["font"]["scriptFiles"]
    # What the screens and the HUD draw, made ahead by the runtime (engine src/libs/ui/src/prewarm.h, Host::Prewarm):
    # written and checked by the engine's storm_ui_pack_check --prewarm over the recorded models.
    prewarm = None
    if not only:
        text = (HERE / "prewarm.json").read_text(encoding="utf-8")
        listed = json.loads(text)
        if not isinstance(listed, dict) or not isinstance(listed.get("layoutWidth"), (int, float)) or \
                not isinstance(listed.get("fonts"), list) or not isinstance(listed.get("icons"), list):
            raise DesignError("prewarm.json: expected {layoutWidth, fonts: [font uses], icons: [icon uses]}")
        files["prewarm.json"] = text
        prewarm = "prewarm.json"
    files["pack.json"] = json.dumps({
        "fonts": [f"fonts/{name}" for name in fonts],
        "components": "components.json",
        "icons": "icons.json",
        "scripts": "scripts.json",
        "overlay": "overlay.rml",
        "screen": "screen.rml",
        "hud": "hud.rml",
        "strings": {language: f"strings/{language}.json" for language in sorted(strings)},
        "hudLayout": "hud-layout.json",
        "gallery": documents,
        "settings": tokens["time"],
        **({"prewarm": prewarm} if prewarm else {}),
    }, ensure_ascii=False, indent=1)
    return files, fonts


def write_tree(root, files):
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="validate the sources without writing the pack")
    parser.add_argument("--pack", type=Path, default=DEFAULT_PACK, help="where to write the runtime UI pack")
    parser.add_argument("--engine-root", type=Path, default=REPO.parent / "engine", metavar="DIR",
                        help="engine tree whose screens are checked for undefined UI string keys (default: engine/)")
    parser.add_argument("--no-engine-check", action="store_true",
                        help="do not check the engine's UI string keys")
    parser.add_argument("--only", metavar="NAMES", help="comma-separated component folders: build a partial pack with just these "
                        "(and their gallery pages); for work on one component")
    parser.add_argument("--game", type=Path, action="append", default=[], metavar="DIR",
                        help="an installed game (repeatable): no name the interface scripts declare is a function, "
                        "global or define of any of its script files")
    parser.add_argument("--script-header", type=Path, metavar="FILE", help="only write the capability header of the "
                        "interface scripts to FILE")
    args = parser.parse_args()
    if args.script_header:
        try:
            text = capability_header(load_capabilities())
        except (DesignError, json.JSONDecodeError) as error:
            print(f"design build failed: {error}", file=sys.stderr)
            return 1
        args.script_header.parent.mkdir(parents=True, exist_ok=True)
        args.script_header.write_text(text, encoding="utf-8")
        return 0
    try:
        tokens = load_tokens()
        check_colour_literals()
        check_baked_textures(tokens)
        check_images()
        only = set(args.only.split(",")) | {"icon"} if args.only else None
        components = load_components(tokens, only)
        icon_pack = build_icons()
        screens = [] if only else load_components(tokens, root="screens")
        if not only:
            check_scripts(load_capabilities(), screens)
            check_script_names(args.game)
        if not only:
            names = {ident[2:] if ident.startswith("i-") else ident for ident in icon_pack["symbols"] if not ident.endswith("-acc")}
            check_references(components + screens, names)
        if not only:
            check_unused_icons([ident for ident in icon_pack["symbols"] if not ident.endswith("-acc")],
                               args.engine_root, not args.no_engine_check)
        strings = load_strings(args.engine_root, not args.no_engine_check)
        check_hud_contract(args.engine_root, not args.no_engine_check)
        pack, fonts = build_pack(tokens, components + screens, icon_pack, strings, only)
    except (DesignError, json.JSONDecodeError, KeyError) as error:
        print(f"design build failed: {error}", file=sys.stderr)
        return 1
    if args.check:
        return 0
    if args.pack.exists():
        shutil.rmtree(args.pack)
    write_tree(args.pack, pack)
    (args.pack / "fonts").mkdir(parents=True, exist_ok=True)
    for name in fonts:
        shutil.copyfile(HERE / "fonts" / name, args.pack / "fonts" / name)
    if not only or {"screen", "header"} & only:
        shutil.copytree(HERE / "textures", args.pack / "textures", ignore=shutil.ignore_patterns("baked.json"))
    if not only:
        shutil.copytree(HERE / "images", args.pack / "images")
    print(f"pack: {len(pack)} files, {len(components)} components, {len(icon_pack['symbols'])} icon symbols -> {args.pack}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
