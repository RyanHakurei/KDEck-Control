"""Converter tests for official Stream Deck profiles. No hardware and no GTK."""

import io
import json
import os
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import kdeck_import as gi  # noqa: E402

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib


HASHES = {
    "33860c96-e0d7-48dd-a8cc-471a027a4822": "6E30P5N0QT4DRA6C8SD04VI848Z",
    "e4f228ae-b631-4b80-a552-76f65595373e": "SJP2HBLM655O19AIERR5B59N7OZ",
    "0767bac7-4a41-40c3-bdf9-921c2f20ff97": "0TJRLHQA850C7FFPI8E2V87WISZ",
    "ff328353-96b5-42ac-b90c-8c63c2a397cb": "WSP86KSMML1APE8CHHHS58SNPCZ",
    "32270b97-45f4-4ba4-9e91-b67277d7ef22": "68JGN5Q5VH5Q97KHMPP7FLWF48Z",
    "d8efb2ec-7317-4fc0-9153-52ca5e22eede": "R3NR5R3J2T7S14AJAB55S8NEROZ",
    "8bd54075-e1e1-4cd4-98c0-75509b029b43": "HFAK0TF1S56D9660EL89M0KR8CZ",
    "5abecb50-4b82-4e9e-a087-1fc3bc990deb": "BAWCMK2BG979T8473W1RP68DTCZ",
    "74e4444c-9eae-4d7e-95df-f21b1e3ac26a": "EJI48J4VLP6NT5EWV8DHSEM2D8Z",
}

PAGE = "66dc13d2-233b-4133-a92b-d351672ee709"
CHILD = "33860c96-e0d7-48dd-a8cc-471a027a4822"


def hotkey(code, **mods):
    slot = {
        "KeyCmd": False, "KeyCtrl": False, "KeyOption": False, "KeyShift": False,
        "KeyModifiers": 0, "NativeCode": code, "QTKeyCode": code, "VKeyCode": code,
    }
    slot.update(mods)
    empty = {
        "KeyCmd": False, "KeyCtrl": False, "KeyOption": False, "KeyShift": False,
        "KeyModifiers": 0, "NativeCode": 146, "QTKeyCode": gi.QT_EMPTY, "VKeyCode": -1,
    }
    return {"Hotkeys": [slot, empty]}


def action(uuid, name, settings=None, title="", image=None, colour=None):
    state = {}
    if title:
        state["Title"] = title
    if image:
        state["Image"] = image
    if colour:
        state["TitleColor"] = colour
    return {
        "Name": name, "UUID": uuid, "Settings": settings or {}, "State": 0,
        "States": [state], "Plugin": {"Name": name, "UUID": uuid},
    }


def page_manifest(name, actions):
    return {"Name": name, "Controllers": [{"Type": "Keypad", "Actions": actions}]}


def build_zip(path, root, pages):
    """pages: {folder name: manifest dict} plus optional files {archive path: bytes}."""
    manifests, files = pages
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("manifest.json", json.dumps(root))
        for folder, manifest in manifests.items():
            zf.writestr(f"Profiles/{folder}/manifest.json", json.dumps(manifest))
        for name, blob in files.items():
            zf.writestr(name, blob)


def test_folder_ids():
    for uuid, folder in HASHES.items():
        assert gi.profile_folder_id(uuid) == folder, uuid


