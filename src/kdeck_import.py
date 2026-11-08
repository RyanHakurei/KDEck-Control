#!/usr/bin/env python3
"""Turn an official Elgato .streamDeckProfile into a KDEck Control profile.

A .streamDeckProfile is a zip: a root manifest.json plus one folder per page
under Profiles/. Each page lists keys as "column,row". This keeps titles,
colours, and pictures, and turns the built-in actions into deck keys.

Key pictures fill the key. The profile's colours replace the default theme,
and its top-screen image (the encoder background, or a wallpaper shipped
beside the profile) is the panel.

  hotkey            keys / sequence
  website, open     exec (xdg-open, or the program when it is already a path)
  multimedia        media, or a volume key
  folder, back      page
  switch profile    profile, when the file names the target
  hotkey multi      sequence

A plugin action (OBS, Discord, and the rest) keeps its title and picture.
KDEck Control has no plugin host, so that key does nothing until it is assigned.
Dials stay the deck's own dials. A 3-column deck keeps its key positions. Any
other shape is poured into 3-by-4 pages in reading order.
"""

import argparse
import io
import json
import os
import re
import shlex
import sys
import zipfile

DECK_COLS, DECK_ROWS, DECK_KEYS = 3, 4, 12

# Elgato hardware id -> (columns, rows). Unknown models are measured from the keys.
MODELS = {
    "20GBA9901": (5, 3), "20GBA9902": (5, 3), "20GAT9901": (5, 3), "20GAT9902": (5, 3),
    "20GBA9903": (3, 2), "20GAI9901": (3, 2),
    "20GBA9911": (8, 4),
    "10GBD9901": (4, 2), "20GBD9901": (4, 2),
    "GRETSCH": (3, 4),  # Corsair Galleon 100 SD, as named in exported profiles
}
DEVICE_TYPES = {0: (5, 3), 1: (3, 2), 2: (8, 4), 7: (4, 2), 9: (4, 2), 12: (3, 4)}

# Qt::Key values Elgato stores as QTKeyCode. Letters and digits are ASCII.
QT_KEYS = {
    16777216: "ESC", 16777217: "TAB", 16777219: "BACKSPACE", 16777220: "ENTER", 16777221: "KPENTER",
    16777222: "INSERT", 16777223: "DELETE", 16777224: "PAUSE", 16777225: "SYSRQ",
    16777232: "HOME", 16777233: "END", 16777234: "LEFT", 16777235: "UP", 16777236: "RIGHT",
    16777237: "DOWN", 16777238: "PAGEUP", 16777239: "PAGEDOWN",
    16777248: "LEFTSHIFT", 16777249: "LEFTCTRL", 16777250: "LEFTMETA", 16777251: "LEFTALT",
    16777252: "CAPSLOCK", 16777253: "NUMLOCK", 16777254: "SCROLLLOCK",
    16777328: "VOLUMEDOWN", 16777329: "MUTE", 16777330: "VOLUMEUP",
    16777344: "PLAYPAUSE", 16777345: "STOPCD", 16777346: "PREVIOUSSONG", 16777347: "NEXTSONG",
}
QT_KEYS.update({16777264 + i: f"F{i + 1}" for i in range(24)})
QT_EMPTY = 33554431

# Windows virtual-key codes, for exports whose Qt code is missing.
VK = {
    0x08: "BACKSPACE", 0x09: "TAB", 0x0D: "ENTER", 0x13: "PAUSE", 0x14: "CAPSLOCK",
    0x1B: "ESC", 0x20: "SPACE", 0x21: "PAGEUP", 0x22: "PAGEDOWN", 0x23: "END", 0x24: "HOME",
    0x25: "LEFT", 0x26: "UP", 0x27: "RIGHT", 0x28: "DOWN", 0x2C: "SYSRQ", 0x2D: "INSERT", 0x2E: "DELETE",
    0x5B: "LEFTMETA", 0x5C: "RIGHTMETA", 0x60: "KP0", 0x61: "KP1", 0x62: "KP2", 0x63: "KP3",
    0x64: "KP4", 0x65: "KP5", 0x66: "KP6", 0x67: "KP7", 0x68: "KP8", 0x69: "KP9",
    0x6A: "KPASTERISK", 0x6B: "KPPLUS", 0x6D: "KPMINUS", 0x6E: "KPDOT", 0x6F: "KPSLASH",
    0x90: "NUMLOCK", 0x91: "SCROLLLOCK", 0xA0: "LEFTSHIFT", 0xA1: "RIGHTSHIFT", 0xA2: "LEFTCTRL",
    0xA3: "RIGHTCTRL", 0xA4: "LEFTALT", 0xA5: "RIGHTALT", 0xAD: "MUTE", 0xAE: "VOLUMEDOWN",
    0xAF: "VOLUMEUP", 0xB0: "NEXTSONG", 0xB1: "PREVIOUSSONG", 0xB2: "STOPCD", 0xB3: "PLAYPAUSE",
    0xBA: "SEMICOLON", 0xBB: "EQUAL", 0xBC: "COMMA", 0xBD: "MINUS", 0xBE: "DOT", 0xBF: "SLASH",
    0xC0: "GRAVE", 0xDB: "LEFTBRACE", 0xDC: "BACKSLASH", 0xDD: "RIGHTBRACE", 0xDE: "APOSTROPHE",
}
VK.update({0x70 + i: f"F{i + 1}" for i in range(24)})
VK.update({ord(ch): ch for ch in "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"})

