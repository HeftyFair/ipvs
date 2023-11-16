ip_vs_conn.c
```
/**
 * @brief 在ip_vs_core.c的hook函数ip_vs_in_hook中被调用
 * cp = INDIRECT_CALL_1(pp->conn_in_get, ip_vs_conn_in_get_proto,
 *			    ipvs, af, skb, &iph);
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
```
其中的转入参数 skb是ip_vs_in_hook的传入参数之一    
ipvs 在 ip_vs_in_hook 中    
struct netns_ipvs *ipvs = net_ipvs(state->net);---state是in_hook参数之一，应该是netfilter产生的    
iph（IP头部信息）在 ip_vs_in_hook 中    
ip_vs_fill_iph_skb(af, skb, false, &iph);
该函数会返回在hashtable中查到的connect的完整信息的结构指针。


ip_vs.h
连接所有的参数
```
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
```