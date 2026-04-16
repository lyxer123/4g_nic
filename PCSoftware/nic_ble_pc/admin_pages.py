"""Admin pages: same REST JSON as webPage/js/app.js, via UART PCAPI (device loopback HTTP)."""

from __future__ import annotations

import json
import sys
import threading
import time
import tkinter as tk
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from tkinter import messagebox, scrolledtext, ttk
from typing import Any, Callable, Dict, List, Optional, TextIO, Tuple

from .device_serial import SerialApiClient, SerialApiError


def _app_base_dir() -> Path:
    """与稳定性测试自动 log 一致：可执行目录或源码上级（含 logs/）。"""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent.parent


def _local_network_config_path() -> Path:
    return _app_base_dir() / "logs" / "network_config_local.json"


# Paths match webPage/js/app.js API
P_DASH = "/api/dashboard/overview"
P_USERS = "/api/users/online"
P_NET = "/api/network/config"
P_APN = "/api/network/apn"
P_WIFI_AP = "/api/wifi/ap"
P_WIFI_STA = "/api/wifi"
P_WIFI_CLEAR = "/api/wifi/clear"
P_WIFI_SCAN = "/api/wifi/scan"
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


def _stab_resolve_current_mode_row(mode_json: dict) -> Optional[dict]:
    """从 /api/mode 响应中解析当前生效的模式条目（modes 中 id 与 current 一致）。"""
    cur: Optional[int] = None
    rs = mode_json.get("runtime_status")
    if isinstance(rs, dict) and rs.get("current_mode") is not None:
        cur = int(rs["current_mode"])
    if cur is None and mode_json.get("current") is not None:
        cur = int(mode_json["current"])
    if cur is None:
        return None
    modes = mode_json.get("modes") or []
    for m in modes:
        if int(m.get("id", -1)) == cur:
            return m
    return None


def stab_mode_lan_flags(mode_json: dict) -> Tuple[Optional[bool], Optional[bool]]:
    """
    当前工作模式是否包含以太网 LAN / SoftAP（Wi‑Fi）LAN。
    返回 (lan_eth, lan_softap)；无法解析时为 (None, None)（UI 可视为「先显示全部」）。
    """
    row = _stab_resolve_current_mode_row(mode_json)
    if not row:
        return None, None
    return bool(row.get("lan_eth")), bool(row.get("lan_softap"))


def format_stability_mode_hint(mode_json: dict) -> str:
    """
    根据 /api/mode 响应，提示当前固件工作模式与（1）以太网 /（2）Wi‑Fi 稳定性测试的对应关系。
    适用于 W5500↔Wi‑Fi 各类组合：由 modes[].lan_eth / lan_softap 判断 PC 应接网线还是连热点。
    """
    cur: Optional[int] = None
    rs = mode_json.get("runtime_status")
    if isinstance(rs, dict) and rs.get("current_mode") is not None:
        cur = int(rs["current_mode"])
    if cur is None and mode_json.get("current") is not None:
        cur = int(mode_json["current"])
    if cur is None:
        return "设备模式：响应中无 current_mode / current"
    row = _stab_resolve_current_mode_row(mode_json)
    if not row:
        return f"设备模式：id={cur}（modes 中未找到该 id）"
    label = str(row.get("label") or f"模式 {cur}")
    le = bool(row.get("lan_eth"))
    ls = bool(row.get("lan_softap"))
    wt = row.get("wan_type")
    try:
        wt_s = WAN_TYPE_LABEL.get(int(wt), f"WAN类型={wt}") if wt is not None else ""
    except (TypeError, ValueError):
        wt_s = ""
    head = f"当前：{label}"
    if wt_s:
        head += f"  ·  上行：{wt_s}"
    head += f"  ·  LAN：有线={'开' if le else '关'}；SoftAP={'开' if ls else '关'}。"
    if le and ls:
        tail = (
            "兼容性：（1）PC 网线接设备 ETH_LAN 做有线测试；（2）PC 无线连设备 SoftAP 做 Wi‑Fi 测试。"
            "二者勿同时运行；取证 LAN 网关常见 192.168.4.1 / 热点侧 192.168.5.1。"
        )
    elif le and not ls:
        tail = "兼容性：请使用（1）以太网测试；本模式无 SoftAP LAN，一般不要用（2）。"
    elif ls and not le:
        tail = "兼容性：请使用（2）Wi‑Fi 测试连 SoftAP；本模式无 ETH LAN，不要用（1）。"
    else:
        tail = "兼容性：请按实际接线在（1）（2）中择一；若均无，固件可能未暴露 LAN 口。"
    return head + " " + tail


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
    get_client: Callable[[], SerialApiClient]
    log: Callable[[str], None]
    set_title: Callable[[str], None]
    send_raw_line: Callable[[str], None]
    is_serial_open: Callable[[], bool]


def _run_bg(root: tk.Misc, work: Callable[[], Any], ok: Callable[[Any], None], err: Callable[[BaseException], None]) -> None:
    def wrap() -> None:
        try:
            r = work()
        except BaseException as e:
            # 3.11+ 在 except 结束后会清除 `e`；复制到另一局部变量再调度，避免 lambda 晚绑定的 NameError
            captured_exc: BaseException = e
            root.after(0, lambda: err(captured_exc))
            return
        res_ok: Any = r
        root.after(0, lambda: ok(res_ok))

    threading.Thread(target=wrap, daemon=True).start()