def test_galleon_page_keeps_positions_and_actions():
    child_folder = gi.profile_folder_id(CHILD)
    shot = b"\x89PNG-fake"
    actions = {
        "0,0": action("com.elgato.streamdeck.system.hotkey", "Hotkey",
                      hotkey(83, KeyCtrl=True, KeyShift=True), title="Save"),
        "1,0": action("com.elgato.streamdeck.system.website", "Website",
                      {"path": "https://example.com/a b"}, title="Site"),
        "2,0": action("com.elgato.streamdeck.system.open", "Open", {"path": "firefox"}, title="Web"),
        "0,1": action("com.elgato.streamdeck.system.open", "Open",
                      {"path": "C:\\\\Windows\\\\notepad.exe"}, title="Notepad"),
        "1,1": action("com.elgato.streamdeck.system.multimedia", "Multimedia",
                      {"action": "playpause"}, title="Play"),
        "2,1": action("com.elgato.streamdeck.system.multimedia", "Multimedia",
                      {"action": "volumeup"}, title="Louder", image="Images/shot.png", colour="#FF8800"),
        "0,2": action("com.elgato.streamdeck.system.hotkey", "Hotkey",
                      {"Hotkeys": [{"KeyCmd": False, "KeyCtrl": False, "KeyOption": False, "KeyShift": False,
                                    "KeyModifiers": 0, "NativeCode": 36, "QTKeyCode": 16777220, "VKeyCode": 36}]},
                      title="Enter"),
        "1,2": action("com.elgato.streamdeck.profile.openchild", "Apps",
                      {"ProfileUUID": CHILD}, title="Apps"),
        "2,2": action("com.elgato.streamdeck.profile.backtoparent", "Back", title="Back"),
        "0,3": action("com.elgato.streamdeck.multiactions.routine", "Combo",
                      {"Routine": [
                          action("com.elgato.streamdeck.system.hotkey", "Hotkey", hotkey(ord("W"))),
                          action("com.elgato.streamdeck.system.hotkey", "Hotkey", hotkey(ord("S"))),
                      ]}, title="Combo"),
        "1,3": action("com.elgato.obsstudio.scene", "Scene", {"scene": "Live"}, title="Live", image="Images/shot.png"),
        "2,3": action("com.elgato.streamdeck.system.text", "Text", title="Hello"),
    }
    root = {
        "Name": "Flight Deck",
        "Version": "2.0",
        "Device": {"Model": "", "UUID": "", "DeviceType": 12},
        "Pages": {"Pages": [PAGE, CHILD]},
    }
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "Flight Deck.streamDeckProfile")
        build_zip(src, root, ({
            PAGE: page_manifest("Main", actions),
            child_folder: page_manifest("", {"0,0": action(
                "com.elgato.streamdeck.profile.backtoparent", "Back", title="Up")}),
        }, {f"Profiles/{PAGE}/Images/shot.png": shot}))
        config = os.path.join(tmp, "config")
        result = gi.import_profile(src, config)
        assert result.name == "flight-deck"
        assert result.actions >= 8
        assert result.kept == 1
        text = open(result.path).read()
        assert "plugin Scene" in text
        data = tomllib.loads(text)
        assert data["theme"] == "mono"
        assert data["theme_overrides"]["accent"] == "#ff8800"
        assert data["theme_overrides"]["key_bg"] == "#000000"
        assert "screen" not in data
        assert data["start_page"] == "Main"
        main = data["page"][0]
        keys = main["keys"]
        assert keys[0]["keys"] == "CTRL+SHIFT+S"
        assert keys[0]["label"] == "Save"
        assert keys[1]["exec"] == "xdg-open 'https://example.com/a b'"
        assert keys[2]["exec"] == "firefox"
        assert "exec" not in keys[3]
        assert keys[3]["label"] == "Notepad"
        assert keys[4]["media"] == "play-pause"
        assert keys[5]["key"] == "VOLUMEUP"
        assert keys[5]["fg"] == "#FF8800"
        assert keys[5]["fill"] is True
        assert keys[5]["image"].startswith("images/flight-deck/")
        assert keys[6]["keys"] == "ENTER"  # Qt return, not Windows VK 36 (Home)
        assert keys[7]["page"] == "Apps"
        assert keys[8]["page"] == "back"
        assert keys[9]["sequence"] == "W S"
        assert "exec" not in keys[10] and "keys" not in keys[10]
        assert keys[10]["label"] == "Live"
        assert keys[11]["label"] == "Hello"
        image = os.path.join(config, keys[5]["image"])
        assert open(image, "rb").read() == shot
        # The same picture is stored once.
        assert keys[10]["image"] == keys[5]["image"]
        again = os.path.join(tmp, "again")
        try:
            gi.import_profile(src, config)
            raise AssertionError("expected a name collision")
        except gi.ProfileImportError:
            pass
        replaced = gi.import_profile(src, config, force=True)
        assert replaced.path == result.path
        assert not os.path.exists(again)


def test_folder_title_names_untitled_child():
    """Re-check the child page name: an empty Name takes the folder button's title."""
    child_folder = gi.profile_folder_id(CHILD)
    actions = {"0,0": action("com.elgato.streamdeck.profile.openchild", "Folder",
                             {"ProfileUUID": CHILD}, title="Apps")}
    root = {"Name": "Deck", "Device": {"DeviceType": 12}, "Pages": {"Pages": [PAGE, CHILD]}}
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("manifest.json", json.dumps(root))
        zf.writestr(f"Profiles/{PAGE}/manifest.json", json.dumps(page_manifest("Main", actions)))
        zf.writestr(f"Profiles/{child_folder}/manifest.json", json.dumps(page_manifest("", {})))
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "deck.streamDeckProfile")
        open(path, "wb").write(buf.getvalue())
        profile, _header, _images, _result = gi.convert(path)
        assert profile["page"][0]["keys"][0]["page"] == "Apps"
        assert profile["page"][1]["name"] == "Apps"


