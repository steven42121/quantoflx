# -*- coding: utf-8 -*-
"""ScreenBrightness external helper.

Backlight.dll reads brightness only from the screen the skin sits on,
which breaks when the skin is on a DDC-incapable monitor. This helper
reads/writes any monitor via dxva2 and feeds Rainmeter variables.

Usage:
  brightness_helper.py get              print brightness of all monitors
  brightness_helper.py set <mon> <val>  set monitor brightness (0-100)
  brightness_helper.py bump <mon> <d>   adjust monitor brightness by d
  brightness_helper.py daemon           push Bri0/Bri1/Bri2 vars forever
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PIDFILE = os.path.join(HERE, ".brightness_daemon.pid")

user32 = ctypes.windll.user32
dxva2 = ctypes.windll.dxva2

MONITORENUMPROC = ctypes.WINFUNCTYPE(
    wt.BOOL, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM
)


class PHYSICAL_MONITOR(ctypes.Structure):
    _fields_ = [
        ("hPhysicalMonitor", wt.HANDLE),
        ("szPhysicalMonitorDescription", wt.WCHAR * 128),
    ]


def _hmonitors():
    mons = []

    def cb(hmon, hdc, lprect, lparam):
        mons.append(hmon)
        return True

    user32.EnumDisplayMonitors(None, None, MONITORENUMPROC(cb), 0)
    return mons


def _with_physical(idx, fn):
    mons = _hmonitors()
    if idx < 0 or idx >= len(mons):
        return None
    cnt = wt.DWORD()
    if not dxva2.GetNumberOfPhysicalMonitorsFromHMONITOR(
        mons[idx], ctypes.byref(cnt)
    ) or cnt.value == 0:
        return None
    arr = (PHYSICAL_MONITOR * cnt.value)()
    dxva2.GetPhysicalMonitorsFromHMONITOR(mons[idx], cnt, arr)
    try:
        return fn(arr[0].hPhysicalMonitor)
    finally:
        dxva2.DestroyPhysicalMonitor(arr[0].hPhysicalMonitor)


def read_brightness(idx):
    def fn(h):
        mn, cur, mx = wt.DWORD(), wt.DWORD(), wt.DWORD()
        if not dxva2.GetMonitorBrightness(h, ctypes.byref(mn), ctypes.byref(cur), ctypes.byref(mx)):
            return None
        return cur.value

    return _with_physical(idx, fn)


def write_brightness(idx, pct):
    pct = max(0, min(100, int(pct)))

    def fn(h):
        mn, cur, mx = wt.DWORD(), wt.DWORD(), wt.DWORD()
        if not dxva2.GetMonitorBrightness(h, ctypes.byref(mn), ctypes.byref(cur), ctypes.byref(mx)):
            return False
        span = max(mx.value - mn.value, 1)
        raw = mn.value + int(round(pct / 100.0 * span))
        raw = max(mn.value, min(mx.value, raw))
        return bool(dxva2.SetMonitorBrightness(h, raw))

    return bool(_with_physical(idx, fn))


STATE = os.path.join('C:\\Users\\steven\\Documents\\Rainmeter\\Skins\\QuantoFlx\\@Resources', "bri_state.txt")
CMD = os.path.join('C:\\Users\\steven\\Documents\\Rainmeter\\Skins\\QuantoFlx\\@Resources', "bri_cmd.txt")


_last = [-1, -1, -1]


def _write_state():
    # Atomic replace so the skin-side Lua never reads a half-written file.
    # Per-pid temp name: the daemon and one-shot set/bump can race.
    tmp = "%s.%d.tmp" % (STATE, os.getpid())
    with open(tmp, "w") as f:
        f.write(" ".join(str(x) for x in _last))
    os.replace(tmp, STATE)


def push_vars():
    # Keep the last good reading: a transient DDC failure must not blank
    # the slider (the skin treats -1 as "unavailable").
    for i in range(3):
        v = read_brightness(i)
        if v is not None:
            _last[i] = v
    _write_state()


def _alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


class _PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", ctypes.c_uint32),
        ("cntUsage", ctypes.c_uint32),
        ("th32ProcessID", ctypes.c_uint32),
        ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", ctypes.c_uint32),
        ("cntThreads", ctypes.c_uint32),
        ("th32ParentProcessID", ctypes.c_uint32),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", ctypes.c_uint32),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


def _rainmeter_alive():
    # Enumerate via kernel32 only: spawning tasklist from pythonw would flash
    # a console window every few seconds on the desktop.
    TH32CS_SNAPPROCESS = 0x00000002
    snap = ctypes.windll.kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap in (0, -1, None):
        return True  # cannot tell; stay up
    try:
        pe = _PROCESSENTRY32W()
        pe.dwSize = ctypes.sizeof(_PROCESSENTRY32W)
        if not ctypes.windll.kernel32.Process32FirstW(snap, ctypes.byref(pe)):
            return True
        while True:
            if pe.szExeFile.lower() == "rainmeter.exe":
                return True
            if not ctypes.windll.kernel32.Process32NextW(snap, ctypes.byref(pe)):
                return False
    finally:
        ctypes.windll.kernel32.CloseHandle(snap)


def _run_cmd():
    """Drain queued skin requests (set/bump) from bri_cmd.txt."""
    try:
        with open(CMD, "r") as f:
            lines = f.read().splitlines()
        os.remove(CMD)
    except OSError:
        return False
    # Collapse the queue: rapid drag/scroll may enqueue many steps; resolve to
    # one final absolute value per monitor so DDC is written only once.
    targets = {}
    for payload in lines:
        parts = payload.split()
        if len(parts) < 3:
            continue
        action, idx, val = parts[0], int(parts[1]), int(parts[2])
        if action == "set":
            targets[idx] = max(0, min(100, int(val)))
        elif action == "bump":
            base = targets.get(idx)
            if base is None:
                if _last[idx] >= 0:
                    base = _last[idx]
                else:
                    cur = read_brightness(idx)
                    base = 0 if cur is None else cur
            targets[idx] = max(0, min(100, base + int(val)))
    done = False
    for idx, target in targets.items():
        if write_brightness(idx, target):
            # Trust the written value and publish immediately; the periodic
            # re-read below realigns if the hardware clamps it.
            _last[idx] = target
            done = True
    if done:
        _write_state()
    return done


def daemon():
    if os.path.exists(PIDFILE):
        try:
            old = int(open(PIDFILE).read().strip())
        except ValueError:
            old = None
        if old and old != os.getpid() and _alive(old):
            return
    try:
        open(PIDFILE, "w").write(str(os.getpid()))
    except OSError:
        pass
    # Seed _last from the hardware so an early bump is not based on -1.
    push_vars()
    align_until = 0.0
    align_next = 0.0
    proc_check_at = 0.0
    try:
        while True:
            now = time.time()
            # Follow Rainmeter's lifecycle: drop out when it is gone.
            if now >= proc_check_at:
                proc_check_at = now + 3.0
                if not _rainmeter_alive():
                    return
            if os.path.exists(CMD):
                _run_cmd()
                # Brief alignment window after a write (confirm/clamp), then
                # go idle: no hardware polling while the user is not adjusting.
                align_until = now + 1.5
                align_next = 0.0
            elif now < align_until:
                if now >= align_next:
                    push_vars()
                    align_next = now + 0.3
            time.sleep(0.05)
    finally:
        try:
            os.remove(PIDFILE)
        except OSError:
            pass


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    cmd = argv[0]
    if cmd == "get":
        for i in range(3):
            print("Monitor %d: %s" % (i, read_brightness(i)))
        return 0
    if cmd == "set" and len(argv) >= 3:
        idx, val = int(argv[1]), int(argv[2])
        ok = write_brightness(idx, val)
        if ok:
            push_vars()
        print("set %d -> %d: %s" % (idx, val, "ok" if ok else "fail"))
        return 0 if ok else 2
    if cmd == "bump" and len(argv) >= 3:
        idx, delta = int(argv[1]), int(argv[2])
        cur = read_brightness(idx)
        if cur is None:
            print("bump %d: monitor unavailable" % idx)
            return 2
        ok = write_brightness(idx, cur + delta)
        if ok:
            push_vars()
        print("bump %d %+d (from %d): %s" % (idx, delta, cur, "ok" if ok else "fail"))
        return 0 if ok else 2
    if cmd == "daemon":
        daemon()
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
