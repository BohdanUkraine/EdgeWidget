"""
MediaSpin emulator: a stand-in for the physical record player while everything runs on one PC.

It plays the device side of the protocol (shared/PROTOCOL.md): a TCP server on 127.0.0.1:47800,
protobuf MessageRequest in varint-length-prefixed frames. EdgeWidget connects to it like it will
connect to the real player over BLE.

    pip install protobuf
    python emulator/mediaspin_emulator.py

Regenerate api/request_pb2.py after changing shared/proto/api/request.proto:
    python -m grpc_tools.protoc -I shared/proto --python_out=emulator shared/proto/api/request.proto
"""

import json
import os
import queue
import socket
import threading
import tkinter as tk
from pathlib import Path
from tkinter import simpledialog, ttk

from api.request_pb2 import MessageRequest, Record

HOST, PORT = "127.0.0.1", 47800
Cmd = MessageRequest.Command
RECORDS_FILE = Path(__file__).with_name("emulator_records.json")
# EdgeWidget's library cache: real playlists / albums to put on the emulated records.
LIBRARY_CACHE = Path(os.environ.get("APPDATA", "")) / "EdgeWidget" / "EdgeWidget" / "library.json"
DEFAULT_COLORS = [0xE53935, 0x1E88E5, 0x43A047, 0xFDD835, 0x8E24AA, 0xFB8C00]
RECORD_COUNT = 6
FADER_TICK_MS = 500


# ---------------------------------------------------------------- framing

def encode_varint(value: int) -> bytes:
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def frame(msg: MessageRequest) -> bytes:
    body = msg.SerializeToString()
    return encode_varint(len(body)) + body


