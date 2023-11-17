源码位于 ip_vs_core.c
### hook

Linux内核的网络栈理数据包时，在数据包流动的关键路径上定义了五个Hook点，可以在Hook点上注册hook函数，当数据包流经时会按照优先级调用hook函数。

![[Pasted image 20231115210554.png]]

- `NF_INET_PRE_ROUTING`：所有接收的数据包抵达的第一个hook触发点，此处将进行数据包目的地转换 (DNAT), 决定数据包是发给 本地进程、其他机器或其他network namespace；
- `NF_INET_LOCAL_IN`：接收到的数据包经过了路由判断，如果目的地址是本机，将触发此Hook；
- `NF_INET_FORWARD`：接收到的数据包经过了路由判断，如果目的地址不是本机，将触发此Hook；
- `NF_INET_LOCAL_OUT`：本机产生的准备发送的数据包，在进入协议栈之前，将触发此Hook；
- `NF_INET_POST_ROUTING`：本机产生的准备发送的或者转发的数据包，在经过路由判断之后，将执行该Hook；

ipvs涉及到其中的`LOCAL_IN`、`FORWARD`和`LOCAL_OUT`这三个Hook点，在每个hook点都注册了两个函数。

```c
static const struct nf_hook_ops ip_vs_ops4[] = {
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_out_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC - 2,
	},
	/* After packet filtering, forward packet through VS/DR, VS/TUN,
	 * or VS/NAT(change destination), so that filtering rules can be
	 * applied to IPVS. */
	{
		.hook		= ip_vs_in_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC - 1,
	},
	/* Before ip_vs_in, change source only for VS/NAT */
	{
		.hook		= ip_vs_out_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST + 1,
	},
	/* After mangle, schedule and forward local requests */
	{
		.hook		= ip_vs_in_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST + 2,
	},
	/* After packet filtering (but before ip_vs_out_icmp), catch icmp
	 * destined for 0.0.0.0/0, which is for incoming IPVS connections */
	{
		.hook		= ip_vs_forward_icmp,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_FORWARD,
		.priority	= 99,
	},
	/* After packet filtering, change source only for VS/NAT */
	{
		.hook		= ip_vs_out_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_FORWARD,
		.priority	= 100,
	},
};
```

.priority 代表优先级，`数值越小，优先级越高。`

```c
enum nf_ip_hook_priorities {
    NF_IP_PRI_FIRST = INT_MIN,
    NF_IP_PRI_RAW_BEFORE_DEFRAG = -450,
    NF_IP_PRI_CONNTRACK_DEFRAG = -400,
    NF_IP_PRI_RAW = -300,
    NF_IP_PRI_SELINUX_FIRST = -225,
    NF_IP_PRI_CONNTRACK = -200,
    NF_IP_PRI_MANGLE = -150,
    NF_IP_PRI_NAT_DST = -100,
    NF_IP_PRI_FILTER = 0,
    NF_IP_PRI_SECURITY = 50,
    NF_IP_PRI_NAT_SRC = 100,
    NF_IP_PRI_SELINUX_LAST = 225,
    NF_IP_PRI_CONNTRACK_HELPER = 300,
    NF_IP_PRI_CONNTRACK_CONFIRM = INT_MAX,
    NF_IP_PRI_LAST = INT_MAX,
};
```

| HOOK              | 函数               | Priority |
| ----------------- | ------------------ |:--------:|
| NF_INET_LOCAL_IN  | ip_vs_out_hook     |    98    |
| NF_INET_LOCAL_IN  | ip_vs_in_hook      |    99    |
| NF_INET_LOCAL_OUT | ip_vs_out_hook     |   -99    |
| NF_INET_LOCAL_OUT | ip_vs_in_hook      |   -98    |
| NF_INET_FORWARD   | ip_vs_forward_icmp |    99    |
| NF_INET_FORWARD   | ip_vs_out_hook     |   100    |

hook函数的返回值有六种：
1. `nf_accept`通知 hook chain 要继续往下跑；
2. `nf_repeat`再执行一遍这个 hook，再继续跑 hook chain；
3. 如果要终止执行后面的 hook chain，要返回`nf_drop`或者`nf_stop`。两者的区别是前者最终会释放 skb，不再进入协议栈；后者相反。
4. `nf_stolen`语意有点不同，同样会终止 hook chain 的执行，但相当于 hook 告诉 netfilter当前这个 skb 我接管了，有 hook 来控制 skb 的生命周期。
5. `nf_queue`会暂缓 hook chain 的执行，将 skb 交由用户空间的 hook 来处理，处理完毕后，使用 nf_reinject 返回内核空间继续执行后面的 hook。
6. 对于整个 hook chain 同样会有一个返回值：返回成功 netfilter 会调用一个 okfn 回调函数来处理 skb，通常 okfn 是协议栈的处理函数；反之释放 skb。

