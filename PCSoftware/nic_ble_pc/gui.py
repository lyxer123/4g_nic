"""Tkinter UI: Web-parity admin over UART (PCAPI). Menubar navigation; serial log on the right."""

from __future__ import annotations

import tkinter as tk
from tkinter import PanedWindow, messagebox, ttk
from typing import Callable, Optional

from .admin_pages import AdminPages, PageContext
from .device_serial import SerialApiClient, SerialApiError, SerialMux, list_com_ports
from .log_view import ColoredLog

BASE_TITLE = "4G NIC — 网络管理（串口）"


def run() -> None:
    root = tk.Tk()
    root.title(BASE_TITLE)
    root.minsize(1180, 620)

    # 经典 PanedWindow：中间有可拖拽的分隔条（sash），左右按比例伸缩
    main_h = PanedWindow(
        root,
        orient=tk.HORIZONTAL,
        sashwidth=6,
        sashrelief=tk.GROOVE,
        sashpad=2,
        bd=0,
    )
    main_h.pack(fill=tk.BOTH, expand=True)

    center_col = ttk.Frame(main_h, padding=8)
    main_h.add(center_col, minsize=360, stretch="always")

    right = ttk.Frame(main_h, padding=(6, 8, 8, 8))
    main_h.add(right, minsize=260, stretch="always")

    def _initial_sash_once(_e: tk.Event | None = None) -> None:
        w = main_h.winfo_width()
        h = max(1, main_h.winfo_height())
        if w < 200:
            return
        # Python 3.11+ 的 PanedWindow 无 sashpos，用 Tcl 层的 sash_place（x 为水平分割位置）
        x = max(360, min(int(w * 0.62), w - 280))
        main_h.sash_place(0, x, h // 2)
        main_h.unbind("<Map>")

    main_h.bind("<Map>", _initial_sash_once, add=True)

    log_lf = ttk.LabelFrame(right, text="串口信息", padding=(0, 4, 0, 0))
    log_lf.pack(fill=tk.BOTH, expand=True)
    log_view = ColoredLog(log_lf, max_lines=900, font=("Consolas", 9))
    log_view.pack(fill=tk.BOTH, expand=True)

    def log_serial(msg: str) -> None:
        root.after(0, lambda m=msg: log_view.append_line(m, source="serial"))

    mux = SerialMux(log_line=log_serial, default_timeout_s=120.0)
    api_holder: dict[str, Optional[SerialApiClient]] = {"c": None}

    def get_client() -> SerialApiClient:
        c = api_holder["c"]
        if c is None:
            raise SerialApiError("请先连接串口（菜单 → 连接设置）")
        return c

    connected = {"v": False}
    connection_label = {"port": "", "baud": ""}
    # 连接设置对话框创建早于 menubar，用 ref 挂接「业务菜单」启用/禁用
    menu_enable_ref: dict[str, Callable[[bool], None]] = {}

    def refresh_window_title() -> None:
        if connected["v"] and connection_label["port"]:
            root.title(
                f"{BASE_TITLE}  |  串口 {connection_label['port']}  {connection_label['baud']} baud"
            )
        else:
            root.title(BASE_TITLE)

    def show_connection_dialog() -> None:
        dlg = tk.Toplevel(root)
        dlg.title("连接设置")
        dlg.resizable(False, False)
        dlg.transient(root)
        dlg.grab_set()

        conn = ttk.LabelFrame(dlg, text="连接", padding=10)
        conn.pack(fill=tk.X, padx=10, pady=(10, 0))
        ttk.Label(conn, text="串口").grid(row=0, column=0, sticky=tk.W)
        port_var = tk.StringVar()
        port_combo = ttk.Combobox(conn, textvariable=port_var, width=22, state="readonly")
        port_combo.grid(row=0, column=1, sticky=tk.EW, padx=(8, 0))
        ttk.Label(conn, text="波特率").grid(row=1, column=0, sticky=tk.W, pady=(8, 0))
        baud_var = tk.StringVar(value="115200")
        baud_ent = ttk.Entry(conn, textvariable=baud_var, width=12)
        baud_ent.grid(row=1, column=1, sticky=tk.W, padx=(8, 0), pady=(8, 0))
        conn.columnconfigure(1, weight=1)

        ble_fr = ttk.LabelFrame(dlg, text="蓝牙", padding=10)
        ble_fr.pack(fill=tk.X, padx=10, pady=(10, 0))
        ttk.Label(ble_fr, text="暂未启用（后续版本）", foreground="gray").pack(anchor=tk.W)
        ttk.Button(ble_fr, text="连接蓝牙设备…", state=tk.DISABLED).pack(anchor=tk.W, pady=(6, 0))

        btn_row = ttk.Frame(dlg, padding=(10, 10, 10, 6))
        btn_row.pack(fill=tk.X)

        def refresh_ports() -> None:
            ports = [p[0] for p in list_com_ports()]
            port_combo["values"] = ports
            if ports and port_var.get() not in ports:
                port_var.set(ports[0])
            elif not ports:
                port_var.set("")

        def set_controls_connected(v: bool) -> None:
            port_combo.configure(state=tk.DISABLED if v else "readonly")
            baud_ent.configure(state=tk.DISABLED if v else "normal")
            btn_rf.configure(state=tk.DISABLED if v else "normal")
            btn_conn.configure(text="断开串口" if v else "连接串口")

        def do_connect() -> None:
            if connected["v"]:
                mux.close()
                api_holder["c"] = None
                connected["v"] = False
                connection_label["port"] = ""
                connection_label["baud"] = ""
                refresh_window_title()
                set_controls_connected(False)
                log_serial("[GUI] 串口已断开")
                fn = menu_enable_ref.get("set_admin_menus")
                if fn:
                    fn(False)
                return
            p = port_var.get().strip()
            if not p:
                messagebox.showwarning("串口", "请选择 COM 口", parent=dlg)
                return
            try:
                baud = int(baud_var.get().strip() or "115200")
            except ValueError:
                messagebox.showerror("串口", "波特率无效", parent=dlg)
                return
            try:
                mux.open(p, baud)
                api_holder["c"] = SerialApiClient(mux, timeout_s=120.0)
                connected["v"] = True
                connection_label["port"] = p
                connection_label["baud"] = str(baud)
                refresh_window_title()
                set_controls_connected(True)
                log_serial(f"[GUI] 已打开 {p} @ {baud}")
                fn = menu_enable_ref.get("set_admin_menus")
                if fn:
                    fn(True)
            except Exception as e:
                messagebox.showerror("串口", str(e), parent=dlg)
                api_holder["c"] = None

        btn_rf = ttk.Button(btn_row, text="刷新列表", command=refresh_ports)
        btn_rf.pack(side=tk.LEFT, padx=(0, 8))
        btn_conn = ttk.Button(btn_row, text="连接串口", command=do_connect)
        btn_conn.pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(btn_row, text="关闭", command=dlg.destroy).pack(side=tk.RIGHT)

        refresh_ports()
        if connected["v"]:
            set_controls_connected(True)
            if connection_label["port"]:
                port_var.set(connection_label["port"])
            baud_var.set(connection_label["baud"] or "115200")

        dlg.wait_window()

    menubar = tk.Menu(root)
    root.config(menu=menubar)

    menubar.add_command(label="连接设置…", command=show_connection_dialog)

    m_status = tk.Menu(menubar, tearoff=0)
    menubar.add_cascade(label="状态", menu=m_status)
    m_net = tk.Menu(menubar, tearoff=0)
    menubar.add_cascade(label="网络配置", menu=m_net)
    m_sys = tk.Menu(menubar, tearoff=0)
    menubar.add_cascade(label="系统管理", menu=m_sys)

    i_status = menubar.index("状态")
    i_net = menubar.index("网络配置")
    i_sys = menubar.index("系统管理")

    def set_admin_menus_enabled(en: bool) -> None:
        st = tk.NORMAL if en else tk.DISABLED
        menubar.entryconfigure(i_status, state=st)
        menubar.entryconfigure(i_net, state=st)
        menubar.entryconfigure(i_sys, state=st)

    menu_enable_ref["set_admin_menus"] = set_admin_menus_enabled
    set_admin_menus_enabled(connected["v"])

    page_title_var = tk.StringVar(value="总览")
    ttk.Label(center_col, textvariable=page_title_var, font=("Segoe UI", 13, "bold")).pack(anchor=tk.W, pady=(0, 6))
    ttk.Label(
        center_col,
        text="管理数据经串口 PCAPI 与设备内 Web 服务相同接口。请使用菜单「连接设置」打开串口。",
        foreground="gray",
        wraplength=620,
    ).pack(anchor=tk.W)
    page_holder = ttk.Frame(center_col)
    page_holder.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

    ctx = PageContext(
        root=root,
        get_client=get_client,
        log=log_serial,
        set_title=page_title_var.set,
    )
    admin = AdminPages(page_holder, ctx)

    def nav(page_id: str) -> None:
        admin.show(page_id)

    m_status.add_command(label="总览", command=lambda: nav("overview"))
    m_status.add_command(label="用户列表", command=lambda: nav("users"))
    m_net.add_command(label="网络配置", command=lambda: nav("network"))
    m_net.add_command(label="APN设置", command=lambda: nav("apn"))
    m_sys.add_command(label="管理密码", command=lambda: nav("password"))
    m_sys.add_command(label="系统时间", command=lambda: nav("systime"))
    m_sys.add_command(label="升级/复位", command=lambda: nav("upgrade"))
    m_sys.add_command(label="系统日志", command=lambda: nav("logs"))
    m_sys.add_command(label="网络检测", command=lambda: nav("probes"))
    m_sys.add_command(label="重启", command=lambda: nav("reboot"))

    admin.show("overview")

    ttk.Label(
        root,
        text="串口/蓝牙参数在「连接设置」中配置；连接后标题栏显示当前串口与波特率。",
        foreground="gray",
    ).pack(fill=tk.X, padx=8, pady=(0, 6))

    def on_close() -> None:
        mux.close()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()
