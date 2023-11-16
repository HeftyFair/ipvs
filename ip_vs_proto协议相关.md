# ip_vs_proto.c协议相关



`ip_vs_proto.c `是 Linux 内核中 `IPVS (IP Virtual Server)` 模块的一部分，它主要负责处理与 IP 协议相关的功能。

它包含了与 IPVS 协议处理相关的函数和实现。这个文件主要负责定义一些通用的函数，用于处理不同协议的数据包。这些函数可以根据协议类型进行组织和定义，例如 TCP、UDP 等。它们提供了对协议相关参数的配置和控制能力，可以根据实际需求进行设置，以适应不同的网络环境和应用场景。

在 `ip_vs_proto.c` 文件中，可以找到一些重要的函数，例如 `register_ip_vs_protocol()` 和 `unregister_ip_vs_protocol()`，用于注册和注销 IPVS 协议。此外，该文件还可能包含其他与协议处理相关的函数，例如连接输出和输入的函数。

总的来说，`ip_vs_proto.c` 提供了一个通用的框架，用于处理不同协议的数据包，而 `ip_vs_proto_tcp.c` 则是该框架的一个具体实现，专门用于处理 TCP 协议的数据包; `ip_vs_proto_udp.c` 专门用于处理 UDP 协议的数据包。这样的设计使得 IPVS 能够灵活地支持不同的协议类型。

## 相关数据结构

```
#define IP_VS_PROTO_TAB_SIZE		32	/* must be power of 2 */  
#define IP_VS_PROTO_HASH(proto)		((proto) & (IP_VS_PROTO_TAB_SIZE-1))  
  
static struct ip_vs_protocol *ip_vs_proto_table[IP_VS_PROTO_TAB_SIZE];  
```

​		声明了一个静态数组 `ip_vs_proto_table`，用于存储 IPVS 协议的指针。

​		数组的大小是 32，这是因为后续代码中使用了哈希表，32 是一个常见的哈希表大小的选择，可以提供较好的性能和冲突率。		`IP_VS_PROTO_HASH` 宏用于计算协议的哈希值。

​		`proto_data_table `是一个用于存储与IP协议相关的数据结构的数组。每个协议在 `proto_data_table `中都有一个对应的条目，称为` ip_vs_proto_data `结构体。

### `struct ip_vs_proto_data` 

是一个数据结构，用于存储和管理与 IP 协议相关的信息。在 Linux 内核中的 IPVS（IP Virtual Server）模块中，它被用来存储每个 IP 协议的相关数据。常与 `ip_vs_protocol`数据结构一起使用，以管理和处理 IPVS 中与特定协议相关的信息。

```
struct ip_vs_proto_data {  
    struct list_head        list;           用于将 struct ip_vs_proto_data 对象链接到全局数据列表的链表头
    atomic_t               refcnt;          引用计数，用于跟踪该对象的引用数
    int                     proto;          IP 协议号，例如 TCP 或 UDP 
    char                     name[IPPROTO_NAME_MAX];  协议名称，例如 "tcp" 或 "udp"
    struct ip_vs_protocol   *handler;       指向处理此协议的 ip_vs_protocol 对象的指针  
    atomic_t               usecnt;          使用计数，跟踪该对象的使用次数  
    unsigned                flags;          标志位，用于存储与协议相关的标志  
    unsigned                netmask;        网络掩码和其他掩码，用于协议处理过程中的掩码操作  
    unsigned                localmask;      /* local network mask */  
    unsigned                ppmask;         /* protocol packet mask */  
    unsigned                rtmask;         /* routing packet mask */  
    unsigned                flags2;         更多的标志位，可能与协议处理相关 
};
```

### `struct ip_vs_protocol`

是定义在 Linux 内核中的一种数据结构，用于表示一个 IPVS 协议。

