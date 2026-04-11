# 多 WAN 智能设计（借鉴 OpenWrt 思路）

本文说明 **OpenWrt 式多 WAN** 在通用路由器中的含义、**多 WAN 共存** 的分层理解，以及在 **ESP-IDF + iot_bridge + lwIP** 场景下可落地的 **「智能选路」** 方法与演进建议。可与根目录 **[`路由器解决方案.md`](路由器解决方案.md)**、工程内 **`system_mode` / `system_wifi_dual_connect`** 等实现对照阅读。

---

## 1. OpenWrt 式多 WAN 在做什么（概念）

典型不是「多根网线插着就行」，而是一套 **控制面** 组合：

| 机制 | 作用 |
|------|------|
| **多逻辑接口** | `wan` / `wan2` / `wwan` 等，各自 DHCP/PPP、各自默认网关。 |
| **策略路由** | `ip rule` + 多张路由表（或 **fwmark**）：不同流量走不同出口。 |
| **mwan3（常见用户态方案）** | 成员接口、**metric**、**track（探测）**、主备/负载、接口 hotplug 事件。 |
| **健康探测** | 对固定 IP/URL 做 ping、HTTP 探针等；失败则 **降优先级或切换成员接口**。 |
| **NAT 与回包** | 每出口做 SNAT，并与连接跟踪配合，保证会话对称。 |

「智能」在 OpenWrt 里多数体现为：**探测 + 规则表 + 切换策略**，而不是单一硬编码规则。

---

## 2. 多 WAN「共存」指什么（三层区分）

| 层次 | 含义 | 难度 |
|------|------|------|
| **物理/协议共存** | 4G、Wi‑Fi STA、有线 WAN 等 **同时拨号/拿 IP**，多个 `netif` 各自为 **UP**。 | 相对低 |
| **转发路径「同时多用」** | LAN 上网 **并行**使用多条上行做 **负载或按目的分流**，需 **策略路由 + 连接级一致性**。 | 高（接近完整 mwan3） |
| **主备式共存** | 多条 WAN **常在线**，但 **默认上网只走一条**；主路异常再切到备用。 | 中（多数 4G 路由器产品形态） |

在资源受限的嵌入式栈上，通常先做 **主备 + 探测**，再视需求扩展 **简单分流**；**多出口负载均衡** 需单独立项评估。

---

## 3. 在 ESP-IDF / lwIP 上「模仿 OpenWrt」的合理预期

### 3.1 与 Linux / OpenWrt 的差异

- OpenWrt 依赖 **完整 iptables/nft、多路由表、丰富 netifd/hotplug**。
- ESP 侧常见模式是：通过 **`esp_netif_set_default_netif`** 选定 **一个默认出口**，再配合 NAPT；**完整 Linux 式多表策略路由** 能力有限。
- 因此「模仿」应理解为：**模仿产品行为（探测、主备、可选策略）**，而非 **1:1 复刻 mwan3 内核机制**。

### 3.2 可落地的「智能判断」方法（由易到难）

| 方法 | 做法 | 说明 |
|------|------|------|
| **静态优先级** | 如：有线 WAN > Wi‑Fi STA > 有线/蜂窝 PPP（顺序可配置） | 实现简单，与现有代码易对齐。 |
| **链路可用性** | 各 WAN **拿到地址 / PPP 已连接** 才视为候选；掉线则从候选剔除。 | 基础必备。 |
| **主动探测（track）** | 周期 ping 固定地址、网关或 DNS；连续失败判「不可用」 | 接近 mwan3 的 **track**，减轻「假在线」。 |
| **抖动抑制** | 主路恢复后 **迟滞若干秒或连续成功若干次** 再切回，避免来回切换。 | 稳定性关键。 |
| **策略分流（进阶）** | 按目的网段、端口等走指定 WAN | 依赖栈能力，工作量大。 |
| **负载均衡** | 按连接哈希或权重分配多 WAN | 嵌入式上 **最难**，需会话与 NAT 对称性整体设计。 |

**建议路线**：先做 **多 WAN 在线 + 主备 + 探测 + 迟滞**；再按需做 **有限分流**；**多 WAN 负载均衡** 单独评估。

---

## 4. 与本仓库现状的对应

当前工程在 **`components/system/system_wifi_dual_connect.c`** 中已对多上行做 **默认路由优先级**（有线 WAN 优先，其次 STA，再 PPP），并调用 **`esp_netif_set_default_netif`** 应用 **单一默认出口**：

```226:265:f:\github\4g_nic\components\system\system_wifi_dual_connect.c
static uplink_t choose_default_uplink(void)
{
    // If both wired WAN and STA are up, prefer ETH_WAN; otherwise STA or none.
#if defined(CONFIG_BRIDGE_EXTERNAL_NETIF_ETHERNET) || defined(CONFIG_BRIDGE_NETIF_ETHERNET_AUTO_WAN_OR_LAN)
    if (s_eth_up) {
        return UPLINK_ETH_WAN;
    }
#endif
    if (s_wifi_up) {
        return UPLINK_WIFI_STA;
    }
#if CONFIG_BRIDGE_EXTERNAL_NETIF_MODEM
    if (s_ppp_up) {
        return UPLINK_USB_MODEM;
    }
#endif
    return UPLINK_NONE;
}

static void apply_default_route(uplink_t u)
{
    if (u == s_default) return;

    if (u == UPLINK_NONE) {
        s_default = u;
        ESP_LOGW(TAG, "Default route: no WAN (sta=%d eth_wan=%d ppp=%d)", (int)s_wifi_up, (int)s_eth_up,
                 (int)s_ppp_up);
        return;
    }

    esp_netif_t *netif = get_uplink_netif(u);
    if (!netif) {
        ESP_LOGW(TAG, "default uplink netif missing: want %s", uplink_t_name(u));
        return;
    }

    esp_netif_set_default_netif(netif);
    s_default = u;
    ESP_LOGI(TAG, "Default route -> %s (%s)", uplink_t_name(u), esp_netif_get_ifkey(netif));
}
```

这可视为 **OpenWrt 中「按 metric 选主出口」的极简版**：**尚无** 多路由表、**尚无** mwan3 式 track，但 **多 WAN 同时 UP、只选一个 default** 的骨架已具备。

---

## 5. 综合结论（设计方向）

- **多 WAN 共存（产品语义）**  
  - **短期**：多条上行 **同时维持**，NAT 转发 **默认走一条**；主路异常按 **优先级 + 探测** 切换 —— 最接近 OpenWrt 的 **failover WAN** 体验，也与当前 `choose_default_uplink` 一致。  
  - **中期**：为每条候选 WAN 增加 **track** 与 **抖动抑制**，减少假链路与频繁切换。  
  - **长期**：若必须 **分流/负载**，再评估 lwIP/bridge 是否支持 **按策略选路**，或 **缩小范围**（例如仅少数目的网段走副 WAN）。

- **「智能」在嵌入式上的务实定义**  
  **可用性判定 + 主动探测 + 迟滞 + 可配置优先级**，而不是一上来对标 OpenWrt 全功能 mwan3。

---

*文档版本：与当前仓库 `system_wifi_dual_connect`、工作模式管理逻辑一致；若实现变更，请同步更新本节与代码引用。*
