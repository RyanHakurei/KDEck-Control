"""Live CPU, memory, GPU, disk and network readings from KDE System Monitor.

KDE's ksystemstats daemon exposes those sensors on the session bus
(org.kde.ksystemstats1). One call feeds every stat key. Readings are cached
for just under a second so a page of keys shares a single query.

Keys name a stat ("cpu", "ram", "gpu", ...) and are painted by the deck with
whatever theme is already selected. Nothing here chooses colours.
"""

import re
import sys
import threading
import time

# (id stored on a key, name shown in the configurator, label written on the key)
STATS = (
    ("cpu", "CPU usage", "CPU"),
    ("cpu-temp", "CPU temperature", "CPU °C"),
    ("cpu-freq", "CPU frequency", "CPU GHz"),
    ("ram", "Memory", "RAM"),
    ("swap", "Swap", "SWAP"),
    ("disk", "Disk space", "DISK"),
    ("gpu", "GPU usage", "GPU"),
    ("gpu-temp", "GPU temperature", "GPU °C"),
    ("gpu-power", "GPU power", "POWER"),
    ("vram", "Video memory", "VRAM"),
    ("net", "Network", "NET"),
    ("io", "Disk throughput", "IO"),
    # From the ZFS ksystemstats plugin (zfs/pool, zfs/arc, zfs/dataset).
    ("zfs", "ZFS pool", "ZFS"),
    ("zfs-health", "ZFS health", "HEALTH"),
    ("zfs-frag", "ZFS fragmentation", "FRAG"),
    ("zfs-io", "ZFS throughput", "ZFS IO"),
    ("zfs-data", "ZFS dataset", "DATA"),
    ("arc", "ARC size", "ARC"),
    ("arc-hit", "ARC hit ratio", "HITS"),
)


def label_for(stat):
    """The short label a stat key starts with. Empty if `stat` is unknown."""
    for sid, _title, label in STATS:
        if sid == stat:
            return label
    return ""

BUS = "org.kde.ksystemstats1"
PATH = "/org/kde/ksystemstats1"
# Full-scale fallbacks when a sensor publishes no usable maximum.
_FREQ_MAX = 6000.0   # MHz
_TEMP_MAX = 100.0    # °C
_POWER_MAX = 400.0   # watts

_lock = threading.Lock()
_state = {
    "proxy": None,
    "meta": None,
    "meta_at": 0.0,
    "gpu": "gpu0",
    "pool": "",
    "data": None,
    "at": 0.0,
    "warned": False,
}


def reading(stat):
    """{"value", "bar", "sub"} for one stat id. Missing sensors read as a dash."""
    return readings().get(stat) or {"value": "—", "bar": None, "sub": ""}


def readings():
    """Every stat, refreshed together about once a second."""
    now = time.monotonic()
    with _lock:
        if _state["data"] is not None and now - _state["at"] < 0.9:
            return _state["data"]
        try:
            data = _query()
        except Exception as exc:  # bus down, daemon gone, or a shape we don't know
            _note(exc)
            _state["proxy"] = None
            if _state["data"] is not None and now - _state["at"] < 15:
                return _state["data"]
            return {}
        _state["data"] = data
        _state["at"] = now
        _state["warned"] = False
        return data


def _note(exc):
    if _state["warned"]:
        return
    _state["warned"] = True
    print(f"system stats: {exc}", file=sys.stderr, flush=True)


def _proxy():
    if _state["proxy"] is not None:
        return _state["proxy"]
    import gi
    gi.require_version("Gio", "2.0")
    from gi.repository import Gio
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    _state["proxy"] = Gio.DBusProxy.new_sync(
        bus, Gio.DBusProxyFlags.NONE, None, BUS, PATH, BUS, None)
    return _state["proxy"]


def _call(method, params, timeout_ms=800):
    import gi
    gi.require_version("Gio", "2.0")
    from gi.repository import Gio
    result = _proxy().call_sync(method, params, Gio.DBusCallFlags.NONE, timeout_ms, None)
    return result.unpack()[0]


