(function () {
  'use strict';

  const API = {
    scan: '/api/wifi/scan',
    wifi: '/api/wifi',
  };

  const LS_KEYS = {
    sta: 'staConfig',
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
    // 兼容多种后端格式：
    // - { aps: [{ ssid, rssi }] }
    // - [{ ssid, rssi }]
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
    // 兼容可能的字段名：ssid/password 或 sta_ssid/sta_password
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
      // 也同步到本地，便于断网时回显
      saveToLocalStorage(ssid, password);
    } catch (e) {
      // 设备侧接口还没接好时，至少先本地可用
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

  function onClearClick() {
    setForm('', '');
    setResultBoxVisible(false);
    try {
      localStorage.removeItem(LS_KEYS.sta);
    } catch (_) {}
    toast('已清空');
  }

  function bind() {
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

    // 首次打开优先回显本地缓存
    const local = loadFromLocalStorage();
    if (local && (local.ssid || local.password)) {
      setForm(local.ssid, local.password);
      showSavedPreview(local.ssid, local.password);
    }
  }

  document.addEventListener('DOMContentLoaded', bind);
})();