```
struct ip_vs_protocol {  
    struct module          *module;        指向此协议所属的模块的指针（如果存在）  
    char                    *name;         协议的名称 
    unsigned char           protocol;      IP 协议号  
    struct ip_vs_protocol   *next;         指向链表中下一个协议对象的指针  
    int                     num_states;    协议状态的数量。对于 SCTP 协议，其值为 IP_VS_SCTP_S_LAST  
    int                    dont_defrag;    如果设置为非零值，则不对此协议的包进行碎片重组  
    void                   (*init)(void);  初始化函数,用于在加载协议时被调用 
    void                   (*exit)(void);  退出函数，用于在卸载协议时被调用 
    int                     (*init_netns)(struct net *net); 在网络命名空间初始化时被调用的函数
    void                   (*exit_netns)(struct net *net);  在网络命名空间退出时被调用的函数
    void              (*register_app)(struct ip_vs_protocol *pp, struct ip_vs_app *app); 注册应用程序函数 
    void              (*unregister_app)(struct ip_vs_protocol *pp, struct ip_vs_app *app); 注销应用程序函数 
    int                     (*conn_schedule)(struct sk_buff *skb, struct ip_vs_protocol *pp,  
                                            struct ip_vs_conn *cp, const struct ip_vs_iphdr *iph);                                                   连接调度函数，用于决定哪个连接应该处理接收到的数据包
    struct ip_vs_conn      *(*conn_in_get)(const struct sk_buff *skb, struct ip_vs_protocol *pp,  
                                           const struct ip_vs_iphdr *iph, unsigned int proto); 
                                           获取输入连接的函数
    struct ip_vs_conn      *(*conn_out_get)(const struct sk_buff *skb, struct ip_vs_protocol *pp,  
                                            const struct ip_vs_iphdr *iph, unsigned int proto);  
                                           获取输出连接的函数
};
```



## 相关函数

### 1.`register_ip_vs_protocol`

**注册IPVS 协议**,它首先计算协议的哈希值，它把给定的协议 `pp` 添加到` ip_vs_proto_table `哈希表中。如果协议的 init 方法存在，则调用它。返回值为0表示成功。

```
static int __used __init register_ip_vs_protocol(struct ip_vs_protocol *pp)  
{  
 unsigned int hash = IP_VS_PROTO_HASH(pp->protocol);  
 pp->next = ip_vs_proto_table[hash];  
 ip_vs_proto_table[hash] = pp;  
  
 if (pp->init != NULL)  
 pp->init(pp);  
  
 return 0;  
}
```

### 2.`register_ip_vs_proto_netns`

**注册与 IPVS 协议相关联的数据结构**,这个函数用于在特定的网络命名空间 ipvs 中注册协议 pp 的网络命名空间相关数据。如果协议的 init_netns 方法存在，则调用它。返回值为0表示成功。

```
// 声明一个函数 register_ip_vs_proto_netns，它接受两个参数：一个指向 netns_ipvs 结构的指针 ipvs，和一个指向 ip_vs_protocol 结构的指针 pp。  
static int register_ip_vs_proto_netns(struct netns_ipvs *ipvs, struct ip_vs_protocol *pp)  
  
{  
    unsigned int hash = IP_VS_PROTO_HASH(pp->protocol);  
    struct ip_vs_proto_data *pd = kzalloc(sizeof(struct ip_vs_proto_data), GFP_KERNEL);  
    if (!pd)  
        return -ENOMEM;  
    pd->pp = pp;                   /* For speed issues */  
    pd->next = ipvs->proto_data_table[hash];  
    ipvs->proto_data_table[hash] = pd;  
    atomic_set(&pd->appcnt, 0);            /* Init app counter */  
 
    if (pp->init_netns != NULL) {  
        int ret = pp->init_netns(ipvs, pd);  
        if (ret) {  
            /* unlink an free proto data */  
            ipvs->proto_data_table[hash] = pd->next;  
            kfree(pd);  
            return ret;  
        }  
    }  
    return 0;  
}
```