PUNCT = {
    " ": "SPACE", "`": "GRAVE", "-": "MINUS", "=": "EQUAL", "[": "LEFTBRACE", "]": "RIGHTBRACE",
    "\\": "BACKSLASH", ";": "SEMICOLON", "'": "APOSTROPHE", ",": "COMMA", ".": "DOT", "/": "SLASH",
}

MEDIA = {
    "play": "play-pause", "pause": "play-pause", "playpause": "play-pause", "play/pause": "play-pause",
    "toggle": "play-pause", "next": "next", "nexttrack": "next", "previous": "previous",
    "prev": "previous", "previoustrack": "previous", "stop": "stop",
}
VOLUME = {"mute": "MUTE", "volumeup": "VOLUMEUP", "volumedown": "VOLUMEDOWN", "up": "VOLUMEUP", "down": "VOLUMEDOWN"}

def resolve_config_dir():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    current = os.path.join(base, "kdeck-control")
    if os.path.isfile(os.path.join(current, "config.toml")):
        return current
    previous = os.path.join(base, "galleon-deck")
    if os.path.isfile(os.path.join(previous, "config.toml")):
        return previous
    return current


CONFIG_DIR = resolve_config_dir()


class ProfileImportError(Exception):
    pass


class ImportResult:
    def __init__(self, name, path, pages, actions, kept, notes):
        self.name = name
        self.path = path
        self.pages = pages
        self.actions = actions
        self.kept = kept
        self.notes = notes


def profile_folder_id(profile_id):
    """Elgato's Profiles/<id> directory name for a page UUID.

    The id is the UUID in base 32, with V rewritten to W and U to V, plus a Z.
    Checked against folder names from exported profiles.
    """
    hexed = profile_id.replace("-", "") + "000"
    alphabet = "0123456789abcdefghijklmnopqrstuv"
    parts = []
    for i in range(0, len(hexed), 5):
        n = int(hexed[i:i + 5], 16)
        digits = []
        while n:
            n, rem = divmod(n, 32)
            digits.append(alphabet[rem])
        parts.append(("".join(reversed(digits)) or "0").rjust(4, "0"))
    return ("".join(parts)[:26].upper().replace("V", "W").replace("U", "V")) + "Z"


def slug(name):
    text = re.sub(r"[^a-z0-9._-]+", "-", str(name).strip().lower().replace(" ", "-"))
    text = re.sub(r"-{2,}", "-", text).strip("-._")
    return text or "imported"


def _toml_str(text):
    out = ['"']
    for ch in str(text):
        o = ord(ch)
        if ch in '"\\':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif o < 0x20:
            out.append(f"\\u{o:04x}")
        else:
            out.append(ch)
    return "".join(out) + '"'


