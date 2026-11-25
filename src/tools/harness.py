"""Minimal headless libretro frontend for Power Rangers (SNES) research.
Pure Python standard library (ctypes + zlib + struct) - no PIL required."""
import ctypes as C
import os
import struct
import zlib

DEFAULT_CORE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "snes9x_libretro.dll"))
CORE = os.environ.get("CORE", DEFAULT_CORE)

B, Y, SELECT, START, UP, DOWN, LEFT, RIGHT, A, X, L, R = range(12)


class GameInfo(C.Structure):
    _fields_ = [
        ("path", C.c_char_p),
        ("data", C.c_void_p),
        ("size", C.c_size_t),
        ("meta", C.c_char_p),
    ]


ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VID = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUD = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
STATE = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


def write_png(path, w, h, rgb_bytes):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw.extend(rgb_bytes[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


class Snes:
    def __init__(self, rom_path, core_path=CORE):
        self.lib = C.CDLL(core_path)
        self.held = set()
        self.w = 256
        self.h = 224
        self.rgb = bytearray(256 * 224 * 3)
        self.fmt = 0
        self.opts = {}
        self.dirty = False
        self._keep_vals = []
        self._cbs = [
            ENV(self._env),
            VID(self._vid),
            AUD(lambda l, r: None),
            AUDB(lambda d, n: n),
            POLL(lambda: None),
            STATE(self._state),
        ]
        L = self.lib
        L.retro_set_environment(self._cbs[0])
        L.retro_init()
        L.retro_set_video_refresh(self._cbs[1])
        L.retro_set_audio_sample(self._cbs[2])
        L.retro_set_audio_sample_batch(self._cbs[3])
        L.retro_set_input_poll(self._cbs[4])
        L.retro_set_input_state(self._cbs[5])
        with open(rom_path, "rb") as f:
            self.rom = f.read()
        self._buf = C.create_string_buffer(self.rom, len(self.rom))
        gi = GameInfo(rom_path.encode("utf-8"), C.cast(self._buf, C.c_void_p), len(self.rom), None)
        assert L.retro_load_game(C.byref(gi)), "retro_load_game failed"
        L.retro_get_memory_data.restype = C.c_void_p
        L.retro_get_memory_size.restype = C.c_size_t
        L.retro_serialize_size.restype = C.c_size_t

    def _env(self, cmd, data):
        cmd &= 0xFFFF
        if cmd == 10:  # SET_PIXEL_FORMAT
            self.fmt = C.cast(data, C.POINTER(C.c_int))[0]
            return True
        if cmd in (9, 31):  # SYSTEM_DIRECTORY, SAVE_DIRECTORY
            s = C.c_char_p(b".")
            self._keep_vals.append(s)
            C.cast(data, C.POINTER(C.c_char_p))[0] = s
            return True
        if cmd == 15:  # GET_VARIABLE
            key = C.cast(data, C.POINTER(C.c_char_p))[0].decode("utf-8")
            val = self.opts.get(key)
            if val is None:
                return False
            s = C.c_char_p(val.encode("utf-8"))
            self._keep_vals.append(s)
            if len(self._keep_vals) > 64:
                self._keep_vals = self._keep_vals[-32:]
            C.cast(data, C.POINTER(C.c_void_p))[1] = C.cast(s, C.c_void_p)
            return True
        if cmd == 17:  # GET_VARIABLE_UPDATE
            C.cast(data, C.POINTER(C.c_bool))[0] = self.dirty
            self.dirty = False
            return True
        if cmd == 3:  # GET_CAN_DUPE
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        return False

    def _vid(self, data, w, h, pitch):
        if not data:
            return
        self.w, self.h, self._pitch = w, h, pitch
        self._raw = C.string_at(data, pitch * h)
        self.rgb = None

    def get_rgb(self):
        if self.rgb is not None:
            return self.rgb
        w, h, pitch, raw = self.w, self.h, self._pitch, self._raw
        out = bytearray(w * h * 3)
        if self.fmt == 2:  # RGB565
            idx = 0
            for y in range(h):
                row_off = y * pitch
                for x in range(w):
                    p = raw[row_off + x * 2] | (raw[row_off + x * 2 + 1] << 8)
                    r = (p >> 11) & 31
                    g = (p >> 5) & 63
                    b = p & 31
                    out[idx] = (r << 3) | (r >> 2)
                    out[idx + 1] = (g << 2) | (g >> 4)
                    out[idx + 2] = (b << 3) | (b >> 2)
                    idx += 3
        elif self.fmt == 1:  # XRGB8888
            idx = 0
            for y in range(h):
                row_off = y * pitch
                for x in range(w):
                    out[idx] = raw[row_off + x * 4 + 2]
                    out[idx + 1] = raw[row_off + x * 4 + 1]
                    out[idx + 2] = raw[row_off + x * 4]
                    idx += 3
        else:  # 0RGB1555
            idx = 0
            for y in range(h):
                row_off = y * pitch
                for x in range(w):
                    p = raw[row_off + x * 2] | (raw[row_off + x * 2 + 1] << 8)
                    r = (p >> 10) & 31
                    g = (p >> 5) & 31
                    b = p & 31
                    out[idx] = (r << 3) | (r >> 2)
                    out[idx + 1] = (g << 3) | (g >> 2)
                    out[idx + 2] = (b << 3) | (b >> 2)
                    idx += 3
        self.rgb = out
        return out

    def _state(self, port, device, index, id):
        if port != 0 or device != 1:
            return 0
        if id == 256:  # RETRO_DEVICE_ID_JOYPAD_MASK
            m = 0
            for b in self.held:
                m |= 1 << b
            return m
        return 1 if id in self.held else 0

    def reset(self):
        self.lib.retro_reset()

    def run(self, n=1, hold=()):
        self.held = set(hold)
        for _ in range(n):
            self.lib.retro_run()
        self.held = set()

    def tap(self, btn, wait=20, hold_frames=4):
        self.run(hold_frames, (btn,))
        self.run(wait)

    def mem(self, kind):  # 2 = WRAM (0x20000), 3 = VRAM (0x10000)
        p = self.lib.retro_get_memory_data(kind)
        n = self.lib.retro_get_memory_size(kind)
        return bytearray(C.string_at(p, n)) if p else bytearray()

    def poke(self, addr, val):
        p = self.lib.retro_get_memory_data(2)
        C.cast(p, C.POINTER(C.c_uint8))[addr] = val

    def save(self):
        n = self.lib.retro_serialize_size()
        b = C.create_string_buffer(n)
        assert self.lib.retro_serialize(b, n)
        return b.raw

    def load(self, s):
        b = C.create_string_buffer(s, len(s))
        assert self.lib.retro_unserialize(b, len(s))

    def layers(self, on):
        for i in range(1, 6):
            self.opts[f"snes9x_layer_{i}"] = "enabled" if i in on else "disabled"
        self.dirty = True

    def nonzero_pixels(self):
        rgb = self.get_rgb()
        cnt = 0
        for i in range(0, len(rgb), 3):
            if rgb[i] or rgb[i + 1] or rgb[i + 2]:
                cnt += 1
        return cnt

    def shot(self, path, scale=2):
        rgb = self.get_rgb()
        if scale == 1:
            write_png(path, self.w, self.h, rgb)
            return
        w, h = self.w, self.h
        nw, nh = w * scale, h * scale
        out = bytearray(nw * nh * 3)
        for y in range(h):
            row = bytearray(nw * 3)
            for x in range(w):
                r, g, b = rgb[(y * w + x) * 3:(y * w + x) * 3 + 3]
                for sx in range(scale):
                    o = (x * scale + sx) * 3
                    row[o] = r
                    row[o + 1] = g
                    row[o + 2] = b
            for sy in range(scale):
                yo = (y * scale + sy) * nw * 3
                out[yo:yo + nw * 3] = row
        write_png(path, nw, nh, out)