def test_wide_deck_is_poured_in_reading_order():
    actions = {f"{c},0": action("com.elgato.streamdeck.system.text", "Text", title=str(c)) for c in range(5)}
    actions["4,2"] = action("com.elgato.streamdeck.system.text", "Text", title="end")
    root = {"Name": "Wide", "Device": {"Model": "20GBA9901"}, "Pages": {"Pages": [PAGE]}}
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "wide.streamDeckProfile")
        build_zip(src, root, ({PAGE: page_manifest("Row", actions)}, {}))
        profile, _h, _i, result = gi.convert(src)
        assert result.pages == 2  # 5x3 = 15 cells, two deck pages
        first = profile["page"][0]["keys"]
        # Row of five becomes 3 + the first two of the next deck row.
        assert [k.get("label") for k in first[:5]] == ["0", "1", "2", "3", "4"]
        second = profile["page"][1]["keys"]
        assert second[-1]["label"] == "end"
        assert profile["page"][1]["name"] == "Row 2"


def _solid_png(color):
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (8, 4), color).save(buf, "PNG")
    return buf.getvalue()


def test_screen_image_and_key_fill():
    """The encoder background is the top screen, and a key picture covers the key."""
    root = {
        "Name": "Neon",
        "Device": {"Model": "GRETSCH"},
        "Pages": {"Pages": [PAGE]},
        "Backgrounds": {"Encoder": "Images/panel.png"},
    }
    page = page_manifest("Main", {
        "0,0": action("com.elgato.streamdeck.system.hotkey", "Hotkey", hotkey(ord("J")),
                      title="Jump", image="Images/jump.png", colour="#ff151b"),
    })
    page["Controllers"][0]["Actions"]["0,0"]["States"][0]["ShowTitle"] = False
    panel = _solid_png((8, 10, 16))
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "Neon.streamDeckProfile")
        build_zip(src, root, ({
            PAGE: page,
        }, {
            "Images/panel.png": panel,
            f"Profiles/{PAGE}/Images/jump.png": b"jump-png",
        }))
        profile, header, images, _result = gi.convert(src)
        assert profile["screen"] == "images/neon/screen.png"
        assert profile["theme"] == "mono"
        assert profile["theme_overrides"]["accent"] == "#ff151b"
        assert profile["theme_overrides"]["fg"] == "#f2f2f2"
        assert profile["theme_overrides"]["key_bg"] == "#000000"
        key = profile["page"][0]["keys"][0]
        assert key["fill"] is True
        assert key["keys"] == "J"
        assert "label" not in key
        blobs = dict(images)
        assert blobs[profile["screen"]] == panel
        assert blobs[key["image"]] == b"jump-png"
        text = gi.render_profile(profile, header)
        assert tomllib.loads(text)["screen"] == profile["screen"]


def test_wallpaper_beside_the_profile_is_the_screen():
    """A marketplace zip's wallpaper is the screen when the profile has no encoder image."""
    root = {"Name": "Pack", "Device": {"Model": "GRETSCH"}, "Pages": {"Pages": [PAGE]}}
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w") as zf:
        zf.writestr("manifest.json", json.dumps(root))
        zf.writestr(f"Profiles/{PAGE}/manifest.json", json.dumps(page_manifest("Main", {})))
    wall = _solid_png((12, 16, 24))
    outer = io.BytesIO()
    with zipfile.ZipFile(outer, "w") as zf:
        zf.writestr("Pack/Profile/Pack.streamDeckProfile", inner.getvalue())
        zf.writestr("Pack/Wallpapers/Pack-Galleon-Wallpaper.png", wall)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "Pack.zip")
        open(path, "wb").write(outer.getvalue())
        profile, _header, images, result = gi.convert(path)
        assert result.name == "pack"
        assert profile["screen"] == "images/pack/screen.png"
        assert dict(images)[profile["screen"]] == wall


def test_modifier_only_hotkey():
    slot = {
        "KeyCmd": False, "KeyCtrl": False, "KeyOption": True, "KeyShift": False,
        "KeyModifiers": 4, "NativeCode": -1, "QTKeyCode": gi.QT_EMPTY, "VKeyCode": -1,
    }
    assert gi._hotkey_fields({"Hotkeys": [slot]}) == {"keys": "ALT"}


