"""Tkinter UI: Web-style admin only (HTTP)."""

from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk
from typing import Any

from .admin_pages import AdminPages, PageContext
from .device_http import WebApiClient, normalize_base_url


def run() -> None:
    root = tk.Tk()
    root.title("4G NIC — 网络管理")
    root.minsize(1180, 620)

    main_h = ttk.PanedWindow(root, orient=tk.HORIZONTAL)
    main_h.pack(fill=tk.BOTH, expand=True)

    work = ttk.PanedWindow(main_h, orient=tk.HORIZONTAL)
    main_h.add(work, weight=3)

    nav_fr = ttk.Frame(work, width=200, padding=(6, 6, 0, 6))
    work.add(nav_fr, weight=0)

    center_col = ttk.Frame(work, padding=8)
    work.add(center_col, weight=2)

    base_url_var = tk.StringVar(value="http://192.168.5.1")
    url_row = ttk.Frame(center_col)
    url_row.pack(fill=tk.X)
    ttk.Label(url_row, text="设备 Web 基址").pack(side=tk.LEFT)
    ttk.Entry(url_row, textvariable=base_url_var, width=32).pack(side=tk.LEFT, padx=6)

    def test_http() -> None:
        try:
            WebApiClient(normalize_base_url(base_url_var.get())).get("/api/mode")
            messagebox.showinfo("HTTP", "连接正常（已读到 /api/mode）")
        except BaseException as e:
            messagebox.showerror("HTTP", str(e))

    ttk.Button(url_row, text="测试连接", command=test_http).pack(side=tk.LEFT)
    ttk.Label(
        center_col,
        text="各管理页与浏览器网页使用相同 REST API。已临时移除 BLE/串口调试功能以提升稳定性。",
        foreground="gray",
        wraplength=560,
    ).pack(anchor=tk.W, pady=(4, 0))
    page_title_var = tk.StringVar(value="总览")
    ttk.Label(center_col, textvariable=page_title_var, font=("Segoe UI", 13, "bold")).pack(anchor=tk.W, pady=(8, 0))
    page_holder = ttk.Frame(center_col)
    page_holder.pack(fill=tk.BOTH, expand=True, pady=(6, 0))

    nav_wrap = ttk.Frame(nav_fr)
    nav_wrap.pack(fill=tk.BOTH, expand=True)
    tv = ttk.Treeview(nav_wrap, show="tree", selectmode="browse")
    tv.heading("#0", text="网络管理")
    ys = ttk.Scrollbar(nav_wrap, orient=tk.VERTICAL, command=tv.yview)
    tv.configure(yscrollcommand=ys.set)
    tv.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
    ys.pack(side=tk.RIGHT, fill=tk.Y)

    tv.insert("", tk.END, "g_status", text="状态", open=True)
    tv.insert("g_status", tk.END, "overview", text="总览")
    tv.insert("g_status", tk.END, "users", text="用户列表")
    tv.insert("", tk.END, "g_net", text="网络配置", open=True)
    tv.insert("g_net", tk.END, "network", text="网络配置")
    tv.insert("g_net", tk.END, "apn", text="APN设置")
    tv.insert("", tk.END, "g_sys", text="系统管理", open=True)
    tv.insert("g_sys", tk.END, "password", text="管理密码")
    tv.insert("g_sys", tk.END, "systime", text="系统时间")
    tv.insert("g_sys", tk.END, "upgrade", text="升级/复位")
    tv.insert("g_sys", tk.END, "logs", text="系统日志")
    tv.insert("g_sys", tk.END, "probes", text="网络检测")
    tv.insert("g_sys", tk.END, "reboot", text="重启")

    ctx = PageContext(
        root=root,
        get_base=lambda: base_url_var.get().strip(),
        log=lambda _s: None,
        set_title=page_title_var.set,
    )
    admin = AdminPages(page_holder, ctx)
    admin.show("overview")

    def on_nav_select(_: Any) -> None:
        sel = tv.selection()
        if not sel:
            return
        iid = sel[0]
        if tv.get_children(iid):
            return
        admin.show(iid)

    tv.bind("<<TreeviewSelect>>", on_nav_select)
    tv.selection_set("overview")

    ttk.Label(
        root,
        text="已临时移除 BLE 与串口 CLI 调试入口；当前仅保留 Web 管理页（HTTP）功能。",
        foreground="gray",
    ).pack(fill=tk.X, padx=8, pady=(0, 6))
    root.mainloop()