### ip_vs_in_hook

该函数主要作用是使 IPVS 可以在网络流量进入系统时做出相应的决策：`NF_ACCEPT` or `NF_DROP`

**过滤数据包**：检查数据包是否已经被标记为 IPVS 处理的请求或响应。如果是，就直接接受该数据包（`NF_ACCEPT`）。
处理特殊情况，数据包类型不是 `PACKET_HOST`或数据包没有有效的目的地。

```c
/* Already marked as IPVS request or reply? */
if (skb->ipvs_property)
	return NF_ACCEPT;

/*
 *	Big tappo:
 *	- remote client: only PACKET_HOST
 *	- route: used for struct net when skb->dev is unset
 */
 // PACKET_HOST 表示数据包不是发往本机
if (unlikely((skb->pkt_type != PACKET_HOST &&
		  hooknum != NF_INET_LOCAL_OUT) ||
		 !skb_dst(skb))) {
	ip_vs_fill_iph_skb(af, skb, false, &iph);
	IP_VS_DBG_BUF(12, "packet type=%d proto=%d daddr=%s"
			  " ignored in hook %u\n",
			  skb->pkt_type, iph.protocol,
			  IP_VS_DBG_ADDR(af, &iph.daddr), hooknum);
	return NF_ACCEPT;
}```

检查 IPVS 是否启用以及是否仅在备份模式下运行。然后，处理一个有效的套接字，当`hooknum`是 `NF_INET_LOCAL_OUT`，且地址族是 `AF_INET`（IPv4），则进行进一步的检查。如果套接字是 IPv4 类型（`PF_INET`），并且设置了 `nodefrag`，则函数同样返回 `NF_ACCEPT`。

```c
/* ipvs enabled in this netns ? */
// 检查 ipvs 是否启用
if (unlikely(sysctl_backup_only(ipvs) || !ipvs->enable))
	return NF_ACCEPT;

// 填充 IP head
ip_vs_fill_iph_skb(af, skb, false, &iph);

/* Bad... Do not break raw sockets */
sk = skb_to_full_sk(skb);
if (unlikely(sk && hooknum == NF_INET_LOCAL_OUT &&
		 af == AF_INET)) {

	if (sk->sk_family == PF_INET && inet_sk(sk)->nodefrag)  // nodefrag 表示是否进行 IP 分片，1-不分片，0-分片。
		return NF_ACCEPT;
	}```
    
**处理ICMP数据包**：调用 `ip_vs_in_icmp` 或 `ip_vs_in_icmp_v6` 特别处理 ICMP 或 ICMPv6 数据包。

```c
#ifdef CONFIG_IP_VS_IPV6
if (af == AF_INET6) {
	if (unlikely(iph.protocol == IPPROTO_ICMPV6)) {
		int related;
		int verdict = ip_vs_in_icmp_v6(ipvs, skb, &related,hooknum, &iph);

		if (related)
			return verdict;
	}
} else
#endif
	if (unlikely(iph.protocol == IPPROTO_ICMP)) {
		int related;
		int verdict = ip_vs_in_icmp(ipvs, skb,   &related,hooknum);
		// 判断是否与现有的 ipvs 相关
		if (related)
			return verdict;
		}```
    
**协议支持检查**：获取数据包的data，同时检查 IPVS 是否支持数据包中的协议（如 TCP、UDP 等）。如果不支持，则根据配置，是否需要在后续处理中跳过tunneled packets。

```c
/* Protocol supported? */
pd = ip_vs_proto_data_get(ipvs, iph.protocol);
if (unlikely(!pd)) {
	/* The only way we'll see this packet again is if it's
	 * encapsulated, so mark it with ipvs_property=1 so we
	 * skip it if we're ignoring tunneled packets
	 */
	 // 如果配置为忽略tunneled，则标记ipvs_property
	if (sysctl_ignore_tunneled(ipvs))
		skb->ipvs_property = 1;

	return NF_ACCEPT;
}```
    
**连接跟踪**：处理 IPVS 中对数据包的连接跟踪和管理。检查每个传入的数据包，确定它们是否属于一个现有的连接，或者是否需要创建新的连接。

