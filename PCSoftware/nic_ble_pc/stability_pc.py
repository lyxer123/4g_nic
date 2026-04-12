"""Windows PC-side network cycling for stability tests (Ethernet / Wi‑Fi). Requires admin rights for netsh.

稳定性测试会话内会通过 powercfg 将「已接通电源」下的关闭显示器与睡眠设为从不，结束后恢复为固定分钟数。
"""

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


# 稳定性测试结束后恢复的「已接通电源」计时（分钟）
STABILITY_RESTORE_MONITOR_MIN = 10
STABILITY_RESTORE_STANDBY_MIN = 10

_power_stab_lock = threading.Lock()
_power_stab_sessions = 0


def windows_set_ac_display_sleep_timeout_minutes(monitor_min: int, standby_min: int) -> Tuple[bool, str]:
    """
    设置当前电源方案下「已接通电源」的关闭显示器与睡眠超时（分钟）。
    monitor_min / standby_min 为 0 表示「从不」。
    """
    if not IS_WINDOWS:
        return False, "not windows"
    c1, o1 = run_cmd(
        ["powercfg", "/change", "monitor-timeout-ac", str(monitor_min)],
        timeout_s=25.0,
        oem_console=True,
    )
    c2, o2 = run_cmd(
        ["powercfg", "/change", "standby-timeout-ac", str(standby_min)],
        timeout_s=25.0,
        oem_console=True,
    )
    ok = c1 == 0 and c2 == 0
    parts = [f"monitor-timeout-ac={monitor_min} rc={c1}", f"standby-timeout-ac={standby_min} rc={c2}"]
    if o1:
        parts.append(o1[:160])
    if o2:
        parts.append(o2[:160])
    return ok, " | ".join(parts)


def stability_test_power_session_enter(log: Optional[LogFn] = None) -> None:
    """开始一路稳定性测试：首路会话时将 AC 下显示器与睡眠设为从不。"""
    global _power_stab_sessions
    if not IS_WINDOWS:
        return
    with _power_stab_lock:
        _power_stab_sessions += 1
        if _power_stab_sessions > 1:
            if log:
                log(
                    f"[电源] 多路稳定性测试并行（当前 {_power_stab_sessions} 路），"
                    "已保持接通电源下关闭显示器/睡眠「从不」"
                )
            return
        ok, msg = windows_set_ac_display_sleep_timeout_minutes(0, 0)
        if log:
            log(
                f"[电源] 已接通电源：关闭显示器与睡眠已设为「从不」 "
                f"({'OK' if ok else 'FAIL'}) {msg[:320]}"
            )


def stability_test_power_session_leave(log: Optional[LogFn] = None) -> None:
    """结束一路稳定性测试：所有路均结束时将 AC 下两项恢复为 STABILITY_RESTORE_* 分钟。"""
    global _power_stab_sessions
    if not IS_WINDOWS:
        return
    with _power_stab_lock:
        if _power_stab_sessions <= 0:
            _power_stab_sessions = 0
            return
        _power_stab_sessions -= 1
        if _power_stab_sessions > 0:
            if log:
                log(
                    f"[电源] 仍有 {_power_stab_sessions} 路稳定性测试在运行，"
                    "暂不恢复电源计时"
                )
            return
        ok, msg = windows_set_ac_display_sleep_timeout_minutes(
            STABILITY_RESTORE_MONITOR_MIN,
            STABILITY_RESTORE_STANDBY_MIN,
        )
        if log:
            log(
                f"[电源] 已接通电源：关闭显示器与睡眠已恢复为 "
                f"{STABILITY_RESTORE_MONITOR_MIN} 分钟 "
                f"({'OK' if ok else 'FAIL'}) {msg[:320]}"
            )


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
  <connectionMode>auto</connectionMode>
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
  <connectionMode>auto</connectionMode>
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


def list_wlan_profile_names_on_interface(interface_name: str) -> List[str]:
    """列出指定无线接口上的已保存配置文件名称（中英文 netsh 输出）。"""
    if not IS_WINDOWS or not interface_name.strip():
        return []
    code, out = run_cmd(
        ["netsh", "wlan", "show", "profiles", f"interface={interface_name.strip()}"],
        timeout_s=30.0,
        oem_console=True,
    )
    if code != 0 or not out:
        return []
    names: List[str] = []
    for line in out.splitlines():
        if ":" not in line:
            continue
        left, right = line.split(":", 1)
        left_l = left.strip().lower()
        right = right.strip()
        if not right or right == "<None>":
            continue
        if "profile" in left_l or "配置文件" in left:
            names.append(right)
    return names