​		首先，根据协议的协议号 `pp->protocol`，使用哈希函数计算出一个哈希值，用于在哈希表中定位该协议的位置。
​		然后，创建一个新的数据结构，用于存储与 IPVS 协议相关的网络命名空间数据。这个数据结构通常是 `struct ip_vs_proto_data`，它包含指向协议本身的指针和其他相关数据。
​		将创建的数据结构添加到 ipvs 指向的 `proto_data_table` 数组的对应位置上。具体来说，它将数据结构的 next 指针设置为当前位置的下一个数据结构（如果存在的话），然后将当前位置的数据结构指针设置为新创建的数据结构。
​		如果协议的 init_netns 方法存在，则调用该方法，对协议的相关数据进行初始化操作。
​		最后，返回0表示注册成功。

### 3.`unregister_ip_vs_protocol`

**注销已注册的 IPVS 协议**,过程与**注册IPVS 协议**`register_ip_vs_protocol`相似：

该函数接受一个指向` ip_vs_protocol `结构体的指针 `pp `作为输入参数。

函数功能：这个函数的主要目的是从 IPVS 协议表中注销一个特定的协议。

首先，根据协议的协议号 `pp->protocol`，使用哈希函数计算出一个哈希值定位到协议在哈希表中的位置，这个位置由 `pp_p `指向。

然后，函数开始一个循环，不断检查` pp_p` 指向的协议是否与要注销的协议 `pp` 相同。如果相同，那么就将 pp 从哈希表中移除，并且如果 `pp `有一个 `exit` 方法，那么就调用这个方法清理资源。这样就完成了注销操作，函数返回0表示成功。

如果函数结束循环后没有找到要注销的协议，那么就返回 -ESRCH 表示没有找到该协议。最后，释放与协议相关联的数据结构的内存空间。

### 4.`unregister_ip_vs_proto_netns`

**注销与 IPVS 协议相关联的数据结构**,它接受两个参数，一个是指向`netns_ipvs`结构的指针`ipvs`，另一个是指向`ip_vs_proto_data`结构的指针`pd`。

```
unregister_ip_vs_proto_netns(struct netns_ipvs *ipvs, struct ip_vs_proto_data *pd)
{
	struct ip_vs_proto_data **pd_p;//定义了一个指向ip_vs_proto_data结构指针的指针pd_p
	unsigned int hash = IP_VS_PROTO_HASH(pd->pp->protocol);//根据协议的协议号计算出一个哈希值

	pd_p = &ipvs->proto_data_table[hash];//根据哈希值定位到应该注销在哪个位置的协议数据
	for (; *pd_p; pd_p = &(*pd_p)->next) //这是一个循环，它会遍历整个协议数据列表，直到找到要注销的项。
	{
		if (*pd_p == pd)//如果找到了要注销的协议，则执行括号内的操作
        {
			*pd_p = pd->next;
			if (pd->pp->exit_netns != NULL)
				pd->pp->exit_netns(ipvs, pd);
			kfree(pd);
			return 0;
		}
	}
	return -ESRCH;
}
```

### 5.`ip_vs_proto_get`

**获取指定协议号的 ip_vs_protocol 对象**,它接受一个无符号短整型参数`proto`作为协议号，并返回一个指向`ip_vs_protocol`结构的指针。这段代码用于通过协议号获取对应的` ip_vs_protocol `对象。如果找到了匹配的协议，则返回其指针；否则返回NULL。

```
/*
 *	get ip_vs_protocol object by its proto.
 */
struct ip_vs_protocol * ip_vs_proto_get(unsigned short proto)
{
	struct ip_vs_protocol *pp;
	unsigned int hash = IP_VS_PROTO_HASH(proto);//计算协议号对应的哈希值
	for (pp = ip_vs_proto_table[hash]; pp; pp = pp->next) //这是一个循环，用于遍历哈希表中与该哈希值对应的链表
	{
		if (pp->protocol == proto)
			return pp;
	}
	return NULL;
}
```

### 6.`ip_vs_proto_data_get`

**获取指定网络命名空间（netns）和协议号（proto）的 ip_vs_proto_data 对象**,这段代码用于通过指定的网络命名空间`ipvs`和协议号`proto`获取对应的 `ip_vs_proto_data 对象`。如果找到了匹配的对象，则返回其指针；否则返回NULL。

