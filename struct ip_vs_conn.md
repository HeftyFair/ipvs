# struct ip_vs_conn    

该结构位于 /include/net/ip_vs.h    

## 代码   

```
/* IP_VS structure allocated for each dynamically scheduled connection */
struct ip_vs_conn {
	struct hlist_node	c_list;         /* hashed list heads */
	/* Protocol, addresses and port numbers */
	__be16                  cport;
	__be16                  dport;
	__be16                  vport;
	u16			af;		/* address family */
	union nf_inet_addr      caddr;          /* client address */
	union nf_inet_addr      vaddr;          /* virtual address */
	union nf_inet_addr      daddr;          /* destination address */
	volatile __u32          flags;          /* status flags */
	__u16                   protocol;       /* Which protocol (TCP/UDP) */
	__u16			daf;		/* Address family of the dest */
	struct netns_ipvs	*ipvs;

	/* counter and timer */
	refcount_t		refcnt;		/* reference count */
	struct timer_list	timer;		/* Expiration timer */
	volatile unsigned long	timeout;	/* timeout */

	/* Flags and state transition */
	spinlock_t              lock;           /* lock for state transition */
	volatile __u16          state;          /* state info */
	volatile __u16          old_state;      /* old state, to be used for
						 * state transition triggerd
						 * synchronization
						 */
	__u32			fwmark;		/* Fire wall mark from skb */
	unsigned long		sync_endtime;	/* jiffies + sent_retries */

	/* Control members */
	struct ip_vs_conn       *control;       /* Master control connection */
	atomic_t                n_control;      /* Number of controlled ones */
	struct ip_vs_dest       *dest;          /* real server */
	atomic_t                in_pkts;        /* incoming packet counter */

	/* Packet transmitter for different forwarding methods.  If it
	 * mangles the packet, it must return NF_DROP or better NF_STOLEN,
	 * otherwise this must be changed to a sk_buff **.
	 * NF_ACCEPT can be returned when destination is local.
	 */
	int (*packet_xmit)(struct sk_buff *skb, struct ip_vs_conn *cp,
			   struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph);

	/* Note: we can group the following members into a structure,
	 * in order to save more space, and the following members are
	 * only used in VS/NAT anyway
	 */
	struct ip_vs_app        *app;           /* bound ip_vs_app object */
	void                    *app_data;      /* Application private data */
	struct_group(sync_conn_opt,
		struct ip_vs_seq  in_seq;       /* incoming seq. struct */
		struct ip_vs_seq  out_seq;      /* outgoing seq. struct */
	);

	const struct ip_vs_pe	*pe;
	char			*pe_data;
	__u8			pe_data_len;

	struct rcu_head		rcu_head;
};
```

## 结构分析

 类型 | 名称 | size | source | 注释 |
 -- | -- | -- | -- | -- |
 struct hlist_node | c_list | 8 | hashed list heads | hash 链表节点 |
 | <td colspan=4><font color=yellow>Protocol addresses port</font></td> |
 __be16 | cport | 2 |  | 客户端port |
 __be16 | dport | 2 |  | 目标port |
 __be16 | vport | 2 |  | 对外虚拟port|
 u16 | af | 2 | address family |
 union nf_inet_addr | caddr | 16 | client address |
 union nf_inet_addr | vaddr | 16 | virtual address |
 union nf_inet_addr | daddr | 16 | destination address |
 volatile __u32 | flags | 4 | status flags |
 __u16 | protocol | 2 | Which protocol (TCP/UDP) |
 __u16 | daf | 2 | Address family of the dest |
 struct netns_ipvs* | ipvs | 8 |  |
| <td colspan=4><font color=yellow>counter and timer</font></td> |
 refcount_t | refcnt | 4 | reference count |
 struct timer_list | timer | 24？ | Expiration timer |
 volatile unsigned long | timeout | 4 | timeout |
| <td colspan=3><font color=yellow>Flags and state transition</font></td> |
 spinlock_t | lock |  | lock for state transition |
 volatile __u16 | state | 2 | state info |
 volatile __u16 | old_state | 2 | old state, to be used for state transition triggerd synchronization |
 __u32 | fwmark | 4 | Fire wall mark from skb |
 unsigned long | sync_endtime | 4 | jiffies + sent_retries |
| <td colspan=3><font color=yellow>Control members</font></td> |
 struct ip_vs_conn* | control | 8 | Master control connection |
 atomic_t | n_control | 4 | Number of controlled ones |
 struct ip_vs_dest* | dest | 8 | real server |
 atomic_t | in_pkts | 4 | incoming packet counter |
| <td colspan=3><font color=yellow>Packet transmitter for different forwarding methods.  If it mangles the packet, it must return NF_DROP or better NF_STOLEN, otherwise this must be changed to a sk_buff NF_ACCEPT can be returned when destination is local.</font></td> |
 int (*packet_xmit)(struct sk_buff *skb, struct ip_vs_conn *cp, struct ip_vs_protocol *pp, struct ip_vs_iphdr *iph); |  | 8 |  |
| <td colspan=3><font color=yellow>Note: we can group the following members into a structure, in order to save more space, and the following members are only used in VS/NAT anyway</font></td> |
 struct ip_vs_app* | app | 8 | bound ip_vs_app object |
 void* | app_data | 8 | Application private data |
| <td colspan=3><font color=yellow>struct_group (sync_conn_op)</font></td> |
 struct ip_vs_seq | in_seq |  | incoming seq. struct |
 struct ip_vs_seq | out_seq |  | outgoing seq. struct |
 const struct ip_vs_pe* | pe | 8 |  |
 char* | pe_data | 8 |  |
 __u8 | pe_data_len | 1 |  |
 struct rcu_head | rcu_head |  |  |

目前size统计远超过config上面的128字节   
```
	  Another note that each connection occupies 128 bytes effectively and
	  each hash entry uses 8 bytes, so you can estimate how much memory is
	  needed for your box.
```