def wlan_set_other_profiles_manual(interface_name: str, except_ssid: str) -> int:
    """
    将除 except_ssid 外的已存 Wi‑Fi 配置设为「手动连接」，避免启用网卡后 Windows 自动连到其它记住的热点。
    返回已改为手动的配置数量。
    """
    iface = interface_name.strip()
    ex = except_ssid.strip()
    if not iface or not ex:
        return 0
    n = 0
    for pname in list_wlan_profile_names_on_interface(iface):
        if pname.casefold() == ex.casefold():
            continue
        code, _ = run_cmd(
            [
                "netsh",
                "wlan",
                "set",
                "profileparameter",
                f"name={pname}",
                "connectionmode=manual",
                f"interface={iface}",
            ],
            timeout_s=25.0,
            oem_console=True,
        )
        if code == 0:
            n += 1
    return n


def wlan_set_other_profiles_manual_all_adapters(except_ssid: str) -> int:
    """
    在本机所有无线接口上，将除目标 SSID 外的已保存配置设为手动连接。
    避免仅处理测试网卡时，其它无线接口（或虚拟适配器）上仍自动连到非 ESP32 热点。
    """
    ex = except_ssid.strip()
    if not ex:
        return 0
    total = 0
    for iface in list_net_adapter_names("wifi"):
        total += wlan_set_other_profiles_manual(iface, ex)
    return total


def wlan_restore_all_profiles_auto(interface_name: str) -> int:
    """测试结束后将本接口上所有已存配置恢复为自动连接。返回成功设置的数量。"""
    iface = interface_name.strip()
    if not iface:
        return 0
    n = 0
    for pname in list_wlan_profile_names_on_interface(iface):
        code, _ = run_cmd(
            [
                "netsh",
                "wlan",
                "set",
                "profileparameter",
                f"name={pname}",
                "connectionmode=auto",
                f"interface={iface}",
            ],
            timeout_s=25.0,
            oem_console=True,
        )
        if code == 0:
            n += 1
    return n


def wlan_restore_all_profiles_auto_all_adapters() -> int:
    """将本机所有无线接口上的已存配置恢复为自动连接（测试结束用）。"""
    total = 0
    for iface in list_net_adapter_names("wifi"):
        total += wlan_restore_all_profiles_auto(iface)
    return total


def wlan_wifi_test_apply_profile_policy(test_adapter: str, target_ssid: str, log: LogFn) -> None:
    """
    Wi‑Fi 稳定性测试开始时：禁止所有已保存热点的自动连接（除目标 SSID），
    并确保目标 SSID 在测试无线网卡上为自动连接。
    """
    ta = test_adapter.strip()
    ssid = target_ssid.strip()
    if not ta or not ssid:
        return
    n_man = wlan_set_other_profiles_manual_all_adapters(ssid)
    wlan_ensure_target_profile_auto(ta, ssid)
    log(
        f"[Wi‑Fi] 测试策略：已在所有无线接口上将非 {ssid!r} 的配置设为手动连接"
        f"（成功 {n_man} 项），目标热点在 {ta} 上为自动连接"
    )


def wlan_ensure_target_profile_auto(interface_name: str, ssid: str) -> None:
    """将目标 SSID 的配置设为自动连接，避免 manual 配置在关联后立刻掉线。"""
    iface = interface_name.strip()
    s = ssid.strip()
    if not iface or not s:
        return
    run_cmd(
        [
            "netsh",
            "wlan",
            "set",
            "profileparameter",
            f"name={s}",
            "connectionmode=auto",
            f"interface={iface}",
        ],
        timeout_s=20.0,
        oem_console=True,
    )


def wlan_get_interface_state(interface_name: str) -> Optional[str]:
    """当前接口 netsh 中的 State（如 connected / disconnected）。"""
    if not IS_WINDOWS or not interface_name.strip():
        return None
    code, out = run_cmd(
        ["netsh", "wlan", "show", "interfaces"],
        timeout_s=25.0,
        oem_console=True,
    )
    if code != 0 or not out:
        return None
    want = interface_name.strip().casefold()
    iface_block: Optional[str] = None
    for line in out.splitlines():
        ls = line.strip()
        low = ls.lower()
        if ":" in line and (
            low.startswith("name")
            or ls.startswith("名称")
            or ("名称" in ls[:12] and ":" in ls)
        ):
            iface_block = ls.split(":", 1)[1].strip().casefold()
            continue
        if iface_block == want and (low.startswith("state") or ls.startswith("状态")) and ":" in ls:
            return ls.split(":", 1)[1].strip()
    return None


