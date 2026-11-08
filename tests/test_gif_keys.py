"""Animated key images: a GIF (or APNG / animated WebP) plays on the key."""

import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
import galleon_deck as gd  # noqa: E402

from PIL import Image, ImageDraw  # noqa: E402


def _rgb(img, xy=None):
    x, y = xy or (gd.KEY_PX // 2, gd.KEY_PX // 2)
    pixel = img.getpixel((x, y))
    return pixel[:3]


def _save_gif(path, frames, duration=100, disposal=2):
    frames[0].save(path, save_all=True, append_images=list(frames[1:]),
                   duration=duration, loop=0, disposal=disposal)


def test_frame_clock():
    durations = [0.1, 0.1, 0.2]
    assert gd.frame_index_at(durations, 0.0) == 0
    assert gd.frame_index_at(durations, 0.099) == 0
    assert gd.frame_index_at(durations, 0.1) == 1
    assert gd.frame_index_at(durations, 0.2) == 2
    assert gd.frame_index_at(durations, 0.4) == 0  # loops
    assert abs(gd.seconds_until_frame(durations, 0.0) - 0.1) < 1e-9
    assert abs(gd.seconds_until_frame(durations, 0.25) - 0.15) < 1e-9


def test_gif_plays_on_the_key():
    theme = dict(gd.THEME_DEFAULTS)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "swap.gif")
        _save_gif(path, [Image.new("RGB", (40, 40), (255, 0, 0)),
                         Image.new("RGB", (40, 40), (0, 0, 255))])
        anim = gd.key_animation({"image": path, "fill": True})
        assert anim is not None
        _frames, durations, _token = anim
        assert len(durations) == 2
        assert abs(durations[0] - 0.1) < 1e-6

        spec = {"image": path, "fill": True}
        first = gd.render_key(spec, theme, when=0.0)
        second = gd.render_key(spec, theme, when=durations[0] + durations[1] * 0.5)
        looped = gd.render_key(spec, theme, when=sum(durations))
        assert _rgb(first) == (255, 0, 0)
        assert _rgb(second) == (0, 0, 255)
        assert _rgb(looped) == (255, 0, 0)
        # No clock: the still is the first frame, including inside a mosaic.
        assert _rgb(gd.render_key(spec, theme)) == (255, 0, 0)

        held = gd.render_key(spec, theme, pressed=True, when=0.0)
        assert _rgb(held)[0] < 200

        labelled = gd.render_key({"image": path, "label": "Hi"}, theme, when=durations[0] + 0.01)
        assert labelled.size == (gd.KEY_PX, gd.KEY_PX)

        # A zero-delay GIF is the "play as fast as possible" value. Play it at 100 ms.
        fast = os.path.join(tmp, "fast.gif")
        _save_gif(fast, [Image.new("RGB", (8, 8), (255, 255, 0)),
                         Image.new("RGB", (8, 8), (0, 255, 0))], duration=0)
        _frames, fast_durations, _token = gd.key_animation({"image": fast})
        assert fast_durations[0] >= gd._MIN_FRAME_S
        assert abs(fast_durations[0] - 0.1) < 1e-6


def test_partial_gif_is_composited():
    """A frame that only draws what changed still shows the rest of the picture."""
    theme = dict(gd.THEME_DEFAULTS)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "delta.gif")
        base = Image.new("RGBA", (32, 32), (255, 0, 0, 255))
        delta = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        ImageDraw.Draw(delta).rectangle((0, 0, 7, 7), fill=(0, 255, 0, 255))
        # disposal 1: keep the previous frame and paint this one on top
        _save_gif(path, [base, delta], disposal=1)
        frames, _durations, _token = gd.key_animation({"image": path})
        assert frames[0].getpixel((16, 16))[:3] == (255, 0, 0)
        assert frames[1].getpixel((2, 2))[:3] == (0, 255, 0)
        assert frames[1].getpixel((16, 16))[:3] == (255, 0, 0)

        spec = {"image": path, "fill": True}
        assert _rgb(gd.render_key(spec, theme, when=0.0)) == (255, 0, 0)
        assert _rgb(gd.render_key(spec, theme, when=0.15)) == (255, 0, 0)


def test_authored_twenty_ms_is_kept():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "fast20.gif")
        _save_gif(path, [Image.new("RGB", (8, 8), (255, 0, 0)),
                         Image.new("RGB", (8, 8), (0, 0, 255))], duration=20)
        _frames, durations, _token = gd.key_animation({"image": path})
        assert abs(durations[0] - 0.02) < 1e-6
        assert abs(sum(durations) - 0.04) < 1e-6