def _toml_value(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return _toml_str(value)
    if isinstance(value, dict):
        if not value:
            return "{}"
        return "{ " + ", ".join(f"{k} = {_toml_value(v)}" for k, v in value.items()) + " }"
    raise TypeError(f"can't write {type(value).__name__} to TOML")


def render_profile(profile, header):
    """The same shape write_profile() in kdeck_control writes."""
    lines = [ln.rstrip("\n") for ln in header]
    while lines and not lines[-1].strip():
        lines.pop()
    if lines:
        lines.append("")
    for key in ("theme", "start_page", "logo", "screen"):
        if profile.get(key):
            lines.append(f"{key} = {_toml_value(profile[key])}")
    if profile.get("theme_overrides"):
        lines += ["", "[theme_overrides]"]
        lines += [f"{key} = {_toml_value(value)}" for key, value in profile["theme_overrides"].items()]
    for page in profile["page"]:
        keys = list(page.get("keys", []))
        while keys and not keys[-1]:
            keys.pop()
        lines += ["", "[[page]]", f"name = {_toml_value(page['name'])}", "keys = ["]
        lines += [f"  {_toml_value(k)}," for k in keys]
        lines.append("]")
    return "\n".join(lines) + "\n"


def _lower_map(names):
    """Map a normalised lower-case path to the name the archive actually stores."""
    out = {}
    for name in names:
        norm = name.replace("\\", "/")
        while norm.startswith("./"):
            norm = norm[2:]
        out[norm.lower()] = name
    return out


def _zip_bytes(blob):
    zf = zipfile.ZipFile(io.BytesIO(blob))

    def opener(name, zf=zf):
        return zf.read(name)
    return _lower_map(zf.namelist()), opener


def _read(source):
    """Return (members, opener). members maps a lower-case path to the real one."""
    if os.path.isdir(source):
        found = []
        for dirpath, _dirs, files in os.walk(source):
            for name in files:
                found.append(os.path.relpath(os.path.join(dirpath, name), source).replace(os.sep, "/"))

        def opener(name):
            with open(os.path.join(source, name), "rb") as f:
                return f.read()
        return _lower_map(found), opener
    if not zipfile.is_zipfile(source):
        raise ProfileImportError(f"{source} is not a .streamDeckProfile zip")
    with open(source, "rb") as f:
        return _zip_bytes(f.read())


def _profile_score(name):
    text = name.lower()
    return sum(word in text for word in ("galleon", "gretsch", "12 key", "12-key", "12key"))


def _unwrap_bundle(members, opener):
    """A Marketplace download is a zip of a .streamDeckProfile plus icons and a readme.

    The profile itself is the inner zip. A file that already is a profile is unchanged.
    """
    try:
        _profile_manifest(members, opener)
        return members, opener
    except ProfileImportError as first:
        nested = []
        for lower, real in members.items():
            if not lower.endswith(".streamdeckprofile"):
                continue
            blob = opener(real)
            if blob[:2] == b"PK":
                nested.append((real, blob))
        if not nested:
            raise ProfileImportError(
                "no profile manifest.json, and no .streamDeckProfile inside this archive") from first
        if len(nested) > 1:
            ranked = sorted(nested, key=lambda item: _profile_score(item[0]), reverse=True)
            if _profile_score(ranked[0][0]) <= _profile_score(ranked[1][0]):
                names = "\n".join(os.path.basename(name) for name, _blob in nested)
                raise ProfileImportError(
                    "this archive contains several profiles; choose the .streamDeckProfile file itself:\n" + names)
            nested = ranked[:1]
        return _zip_bytes(nested[0][1])


def _json_at(members, opener, name):
    try:
        return json.loads(opener(name).decode("utf-8-sig"))
    except (KeyError, UnicodeError, json.JSONDecodeError) as e:
        raise ProfileImportError(f"{name}: {e}") from None


def _profile_manifest(members, opener):
    """The root manifest of an export, or the nested one inside a packaged profile."""
    found = []
    for lower, real in members.items():
        if not lower.endswith("/manifest.json") and lower != "manifest.json":
            if not lower.endswith("manifest.json"):
                continue
        data = _json_at(members, opener, real)
        if isinstance(data, dict) and ("Pages" in data or "Device" in data) and "Controllers" not in data:
            found.append((lower.count("/"), lower, real, data))
    if not found:
        raise ProfileImportError("no profile manifest.json in this file")
    found.sort()
    _depth, _lower, real, data = found[0]
    return real, data


def _page_order(manifest):
    pages = manifest.get("Pages") if isinstance(manifest.get("Pages"), dict) else {}
    order = []
    default = pages.get("Default")
    if isinstance(default, str):
        order.append(default)
    for item in pages.get("Pages") or []:
        if isinstance(item, str) and item not in order:
            order.append(item)
    return order


def _coord(key):
    match = re.fullmatch(r"\s*(\d+)\s*,\s*(\d+)\s*", str(key))
    if not match:
        return None
    return int(match.group(1)), int(match.group(2))


def _keypad_actions(page):
    controllers = page.get("Controllers")
    if isinstance(controllers, list):
        keypad = [c for c in controllers if isinstance(c, dict) and c.get("Type", "Keypad") == "Keypad"]
        for controller in keypad or controllers:
            if isinstance(controller, dict) and isinstance(controller.get("Actions"), dict):
                return controller["Actions"]
    if isinstance(page.get("Actions"), dict):
        return page["Actions"]
    return {}


def _geometry(manifest, pages):
    device = manifest.get("Device") if isinstance(manifest.get("Device"), dict) else {}
    model = str(device.get("Model") or manifest.get("DeviceModel") or "")
    if model in MODELS:
        return MODELS[model]
    dtype = device.get("DeviceType", device.get("Type", manifest.get("DeviceType")))
    if isinstance(dtype, int) and dtype in DEVICE_TYPES:
        return DEVICE_TYPES[dtype]
    max_c = max_r = -1
    for actions in pages:
        for key in actions:
            pos = _coord(key)
            if pos:
                max_c, max_r = max(max_c, pos[0]), max(max_r, pos[1])
    if max_c < 0:
        return DECK_COLS, DECK_ROWS
    cols, rows = max_c + 1, max_r + 1
    # A deck no wider or taller than the Galleon keeps its coordinates. A wider
    # one is measured from the furthest key and poured into 3-column pages.
    if cols <= DECK_COLS and rows <= DECK_ROWS:
        return DECK_COLS, max(rows, 1)
    return cols, rows


def _load_pages(manifest_name, manifest, members, opener):
    manifest_norm = manifest_name.replace("\\", "/")
    while manifest_norm.startswith("./"):
        manifest_norm = manifest_norm[2:]
    root = os.path.dirname(manifest_norm)
    pages_root = f"{root}/Profiles" if root else "Profiles"
    order = _page_order(manifest)
    wanted = {}
    for uuid in order:
        for candidate in (uuid, uuid.upper(), uuid.lower(), profile_folder_id(uuid)):
            real = members.get(f"{pages_root}/{candidate}/manifest.json".lower())
            if real:
                wanted[uuid] = real
                break
    used = set(wanted.values())
    extras = []
    prefix = pages_root.lower() + "/"
    for lower, real in sorted(members.items()):
        if real in used or not lower.endswith("/manifest.json"):
            continue
        if not lower.startswith(prefix):
            continue
        # A packaged export nests the profile; skip a manifest that is the profile itself.
        if lower == manifest_norm.lower():
            continue
        extras.append(real)
    pages = []
    for uuid in order:
        if uuid in wanted:
            stored = wanted[uuid]
            norm = stored.replace("\\", "/")
            while norm.startswith("./"):
                norm = norm[2:]
            pages.append((uuid, norm, _json_at(members, opener, stored)))
    for real in extras:
        norm = real.replace("\\", "/")
        while norm.startswith("./"):
            norm = norm[2:]
        pages.append((None, norm, _json_at(members, opener, real)))
    if not pages and isinstance(manifest.get("Actions"), dict):
        pages.append((None, manifest_norm, manifest))
    if not pages:
        raise ProfileImportError("the profile has no pages")
    return pages


def _setting(settings, *keys):
    if not isinstance(settings, dict):
        return None
    folded = {str(k).lower(): v for k, v in settings.items()}
    for key in keys:
        value = folded.get(key.lower())
        if value not in (None, ""):
            return value
    return None


def _unquote(value):
    text = str(value).strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        text = text[1:-1]
    return text.strip()


def _kind(uuid):
    text = str(uuid or "").lower()
    if text.endswith(".hotkey"):
        return "hotkey"
    if text.endswith(".website"):
        return "website"
    if text.endswith(".open"):
        return "open"
    if text.endswith(".text"):
        return "text"
    if "multimedia" in text:
        return "media"
    if text.endswith(".volume"):
        return "volume"
    if "multiaction" in text or text.endswith(".routine") or text.endswith(".routine2"):
        return "multi"
    if text.endswith(".openchild") or text.endswith(".openfolder"):
        return "folder"
    if "backtoparent" in text:
        return "back"
    if text.endswith(".switchprofile") or text.endswith("profile.switch"):
        return "switch"
    if text.endswith(".page.next"):
        return "page-next"
    if text.endswith(".page.previous"):
        return "page-prev"
    if text.endswith(".page.goto"):
        return "page-goto"
    if text.endswith(".keys.stack"):
        return "stack"
    return "plugin"


def _ascii_key(code):
    if not isinstance(code, int) or not 32 <= code <= 126:
        return None
    ch = chr(code)
    if ch.isalpha():
        return ch.upper()
    if ch.isdigit():
        return ch
    return PUNCT.get(ch)


def _key_name(hotkey):
    qt = hotkey.get("QTKeyCode")
    if isinstance(qt, int) and qt not in (0, QT_EMPTY, -1):
        name = QT_KEYS.get(qt) or _ascii_key(qt)
        if name:
            return name
    vk = hotkey.get("VKeyCode")
    if isinstance(vk, int) and vk in VK:
        return VK[vk]
    native = hotkey.get("NativeCode")
    if isinstance(native, int) and native in QT_KEYS:
        return QT_KEYS[native]
    return None


def _modifiers(hotkey):
    flags = hotkey.get("KeyModifiers") or 0
    if not isinstance(flags, int):
        flags = 0
    mods = []
    if hotkey.get("KeyCtrl") or flags & 2:
        mods.append("CTRL")
    if hotkey.get("KeyShift") or flags & 1:
        mods.append("SHIFT")
    if hotkey.get("KeyOption") or flags & 4:
        mods.append("ALT")
    if hotkey.get("KeyCmd") or flags & 8:
        mods.append("SUPER")
    return mods


def _empty_slot(hotkey):
    if not isinstance(hotkey, dict):
        return True
    # A modifier with no key code is still a hotkey: Alt alone, Ctrl alone.
    if _modifiers(hotkey):
        return False
    vk = hotkey.get("VKeyCode")
    qt = hotkey.get("QTKeyCode")
    return vk in (-1, None) and qt in (QT_EMPTY, None, 0)


def _combo(hotkey):
    name = _key_name(hotkey)
    mods = _modifiers(hotkey)
    if name:
        mods = [m for m in mods if m.replace("LEFT", "").replace("RIGHT", "") not in name]
        return "+".join(mods + [name])
    if mods:
        return "+".join(mods)
    return None


def _hotkey_fields(settings):
    slots = settings.get("Hotkeys") if isinstance(settings, dict) else None
    if not isinstance(slots, list):
        slots = [settings] if isinstance(settings, dict) and "VKeyCode" in settings else []
    combos = []
    for slot in slots:
        if _empty_slot(slot):
            continue
        combo = _combo(slot)
        if combo:
            combos.append(combo)
    if not combos:
        return None
    if len(combos) == 1:
        return {"keys": combos[0]}
    return {"sequence": " ".join(combos)}


def _command_for_path(path):
    """A shell command for an Open/Website path, or None when it is a Windows path."""
    path = _unquote(path)
    if not path:
        return None
    if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*://", path) or path.startswith("mailto:"):
        return "xdg-open " + shlex.quote(path)
    if re.match(r"^[A-Za-z]:[\\/]", path) or path.startswith("\\\\") or "\\" in path:
        return None
    if "/" in path or path.startswith("~"):
        return "xdg-open " + shlex.quote(path)
    if re.fullmatch(r"[A-Za-z0-9_.:@+-]+", path):
        return path
    return None


def _media_fields(settings, kind):
    raw = _setting(settings, "action", "selectedAction", "multimedia", "volumeAction", "selectedOption")
    text = re.sub(r"[\s_-]", "", str(raw or "")).lower()
    if text in MEDIA:
        return {"media": MEDIA[text]}
    if text in VOLUME or kind == "volume" and text in VOLUME:
        return {"key": VOLUME[text]}
    if kind == "volume" and not text:
        return {"key": "MUTE"}
    return None


def _multi_steps(action):
    lanes = action.get("Actions")
    if isinstance(lanes, list) and lanes:
        first = lanes[0]
        if isinstance(first, dict) and isinstance(first.get("Actions"), list):
            return first["Actions"]
        if all(isinstance(item, dict) for item in lanes):
            return lanes
    settings = action.get("Settings") if isinstance(action.get("Settings"), dict) else {}
    for key in ("Routine", "routine", "Actions"):
        if isinstance(settings.get(key), list):
            return settings[key]
    return []


def _look(action):
    states = action.get("States") if isinstance(action.get("States"), list) else []
    index = action.get("State") if isinstance(action.get("State"), int) else 0
    state = states[index] if 0 <= index < len(states) and isinstance(states[index], dict) else {}
    if not state and states and isinstance(states[0], dict):
        state = states[0]
    title = "" if state.get("ShowTitle") is False else str(state.get("Title") or "")
    colour = state.get("TitleColor") or state.get("TitleColour")
    fg = colour if isinstance(colour, str) and re.fullmatch(r"#?[0-9A-Fa-f]{6}", colour.strip()) else None
    if isinstance(fg, str) and not fg.startswith("#"):
        fg = "#" + fg
    image = state.get("Image") if isinstance(state.get("Image"), str) else None
    return title.strip(), fg, image


def _selected_stack_item(action):
    """The action an Action Wheel had selected. The other wheel items are not keys."""
    if _kind(action.get("UUID") or (action.get("Plugin") or {}).get("UUID")) != "stack":
        return None
    nested = [item for item in action.get("Actions") or [] if isinstance(item, dict)]
    if not nested:
        return None
    settings = action.get("Settings") if isinstance(action.get("Settings"), dict) else {}
    index = settings.get("CurrentIdx") if isinstance(settings.get("CurrentIdx"), int) else 0
    if index < 0 or index >= len(nested):
        index = 0
    return nested[index]


def _convert(action, page_names, page_order):
    """Return (action fields, note). A note means the key kept its picture only."""
    if not isinstance(action, dict):
        return {}, None
    uuid = action.get("UUID") or (action.get("Plugin") or {}).get("UUID")
    kind = _kind(uuid)
    settings = action.get("Settings") if isinstance(action.get("Settings"), dict) else {}
    label = action.get("Name") or (action.get("Plugin") or {}).get("Name") or kind
    if kind == "hotkey":
        fields = _hotkey_fields(settings)
        if fields:
            return fields, None
        return {}, f"hotkey {label!r} has no key code"
    if kind in ("website", "open"):
        path = _setting(settings, "path", "url", "uri") or ""
        command = _command_for_path(path)
        if command:
            return {"exec": command}, None
        if path:
            return {}, f"open { _unquote(path)!r} needs a Linux command"
        return {}, None
    if kind in ("media", "volume"):
        fields = _media_fields(settings, kind)
        return (fields or {}), None if fields else f"media action {label!r}"
    if kind == "text":
        return {}, None
    if kind == "back":
        return {"page": "back"}, None
    if kind == "page-next":
        return {"page": "next"}, None
    if kind == "page-prev":
        return {"page": "previous"}, None
    if kind == "page-goto":
        index = _setting(settings, "PageIndex", "pageIndex")
        if isinstance(index, int) and 0 <= index < len(page_order):
            return {"page": page_order[index]}, None
        return {}, f"go to page {index}"
    if kind == "folder":
        target = _setting(settings, "ProfileUUID", "ProfileUuid")
        name = page_names.get(str(target).lower()) if target else None
        if name:
            return {"page": name}, None
        return {}, f"folder {label!r} has no matching page"
    if kind == "switch":
        target = _setting(settings, "ProfileName", "profileName")
        if isinstance(target, str) and target.strip():
            return {"profile": slug(target)}, None
        return {}, f"switch profile {label!r} names no profile"
    if kind == "multi":
        steps = [step for step in _multi_steps(action) if isinstance(step, dict)]
        combos = []
        for step in steps:
            step_uuid = step.get("UUID") or (step.get("Plugin") or {}).get("UUID")
            if _kind(step_uuid) != "hotkey":
                return {}, f"multi action {label!r} includes a plugin step"
            fields = _hotkey_fields(step.get("Settings") if isinstance(step.get("Settings"), dict) else {})
            combo = (fields or {}).get("keys") or (fields or {}).get("sequence")
            if not combo:
                return {}, f"multi action {label!r} includes a key with no code"
            combos.append(combo)
        if len(combos) == 1 and " " not in combos[0]:
            return {"keys": combos[0]}, None
        if combos:
            return {"sequence": " ".join(combos)}, None
        return {}, None
    return {}, f"plugin {label}"


def _unique(name, used):
    base = (name or "page").strip() or "page"
    if base.lower() == "settings":
        base = "settings page"
    candidate, n = base, 2
    while candidate in used or candidate.lower() == "settings":
        candidate = f"{base} {n}"
        n += 1
    used.add(candidate)
    return candidate


def _layout(cells, cols, rows):
    if cols == DECK_COLS:
        pages = []
        for top in range(0, max(rows, 1), DECK_ROWS):
            page = [{} for _ in range(DECK_KEYS)]
            used = False
            for r in range(DECK_ROWS):
                for c in range(DECK_COLS):
                    spec = cells.get((c, top + r))
                    if spec:
                        page[r * DECK_COLS + c] = spec
                        used = True
            if used:
                pages.append(page)
        return pages
    seq = [cells.get((c, r), {}) for r in range(rows) for c in range(cols)]
    for (c, r) in sorted(cells, key=lambda p: (p[1], p[0])):
        if c >= cols or r >= rows:
            seq.append(cells[(c, r)])
    return [seq[i:i + DECK_KEYS] for i in range(0, len(seq), DECK_KEYS) if any(seq[i:i + DECK_KEYS])]


def _find_image(members, opener, page_dir, image):
    if not image:
        return None, None
    rel = image.replace("\\", "/").lstrip("/")
    base = os.path.basename(rel)
    candidates = [rel, rel + ".png", rel + ".jpg", rel + ".jpeg", "Images/" + base, "Images/" + base + ".png"]
    for candidate in candidates:
        rel = f"{page_dir}/{candidate}" if page_dir else candidate
        real = members.get(rel.lower())
        if real:
            return real, opener(real)
    return None, None


def _safe_image_name(profile, page, index, source_name):
    ext = os.path.splitext(source_name)[1].lower()
    if ext not in (".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"):
        ext = ".png"
    return f"images/{profile}/{slug(page)}-{index}{ext}"


def _image_ext(name):
    ext = os.path.splitext(name)[1].lower()
    return ext if ext in (".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp") else ".png"


def _norm_hex(value):
    text = str(value).strip()
    if not text.startswith("#"):
        text = "#" + text
    if re.fullmatch(r"#[0-9A-Fa-f]{6}", text):
        return "#" + text[1:].lower()
    return None


def _rgb(value):
    return tuple(int(value[i:i + 2], 16) for i in (1, 3, 5))


def _luminance(value):
    def lin(channel):
        channel /= 255
        return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4
    r, g, b = _rgb(value)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def _contrast(a, b):
    light, dark = sorted((_luminance(a), _luminance(b)), reverse=True)
    return (light + 0.05) / (dark + 0.05)


def _hex_rgb(rgb):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(c))) for c in rgb)