def wlan_has_usable_ipv4(interface_name: str) -> bool:
    """接口上是否存在非 APIPA 的 IPv4（DHCP 已下发）。"""
    if not IS_WINDOWS or not interface_name.strip():
        return False
    ia = interface_name.strip().replace("'", "''")
    ps = (
        f"$x = @(Get-NetIPAddress -InterfaceAlias '{ia}' -AddressFamily IPv4 -ErrorAction SilentlyContinue | "
        "Where-Object { $_.IPAddress -notlike '169.254.*' }); "
        "if ($x.Count -gt 0) { '1' } else { '0' }"
    )
    code, out = powershell_json(ps, timeout_s=22.0)
    if code != 0:
        return False
    return out.strip() == "1"


def wlan_wait_until_ready(
    interface_name: str,
    ssid: str,
    stop: threading.Event,
    log: LogFn,
    tag: str,
    timeout_s: float = 60.0,
) -> Tuple[bool, str]:
    """
    等待关联到目标 SSID、State 为已连接且拿到非 APIPA 的 IPv4。
    解决：netsh connect 已成功但 DHCP 未完成或 manual 配置导致随后掉线，进入检测阶段时「媒体已断开」。
    """
    t0 = time.monotonic()
    target = ssid.strip().casefold()
    last_detail = ""
    next_log = t0
    while time.monotonic() - t0 < timeout_s:
        if stop.is_set():
            return False, "stopped"
        cur = wlan_get_connected_ssid(interface_name)
        st = wlan_get_interface_state(interface_name)
        st_l = (st or "").lower()
        ip_ok = wlan_has_usable_ipv4(interface_name)
        last_detail = f"ssid={cur!r} state={st!r} ipv4={ip_ok}"
        ssid_ok = cur is not None and cur.casefold() == target
        state_ok = (st is None) or ("connected" in st_l) or ("已连接" in (st or ""))
        if ssid_ok and ip_ok and state_ok:
            return True, last_detail
        now = time.monotonic()
        if now >= next_log:
            log(f"{tag} 等待链路/DHCP… {last_detail}")
            next_log = now + 8.0
        time.sleep(0.45)
    return False, f"超时 {timeout_s:g}s: {last_detail}"


def wlan_get_connected_ssid(interface_name: str) -> Optional[str]:
    """当前接口已关联的 SSID（show interfaces）。"""
    if not IS_WINDOWS or not interface_name.strip():
        return None
    code, out = run_cmd(
        ["netsh", "wlan", "show", "interfaces"],
        timeout_s=25.0,
        oem_console=True,
    )
    if code != 0 or not out:
        return None
    want = interface_name.strip().casefold()
    iface_block: Optional[str] = None
    for line in out.splitlines():
        ls = line.strip()
        low = ls.lower()
        if ":" in line and (
            low.startswith("name")
            or ls.startswith("名称")
            or ("名称" in ls[:12] and ":" in ls)
        ):
            iface_block = ls.split(":", 1)[1].strip().casefold()
            continue
        if iface_block == want and ls.lower().startswith("ssid") and ":" in line:
            v = ls.split(":", 1)[1].strip()
            if v and v.lower() not in ("none", "n/a", ""):
                return v
    return None


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