def _meta():
    now = time.monotonic()
    if _state["meta"] is not None and now - _state["meta_at"] < 30:
        return _state["meta"]
    meta = _call("allSensors", None, timeout_ms=1500)
    _state["meta"] = meta
    _state["meta_at"] = now
    _state["gpu"] = _pick_gpu(meta)
    _state["pool"] = _pick_pool(meta)
    return meta


def _meta_max(meta, sensor):
    row = (meta or {}).get(sensor)
    if not row or len(row) < 7:
        return 0.0
    try:
        return float(row[6] or 0)
    except (TypeError, ValueError):
        return 0.0


def _pick_gpu(meta):
    """The discrete GPU: the one with the most video memory. Integrated graphics
    report little or none, so this prefers a card like the RTX over the iGPU."""
    best, score = "gpu0", -1.0
    names = set()
    for sensor in meta:
        parts = sensor.split("/")
        if len(parts) == 3 and parts[0] == "gpu" and parts[1].startswith("gpu") and parts[1][3:].isdigit():
            names.add(parts[1])
    for name in sorted(names):
        score_n = max(_meta_max(meta, f"gpu/{name}/usedVram"), _meta_max(meta, f"gpu/{name}/totalVram"))
        if score_n > score:
            best, score = name, score_n
    return best


def _pick_pool(meta):
    """The largest ZFS pool. Pool names do not contain slashes, so the sensor
    id is zfs/pool/<name>/<metric>."""
    best, score = "", -1.0
    names = set()
    for sensor in meta or {}:
        parts = sensor.split("/")
        if len(parts) == 4 and parts[0] == "zfs" and parts[1] == "pool":
            names.add(parts[2])
    for name in sorted(names):
        score_n = max(_meta_max(meta, f"zfs/pool/{name}/size"), _meta_max(meta, f"zfs/pool/{name}/allocated"))
        if score_n > score:
            best, score = name, score_n
    return best


def _query():
    gpu = _state.get("gpu") or "gpu0"
    meta = None
    try:
        meta = _meta()
        gpu = _state["gpu"]
    except Exception:
        _state["proxy"] = None  # the next call opens a fresh proxy
    ids = [
        "cpu/all/usage", "cpu/all/averageTemperature", "cpu/all/averageFrequency",
        "memory/physical/used", "memory/physical/total", "memory/physical/usedPercent",
        "memory/swap/used", "memory/swap/total", "memory/swap/usedPercent",
        "disk/all/used", "disk/all/total", "disk/all/usedPercent",
        "disk/all/read", "disk/all/write",
        "network/all/download", "network/all/upload",
        f"gpu/{gpu}/usage", f"gpu/{gpu}/temperature", f"gpu/{gpu}/power", f"gpu/{gpu}/name",
        f"gpu/{gpu}/usedVram", f"gpu/{gpu}/totalVram", f"gpu/{gpu}/usedPercent",
    ]
    pool = _state.get("pool") or ""
    if pool:
        base = f"zfs/pool/{pool}"
        ids += [f"{base}/{name}" for name in
                ("capacity", "allocated", "size", "fragmentation", "health", "read", "write")]
        root = f"zfs/dataset/{pool}"
        ids += [f"{root}/used", f"{root}/available", f"{root}/usedPercent"]
    ids += ["zfs/arc/size", "zfs/arc/target", "zfs/arc/hitRatio", "zfs/arc/recentHitRatio"]
    if meta:
        ids = [sensor for sensor in ids if not sensor.startswith("zfs/") or sensor in meta]
    import gi
    gi.require_version("GLib", "2.0")
    from gi.repository import GLib
    values = {}
    for sensor, value in _call("sensorData", GLib.Variant("(as)", (ids,))):
        values[sensor] = value
    return _format(values, gpu, _state.get("pool") or "", meta)