def _mix(a, b, t):
    return _hex_rgb(x + (y - x) * t for x, y in zip(_rgb(a), _rgb(b)))


def _dark_sample(blob):
    """A dark colour from an image, for the profile background. None if it is unreadable."""
    if not blob:
        return None
    try:
        from PIL import Image
    except ImportError:
        return None
    try:
        im = Image.open(io.BytesIO(blob)).convert("RGB")
    except Exception:
        return None
    im.thumbnail((64, 64))
    pixels = list(im.getdata())
    if not pixels:
        return None
    pixels.sort(key=lambda p: 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2])
    dark = pixels[:max(1, len(pixels) // 5)]
    n = len(dark)
    return _hex_rgb(sum(p[i] for p in dark) / n for i in range(3))


def _look_theme(colours, screen_blob):
    """Colours for an imported profile: its title colours, and a dark tint of its screen."""
    norm = [c for c in (_norm_hex(c) for c in colours) if c]
    counts = {}
    for colour in norm:
        counts[colour] = counts.get(colour, 0) + 1
    accent = max(counts, key=lambda c: (counts[c], -norm.index(c))) if counts else "#d0d0d0"
    lights = [c for c in norm if _luminance(c) >= 0.7]
    fg = lights[0] if lights else "#f2f2f2"
    bg = _dark_sample(screen_blob) or "#07080c"
    r, g, b = _rgb(bg)
    for _ in range(10):
        bg = _hex_rgb((r, g, b))
        if _contrast(fg, bg) >= 4.5:
            break
        r, g, b = (channel * 0.7 for channel in (r, g, b))
    key_bg = "#000000"
    return {
        "bg": bg,
        "key_bg": key_bg,
        "fg": fg,
        "accent": accent,
        "dim": _mix(fg, bg, 0.62),
        "border": _mix(accent, key_bg, 0.82),
        "frame": accent,
    }


def _bundle_wallpaper(members, opener):
    """A wallpaper image shipped next to a profile in a marketplace zip, if there is one."""
    hits = []
    for lower, real in members.items():
        base = lower.rsplit("/", 1)[-1]
        if "wallpaper" not in lower and "/wallpapers/" not in f"/{lower}":
            continue
        if not base.endswith((".png", ".jpg", ".jpeg", ".webp")):
            continue
        hits.append((lower, real))
    if not hits:
        return None
    hits.sort(key=lambda item: ("galleon" not in item[0], len(item[0])))
    real = hits[0][1]
    return real, opener(real)


def convert(source, name=None):
    """Read a profile and return (profile dict, header lines, image blobs, result counts).

    image blobs are (config-relative path, bytes). Nothing is written.
    """
    members, opener = _read(source)
    wallpaper = _bundle_wallpaper(members, opener)
    members, opener = _unwrap_bundle(members, opener)
    manifest_name, manifest = _profile_manifest(members, opener)
    loaded = _load_pages(manifest_name, manifest, members, opener)
    actions_per_page = [_keypad_actions(data) for _uuid, _path, data in loaded]
    cols, rows = _geometry(manifest, actions_per_page)

    page_dirs = []
    for uuid, path, data in loaded:
        title = data.get("Name") if isinstance(data.get("Name"), str) else ""
        page_dirs.append((uuid, os.path.dirname(path), title.strip()))
    # A folder button's title names an otherwise untitled child page.
    folder_titles = {}
    for actions in actions_per_page:
        for action in actions.values():
            if not isinstance(action, dict):
                continue
            uuid = action.get("UUID") or (action.get("Plugin") or {}).get("UUID")
            if _kind(uuid) != "folder":
                continue
            target = _setting(action.get("Settings") or {}, "ProfileUUID", "ProfileUuid")
            look_title, _fg, _image = _look(action)
            if target and look_title:
                folder_titles.setdefault(str(target).lower(), look_title)

    used_names = set()
    named = []
    for uuid, _directory, title in page_dirs:
        chosen = title or folder_titles.get(str(uuid).lower() if uuid else "", "")
        named.append(_unique(chosen or "page", used_names))
    by_uuid = {}
    for (uuid, _directory, _title), page_name in zip(page_dirs, named):
        if uuid:
            by_uuid[str(uuid).lower()] = page_name

    profile_name = slug(name or manifest.get("Name") or os.path.splitext(os.path.basename(source))[0])
    notes = []
    images = []
    seen_blobs = {}
    colours = []
    pages_out = []
    action_count = 0
    kept_count = 0
    wheels = 0
    for (uuid, directory, _title), page_name, actions in zip(page_dirs, named, actions_per_page):
        cells = {}
        for key, action in actions.items():
            pos = _coord(key)
            if pos is None or not isinstance(action, dict):
                continue
            chosen = _selected_stack_item(action)
            if chosen is not None:
                action = chosen
                wheels += 1
            title, fg, image_ref = _look(action)
            fields, note = _convert(action, by_uuid, named)
            spec = {}
            if title:
                spec["label"] = title
            elif note and note.startswith("plugin"):
                plugin_name = action.get("Name") or (action.get("Plugin") or {}).get("Name")
                if plugin_name:
                    spec["label"] = str(plugin_name)
            real, blob = _find_image(members, opener, directory, image_ref)
            if blob is not None:
                rel = seen_blobs.get(real)
                if rel is None:
                    rel = _safe_image_name(profile_name, page_name, len(images) + 1, real)
                    seen_blobs[real] = rel
                    images.append((rel, blob))
                spec["image"] = rel
                spec["fill"] = True
            if fg and (title or spec.get("image")):
                spec["fg"] = fg
                colours.append(fg)
            if fields:
                spec.update(fields)
                action_count += 1
            elif note:
                if note.startswith("plugin") or note.startswith("multi"):
                    kept_count += 1
                where = f"{page_name} row {pos[1] + 1} column {pos[0] + 1}"
                notes.append(f"{where}: {note}")
            if spec:
                cells[pos] = spec
        layouts = _layout(cells, cols, rows) or [[]]
        for index, keys in enumerate(layouts):
            this_name = page_name if index == 0 else _unique(f"{page_name} {index + 1}", used_names)
            pages_out.append({"name": this_name, "keys": keys})
    if not pages_out:
        pages_out = [{"name": "page", "keys": []}]
    start = next((page["name"] for page in pages_out if any(page["keys"])), pages_out[0]["name"])
    manifest_dir = os.path.dirname(manifest_name.replace("\\", "/"))
    while manifest_dir.startswith("./"):
        manifest_dir = manifest_dir[2:]
    backgrounds = manifest.get("Backgrounds") if isinstance(manifest.get("Backgrounds"), dict) else {}
    screen_ref = backgrounds.get("Encoder") if isinstance(backgrounds.get("Encoder"), str) else None
    screen_rel, screen_blob = None, None
    if screen_ref:
        real, blob = _find_image(members, opener, manifest_dir, screen_ref)
        if blob:
            screen_rel = f"images/{profile_name}/screen{_image_ext(real)}"
            images.append((screen_rel, blob))
            screen_blob = blob
    if screen_rel is None and wallpaper:
        real, blob = wallpaper
        screen_rel = f"images/{profile_name}/screen{_image_ext(real)}"
        images.append((screen_rel, blob))
        screen_blob = blob
    profile = {
        "theme": "mono",
        "start_page": start,
        "page": pages_out,
        "theme_overrides": _look_theme(colours, screen_blob),
    }
    if screen_rel:
        profile["screen"] = screen_rel
    origin = os.path.basename(source)
    header = [
        f"# Imported from {origin}.",
        "# Titles, pictures, hotkeys, websites, open, media, and page changes were kept.",
        "# Key pictures fill the key. Colours come from the profile.",
        "# A plugin key keeps its title and picture. Assign what it does in the configurator.",
    ]
    if screen_rel:
        header.append("# The top screen shows the profile's screen image.")
    if wheels:
        header.append("# An action wheel keeps the action that was selected when the profile was exported.")
    if notes:
        header += ["#", "# Plugin keys and other actions left without a command:"]
        header += [f"#   {note}" for note in notes[:40]]
        if len(notes) > 40:
            header.append(f"#   … and {len(notes) - 40} more")
    result = ImportResult(profile_name, None, len(pages_out), action_count, kept_count, notes)
    return profile, header, images, result


def import_profile(source, config_dir=None, name=None, force=False):
    """Write profiles/<name>.toml and its images under config_dir. Returns ImportResult."""
    config_dir = config_dir or CONFIG_DIR
    profile, header, images, result = convert(source, name=name)
    path = os.path.join(config_dir, "profiles", result.name + ".toml")
    if os.path.exists(path) and not force:
        raise ProfileImportError(f"there is already a profile called {result.name!r}")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for rel, blob in images:
        dest = os.path.join(config_dir, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(blob)
    text = render_profile(profile, header)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    result.path = path
    return result


def peek_name(source):
    """Suggested profile slug, without writing."""
    _profile, _header, _images, result = convert(source)
    return result.name


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="kdeck-control import",
        description="Convert an official Stream Deck profile (.streamDeckProfile) into a KDEck Control profile.",
    )
    parser.add_argument("file", help="exported .streamDeckProfile (a zip) or an unpacked folder")
    parser.add_argument("--name", help="profile name (default: the name inside the file)")
    parser.add_argument("--config", default=CONFIG_DIR, help="config directory (default: ~/.config/kdeck-control)")
    parser.add_argument("--force", action="store_true", help="replace an existing profile of the same name")
    parser.add_argument("--dry-run", action="store_true", help="report what would be written")
    args = parser.parse_args(argv)
    try:
        if args.dry_run:
            profile, header, images, result = convert(args.file, name=args.name)
            text = render_profile(profile, header)
            print(f"{result.name}: {result.pages} pages, {result.actions} actions, {result.kept} plugin keys, {len(images)} images")
            sys.stdout.write(text)
            return 0
        result = import_profile(args.file, config_dir=args.config, name=args.name, force=args.force)
    except ProfileImportError as e:
        print(f"kdeck-control import: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"kdeck-control import: {e}", file=sys.stderr)
        return 1
    print(f"Imported {result.name} -> {result.path}")
    print(f"{result.pages} pages, {result.actions} actions, {result.kept} plugin keys kept their title and picture")
    for note in result.notes[:15]:
        print(f"  {note}")
    if len(result.notes) > 15:
        print(f"  … and {len(result.notes) - 15} more, listed in the profile file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