def wlan_connect(
    ssid: str,
    password: str,
    interface_name: str,
    *,
    policy_all_adapters: bool = True,
) -> Tuple[bool, str]:
    """
    Connect to WLAN. `netsh wlan connect` does NOT accept key=; we add a temporary WPA2 profile via XML.
    Profile must be added **on the same interface** as connect;先 disconnect 再扫描再连。
    policy_all_adapters=False：仅在本机指定网卡上把其它配置改为手动（用于 STA 保存前校验，减少全局改动）。
    """
    if not IS_WINDOWS or not ssid.strip():
        return False, "invalid ssid"
    iface = interface_name.strip()
    if not iface:
        return False, "需要指定无线网卡名称（与「网络连接」中名称一致）"
    lines: List[str] = []

    # 0) 断开，并把其它已存配置改为「手动」，防止启用网卡后自动连到非测试热点
    dcode, dout = wlan_disconnect(iface)
    lines.append(f"disconnect: exit={dcode} {dout[:120] if dout else ''}".strip())
    if policy_all_adapters:
        nman = wlan_set_other_profiles_manual_all_adapters(ssid)
        if nman:
            lines.append(
                f"已在所有无线接口上将其它 {nman} 个 Wi‑Fi 配置设为手动连接（目标 {ssid!r} 可自动/显式连接）"
            )
    else:
        nman = wlan_set_other_profiles_manual(iface, ssid)
        if nman:
            lines.append(
                f"已在本网卡上将其它 {nman} 个 Wi‑Fi 配置设为手动连接（目标 {ssid!r}）"
            )
    time.sleep(1.0)

    def _verify_ssid_ok() -> bool:
        target = ssid.strip().casefold()
        for attempt in range(4):
            if attempt:
                time.sleep(0.55)
            cur = wlan_get_connected_ssid(iface)
            if cur is None:
                continue
            if cur.casefold() == target:
                return True
            lines.append(f"(SSID 校验: 当前已连 {cur!r}，目标 {ssid!r} — 不匹配)")
            return False
        return True

    # 1) 尝试用已有配置文件连接（仅 name + interface，避免 ssid 重复参数问题）
    args_try = ["netsh", "wlan", "connect", f"name={ssid}", f"interface={iface}"]
    code0, out0 = run_cmd(args_try, timeout_s=25.0, oem_console=True)
    if code0 == 0 and _verify_ssid_ok():
        wlan_ensure_target_profile_auto(iface, ssid)
        return True, "\n".join(lines) + "\n" + (out0 or "connected (existing profile)")
    if code0 == 0:
        lines.append("(已有配置 connect 返回成功但 SSID 非目标，将重新导入 XML)")
    else:
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
        ok2 = code2 == 0 and _verify_ssid_ok()
        if ok2:
            wlan_ensure_target_profile_auto(iface, ssid)
        return ok2, "\n".join(lines)
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def wlan_validate_sta_before_device_save(
    ssid: str,
    password: str,
    interface_name: str,
) -> Tuple[bool, str]:
    """
    将 STA 写入设备前：在本机指定无线网卡上连接该 SSID/密码，并等待关联与 DHCP。
    使用 policy_all_adapters=False，仅改动该网卡上的其它配置为手动，避免与稳定性测试同级的全局策略。
    """
    if not IS_WINDOWS:
        return False, "not windows"
    ok, msg = wlan_connect(ssid, password, interface_name, policy_all_adapters=False)
    if not ok:
        return False, msg
    stop = threading.Event()

    def _noop(_s: str) -> None:
        pass

    ready, rmsg = wlan_wait_until_ready(interface_name, ssid, stop, _noop, "[STA校验]", timeout_s=75.0)
    if not ready:
        return False, f"已尝试连接但链路/DHCP 未就绪：{rmsg}\n\n{msg[:2500]}"
    return True, f"{msg}\nSTA 本机校验：{rmsg}"


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
    # wlan_connect 内会 disconnect，并在所有无线接口上将非目标配置改为手动、保证目标为自动连接
    time.sleep(0.4)
    log(f"[Wi‑Fi] 连接热点 {ssid!r} …")
    wok, wmsg = wlan_connect(ssid, password, cfg.adapter)
    log(f"[Wi‑Fi] 连接结果: {'OK' if wok else 'FAIL'} {wmsg[:1200]}")
    if wok:
        ready, rmsg = wlan_wait_until_ready(cfg.adapter, ssid, stop, log, "[Wi‑Fi]", timeout_s=60.0)
        log(f"[Wi‑Fi] 链路/DHCP 就绪: {'OK' if ready else 'FAIL'} {rmsg}")
        if not ready:
            log("[Wi‑Fi] 重试连接一次…")
            wok2, wmsg2 = wlan_connect(ssid, password, cfg.adapter)
            log(f"[Wi‑Fi] 重试连接: {'OK' if wok2 else 'FAIL'} {wmsg2[:800]}")
            if wok2:
                ready, rmsg = wlan_wait_until_ready(cfg.adapter, ssid, stop, log, "[Wi‑Fi]", timeout_s=45.0)
                log(f"[Wi‑Fi] 重试后链路/DHCP: {'OK' if ready else 'FAIL'} {rmsg}")
            wok = bool(wok2 and ready)
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
        wlan_wifi_test_apply_profile_policy(cfg.adapter, ssid, log)
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
        nauto = wlan_restore_all_profiles_auto_all_adapters()
        if nauto:
            log(f"[Wi‑Fi] 已在所有无线接口上恢复 {nauto} 个 Wi‑Fi 配置为自动连接（测试结束）")
        done()