```
/*
 *	get ip_vs_protocol object data by netns and proto
 */
struct ip_vs_proto_data *ip_vs_proto_data_get(struct netns_ipvs *ipvs, unsigned short proto)
{
	struct ip_vs_proto_data *pd;
	unsigned int hash = IP_VS_PROTO_HASH(proto);

	for (pd = ipvs->proto_data_table[hash]; pd; pd = pd->next) {
		if (pd->pp->protocol == proto)
			return pd;
	}
	return NULL;
}
```

### 7.`ip_vs_protocol_timeout_change`

**超时变化函数**,这个函数遍历所有的协议，并对每个协议调用其超时变化函数（如果存在），传递当前协议的数据和标志作为参数，从而改变特定的超时设置。

### 8.`ip_vs_create_timeout_table` 

**创建新的内存副本**,这个函数创建一个新的内存副本（即重复分配的内存）来存储给定的表，两个参数：一个是指向整型数组的指针 table，另一个是整型数 size，表示数组的大小。该函数调用内核函数 kmemdup 来创建一个新的内存副本，这样就可以安全地在不同的地方修改原始数据，而不会影响到原始数据。

### 9.`ip_vs_set_state_timeout`

**查找状态并设置超时值**,用于设置一个由名称指定的状态其在给定表中的超时值。它被设计为处理具有特定格式的表，并使用名称来查找状态并设置超时值。即如果存在状态“name”的话，定义状态“name”的超时值为“to”。

### 10.`ip_vs_state_name`

**获取与给定协议和状态号相对应的状态名称**,其中会调用函数`ip_vs_proto_get`**获取指定协议号的 ip_vs_protocol 对象**，并查询其状态。

```
const char * ip_vs_state_name(__u16 proto, int state)
{
	struct ip_vs_protocol *pp = ip_vs_proto_get(proto);//返回指定协议号的对象的指针

	if (pp == NULL || pp->state_name == NULL)
		return (IPPROTO_IP == proto) ? "NONE" : "ERR!";
	return pp->state_name(state);//如果都不满足，将state作为参数传入。这个函数的返回值会被返回作为整个函数的返回值
}
```

### 11.`ip_vs_tcpudp_debug_packet_v4`

**调试并处理IPv4的TCP/UDP数据包**,用于调试IPv4的TCP或UDP数据包，并提取相关的源和目标IP地址以及端口信息，然后将其打印出来以供进一步分析。

接受四个参数：

* `struct ip_vs_protocol *pp`: 指向IPVS协议结构的指针。  

* `const struct sk_buff *skb`: 指向一个sk_buff结构体的指针，该结构体表示一个TCP/UDP数据包。  

* `int offset`: 数据包的偏移量，用于访问数据包中的特定部分。  