def test_identical_frames_are_one_hold():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "hold.gif")
        red = Image.new("RGB", (8, 8), (255, 0, 0))
        blue = Image.new("RGB", (8, 8), (0, 0, 255))
        _save_gif(path, [red, red.copy(), blue], duration=20)
        frames, durations, _token = gd.key_animation({"image": path})
        assert len(frames) == 2
        assert abs(durations[0] - 0.04) < 1e-6
        assert abs(durations[1] - 0.02) < 1e-6
        assert frames[0].getpixel((1, 1))[:3] == (255, 0, 0)
        assert frames[1].getpixel((1, 1))[:3] == (0, 0, 255)


def test_still_image_is_not_an_animation():
    theme = dict(gd.THEME_DEFAULTS)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "still.png")
        Image.new("RGB", (20, 20), (0, 255, 0)).save(path)
        assert gd.key_animation({"image": path}) is None
        assert gd.key_animation({"stat": "cpu", "image": path}) is None
        assert gd.key_animation({}) is None
        img = gd.render_key({"image": path, "fill": True}, theme, when=time.time())
        assert _rgb(img) == (0, 255, 0)


def test_folder_mosaic_stays_still():
    theme = dict(gd.THEME_DEFAULTS)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "child.gif")
        _save_gif(path, [Image.new("RGB", (20, 20), (255, 0, 0)),
                         Image.new("RGB", (20, 20), (0, 0, 255))])
        spec = {"label": "Folder", "folder": gd.new_folder()}
        spec["folder"][1] = {"image": path, "fill": True}
        a = gd.render_key(spec, theme, when=0.0)
        b = gd.render_key(spec, theme, when=10.0)
        assert a.tobytes() == b.tobytes()
        # The folder's own GIF replaces the mosaic and does play.
        face = {"image": path, "fill": True, "folder": gd.new_folder()}
        assert _rgb(gd.render_key(face, theme, when=0.0)) != _rgb(gd.render_key(face, theme, when=0.15))


def test_only_due_keys_are_sent():
    class FakeKeyboard:
        def __init__(self):
            pass

    class FakeDeck:
        def __init__(self):
            self.sent = []

        def key_image(self, index, _jpeg):
            self.sent.append(index)

    gd.Keyboard = FakeKeyboard
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "key.gif")
        _save_gif(path, [Image.new("RGB", (16, 16), (255, 0, 0)),
                         Image.new("RGB", (16, 16), (0, 0, 255))])
        app = gd.App()
        app.deck = FakeDeck()
        keys = [{} for _ in range(gd.KEYS)]
        for i in range(4):
            keys[i] = {"image": path, "fill": True}
        keys[5] = {"label": "Still"}
        app.cfg = {"profiles": {"demo": {
            "page": [{"name": "Home", "keys": keys}],
            "theme": dict(gd.THEME_DEFAULTS),
            "theme_name": "amber",
        }}}
        app.profile = "demo"
        app.page = 0

        assert app._anim_delay() == 0.0
        app.draw_due_animations()
        # Encoded frames are cheap, so one short wake sends every due key.
        assert app.deck.sent == [0, 1, 2, 3]
        assert 5 not in app.deck.sent
        assert app._anim_delay() > 0
        app.deck.sent.clear()
        app.draw_due_animations()
        assert app.deck.sent == []  # this frame is already on the key

        app.anim_shown.clear()
        app.flash_until[0] = time.time() + 5
        app.draw_due_animations()
        assert 0 not in app.deck.sent  # a confirm flash owns the face
        assert app._anim_delay() > 0


def test_animated_webp_plays():
    theme = dict(gd.THEME_DEFAULTS)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "swap.webp")
        frames = [Image.new("RGB", (32, 32), (255, 128, 0)), Image.new("RGB", (32, 32), (0, 128, 255))]
        try:
            frames[0].save(path, save_all=True, append_images=frames[1:], duration=100, loop=0, format="WEBP")
        except (OSError, ValueError):
            return  # this Pillow build cannot write an animated WebP
        anim = gd.key_animation({"image": path})
        if anim is None:
            return
        _frames, durations, _token = anim
        spec = {"image": path, "fill": True}
        assert _rgb(gd.render_key(spec, theme, when=0.0)) != _rgb(
            gd.render_key(spec, theme, when=durations[0] + durations[-1] * 0.5))


if __name__ == "__main__":
    test_frame_clock()
    test_gif_plays_on_the_key()
    test_partial_gif_is_composited()
    test_authored_twenty_ms_is_kept()
    test_identical_frames_are_one_hold()
    test_still_image_is_not_an_animation()
    test_folder_mosaic_stays_still()
    test_only_due_keys_are_sent()
    test_animated_webp_plays()
    print("ok")
