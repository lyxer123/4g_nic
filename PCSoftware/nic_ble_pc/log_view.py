"""Colored log Text widget with ESP-IDF–style line tagging and max line count."""

from __future__ import annotations

import re
from datetime import datetime
import tkinter as tk
from tkinter import ttk

# ESP-ROM / bootloader style
_RE_ROM = re.compile(r"^(load:|entry |rst:)", re.I)
# I (123) tag: ...
_RE_IDF = re.compile(r"^([IWEVD])\s+\(\d+\)")
_RE_OK = re.compile(r"^ok\s", re.I)
_RE_ERR = re.compile(r"^(ERR|error|Guru Meditation)", re.I)


def line_tag(line: str) -> str:
    s = line.rstrip("\r\n")
    if _RE_ERR.search(s):
        return "lvl_e"
    if _RE_OK.match(s):
        return "lvl_ok"
    m = _RE_IDF.match(s)
    if m:
        lv = m.group(1).upper()
        if lv == "E":
            return "lvl_e"
        if lv == "W":
            return "lvl_w"
        if lv == "I":
            return "lvl_i"
        if lv in ("D", "V"):
            return "lvl_d"
    if _RE_ROM.match(s):
        return "rom"
    if s.startswith("---") or s.startswith("Backtrace:"):
        return "meta"
    return "plain"


class ColoredLog(ttk.Frame):
    """Right-side log: scroll up, trim to max_lines, per-line color tags."""

    def __init__(self, parent: tk.Misc, *, max_lines: int = 800, font: tuple[str, int] | None = None) -> None:
        super().__init__(parent)
        self.max_lines = max_lines
        ff = font or ("Consolas", 9)
        self.text = tk.Text(
            self,
            wrap=tk.NONE,
            font=ff,
            state=tk.DISABLED,
            bg="#1e1e1e",
            fg="#d4d4d4",
            insertbackground="#d4d4d4",
            selectbackground="#264f78",
            relief=tk.FLAT,
            padx=6,
            pady=4,
        )
        yscroll = ttk.Scrollbar(self, orient=tk.VERTICAL, command=self.text.yview)
        self.text.configure(yscrollcommand=yscroll.set)
        self.text.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)

        # Tags (dark terminal–like)
        self.text.tag_configure("plain", foreground="#d4d4d4")
        self.text.tag_configure("lvl_i", foreground="#4ec9b0")
        self.text.tag_configure("lvl_w", foreground="#dcdcaa")
        self.text.tag_configure("lvl_e", foreground="#f44747")
        self.text.tag_configure("lvl_d", foreground="#808080")
        self.text.tag_configure("lvl_ok", foreground="#6a9955")
        self.text.tag_configure("rom", foreground="#ce9178")
        self.text.tag_configure("meta", foreground="#569cd6")
        self.text.tag_configure("src_ble", foreground="#c586c0")
        self.text.tag_configure("src_serial", foreground="#9cdcfe")

    def append_line(self, line: str, *, source: str | None = None) -> None:
        """Append one logical line (no trailing \\n in `line`)."""
        body_tag = line_tag(line)
        self.text.configure(state=tk.NORMAL)
        if source == "ble":
            self.text.insert(tk.END, "[BLE] ", ("src_ble",))
            self.text.insert(tk.END, line + "\n", (body_tag,))
        elif source == "serial":
            ts = datetime.now().strftime("%H:%M:%S")
            self.text.insert(tk.END, f"[串][{ts}] ", ("src_serial",))
            self.text.insert(tk.END, line + "\n", (body_tag,))
        else:
            self.text.insert(tk.END, line + "\n", (body_tag,))
        self._trim()
        self.text.see(tk.END)
        self.text.configure(state=tk.DISABLED)

    def append_block(self, text: str, *, source: str | None = None) -> None:
        for ln in text.splitlines():
            self.append_line(ln, source=source)

    def _trim(self) -> None:
        try:
            last = self.text.index("end-1c")
            line_no = int(last.split(".")[0])
        except (ValueError, tk.TclError):
            return
        if line_no <= self.max_lines:
            return
        to_del = line_no - self.max_lines
        self.text.delete("1.0", f"{to_del + 1}.0")

    def clear(self) -> None:
        self.text.configure(state=tk.NORMAL)
        self.text.delete("1.0", tk.END)
        self.text.configure(state=tk.DISABLED)
