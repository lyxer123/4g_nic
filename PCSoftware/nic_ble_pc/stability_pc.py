"""Windows PC-side network cycling for stability tests (Ethernet / Wi‑Fi). Requires admin rights for netsh."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from xml.sax.saxutils import escape as _xml_escape

from dataclasses import dataclass
from typing import Callable, List, Optional, Tuple

IS_WINDOWS = sys.platform == "win32"

LogFn = Callable[[str], None]
CycleResultFn = Callable[[bool], None]


def _no_window_kwargs() -> dict:
    if not IS_WINDOWS:
        return {}
    # Python 3.7+ on Windows
    return {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0)}


def _decode_console_output(raw: bytes, *, prefer_oem: bool) -> str:
    """ping/netsh 在中文 Windows 常为 OEM/GBK；PowerShell 在设置 UTF-8 后多为 UTF-8。"""
    if not raw:
        return ""
    if not IS_WINDOWS:
        return raw.decode("utf-8", errors="replace")
    order = ("gbk", "cp936", "utf-8-sig", "utf-8") if prefer_oem else ("utf-8-sig", "utf-8", "gbk", "cp936")
    for enc in order:
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", errors="replace")


def run_cmd(
    args: List[str],
    *,
    timeout_s: float = 120.0,
    oem_console: bool = False,
) -> Tuple[int, str]:
    try:
        p = subprocess.run(
            args,
            capture_output=True,
            timeout=timeout_s,
            **_no_window_kwargs(),
        )
        raw = (p.stdout or b"") + (p.stderr or b"")
        out = _decode_console_output(raw, prefer_oem=oem_console)
        return p.returncode, out.strip()
    except subprocess.TimeoutExpired as e:
        return -1, f"timeout: {e}"
    except OSError as e:
        return -1, str(e)


def powershell_json(command: str, timeout_s: float = 30.0) -> Tuple[int, str]:
    # Force UTF-8 so Chinese adapter names survive; decode prefers UTF-8 first (oem_console=False).
    ps = (
        "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); "
        "$OutputEncoding = [Console]::OutputEncoding; "
        + command
    )
    args = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        ps,
    ]
    return run_cmd(args, timeout_s=timeout_s)


def list_net_adapter_names(kind: str = "all") -> List[str]:
    """kind: 'all' | 'ethernet' | 'wifi' — Windows only."""
    if not IS_WINDOWS:
        return []
    # MediaType: 802.3 wired, 802.11 wireless (wording varies by locale/driver)
    filt = ""
    if kind == "ethernet":
        filt = "| Where-Object { $_.MediaType -match '802.3' -or $_.PhysicalMediaType -match '802.3' }"
    elif kind == "wifi":
        filt = "| Where-Object { $_.MediaType -match '802.11' -or $_.InterfaceDescription -match 'Wireless|Wi-?Fi|WLAN' }"
    cmd = (
        "Get-NetAdapter | Where-Object { $_.Status -ne $null } "
        f"{filt} "
        "| Sort-Object Name | Select-Object -ExpandProperty Name"
    )
    code, out = powershell_json(cmd)
    if code != 0 or not out:
        # fallback: all names
        code2, out2 = powershell_json(
            "Get-NetAdapter | Sort-Object Name | Select-Object -ExpandProperty Name"
        )
        if code2 == 0 and out2:
            names = [x.strip() for x in out2.splitlines() if x.strip()]
            return names
        return []
    return [x.strip() for x in out.splitlines() if x.strip()]


def set_interface_admin(name: str, enabled: bool) -> Tuple[bool, str]:
    """Enable or disable a network interface by exact name (netsh)."""
    if not IS_WINDOWS or not name.strip():
        return False, "invalid"
    adm = "ENABLED" if enabled else "DISABLED"
    code, out = run_cmd(
        ["netsh", "interface", "set", "interface", name, f"admin={adm}"],
        timeout_s=60.0,
        oem_console=True,
    )
    ok = code == 0
    return ok, out or f"exit {code}"


def _wlan_profile_xml_wpa2(ssid: str, passphrase: str) -> str:
    s = _xml_escape(ssid)
    k = _xml_escape(passphrase)
    return f"""<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>{s}</name>
  <SSIDConfig>
    <SSID>
      <name>{s}</name>
    </SSID>
  </SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM>
    <security>
      <authEncryption>
        <authentication>WPA2PSK</authentication>
        <encryption>AES</encryption>
        <useOneX>false</useOneX>
      </authEncryption>
      <sharedKey>
        <keyType>passPhrase</keyType>
        <protected>false</protected>
        <keyMaterial>{k}</keyMaterial>
      </sharedKey>
    </security>
  </MSM>