```c
/*
 * Check if the packet belongs to an existing connection entry
 */
// 获取与当前 skb 相关联的现有连接cp，cp 是 struct ip_vs_conn
cp = INDIRECT_CALL_1(pp->conn_in_get, ip_vs_conn_in_get_proto,ipvs, af, skb, &iph);

// 检查现有的连接是否是一个新连接  iph.fragoffs 表示是否为一个分段 0-不是；1-是
if (!iph.fragoffs && is_new_conn(skb, &iph) && cp) {
	// 确定重用模式的设置，默认为 1 
	int conn_reuse_mode = sysctl_conn_reuse_mode(ipvs);
	// old_ct 表示是否使用旧的连接跟踪机制。resched 表示是否需要重新调度连接
	bool old_ct = false, resched = false;
	// sysctl_expire_nodest_conn()表示党连接的目标服务器变得不可用时，是否需要将该连接过期，默认为0 
	if (unlikely(sysctl_expire_nodest_conn(ipvs)) && cp->dest &&
		unlikely(!atomic_read(&cp->dest->weight))) {
		resched = true;
		old_ct = ip_vs_conn_uses_old_conntrack(cp, skb);
	} else if (conn_reuse_mode &&   // 如果设置了重用模式且连接符合重用条件
		   is_new_conn_expected(cp, conn_reuse_mode)) {
		// 检查是否使用了旧的连接跟踪
		old_ct = ip_vs_conn_uses_old_conntrack(cp, skb);
		if (!atomic_read(&cp->n_control)) {
			resched = true;
		} else {
			/* Do not reschedule controlling connection
			 * that uses conntrack while it is still
			 * referenced by controlled connection(s).
			 */
			resched = !old_ct;
		}
	}

	if (resched) {
		if (!old_ct)
			// 清除连接标志
			cp->flags &= ~IP_VS_CONN_F_NFCT;
		// cp->n_control为0，不存在与当前cp相关联的控制信息，则过期该连接
		if (!atomic_read(&cp->n_control))
			ip_vs_conn_expire_now(cp);
		// 释放连接
		__ip_vs_conn_put(cp);
		// 使用旧的连接跟踪则丢弃该数据包
		if (old_ct)
			return NF_DROP;
		cp = NULL;
	}
	}```
    
**服务器状态检查**：检查和处理指向不可用目标服务器的连接。

```c
/* Check the server status */
// IP_VS_DEST_F_AVAILABLE 表示该服务器是否可用
if (cp && cp->dest && !(cp->dest->flags & IP_VS_DEST_F_AVAILABLE)) {
	/* the destination server is not available */
	// sysctl_expire_nodest_conn()默认为0
	if (sysctl_expire_nodest_conn(ipvs)) {
		bool old_ct = ip_vs_conn_uses_old_conntrack(cp, skb);

		if (!old_ct)
			cp->flags &= ~IP_VS_CONN_F_NFCT;

		ip_vs_conn_expire_now(cp);
		__ip_vs_conn_put(cp);
		if (old_ct)
			return NF_DROP;
		cp = NULL;
	} else {
		// 丢弃这个数据包
		__ip_vs_conn_put(cp);
		return NF_DROP;
	}
	}```

**一些后续处理**：如果数据包没有现有连接，尝试为其创建新的连接。此外，还涉及更新统计信息、设置连接状态、处理数据包传输，以及在需要时同步连接状态

```c
// 如果没有现有连接，则在这边进行调度
if (unlikely(!cp)) {
	int v;

	if (!ip_vs_try_to_schedule(ipvs, af, skb, pd, &v, &cp, &iph))
		return v;
}

IP_VS_DBG_PKT(11, af, pp, skb, iph.off, "Incoming packet");

// 更新状态，设置ipvs的连接状态为IP_VS_DIR_INPUT
ip_vs_in_stats(cp, skb);
ip_vs_set_state(cp, IP_VS_DIR_INPUT, skb, pd);
// 如果定义packet_xmit，则用其处理数据包的传输
if (cp->packet_xmit)
	ret = cp->packet_xmit(skb, cp, pp, &iph);
	/* do not touch skb anymore */
else {
	IP_VS_DBG_RL("warning: packet_xmit is null");
	ret = NF_ACCEPT;
}

/* Increase its packet counter and check if it is needed
 * to be synchronized
 *
 * Sync connection if it is about to close to
 * encorage the standby servers to update the connections timeout
 *
 * For ONE_PKT let ip_vs_sync_conn() do the filter work.
 */
// 判断是否为单数据包连接状态(IP_VS_CONN_F_ONE_PACKET)
if (cp->flags & IP_VS_CONN_F_ONE_PACKET)
	pkts = sysctl_sync_threshold(ipvs);
else
	// 不是，增加一个数据包数量
	pkts = atomic_inc_return(&cp->in_pkts);
// ipvs 是否为主节点状态(IP_VS_STATE_MASTER)
if (ipvs->sync_state & IP_VS_STATE_MASTER)
	// 调用ip_vs_sync_conn进行同步连接状态
	ip_vs_sync_conn(ipvs, cp, pkts); 
else if ((cp->flags & IP_VS_CONN_F_ONE_PACKET) && cp->control)
	/* increment is done inside ip_vs_sync_conn too */
	atomic_inc(&cp->control->in_pkts);
// 释放连接，返回结果
ip_vs_conn_put(cp);
return ret;
	}```


