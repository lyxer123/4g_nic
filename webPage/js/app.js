(function () {
  'use strict';

  const API = {
    dashboard: '/api/dashboard/overview',
    network: '/api/network/config',
    apn: '/api/network/apn',
    wifiAp: '/api/wifi/ap',
    wifiSta: '/api/wifi',
    wifiScan: '/api/wifi/scan',
    wifiClear: '/api/wifi/clear',
    mode: '/api/mode',
    modeApply: '/api/mode/apply',
    ethWan: '/api/eth_wan',
    probes: '/api/system/probes',
    time: '/api/system/time',
    syncTime: '/api/system/sync_time',
    reboot: '/api/system/reboot',
    rbSched: '/api/system/reboot/schedule',
    logs: '/api/system/logs',
    password: '/api/system/password',
    factory: '/api/system/factory_reset',
    cfgExport: '/api/system/config/export',
    cfgImport: '/api/system/config/import',
    usersOnline: '/api/users/online',
  };

  function $(id) {
    return document.getElementById(id);
  }

  function toast(msg, isErr) {
    const t = $('toast');
    const b = $('toastBody');
    if (!t || !b) return;
    b.textContent = msg;
    t.classList.toggle('error', !!isErr);
    t.hidden = false;
    clearTimeout(toast._tm);
    toast._tm = setTimeout(() => {
      t.hidden = true;
    }, 2600);
  }

  async function jfetch(url, opt) {
    const res = await fetch(url, opt);
    const txt = await res.text();
    let data = null;
    try {
      data = txt ? JSON.parse(txt) : null;
    } catch (_) {
      data = { raw: txt };
    }
    if (!res.ok) {
      const m = (data && (data.message || data.status)) || res.statusText;
      throw new Error(m || 'HTTP ' + res.status);
    }
    return data;
  }

  function showPage(id) {
    document.querySelectorAll('.page').forEach((p) => {
      p.classList.toggle('hidden', p.id !== 'page-' + id);
    });
    document.querySelectorAll('.nav-item').forEach((n) => {
      n.classList.toggle('active', n.getAttribute('data-page') === id);
    });
    const sec = document.querySelector('.page#page-' + id);
    const title = sec ? sec.getAttribute('data-title') : '';
    const pt = $('pageTitle');
    if (pt && title) pt.textContent = title;
  }

  function esc(s) {
    const d = document.createElement('div');
    d.textContent = s == null ? '' : String(s);
    return d.innerHTML;
  }

  function ifaceLooksUp(x) {
    if (!x) return false;
    if (x.up === true) return true;
    const addr = String(x.address || '').trim();
    return addr !== '' && addr !== '--' && addr !== '0.0.0.0';
  }

  function barsFromSignal(raw) {
    if (raw == null) return '▮▮▮▯';
    const s = String(raw).trim();
    if (!s || s === '--' || s === '—') return '▮▮▮▯';
    const n = parseInt(s, 10);
    if (!Number.isFinite(n)) return '▮▮▮▯';
    if (n >= -70) return '▮▮▮▮';
    if (n >= -85) return '▮▮▮▯';
    if (n >= -100) return '▮▮▯▯';
    return '▮▯▯▯';
  }

  async function loadOverview() {
    const d = await jfetch(API.dashboard, { method: 'GET' });
    const sys = d.system || {};
    const rows = [
      ['系统模式', sys.system_mode || '—'],
      ['型号', '4G_NIC'],
      ['版本', sys.firmware_version || '—'],
      ['CPU', sys.cpu_percent != null ? sys.cpu_percent + '%' : '—'],
      ['系统时间', sys.system_time || '—'],
      ['内存', sys.memory_percent != null ? sys.memory_percent + '%' : '—'],
      ['运行时间', fmtDuration(sys.uptime_s)],
      ['在线用户数', String(sys.online_users != null ? sys.online_users : '—')],
    ];
    const el = $('dashSystem');
    if (el) {
      el.innerHTML = rows.map(([a, b]) => '<dt>' + esc(a) + '</dt><dd>' + esc(b) + '</dd>').join('');
    }
    const cell = d.cellular || {};
    const cr = [
      ['运营商', cell.operator || '—'],
      ['网络模式', cell.network_mode || '—'],
      ['IMEI', cell.imei || '—'],
      ['ICCID', cell.iccid || '—'],
      ['网络信号', cell.signal || '—'],
      ['USB', cell.usb_probe || '—'],
    ];
    const ce = $('dashCell');
    if (ce) {
      ce.innerHTML = cr.map(([a, b]) => '<dt>' + esc(a) + '</dt><dd>' + esc(b) + '</dd>').join('');
    }
    const ifaces = Array.isArray(d.interfaces) ? d.interfaces : [];
    const byName = {};
    for (let i = 0; i < ifaces.length; i++) {
      byName[String(ifaces[i].name || '')] = ifaces[i];
    }

    const cards = [];
    const cellularBars = barsFromSignal(cell.signal);
    const ppp = byName.wanrelay;
    if (ppp && ifaceLooksUp(ppp)) {
      cards.push({ ...ppp, name: '4G', signalBars: cellularBars });
    }

    const ethWan = byName.eth_wan;
    const ethLan = byName.lan_eth;
    if (ifaceLooksUp(ethWan)) {
      cards.push({ ...ethWan, name: 'ETH_WAN' });
    } else if (ifaceLooksUp(ethLan)) {
      cards.push({ ...ethLan, name: 'ETH_LAN' });
    }

    const sta = byName.sta;
    if (sta) {
      cards.push({ ...sta, name: 'WIFI_STA', signalBars: '▮▮▮▯' });
    }
    const ap = byName.lanrelay;
    if (ap) {
      cards.push({ ...ap, name: 'WIFI_AP' });
    }

    const ig = $('dashIfaces');
    if (ig) {
      ig.innerHTML = cards
        .map(
          (x) =>
            '<div class="iface-card"><div class="iface-card-head"><h4>' +
            esc(x.name) +
            '</h4>' +
            ((x.name === '4G' || x.name === 'WIFI_STA') ? '<span class="iface-signal" title="信号强度">' + esc(x.signalBars || '▮▮▮▯') + '</span>' : '') +
            '</div><dl>' +
            '<dt>地址</dt><dd>' +
            esc(x.address) +
            '</dd>' +
            '<dt>MAC</dt><dd>' +
            esc(x.mac) +
            '</dd></dl></div>'
        )
        .join('');
    }
  }

  function fmtDuration(sec) {
    if (sec == null || sec === '') return '—';
    const s = Math.floor(Number(sec));
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const r = s % 60;
    return h + '时 ' + m + '分 ' + r + '秒';
  }

  async function loadUsers() {
    const d = await jfetch(API.usersOnline, { method: 'GET' });
    const users = Array.isArray(d.users) ? d.users : [];
    const tb = document.querySelector('#tableUsers tbody');
    if (tb) {
      tb.innerHTML = users
        .map(
          (u, i) =>
            '<tr><td>' +
            esc(u.id != null ? u.id : i + 1) +
            '</td><td>' +
            esc(u.hostname) +
            '</td><td>' +
            esc(u.online_duration) +
            '</td><td>' +
            esc(u.ip_address) +
            '</td><td>' +
            esc(u.mac_address) +
            '</td><td>—</td><td>—</td></tr>'
        )
        .join('');
    }
    const foot = $('usersFoot');
    if (foot) {
      foot.textContent = '共 ' + (d.total != null ? d.total : users.length) + ' 条';
    }
  }

  const WAN_TYPE_LABEL = {
    0: '无上行（仅配网 / 热点）',
    1: '4G / USB 蜂窝（外网）',
    2: 'Wi‑Fi 连上级路由（STA 外网）',
    3: 'RJ45 W5500 作外网（WAN）',
  };

  let cachedModePayload = null;

  function wanTypesFromModes(modes) {
    const s = new Set();
    for (let i = 0; i < modes.length; i++) {
      s.add(Number(modes[i].wan_type));
    }
    const all = Array.from(s).sort((a, b) => a - b);
    const visible = all.filter((t) => t !== 0);
    return visible.length ? visible : all;
  }

  function modesForWanType(modes, wt) {
    return modes.filter((m) => Number(m.wan_type) === Number(wt));
  }

  function lanSummaryLabel(m) {
    const parts = [];
    if (m.lan_softap) parts.push('Wi‑Fi 热点');
    if (m.lan_eth) parts.push('W5500 作内网(LAN)');
    if (!parts.length) parts.push('无下行');
    const title = m.label != null ? String(m.label) : '模式';
    return title + ' — ' + parts.join(' + ');
  }

  function fillWanSelect(modes, wanOptions) {
    const sel = $('selWanType');
    if (!sel) return;
    const types = wanTypesFromModes(modes);
    const optMap = {};
    if (Array.isArray(wanOptions)) {
      for (let i = 0; i < wanOptions.length; i++) {
        const w = wanOptions[i];
        optMap[Number(w.wan_type)] = w;
      }
    }
    if (!types.length) {
      sel.innerHTML = '<option value="">— 无可用模式 —</option>';
      return;
    }
    sel.innerHTML = types
      .map((t) => {
        const o = optMap[t];
        const note = o && o.reason_code && o.reason_code !== 'ok' ? '（' + o.reason_code + '）' : '';
        return '<option value="' + t + '">' + esc((WAN_TYPE_LABEL[t] || 'WAN ' + t) + note) + '</option>';
      })
      .join('');
  }

  function fillModeSelect(modes, wt) {
    const sel = $('selWorkMode');
    if (!sel) return;
    const list = modesForWanType(modes, wt);
    if (!list.length) {
      sel.innerHTML = '<option value="">—</option>';
      return;
    }
    sel.innerHTML = list
      .map((m) => '<option value="' + m.id + '">' + esc(lanSummaryLabel(m)) + '</option>')
      .join('');
  }

  function setHwHint(hw) {
    const el = $('hwProbeHint');
    if (!el) return;
    if (!hw) {
      el.textContent = '';
      return;
    }
    const bits = [];
    bits.push('本机 Wi‑Fi 已具备');
    bits.push('W5500：' + (hw.w5500 ? '已检测到' : '未检测到'));
    bits.push('USB 蜂窝：' + (hw.usb_modem_present ? '已检测到（' + (hw.usb_ids || '') + '）' : '未检测到'));
    el.textContent = bits.join(' · ');
  }

  function selectedModeRow() {
    const sel = $('selWorkMode');
    if (!sel || !sel.value || !cachedModePayload || !Array.isArray(cachedModePayload.modes)) {
      return null;
    }
    const id = parseInt(sel.value, 10);
    return cachedModePayload.modes.find((m) => Number(m.id) === id) || null;
  }

  async function loadEthWanFields() {
    try {
      const ew = await jfetch(API.ethWan, { method: 'GET' });
      if ($('ethWanDhcp')) $('ethWanDhcp').checked = ew.dhcp !== false;
      if ($('ethWanIp')) $('ethWanIp').value = ew.ip || '';
      if ($('ethWanMask')) $('ethWanMask').value = ew.mask || '';
      if ($('ethWanGw')) $('ethWanGw').value = ew.gw || '';
      if ($('ethWanDns1')) $('ethWanDns1').value = ew.dns1 || '';
      if ($('ethWanDns2')) $('ethWanDns2').value = ew.dns2 || '';
    } catch (_) {}
  }

  function buildEthWanBody() {
    return {
      dhcp: $('ethWanDhcp') ? $('ethWanDhcp').checked : true,
      ip: $('ethWanIp') ? $('ethWanIp').value.trim() : '',
      mask: $('ethWanMask') ? $('ethWanMask').value.trim() : '',
      gw: $('ethWanGw') ? $('ethWanGw').value.trim() : '',
      dns1: $('ethWanDns1') ? $('ethWanDns1').value.trim() : '',
      dns2: $('ethWanDns2') ? $('ethWanDns2').value.trim() : '',
    };
  }

  async function saveEthWanOnly() {
    await jfetch(API.ethWan, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(buildEthWanBody()),
    });
    try {
      await jfetch(API.modeApply, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({}),
      });
    } catch (_) {}
    toast('有线 WAN 已写入，并已按当前工作模式重试应用');
  }

  async function loadNetworkForm() {
    const [d, md] = await Promise.all([jfetch(API.network, { method: 'GET' }), jfetch(API.mode, { method: 'GET' })]);
    await loadEthWanFields();
    await loadLanWifiFields();
    cachedModePayload = md;
    const modes = Array.isArray(md.modes) ? md.modes : [];
    setHwHint(md.hardware);

    fillWanSelect(modes, md.wan_options);
    const curId = d.work_mode_id != null ? Number(d.work_mode_id) : Number(md.current) || 0;
    let curMode = modes.find((m) => Number(m.id) === curId);
    let wanT = curMode ? Number(curMode.wan_type) : d.wan_type != null ? Number(d.wan_type) : null;
    const types = wanTypesFromModes(modes);
    if (wanT == null && types.length) {
      wanT = types[0];
    }
    if (wanT != null && !types.includes(wanT) && types.length) {
      wanT = types[0];
    }
    const wanSel = $('selWanType');
    if (wanSel && wanT != null) {
      wanSel.value = String(wanT);
    }
    if (wanT != null) {
      fillModeSelect(modes, wanT);
    }
    const modeSel = $('selWorkMode');
    if (modeSel && wanT != null) {
      const sub = modesForWanType(modes, wanT);
      const still = sub.some((m) => Number(m.id) === curId);
      const pick = still ? curId : sub[0] ? Number(sub[0].id) : '';
      if (pick !== '') {
        modeSel.value = String(pick);
      }
    }
    const lan = d.lan || {};
    if ($('lanIp')) $('lanIp').value = lan.ip || '';
    if ($('lanMask')) $('lanMask').value = lan.mask || '';
    if ($('lanDhcp')) $('lanDhcp').checked = !!lan.dhcp_enabled;
    if ($('lanStart')) $('lanStart').value = lan.dhcp_start || '';
    if ($('lanEnd')) $('lanEnd').value = lan.dhcp_end || '';
    if ($('lanLease')) $('lanLease').value = lan.lease_hours != null ? lan.lease_hours : 12;
    if ($('lanDns1')) $('lanDns1').value = lan.dns1 || '';
    if ($('lanDns2')) $('lanDns2').value = lan.dns2 || '';
    toggleLanPanels();
    toggleEthWanPanel();
  }

  async function loadLanWifiFields() {
    try {
      const d = await jfetch(API.wifiAp, { method: 'GET' });
      if ($('lanApSsid')) $('lanApSsid').value = d.ssid || '';
      if ($('lanApEnc')) $('lanApEnc').value = d.encryption_mode || 'WPA2-PSK';
      if ($('lanApPwd')) $('lanApPwd').value = '';
      if ($('lanApHidden')) $('lanApHidden').checked = !!d.hidden_ssid;
    } catch (_) {}
  }

  function toggleLanPanels() {
    const row = selectedModeRow();
    const ethCard = $('lanEthCard');
    const comboCard = $('wirelessComboCard');
    const staSection = $('staSection');
    const wifiSection = $('wifiLanSection');
    const divider = $('wirelessSectionDivider');
    const showEth = row ? row.lan_eth === true : true;
    const showSta = row ? row.needs_sta === true : false;
    const showWifi = row ? row.lan_softap === true : false;
    if (ethCard) ethCard.classList.toggle('hidden', !showEth);
    if (comboCard) comboCard.classList.toggle('hidden', !(showSta || showWifi));
    if (staSection) staSection.classList.toggle('hidden', !showSta);
    if (wifiSection) wifiSection.classList.toggle('hidden', !showWifi);
    if (divider) divider.classList.toggle('hidden', !(showSta && showWifi));
    updateNetworkCardsGrid();
  }

  function toggleEthWanPanel() {
    const row = selectedModeRow();
    const sec = $('ethWanPanel');
    const v = row && row.needs_eth_wan === true;
    if (sec) sec.classList.toggle('hidden', !v);
    updateNetworkCardsGrid();
  }

  function updateNetworkCardsGrid() {
    const grid = $('networkCardsGrid');
    if (!grid) return;
    const ids = ['ethWanPanel', 'wirelessComboCard', 'lanEthCard'];
    let visible = 0;
    for (let i = 0; i < ids.length; i++) {
      const el = $(ids[i]);
      if (el && !el.classList.contains('hidden')) {
        visible += 1;
      }
    }
    grid.classList.toggle('single-wide', visible <= 1);
  }

  async function saveNetwork() {
    const wmSel = $('selWorkMode');
    const workId = wmSel && wmSel.value ? parseInt(wmSel.value, 10) : NaN;
    if (!Number.isFinite(workId)) {
      toast('请选择 LAN/工作模式', true);
      return;
    }
    const rowPre = selectedModeRow();
    if (rowPre && rowPre.lan_softap === true) {
      await jfetch(API.wifiAp, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          wifi_enabled: true,
          ssid: $('lanApSsid') ? $('lanApSsid').value.trim() : '',
          encryption_mode: $('lanApEnc') ? $('lanApEnc').value : 'WPA2-PSK',
          password: $('lanApPwd') ? $('lanApPwd').value : '',
          hidden_ssid: $('lanApHidden') ? $('lanApHidden').checked : false,
        }),
      });
    }
    if (rowPre && rowPre.needs_eth_wan) {
      await jfetch(API.ethWan, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(buildEthWanBody()),
      });
    }
    const body = {
      work_mode_id: workId,
      wan_type: parseInt($('selWanType').value, 10),
      lan: {
        ip: $('lanIp').value.trim(),
        mask: $('lanMask').value.trim(),
        dhcp_enabled: $('lanDhcp').checked,
        dhcp_start: $('lanStart').value.trim(),
        dhcp_end: $('lanEnd').value.trim(),
        lease_hours: parseInt($('lanLease').value, 10) || 12,
        dns1: $('lanDns1').value.trim(),
        dns2: $('lanDns2').value.trim(),
      },
    };
    await jfetch(API.network, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    toast('网络配置已保存');
  }

  async function loadSta() {
    const d = await jfetch(API.wifiSta, { method: 'GET' });
    if ($('staSsid')) $('staSsid').value = d.ssid || '';
    if ($('staPwd')) $('staPwd').value = d.password || '';
    toast('已读取 STA');
  }

  async function saveSta() {
    const ssid = $('staSsid').value.trim();
    const password = $('staPwd').value;
    if (!ssid) {
      toast('请填写 SSID', true);
      return;
    }
    await jfetch(API.wifiSta, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password }),
    });
    toast('STA 已保存');
  }

  async function clearSta() {
    await jfetch(API.wifiClear, { method: 'POST' });
    if ($('staSsid')) $('staSsid').value = '';
    if ($('staPwd')) $('staPwd').value = '';
    toast('STA 已清空');
  }

  async function scanSta() {
    const pop = $('ssidPop');
    const d = await jfetch(API.wifiScan, { method: 'GET' });
    const aps = Array.isArray(d.aps) ? d.aps : [];
    if (pop) {
      pop.hidden = false;
      pop.innerHTML = aps
        .map(
          (a) =>
            '<li data-ssid="' + esc(a.ssid).replace(/"/g, '&quot;') + '">' + esc(a.ssid) + ' (' + esc(a.rssi) + ')</li>'
        )
        .join('');
      pop.querySelectorAll('li[data-ssid]').forEach((li) => {
        li.addEventListener('click', () => {
          $('staSsid').value = li.getAttribute('data-ssid') || '';
          pop.hidden = true;
        });
      });
    }
  }

  async function loadApn() {
    const d = await jfetch(API.apn, { method: 'GET' });
    if ($('apnName')) $('apnName').value = d.apn || '';
    if ($('apnUser')) $('apnUser').value = d.username || '';
    if ($('apnPass')) $('apnPass').value = d.password || '';
  }

  async function saveApn() {
    await jfetch(API.apn, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        apn: $('apnName').value.trim(),
        username: $('apnUser').value.trim(),
        password: $('apnPass').value,
      }),
    });
    toast('APN 已保存');
  }

  async function loadProbes() {
    const d = await jfetch(API.probes, { method: 'GET' });
    if ($('p1')) $('p1').value = d.detection_ip1 || '';
    if ($('p2')) $('p2').value = d.detection_ip2 || '';
    if ($('p3')) $('p3').value = d.detection_ip3 || '';
    if ($('p4')) $('p4').value = d.detection_ip4 || '';
  }

  async function saveProbes() {
    await jfetch(API.probes, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        detection_ip1: $('p1').value.trim(),
        detection_ip2: $('p2').value.trim(),
        detection_ip3: $('p3').value.trim(),
        detection_ip4: $('p4').value.trim(),
      }),
    });
    toast('探测地址已保存');
  }

  async function loadSysTime() {
    const d = await jfetch(API.time, { method: 'GET' });
    if ($('sysTimeDisp')) $('sysTimeDisp').value = d.system_time || '';
    if ($('fwVerDisp')) $('fwVerDisp').value = d.firmware_version || '';
    if ($('tzSelect') && d.timezone) {
      const tz = $('tzSelect');
      let found = false;
      for (let i = 0; i < tz.options.length; i++) {
        if (tz.options[i].value === d.timezone || tz.options[i].textContent.indexOf(d.timezone) >= 0) {
          tz.selectedIndex = i;
          found = true;
          break;
        }
      }
      if (!found) {
        const o = document.createElement('option');
        o.value = d.timezone;
        o.textContent = d.timezone;
        tz.appendChild(o);
        tz.value = d.timezone;
      }
    }
  }

  async function saveSysTime() {
    await jfetch(API.time, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ timezone: $('tzSelect').value }),
    });
    toast('时区已保存');
  }

  async function syncLocalTime() {
    await jfetch(API.syncTime, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ local_timestamp_ms: Date.now() }),
    });
    toast('已同步本地时间');
    loadSysTime();
  }

  async function loadLogs() {
    const d = await jfetch(API.logs, { method: 'GET' });
    const logs = Array.isArray(d.logs) ? d.logs : [];
    if ($('logView')) $('logView').textContent = logs.join('\n') || '(无)';
  }

  async function clearLogs() {
    await jfetch(API.logs, { method: 'DELETE' });
    toast('日志已清空');
    loadLogs();
  }

  async function savePwd() {
    const a = $('pwdNew').value;
    const b = $('pwdNew2').value;
    if (a !== b) {
      toast('两次新密码不一致', true);
      return;
    }
    await jfetch(API.password, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        old_password: $('pwdOld').value,
        new_password: a,
      }),
    });
    toast('密码已更新');
  }

  async function doFactory() {
    if (!window.confirm('确定恢复出厂？将清除 NVS 中的配置。')) return;
    await jfetch(API.factory, { method: 'POST' });
    toast('已执行复位，请重启设备');
  }

  async function exportCfg() {
    const d = await jfetch(API.cfgExport, { method: 'GET' });
    const blob = new Blob([JSON.stringify(d, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'router-config.json';
    a.click();
    URL.revokeObjectURL(a.href);
    toast('已下载备份');
  }

  async function importCfg() {
    const txt = $('importCfgText').value.trim();
    if (!txt) {
      toast('请粘贴 JSON', true);
      return;
    }
    const obj = JSON.parse(txt);
    await jfetch(API.cfgImport, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(obj),
    });
    toast('导入成功');
  }

  async function loadRebootSched() {
    const d = await jfetch(API.rbSched, { method: 'GET' });
    if ($('schEn')) $('schEn').checked = !!d.enabled;
    if ($('schH')) $('schH').value = d.hour != null ? d.hour : 3;
    if ($('schM')) $('schM').value = d.minute != null ? d.minute : 30;
  }

  async function saveRebootSched() {
    await jfetch(API.rbSched, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        enabled: $('schEn').checked,
        hour: parseInt($('schH').value, 10),
        minute: parseInt($('schM').value, 10),
      }),
    });
    toast('定时重启已保存');
  }

  async function doReboot() {
    if (!window.confirm('确定立即重启设备？')) return;
    try {
      await fetch(API.reboot, { method: 'POST' });
    } catch (_) {}
    toast('设备重启中…');
  }

  function isMobileLayout() {
    return window.matchMedia('(max-width: 899px)').matches;
  }

  function setSidebarOpen(open) {
    document.body.classList.toggle('sidebar-open', !!open);
    const bt = $('menuToggle');
    const bd = $('sidebarBackdrop');
    if (bt) {
      bt.setAttribute('aria-expanded', open ? 'true' : 'false');
      bt.setAttribute('aria-label', open ? '关闭导航菜单' : '打开导航菜单');
    }
    if (bd) {
      bd.setAttribute('aria-hidden', open ? 'false' : 'true');
    }
  }

  function closeSidebarMobile() {
    if (isMobileLayout()) {
      setSidebarOpen(false);
    }
  }

  function toggleSidebarMobile() {
    if (!isMobileLayout()) {
      return;
    }
    setSidebarOpen(!document.body.classList.contains('sidebar-open'));
  }

  function debounce(fn, ms) {
    let t = null;
    return function () {
      clearTimeout(t);
      const a = arguments;
      const self = this;
      t = setTimeout(function () {
        fn.apply(self, a);
      }, ms);
    };
  }

  function onNav(page) {
    showPage(page);
    closeSidebarMobile();
    const loaders = {
      overview: loadOverview,
      users: loadUsers,
      network: loadNetworkForm,
      apn: loadApn,
      password: null,
      systime: loadSysTime,
      upgrade: null,
      logs: loadLogs,
      probes: loadProbes,
      reboot: loadRebootSched,
    };
    const fn = loaders[page];
    if (fn) {
      fn().catch((e) => toast(e.message || String(e), true));
    }
  }

  $('menuToggle') &&
    $('menuToggle').addEventListener('click', () => {
      toggleSidebarMobile();
    });

  $('sidebarBackdrop') &&
    $('sidebarBackdrop').addEventListener('click', () => {
      closeSidebarMobile();
    });

  window.addEventListener(
    'resize',
    debounce(function () {
      if (!isMobileLayout()) {
        setSidebarOpen(false);
      }
    }, 150)
  );

  document.addEventListener('keydown', (ev) => {
    if (ev.key === 'Escape') {
      closeSidebarMobile();
    }
  });

  document.querySelectorAll('.nav-item[data-page]').forEach((btn) => {
    btn.addEventListener('click', () => onNav(btn.getAttribute('data-page')));
  });

  document.querySelectorAll('[data-goto]').forEach((btn) => {
    btn.addEventListener('click', () => onNav(btn.getAttribute('data-goto')));
  });

  document.querySelectorAll('.nav-group-title[data-toggle]').forEach((t) => {
    t.addEventListener('click', () => {
      const id = t.getAttribute('data-toggle');
      const el = document.getElementById(id);
      if (el) el.style.display = el.style.display === 'none' ? 'block' : 'none';
    });
  });

  $('btnRefreshOverview') && $('btnRefreshOverview').addEventListener('click', () => loadOverview().catch((e) => toast(e.message, true)));
  $('btnRefreshUsers') && $('btnRefreshUsers').addEventListener('click', () => loadUsers().catch((e) => toast(e.message, true)));
  $('btnSaveNetwork') && $('btnSaveNetwork').addEventListener('click', () => saveNetwork().catch((e) => toast(e.message, true)));
  $('selWanType') &&
    $('selWanType').addEventListener('change', () => {
      const modes = cachedModePayload && Array.isArray(cachedModePayload.modes) ? cachedModePayload.modes : [];
      const wt = parseInt($('selWanType').value, 10);
      if (Number.isFinite(wt)) {
        fillModeSelect(modes, wt);
      }
      toggleLanPanels();
      toggleEthWanPanel();
    });
  $('selWorkMode') &&
    $('selWorkMode').addEventListener('change', () => {
      toggleLanPanels();
      toggleEthWanPanel();
    });
  $('btnSaveEthWan') && $('btnSaveEthWan').addEventListener('click', () => saveEthWanOnly().catch((e) => toast(e.message, true)));
  $('btnSaveSta') && $('btnSaveSta').addEventListener('click', () => saveSta().catch((e) => toast(e.message, true)));
  $('btnLoadSta') && $('btnLoadSta').addEventListener('click', () => loadSta().catch((e) => toast(e.message, true)));
  $('btnScanSta') && $('btnScanSta').addEventListener('click', () => scanSta().catch((e) => toast(e.message, true)));
  $('btnClearSta') && $('btnClearSta').addEventListener('click', () => clearSta().catch((e) => toast(e.message, true)));

  $('btnSaveApn') && $('btnSaveApn').addEventListener('click', () => saveApn().catch((e) => toast(e.message, true)));

  $('btnSavePwd') && $('btnSavePwd').addEventListener('click', () => savePwd().catch((e) => toast(e.message, true)));
  $('btnSaveTime') && $('btnSaveTime').addEventListener('click', () => saveSysTime().catch((e) => toast(e.message, true)));
  $('btnSyncLocal') && $('btnSyncLocal').addEventListener('click', () => syncLocalTime().catch((e) => toast(e.message, true)));
  $('btnRefreshLogs') && $('btnRefreshLogs').addEventListener('click', () => loadLogs().catch((e) => toast(e.message, true)));
  $('btnClearLogs') && $('btnClearLogs').addEventListener('click', () => clearLogs().catch((e) => toast(e.message, true)));
  $('btnSaveProbes') && $('btnSaveProbes').addEventListener('click', () => saveProbes().catch((e) => toast(e.message, true)));
  $('btnRebootNow') && $('btnRebootNow').addEventListener('click', () => doReboot());
  $('btnSaveSchedule') && $('btnSaveSchedule').addEventListener('click', () => saveRebootSched().catch((e) => toast(e.message, true)));
  $('btnFactory') && $('btnFactory').addEventListener('click', () => doFactory().catch((e) => toast(e.message, true)));
  $('btnExportCfg') && $('btnExportCfg').addEventListener('click', () => exportCfg().catch((e) => toast(e.message, true)));
  $('btnImportCfg') &&
    $('btnImportCfg').addEventListener('click', () => importCfg().catch((e) => toast(e.message, true)));
  $('btnLogout') &&
    $('btnLogout').addEventListener('click', () => {
      toast('本地页面无会话，关闭浏览器即可。');
    });

  const LS_SIDEBAR_COLLAPSED = 'sidebarCollapsed';

  function updateSidebarToggleUi() {
    const btn = $('sidebarToggle');
    if (!btn) {
      return;
    }
    const collapsed = document.body.classList.contains('sidebar-collapsed');
    btn.textContent = collapsed ? '▶' : '◀';
    btn.title = collapsed ? '展开菜单' : '收起菜单';
    btn.setAttribute('aria-label', collapsed ? '展开侧栏' : '收起侧栏');
  }

  function setSidebarCollapsed(collapsed, persist) {
    document.body.classList.toggle('sidebar-collapsed', !!collapsed);
    updateSidebarToggleUi();
    if (persist) {
      try {
        localStorage.setItem(LS_SIDEBAR_COLLAPSED, collapsed ? '1' : '0');
      } catch (_) {}
    }
  }

  function syncSidebarLayout() {
    const desktop = window.matchMedia('(min-width: 900px)').matches;
    const btn = $('sidebarToggle');
    if (btn) {
      btn.setAttribute('aria-hidden', desktop ? 'false' : 'true');
    }
    if (!desktop) {
      document.body.classList.remove('sidebar-collapsed');
      updateSidebarToggleUi();
      return;
    }
    try {
      setSidebarCollapsed(localStorage.getItem(LS_SIDEBAR_COLLAPSED) === '1', false);
    } catch (_) {}
  }

  $('sidebarToggle') &&
    $('sidebarToggle').addEventListener('click', () => {
      const next = !document.body.classList.contains('sidebar-collapsed');
      setSidebarCollapsed(next, true);
    });

  window.addEventListener('resize', debounce(syncSidebarLayout, 150));
  syncSidebarLayout();

  showPage('overview');
  loadOverview().catch(() => {});
})();
