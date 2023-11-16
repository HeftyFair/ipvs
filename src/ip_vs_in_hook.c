/**
 * @brief 在ip_vs_core.c的hook函数ip_vs_in_hook中被调用
 * cp = INDIRECT_CALL_1(pp->conn_in_get, ip_vs_conn_in_get_proto,
 *			    ipvs, af, skb, &iph);
 * 其中的转入参数 skb是ip_vs_in_hook的传入参数之一    
 * ipvs 在 ip_vs_in_hook 中    
 * struct netns_ipvs *ipvs = net_ipvs(state->net);---state是in_hook参数之一，应该是netfilter产生的    
 * iph（IP头部信息）在 ip_vs_in_hook 中    
 * ip_vs_fill_iph_skb(af, skb, false, &iph);
 * 该函数会返回在hashtable中查到的connect的完整信息的结构指针。
 *
 * @param ipvs struct netns_ipvs *  这个应该是linux的net namespace for ipvs ，
 * @param af  addr. family  (IPv4v6)
 * @param skb sk_buff* 数据buffer
 * @param iph ip头部的信息集合
 * @return struct ip_vs_conn* 在hashtable中找到已经记录的数据，并且返回 ip_vs_conn结构的指针
 */
struct ip_vs_conn *
ip_vs_conn_in_get_proto(struct netns_ipvs *ipvs, int af,
			const struct sk_buff *skb,
			const struct ip_vs_iphdr *iph)
{
    //连接参数
	struct ip_vs_conn_param p;

    //填入连接参数，
	if (ip_vs_conn_fill_param_proto(ipvs, af, skb, iph, &p))
		return NULL;

    //找到连接数据
	return ip_vs_conn_in_get(&p);
}

/**
 * @brief 连接的参数 5元组+addr. family
 * 
 */
struct ip_vs_conn_param {
	struct netns_ipvs		*ipvs;
	const union nf_inet_addr	*caddr;
	const union nf_inet_addr	*vaddr;
	__be16				cport;
	__be16				vport;
	__u16				protocol;
	u16				af;

	const struct ip_vs_pe		*pe;
	char				*pe_data;
	__u8				pe_data_len;
};

struct ip_vs_conn *ip_vs_conn_in_get(const struct ip_vs_conn_param *p)
{
	struct ip_vs_conn *cp;

	cp = __ip_vs_conn_in_get(p);
	if (!cp && atomic_read(&ip_vs_conn_no_cport_cnt)) {
		struct ip_vs_conn_param cport_zero_p = *p;
		cport_zero_p.cport = 0;
		cp = __ip_vs_conn_in_get(&cport_zero_p);
	}

	IP_VS_DBG_BUF(9, "lookup/in %s %s:%d->%s:%d %s\n",
		      ip_vs_proto_name(p->protocol),
		      IP_VS_DBG_ADDR(p->af, p->caddr), ntohs(p->cport),
		      IP_VS_DBG_ADDR(p->af, p->vaddr), ntohs(p->vport),
		      cp ? "hit" : "not hit");

	return cp;
}

/**
 *  Gets ip_vs_conn associated with supplied parameters in the ip_vs_conn_tab.
 *  在hashtable里面通过parameter得到connect信息
 *  Called for pkts coming from OUTside-to-INside.
 *	p->caddr, p->cport: pkt source address (foreign host)
 *	p->vaddr, p->vport: pkt dest address (load balancer)
 */
static inline struct ip_vs_conn *
__ip_vs_conn_in_get(const struct ip_vs_conn_param *p)
{
	unsigned int hash;
	struct ip_vs_conn *cp;

	//计算hash值
	hash = ip_vs_conn_hashkey_param(p, false);
	//rcu lock
	rcu_read_lock();
	//hash值链表 hlist 的 for循环  //c_list 是ip_vs_conn的hash链表的变量名
	hlist_for_each_entry_rcu(cp, &ip_vs_conn_tab[hash], c_list) {
		if (p->cport == cp->cport && p->vport == cp->vport &&
		    cp->af == p->af &&
		    ip_vs_addr_equal(p->af, p->caddr, &cp->caddr) &&
		    ip_vs_addr_equal(p->af, p->vaddr, &cp->vaddr) &&
		    ((!p->cport) ^ (!(cp->flags & IP_VS_CONN_F_NO_CPORT))) &&
		    p->protocol == cp->protocol &&
		    cp->ipvs == p->ipvs) {
			if (!__ip_vs_conn_get(cp)) //获取访问权限
				continue;
			/* HIT */
			rcu_read_unlock();
			return cp;
		}
	}

	rcu_read_unlock();

	return NULL;
}

/* Get reference to gain full access to conn.
 * By default, RCU read-side critical sections have access only to
 * conn fields and its PE data, see ip_vs_conn_rcu_free() for reference.
 */
static inline bool __ip_vs_conn_get(struct ip_vs_conn *cp)
{
	return refcount_inc_not_zero(&cp->refcnt);
}