class FrameReader:
    """Splits a TCP byte stream into length-prefixed protobuf messages."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[MessageRequest]:
        self.buf += data
        messages = []
        while True:
            length, shift, pos = 0, 0, 0
            while pos < len(self.buf):
                byte = self.buf[pos]
                pos += 1
                length |= (byte & 0x7F) << shift
                if not byte & 0x80:
                    break
                shift += 7
            else:
                return messages  # length prefix not complete yet
            if len(self.buf) - pos < length:
                return messages  # body not complete yet
            msg = MessageRequest()
            msg.ParseFromString(bytes(self.buf[pos:pos + length]))
            messages.append(msg)
            del self.buf[:pos + length]


def describe(msg: MessageRequest) -> str:
    text = Cmd.Name(msg.command)
    if msg.data:
        text += f"  data={msg.data!r}"
    if msg.HasField("record"):
        text += f"  record{{uri={msg.record.uri!r}, color=#{msg.record.color:06X}}}"
    return text


def mmss(seconds: float) -> str:
    s = int(seconds)
    return f"{s // 60}:{s % 60:02d}"


# ---------------------------------------------------------------- server

class Server:
    """One client at a time (like the BLE peripheral). Events go to the UI through a queue."""

    def __init__(self, events: queue.Queue):
        self.events = events
        self.conn: socket.socket | None = None
        self.lock = threading.Lock()
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # Windows' SO_REUSEADDR would let a second emulator share the port; refuse that instead.
        srv.setsockopt(socket.SOL_SOCKET, getattr(socket, "SO_EXCLUSIVEADDRUSE", socket.SO_REUSEADDR), 1)
        try:
            srv.bind((HOST, PORT))
        except OSError as e:
            self.events.put(("error", f"порт {PORT} зайнятий (емулятор уже запущено?): {e}"))
            return
        srv.listen(1)
        while True:
            conn, addr = srv.accept()
            with self.lock:
                self.conn = conn
            self.events.put(("connected", addr))
            reader = FrameReader()
            try:
                while data := conn.recv(4096):
                    for msg in reader.feed(data):
                        self.events.put(("message", msg))
            except OSError:
                pass
            with self.lock:
                self.conn = None
            conn.close()
            self.events.put(("disconnected", None))

    def send(self, msg: MessageRequest) -> bool:
        with self.lock:
            if not self.conn:
                return False
            try:
                self.conn.sendall(frame(msg))
                return True
            except OSError:
                return False


# ---------------------------------------------------------------- app

class Emulator:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("MediaSpin emulator")
        self.root.resizable(False, False)

        self.events: queue.Queue = queue.Queue()
        self.server = Server(self.events)

        self.records = self._load_records()
        self.on_reader: int | None = None   # index of the record lying on the reader
        self.track_len = 0
        self.playing = False
        self.dragging = False
        self.shuffle = False
        self.repeat = 0  # 0 off, 1 album (one lamp), 2 track (two lamps)

        self._build_ui()
        self.root.after(50, self._poll)
        self.root.after(FADER_TICK_MS, self._fader_tick)

    # ------------------------------------------------------------ records

    def _load_records(self) -> list[dict]:
        if RECORDS_FILE.exists():
            return json.loads(RECORDS_FILE.read_text(encoding="utf-8"))
        items = []
        if LIBRARY_CACHE.exists():
            items = [i for i in json.loads(LIBRARY_CACHE.read_text(encoding="utf-8")) if i.get("uri")]
        records = []
        for n in range(RECORD_COUNT):
            item = items[n] if n < len(items) else {}
            records.append({"name": item.get("name", f"Платівка {n + 1}"), "uri": item.get("uri", ""),
                            "color": DEFAULT_COLORS[n % len(DEFAULT_COLORS)]})
        return records

    def _save_records(self):
        RECORDS_FILE.write_text(json.dumps(self.records, ensure_ascii=False, indent=2), encoding="utf-8")

    # ------------------------------------------------------------ ui

    def _build_ui(self):
        pad = {"padx": 8, "pady": 4}

        self.status = ttk.Label(self.root, text=f"Чекаю на віджет: {HOST}:{PORT}")
        self.status.pack(anchor="w", **pad)

        rec = ttk.LabelFrame(self.root, text="Платівки (колір — заводський) — клік: покласти на рідер, правий клік: змінити вручну")
        rec.pack(fill="x", **pad)
        self.canvas = tk.Canvas(rec, width=6 * 92, height=130, bg="#202020", highlightthickness=0)
        self.canvas.pack(padx=6, pady=6)
        self.canvas.bind("<Button-1>", lambda e: self._click_record(e, edit=False))
        self.canvas.bind("<Button-3>", lambda e: self._click_record(e, edit=True))
        self._draw_records()

        ctl = ttk.LabelFrame(self.root, text="Керування")
        ctl.pack(fill="x", **pad)
        row = ttk.Frame(ctl)
        row.pack(fill="x", padx=6, pady=4)
        for text, cmd in [("⏮", Cmd.GO_BACK), ("⏸", Cmd.PAUSE), ("▶", Cmd.RESUME), ("⏭", Cmd.GO_FORTH)]:
            ttk.Button(row, text=text, width=9, command=lambda c=cmd: self._button(c)).pack(side="left", padx=2)
        # Lamps on the case: the buttons change them, the PC sets them too.
        ttk.Button(row, text="🔁 повтор", width=9, command=self._cycle_repeat).pack(side="left", padx=(8, 0))
        self.repeat_lamps = [tk.Label(row, text="●", font=("Segoe UI", 12)) for _ in range(2)]
        for lamp in self.repeat_lamps:
            lamp.pack(side="left")
        ttk.Button(row, text="🔀 шафл", width=9, command=self._toggle_shuffle).pack(side="left", padx=(8, 0))
        self.shuffle_lamp = tk.Label(row, text="●", font=("Segoe UI", 12))
        self.shuffle_lamp.pack(side="left")
        self._draw_lamps()

        vol = ttk.Frame(ctl)
        vol.pack(fill="x", padx=6, pady=4)
        ttk.Label(vol, text="Гучність").pack(side="left")
        self.volume = tk.Scale(vol, from_=0, to=100, orient="horizontal", length=300, showvalue=True)
        self.volume.set(50)
        self.volume.pack(side="left", padx=6)
        self.volume.bind("<ButtonRelease-1>", lambda e: self._send(Cmd.SET_VOLUME, str(self.volume.get())))

        fad = ttk.Frame(ctl)
        fad.pack(fill="x", padx=6, pady=4)
        ttk.Label(fad, text="Фейдер").pack(side="left")
        self.fader = tk.Scale(fad, from_=0, to=1, orient="horizontal", length=360, showvalue=False,
                              resolution=0.5)
        self.fader.pack(side="left", padx=6)
        self.fader_label = ttk.Label(fad, text="0:00 / 0:00", width=12)
        self.fader_label.pack(side="left")
        self.fader.bind("<ButtonPress-1>", lambda e: setattr(self, "dragging", True))
        self.fader.bind("<ButtonRelease-1>", self._fader_released)

        raw = ttk.LabelFrame(self.root, text="Ручна відправка")
        raw.pack(fill="x", **pad)
        self.raw_cmd = ttk.Combobox(raw, values=[n for n in Cmd.keys() if n != "UNKNOWN"], width=12,
                                    state="readonly")
        self.raw_cmd.set("SWITCH_TO")
        self.raw_data = ttk.Entry(raw, width=8)
        self.raw_uri = ttk.Entry(raw, width=34)
        self.raw_color = ttk.Entry(raw, width=8)
        for label, widget in [("команда", self.raw_cmd), ("data", self.raw_data),
                              ("record.uri", self.raw_uri), ("колір #", self.raw_color)]:
            ttk.Label(raw, text=label).pack(side="left", padx=(6, 2))
            widget.pack(side="left", pady=6)
        ttk.Button(raw, text="Відправити", command=self._send_raw).pack(side="left", padx=6)

        logf = ttk.LabelFrame(self.root, text="Обмін (→ відправлено, ← отримано)")
        logf.pack(fill="both", **pad)
        self.log = tk.Text(logf, width=96, height=12, bg="#111", fg="#ddd", font=("Consolas", 9))
        self.log.pack(padx=6, pady=6)

    def _draw_records(self):
        c = self.canvas
        c.delete("all")
        for i, r in enumerate(self.records):
            x, y = 46 + i * 92, 50
            color = f"#{r['color']:06X}"
            if i == self.on_reader:  # lying on the reader
                c.create_oval(x - 42, y - 42, x + 42, y + 42, outline="#1ED760", width=3)
            c.create_oval(x - 36, y - 36, x + 36, y + 36, fill=color, outline="#000")
            groove = "#%02X%02X%02X" % tuple(int(v * 0.6) for v in bytes.fromhex(color[1:]))
            for k in (28, 20):  # grooves, a darker shade of the record colour
                c.create_oval(x - k, y - k, x + k, y + k, outline=groove)
            c.create_oval(x - 10, y - 10, x + 10, y + 10, fill="#EEE", outline="")
            c.create_oval(x - 2, y - 2, x + 2, y + 2, fill="#000", outline="")
            name = r["name"] if len(r["name"]) <= 12 else r["name"][:11] + "…"
            c.create_text(x, y + 52, text=name, fill="#EEE", font=("Segoe UI", 8))
            c.create_text(x, y + 66, text="" if r["uri"] else "(порожня)", fill="#888", font=("Segoe UI", 7))

    def _record_at(self, x: int) -> int | None:
        i = x // 92
        return i if 0 <= i < len(self.records) else None

    # ------------------------------------------------------------ actions

    def _click_record(self, event, edit: bool):
        i = self._record_at(event.x)
        if i is None:
            return
        if edit:
            self._edit_record(i)
            return

        self.on_reader = i
        self._draw_records()
        self._log(f"  платівка {i + 1} на рідері")
        self._announce()

    def _announce(self):
        """SWITCH_TO for the record on the reader: its colour always, its uri if it has one."""
        if self.on_reader is None:
            return
        r = self.records[self.on_reader]
        self._send(Cmd.SWITCH_TO, r["uri"], Record(uri=r["uri"], color=r["color"]))

    def _write(self, msg: MessageRequest):
        """BIND / UNBIND: a tag can only be written while it lies on the reader."""
        if self.on_reader is None:
            self._log("  на рідері немає платівки: нічого не записано")
            return
        r = self.records[self.on_reader]
        if msg.command == Cmd.BIND:  # only the uri is written; the colour is the record's own
            r["uri"] = msg.record.uri or msg.data
            r["name"] = r["uri"].split(":")[-1][:12] or r["name"]
            self._log(f"  записано на платівку {self.on_reader + 1}")
        else:
            r["uri"] = ""
            self._log(f"  платівку {self.on_reader + 1} стерто")
        self._save_records()
        self._draw_records()
        self._announce()

    def _edit_record(self, i: int):
        r = self.records[i]
        # Like BIND from the PC: only the link can be written, the colour is factory-set.
        uri = simpledialog.askstring("Платівка", "Spotify URI або посилання:", initialvalue=r["uri"],
                                     parent=self.root)
        if uri is None:
            return
        name = simpledialog.askstring("Платівка", "Назва:", initialvalue=r["name"], parent=self.root)
        r["uri"] = uri.strip()
        if name:
            r["name"] = name
        self._save_records()
        self._draw_records()

    def _button(self, cmd: int):
        self._send(cmd)
        if cmd in (Cmd.PAUSE, Cmd.RESUME):  # the device's own state follows its buttons
            self.playing = cmd == Cmd.RESUME

    def _cycle_repeat(self):
        # off -> album (one lamp) -> track (two lamps) -> off
        self.repeat = (self.repeat + 1) % 3
        self._draw_lamps()
        if self.repeat == 1:
            self._send(Cmd.LOOP_ALBUM, "1")
        else:
            self._send(Cmd.LOOP_SONG, "1" if self.repeat == 2 else "0")

    def _toggle_shuffle(self):
        self.shuffle = not self.shuffle
        self._draw_lamps()
        self._send(Cmd.REMIX, "1" if self.shuffle else "0")

    def _set_mode(self, cmd: int, on: bool):
        """Lamps from the PC. It sends LOOP_ALBUM and LOOP_SONG together."""
        if cmd == Cmd.REMIX:
            self.shuffle = on
        else:
            mode = 1 if cmd == Cmd.LOOP_ALBUM else 2
            if on:
                self.repeat = mode
            elif self.repeat == mode:
                self.repeat = 0
        self._draw_lamps()

    def _draw_lamps(self):
        green, off = "#1ED760", "#555"
        for n, lamp in enumerate(self.repeat_lamps):
            lamp.config(fg=green if self.repeat > n else off)
        self.shuffle_lamp.config(fg=green if self.shuffle else off)

    def _fader_released(self, _event):
        self.dragging = False
        if self.track_len:
            self._send(Cmd.MOVE_TO, str(int(self.fader.get())))

    def _send_raw(self):
        msg = MessageRequest(command=Cmd.Value(self.raw_cmd.get()), data=self.raw_data.get())
        if self.raw_uri.get() or self.raw_color.get():
            try:
                color = int(self.raw_color.get().lstrip("#") or "0", 16)
            except ValueError:
                self._log("× колір має бути hex, напр. E53935")
                return
            msg.record.CopyFrom(Record(uri=self.raw_uri.get(), color=color))
        self._send_msg(msg)

    def _send(self, cmd: int, data: str = "", record: Record | None = None):
        msg = MessageRequest(command=cmd, data=data)
        if record is not None:
            msg.record.CopyFrom(record)
        self._send_msg(msg)

    def _send_msg(self, msg: MessageRequest):
        if self.server.send(msg):
            self._log("→ " + describe(msg))
        else:
            self._log("× немає з'єднання: " + describe(msg))

    # ------------------------------------------------------------ incoming (from the PC)

    def _handle(self, msg: MessageRequest):
        self._log("← " + describe(msg))
        cmd = msg.command
        if cmd == Cmd.TRACK_LEN:
            self.track_len = max(0, round(float(msg.data or 0)))
            self.fader.config(to=max(1, self.track_len))
            self.fader.set(0)
            self.playing = True
        elif cmd == Cmd.PAUSE:
            self.playing = False
        elif cmd == Cmd.RESUME:
            self.playing = True
        elif cmd == Cmd.MOVE_TO:
            if not self.dragging:
                self.fader.set(int(float(msg.data or 0)))
        elif cmd in (Cmd.BIND, Cmd.UNBIND):
            self._write(msg)
        elif cmd in (Cmd.REMIX, Cmd.LOOP_ALBUM, Cmd.LOOP_SONG):
            self._set_mode(cmd, msg.data == "1")
        self._update_fader_label()

    def _poll(self):
        try:
            self._drain_events()
        except Exception as e:  # a bad message must not stop the loop
            self._log(f"!! помилка: {e!r}")
        self.root.after(50, self._poll)

    def _drain_events(self):
        while not self.events.empty():
            kind, payload = self.events.get()
            if kind == "connected":
                self.status.config(text=f"Віджет підключено ({payload[0]}:{payload[1]})")
                self._log("== віджет підключився")
                self._announce()  # the PC learns what lies on the reader
            elif kind == "error":
                self.status.config(text=payload)
                self._log("!! " + payload)
            elif kind == "disconnected":
                self.status.config(text=f"Чекаю на віджет: {HOST}:{PORT}")
                self._log("== віджет відключився")
            else:
                self._handle(payload)

    # ------------------------------------------------------------ fader motor

    def _fader_tick(self):
        if self.playing and self.track_len and not self.dragging:
            pos = min(self.track_len, self.fader.get() + FADER_TICK_MS / 1000)
            self.fader.set(pos)
            if pos >= self.track_len:
                self.playing = False
        self._update_fader_label()
        self.root.after(FADER_TICK_MS, self._fader_tick)

    def _update_fader_label(self):
        self.fader_label.config(text=f"{mmss(self.fader.get())} / {mmss(self.track_len)}")

    def _log(self, text: str):
        self.log.insert("end", text + "\n")
        self.log.see("end")

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    Emulator().run()
