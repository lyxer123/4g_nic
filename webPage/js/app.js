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
    blacklist: '/api/users/blacklist',
    traffic: '/api/traffic',
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
      ['当月流量', cell.monthly_traffic || '—'],
      ['USB', cell.usb_probe || '—'],
    ];
    const ce = $('dashCell');
    if (ce) {
      ce.innerHTML = cr.map(([a, b]) => '<dt>' + esc(a) + '</dt><dd>' + esc(b) + '</dd>').join('');
    }
    const ifaces = Array.isArray(d.interfaces) ? d.interfaces : [];
    const ig = $('dashIfaces');
    if (ig) {
      ig.innerHTML = ifaces
        .map(
          (x) =>
            '<div class="iface-card"><h4>' +
            esc(x.name) +
            '</h4><dl>' +
            '<dt>地址</dt><dd>' +
            esc(x.address) +
            '</dd>' +
            '<dt>MAC</dt><dd>' +
            esc(x.mac) +
            '</dd>' +
            '<dt>接收</dt><dd>' +
            esc(x.rx) +
            '</dd>' +
            '<dt>发送</dt><dd>' +
            esc(x.tx) +
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

  async function loadBlacklist() {
    const d = await jfetch(API.blacklist, { method: 'GET' });
    const pre = $('blacklistJson');
    if (pre) pre.textContent = JSON.stringify(d, null, 2);
  }

  async function loadNetworkForm() {
    const d = await jfetch(API.network, { method: 'GET' });
    const wm = $('workingMode');
    if (wm && d.working_mode) wm.value = d.working_mode;
    const lan = d.lan || {};
    if ($('lanIp')) $('lanIp').value = lan.ip || '';
    if ($('lanMask')) $('lanMask').value = lan.mask || '';
    if ($('lanDhcp')) $('lanDhcp').checked = !!lan.dhcp_enabled;
    if ($('lanStart')) $('lanStart').value = lan.dhcp_start || '';
    if ($('lanEnd')) $('lanEnd').value = lan.dhcp_end || '';
    if ($('lanLease')) $('lanLease').value = lan.lease_hours != null ? lan.lease_hours : 12;
    if ($('lanDns1')) $('lanDns1').value = lan.dns1 || '';
    if ($('lanDns2')) $('lanDns2').value = lan.dns2 || '';
    const wan = d.wan || {};
    if ($('wan4g')) $('wan4g').checked = wan.lte_enabled !== false;
    if ($('wanNetMode')) $('wanNetMode').value = wan.network_mode || 'auto';
    toggleStaPanel();
  }

  function toggleStaPanel() {
    const wm = $('workingMode');
    const sec = $('staAdvanced');
    const v = wm && wm.value === 'router';
    if (sec) sec.classList.toggle('hidden', !v);
  }

  async function saveNetwork() {
    const body = {
      working_mode: $('workingMode') ? $('workingMode').value : '4g',
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
      wan: {
        lte_enabled: $('wan4g').checked,
        network_mode: $('wanNetMode').value,
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

  async function loadWifiAp() {
    const d = await jfetch(API.wifiAp, { method: 'GET' });
    if ($('wifiEn')) $('wifiEn').checked = d.wifi_enabled !== false;
    if ($('apSsid')) $('apSsid').value = d.ssid || '';
    if ($('apEnc')) $('apEnc').value = d.encryption_mode || 'WPA2-PSK';
    if ($('apPwd')) $('apPwd').value = '';
    if ($('apHidden')) $('apHidden').checked = !!d.hidden_ssid;
    if ($('apProto')) $('apProto').value = d.protocol || 'auto';
    if ($('apBw')) $('apBw').value = d.bandwidth || 'auto';
    if ($('apCh')) $('apCh').value = d.channel || 'auto';
    if ($('apSig')) $('apSig').value = d.signal_strength || 'auto';
    if ($('apWps')) $('apWps').checked = d.wps_enabled !== false;
    if ($('apWpsPin')) $('apWpsPin').checked = !!d.wps_pin_enabled;
  }

  async function saveWifiAp() {
    const body = {
      wifi_enabled: $('wifiEn').checked,
      ssid: $('apSsid').value.trim(),
      encryption_mode: $('apEnc').value,
      password: $('apPwd').value,
      hidden_ssid: $('apHidden').checked,
      protocol: $('apProto').value,
      bandwidth: $('apBw').value,
      channel: $('apCh').value,
      signal_strength: $('apSig').value,
      wps_enabled: $('apWps').checked,
      wps_pin_enabled: $('apWpsPin').checked,
    };
    await jfetch(API.wifiAp, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    toast('WiFi 配置已保存');
  }

  async function loadTraffic() {
    const d = await jfetch(API.traffic, { method: 'GET' });
    if ($('trafficEn')) $('trafficEn').checked = !!d.traffic_enabled;
    if ($('trafficNote')) $('trafficNote').textContent = d.note || '';
    if ($('headerTrafficToggle')) $('headerTrafficToggle').checked = !!d.traffic_enabled;
  }

  async function saveTraffic() {
    await jfetch(API.traffic, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ traffic_enabled: $('trafficEn').checked }),
    });
    if ($('headerTrafficToggle')) $('headerTrafficToggle').checked = $('trafficEn').checked;
    toast('已保存');
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

  function onNav(page) {
    showPage(page);
    const loaders = {
      overview: loadOverview,
      users: loadUsers,
      blacklist: loadBlacklist,
      network: loadNetworkForm,
      apn: loadApn,
      wifi: loadWifiAp,
      traffic: loadTraffic,
      password: null,
      systime: loadSysTime,
      upgrade: null,
      logs: loadLogs,
      probes: loadProbes,
      reboot: loadRebootSched,
      wizard: null,
    };
    const fn = loaders[page];
    if (fn) {
      fn().catch((e) => toast(e.message || String(e), true));
    }
  }

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
  $('workingMode') && $('workingMode').addEventListener('change', toggleStaPanel);
  $('btnSaveSta') && $('btnSaveSta').addEventListener('click', () => saveSta().catch((e) => toast(e.message, true)));
  $('btnLoadSta') && $('btnLoadSta').addEventListener('click', () => loadSta().catch((e) => toast(e.message, true)));
  $('btnScanSta') && $('btnScanSta').addEventListener('click', () => scanSta().catch((e) => toast(e.message, true)));
  $('btnClearSta') && $('btnClearSta').addEventListener('click', () => clearSta().catch((e) => toast(e.message, true)));

  $('btnSaveApn') && $('btnSaveApn').addEventListener('click', () => saveApn().catch((e) => toast(e.message, true)));
  $('btnSaveWifi') && $('btnSaveWifi').addEventListener('click', () => saveWifiAp().catch((e) => toast(e.message, true)));
  $('btnSaveTraffic') && $('btnSaveTraffic').addEventListener('click', () => saveTraffic().catch((e) => toast(e.message, true)));
  $('headerTrafficToggle') &&
    $('headerTrafficToggle').addEventListener('change', () => {
      if ($('trafficEn')) $('trafficEn').checked = $('headerTrafficToggle').checked;
      saveTraffic().catch((e) => toast(e.message, true));
    });

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

  showPage('overview');
  loadOverview().catch(() => {});
  loadTraffic().catch(() => {});
})();
