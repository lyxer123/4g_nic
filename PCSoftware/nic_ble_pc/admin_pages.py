"""Web-style admin pages: same REST API as webPage/js/app.js (HTTP). Serial/BLE stay on gui right for CLI/log."""

from __future__ import annotations

import json
import threading
import time
import tkinter as tk
from dataclasses import dataclass
from tkinter import messagebox, scrolledtext, ttk
from typing import Any, Callable, Dict, List, Optional

from .device_http import WebApiClient, WebApiError, normalize_base_url

# Paths match webPage/js/app.js API
P_DASH = "/api/dashboard/overview"
P_USERS = "/api/users/online"
P_NET = "/api/network/config"
P_APN = "/api/network/apn"
P_WIFI_AP = "/api/wifi/ap"
P_WIFI_STA = "/api/wifi"
P_WIFI_CLEAR = "/api/wifi/clear"
P_MODE = "/api/mode"
P_MODE_APPLY = "/api/mode/apply"
P_ETH_WAN = "/api/eth_wan"
P_PROBES = "/api/system/probes"
P_TIME = "/api/system/time"
P_SYNC_TIME = "/api/system/sync_time"
P_REBOOT = "/api/system/reboot"
P_RB_SCHED = "/api/system/reboot/schedule"
P_LOGS = "/api/system/logs"
P_PASSWORD = "/api/system/password"
P_FACTORY = "/api/system/factory_reset"
P_CFG_EXP = "/api/system/config/export"
P_CFG_IMP = "/api/system/config/import"


WAN_TYPE_LABEL = {
    0: "无上行（仅配网 / WiFi热点）",
    1: "4G蜂窝连外网（WAN）",
    2: "WiFi 连上级路由（WAN）",
    3: "有线网络连上级路由（WAN）",
}


def wan_types_from_modes(modes: List[dict]) -> List[int]:
    s = {int(m["wan_type"]) for m in modes}
    all_t = sorted(s)
    vis = [t for t in all_t if t != 0]
    return vis if vis else all_t


def modes_for_wan(modes: List[dict], wt: int) -> List[dict]:
    return [m for m in modes if int(m["wan_type"]) == int(wt)]


def lan_summary_label(m: dict) -> str:
    lab = m.get("label")
    if lab is not None and str(lab).strip():
        return str(lab).strip()
    parts: List[str] = []
    if m.get("lan_softap"):
        parts.append("WiFi热点")
    if m.get("lan_eth"):
        parts.append("有线网络")
    if not parts:
        parts.append("无下行")
    return "+".join(parts)


@dataclass
class PageContext:
    root: tk.Misc
    get_base: Callable[[], str]
    log: Callable[[str], None]
    set_title: Callable[[str], None]


def _run_bg(root: tk.Misc, work: Callable[[], Any], ok: Callable[[Any], None], err: Callable[[BaseException], None]) -> None:
    def wrap() -> None:
        try:
            r = work()
        except BaseException as e:
            root.after(0, lambda: err(e))
            return
        root.after(0, lambda: ok(r))

    threading.Thread(target=wrap, daemon=True).start()