</WLANProfile>
"""


def _wlan_profile_xml_open(ssid: str) -> str:
    s = _xml_escape(ssid)
    return f"""<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>{s}</name>
  <SSIDConfig>
    <SSID>
      <name>{s}</name>
    </SSID>
  </SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM>
    <security>
      <authEncryption>
        <authentication>open</authentication>
        <encryption>none</encryption>
        <useOneX>false</useOneX>
      </authEncryption>
    </security>
  </MSM>
</WLANProfile>
"""


def wlan_disconnect(interface_name: str) -> Tuple[int, str]:
    """断开当前 WLAN 连接，避免自动连到其它记住的网络。"""
    if not interface_name.strip():
        return 0, ""
    return run_cmd(
        ["netsh", "wlan", "disconnect", f"interface={interface_name.strip()}"],
        timeout_s=25.0,
        oem_console=True,
    )


def wlan_show_networks_brief() -> str:
    """扫描可见 SSID（用于日志；节选避免过长）。"""
    code, out = run_cmd(
        ["netsh", "wlan", "show", "networks", "mode=bssid"],
        timeout_s=35.0,
        oem_console=True,
    )
    if code != 0:
        return out[:1200] if out else f"(show networks exit {code})"
    return out[:2000]


def wlan_connect(ssid: str, password: str, interface_name: str) -> Tuple[bool, str]:
    """
    Connect to WLAN. `netsh wlan connect` does NOT accept key=; we add a temporary WPA2 profile via XML.
    Profile must be added **on the same interface** as connect;先 disconnect 再扫描再连。
    """
    if not IS_WINDOWS or not ssid.strip():
        return False, "invalid ssid"
    iface = interface_name.strip()
    if not iface:
        return False, "需要指定无线网卡名称（与「网络连接」中名称一致）"
    lines: List[str] = []

    # 0) 断开当前连接，避免连到其它已保存热点
    dcode, dout = wlan_disconnect(iface)
    lines.append(f"disconnect: exit={dcode} {dout[:120] if dout else ''}".strip())
    time.sleep(1.0)

    # 1) 尝试用已有配置文件连接（仅 name + interface，避免 ssid 重复参数问题）
    args_try = ["netsh", "wlan", "connect", f"name={ssid}", f"interface={iface}"]
    code0, out0 = run_cmd(args_try, timeout_s=25.0, oem_console=True)
    if code0 == 0:
        return True, "\n".join(lines) + "\n" + (out0 or "connected (existing profile)")
    lines.append(f"(已有配置连接失败) {out0[:280]}")

    # 2) 删除该接口上的同名配置后重新导入 XML（UTF-8 BOM）
    xml_body = _wlan_profile_xml_open(ssid) if not password.strip() else _wlan_profile_xml_wpa2(ssid, password)
    fd, path = tempfile.mkstemp(suffix=".xml", prefix="nic_wlan_")
    try:
        with os.fdopen(fd, "w", encoding="utf-8-sig") as wf:
            wf.write(xml_body)
        run_cmd(
            ["netsh", "wlan", "delete", "profile", f"name={ssid}", f"interface={iface}"],
            timeout_s=25.0,
            oem_console=True,
        )
        code1, out1 = run_cmd(
            [
                "netsh",
                "wlan",
                "add",
                "profile",
                f"filename={path}",
                "user=current",
                f"interface={iface}",
            ],
            timeout_s=35.0,
            oem_console=True,
        )
        lines.append(f"add profile: {out1[:500]}")
        if code1 != 0:
            return False, "\n".join(lines)

        time.sleep(2.0)
        scan = wlan_show_networks_brief()
        lines.append("--- show networks (节选) ---")
        lines.append(scan[:1500])
        if ssid not in scan:
            lines.append(f"(提示: 扫描结果中未出现 SSID 文本「{ssid}」，请确认热点已开启且在范围内、2.4GHz 可见)")

        time.sleep(1.0)
        args2 = ["netsh", "wlan", "connect", f"name={ssid}", f"interface={iface}"]
        code2, out2 = run_cmd(args2, timeout_s=45.0, oem_console=True)
        lines.append(f"connect: {out2[:700]}")
        return code2 == 0, "\n".join(lines)
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def ping_once(host: str, timeout_ms: int = 3000) -> Tuple[bool, str]:
    if not IS_WINDOWS:
        return False, "not windows"
    code, out = run_cmd(
        ["ping", "-n", "1", "-w", str(timeout_ms), host],
        timeout_s=max(5.0, timeout_ms / 1000.0 + 2.0),
        oem_console=True,
    )
    ok = code == 0 and ("TTL=" in out or "ttl=" in out.lower())
    return ok, out


def http_check_baidu(timeout_s: float = 10.0) -> Tuple[bool, str]:
    url = "http://www.baidu.com/"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (compatible; 4g-nic-pc-test/1.0)"})
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            body = resp.read(2048)
            return True, f"HTTP {resp.status} len={len(body)}"
    except urllib.error.HTTPError as e:
        return e.code in (200, 301, 302, 303, 307, 308), f"HTTPError {e.code} {e.reason}"
    except Exception as e:
        return False, repr(e)


def _dashboard_overview_url(base: str) -> str:
    b = base.strip().rstrip("/")
    if not b:
        return ""
    return f"{b}/api/dashboard/overview"


def fetch_device_dashboard_overview(
    base_url: str,
    *,
    timeout_s: float = 10.0,
) -> Tuple[bool, str]:
    """GET /api/dashboard/overview（JSON 节选写入日志，用于长测对照 WAN）。"""
    url = _dashboard_overview_url(base_url)
    if not url:
        return False, "empty base_url"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (compatible; 4g-nic-pc-test/1.0)"})
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            raw = resp.read(8192)
            text = raw.decode("utf-8", errors="replace")
            try:
                data = json.loads(text)
                compact = json.dumps(data, ensure_ascii=False)[:3500]
                return True, compact
            except json.JSONDecodeError:
                return True, text[:3500]
    except urllib.error.HTTPError as e:
        return False, f"HTTPError {e.code} {e.reason}"
    except Exception as e:
        return False, repr(e)


def windows_default_routes_ipv4() -> str:
    if not IS_WINDOWS:
        return "(not windows)"
    ps = (
        "Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' "
        "-ErrorAction SilentlyContinue | "
        "Select-Object InterfaceAlias,NextHop,RouteMetric | "
        "Format-Table -AutoSize | Out-String -Width 220"
    )
    code, out = powershell_json(ps, timeout_s=20.0)
    if out.strip():
        return out.strip()
    return out or f"(Get-NetRoute exit {code})"


def windows_ipconfig_excerpt(max_chars: int = 9000) -> str:
    if not IS_WINDOWS:
        return "(not windows)"
    code, out = run_cmd(["ipconfig", "/all"], timeout_s=45.0, oem_console=True)
    if not out:
        return f"(ipconfig exit {code})"
    return out[:max_chars]


@dataclass
class CycleConfig:
    adapter: str
    # L = A+B+C+D（单轮：禁用→D→启用→A→检测B→保持C→下一轮再禁用）
    stable_after_enable_s: float  # A：启用（Wi‑Fi 含连接成功）后保持，链路稳定
    test_phase_s: float  # B：检测阶段时长（执行 ping/http 后不足则补齐等待）
    stable_after_test_s: float  # C：检测结束后、禁用前保持
    disabled_hold_s: float  # D：禁用后保持，至下一次启用
    test: str = "ping"  # "ping" | "http" | "ping_http"
    ping_host: str = "8.8.8.8"
    # Wi‑Fi 专用：测试全程禁用有线网卡（停止后恢复），避免 ping/http 仍走以太网
    disable_eth_for_wifi: bool = False
    # 以太网专用：测试全程禁用无线网卡（停止后恢复），避免 ping/http 仍走 Wi‑Fi
    disable_wifi_for_eth: bool = False
    # 失败取证：ping 设备 LAN 口（有线 192.168.4.1 / 连 SoftAP 时常为 192.168.5.1）
    lan_gateway_host: str = "192.168.4.1"
    connectivity_retries: int = 3
    retry_delay_s: float = 1.5
    # 长测：每 N 轮 GET 一次设备 /api/dashboard/overview（空 base 或 0 则关闭）
    dashboard_base_url: str = ""
    dashboard_every_n_rounds: int = 0


def _maybe_log_device_dashboard(cfg: CycleConfig, round_idx: int, log: LogFn, tag: str) -> None:
    """每 N 轮拉取一次设备 dashboard JSON（round_idx 从 1 开始）。"""
    interval = int(cfg.dashboard_every_n_rounds)
    base = (cfg.dashboard_base_url or "").strip()
    if interval <= 0 or not base:
        return
    if round_idx % interval != 0:
        return
    ok, msg = fetch_device_dashboard_overview(base)
    log(f"{tag} 设备概览 /api/dashboard/overview（第 {round_idx} 轮）: {'OK' if ok else 'FAIL'} {msg[:3200]}")


def log_connectivity_failure_diagnostics(cfg: CycleConfig, log: LogFn, tag: str) -> None:
    """失败时记录：默认路由、ipconfig 节选、到设备 LAN 口的 ping，区分「无 DHCP」与「无公网」。"""
    log(f"{tag} --- 失败取证：本机快照 ---")
    try:
        r = windows_default_routes_ipv4()
        log(f"{tag} [默认路由 IPv4]\n{r}")
    except Exception as e:
        log(f"{tag} [默认路由] 获取失败: {e!r}")
    try:
        ic = windows_ipconfig_excerpt()
        log(f"{tag} [ipconfig /all 节选]\n{ic}")
    except Exception as e:
        log(f"{tag} [ipconfig] 获取失败: {e!r}")
    gw = (cfg.lan_gateway_host or "").strip() or "192.168.4.1"
    pok, pmsg = ping_once(gw, timeout_ms=2500)
    log(f"{tag} ping {gw}（设备 LAN，见「LAN 网关取证」配置）: {'OK' if pok else 'FAIL'}\n{pmsg[:600]}")


def _sleep_logged(
    stop: threading.Event,
    seconds: float,
    log: LogFn,
    tag: str,
    phase_desc: str,
    letter: str,
) -> bool:
    """等待 seconds 秒；记录计划与墙钟实际耗时。若被 stop 打断则返回 True。"""
    if seconds <= 0:
        return False
    t0 = time.monotonic()
    interrupted = stop.wait(seconds)
    actual = time.monotonic() - t0
    intr = "（提前停止）" if interrupted else ""
    log(f"{tag} {phase_desc}：计划 {letter}={seconds:g}s，实际 {actual:.2f}s{intr}")
    return interrupted


def _phase_b_test(cfg: CycleConfig, stop: threading.Event, log: LogFn, tag: str) -> bool:
    """B：执行 ping/http（或二者），并将本阶段墙钟时间补齐至 test_phase_s。"""
    t0 = time.monotonic()
    verdict = _connectivity_test_verdict(cfg, log, tag, stop)
    if not verdict:
        log_connectivity_failure_diagnostics(cfg, log, tag)
    elapsed = time.monotonic() - t0
    rem = max(0.0, float(cfg.test_phase_s) - elapsed)
    if rem > 0:
        log(f"{tag} 检测阶段补齐等待 {rem:.2f}s（B={cfg.test_phase_s:g}s，已用 {elapsed:.2f}s）")
        if stop.wait(rem):
            pass
    return verdict


def _wait_retry_gap(stop: threading.Event, delay_s: float) -> bool:
    """若被 stop 打断返回 True。"""
    if delay_s <= 0:
        return False
    return stop.wait(delay_s)


def _ping_with_retries(
    host: str,
    cfg: CycleConfig,
    log: LogFn,
    tag: str,
    label: str,
    stop: threading.Event,
) -> Tuple[bool, str]:
    n = max(1, int(cfg.connectivity_retries))
    delay = max(0.0, float(cfg.retry_delay_s))
    last_out = ""
    for i in range(n):
        if stop.is_set():
            return False, last_out
        ok, out = ping_once(host, timeout_ms=3000)
        last_out = out
        if ok:
            note = f"（第 {i + 1}/{n} 次成功）" if i > 0 else ""
            log(f"{tag} {label}: OK{note}\n{out[:500]}")
            return True, out
        if i + 1 < n and _wait_retry_gap(stop, delay):
            return False, last_out
    log(f"{tag} {label}: FAIL（{n} 次均失败）\n{last_out[:500]}")
    return False, last_out


def _http_with_retries(
    cfg: CycleConfig,
    log: LogFn,
    tag: str,
    stop: threading.Event,
) -> Tuple[bool, str]:
    n = max(1, int(cfg.connectivity_retries))
    delay = max(0.0, float(cfg.retry_delay_s))
    last_msg = ""
    for i in range(n):
        if stop.is_set():
            return False, last_msg
        ok, msg = http_check_baidu()
        last_msg = msg
        if ok:
            note = f"（第 {i + 1}/{n} 次成功）" if i > 0 else ""
            log(f"{tag} baidu.com: OK{note} {msg}")
            return True, msg
        if i + 1 < n and _wait_retry_gap(stop, delay):
            return False, last_msg
    log(f"{tag} baidu.com: FAIL（{n} 次均失败） {last_msg}")
    return False, last_msg


def _connectivity_test_verdict(
    cfg: CycleConfig,
    log: LogFn,
    tag: str,
    stop: threading.Event,
) -> bool:
    """一轮连通性检测：ping / http / 二者均测；支持有限重试。"""
    if cfg.test == "http":
        hok, _ = _http_with_retries(cfg, log, tag, stop)
        return hok
    if cfg.test == "ping_http":
        pok, _ = _ping_with_retries(cfg.ping_host, cfg, log, tag, f"ping {cfg.ping_host}", stop)
        if not pok:
            return False
        hok, _ = _http_with_retries(cfg, log, tag, stop)
        return hok
    pok, _ = _ping_with_retries(cfg.ping_host, cfg, log, tag, f"ping {cfg.ping_host}", stop)
    return pok


def _one_eth_cycle(
    cfg: CycleConfig,
    stop: threading.Event,
    log: LogFn,
) -> Optional[bool]:
    """单轮：禁用→D→启用→A→B→C。返回本轮检测结果；若在 A 之前被停止则返回 None。"""
    d, a, c = cfg.disabled_hold_s, cfg.stable_after_enable_s, cfg.stable_after_test_s
    log(f"[以太网] 禁用 {cfg.adapter} （D={d:g}s）…")
    ok, msg = set_interface_admin(cfg.adapter, False)
    log(f"[以太网] 禁用结果: {'OK' if ok else 'FAIL'} {msg[:300]}")
    if _sleep_logged(stop, d, log, "[以太网]", "禁用保持", "D"):
        return None
    log(f"[以太网] 启用 {cfg.adapter} （A={a:g}s）…")
    ok2, msg2 = set_interface_admin(cfg.adapter, True)
    log(f"[以太网] 启用结果: {'OK' if ok2 else 'FAIL'} {msg2[:300]}")
    if _sleep_logged(stop, a, log, "[以太网]", "启动后保持", "A"):
        return None
    verdict = _phase_b_test(cfg, stop, log, "[以太网]")
    if _sleep_logged(stop, c, log, "[以太网]", "检测后保持", "C"):
        return verdict
    return verdict


def _one_wifi_cycle(
    cfg: CycleConfig,
    ssid: str,
    password: str,
    stop: threading.Event,
    log: LogFn,
) -> Optional[bool]:
    d, a, c = cfg.disabled_hold_s, cfg.stable_after_enable_s, cfg.stable_after_test_s
    log(f"[Wi‑Fi] 禁用 {cfg.adapter} （D={d:g}s）…")
    ok, msg = set_interface_admin(cfg.adapter, False)
    log(f"[Wi‑Fi] 禁用结果: {'OK' if ok else 'FAIL'} {msg[:300]}")
    if _sleep_logged(stop, d, log, "[Wi‑Fi]", "禁用保持", "D"):
        return None
    log(f"[Wi‑Fi] 启用 {cfg.adapter} （A={a:g}s）…")
    ok2, msg2 = set_interface_admin(cfg.adapter, True)
    log(f"[Wi‑Fi] 启用结果: {'OK' if ok2 else 'FAIL'} {msg2[:300]}")
    log(f"[Wi‑Fi] 连接热点 {ssid!r} …")
    wok, wmsg = wlan_connect(ssid, password, cfg.adapter)
    log(f"[Wi‑Fi] 连接结果: {'OK' if wok else 'FAIL'} {wmsg[:1200]}")
    if not wok:
        log(
            "[Wi‑Fi] 若热点使用 WPA2 密码，请在上方填写正确密码；开放热点请留空密码。"
            " ping 成功仍可能经有线出口，请勾选「测试全程禁用有线网卡」。"
        )
    if _sleep_logged(stop, a, log, "[Wi‑Fi]", "启动后保持", "A"):
        return None
    verdict = _phase_b_test(cfg, stop, log, "[Wi‑Fi]")
    if _sleep_logged(stop, c, log, "[Wi‑Fi]", "检测后保持", "C"):
        return verdict
    return verdict


def run_eth_loop(
    cfg: CycleConfig,
    stop: threading.Event,
    log: LogFn,
    done: Callable[[], None],
    on_cycle_result: Optional[CycleResultFn] = None,
) -> None:
    disabled_wifi: List[str] = []
    try:
        if cfg.disable_wifi_for_eth:
            for wname in list_net_adapter_names("wifi"):
                ok_w, _ = set_interface_admin(wname, False)
                if ok_w:
                    disabled_wifi.append(wname)
            if disabled_wifi:
                log(
                    f"[以太网] 已禁用无线网卡（测试全程保持，停止后恢复）: {', '.join(disabled_wifi)}"
                )
            else:
                log(
                    "[以太网] 未找到无线网卡或禁用失败（若 ping 仍通，可能仍走 Wi‑Fi 或其它出口）"
                )
        n = 0
        while not stop.is_set():
            n += 1
            log(f"======== 以太网 第 {n} 轮 ========")
            _maybe_log_device_dashboard(cfg, n, log, "[以太网]")
            verdict = _one_eth_cycle(cfg, stop, log)
            if verdict is not None and on_cycle_result is not None:
                on_cycle_result(verdict)
    finally:
        for wname in disabled_wifi:
            set_interface_admin(wname, True)
        if disabled_wifi:
            log(f"[以太网] 已恢复无线网卡: {', '.join(disabled_wifi)}")
        done()


def run_wifi_loop(
    cfg: CycleConfig,
    ssid: str,
    password: str,
    stop: threading.Event,
    log: LogFn,
    done: Callable[[], None],
    on_cycle_result: Optional[CycleResultFn] = None,
) -> None:
    disabled_eth: List[str] = []
    try:
        if cfg.disable_eth_for_wifi:
            for ename in list_net_adapter_names("ethernet"):
                ok_e, _ = set_interface_admin(ename, False)
                if ok_e:
                    disabled_eth.append(ename)
            if disabled_eth:
                log(
                    f"[Wi‑Fi] 已禁用有线网卡（测试全程保持，停止后恢复）: {', '.join(disabled_eth)}"
                )
            else:
                log("[Wi‑Fi] 未找到有线网卡或禁用失败（若 ping 仍通，可能仍走其它出口）")
        n = 0
        while not stop.is_set():
            n += 1
            log(f"======== Wi‑Fi 第 {n} 轮 ========")
            _maybe_log_device_dashboard(cfg, n, log, "[Wi‑Fi]")
            verdict = _one_wifi_cycle(cfg, ssid, password, stop, log)
            if verdict is not None and on_cycle_result is not None:
                on_cycle_result(verdict)
    finally:
        for ename in disabled_eth:
            set_interface_admin(ename, True)
        if disabled_eth:
            log(f"[Wi‑Fi] 已恢复有线网卡: {', '.join(disabled_eth)}")
        done()