def _format(values, gpu, pool, meta):
    def get(sensor):
        return values.get(sensor)

    cpu = _pct(get("cpu/all/usage"))
    temp = _num(get("cpu/all/averageTemperature"))
    freq = _num(get("cpu/all/averageFrequency"))
    ram = _pct(get("memory/physical/usedPercent"))
    swap = _pct(get("memory/swap/usedPercent"))
    disk = _pct(get("disk/all/usedPercent"))
    usage = _pct(get(f"gpu/{gpu}/usage"))
    gpu_temp = _num(get(f"gpu/{gpu}/temperature"))
    power = _num(get(f"gpu/{gpu}/power"))
    vram = _pct(get(f"gpu/{gpu}/usedPercent"))
    freq_max = _limit(meta, "cpu/all/averageFrequency", _FREQ_MAX, 1000)
    out = {
        "cpu": _gauge(_fmt_pct(cpu), cpu, _cpu_model(get("cpu/all/name"))),
        "cpu-temp": _gauge(_fmt_temp(temp), None if temp is None else temp / _TEMP_MAX * 100),
        "cpu-freq": _gauge(_fmt_freq(freq), None if freq is None else freq / freq_max * 100),
        "ram": _gauge(_fmt_pct(ram), ram, _ratio(get("memory/physical/used"), get("memory/physical/total"))),
        "swap": _gauge(_fmt_pct(swap), swap, _ratio(get("memory/swap/used"), get("memory/swap/total"))),
        "disk": _gauge(_fmt_pct(disk), disk, _ratio(get("disk/all/used"), get("disk/all/total"))),
        "gpu": _gauge(_fmt_pct(usage), usage, _short_gpu(get(f"gpu/{gpu}/name"))),
        "gpu-temp": _gauge(_fmt_temp(gpu_temp), None if gpu_temp is None else gpu_temp / _TEMP_MAX * 100),
        "gpu-power": _gauge(_fmt_power(power), None if power is None else power / _limit(meta, f"gpu/{gpu}/power", _POWER_MAX, 50) * 100),
        "vram": _gauge(_fmt_pct(vram), vram, _ratio(get(f"gpu/{gpu}/usedVram"), get(f"gpu/{gpu}/totalVram"))),
        "net": {
            "value": _rate(get("network/all/download")),
            "bar": None,
            "sub": "up " + _rate(get("network/all/upload")),
        },
        "io": {
            "value": _rate(get("disk/all/read")),
            "bar": None,
            "sub": "write " + _rate(get("disk/all/write")),
        },
    }
    out.update(_zfs(get, pool))
    return out


def _zfs(get, pool):
    """Readings from the ZFS ksystemstats plugin. Absent sensors stay blank."""
    base = f"zfs/pool/{pool}" if pool else ""
    capacity = _pct(get(f"{base}/capacity")) if base else None
    frag = _pct(get(f"{base}/fragmentation")) if base else None
    health = get(f"{base}/health") if base else None
    health = health.strip() if isinstance(health, str) else ""
    used = _num(get(f"zfs/dataset/{pool}/used")) if pool else None
    avail = _num(get(f"zfs/dataset/{pool}/available")) if pool else None
    data = _pct(get(f"zfs/dataset/{pool}/usedPercent")) if pool else None
    arc = _num(get("zfs/arc/size"))
    arc_target = _num(get("zfs/arc/target"))
    arc_pct = None if arc is None or not arc_target else max(0.0, min(100.0, arc / arc_target * 100))
    hit = _pct(get("zfs/arc/hitRatio"))
    recent = _pct(get("zfs/arc/recentHitRatio"))
    return {
        "zfs": _gauge(_fmt_pct(capacity), capacity, _ratio(get(f"{base}/allocated"), get(f"{base}/size")) if base else ""),
        "zfs-health": _gauge(health or "—", None, pool),
        "zfs-frag": _gauge(_fmt_pct(frag), frag, pool),
        "zfs-io": {
            "value": _rate(get(f"{base}/read")) if base else "—",
            "bar": None,
            "sub": "write " + (_rate(get(f"{base}/write")) if base else "—"),
        },
        "zfs-data": _gauge(_fmt_pct(data), data, _ratio(used, None if used is None or avail is None else used + avail)),
        "arc": _gauge(_fmt_pct(arc_pct), arc_pct, _ratio(arc, arc_target)),
        "arc-hit": _gauge(_fmt_pct(hit), hit, "" if recent is None else f"now {recent:.0f}%"),
    }


