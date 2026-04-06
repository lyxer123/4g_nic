(function () {
  'use strict';

  const API = {
    scan: '/api/wifi/scan',
    wifi: '/api/wifi',
    mode: '/api/mode',
    overview: '/api/system/overview',
  };

  const LS_KEYS = {
    sta: 'staConfig',
  };

  /** 与固件 work_mode id 一致；用于界面中文说明 */
  const MODE_LABEL_ZH = {
    1: '4G(Cat1) → Wi-Fi 热点',
    2: '4G(Cat1) → W5500 以太网(LAN)',
    3: '4G(Cat1) → Wi-Fi + W5500',
    4: 'Wi-Fi STA → 热点',
    5: 'Wi-Fi STA → W5500(LAN)',
    6: 'Wi-Fi STA → Wi-Fi + W5500',
    7: 'W5500(WAN) → 仅热点',
  };

  const MODE_NET_ZH = {
    1: { wan: 'USB 4G Cat1（PPP 等，由固件拨号）', lan: 'Wi-Fi SoftAP' },
    2: { wan: 'USB 4G Cat1', lan: 'W5500 作为下行以太网' },
    3: { wan: 'USB 4G Cat1', lan: 'Wi-Fi SoftAP 与 W5500 同时作为 LAN' },
    4: { wan: 'Wi-Fi STA 连接上级路由', lan: 'Wi-Fi SoftAP' },
    5: { wan: 'Wi-Fi STA', lan: 'W5500 作为下行以太网' },
    6: { wan: 'Wi-Fi STA', lan: 'SoftAP + W5500 同时作为 LAN' },
    7: { wan: 'W5500 作为上行 WAN', lan: '仅 Wi-Fi SoftAP（无 W5500 LAN 转发）' },
  };

  function $(id) {
    return document.getElementById(id);
  }

  function setStatus(ok, text) {
    const dot = document.querySelector('.status-dot');
    const t = $('connectionText');
    if (dot) dot.classList.toggle('ok', !!ok);
    if (t) t.textContent = text || (ok ? '已连接' : '未连接');
  }

  let toastTimer = null;
  function toast(msg, isError) {
    const box = $('toast');
    const content = $('toastContent');
    if (!box || !content) return;
    content.textContent = String(msg || '');
    box.classList.toggle('error', !!isError);
    box.style.display = 'block';
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      box.style.display = 'none';
    }, 2500);
  }

  function escapeHtml(s) {
    return String(s || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function setResultBoxVisible(visible) {
    const box = $('resultBox');
    if (box) box.style.display = visible ? 'block' : 'none';
  }

  function showSavedPreview(ssid, pwd) {
    const s = $('savedSsid');
    const p = $('savedPwd');
    if (s) s.textContent = ssid ? ssid : '—';
    if (p) p.textContent = pwd ? '******' : '—';
    setResultBoxVisible(true);
  }

  function getForm() {
    const ssid = $('wifiSsidInput') ? $('wifiSsidInput').value.trim() : '';
    const password = $('wifiPasswordInput') ? $('wifiPasswordInput').value : '';
    return { ssid, password };
  }

  function setForm(ssid, password) {
    const s = $('wifiSsidInput');
    const p = $('wifiPasswordInput');
    if (s) s.value = ssid || '';
    if (p) p.value = password || '';
  }

  function loadFromLocalStorage() {
    try {
      const raw = localStorage.getItem(LS_KEYS.sta);
      if (!raw) return null;
      const data = JSON.parse(raw);
      if (!data || typeof data !== 'object') return null;
      return {
        ssid: typeof data.ssid === 'string' ? data.ssid : '',
        password: typeof data.password === 'string' ? data.password : '',
      };
    } catch (_) {
      return null;
    }
  }

  function saveToLocalStorage(ssid, password) {
    try {
      localStorage.setItem(LS_KEYS.sta, JSON.stringify({ ssid, password }));
      return true;
    } catch (_) {
      return false;
    }
  }

  async function apiScan() {
    const res = await fetch(API.scan, { method: 'GET' });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      const msg = data && data.message ? data.message : `HTTP ${res.status}`;
      throw new Error(msg);
    }
    const aps = Array.isArray(data) ? data : (Array.isArray(data.aps) ? data.aps : []);
    return aps
      .filter(ap => ap && typeof ap.ssid === 'string' && ap.ssid.trim())
      .map(ap => ({ ssid: ap.ssid.trim(), rssi: ap.rssi }));
  }

  function renderSsidList(aps) {
    const box = $('availableSsidsBox');
    const list = $('availableSsidsList');
    if (!box || !list) return;

    box.style.display = 'block';
    if (!aps || aps.length === 0) {
      list.innerHTML = '<li>未找到 Wi-Fi</li>';
      return;
    }

    list.innerHTML = aps.map(ap => {
      const s = escapeHtml(ap.ssid);
      const r = (ap.rssi === 0 || ap.rssi) ? ` (${ap.rssi} dBm)` : '';
      return `<li data-ssid="${s}">${s}${r}</li>`;
    }).join('');

    list.querySelectorAll('li[data-ssid]').forEach(li => {
      li.addEventListener('click', () => {
        const ssid = li.getAttribute('data-ssid') || '';
        setForm(ssid, $('wifiPasswordInput') ? $('wifiPasswordInput').value : '');
        box.style.display = 'none';
      });
    });
  }

  async function onScanClick() {
    const btn = $('wifiScanBtn');
    if (btn) btn.disabled = true;
    try {
      renderSsidList([{ ssid: '扫描中…', rssi: '' }]);
      const aps = await apiScan();
      renderSsidList(aps);
      toast('扫描完成');
      setStatus(true, '设备在线');
    } catch (e) {
      renderSsidList([]);
      toast(`扫描失败：${e && e.message ? e.message : '未知错误'}`, true);
      setStatus(false, '设备离线/接口未实现');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function apiLoadSta() {
    const res = await fetch(API.wifi, { method: 'GET' });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      const msg = data && data.message ? data.message : `HTTP ${res.status}`;
      throw new Error(msg);
    }
    const ssid =
      (typeof data.ssid === 'string' ? data.ssid :
      (typeof data.sta_ssid === 'string' ? data.sta_ssid : '')) || '';
    const password =
      (typeof data.password === 'string' ? data.password :
      (typeof data.sta_password === 'string' ? data.sta_password : '')) || '';
    return { ssid, password };
  }

  async function apiSaveSta(ssid, password) {
    const payload = { ssid, password };
    const res = await fetch(API.wifi, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok || (data && data.status === 'error')) {
      const msg = data && data.message ? data.message : `HTTP ${res.status}`;
      throw new Error(msg);
    }
    return data;
  }

  async function onSaveClick() {
    const { ssid, password } = getForm();
    if (!ssid) {
      toast('请先填写 SSID', true);
      return;
    }

    try {
      await apiSaveSta(ssid, password);
      toast('已保存到设备');
      showSavedPreview(ssid, password);
      setStatus(true, '设备在线');
      saveToLocalStorage(ssid, password);
    } catch (e) {
      const ok = saveToLocalStorage(ssid, password);
      if (ok) {
        toast(`设备保存失败，已保存到本地：${e && e.message ? e.message : '未知错误'}`, true);
        showSavedPreview(ssid, password);
      } else {
        toast(`保存失败：${e && e.message ? e.message : '未知错误'}`, true);
      }
      setStatus(false, '设备离线/接口未实现');
    }
  }

  async function onLoadClick() {
    try {
      const data = await apiLoadSta();
      setForm(data.ssid, data.password);
      showSavedPreview(data.ssid, data.password);
      toast('已从设备读取');
      setStatus(true, '设备在线');
      saveToLocalStorage(data.ssid, data.password);
    } catch (e) {
      const local = loadFromLocalStorage();
      if (local) {
        setForm(local.ssid, local.password);
        showSavedPreview(local.ssid, local.password);
        toast(`设备读取失败，已从本地读取：${e && e.message ? e.message : '未知错误'}`, true);
      } else {
        toast(`读取失败：${e && e.message ? e.message : '未知错误'}`, true);
      }
      setStatus(false, '设备离线/接口未实现');
    }
  }

  async function onClearClick() {
    setForm('', '');
    setResultBoxVisible(false);
    try {
      localStorage.removeItem(LS_KEYS.sta);
    } catch (_) {}

    try {
      const res = await fetch('/api/wifi/clear', { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || (data && data.status === 'error')) {
        const msg = data && data.message ? data.message : `HTTP ${res.status}`;
        throw new Error(msg);
      }
      toast('设备 STA 已清空');
      setStatus(false, '未连接');
    } catch (e) {
      toast(`清空设备失败：${e && e.message ? e.message : '未知错误'}`, true);
      setStatus(false, '设备离线/接口未实现');
    }
  }

  function fmtIp(v) {
    if (v === null || v === undefined) return '—';
    const s = String(v);
    return s === '' ? '—' : s;
  }

  function fmtBool(v) {
    if (v === true) return '是';
    if (v === false) return '否';
    return '—';
  }

  function renderOverview(data) {
    const grid = $('overviewGrid');
    if (!grid) return;

    const rows = [
      ['Wi-Fi 模式', data.wifi_mode != null ? String(data.wifi_mode) : '—'],
      ['SoftAP IPv4', fmtIp(data.ip_softap)],
      ['STA IPv4', fmtIp(data.ip_sta)],
      ['ETH LAN IPv4', fmtIp(data.ip_eth_lan)],
      ['ETH WAN IPv4', fmtIp(data.ip_eth_wan)],
      ['PPP IPv4', fmtIp(data.ip_ppp)],
      ['空闲堆（字节）', data.free_heap != null ? String(Math.round(data.free_heap)) : '—'],
      ['运行时间（秒）', data.uptime_s != null ? String(Math.round(data.uptime_s)) : '—'],
      ['W5500', fmtBool(data.hw_w5500)],
      ['USB 模块(枚举)', fmtBool(data.hw_usb_modem_present)],
      ['USB 作 4G WAN', fmtBool(data.hw_usb_lte)],
      ['已保存工作模式', data.saved_work_mode != null && data.saved_work_mode >= 1
        ? `${data.saved_work_mode} · ${MODE_LABEL_ZH[data.saved_work_mode] || ''}`
        : '未设置'],
    ];

    grid.innerHTML = rows.map(([dt, dd]) =>
      `<div class="kv-row"><dt>${escapeHtml(dt)}</dt><dd>${escapeHtml(dd)}</dd></div>`
    ).join('');
  }

  async function fetchOverview() {
    const ph = $('overviewPlaceholder');
    try {
      const res = await fetch(API.overview, { method: 'GET' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok) {
        const msg = data && data.message ? data.message : `HTTP ${res.status}`;
        throw new Error(msg);
      }
      renderOverview(data);
      setStatus(true, '设备在线');
    } catch (e) {
      if (ph) {
        ph.textContent = `加载失败：${e && e.message ? e.message : '未知错误'}`;
      } else {
        const grid = $('overviewGrid');
        if (grid) {
          grid.innerHTML = `<div class="kv-row"><dt>错误</dt><dd>${escapeHtml(e.message || '未知')}</dd></div>`;
        }
      }
      setStatus(false, '设备离线/接口未实现');
    }
  }

  function updateWanLanDesc(modeId) {
    const wan = $('wanDesc');
    const lan = $('lanDesc');
    const n = MODE_NET_ZH[modeId];
    if (!wan || !lan) return;
    if (!n) {
      wan.textContent = '—';
      lan.textContent = '—';
      return;
    }
    wan.textContent = n.wan;
    lan.textContent = n.lan;
  }

  function selectedModeNeedsSta() {
    const sel = $('workModeSelect');
    if (!sel || !sel.value) return false;
    const opt = sel.options[sel.selectedIndex];
    return opt && opt.dataset.needsSta === '1';
  }

  function onWorkModeSelectChange() {
    const staBlock = $('staSection');
    const id = parseInt($('workModeSelect').value, 10);
    if (staBlock) {
      staBlock.style.display = selectedModeNeedsSta() ? 'block' : 'none';
    }
    if (!Number.isNaN(id) && id >= 1) {
      updateWanLanDesc(id);
    } else {
      updateWanLanDesc(0);
    }
  }

  function renderHwSummary(hw) {
    $('hwW5500').textContent = fmtBool(hw && hw.w5500);
    $('hwUsbProbe').textContent = fmtBool(hw && hw.usb_modem_present);
    $('hwUsbWan').textContent = fmtBool(hw && hw.usb_lte);
    $('hwUsbIds').textContent = (hw && hw.usb_ids) ? String(hw.usb_ids) : '—';
  }

  function showModeApiError(msg) {
    const el = $('modeApiErrorHint');
    if (!el) return;
    el.textContent = msg || '';
    el.style.display = msg ? 'block' : 'none';
  }

  async function fetchModeConfig() {
    const sel = $('workModeSelect');
    const noHint = $('noModesHint');
    const saveBtn = $('modeSaveBtn');
    const invalidHint = $('modeInvalidHint');
    showModeApiError('');
    try {
      const res = await fetch(API.mode, { method: 'GET' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok) {
        const msg = data && data.message ? data.message : `HTTP ${res.status}`;
        throw new Error(msg);
      }
      renderHwSummary(data.hardware);

      const modes = Array.isArray(data.modes) ? data.modes : [];
      sel.innerHTML = '';
      if (modes.length === 0) {
        sel.disabled = true;
        if (saveBtn) saveBtn.disabled = true;
        if (noHint) {
          noHint.style.display = 'block';
          const wanOff = data.hardware && data.hardware.usb_modem_present && !data.hardware.usb_lte;
          if (wanOff) {
            noHint.textContent = '已枚举到 USB 模块，但当前固件未在 menuconfig 中启用「External netif Modem / USB 4G 上行」，因此不会出现 4G 相关模式。可开启 CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM 等后重新编译烧录。';
          } else {
            noHint.textContent = '当前硬件或固件配置下没有可用的工作模式，请检查 W5500 / USB 模块与 menuconfig 中的网桥选项。';
          }
        }
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = '无可用模式';
        sel.appendChild(opt);
        if (invalidHint) invalidHint.style.display = 'none';
        onWorkModeSelectChange();
        setStatus(true, '设备在线');
        return;
      }
      if (noHint) noHint.style.display = 'none';
      sel.disabled = false;
      if (saveBtn) saveBtn.disabled = false;

      const placeholder = document.createElement('option');
      placeholder.value = '';
      placeholder.textContent = '请选择…';
      sel.appendChild(placeholder);

      for (let i = 0; i < modes.length; i++) {
        const m = modes[i];
        const opt = document.createElement('option');
        opt.value = String(m.id);
        const zh = MODE_LABEL_ZH[m.id] || m.label || `模式 ${m.id}`;
        opt.textContent = `${m.id}. ${zh}`;
        opt.dataset.needsSta = m.needs_sta ? '1' : '0';
        sel.appendChild(opt);
      }

      const cur = data.current;
      const allowed = modes.some(x => x.id === cur);
      if (invalidHint) {
        if (cur >= 1 && cur <= 7 && !allowed) {
          invalidHint.textContent = `NVS 中保存的模式 ${cur} 在当前硬件下不可用，请重新选择并保存。`;
          invalidHint.style.display = 'block';
        } else {
          invalidHint.style.display = 'none';
        }
      }

      if (allowed) {
        sel.value = String(cur);
      } else {
        sel.value = '';
      }
      onWorkModeSelectChange();
      setStatus(true, '设备在线');
    } catch (e) {
      sel.innerHTML = '';
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = '加载失败';
      sel.appendChild(opt);
      sel.disabled = true;
      if (saveBtn) saveBtn.disabled = true;
      const raw = e && e.message ? String(e.message) : '未知错误';
      let detail = raw;
      if (/HTTP\s+404/i.test(raw) || raw === 'Failed to fetch') {
        detail = `${raw}。常见原因：只更新了 www 网页分区，应用固件仍是旧版（无 /api/mode）。请用 idf.py flash 烧录完整应用，或确认本页是从设备 IP 打开（勿用 file:// 本地文件）。`;
      }
      showModeApiError(detail);
      toast(`模式接口失败：${raw}`, true);
      setStatus(false, '设备离线/接口未实现');
    }
  }

  async function onModeSaveClick() {
    const sel = $('workModeSelect');
    const hint = $('modeSaveHint');
    const id = parseInt(sel.value, 10);
    if (!sel.value || Number.isNaN(id)) {
      toast('请先选择工作模式', true);
      return;
    }
    try {
      const res = await fetch(API.mode, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: id }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || (data && data.status === 'error')) {
        const msg = data && data.message ? data.message : `HTTP ${res.status}`;
        throw new Error(msg);
      }
      toast('工作模式已写入 NVS');
      if (hint) {
        hint.textContent = data.hint || '若与当前编译的网桥拓扑不一致，需调整 menuconfig 并重新烧录/重启。';
        hint.style.display = 'block';
      }
      if ($('modeInvalidHint')) $('modeInvalidHint').style.display = 'none';
      fetchOverview();
    } catch (e) {
      toast(`保存失败：${e && e.message ? e.message : '未知错误'}`, true);
    }
  }

  function showView(name) {
    const ov = $('viewOverview');
    const st = $('viewSettings');
    const t1 = $('tabOverview');
    const t2 = $('tabSettings');
    if (ov) ov.hidden = name !== 'overview';
    if (st) st.hidden = name !== 'settings';
    if (t1) t1.classList.toggle('active', name === 'overview');
    if (t2) t2.classList.toggle('active', name === 'settings');

    if (name === 'overview') {
      fetchOverview();
    } else {
      fetchModeConfig();
    }
  }

  function bindNav() {
    document.querySelectorAll('.nav-tab').forEach(btn => {
      btn.addEventListener('click', () => {
        const v = btn.getAttribute('data-view');
        if (v) showView(v);
      });
    });
    const ref = $('overviewRefresh');
    ref && ref.addEventListener('click', () => fetchOverview());
    const sel = $('workModeSelect');
    sel && sel.addEventListener('change', onWorkModeSelectChange);
    const ms = $('modeSaveBtn');
    ms && ms.addEventListener('click', onModeSaveClick);
  }

  function bindSta() {
    const scanBtn = $('wifiScanBtn');
    const saveBtn = $('saveBtn');
    const loadBtn = $('loadBtn');
    const clearBtn = $('clearBtn');
    const closeBtn = $('ssidListCloseBtn');

    scanBtn && scanBtn.addEventListener('click', onScanClick);
    saveBtn && saveBtn.addEventListener('click', onSaveClick);
    loadBtn && loadBtn.addEventListener('click', onLoadClick);
    clearBtn && clearBtn.addEventListener('click', onClearClick);
    closeBtn && closeBtn.addEventListener('click', () => {
      const box = $('availableSsidsBox');
      if (box) box.style.display = 'none';
    });

    const local = loadFromLocalStorage();
    if (local && (local.ssid || local.password)) {
      setForm(local.ssid, local.password);
      showSavedPreview(local.ssid, local.password);
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    bindNav();
    bindSta();
    showView('overview');
  });
})();
