"""Folders: a key opens its own 12 keys, and Back steps out."""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import kdeck_control as gd  # noqa: E402


def test_walk_and_label():
    inner = gd.new_folder()
    inner[1] = {"label": "Save", "key": "F5"}
    outer = gd.new_folder()
    outer[3] = {"label": "Games", "folder": inner}
    page = [{} for _ in range(gd.KEYS)]
    page[2] = {"label": "Play", "folder": outer}

    keys, path = gd.walk_folders(page, [2])
    assert path == [2]
    assert keys[3]["label"] == "Games"
    assert keys[0]["back"] is True

    keys, path = gd.walk_folders(page, [2, 3])
    assert path == [2, 3]
    assert keys[1]["key"] == "F5"
    assert gd.place_label("Home", page, [2, 3]) == "Home / Play / Games"

    keys, path = gd.walk_folders(page, [2, 1])
    assert path == [2]  # key 1 inside Play is not a folder
    assert gd.place_label("Home", page, []) == "Home"


def test_round_trip(tmp_path=None):
    folder = gd.new_folder()
    folder[1] = {"label": "Nested", "folder": [{"back": True, "label": "Up"}, {"exec": "true"}]}
    profile = {"theme": "amber", "page": [{"name": "Home", "keys": [
        {"label": "Folder", "folder": folder},
    ]}]}
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "demo.toml")
        gd.write_profile(path, profile)
        text = open(path).read()
        assert "back = true" in text
        assert "exec = \"true\"" in text
        import tomllib
        data = tomllib.load(open(path, "rb"))
        gd.validate_pages(data["page"], "demo")
        home = data["page"][0]["keys"]
        assert len(home) == gd.KEYS
        assert home[0]["folder"][0]["back"] is True
        assert home[0]["folder"][1]["folder"][1]["exec"] == "true"
        # Trailing empty slots inside the folder were not written, then padded on load.
        assert home[0]["folder"][11] == {}


def test_bad_folder_key():
    pages = [{"name": "Home", "keys": [{"folder": [{"key": "NOT_A_KEY"}]}]}]
    try:
        gd.validate_pages(pages, "demo")
    except ValueError as e:
        assert "NOT_A_KEY" in str(e) or "unknown key" in str(e)
    else:
        raise AssertionError("expected a bad key to fail")


def test_navigation_without_a_deck():
    class FakeKeyboard:
        def release(self, _codes):
            pass

    gd.Keyboard = FakeKeyboard
    app = gd.App()
    app.deck = None
    folder = gd.new_folder()
    folder[4] = {"label": "More", "folder": gd.new_folder()}
    app.cfg = {"profiles": {"demo": {
        "page": [{"name": "Home", "keys": [{} for _ in range(gd.KEYS)]},
                 {"name": "Other", "keys": [{} for _ in range(gd.KEYS)]}],
        "theme": dict(gd.THEME_DEFAULTS),
        "theme_name": "amber",
    }}}
    app.cfg["profiles"]["demo"]["page"][0]["keys"][2] = {"label": "Folder", "folder": folder}
    app.profile = "demo"
    app.page = 0
    app.folder_path = []

    app.enter_folder(2)
    assert app.folder_path == [2]
    assert app.keys()[0]["back"] is True
    app.enter_folder(4)
    assert app.place_name() == "Home / Folder / More"

    app.key_down(0)
    app.down_at[0] -= 0.1  # a tap
    app.key_up(0)
    assert app.folder_path == [2]

    app.key_down(0)
    app.down_at[0] -= 2  # held
    app.key_up(0)
    assert app.folder_path == []

    app.enter_folder(2)
    app.set_page(1, remember=False)
    assert app.page == 1 and app.folder_path == []

    app.page = 0
    app.enter_folder(2)
    app.history.clear()
    home = app.start_page_index()
    assert app.page == home
    app.folder_path.clear()
    app.release_held()
    assert app.folder_path == []


def test_folder_face_renders():
    theme = dict(gd.THEME_DEFAULTS)
    spec = {"label": "Folder", "folder": gd.new_folder()}
    spec["folder"][1] = {"label": "Go", "icon": gd.FOLDER_ICON}
    img = gd.render_key(spec, theme)
    assert img.size == (gd.KEY_PX, gd.KEY_PX)
    icon = gd.render_key({"label": "Mine", "icon": gd.FOLDER_ICON, "folder": gd.new_folder()}, theme)
    assert icon.size == (gd.KEY_PX, gd.KEY_PX)
    # A chosen icon replaces the mosaic, so the two faces are not the same picture.
    assert img.tobytes() != icon.tobytes()


if __name__ == "__main__":
    test_walk_and_label()
    test_round_trip()
    test_bad_folder_key()
    test_navigation_without_a_deck()
    test_folder_face_renders()
    print("ok")