def _gauge(value, percent, sub=""):
    bar = None if percent is None else max(0.0, min(1.0, percent / 100.0))
    return {"value": value, "bar": bar, "sub": sub}


def _limit(meta, sensor, fallback, floor):
    mx = _meta_max(meta, sensor)
    return mx if mx >= floor else fallback


def _num(value):
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    return None


def _pct(value):
    number = _num(value)
    if number is None:
        return None
    return max(0.0, min(100.0, number))


def _fmt_pct(percent):
    if percent is None:
        return "—"
    return f"{percent:.0f}%"


def _fmt_temp(celsius):
    if celsius is None:
        return "—"
    return f"{celsius:.0f}°"


def _fmt_freq(mhz):
    if mhz is None:
        return "—"
    if mhz >= 1000:
        return f"{mhz / 1000:.1f} GHz"
    return f"{mhz:.0f} MHz"


def _fmt_power(watts):
    if watts is None:
        return "—"
    return f"{watts:.0f} W"


def _fmt_bytes(n):
    n = max(0.0, float(n))
    for unit in ("B", "KB", "MB", "GB", "TB", "PB"):
        if n < 1024 or unit == "PB":
            if unit == "B":
                return f"{n:.0f} B"
            text = f"{n:.0f}" if n >= 100 else f"{n:.1f}"
            return f"{text} {unit}"
        n /= 1024
    return f"{n:.0f} PB"


def _rate(value):
    number = _num(value)
    if number is None:
        return "—"
    return _fmt_bytes(number) + "/s"


def _ratio(used, total):
    used_n, total_n = _num(used), _num(total)
    if used_n is None or total_n is None or total_n <= 0:
        return ""
    return f"{_fmt_bytes(used_n)} / {_fmt_bytes(total_n)}"


_cpu_name = None


def _cpu_model(sensor_name):
    """A short processor name, like the GPU key's card name. KDE's sensor is
    often just "All", so the model line in /proc/cpuinfo fills in."""
    if isinstance(sensor_name, str):
        text = sensor_name.strip()
        if text and text.lower() not in ("all", "cpu") and not text.lower().startswith("core "):
            return _short_cpu(text)
    global _cpu_name
    if _cpu_name is None:
        _cpu_name = ""
        try:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.lower().startswith("model name"):
                        _cpu_name = _short_cpu(line.split(":", 1)[1])
                        break
        except OSError:
            _cpu_name = ""
    return _cpu_name


def _short_cpu(name):
    text = name.replace("(R)", " ").replace("(TM)", " ").replace("®", " ").replace("™", " ")
    text = " ".join(text.split())
    match = re.search(r"((?:Core )?i[3-9]-\S+|Ryzen \d+ \S+|Core Ultra \S+)", text)
    if match:
        return match.group(1)[:22]
    text = re.sub(r"\b\d+(?:st|nd|rd|th) Gen\b", "", text)
    for prefix in ("Intel ", "AMD "):
        if text.startswith(prefix):
            text = text[len(prefix):]
    return text.strip()[:22]


def _short_gpu(name):
    if not isinstance(name, str) or not name.strip():
        return ""
    text = name.strip()
    for prefix in ("NVIDIA GeForce ", "AMD Radeon ", "Intel "):
        if text.startswith(prefix):
            text = text[len(prefix):]
            break
    if "[" in text and text.endswith("]"):
        text = text[text.rfind("[") + 1:-1]
    return text.strip()[:22]