* `const char *msg`: 一个字符串，用于提供调试信息的上下文。

  ```
  static void
  ip_vs_tcpudp_debug_packet_v4(struct ip_vs_protocol *pp,
  			     const struct sk_buff *skb,
  			     int offset,
  			     const char *msg)
  {
  	char buf[128];//用于存储提取的IP和端口信息
  	struct iphdr _iph, *ih;
  
  	ih = skb_header_pointer(skb, offset, sizeof(_iph), &_iph);
  	//从skb中提取指定偏移量offset后的IP头信息，并将其存储在ih指针中
  	if (ih == NULL)
  		sprintf(buf, "TRUNCATED");//如果ih为NULL（即偏移量超过了数据包的有效长度），则将buf设置为"TRUNCATED"
  	else if (ih->frag_off & htons(IP_OFFSET))
  		sprintf(buf, "%pI4->%pI4 frag", &ih->saddr, &ih->daddr);
  	//如果IP头信息完整并且是一个分片（`ih->frag_off & htons(IP_OFFSET)`），则提取源和目标IP地址，并存储在`buf`中
  	else {
  		__be16 _ports[2], *pptr;
  
  		pptr = skb_header_pointer(skb, offset + ih->ihl*4,
  					  sizeof(_ports), _ports);
  		if (pptr == NULL)
  			sprintf(buf, "TRUNCATED %pI4->%pI4",
  				&ih->saddr, &ih->daddr);
  		else
  			sprintf(buf, "%pI4:%u->%pI4:%u",
  				&ih->saddr, ntohs(pptr[0]),
  				&ih->daddr, ntohs(pptr[1]));
  	}
  //否则，尝试提取TCP或UDP端口信息，并将其与源和目标IP地址一起存储在`buf`中。如果端口信息也完整，则将源和目标端口一并提取并存储。如果端口信息不完整，则将`buf`设置为"TRUNCATED <源IP>:<源端口>-><目标IP>:<目标端口>"。
  
  	pr_debug("%s: %s %s\n", msg, pp->name, buf);
  	//使用pr_debug宏打印函数名（即传入的msg）和协议名称（即pp->name）以及提取的IP和端口信息（即buf）。
  }
  ```

  ### 12.`ip_vs_tcpudp_debug_packet_v6`

  **调试并处理IPv6的TCP/UDP数据包**,用于处理IPv6的TCP或UDP数据包，并提取相关信息。与上一个函数逻辑类似，因IPV4与IPV6差异，此处char buf[192]，读取位数稍有差异;

  ### 13.`__init ip_vs_protocol_net_init`

  **初始化IPVS协议族**,主要操作是在网络命名空间初始化时，注册一系列IPVS协议。

  ​		定义一个静态的IPVS协议数组`protos[]`，这个数组包含了所有需要注册的协议。这些协议在数组中的顺序对应于它们在内核配置文件中的顺序。
  ​		遍历这个数组，并调用`register_ip_vs_proto_netns()`函数来注册每个协议。如果注册失败，就跳到cleanup标签进行清理工作。
  如果所有协议都成功注册了，函数就返回0，表示初始化成功。
  ​		如果在注册过程中有任何一个协议失败，函数就会跳到cleanup标签，在那里它调用`ip_vs_protocol_net_cleanup()`来清理已经注册的协议，并返回错误代码。

  ```
  void __net_exit ip_vs_protocol_net_cleanup(struct netns_ipvs *ipvs)
  {
  	struct ip_vs_proto_data *pd;
  	int i;
  /* unregister all the ipvs proto data for this netns */
  	for (i = 0; i < IP_VS_PROTO_TAB_SIZE; i++) //遍历ipvs->proto_data_table[]数组
  	{
  		while ((pd = ipvs->proto_data_table[i]) != NULL)
  			unregister_ip_vs_proto_netns(ipvs, pd);//注销函数
  	}
  }
  ```

  ​		需要注意的是，这段代码依赖于内核配置。如果在内核配置中没有启用某种协议，那么在protos[]数组中对应的元素就会被设置为NULL，这样在注册过程中就会跳过这个协议。

  ​		总的来说，这是一个典型的网络协议族初始化的例子，它依赖于宏和静态数组来定义需要注册的协议，然后遍历这个数组并尝试注册每个协议。如果在注册过程中遇到错误，它会清理已经注册的协议并返回错误代码。

  ### 14.`ip_vs_protocol_cleanup`

  **清理并注销所有已经注册的IPVS协议**,它遍历`ip_vs_proto_table[]`数组，该数组包含所有已注册的协议的相关数据，对数组中的每个元素，如果找到任何注册的协议，就会调用`unregister_ip_vs_protocol()`函数来注销它们。

  ```
  void ip_vs_protocol_cleanup(void)
  {
  	struct ip_vs_protocol *pp;
  	int i;
  	/* unregister all the ipvs protocols */
  	for (i = 0; i < IP_VS_PROTO_TAB_SIZE; i++) {
  		while ((pp = ip_vs_proto_table[i]) != NULL)
  			unregister_ip_vs_protocol(pp);
  	}
  }
  ```

  