def _bind_tooltip(widget: tk.Misc, text: str) -> None:
    """鼠标悬停时显示简短说明（用于 A/B/C/D 等控件）。"""
    tip: Dict[str, Optional[tk.Toplevel]] = {"w": None}

    def on_enter(_e: tk.Event) -> None:
        if tip["w"] is not None:
            return
        x = widget.winfo_rootx() + 8
        y = widget.winfo_rooty() + widget.winfo_height() + 2
        tw = tk.Toplevel(widget)
        tw.wm_overrideredirect(True)
        try:
            tw.wm_attributes("-topmost", True)
        except tk.TclError:
            pass
        tw.wm_geometry(f"+{x}+{y}")
        lb = tk.Label(
            tw,
            text=text,
            justify=tk.LEFT,
            background="#ffffe0",
            foreground="#000000",
            relief=tk.SOLID,
            borderwidth=1,
            padx=8,
            pady=6,
            font=("TkDefaultFont", 9),
            wraplength=380,
        )
        lb.pack()
        tip["w"] = tw

    def on_leave(_e: tk.Event) -> None:
        if tip["w"] is not None:
            tip["w"].destroy()
            tip["w"] = None

    widget.bind("<Enter>", on_enter)
    widget.bind("<Leave>", on_leave)


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
        self._build_stability()
        self._build_at_test()
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
            "stability": "稳定性测试",
            "at_test": "AT 指令测试",
            "reboot": "重启",
        }
        self.ctx.set_title(titles.get(page_id, page_id))
        if page_id == "network":
            fn = getattr(self, "_network_on_show", None)
            if callable(fn):
                fn()
        if page_id == "stability":
            fn = getattr(self, "_stability_on_show", None)
            if callable(fn):
                fn()

    # --- helpers ---
    def _client(self) -> SerialApiClient:
        return self.ctx.get_client()

    def _log(self, msg: str) -> None:
        self.ctx.log(msg)

    def _sync_softap_to_stability(self, ssid: str, password: str) -> None:
        """将网络配置里 WiFi 热点（SoftAP）的 SSID/密码写入稳定性测试的 Wi‑Fi 区块。"""
        v_ssid = getattr(self, "_stab_wf_ssid", None)
        v_pwd = getattr(self, "_stab_wf_pwd", None)
        if v_ssid is None or v_pwd is None:
            return
        s = (ssid or "").strip()
        if not s:
            return
        v_ssid.set(s)
        v_pwd.set(password or "")
        self._log("已同步 SoftAP SSID/密码 → 稳定性测试 Wi‑Fi")

    def _persist_network_config_local(self, data: dict) -> None:
        """将网络配置表单快照写入本机 logs/network_config_local.json（与自动 log 同目录策略）。"""
        try:
            p = _local_network_config_path()
            p.parent.mkdir(parents=True, exist_ok=True)
            with open(p, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except OSError:
            pass

    def _softap_password_from_local_cache(self, ssid: str) -> str:
        """本机副本中与 SSID 匹配的 SoftAP 密码（设备 API 常不返回已保存密码）。"""
        s = (ssid or "").strip()
        if not s:
            return ""
        try:
            p = _local_network_config_path()
            if not p.is_file():
                return ""
            with open(p, "r", encoding="utf-8") as f:
                d = json.load(f)
            sp = d.get("softap") if isinstance(d, dict) else None
            if not isinstance(sp, dict):
                return ""
            if str(sp.get("ssid") or "").strip() != s:
                return ""
            return str(sp.get("password") or "")
        except (OSError, json.JSONDecodeError, TypeError):
            return ""

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
        cell_labels: Dict[str, tk.StringVar] = {
            k: tk.StringVar(value="—")
            for k in ("op", "nm", "act", "imsi", "imei", "iccid", "sig", "rssi_ber", "maker", "module", "fw", "ppp", "usb")
        }

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
            ("网络制式ACT", "act"),
            ("IMSI", "imsi"),
            ("IMEI", "imei"),
            ("ICCID", "iccid"),
            ("网络信号", "sig"),
            ("RSSI/BER", "rssi_ber"),
            ("模块厂商", "maker"),
            ("模块型号", "module"),
            ("模块固件", "fw"),
            ("PPP已拨号", "ppp"),
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
            sys_labels["model"].set(str(sys.get("model") or "4G_NIC"))
            sys_labels["ver"].set(str(sys.get("firmware_version") or "—"))
            sys_labels["time"].set(str(sys.get("system_time") or "—"))
            mp = sys.get("memory_percent")
            sys_labels["mem"].set(str(mp) + "%" if mp is not None else "—")
            sys_labels["uptime"].set(fmt_dur(sys.get("uptime_s")))
            ou = sys.get("online_users")
            sys_labels["users"].set(str(ou) if ou is not None else "—")
            cel = d.get("cellular") or {}
            if not cel.get("usb_lte_ready"):
                cell_kv.pack_forget()
            else:
                # 重新显示在接口状态之前
                cell_kv.pack(fill=tk.X, pady=(8, 0), before=if_kv)
                
            cell_labels["op"].set(str(cel.get("operator") or "—"))
            cell_labels["nm"].set(str(cel.get("network_mode") or "—"))
            act = cel.get("network_act")
            cell_labels["act"].set(str(act) if act is not None else "—")
            cell_labels["imsi"].set(str(cel.get("imsi") or "—"))
            cell_labels["imei"].set(str(cel.get("imei") or "—"))
            cell_labels["iccid"].set(str(cel.get("iccid") or "—"))
            cell_labels["sig"].set(str(cel.get("signal") or "—"))
            rssi = cel.get("signal_rssi")
            ber = cel.get("signal_ber")
            cell_labels["rssi_ber"].set(f'{rssi if rssi is not None else "—"} / {ber if ber is not None else "—"}')
            cell_labels["maker"].set(str(cel.get("manufacturer") or "—"))
            cell_labels["module"].set(str(cel.get("module_name") or "—"))
            cell_labels["fw"].set(str(cel.get("fw_version") or "—"))
            cell_labels["ppp"].set("是" if cel.get("ppp_has_ip") else "否")
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
                self._log("总览已刷新")

            def er(e: BaseException) -> None:
                messagebox.showerror("总览", str(e))
                self._log("总览失败: " + str(e))

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
                self._log("用户列表已刷新")

            def er(e: BaseException) -> None:
                messagebox.showerror("用户列表", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(tbar, text="刷新", command=refresh).pack(side=tk.LEFT)

    # --- network (panel visibility / load / save order aligned with webPage/js/app.js) ---
    def _build_network(self) -> None:
        from .stability_pc import IS_WINDOWS, list_net_adapter_names, wlan_validate_sta_before_device_save

        fr = ttk.LabelFrame(self._container, text="网络配置", padding=8)
        self.pages["network"] = fr
        outer = ttk.Frame(fr)
        outer.pack(fill=tk.BOTH, expand=True)
        canv = tk.Canvas(outer, highlightthickness=0)
        sb = ttk.Scrollbar(outer, orient=tk.VERTICAL, command=canv.yview)
        inner = ttk.Frame(canv)
        # Canvas 内留白：避免「工作模式 / 无线 / 有线」等框与右侧滚动条、分割条贴死
        _net_pad_l, _net_pad_r = 4, 22
        inner_win = canv.create_window((_net_pad_l, 0), window=inner, anchor=tk.NW)

        def _inner_width_for_canvas(w: int) -> int:
            return max(120, w - _net_pad_l - _net_pad_r)

        def _conf(_e: Any) -> None:
            canv.configure(scrollregion=canv.bbox("all"))
            canv.itemconfigure(inner_win, width=_inner_width_for_canvas(canv.winfo_width()))

        inner.bind("<Configure>", _conf)
        canv.bind(
            "<Configure>",
            lambda e: canv.itemconfigure(inner_win, width=_inner_width_for_canvas(e.width)),
        )
        canv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        canv.configure(yscrollcommand=sb.set)

        mode_top = ttk.LabelFrame(inner, text="工作模式（WAN + LAN）", padding=8)
        mode_top.pack(fill=tk.X, pady=(0, 4))
        hw_hint = tk.StringVar(value="")
        ttk.Label(mode_top, textvariable=hw_hint, foreground="gray", wraplength=520).pack(anchor=tk.W)
        row1 = ttk.Frame(mode_top)
        row1.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(row1, text="WAN 上行（外网从哪走）").pack(side=tk.LEFT)
        wan_combo = ttk.Combobox(row1, width=34, state="readonly")
        wan_combo.pack(side=tk.LEFT, padx=8)
        row2 = ttk.Frame(mode_top)
        row2.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(row2, text="LAN 组合（内网）").pack(side=tk.LEFT)
        mode_combo = ttk.Combobox(row2, width=34, state="readonly")
        mode_combo.pack(side=tk.LEFT, padx=8)
        ttk.Label(
            mode_top,
            text="点击「刷新」从设备读取当前 WAN 上行与 LAN 组合；切换上方两个下拉框后，下方有线外网 / 无线配置 / 有线网络会联动显示（与网页一致）。",
            foreground="gray",
            wraplength=560,
        ).pack(anchor=tk.W, pady=(8, 0))

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

        # --- ETH WAN（顺序与 web 一致：先于无线组合） ---
        ew_card = ttk.LabelFrame(inner, text="有线外网（ETH_WAN）", padding=8)
        ew_dhcp = tk.BooleanVar(value=True)
        ew_ip, ew_mask, ew_gw = tk.StringVar(), tk.StringVar(), tk.StringVar()
        ew_d1, ew_d2 = tk.StringVar(), tk.StringVar()
        ttk.Label(
            ew_card,
            text="当前模式为「有线网络连上级路由（WAN）」时配置本口；可与网页相同单独保存应用。",
            foreground="gray",
            wraplength=520,
        ).grid(row=0, column=0, columnspan=2, sticky=tk.W)
        ttk.Checkbutton(ew_card, text="DHCP 自动获取", variable=ew_dhcp).grid(row=1, column=0, columnspan=2, sticky=tk.W, pady=(8, 2))
        ttk.Label(ew_card, text="IP").grid(row=2, column=0, sticky=tk.W, pady=2)
        ttk.Entry(ew_card, textvariable=ew_ip, width=22).grid(row=2, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="掩码").grid(row=3, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_mask, width=22).grid(row=3, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="网关").grid(row=4, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_gw, width=22).grid(row=4, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="DNS1").grid(row=5, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_d1, width=22).grid(row=5, column=1, sticky=tk.W, padx=6)
        ttk.Label(ew_card, text="DNS2").grid(row=6, column=0, sticky=tk.W)
        ttk.Entry(ew_card, textvariable=ew_d2, width=22).grid(row=6, column=1, sticky=tk.W, padx=6)

        # --- 无线配置（STA + SoftAP 同卡，按模式显隐） ---
        wireless_combo = ttk.LabelFrame(inner, text="无线配置", padding=8)
        sta_inner = ttk.Frame(wireless_combo)
        sta_divider = ttk.Separator(wireless_combo, orient=tk.HORIZONTAL)
        ap_inner = ttk.Frame(wireless_combo)
        sta_ssid = tk.StringVar()
        sta_pwd = tk.StringVar()
        ttk.Label(sta_inner, text="WiFi STA（连接上级路由），SSID").grid(row=0, column=0, sticky=tk.W)
        sta_row = ttk.Frame(sta_inner)
        sta_row.grid(row=1, column=0, columnspan=2, sticky=tk.W, pady=(4, 0))
        ttk.Entry(sta_row, textvariable=sta_ssid, width=26).pack(side=tk.LEFT)

        def scan_sta() -> None:
            def work() -> dict:
                return self._client().get(P_WIFI_SCAN)

            def ok(d: dict) -> None:
                aps = d.get("aps") if isinstance(d, dict) else []
                if not isinstance(aps, list):
                    aps = []
                pop = tk.Toplevel(self.ctx.root)
                pop.title("选择 SSID")
                pop.transient(self.ctx.root)
                lb = tk.Listbox(pop, width=40, height=min(16, max(4, len(aps) + 1)))
                sb2 = ttk.Scrollbar(pop, orient=tk.VERTICAL, command=lb.yview)
                lb.configure(yscrollcommand=sb2.set)
                lb.grid(row=0, column=0, sticky="nsew")
                sb2.grid(row=0, column=1, sticky="ns")
                pop.grid_rowconfigure(0, weight=1)
                pop.grid_columnconfigure(0, weight=1)
                for it in aps:
                    if not isinstance(it, dict):
                        continue
                    ss = str(it.get("ssid") or "")
                    rs = it.get("rssi")
                    lb.insert(tk.END, f"{ss} ({rs})" if rs is not None else ss)

                def pick(_: Any = None) -> None:
                    sel = lb.curselection()
                    if not sel:
                        return
                    line = lb.get(sel[0])
                    if "(" in line:
                        line = line.rsplit("(", 1)[0].strip()
                    sta_ssid.set(line)
                    pop.destroy()

                lb.bind("<Double-Button-1>", pick)
                bf = ttk.Frame(pop, padding=6)
                bf.grid(row=1, column=0, columnspan=2, sticky=tk.EW)
                ttk.Button(bf, text="使用所选", command=pick).pack(side=tk.LEFT)
                ttk.Button(bf, text="关闭", command=pop.destroy).pack(side=tk.RIGHT)

            def er(e: BaseException) -> None:
                messagebox.showerror("扫描", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(sta_row, text="扫描", command=scan_sta, width=8).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Label(sta_inner, text="密码").grid(row=2, column=0, sticky=tk.W, pady=(6, 0))
        ttk.Entry(sta_inner, textvariable=sta_pwd, width=30, show="*").grid(row=3, column=0, sticky=tk.W, pady=(2, 0))
        sta_btns = ttk.Frame(sta_inner)
        sta_btns.grid(row=4, column=0, sticky=tk.W, pady=(8, 0))

        def load_sta() -> None:
            def work() -> dict:
                return self._client().get(P_WIFI_STA)

            def ok(d: dict) -> None:
                sta_ssid.set(str(d.get("ssid") or ""))
                sta_pwd.set(str(d.get("password") or ""))
                self._log("已读取 STA")

            def er(e: BaseException) -> None:
                messagebox.showerror("STA", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_sta() -> None:
            if not sta_ssid.get().strip():
                messagebox.showwarning("STA", "请填写 SSID", parent=self.ctx.root)
                return
            ssid = sta_ssid.get().strip()
            pwd = sta_pwd.get()

            if not IS_WINDOWS:
                if not messagebox.askyesno(
                    "STA",
                    "非 Windows 无法在本机连接该 Wi‑Fi 做校验。\n是否仍直接保存到设备？",
                    parent=self.ctx.root,
                ):
                    return

                def work_direct() -> None:
                    self._client().post_json(P_WIFI_STA, {"ssid": ssid, "password": pwd})

                def ok_direct(_: Any) -> None:
                    messagebox.showinfo("STA", "已保存到设备", parent=self.ctx.root)
                    self._log("STA 已保存（未做本机 Wi‑Fi 校验）")

                def er_direct(e: BaseException) -> None:
                    messagebox.showerror("STA", str(e), parent=self.ctx.root)

                _run_bg(self.ctx.root, work_direct, ok_direct, er_direct)
                return

            wifi_names = list_net_adapter_names("wifi")
            if not wifi_names:
                messagebox.showerror(
                    "STA",
                    "未检测到无线网卡，无法在本机连接测试。\n请启用 WLAN 后再试。",
                    parent=self.ctx.root,
                )
                return
            iface = wifi_names[0]

            def work() -> None:
                ok_pc, detail = wlan_validate_sta_before_device_save(ssid, pwd, iface)
                if not ok_pc:
                    raise ValueError(detail)
                self._client().post_json(P_WIFI_STA, {"ssid": ssid, "password": pwd})

            def ok_save(_: Any) -> None:
                messagebox.showinfo(
                    "STA",
                    "本机已成功连接该 Wi‑Fi 并拿到地址，已写入设备。",
                    parent=self.ctx.root,
                )
                self._log(f"STA 已保存（本机网卡 {iface} 校验通过）")

            def er_save(e: BaseException) -> None:
                if isinstance(e, ValueError):
                    messagebox.showerror(
                        "STA",
                        "本机 Wi‑Fi 校验失败，未写入设备。\n\n" + str(e)[:2000],
                        parent=self.ctx.root,
                    )
                else:
                    messagebox.showerror("STA", str(e), parent=self.ctx.root)

            _run_bg(self.ctx.root, work, ok_save, er_save)

        def clear_sta() -> None:
            def work() -> None:
                self._client().post_json(P_WIFI_CLEAR, {})

            def ok(_: Any) -> None:
                sta_ssid.set("")
                sta_pwd.set("")
                self._log("STA 已清空")

            def er(e: BaseException) -> None:
                messagebox.showerror("STA", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(sta_btns, text="保存 STA", command=save_sta).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(sta_btns, text="读取", command=load_sta).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(sta_btns, text="清空 STA", command=clear_sta).pack(side=tk.LEFT)

        ap_ssid = tk.StringVar()
        ap_enc = tk.StringVar(value="WPA2-PSK")
        ap_pwd = tk.StringVar()
        ap_hid = tk.BooleanVar(value=False)
        ttk.Label(ap_inner, text="WiFi 热点（SoftAP）").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(ap_inner, textvariable=ap_ssid, width=32).grid(row=1, column=0, sticky=tk.W, pady=(4, 0))
        ttk.Label(ap_inner, text="加密方式").grid(row=2, column=0, sticky=tk.W, pady=(6, 0))
        ttk.Combobox(ap_inner, textvariable=ap_enc, values=("WPA2-PSK", "WPA-PSK", "OPEN"), width=14, state="readonly").grid(
            row=3, column=0, sticky=tk.W
        )
        ttk.Label(ap_inner, text="WiFi 密码（页面不显示已保存；改加密时请重填）").grid(row=4, column=0, sticky=tk.W, pady=(6, 0))
        ttk.Entry(ap_inner, textvariable=ap_pwd, width=32, show="*").grid(row=5, column=0, sticky=tk.W)
        ttk.Checkbutton(ap_inner, text="隐藏 WiFi", variable=ap_hid).grid(row=6, column=0, sticky=tk.W, pady=(6, 0))

        # --- 有线网络 LAN ---
        lan_card = ttk.LabelFrame(inner, text="有线网络", padding=8)
        lan_ip, lan_mask = tk.StringVar(), tk.StringVar()
        lan_dhcp = tk.BooleanVar(value=True)
        lan_s, lan_e = tk.StringVar(), tk.StringVar()
        lan_d1, lan_d2 = tk.StringVar(), tk.StringVar()
        r = 0
        ttk.Label(lan_card, text="IP 地址").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_ip, width=22).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="子网掩码").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_mask, width=22).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Checkbutton(lan_card, text="DHCP", variable=lan_dhcp).grid(row=r, column=0, columnspan=2, sticky=tk.W, pady=4)
        r += 1
        ttk.Label(lan_card, text="起始 IP").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_s, width=22).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="结束 IP").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_e, width=22).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="DNS1").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_d1, width=22).grid(row=r, column=1, padx=6)
        r += 1
        ttk.Label(lan_card, text="DNS2").grid(row=r, column=0, sticky=tk.W)
        ttk.Entry(lan_card, textvariable=lan_d2, width=22).grid(row=r, column=1, padx=6)

        def selected_mode_id() -> Optional[int]:
            s = mode_combo.get().strip()
            if not s or s.startswith("—"):
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

        def toggle_network_panels(_: Any = None) -> None:
            for w in (ew_card, wireless_combo, lan_card):
                w.pack_forget()
            sta_inner.pack_forget()
            sta_divider.pack_forget()
            ap_inner.pack_forget()

            mid = selected_mode_id()
            row = mode_row_by_id(mid) if mid is not None else None
            show_eth = row.get("lan_eth") is True if row else True
            show_sta = row.get("needs_sta") is True if row else False
            show_wifi = row.get("lan_softap") is True if row else False
            show_eth_wan = row.get("needs_eth_wan") is True if row else False

            if show_eth_wan:
                ew_card.pack(fill=tk.X, pady=(10, 0))
            if show_sta or show_wifi:
                wireless_combo.pack(fill=tk.X, pady=(10, 0))
                if show_sta:
                    sta_inner.pack(fill=tk.X, anchor=tk.W)
                if show_sta and show_wifi:
                    sta_divider.pack(fill=tk.X, pady=8)
                if show_wifi:
                    ap_inner.pack(fill=tk.X, anchor=tk.W)
            if show_eth:
                lan_card.pack(fill=tk.X, pady=(10, 0))

        def refill_mode_combo() -> None:
            md = self._mode_payload
            if not md or not isinstance(md.get("modes"), list):
                mode_combo["values"] = ()
                return
            wv = wan_combo.get().strip()
            if not wv or "无可用" in wv:
                mode_combo["values"] = ()
                return
            try:
                wt = int(wv.split(":")[0])
            except (ValueError, IndexError):
                mode_combo["values"] = ()
                return
            lst = modes_for_wan(md["modes"], wt)
            if not lst:
                mode_combo["values"] = ("—",)
                return
            mode_combo["values"] = tuple(f'{int(m["id"])}: {lan_summary_label(m)}' for m in lst)

        skip_wan_combo_event = {"v": False}

        def on_wan_change(_: Any = None) -> None:
            if skip_wan_combo_event["v"]:
                return
            refill_mode_combo()
            vals = mode_combo["values"]
            if vals and (len(vals) != 1 or vals[0] != "—"):
                mode_combo.current(0)
            toggle_network_panels()

        def load_network() -> None:
            def work() -> tuple:
                c = self._client()
                net = c.get(P_NET)
                mode = c.get(P_MODE)
                ap = {}
                ew = {}
                try:
                    ap = c.get(P_WIFI_AP)
                except SerialApiError:
                    pass
                try:
                    ew = c.get(P_ETH_WAN)
                except SerialApiError:
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
                if not wts:
                    wan_combo["values"] = ("— 无可用模式 —",)
                    wan_combo.set("— 无可用模式 —")
                    mode_combo["values"] = ()
                else:
                    wan_combo["values"] = tuple(wan_labels)
                    skip_wan_combo_event["v"] = True
                    try:
                        if wan_t is not None:
                            idx = next((i for i, x in enumerate(wts) if x == wan_t), 0)
                            wan_combo.current(idx)
                        refill_mode_combo()
                        sub = modes_for_wan(modes, wan_t) if wan_t is not None else []
                        still = cur_id is not None and any(int(m["id"]) == cur_id for m in sub)
                        pick = cur_id if still else (int(sub[0]["id"]) if sub else None)
                        if pick is not None and mode_combo["values"]:
                            for i, lbl in enumerate(mode_combo["values"]):
                                if lbl.startswith(str(pick) + ":"):
                                    mode_combo.current(i)
                                    break
                        elif mode_combo["values"] and (len(mode_combo["values"]) != 1 or mode_combo["values"][0] != "—"):
                            mode_combo.current(0)
                    finally:
                        skip_wan_combo_event["v"] = False
                toggle_network_panels()

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

                if cur_mode and cur_mode.get("lan_softap") and isinstance(ap, dict) and (str(ap.get("ssid") or "").strip()):
                    self._sync_softap_to_stability(ap_ssid.get().strip(), ap_pwd.get())

                self._persist_network_config_local(_network_snapshot_dict())
                self._log("网络配置已读取（本机副本已更新）")

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
                except SerialApiError:
                    pass

            def ok(_: Any) -> None:
                messagebox.showinfo("ETH WAN", "有线 WAN 已写入，并已按当前工作模式重试应用")
                self._log("有线 WAN 已写")

            def er(e: BaseException) -> None:
                messagebox.showerror("ETH WAN", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        ttk.Button(ew_card, text="仅保存有线 WAN 参数并应用", command=save_eth_wan_only).grid(
            row=7, column=0, columnspan=2, sticky=tk.W, pady=(12, 0)
        )

        def save_network() -> None:
            mid = selected_mode_id()
            if mid is None:
                messagebox.showwarning("网络", "请选择 LAN/工作模式")
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
                wv = wan_combo.get().strip()
                if not wv or "无可用" in wv:
                    raise SerialApiError("无可用 WAN 类型")
                try:
                    wt = int(wv.split(":")[0])
                except ValueError as exc:
                    raise SerialApiError("WAN 选择无效") from exc
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
                self._log("/api/network/config 已 POST")
                self._persist_network_config_local(_network_snapshot_dict())
                self._log(f"本机网络配置副本：{_local_network_config_path().name}")
                if row_pre and row_pre.get("lan_softap"):
                    self._sync_softap_to_stability(ap_ssid.get().strip(), ap_pwd.get())

            def er(e: BaseException) -> None:
                messagebox.showerror("网络", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        btnf = ttk.Frame(inner)

        def on_mode_combo_change(_: Any = None) -> None:
            """与网页 selWorkMode 一致：仅按当前 work_mode 行切换面板，不改 WAN 子列表。"""
            toggle_network_panels()

        wan_combo.bind("<<ComboboxSelected>>", on_wan_change)
        mode_combo.bind("<<ComboboxSelected>>", on_mode_combo_change)
        self._network_on_show = load_network

        def _network_snapshot_dict() -> dict:
            return {
                "saved_at": datetime.now().isoformat(timespec="seconds"),
                "wan_combo": wan_combo.get(),
                "mode_combo": mode_combo.get(),
                "work_mode_id": selected_mode_id(),
                "eth_wan": {
                    "dhcp": ew_dhcp.get(),
                    "ip": ew_ip.get().strip(),
                    "mask": ew_mask.get().strip(),
                    "gw": ew_gw.get().strip(),
                    "dns1": ew_d1.get().strip(),
                    "dns2": ew_d2.get().strip(),
                },
                "sta": {
                    "ssid": sta_ssid.get().strip(),
                    "password": sta_pwd.get(),
                },
                "softap": {
                    "ssid": ap_ssid.get().strip(),
                    "encryption_mode": ap_enc.get(),
                    "password": ap_pwd.get(),
                    "hidden_ssid": ap_hid.get(),
                },
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

        toggle_network_panels()
        btnf.pack(fill=tk.X, pady=(24, 8))
        ttk.Button(btnf, text="刷新", command=load_network).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(btnf, text="保存", width=12, command=save_network).pack(side=tk.LEFT)

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
                self._log("APN 已读取")

            def er(e: BaseException) -> None:
                messagebox.showerror("APN", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        def save_apn() -> None:
            def work() -> None:
                self._client().post_json(P_APN, {"apn": a.get().strip(), "username": u.get().strip(), "password": p.get()})

            def ok(_: Any) -> None:
                messagebox.showinfo("APN", "已保存")
                self._log("APN 已保存")

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
                self._log("管理密码已更新")

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
                self._log("系统时间已读取")

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
                self._log("时区已保存")

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
                self._log("factory_reset")

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
                self._log("已生成备份 JSON")

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
                self._log("配置已导入")

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
                self._log("系统日志已刷新")

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
                self._log("探测地址已读取")

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
                self._log("probes 已保存")

            def er(e: BaseException) -> None:
                messagebox.showerror("探测", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.grid(row=5, column=0, columnspan=2, sticky=tk.W, pady=12)
        ttk.Button(bf, text="读取", command=load_p).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="保存", command=save_p).pack(side=tk.LEFT)

    def _build_stability(self) -> None:
        from .stability_pc import (
            CycleConfig,
            IS_WINDOWS,
            list_net_adapter_names,
            run_eth_loop,
            run_wifi_loop,
            stability_test_power_session_enter,
            stability_test_power_session_leave,
        )

        fr = ttk.Frame(self._container)
        self.pages["stability"] = fr
        self._stability_on_show = lambda: None
        fr.grid_columnconfigure(0, weight=1)

        if not IS_WINDOWS:
            ttk.Label(fr, text="当前非 Windows 系统，此功能不可用。", foreground="red").grid(
                row=0, column=0, sticky=tk.W
            )
            return

        result_lf = ttk.LabelFrame(fr, text="测试结果", padding=2)
        out = scrolledtext.ScrolledText(result_lf, height=5, wrap=tk.WORD, font=("Consolas", 9))

        stab_auto = tk.BooleanVar(value=False)
        stab_path_var = tk.StringVar(value="")
        stab_log_state: dict[str, Optional[TextIO]] = {"fp": None}

        def _stab_app_base_dir() -> Path:
            if getattr(sys, "frozen", False):
                return Path(sys.executable).resolve().parent
            return Path(__file__).resolve().parent.parent

        def _stab_logs_dir() -> Path:
            return _stab_app_base_dir() / "logs"

        def _close_stab_log_file() -> None:
            fp: Optional[TextIO] = stab_log_state["fp"]
            if fp is None:
                return
            try:
                fp.write(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} 测试结果自动记录结束\n\n")
                fp.flush()
                fp.close()
            except OSError:
                pass
            stab_log_state["fp"] = None

        def on_stab_auto_log_toggle() -> None:
            if stab_auto.get():
                try:
                    d = _stab_logs_dir()
                    d.mkdir(parents=True, exist_ok=True)
                    path = d / f"stability_{datetime.now().strftime('%Y-%m-%d')}.log"
                    _close_stab_log_file()
                    fp = open(path, "a", encoding="utf-8")
                    stab_log_state["fp"] = fp
                    fp.write(f"\n{'='*60}\n")
                    fp.write(
                        f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} 测试结果自动记录开始  {path}\n"
                    )
                    fp.write(f"{'='*60}\n")
                    fp.flush()
                    stab_path_var.set(f"→ {path}")
                except OSError as e:
                    stab_auto.set(False)
                    stab_path_var.set("")
                    messagebox.showwarning("自动log", f"无法写入日志文件：{e}", parent=self.ctx.root)
            else:
                _close_stab_log_file()
                stab_path_var.set("")

        def append_line(s: str) -> None:
            ts = time.strftime("%H:%M:%S")
            out.insert(tk.END, f"[{ts}] {s}\n")
            out.see(tk.END)
            if stab_auto.get() and stab_log_state["fp"] is not None:
                try:
                    fts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    stab_log_state["fp"].write(f"[{fts}] {s}\n")
                    stab_log_state["fp"].flush()
                except OSError:
                    stab_auto.set(False)
                    _close_stab_log_file()
                    stab_path_var.set("")
                    messagebox.showwarning(
                        "自动log", "写入失败，已关闭自动记录。", parent=self.ctx.root
                    )

        def clear_test_log() -> None:
            out.delete("1.0", tk.END)

        stab_mode_var = tk.StringVar(
            value="设备模式：连接串口后进入本页将自动读取；亦可点「刷新模式」。"
        )

        result_tb = ttk.Frame(result_lf)
        result_tb.pack(fill=tk.X, anchor=tk.W, pady=(0, 2))
        ttk.Button(result_tb, text="清除", command=clear_test_log).pack(side=tk.LEFT)
        ttk.Checkbutton(
            result_tb,
            text="自动log",
            variable=stab_auto,
            command=on_stab_auto_log_toggle,
        ).pack(side=tk.LEFT, padx=(16, 0))
        ttk.Label(
            result_tb,
            textvariable=stab_path_var,
            foreground="gray",
            font=("Segoe UI", 8),
            wraplength=360,
        ).pack(side=tk.LEFT, padx=(6, 0), fill=tk.X, expand=True)
        out.pack(fill=tk.BOTH, expand=True)

        eth_stop = threading.Event()
        wifi_stop = threading.Event()
        eth_thr: Dict[str, Optional[threading.Thread]] = {"t": None}
        wifi_thr: Dict[str, Optional[threading.Thread]] = {"t": None}

        eth_ad = tk.StringVar()
        eth_a = tk.StringVar(value="20")
        eth_b = tk.StringVar(value="8")
        eth_c = tk.StringVar(value="2")
        eth_d = tk.StringVar(value="20")
        eth_l = tk.StringVar(value="")

        def _eth_update_l(*_: object) -> None:
            try:
                a = float(eth_a.get().strip() or "0")
                b = float(eth_b.get().strip() or "0")
                c = float(eth_c.get().strip() or "0")
                d = float(eth_d.get().strip() or "0")
                eth_l.set(f"L=A+B+C+D={a + b + c + d:g}s")
            except ValueError:
                eth_l.set("L=A+B+C+D=（数值无效）")

        for _ev in (eth_a, eth_b, eth_c, eth_d):
            _ev.trace_add("write", _eth_update_l)
        _eth_update_l()

        eth_test = tk.StringVar(value="ping")
        eth_ping = tk.StringVar(value="8.8.8.8")

        wf_ad = tk.StringVar()
        wf_ssid = tk.StringVar()
        wf_pwd = tk.StringVar()
        wf_a = tk.StringVar(value="20")
        wf_b = tk.StringVar(value="8")
        wf_c = tk.StringVar(value="2")
        wf_d = tk.StringVar(value="20")
        wf_l = tk.StringVar(value="")

        def _wf_update_l(*_: object) -> None:
            try:
                a = float(wf_a.get().strip() or "0")
                b = float(wf_b.get().strip() or "0")
                c = float(wf_c.get().strip() or "0")
                d = float(wf_d.get().strip() or "0")
                wf_l.set(f"L=A+B+C+D={a + b + c + d:g}s")
            except ValueError:
                wf_l.set("L=A+B+C+D=（数值无效）")

        for _wv in (wf_a, wf_b, wf_c, wf_d):
            _wv.trace_add("write", _wf_update_l)
        _wf_update_l()

        wf_test = tk.StringVar(value="ping")
        wf_ping = tk.StringVar(value="8.8.8.8")

        def refresh_adapters() -> None:
            def work() -> Tuple[List[str], List[str]]:
                return list_net_adapter_names("ethernet"), list_net_adapter_names("wifi")

            def ok(pair: Any) -> None:
                eth_names, wifi_names = pair
                eth_combo["values"] = eth_names
                wf_combo["values"] = wifi_names
                if eth_names and not eth_ad.get().strip():
                    eth_ad.set(eth_names[0])
                if wifi_names and not wf_ad.get().strip():
                    wf_ad.set(wifi_names[0])
                append_line(f"适配器列表已刷新：有线 {len(eth_names)} 个，无线 {len(wifi_names)} 个")

            def er(e: BaseException) -> None:
                append_line(f"刷新适配器失败: {e!r}")

            _run_bg(self.ctx.root, work, ok, er)

        mode_lf = ttk.LabelFrame(fr, text="与设备工作模式（W5500 / Wi‑Fi）", padding=4)
        mode_lf.grid(row=0, column=0, sticky=tk.EW, pady=(0, 4))
        mode_row = ttk.Frame(mode_lf)
        mode_row.pack(fill=tk.X)
        ttk.Label(
            mode_row,
            textvariable=stab_mode_var,
            foreground="gray",
            wraplength=760,
            justify=tk.LEFT,
            font=("Segoe UI", 9),
        ).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(mode_row, text="刷新模式", command=lambda: self._stability_on_show()).pack(
            side=tk.RIGHT, padx=(8, 0)
        )

        stab_tests_mid = ttk.Frame(fr)
        eth_fr = ttk.LabelFrame(stab_tests_mid, text="（1）以太网：定时禁用/启用后检测", padding=4)
        eth_fr.pack(fill=tk.X, pady=(0, 2))
        eth_disable_wifi = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            eth_fr,
            text="测以太网时全程禁用 Wi‑Fi，停止后恢复（避免每轮启停不稳定，推荐开启）",
            variable=eth_disable_wifi,
        ).pack(anchor=tk.W, pady=(0, 2))
        er1 = ttk.Frame(eth_fr)
        er1.pack(fill=tk.X)
        eth_row0 = ttk.Frame(er1)
        eth_row0.grid(row=0, column=0, sticky=tk.W)
        ttk.Label(eth_row0, text="网卡名称").pack(side=tk.LEFT)
        eth_combo = ttk.Combobox(eth_row0, textvariable=eth_ad, width=28)
        eth_combo.pack(side=tk.LEFT, padx=(4, 4))
        ttk.Button(eth_row0, text="刷新列表", command=refresh_adapters).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Label(eth_row0, textvariable=eth_l, foreground="gray").pack(side=tk.LEFT)
        _ep = {"pady": (0, 2)}
        _abcd_hints = {
            "A": "A · 启动后保持（秒）。以太网：网卡启用成功后；Wi‑Fi：连接热点成功后。再进入检测。长测勿过小：设备需完成 ETH 链路协商与 DHCPS/NAPT 重绑。",
            "B": "B · 检测阶段（秒）。本阶段内执行 ping / http / 二者；若实际更快，会补齐等待至该时长。过小易在 NAPT 尚未就绪时判失败。",
            "C": "C · 检测后保持（秒）。检测结束至下一轮禁用网卡前的等待。",
            "D": "D · 禁用保持（秒）。网卡禁用后保持的时长，至下一次启用。过短会加剧 ETH 频繁 Up/Down，与固件 SoftAP 全量重跑叠加易不稳定。",
        }
        _abcd_suffix_cn = {"A": "启用", "B": "测试", "C": "保持", "D": "禁用"}
        eth_abcd = ttk.Frame(er1)
        eth_abcd.grid(row=1, column=0, sticky=tk.W, **_ep)
        for lab, var, tail in (("A", eth_a, 10), ("B", eth_b, 10), ("C", eth_c, 10), ("D", eth_d, 0)):
            pair = ttk.Frame(eth_abcd)
            ttk.Label(pair, text=lab).pack(side=tk.LEFT)
            ttk.Label(pair, text=_abcd_suffix_cn[lab], foreground="gray").pack(side=tk.LEFT, padx=(2, 0))
            ttk.Entry(pair, textvariable=var, width=5).pack(side=tk.LEFT, padx=(4, 0))
            pair.pack(side=tk.LEFT, padx=(0, tail))
            _bind_tooltip(pair, _abcd_hints[lab])
        ttk.Label(
            er1,
            text="长测建议：A/B/D 勿过小（设备需 DHCPS/NAPT 重绑时间）；链路切换时避免频繁打开管理页连续拉取接口配置。",
            foreground="gray",
            font=("Segoe UI", 8),
            wraplength=560,
            justify=tk.LEFT,
        ).grid(row=2, column=0, sticky=tk.W, pady=(0, 2))
        eth_det = ttk.Frame(er1)
        eth_det.grid(row=3, column=0, sticky=tk.W, **_ep)
        ttk.Label(eth_det, text="检测").pack(side=tk.LEFT)
        eth_test_fr = ttk.Frame(eth_det)
        eth_test_fr.pack(side=tk.LEFT, padx=(4, 0))
        ttk.Radiobutton(eth_test_fr, text="ping", variable=eth_test, value="ping").pack(side=tk.LEFT)
        ttk.Radiobutton(eth_test_fr, text="http baidu", variable=eth_test, value="http").pack(side=tk.LEFT, padx=(6, 0))
        ttk.Radiobutton(eth_test_fr, text="ping+http baidu", variable=eth_test, value="ping_http").pack(
            side=tk.LEFT, padx=(6, 0)
        )
        ttk.Label(eth_det, text="ping 目标").pack(side=tk.LEFT, padx=(8, 4))
        ttk.Entry(eth_det, textvariable=eth_ping, width=16).pack(side=tk.LEFT)

        eth_counts = {"ok": 0, "fail": 0}
        eth_stats_var = tk.StringVar(value="成功 0 / 失败 0 / 总计 0")

        def _refresh_eth_stats() -> None:
            t = eth_counts["ok"] + eth_counts["fail"]
            eth_stats_var.set(f"成功 {eth_counts['ok']} / 失败 {eth_counts['fail']} / 总计 {t}")

        def _on_eth_cycle(ok: bool) -> None:
            if ok:
                eth_counts["ok"] += 1
            else:
                eth_counts["fail"] += 1
            self.ctx.root.after(0, _refresh_eth_stats)

        eth_row_btns = ttk.Frame(er1)
        eth_row_btns.grid(row=4, column=0, sticky=tk.W, pady=(4, 0))
        eth_btns = ttk.Frame(eth_row_btns)
        eth_btns.pack(side=tk.LEFT)
        eth_btn_start = ttk.Button(eth_btns, text="开始以太网测试")
        eth_btn_stop = ttk.Button(eth_btns, text="停止", state=tk.DISABLED)
        eth_btn_start.pack(side=tk.LEFT, padx=(0, 4))
        eth_btn_stop.pack(side=tk.LEFT)
        ttk.Label(eth_row_btns, textvariable=eth_stats_var).pack(side=tk.LEFT, padx=(12, 0))

        def eth_done() -> None:
            eth_btn_start.configure(state=tk.NORMAL)
            eth_btn_stop.configure(state=tk.DISABLED)

        def start_eth() -> None:
            name = eth_ad.get().strip()
            if not name:
                messagebox.showwarning("稳定性", "请填写以太网网卡名称", parent=self.ctx.root)
                return
            try:
                a = float(eth_a.get().strip() or "0")
                b = float(eth_b.get().strip() or "0")
                c = float(eth_c.get().strip() or "0")
                d = float(eth_d.get().strip() or "0")
            except ValueError:
                messagebox.showerror("稳定性", "时间参数无效", parent=self.ctx.root)
                return
            if min(a, b, c, d) < 0:
                messagebox.showerror("稳定性", "A/B/C/D 不能为负数", parent=self.ctx.root)
                return
            if eth_thr["t"] and eth_thr["t"].is_alive():
                messagebox.showinfo("稳定性", "以太网测试已在运行", parent=self.ctx.root)
                return
            eth_stop.clear()
            eth_counts["ok"] = 0
            eth_counts["fail"] = 0
            _refresh_eth_stats()
            cfg = CycleConfig(
                adapter=name,
                stable_after_enable_s=a,
                test_phase_s=b,
                stable_after_test_s=c,
                disabled_hold_s=d,
                test=str(eth_test.get()),
                ping_host=eth_ping.get().strip() or "8.8.8.8",
                disable_wifi_for_eth=eth_disable_wifi.get(),
                **_stab_adv_kwargs(),
            )

            def log(msg: str) -> None:
                self.ctx.root.after(0, lambda m=msg: append_line(m))

            def done() -> None:
                def _ui() -> None:
                    stability_test_power_session_leave(append_line)
                    eth_done()

                self.ctx.root.after(0, _ui)

            def run() -> None:
                run_eth_loop(cfg, eth_stop, log, done, on_cycle_result=_on_eth_cycle)

            t = threading.Thread(target=run, daemon=True)
            eth_thr["t"] = t
            eth_btn_start.configure(state=tk.DISABLED)
            eth_btn_stop.configure(state=tk.NORMAL)
            stability_test_power_session_enter(append_line)
            append_line("--- 以太网测试开始 ---")
            t.start()

        def stop_eth() -> None:
            eth_stop.set()
            append_line("--- 以太网测试停止请求已发送 ---")

        eth_btn_start.configure(command=start_eth)
        eth_btn_stop.configure(command=stop_eth)

        wf_fr = ttk.LabelFrame(stab_tests_mid, text="（2）Wi‑Fi：定时禁用/启用后连接热点并检测", padding=4)
        wf_fr.pack(fill=tk.X, pady=(0, 2))
        wf_fr.grid_columnconfigure(0, weight=1)
        wf_disable_eth = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            wf_fr,
            text="测 Wi‑Fi 时全程禁用有线网卡，停止后恢复（避免每轮启停不稳定，推荐开启）",
            variable=wf_disable_eth,
        ).grid(row=0, column=0, sticky=tk.W, pady=(0, 2))
        wr1 = ttk.Frame(wf_fr)
        wr1.grid(row=1, column=0, sticky=tk.EW)
        _wp = {"pady": (0, 2)}
        wf_row0 = ttk.Frame(wr1)
        wf_row0.grid(row=0, column=0, sticky=tk.W)
        ttk.Label(wf_row0, text="无线网卡名称").pack(side=tk.LEFT)
        wf_combo = ttk.Combobox(wf_row0, textvariable=wf_ad, width=24)
        wf_combo.pack(side=tk.LEFT, padx=(4, 8))
        ttk.Label(wf_row0, textvariable=wf_l, foreground="gray").pack(side=tk.LEFT)
        wf_row1 = ttk.Frame(wr1)
        wf_row1.grid(row=1, column=0, sticky=tk.W, **_wp)
        ttk.Label(wf_row1, text="SSID").pack(side=tk.LEFT)
        ttk.Entry(wf_row1, textvariable=wf_ssid, width=22).pack(side=tk.LEFT, padx=(2, 0))
        ttk.Label(wf_row1, text="密码").pack(side=tk.LEFT, padx=(8, 0))
        ttk.Entry(wf_row1, textvariable=wf_pwd, width=22, show="*").pack(side=tk.LEFT, padx=(2, 0))
        wf_abcd = ttk.Frame(wr1)
        wf_abcd.grid(row=2, column=0, sticky=tk.W, **_wp)
        for lab, var, tail in (("A", wf_a, 10), ("B", wf_b, 10), ("C", wf_c, 10), ("D", wf_d, 0)):
            pair = ttk.Frame(wf_abcd)
            ttk.Label(pair, text=lab).pack(side=tk.LEFT)
            ttk.Label(pair, text=_abcd_suffix_cn[lab], foreground="gray").pack(side=tk.LEFT, padx=(2, 0))
            ttk.Entry(pair, textvariable=var, width=5).pack(side=tk.LEFT, padx=(4, 0))
            pair.pack(side=tk.LEFT, padx=(0, tail))
            _bind_tooltip(pair, _abcd_hints[lab])
        ttk.Label(
            wr1,
            text="长测建议：A/B/D 勿过小；稳定性测试期间尽量少开管理页批量读配置，以免与网卡启停、NAPT 重绑争用。",
            foreground="gray",
            font=("Segoe UI", 8),
            wraplength=560,
            justify=tk.LEFT,
        ).grid(row=3, column=0, sticky=tk.W, pady=(0, 2))
        wf_det = ttk.Frame(wr1)
        wf_det.grid(row=4, column=0, sticky=tk.W, **_wp)
        ttk.Label(wf_det, text="检测").pack(side=tk.LEFT)
        wf_test_fr = ttk.Frame(wf_det)
        wf_test_fr.pack(side=tk.LEFT, padx=(4, 0))
        ttk.Radiobutton(wf_test_fr, text="ping", variable=wf_test, value="ping").pack(side=tk.LEFT)
        ttk.Radiobutton(wf_test_fr, text="http baidu", variable=wf_test, value="http").pack(side=tk.LEFT, padx=(6, 0))
        ttk.Radiobutton(wf_test_fr, text="ping+http baidu", variable=wf_test, value="ping_http").pack(
            side=tk.LEFT, padx=(6, 0)
        )
        ttk.Label(wf_det, text="ping 目标").pack(side=tk.LEFT, padx=(8, 4))
        ttk.Entry(wf_det, textvariable=wf_ping, width=16).pack(side=tk.LEFT)

        wf_counts = {"ok": 0, "fail": 0}
        wf_stats_var = tk.StringVar(value="成功 0 / 失败 0 / 总计 0")

        def _refresh_wf_stats() -> None:
            t = wf_counts["ok"] + wf_counts["fail"]
            wf_stats_var.set(f"成功 {wf_counts['ok']} / 失败 {wf_counts['fail']} / 总计 {t}")

        def _on_wf_cycle(ok: bool) -> None:
            if ok:
                wf_counts["ok"] += 1
            else:
                wf_counts["fail"] += 1
            self.ctx.root.after(0, _refresh_wf_stats)

        wf_row_btns = ttk.Frame(wr1)
        wf_row_btns.grid(row=5, column=0, sticky=tk.W, pady=(4, 0))
        wf_btns = ttk.Frame(wf_row_btns)
        wf_btns.pack(side=tk.LEFT)
        wf_btn_start = ttk.Button(wf_btns, text="开始 Wi‑Fi 测试")
        wf_btn_stop = ttk.Button(wf_btns, text="停止", state=tk.DISABLED)
        wf_btn_start.pack(side=tk.LEFT, padx=(0, 4))
        wf_btn_stop.pack(side=tk.LEFT)
        ttk.Label(wf_row_btns, textvariable=wf_stats_var).pack(side=tk.LEFT, padx=(12, 0))

        def wf_done() -> None:
            wf_btn_start.configure(state=tk.NORMAL)
            wf_btn_stop.configure(state=tk.DISABLED)

        def start_wifi() -> None:
            name = wf_ad.get().strip()
            ssid = wf_ssid.get().strip()
            pwd = wf_pwd.get()
            if not name or not ssid:
                messagebox.showwarning("稳定性", "请填写无线网卡名称与 SSID", parent=self.ctx.root)
                return
            try:
                a = float(wf_a.get().strip() or "0")
                b = float(wf_b.get().strip() or "0")
                c = float(wf_c.get().strip() or "0")
                d = float(wf_d.get().strip() or "0")
            except ValueError:
                messagebox.showerror("稳定性", "时间参数无效", parent=self.ctx.root)
                return
            if min(a, b, c, d) < 0:
                messagebox.showerror("稳定性", "A/B/C/D 不能为负数", parent=self.ctx.root)
                return
            if wifi_thr["t"] and wifi_thr["t"].is_alive():
                messagebox.showinfo("稳定性", "Wi‑Fi 测试已在运行", parent=self.ctx.root)
                return
            wifi_stop.clear()
            wf_counts["ok"] = 0
            wf_counts["fail"] = 0
            _refresh_wf_stats()
            cfg = CycleConfig(
                adapter=name,
                stable_after_enable_s=a,
                test_phase_s=b,
                stable_after_test_s=c,
                disabled_hold_s=d,
                test=str(wf_test.get()),
                ping_host=wf_ping.get().strip() or "8.8.8.8",
                disable_eth_for_wifi=wf_disable_eth.get(),
                **_stab_adv_kwargs(),
            )

            def log(msg: str) -> None:
                self.ctx.root.after(0, lambda m=msg: append_line(m))

            def done() -> None:
                def _ui() -> None:
                    stability_test_power_session_leave(append_line)
                    wf_done()

                self.ctx.root.after(0, _ui)

            def run() -> None:
                run_wifi_loop(cfg, ssid, pwd, wifi_stop, log, done, on_cycle_result=_on_wf_cycle)

            t = threading.Thread(target=run, daemon=True)
            wifi_thr["t"] = t
            wf_btn_start.configure(state=tk.DISABLED)
            wf_btn_stop.configure(state=tk.NORMAL)
            stability_test_power_session_enter(append_line)
            append_line("--- Wi‑Fi 测试开始 ---")
            t.start()

        def stop_wifi() -> None:
            wifi_stop.set()
            append_line("--- Wi‑Fi 测试停止请求已发送 ---")

        wf_btn_start.configure(command=start_wifi)
        wf_btn_stop.configure(command=stop_wifi)

        stab_lan_gw = tk.StringVar(value="192.168.4.1")
        stab_dash_url = tk.StringVar(value="http://192.168.4.1")
        stab_dash_n = tk.StringVar(value="0")
        stab_retries = tk.StringVar(value="3")
        stab_retry_gap = tk.StringVar(value="1.5")

        def _stab_adv_kwargs() -> dict:
            lan = stab_lan_gw.get().strip() or "192.168.4.1"
            try:
                retries = int(stab_retries.get().strip() or "3")
            except ValueError:
                retries = 3
            try:
                gap = float(stab_retry_gap.get().strip() or "1.5")
            except ValueError:
                gap = 1.5
            url = stab_dash_url.get().strip()
            try:
                dn = int(stab_dash_n.get().strip() or "0")
            except ValueError:
                dn = 0
            return {
                "lan_gateway_host": lan,
                "connectivity_retries": max(1, retries),
                "retry_delay_s": max(0.0, gap),
                "dashboard_base_url": url,
                "dashboard_every_n_rounds": max(0, dn),
            }

        adv_fr = ttk.LabelFrame(stab_tests_mid, text="（3）取证与长测（可选）", padding=4)
        adv_fr.pack(fill=tk.X, pady=(0, 2))
        adv_r1 = ttk.Frame(adv_fr)
        adv_r1.pack(fill=tk.X, anchor=tk.W)
        ttk.Label(adv_r1, text="LAN 网关取证 ping").pack(side=tk.LEFT)
        ttk.Entry(adv_r1, textvariable=stab_lan_gw, width=14).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(adv_r1, text="重试次数").pack(side=tk.LEFT, padx=(12, 0))
        ttk.Entry(adv_r1, textvariable=stab_retries, width=4).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(adv_r1, text="间隔(s)").pack(side=tk.LEFT, padx=(8, 0))
        ttk.Entry(adv_r1, textvariable=stab_retry_gap, width=5).pack(side=tk.LEFT, padx=(4, 0))
        adv_r2 = ttk.Frame(adv_fr)
        adv_r2.pack(fill=tk.X, anchor=tk.W, pady=(4, 0))
        ttk.Label(adv_r2, text="设备 API 基址").pack(side=tk.LEFT)
        ttk.Entry(adv_r2, textvariable=stab_dash_url, width=26).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(adv_r2, text="每 N 轮概览(0=关)").pack(side=tk.LEFT, padx=(8, 0))
        ttk.Entry(adv_r2, textvariable=stab_dash_n, width=5).pack(side=tk.LEFT, padx=(4, 0))

        def apply_stab_visibility(le: Optional[bool], ls: Optional[bool]) -> None:
            """
            按当前模式是否含以太网 LAN / Wi‑Fi(SoftAP) LAN 显示（1）（2）；
            （3）在任一路 LAN 需要时显示。隐藏时「测试结果」整体上移。
            le/ls 为 None 时视为未解析，显示全部（与首次进入一致）。
            """
            if le is None or ls is None:
                le, ls = True, True
            eth_fr.pack_forget()
            wf_fr.pack_forget()
            adv_fr.pack_forget()
            if le:
                eth_fr.pack(fill=tk.X, pady=(0, 2))
            if ls:
                wf_fr.pack(fill=tk.X, pady=(0, 2))
            if le or ls:
                adv_fr.pack(fill=tk.X, pady=(0, 2))
            if le or ls:
                stab_tests_mid.grid(row=1, column=0, sticky=tk.EW)
                result_lf.grid(row=2, column=0, sticky=tk.NSEW, pady=(4, 0))
                fr.grid_rowconfigure(1, weight=0)
                fr.grid_rowconfigure(2, weight=1)
            else:
                stab_tests_mid.grid_remove()
                result_lf.grid(row=1, column=0, sticky=tk.NSEW, pady=(4, 0))
                fr.grid_rowconfigure(1, weight=1)
                fr.grid_rowconfigure(2, weight=0)

        self._apply_stab_visibility = apply_stab_visibility
        apply_stab_visibility(None, None)

        def refresh_stab_mode_hint() -> None:
            def work() -> Tuple[dict, dict, dict]:
                c = self._client()
                mode = c.get(P_MODE)
                ap: dict = {}
                net: dict = {}
                try:
                    r = c.get(P_WIFI_AP)
                    if isinstance(r, dict):
                        ap = r
                except SerialApiError:
                    pass
                try:
                    r2 = c.get(P_NET)
                    if isinstance(r2, dict):
                        net = r2
                except SerialApiError:
                    pass
                return mode, ap, net

            def ok(tup: Tuple[dict, dict, dict]) -> None:
                mode, ap, net = tup
                try:
                    stab_mode_var.set(format_stability_mode_hint(mode))
                    le, ls = stab_mode_lan_flags(mode)
                    fn = getattr(self, "_apply_stab_visibility", None)
                    if callable(fn):
                        fn(le, ls)
                except Exception as e:
                    stab_mode_var.set(f"设备模式：解析失败 {e!r}")
                    fn = getattr(self, "_apply_stab_visibility", None)
                    if callable(fn):
                        fn(None, None)
                    return
                row = _stab_resolve_current_mode_row(mode) if isinstance(mode, dict) else None
                if row and row.get("lan_softap") and isinstance(ap, dict):
                    ssid = str(ap.get("ssid") or "").strip()
                    if ssid:
                        wf_ssid.set(ssid)
                        pwd = str(ap.get("password") or "").strip()
                        if not pwd:
                            pwd = self._softap_password_from_local_cache(ssid)
                        wf_pwd.set(pwd)
                if isinstance(net, dict):
                    lan = net.get("lan") if isinstance(net.get("lan"), dict) else {}
                    ip = str(lan.get("ip") or "").strip()
                    if ip:
                        stab_lan_gw.set(ip)
                        stab_dash_url.set(f"http://{ip}")

            def er(e: BaseException) -> None:
                stab_mode_var.set(f"设备模式：读取失败（{e}）。请先连接串口。")
                fn = getattr(self, "_apply_stab_visibility", None)
                if callable(fn):
                    fn(None, None)

            _run_bg(self.ctx.root, work, ok, er)

        self._stability_on_show = refresh_stab_mode_hint

        self._stab_wf_ssid = wf_ssid
        self._stab_wf_pwd = wf_pwd
        refresh_adapters()

    def _build_at_test(self) -> None:
        fr = ttk.Frame(self._container, padding=8)
        self.pages["at_test"] = fr

        intro = (
            "通过本机已打开的串口向 ESP32‑S3 的 UART0 发送文本行（与窗口右侧「串口信息」为同一链路）。"
            "非 AT 行由 serial_cli 处理（如 modem_info、help）；以 AT 开头的行由嵌入式 router_at 处理（与控制台共用 UART 时生效）。"
            "应答与设备日志均在右侧显示。"
        )
        ttk.Label(fr, text=intro, wraplength=640, justify=tk.LEFT).pack(anchor=tk.W, pady=(0, 12))

        row = ttk.Frame(fr)
        row.pack(fill=tk.X, pady=(0, 8))
        ttk.Label(row, text="指令").pack(side=tk.LEFT, padx=(0, 8))
        at_var = tk.StringVar(value="AT")
        ent = ttk.Entry(row, textvariable=at_var, width=56)
        ent.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 8))

        def do_send() -> None:
            if not self.ctx.is_serial_open():
                messagebox.showwarning("AT 测试", "请先连接串口（菜单 → 连接设置）", parent=self.ctx.root)
                return
            text = at_var.get().strip()
            if not text:
                messagebox.showwarning("AT 测试", "请输入指令", parent=self.ctx.root)
                return
            try:
                self.ctx.send_raw_line(text)
            except SerialApiError as e:
                messagebox.showwarning("AT 测试", str(e), parent=self.ctx.root)
            except Exception as e:
                messagebox.showerror("AT 测试", str(e), parent=self.ctx.root)

        ttk.Button(row, text="发送", command=do_send, width=10).pack(side=tk.LEFT)
        ent.bind("<Return>", lambda _e: do_send())

        pf = ttk.LabelFrame(fr, text="快捷发送", padding=8)
        pf.pack(fill=tk.X, pady=(8, 0))

        presets = [
            # 基础指令
            ("AT", "AT"),
            ("ATE0", "ATE0"),
            ("ATE1", "ATE1"),
            ("AT+GMR", "AT+GMR"),
            ("AT+IDF", "AT+IDF"),
            ("AT+CHIP", "AT+CHIP"),
            ("AT+MEM", "AT+MEM"),
            ("AT+RST", "AT+RST"),
            ("AT+CMD", "AT+CMD"),
            ("AT+TIME", "AT+TIME"),
            ("AT+SYSRAM", "AT+SYSRAM"),
            # 路由器相关
            ("AT+ROUTER", "AT+ROUTER"),
            ("AT+MODE?", "AT+MODE?"),
            ("AT+PING", "AT+PING"),
            # 4G模组相关
            ("AT+MODEMINFO", "AT+MODEMINFO"),
            ("AT+MODEMTIME", "AT+MODEMTIME"),
            # W5500以太网
            ("AT+W5500", "AT+W5500"),
            ("AT+W5500IP", "AT+W5500IP"),
            # USB 4G
            ("AT+USB4G", "AT+USB4G"),
            ("AT+USB4GIP", "AT+USB4GIP"),
            # 网络检测
            ("AT+NETCHECK", "AT+NETCHECK"),
            # serial_cli命令
            ("modem_info", "modem_info"),
            ("help", "help"),
        ]

        def preset(s: str) -> None:
            at_var.set(s)
            do_send()

        pr = ttk.Frame(pf)
        pr.pack(fill=tk.X)
        for i, (lab, cmd) in enumerate(presets):
            ttk.Button(pr, text=lab, command=lambda c=cmd: preset(c)).grid(
                row=i // 3, column=i % 3, padx=4, pady=4, sticky=tk.W
            )

        ttk.Label(
            fr,
            text="提示：modem_info 走 serial_cli（4G 模组信息）；AT+CMD 列出固件已实现的 AT 子集；"
            "已实现的 AT 指令包括：AT, ATE0/1, AT+GMR, AT+IDF, AT+CHIP, AT+MEM, AT+RST, AT+CMD, "
            "AT+TIME(查询/设置), AT+SYSRAM, AT+ROUTER, AT+MODE(查询/设置), AT+PING, "
            "AT+MODEMINFO, AT+MODEMTIME, AT+W5500, AT+W5500IP, AT+USB4G, AT+USB4GIP, AT+NETCHECK。"
            "若 UART 与日志控制台共用，仅「像 AT 的一整行」会进 router_at。",
            foreground="gray",
            wraplength=640,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(16, 0))

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
                except SerialApiError:
                    pass

            def ok(_: Any) -> None:
                messagebox.showinfo("重启", "已发送重启指令")
                self._log("reboot POST")

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
                self._log("reboot schedule saved")

            def er(e: BaseException) -> None:
                messagebox.showerror("定时重启", str(e))

            _run_bg(self.ctx.root, work, ok, er)

        bf = ttk.Frame(fr)
        bf.pack(fill=tk.X, pady=(12, 0))
        ttk.Button(bf, text="读取定时设置", command=load_sch).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bf, text="保存定时设置", command=save_sch).pack(side=tk.LEFT)