class AdminPages:
    def __init__(self, parent: ttk.Frame, ctx: PageContext) -> None:
        self.ctx = ctx
        self.pages: Dict[str, ttk.Frame] = {}
        self._mode_payload: Optional[dict] = None
        self._container = ttk.Frame(parent)
        self._container.pack(fill=tk.BOTH, expand=True)
        self._container.grid_rowconfigure(0, weight=1)
        self._container.grid_columnconfigure(0, weight=1)

        self._build_overview()
        self._build_users()
        self._build_network()
        self._build_apn()
        self._build_password()
        self._build_systime()
        self._build_upgrade()
        self._build_logs()
        self._build_probes()
        self._build_reboot()

    def show(self, page_id: str) -> None:
        fr = self.pages.get(page_id)
        if fr:
            fr.grid(row=0, column=0, sticky="nsew")
            fr.tkraise()
        titles = {
            "overview": "总览",
            "users": "用户列表",
            "network": "网络配置",
            "apn": "APN设置",
            "password": "管理密码",
            "systime": "系统时间",
            "upgrade": "升级/复位",
            "logs": "系统日志",
            "probes": "网络检测",
            "reboot": "重启",
        }
        self.ctx.set_title(titles.get(page_id, page_id))

    # --- helpers ---
    def _client(self) -> WebApiClient:
        return WebApiClient(normalize_base_url(self.ctx.get_base()))

    def _log(self, msg: str) -> None:
        self.ctx.log(msg)

    # --- overview ---
    def _build_overview(self) -> None:
        fr = ttk.LabelFrame(self._container, text="总览", padding=8)
        self.pages["overview"] = fr
        tbar = ttk.Frame(fr)
        tbar.pack(fill=tk.X)
        sys_kv = ttk.LabelFrame(fr, text="系统状态", padding=6)
        sys_kv.pack(fill=tk.X, pady=(8, 0))
        cell_kv = ttk.LabelFrame(fr, text="4G状态", padding=6)
        cell_kv.pack(fill=tk.X, pady=(8, 0))
        if_kv = ttk.LabelFrame(fr, text="接口状态", padding=6)
        if_kv.pack(fill=tk.BOTH, expand=True, pady=(8, 0))
        iface_txt = scrolledtext.ScrolledText(if_kv, height=10, wrap=tk.WORD, font=("Consolas", 9))
        iface_txt.pack(fill=tk.BOTH, expand=True)

        sys_labels: Dict[str, tk.StringVar] = {k: tk.StringVar(value="—") for k in ("mode", "model", "ver", "time", "mem", "uptime", "users")}
        cell_labels: Dict[str, tk.StringVar] = {k: tk.StringVar(value="—") for k in ("op", "nm", "imei", "iccid", "sig", "usb")}

        def row(parent: ttk.Frame, r: int, a: str, var: tk.StringVar) -> None:
            ttk.Label(parent, text=a).grid(row=r, column=0, sticky=tk.W, padx=(0, 8), pady=2)
            ttk.Label(parent, textvariable=var).grid(row=r, column=1, sticky=tk.W, pady=2)

        pairs_sys = [
            ("系统模式", "mode"),
            ("型号", "model"),
            ("版本", "ver"),
            ("系统时间", "time"),
            ("内存", "mem"),
            ("运行时间", "uptime"),
            ("在线用户数", "users"),
        ]
        for i, (a, k) in enumerate(pairs_sys):
            row(sys_kv, i, a, sys_labels[k])
        pairs_cell = [
            ("运营商", "op"),
            ("网络模式", "nm"),
            ("IMEI", "imei"),
            ("ICCID", "iccid"),
            ("网络信号", "sig"),
            ("USB", "usb"),
        ]
        for i, (a, k) in enumerate(pairs_cell):
            row(cell_kv, i, a, cell_labels[k])

        def fmt_dur(sec: Any) -> str:
            try:
                s = int(sec)
            except (TypeError, ValueError):
                return "—"
            h, m = s // 3600, (s % 3600) // 60
            r = s % 60
            return f"{h}时 {m}分 {r}秒"

        def apply_dash(d: dict) -> None:
            sys = d.get("system") or {}
            sys_labels["mode"].set(str(sys.get("system_mode") or "—"))
            sys_labels["model"].set("4G_NIC")
            sys_labels["ver"].set(str(sys.get("firmware_version") or "—"))
            sys_labels["time"].set(str(sys.get("system_time") or "—"))
            mp = sys.get("memory_percent")
            sys_labels["mem"].set(str(mp) + "%" if mp is not None else "—")
            sys_labels["uptime"].set(fmt_dur(sys.get("uptime_s")))
            ou = sys.get("online_users")
            sys_labels["users"].set(str(ou) if ou is not None else "—")
            cel = d.get("cellular") or {}
            cell_labels["op"].set(str(cel.get("operator") or "—"))
            cell_labels["nm"].set(str(cel.get("network_mode") or "—"))
            cell_labels["imei"].set(str(cel.get("imei") or "—"))
            cell_labels["iccid"].set(str(cel.get("iccid") or "—"))
            cell_labels["sig"].set(str(cel.get("signal") or "—"))
            cell_labels["usb"].set(str(cel.get("usb_probe") or "—"))
            ifaces = d.get("interfaces") or []
            lines: List[str] = []
            for it in ifaces:
                name = it.get("name") or "?"
                lines.append(f"{name}:  地址={it.get('address', '—')}  MAC={it.get('mac', '—')}")
            iface_txt.configure(state=tk.NORMAL)
            iface_txt.delete("1.0", tk.END)
            iface_txt.insert(tk.END, "\n".join(lines) if lines else "(无接口数据)")
            iface_txt.configure(state=tk.DISABLED)

        def refresh() -> None:
            def work() -> dict:
                return self._client().get(P_DASH)

            def ok(d: dict) -> None:
                apply_dash(d if isinstance(d, dict) else {})
                self._log("[HTTP] 总览已刷新")

            def er(e: BaseException) -> None:
                messagebox.showerror("总览", str(e))
                self._log("[HTTP] 总览失败: " + str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(tbar, text="刷新", command=refresh).pack(side=tk.LEFT)

    # --- users ---
    def _build_users(self) -> None:
        fr = ttk.LabelFrame(self._container, text="用户列表", padding=8)
        self.pages["users"] = fr
        tbar = ttk.Frame(fr)
        tbar.pack(fill=tk.X)
        foot = tk.StringVar(value="")
        cols = ("seq", "host", "dur", "ip", "mac", "bind", "allow")
        tv = ttk.Treeview(fr, columns=cols, show="headings", height=12)
        for c, h in zip(
            cols,
            ("序号", "在线用户", "在线时长", "IP地址", "MAC地址", "IP/MAC绑定", "允许接入"),
        ):
            tv.heading(c, text=h)
            tv.column(c, width=90, stretch=True)
        ys = ttk.Scrollbar(fr, orient=tk.VERTICAL, command=tv.yview)
        tv.configure(yscrollcommand=ys.set)
        tv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, pady=(8, 0))
        ys.pack(side=tk.RIGHT, fill=tk.Y, pady=(8, 0))
        ttk.Label(fr, textvariable=foot).pack(anchor=tk.W, pady=(4, 0))

        def refresh() -> None:
            def work() -> dict:
                return self._client().get(P_USERS)

            def ok(d: dict) -> None:
                for x in tv.get_children():
                    tv.delete(x)
                users = d.get("users") if isinstance(d, dict) else None
                if not isinstance(users, list):
                    users = []
                for i, u in enumerate(users):
                    tv.insert(
                        "",
                        tk.END,
                        values=(
                            u.get("id", i + 1),
                            u.get("hostname", ""),
                            u.get("online_duration", ""),
                            u.get("ip_address", ""),
                            u.get("mac_address", ""),
                            "—",
                            "—",
                        ),
                    )
                tot = d.get("total") if isinstance(d, dict) else None
                foot.set(f"共 {tot if tot is not None else len(users)} 条")
                self._log("[HTTP] 用户列表已刷新")

            def er(e: BaseException) -> None:
                messagebox.showerror("用户列表", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(tbar, text="刷新", command=refresh).pack(side=tk.LEFT)

    # --- network (simplified full parity with web save path) ---
    def _build_network(self) -> None:
        fr = ttk.LabelFrame(self._container, text="网络配置", padding=8)
        self.pages["network"] = fr
        outer = ttk.Frame(fr)
        outer.pack(fill=tk.BOTH, expand=True)
        canv = tk.Canvas(outer, highlightthickness=0)
        sb = ttk.Scrollbar(outer, orient=tk.VERTICAL, command=canv.yview)
        inner = ttk.Frame(canv)
        inner_win = canv.create_window((0, 0), window=inner, anchor=tk.NW)

        def _conf(_e: Any) -> None:
            canv.configure(scrollregion=canv.bbox("all"))
            canv.itemconfigure(inner_win, width=canv.winfo_width())

        inner.bind("<Configure>", _conf)
        canv.bind("<Configure>", lambda e: canv.itemconfigure(inner_win, width=e.width))
        canv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        canv.configure(yscrollcommand=sb.set)

        hw_hint = tk.StringVar(value="")
        ttk.Label(inner, textvariable=hw_hint, foreground="gray", wraplength=520).pack(anchor=tk.W)
        row1 = ttk.Frame(inner)
        row1.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(row1, text="WAN 上行").pack(side=tk.LEFT)
        wan_combo = ttk.Combobox(row1, width=36, state="readonly")
        wan_combo.pack(side=tk.LEFT, padx=8)
        row2 = ttk.Frame(inner)
        row2.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(row2, text="LAN 组合").pack(side=tk.LEFT)
        mode_combo = ttk.Combobox(row2, width=36, state="readonly")
        mode_combo.pack(side=tk.LEFT, padx=8)

        def set_hw(hw: Optional[dict]) -> None:
            if not hw:
                hw_hint.set("")
                return
            bits = [
                "本机 WiFi 已具备",
                "有线网络：" + ("已检测到" if hw.get("w5500") else "未检测到"),
                "USB 蜂窝："
                + (
                    "已检测到（" + str(hw.get("usb_ids") or "") + "）"
                    if hw.get("usb_modem_present")
                    else "未检测到"
                ),
            ]
            hw_hint.set(" · ".join(bits))

        def refill_mode_combo() -> None:
            md = self._mode_payload
            if not md or not isinstance(md.get("modes"), list):
                mode_combo["values"] = ()
                return
            try:
                wt = int(wan_combo.get().split(":")[0])
            except (ValueError, IndexError):
                return
            lst = modes_for_wan(md["modes"], wt)
            mode_combo["values"] = tuple(f'{int(m["id"])}: {lan_summary_label(m)}' for m in lst)

        def on_wan_change(_: Any = None) -> None:
            refill_mode_combo()
            if mode_combo["values"]:
                mode_combo.current(0)

        wan_combo.bind("<<ComboboxSelected>>", on_wan_change)

        # SoftAP
        ap_card = ttk.LabelFrame(inner, text="无线配置（SoftAP）", padding=6)
        ap_card.pack(fill=tk.X, pady=(10, 0))
        ap_ssid = tk.StringVar()
        ap_enc = tk.StringVar(value="WPA2-PSK")
        ap_pwd = tk.StringVar()
        ap_hid = tk.BooleanVar(value=False)
        ttk.Label(ap_card, text="SSID").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(ap_card, textvariable=ap_ssid, width=32).grid(row=0, column=1, sticky=tk.W, padx=6)
        ttk.Label(ap_card, text="加密").grid(row=1, column=0, sticky=tk.W, pady=4)
        ttk.Combobox(ap_card, textvariable=ap_enc, values=("WPA2-PSK", "WPA-PSK", "OPEN"), width=14, state="readonly").grid(
            row=1, column=1, sticky=tk.W, padx=6, pady=4
        )
        ttk.Label(ap_card, text="密码").grid(row=2, column=0, sticky=tk.W)
        ttk.Entry(ap_card, textvariable=ap_pwd, width=32, show="*").grid(row=2, column=1, sticky=tk.W, padx=6)
        ttk.Checkbutton(ap_card, text="隐藏WiFi", variable=ap_hid).grid(row=3, column=1, sticky=tk.W)

        # STA
        sta_card = ttk.LabelFrame(inner, text="WiFi STA（连上级）", padding=6)
        sta_card.pack(fill=tk.X, pady=(10, 0))
        sta_ssid = tk.StringVar()
        sta_pwd = tk.StringVar()
        ttk.Label(sta_card, text="SSID").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(sta_card, textvariable=sta_ssid, width=28).grid(row=0, column=1, padx=6)
        ttk.Label(sta_card, text="密码").grid(row=1, column=0, sticky=tk.W, pady=4)
        ttk.Entry(sta_card, textvariable=sta_pwd, width=28, show="*").grid(row=1, column=1, padx=6, pady=4)

        def load_sta() -> None:
            def work() -> dict:
                return self._client().get(P_WIFI_STA)

            def ok(d: dict) -> None:
                sta_ssid.set(str(d.get("ssid") or ""))
                sta_pwd.set(str(d.get("password") or ""))
                self._log("[HTTP] 已读取 STA")

            def er(e: BaseException) -> None:
                messagebox.showerror("STA", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_sta() -> None:
            def work() -> None:
                self._client().post_json(
                    P_WIFI_STA,
                    {"ssid": sta_ssid.get().strip(), "password": sta_pwd.get()},
                )

            def ok(_: Any) -> None:
                messagebox.showinfo("STA", "已保存")
                self._log("[HTTP] STA 已保存")

            def er(e: BaseException) -> None:
                messagebox.showerror("STA", str(e))

            if not sta_ssid.get().strip():
                messagebox.showwarning("STA", "请填写 SSID")
                return
            _run_bg(self.ctx.root, work, ok, er)

        def clear_sta() -> None:
            def work() -> None:
                self._client().post_json(P_WIFI_CLEAR, {})

            def ok(_: Any) -> None:
                sta_ssid.set("")
                sta_pwd.set("")
                self._log("[HTTP] STA 已清空")

            def er(e: BaseException) -> None:
                messagebox.showerror("STA", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        sf = ttk.Frame(sta_card)
        sf.grid(row=2, column=0, columnspan=2, sticky=tk.W, pady=6)
        ttk.Button(sf, text="读取 STA", command=load_sta).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(sf, text="保存 STA", command=save_sta).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(sf, text="清空 STA", command=clear_sta).pack(side=tk.LEFT)

        # ETH WAN
        ew_card = ttk.LabelFrame(inner, text="有线外网（ETH_WAN，按模式显示）", padding=6)
        ew_card.pack(fill=tk.X, pady=(10, 0))
        ew_dhcp = tk.BooleanVar(value=True)
        ew_ip, ew_mask, ew_gw = tk.StringVar(), tk.StringVar(), tk.StringVar()
        ew_d1, ew_d2 = tk.StringVar(), tk.StringVar()
        ttk.Checkbutton(ew_card, text="DHCP", variable=ew_dhcp).grid(row=0, column=0, sticky=tk.W)
        ttk.Label(ew_card, text="IP").grid(row=1, column=0, sticky=tk.W, pady=2)
        ttk.Entry(ew_card, textvariable=ew_ip, width=18).grid(row=1, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="掩码").grid(row=2, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_mask, width=18).grid(row=2, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="网关").grid(row=3, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_gw, width=18).grid(row=3, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="DNS1").grid(row=4, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_d1, width=18).grid(row=4, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="DNS2").grid(row=5, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_d2, width=18).grid(row=5, column=1, sticky=tk.W, padx=6)

        # LAN eth
        lan_card = ttk.LabelFrame(inner, text="有线网络（LAN）", padding=6)
        lan_card.pack(fill=tk.X, pady=(10, 0))
        lan_ip, lan_mask = tk.StringVar(), tk.StringVar()
        lan_dhcp = tk.BooleanVar(value=True)
        lan_s, lan_e = tk.StringVar(), tk.StringVar()
        lan_d1, lan_d2 = tk.StringVar(), tk.StringVar()
        r = 0
        ttk.Label(lan_card, text="IP").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_ip, width=18).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="掩码").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_mask, width=18).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Checkbutton(lan_card, text="DHCP 服务器", variable=lan_dhcp).grid(row=r, column=0, columnspan=2, sticky=tk.W, pady=4)
        r += 1
        ttk.Label(lan_card, text="起始IP").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_s, width=18).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="结束IP").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_e, width=18).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="DNS1").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_d1, width=18).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="DNS2").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_d2, width=18).grid(row=r, column=1, padx=6)

        def selected_mode_id() -> Optional[int]:
            s = mode_combo.get()
            if not s:
                return None
            try:
                return int(s.split(":", 1)[0].strip())
            except ValueError:
                return None

        def mode_row_by_id(mid: int) -> Optional[dict]:
            md = self._mode_payload
            if not md or not isinstance(md.get("modes"), list):
                return None
            for m in md["modes"]:
                if int(m["id"]) == mid:
                    return m
            return None

        def load_network() -> None:
            def work() -> tuple:
                c = self._client()
                net = c.get(P_NET)
                mode = c.get(P_MODE)
                ap = {}
                ew = {}
                try:
                    ap = c.get(P_WIFI_AP)
                except WebApiError:
                    pass
                try:
                    ew = c.get(P_ETH_WAN)
                except WebApiError:
                    pass
                return net, mode, ap, ew

            def ok(tup: tuple) -> None:
                net, mode, ap, ew = tup
                self._mode_payload = mode if isinstance(mode, dict) else {}
                modes = self._mode_payload.get("modes") if isinstance(self._mode_payload.get("modes"), list) else []
                set_hw(self._mode_payload.get("hardware") if isinstance(self._mode_payload, dict) else None)
                wts = wan_types_from_modes(modes)
                wan_labels = []
                opt_map = {}
                for o in self._mode_payload.get("wan_options") or []:
                    if isinstance(o, dict) and "wan_type" in o:
                        opt_map[int(o["wan_type"])] = o
                for t in wts:
                    note = ""
                    o = opt_map.get(t)
                    if o and o.get("reason_code") and o["reason_code"] != "ok":
                        note = f'（{o["reason_code"]}）'
                    wan_labels.append(f'{t}: {WAN_TYPE_LABEL.get(t, "WAN " + str(t))}{note}')
                wan_combo["values"] = tuple(wan_labels)
                cur_id = None
                if isinstance(net, dict):
                    if net.get("work_mode_id") is not None:
                        cur_id = int(net["work_mode_id"])
                    elif self._mode_payload.get("current") is not None:
                        cur_id = int(self._mode_payload["current"])
                cur_mode = next((m for m in modes if int(m["id"]) == cur_id), None) if cur_id is not None else None
                wan_t = None
                if cur_mode:
                    wan_t = int(cur_mode["wan_type"])
                elif isinstance(net, dict) and net.get("wan_type") is not None:
                    wan_t = int(net["wan_type"])
                if wan_t is None and wts:
                    wan_t = wts[0]
                if wan_t is not None and wts and wan_t not in wts:
                    wan_t = wts[0]
                if wan_t is not None:
                    idx = next((i for i, x in enumerate(wts) if x == wan_t), 0)
                    if wan_combo["values"]:
                        wan_combo.current(idx)
                on_wan_change()
                if cur_id is not None and mode_combo["values"]:
                    for i, lbl in enumerate(mode_combo["values"]):
                        if lbl.startswith(str(cur_id) + ":"):
                            mode_combo.current(i)
                            break

                if isinstance(ew, dict):
                    ew_dhcp.set(ew.get("dhcp") is not False)
                    ew_ip.set(str(ew.get("ip") or ""))
                    ew_mask.set(str(ew.get("mask") or ""))
                    ew_gw.set(str(ew.get("gw") or ""))
                    ew_d1.set(str(ew.get("dns1") or ""))
                    ew_d2.set(str(ew.get("dns2") or ""))

                lan = net.get("lan") if isinstance(net, dict) else {}
                if isinstance(lan, dict):
                    lan_ip.set(str(lan.get("ip") or ""))
                    lan_mask.set(str(lan.get("mask") or ""))
                    lan_dhcp.set(bool(lan.get("dhcp_enabled")))
                    lan_s.set(str(lan.get("dhcp_start") or ""))
                    lan_e.set(str(lan.get("dhcp_end") or ""))
                    lan_d1.set(str(lan.get("dns1") or ""))
                    lan_d2.set(str(lan.get("dns2") or ""))

                if isinstance(ap, dict):
                    ap_ssid.set(str(ap.get("ssid") or ""))
                    enc = str(ap.get("encryption_mode") or "WPA2-PSK")
                    ap_enc.set(enc if enc in ("WPA2-PSK", "WPA-PSK", "OPEN") else "WPA2-PSK")
                    ap_pwd.set("")
                    ap_hid.set(bool(ap.get("hidden_ssid")))

                self._log("[HTTP] 网络配置已读取")

            def er(e: BaseException) -> None:
                messagebox.showerror("网络配置", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_eth_wan_only() -> None:
            def work() -> None:
                c = self._client()
                c.post_json(
                    P_ETH_WAN,
                    {
                        "dhcp": ew_dhcp.get(),
                        "ip": ew_ip.get().strip(),
                        "mask": ew_mask.get().strip(),
                        "gw": ew_gw.get().strip(),
                        "dns1": ew_d1.get().strip(),
                        "dns2": ew_d2.get().strip(),
                    },
                )
                try:
                    c.post_json(P_MODE_APPLY, {})
                except WebApiError:
                    pass

            def ok(_: Any) -> None:
                messagebox.showinfo("ETH WAN", "已保存并尝试应用")
                self._log("[HTTP] 有线 WAN 已写")

            def er(e: BaseException) -> None:
                messagebox.showerror("ETH WAN", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_network() -> None:
            mid = selected_mode_id()
            if mid is None:
                messagebox.showwarning("网络", "请选择 LAN 组合")
                return
            row_pre = mode_row_by_id(mid)
            enc = ap_enc.get()
            pwd = ap_pwd.get()
            if row_pre and row_pre.get("lan_softap") and enc != "OPEN" and pwd and len(pwd) < 8:
                messagebox.showwarning("网络", "Wi‑Fi 密码至少 8 位")
                return

            def work() -> None:
                c = self._client()
                if row_pre and row_pre.get("lan_softap"):
                    c.post_json(
                        P_WIFI_AP,
                        {
                            "wifi_enabled": True,
                            "ssid": ap_ssid.get().strip(),
                            "encryption_mode": enc,
                            "password": pwd,
                            "hidden_ssid": ap_hid.get(),
                        },
                    )
                if row_pre and row_pre.get("needs_eth_wan"):
                    c.post_json(
                        P_ETH_WAN,
                        {
                            "dhcp": ew_dhcp.get(),
                            "ip": ew_ip.get().strip(),
                            "mask": ew_mask.get().strip(),
                            "gw": ew_gw.get().strip(),
                            "dns1": ew_d1.get().strip(),
                            "dns2": ew_d2.get().strip(),
                        },
                    )
                wt = int(wan_combo.get().split(":")[0])
                body = {
                    "work_mode_id": mid,
                    "wan_type": wt,
                    "lan": {
                        "ip": lan_ip.get().strip(),
                        "mask": lan_mask.get().strip(),
                        "dhcp_enabled": lan_dhcp.get(),
                        "dhcp_start": lan_s.get().strip(),
                        "dhcp_end": lan_e.get().strip(),
                        "dns1": lan_d1.get().strip(),
                        "dns2": lan_d2.get().strip(),
                    },
                }
                c.post_json(P_NET, body)

            def ok(_: Any) -> None:
                messagebox.showinfo("网络", "网络配置已保存")
                self._log("[HTTP] /api/network/config 已 POST")

            def er(e: BaseException) -> None:
                messagebox.showerror("网络", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        btnf = ttk.Frame(inner)
        btnf.pack(fill=tk.X, pady=(24, 8))
        ttk.Button(btnf, text="从设备读取", command=load_network).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(btnf, text="保存网络配置", command=save_network).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(btnf, text="仅保存有线 WAN 并应用", command=save_eth_wan_only).pack(side=tk.LEFT)

    def _build_apn(self) -> None:
        fr = ttk.LabelFrame(self._container, text="APN设置", padding=8)
        self.pages["apn"] = fr
        a, u, p = tk.StringVar(), tk.StringVar(), tk.StringVar()
        ttk.Label(fr, text="APN").pack(anchor=tk.W)
        ttk.Entry(fr, textvariable=a, width=40).pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(fr, text="用户名").pack(anchor=tk.W)
        ttk.Entry(fr, textvariable=u, width=40).pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(fr, text="密码").pack(anchor=tk.W)
        ttk.Entry(fr, textvariable=p, width=40, show="*").pack(anchor=tk.W, pady=(0, 10))

        def load_apn() -> None:
            def work() -> dict:
                return self._client().get(P_APN)

            def ok(d: dict) -> None:
                a.set(str(d.get("apn") or ""))
                u.set(str(d.get("username") or ""))
                p.set(str(d.get("password") or ""))
                self._log("[HTTP] APN 已读取")

            def er(e: BaseException) -> None:
                messagebox.showerror("APN", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_apn() -> None:
            def work() -> None:
                self._client().post_json(P_APN, {"apn": a.get().strip(), "username": u.get().strip(), "password": p.get()})

            def ok(_: Any) -> None:
                messagebox.showinfo("APN", "已保存")
                self._log("[HTTP] APN 已保存")

            def er(e: BaseException) -> None:
                messagebox.showerror("APN", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(bf, text="读取", command=load_apn).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="保存", command=save_apn).pack(side=tk.LEFT)

    def _build_password(self) -> None:
        fr = ttk.LabelFrame(self._container, text="管理密码", padding=8)
        self.pages["password"] = fr
        o, n1, n2 = tk.StringVar(), tk.StringVar(), tk.StringVar()
        ttk.Label(fr, text="原密码").pack(anchor=tk.W)
        ttk.Entry(fr, textvariable=o, width=32, show="*").pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(fr, text="新密码").pack(anchor=tk.W)
        ttk.Entry(fr, textvariable=n1, width=32, show="*").pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(fr, text="确认新密码").pack(anchor=tk.W)
        ttk.Entry(fr, textvariable=n2, width=32, show="*").pack(anchor=tk.W, pady=(0, 10))

        def save_pwd() -> None:
            if n1.get() != n2.get():
                messagebox.showerror("密码", "两次新密码不一致")
                return

            def work() -> None:
                self._client().post_json(P_PASSWORD, {"old_password": o.get(), "new_password": n1.get()})

            def ok(_: Any) -> None:
                messagebox.showinfo("密码", "已更新")
                self._log("[HTTP] 管理密码已更新")

            def er(e: BaseException) -> None:
                messagebox.showerror("密码", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(fr, text="保存", command=save_pwd).pack(anchor=tk.W)

    def _build_systime(self) -> None:
        fr = ttk.LabelFrame(self._container, text="系统时间", padding=8)
        self.pages["systime"] = fr
        disp = tk.StringVar()
        fwv = tk.StringVar()
        tz = tk.StringVar(value="CST-8")

        ttk.Label(fr, text="设备当前时间").pack(anchor=tk.W)
        ttk.Label(fr, textvariable=disp, anchor=tk.W, width=44).pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(fr, text="时区").pack(anchor=tk.W)
        ttk.Combobox(
            fr,
            textvariable=tz,
            values=("CST-8", "UTC0", "CST+9"),
            width=34,
            state="readonly",
        ).pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(fr, text="固件版本", foreground="gray").pack(anchor=tk.W, pady=(10, 0))
        ttk.Label(fr, textvariable=fwv, anchor=tk.W).pack(anchor=tk.W, pady=(0, 10))

        def load_t() -> None:
            def work() -> dict:
                return self._client().get(P_TIME)

            def ok(d: dict) -> None:
                disp.set(str(d.get("system_time") or ""))
                fwv.set(str(d.get("firmware_version") or ""))
                tzz = d.get("timezone")
                if tzz:
                    tz.set(str(tzz))
                self._log("[HTTP] 系统时间已读取")

            def er(e: BaseException) -> None:
                messagebox.showerror("时间", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def sync_local() -> None:
            def work() -> None:
                self._client().post_json(P_SYNC_TIME, {"local_timestamp_ms": int(time.time() * 1000)})

            def ok(_: Any) -> None:
                messagebox.showinfo("时间", "已同步本地时间")
                load_t()

            def er(e: BaseException) -> None:
                messagebox.showerror("时间", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_tz() -> None:
            def work() -> None:
                self._client().post_json(P_TIME, {"timezone": tz.get()})

            def ok(_: Any) -> None:
                messagebox.showinfo("时间", "时区已保存")
                self._log("[HTTP] 时区已保存")

            def er(e: BaseException) -> None:
                messagebox.showerror("时间", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(bf, text="读取", command=load_t).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="同步本地时间", command=sync_local).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="保存时区", command=save_tz).pack(side=tk.LEFT)

    def _build_upgrade(self) -> None:
        fr = ttk.LabelFrame(self._container, text="升级/复位", padding=8)
        self.pages["upgrade"] = fr
        ttk.Label(fr, text="恢复出厂：清除 Web UI 相关 NVS", foreground="gray").pack(anchor=tk.W)

        def factory() -> None:
            if not messagebox.askyesno("出厂", "确定恢复出厂？"):
                return

            def work() -> None:
                self._client().post_json(P_FACTORY, {})

            def ok(_: Any) -> None:
                messagebox.showinfo("出厂", "已执行，请重启设备")
                self._log("[HTTP] factory_reset")

            def er(e: BaseException) -> None:
                messagebox.showerror("出厂", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(fr, text="执行复位", command=factory).pack(anchor=tk.W, pady=(4, 16))
        ttk.Label(fr, text="备份 JSON（生成后可复制；或粘贴后导入）").pack(anchor=tk.W)
        txt = scrolledtext.ScrolledText(fr, height=8, wrap=tk.WORD, font=("Consolas", 9))
        txt.pack(fill=tk.BOTH, expand=True, pady=(4, 8))

        def export_cfg() -> None:
            def work() -> dict:
                return self._client().get(P_CFG_EXP)

            def ok(d: dict) -> None:
                txt.delete("1.0", tk.END)
                txt.insert(tk.END, json.dumps(d, ensure_ascii=False, indent=2))
                self._log("[HTTP] 已生成备份 JSON")

            def er(e: BaseException) -> None:
                messagebox.showerror("备份", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def import_cfg() -> None:
            raw = txt.get("1.0", tk.END).strip()
            if not raw:
                messagebox.showwarning("导入", "请粘贴 JSON")
                return
            try:
                obj = json.loads(raw)
            except json.JSONDecodeError as e:
                messagebox.showerror("导入", str(e))
                return
            if not isinstance(obj, dict):
                messagebox.showerror("导入", "需要 JSON 对象")
                return

            def work() -> None:
                self._client().post_json(P_CFG_IMP, obj)

            def ok(_: Any) -> None:
                messagebox.showinfo("导入", "导入成功")
                self._log("[HTTP] 配置已导入")

            def er(e: BaseException) -> None:
                messagebox.showerror("导入", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.pack(fill=tk.X)
        ttk.Button(bf, text="生成备份", command=export_cfg).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="导入配置", command=import_cfg).pack(side=tk.LEFT)
        ttk.Label(fr, text="本地固件刷写请用 esptool / idf.py（设备 HTTP 501）", foreground="gray").pack(anchor=tk.W, pady=(16, 0))

    def _build_logs(self) -> None:
        fr = ttk.LabelFrame(self._container, text="系统日志", padding=8)
        self.pages["logs"] = fr
        tbar = ttk.Frame(fr)
        tbar.pack(fill=tk.X)
        box = scrolledtext.ScrolledText(fr, height=22, wrap=tk.NONE, font=("Consolas", 9))
        box.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        def refresh() -> None:
            def work() -> dict:
                return self._client().get(P_LOGS)

            def ok(d: dict) -> None:
                logs = d.get("logs") if isinstance(d, dict) else None
                if not isinstance(logs, list):
                    logs = []
                box.delete("1.0", tk.END)
                box.insert(tk.END, "\n".join(str(x) for x in logs) if logs else "(无)")
                self._log("[HTTP] 系统日志已刷新")

            def er(e: BaseException) -> None:
                messagebox.showerror("日志", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def clear() -> None:
            def work() -> None:
                self._client().delete(P_LOGS)

            def ok(_: Any) -> None:
                refresh()

            def er(e: BaseException) -> None:
                messagebox.showerror("日志", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(tbar, text="刷新", command=refresh).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(tbar, text="清空", command=clear).pack(side=tk.LEFT)

    def _build_probes(self) -> None:
        fr = ttk.LabelFrame(self._container, text="网络检测", padding=8)
        self.pages["probes"] = fr
        p1, p2, p3, p4 = tk.StringVar(), tk.StringVar(), tk.StringVar(), tk.StringVar()
        for i, (lab, v) in enumerate((("线路探测1", p1), ("线路探测2", p2), ("线路探测3", p3), ("线路探测4", p4)), 0):
            ttk.Label(fr, text=lab).grid(row=i, column=0, sticky=tk.W, pady=4)
            ttk.Entry(fr, textvariable=v, width=36).grid(row=i, column=1, sticky=tk.W, padx=8, pady=4)

        def load_p() -> None:
            def work() -> dict:
                return self._client().get(P_PROBES)

            def ok(d: dict) -> None:
                p1.set(str(d.get("detection_ip1") or ""))
                p2.set(str(d.get("detection_ip2") or ""))
                p3.set(str(d.get("detection_ip3") or ""))
                p4.set(str(d.get("detection_ip4") or ""))
                self._log("[HTTP] 探测地址已读取")

            def er(e: BaseException) -> None:
                messagebox.showerror("探测", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_p() -> None:
            def work() -> None:
                self._client().post_json(
                    P_PROBES,
                    {
                        "detection_ip1": p1.get().strip(),
                        "detection_ip2": p2.get().strip(),
                        "detection_ip3": p3.get().strip(),
                        "detection_ip4": p4.get().strip(),
                    },
                )

            def ok(_: Any) -> None:
                messagebox.showinfo("探测", "已保存")
                self._log("[HTTP] probes 已保存")

            def er(e: BaseException) -> None:
                messagebox.showerror("探测", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.grid(row=5, column=0, columnspan=2, sticky=tk.W, pady=12)
        ttk.Button(bf, text="读取", command=load_p).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="保存", command=save_p).pack(side=tk.LEFT)

    def _build_reboot(self) -> None:
        fr = ttk.LabelFrame(self._container, text="重启", padding=8)
        self.pages["reboot"] = fr
        ttk.Label(fr, text="立即重启后设备将短暂断线。").pack(anchor=tk.W)

        def reboot_now() -> None:
            if not messagebox.askyesno("重启", "确定立即重启？"):
                return

            def work() -> None:
                try:
                    self._client().post_json(P_REBOOT, {})
                except WebApiError:
                    pass

            def ok(_: Any) -> None:
                messagebox.showinfo("重启", "已发送重启指令")
                self._log("[HTTP] reboot POST")

            def er(e: BaseException) -> None:
                messagebox.showwarning("重启", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(fr, text="执行重启", command=reboot_now).pack(anchor=tk.W, pady=(8, 24))
        sch_en = tk.BooleanVar(value=False)
        sch_h = tk.StringVar(value="3")
        sch_m = tk.StringVar(value="30")
        ttk.Checkbutton(fr, text="启用定时重启（仅保存 NVS，需固件调度）", variable=sch_en).pack(anchor=tk.W)
        rf = ttk.Frame(fr)
        rf.pack(fill=tk.X, pady=(8, 0))
        ttk.Label(rf, text="时").pack(side=tk.LEFT)
        ttk.Entry(rf, textvariable=sch_h, width=4).pack(side=tk.LEFT, padx=6)
        ttk.Label(rf, text="分").pack(side=tk.LEFT)
        ttk.Entry(rf, textvariable=sch_m, width=4).pack(side=tk.LEFT, padx=6)

        def load_sch() -> None:
            def work() -> dict:
                return self._client().get(P_RB_SCHED)

            def ok(d: dict) -> None:
                sch_en.set(bool(d.get("enabled")))
                sch_h.set(str(d.get("hour", 3)))
                sch_m.set(str(d.get("minute", 30)))

            def er(e: BaseException) -> None:
                messagebox.showerror("定时重启", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_sch() -> None:
            def work() -> None:
                self._client().post_json(
                    P_RB_SCHED,
                    {
                        "enabled": sch_en.get(),
                        "hour": int(sch_h.get() or 0),
                        "minute": int(sch_m.get() or 0),
                    },
                )

            def ok(_: Any) -> None:
                messagebox.showinfo("定时重启", "已保存")
                self._log("[HTTP] reboot schedule saved")

            def er(e: BaseException) -> None:
                messagebox.showerror("定时重启", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.pack(fill=tk.X, pady=(12, 0))
        ttk.Button(bf, text="读取定时设置", command=load_sch).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="保存定时设置", command=save_sch).pack(side=tk.LEFT)