def test_marketplace_zip_opens_the_profile_inside():
    """Icon-pack downloads are a zip around the .streamDeckProfile, with no manifest of their own."""
    page_id = "0D0FEED0-64BA-4195-B9A5-6AD4ED6CFBF1"
    home_id = "A40F1154-EADC-4E61-8AEE-18D428A333C6"
    profile_id = "C85FD6A2-C845-4326-8CE5-9586EAFF68A1"
    root = {
        "Name": "Cyberpunk",
        "Device": {"Model": "GRETSCH", "UUID": ""},
        "Pages": {"Default": home_id, "Pages": [page_id]},
        "Version": "3.0",
    }
    fight = page_manifest("Fight", {
        "0,0": {
            "Name": "Action Wheel", "UUID": "com.elgato.streamdeck.keys.stack",
            "Settings": {"CurrentIdx": 0},
            "States": [{}],
            "Actions": [action("com.elgato.streamdeck.system.hotkey", "Hotkey", hotkey(ord("J")),
                               title="Journal", image="Images/j.png")],
        },
        "2,3": action("com.elgato.streamdeck.page.next", "Next Page", title="Next"),
        "0,3": action("com.elgato.streamdeck.page.goto", "Go to Page", {"PageIndex": 0}, title="Home"),
    })
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w") as zf:
        zf.writestr("package.json", json.dumps({"DeviceModel": "GRETSCH", "Name": "Cyberpunk"}))
        base = f"Profiles/{profile_id}.sdProfile"
        zf.writestr(f"{base}/manifest.json", json.dumps(root))
        zf.writestr(f"{base}/Profiles/{home_id}/manifest.json", json.dumps(page_manifest("", {})))
        zf.writestr(f"{base}/Profiles/{page_id}/manifest.json", json.dumps(fight))
        zf.writestr(f"{base}/Profiles/{page_id}/Images/j.png", b"journal-png")
    outer = io.BytesIO()
    with zipfile.ZipFile(outer, "w") as zf:
        zf.writestr("Pack/README.txt", "icons and a profile")
        zf.writestr("Pack/Profile/Cyberpunk.streamDeckProfile", inner.getvalue())
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "Cyberpunk.zip")
        open(path, "wb").write(outer.getvalue())
        profile, _header, images, result = gi.convert(path)
        assert result.name == "cyberpunk"
        assert profile["start_page"] == "Fight"
        keys = next(page["keys"] for page in profile["page"] if page["name"] == "Fight")
        assert keys[0]["keys"] == "J"
        assert keys[0]["image"].endswith(".png")
        assert images and images[0][1] == b"journal-png"
        assert keys[11]["page"] == "next"  # row 4, column 3 -> index 11
        assert keys[9]["page"] == "page"  # PageIndex 0 is the empty default page


def test_dry_run_and_unreadable_file():
    with tempfile.TemporaryDirectory() as tmp:
        bad = os.path.join(tmp, "nope.streamDeckProfile")
        open(bad, "w").write("not a zip")
        code = gi.main([bad, "--dry-run"])
        assert code == 1


def test_directory_source():
    root = {"Name": "Loose", "Device": {"DeviceType": 1}, "Pages": {"Pages": [PAGE]}}
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "Profiles", PAGE))
        with open(os.path.join(tmp, "manifest.json"), "w") as f:
            json.dump(root, f)
        with open(os.path.join(tmp, "Profiles", PAGE, "manifest.json"), "w") as f:
            json.dump(page_manifest("Mini", {"2,1": action(
                "com.elgato.streamdeck.system.hotkey", "Hotkey", hotkey(ord("A")), title="A")}), f)
        profile, _h, _i, _r = gi.convert(tmp)
        # Mini is 3 columns, so column 2 of row 1 stays at deck index 5.
        keys = profile["page"][0]["keys"]
        assert keys[5]["label"] == "A"
        assert keys[5]["keys"] == "A"


if __name__ == "__main__":
    test_folder_ids()
    test_galleon_page_keeps_positions_and_actions()
    test_folder_title_names_untitled_child()
    test_wide_deck_is_poured_in_reading_order()
    test_screen_image_and_key_fill()
    test_wallpaper_beside_the_profile_is_the_screen()
    test_modifier_only_hotkey()
    test_marketplace_zip_opens_the_profile_inside()
    test_dry_run_and_unreadable_file()
    test_directory_source()
    print("ok")
