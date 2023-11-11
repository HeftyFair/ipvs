---
bibliography: /Users/andreigavrilov/Library/Mobile
  Documents/iCloud~md~obsidian/Documents/notes/Bibliography/My
  Library.bib
---

# 头文件`ip_vs.h`中的结构

### `struct ip_vs_conn`

表示一个网络链接。该结构存储在一个哈希表中，在[\#ip_vs_conn.c](#ip_vs_conn.c "wikilink")中有更详细的介绍。

### `struct netns_ipvs`

这个结构直接被嵌入到`struct net`（表示一个网络命名空间，例如docker等容器服务可能使用不同的网络命名空间，因此有不同的net结构）里面，它存储着一些比较重要的状态，像函数指针，例如`ip_vs_proto_data`，还有一些全局配置相关的信息(`sysctl`等)。

### `struct ip_vs_service`

这个结构表示一个服务表；表中包含了真实服务器的信息(`svc->destinations`)，源码当中的调度代码根据这个信息来选择最佳目标地址。  
该结构由相应的调度模块负责保存。

### `struct ip_vs_app`

表示某个使用传输层服务的应用层用户程序。

### `struct ip_vs_protocol`

与具体协议的实现相关；相应的源码在ip_vs_tcp，ip_vs_udp；ipvs当中协议需要完成的工作：  
- 从`sk_buff`（[\#\`struct sock\` , \`struct sk_buff\`](#`struct sock` , `struct sk_buff` "wikilink")）得到一个`ip_vs_conn` ipvs链接条目（tcp和udp都是调用`ip_vs_conn_in_get_proto`）。  
- 实现状态转移函数，处理由于收到的报文中带有的状态字样，而导致的链接状态的变化。  
- 处理nat对报文的修改。

### `struct ip_vs_dest`

表示一个ipvs调度的目标。里面有路由表的cache

# `ip_vs_core.c`

## Hook

在core函数里，注册了netfilter的钩子[^1]。  
`netfilter`的钩子会被`nf_hook`调用（注意钩子函数是在RCU读锁的上下文中的[\#RCU(Read, Copy, Update)](#RCU(Read, Copy, Update) "wikilink")）。linux内核的网络栈在处理包时会调用`NF_HOOK`相关的宏，从而执行相应的函数指针。  
钩子总共有五个。

- NF_INET_PRE_ROUTING：此钩子在数据包被网络接口接收之后但在路由决策之前触发。它适用于所有进入的数据包，不论是通过哪个接口。
- NF_INET_LOCAL_IN：如果路由决策确定数据包是发往本地机器，这个钩子就会被调用。
- NF_INET_FORWARD：如果数据包将被转发（即，机器充当路由器或网关），这个钩子就会被调用。
- NF_INET_LOCAL_OUT：此钩子在本地机器产生的数据包即将发送出去时触发。
- NF_INET_POST_ROUTING：在路由代码决定将数据包发送到下一个位置（可能是另一个接口或通过相同的接口传输）之后，这个钩子被调用。它适用于所有出去的数据包，不论是通过哪个接口。

nftables的网站[^2]给出了详细的解释。还有一篇博客写的也很好[^3]。

<figure>
<img
src="IPVS%20source-media/8d58a825b74b7f7e6bc54a4007542951a1caa4ce.png"
title="wikilink" alt="packet flows through Linux networking" />
<figcaption aria-hidden="true">packet flows through Linux
networking</figcaption>
</figure>

``` c
enum nf_inet_hooks {
    NF_INET_PRE_ROUTING,
    NF_INET_LOCAL_IN,
    NF_INET_FORWARD,
    NF_INET_LOCAL_OUT,
    NF_INET_POST_ROUTING,
    NF_INET_NUMHOOKS,
    NF_INET_INGRESS = NF_INET_NUMHOOKS,
};
```

ipvs用到其中的NF_INET_LOCAL_IN，NF_INET_LOCAL_OUT和NF_INET_FORWARD三个钩子。

此外还定义了优先级，ipvs的优先级在filter和security之后。

``` c
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

> 注意ipvs是作为中间件，起到负载均衡的作用，因此主机中会有多个网络设备；例如，一个网络设备连接外部，一个设备连接内网。

钩子函数的返回值有六种[^4]

nf_accept通知hook chain要继续往下跑；

nf_repeat再执行一遍这个hook，再继续跑hook chain

如果要终止执行后面的hook chain，要返回nf_drop或者nf_stop。两者的区别是前者最终会释放skb，不再进入协议栈；后者相反。

nf_stolen语意有点不同，同样会终止hook chain的执行，但相当于hook告诉netfilter当前这个skb我接管了，有hook来控制skb的生命周期。

> 例如，如果对报文作了修改，例如nat模式，那么必须返回nf_drop或者nf_stolen更好。

nf_queue会暂缓hook chain的执行，将skb交由用户空间的hook来处理，处理完毕后，使用nf_reinject返回内核空间继续执行后面的hook。

对于整个hook chain同样会有一个返回值：返回成功netfilter会调用一个okfn回调函数来处理sb，通常okfn是协议栈的处理函数；反之释放skb。

## handle_response

重写地址，调用协议的状态转移函数。  
nat传输方式在该函数处理由内向外的请求（通过协议实现的`snat_handler`）。

## `ip_vs_in`

``` c
static unsigned int
ip_vs_in(struct netns_ipvs *ipvs, unsigned int hooknum, struct sk_buff *skb, int af)
```

在netfilter钩子中运行的时机为`NF_INET_LOCAL_IN`(处理客户端到虚拟服务器的连接)和`NF_INET_LOCAL_OUT`（处理本地中，虚拟服务器到真实服务器的连接）。  
检查是否发向虚拟设备，对发往虚拟设备的包进行调度处理。

`ip_vs_in`会检查数据包是否访问了虚拟服务，若访问了，并且这个连接是一个新连接，则进行调度处理(`ip_vs_try_to_schedule`)，将其调度至某个真实服务器。检查连接是否为新的过程会经过一个哈希表（通过[\#ip_vs_conn.c](#ip_vs_conn.c "wikilink")的哈希函数`conn_in_get`）。

## `ip_vs_out`

处理从本机发往外部的包。  
它运行在三个钩子：`NF_INET_FORWARD` `NF_INET_LOCAL_IN` `NF_INET_LOCAL_OUT`  
最后调用[\#handle_response](#handle_response "wikilink")

## ip_vs_try_to_schedule

调用对应协议的`conn_schedule`函数。

## ip_vs_schedule

这个函数被具体协议的conn_schedule字段调用，而这个方法又被ip_vs_try_to_schedule调用。

# ip_vs_conn.c

## 哈希表

``` c

/*
 *  Connection hash table: for input and output packets lookups of IPVS
 */
static struct hlist_head *ip_vs_conn_tab __read_mostly;
```

哈希表的大小为`ip_vs_conn_tab_size`，这个大小是由比特数`ip_vs_conn_tab_bits`确定的，可以被修改(通过module params[^5])。

该结构在ipvs初始化的时候被建立（`ip_vs_conn_init`）。  
保存的数据是`ip_vs_conn`结构。

`ip_vs_conn_in_get`函数对哈希表进行查找（三元组），它对`ip_vs_conn_hashkey_param`函数作了包装，参数由协议来实现补充。

### `ip_vs_conn_hashkey_param`

根据param生成`hashkey`，param是下面这个结构

``` c
struct ip_vs_conn_param {
    struct netns_ipvs       *ipvs;
    const union nf_inet_addr    *caddr;
    const union nf_inet_addr    *vaddr;
    __be16              cport;
    __be16              vport;
    __u16               protocol;
    u16             af;

    const struct ip_vs_pe       *pe;
    char                *pe_data;
    __u8                pe_data_len;
};
```

但只选择`caddr`和`vaddr`的一种进行哈希(根据`bool`参数`inverse`与否)。

### `ip_vs_conn_hashkey`

对`proto`, `addr`, `port`参数进行`jhash`运算

### `ip_vs_conn_in_get`

将参数传给`ip_vs_conn_hashkey_param`得到`hashkey`，取出哈希表中的条目。

``` c
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
```

### 回收策略

`keventd`定期回收哈希表中的条目，以防止内存溢出。相关的函数是`ip_vs_random_dropentry`

# `ip_vs_app.c`

提供应用层的支持，目前为`ftp`。

`app incarnation`我的理解是某种应用层协议所依赖的端口。

# `ip_vs_sched.c`

主要实现了调度算法的动态加载。

``` c
static struct ip_vs_dest *
ip_vs_schedule(struct ip_vs_service *svc, const struct sk_buff *skb,
          struct ip_vs_iphdr *iph)
```

`ip_vs_schedule`函数根据报文(`skb`)和ip头(`iph`)，从目前的服务表(`svc`)当中选择一个最合适的服务。具体的实现由动态加载的模块完成。

# ip_vs_est.c

统计某个开启的服务(service), 真实服务器(destination)，以及全部流量(tot_stats)的报文数目、速率信息。

> 这部分代码当中有一些术语。Jiffy表示内核中的单位时间，也就是时间中断的次数，一个tick表示增加了一个单位时间，HZ表示一秒中Jiffy增加的个数。`now = jiffies;`表示读取`jiffies`这个全局变量，它记录了内核运行到当前时间的tick数目。

ip_vs以每两秒的频率更新数据；同时，每隔`IPVS_EST_TICK`就会检查有没有数据需要更新。

``` c
/* Process estimators in multiple timer ticks (20/50/100, see ktrow) */
#define IPVS_EST_NTICKS     50
/* Estimation uses a 2-second period containing ticks (in jiffies) */
#define IPVS_EST_TICK       ((2 * HZ) / IPVS_EST_NTICKS)
```

`ip_vs_est.c`主要就是为了往`ip_vs_estimator`结构写一些统计数据。

``` c
/* IPVS statistics objects */
struct ip_vs_estimator {
    struct hlist_node   list;

    u64         last_inbytes;
    u64         last_outbytes;
    u64         last_conns;
    u64         last_inpkts;
    u64         last_outpkts;

    u64         cps;
    u64         inpps;
    u64         outpps;
    u64         inbps;
    u64         outbps;

    s32         ktid:16,    /* kthread ID, -1=temp list */
                ktrow:8,    /* row/tick ID for kthread */
                ktcid:8;    /* chain ID for kthread tick */
};
```

这里的`:16`这种写法是为了控制字段的大小。  
`ip_vs_estimator`结构是嵌入在`ip_vs_stats`里面的，而这个结构嵌入在`netns_ipvs`中。

``` c
struct ip_vs_stats {
    struct ip_vs_kstats kstats;     /* kernel statistics */
    struct ip_vs_estimator  est;        /* estimator */
    struct ip_vs_cpu_stats __percpu *cpustats;  /* per cpu counters */
    spinlock_t      lock;       /* spin lock */
    struct ip_vs_kstats kstats0;    /* reset values */
};
```

这里有一个变量声明为percpu的`cpustat`，关于percpu在后面([\#Per-CPU](#Per-CPU "wikilink"))中有介绍。ipvs处理进出的报文时，会分别调用`ip_vs_in_stats`和`ip_vs_out_stats`方法，更新percpu的变量值。这些函数会分别为service, destination, tot_stats更新一次。

``` c
u64_stats_update_begin(&s->syncp);
u64_stats_inc(&s->cnt.outpkts);
u64_stats_add(&s->cnt.outbytes, skb->len);
u64_stats_update_end(&s->syncp);
```

主要更新的就是报文数和数据量。est模块主要就是根据这个`cpustat`来更新数据。  
### 实现

通过开启一个`kthread`实现。

`kthread`执行`ip_vs_estimation_kthread`函数，该函数每`IPVS_EST_TICK`就会检查需不需要更新（检查一个大小为`IPVS_EST_NTICKS`的数组，每次加一，这样检查到的正好时间间隔为2秒）。如果需要，调用`ip_vs_tick_estimation`来更新数据。

`ip_vs_tick_estimation`会对每一个estimator（源码也称作`chain`）都调用`ip_vs_chain_estimation`函数。这个函数主要更新报文个数和速率，速率用下面的公式更新：

``` txt
    avgrate = avgrate*(1-W) + rate*W
    where W = 2^(-2)
```

其中，rate表示两秒内的速率，右边的avgrate表示之前的值，这样就能够计算出新的avgrate值。  
### 数据访问

得到的数据会拷贝到`ip_vs_stats_user`结构发送到用户态程序中。用户可以得到下面的数据(文件在`linux/include/uapi/linux/ip_vs.h`，`uapi`表示该头文件会被复制到系统头文件中，c语言`#include <linux/ip_vs.h>`即可访问到)：

``` c
/*
 *  IPVS statistics object (for user space)
 */
struct ip_vs_stats_user {
    __u32                   conns;          /* connections scheduled */
    __u32                   inpkts;         /* incoming packets */
    __u32                   outpkts;        /* outgoing packets */
    __u64                   inbytes;        /* incoming bytes */
    __u64                   outbytes;       /* outgoing bytes */

    __u32           cps;        /* current connection rate */
    __u32           inpps;      /* current in packet rate */
    __u32           outpps;     /* current out packet rate */
    __u32           inbps;      /* current in byte rate */
    __u32           outbps;     /* current out byte rate */
};
```

用命令行程序`ipvsadm`也可以看到这些数据(`--stat`)。

# `ip_vs_xmit.c`

执行对数据包的修改；实现的函数被写入`ip_vs_conn`的函数指针里，`packet_xmit`。  
`packet_xmit`函数被`ip_vs_in`调用。

三种传输的模式：

- Nat (`ip_vs_nat_xmit`)
- Direct Routing (`ip_vs_dr_xmit`)
- Tunnel (`ip_vs_tunnel_xmit`)

### `ip_vs_nat_xmit`

该函数仅处理外部访问内部的请求，而内部发往外部的包由[\#handle_response](#handle_response "wikilink")来处理。

通过`__ip_vs_get_out_rt`函数找到到达目标的路由。

报文处理依赖于协议的实现；调用协议中的`dnat_handler`方法，让协议更改端口号和校验和等字段。随后改变ip地址信息。  
最后通过`ip_vs_nat_send_or_cont`函数，通知netfilter以`NF_INET_LOCAL_OUT`继续处理。

<figure>
<img
src="IPVS%20source-media/27b00665c7ad0b339265b10ee32aac0424c4a7da.png"
title="wikilink" alt=" Nat transmission" />
<figcaption aria-hidden="true"> Nat transmission</figcaption>
</figure>

### `ip_vs_dr_xmit`

此方法采用mac地址重写的方式，需要和真实服务器在同一个子网下。一般，这种情况下效率最高，因为只有进入的流量需要处理。

真实服务器需要首先设置一个`dummy device`，它的地址需要和ipvs的虚拟服务器地址相同。当接收到报文，ipvs找到dest的路由，然后发给dest。dest接收到该报文，由于dummy device的地址与报文的目标相同，进而由loopback设备处理。

### `ip_vs_tunnel_xmit`

# 协议实现

## `ip_vs_proto_tcp.c`

### `tcp_snat_handler`

### `tcp_dnat_handler`

将tcp的头中的端口设置为目标的端口号，然后设置checksum。

### 状态转移

tcp定义了两种状态表`tcp_states`，`tcp_states_dos`，其中`tcp_states_dos`当secure_tcp选项开启时，会被选择。

内核中对secure_tcp的文档如下[^6]：  
secure_tcp - INTEGER  
- 0 - disabled (default)  
The secure_tcp defense is to use a more complicated TCP state transition table. For VS/NAT, it also delays entering the TCP ESTABLISHED state until the three way handshake is completed.

# 调度算法

ipvs中的调度算法采用dkms(可加载模块)方式动态加载，用户可以在`ipvsadm`中选择一个算法。  
每个调度算法可以把状态保存在`sched_data`里面。

一部分涉及到哈希的算法采用的是`hash32`

``` c
#ifndef HAVE_ARCH__HASH_32
#define __hash_32 __hash_32_generic
#endif
static inline u32 __hash_32_generic(u32 val)
{
    return val * GOLDEN_RATIO_32;
}

#ifndef HAVE_ARCH_HASH_32
#define hash_32 hash_32_generic
#endif
static inline u32 hash_32_generic(u32 val, unsigned int bits)
{
    /* High bits are more random, so use them. */
    return __hash_32(val) >> (32 - bits);
}
```

## ip_vs_sh.c 源哈希调度

> 当开启了`IP_VS_SVC_F_SCHED_SH_PORT`标识位，也会哈希端口号，否则直接置端口号为0

- 哈希对象：源ip (+源端口)
- 哈希算法：`hash32`(通用哈希算法)

哈希表的大小由`CONFIG_IP_VS_SH_TAB_BITS`定义

调度算法根据哈希值来选择调度的目标(destination)。

当真实服务器出现变动时，`ip_vs_sh_reassign`函数就更新一次哈希表和服务之间的绑定关系。更新的方法：首先将现有的destination清空，维护一个计数器，将destination添依次与buckets相对应，直到计数器满为止。

``` c
if (++d_count >= atomic_read(&dest->weight)) {
                p = p->next;
                d_count = 0;
            }
```

## `ip_vs_dh.c` 目标哈希调度

哈希对象采用目标ip，其他基本与源哈希基本一致，但是没有读取weight字段，而是直接依次将destination绑定到对应的bucket上。

# 一些其它的常见linux内核知识

## 常规数据结构

### 侵入式链表

linux内核的大部分结构都是侵入式的。  
使用时，数据结构需要添加一个`list_head`的成员。  
链表遍历使用`list_for_each_entry`宏

``` c
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");

struct my_list {
    int data;
    struct list_head list;
};

static LIST_HEAD(my_linked_list);

void add_to_list(int data) {
    struct my_list *new_node;

    // Allocate memory for the new node.
    new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node)
        return;

    // Initialize the node's data.
    new_node->data = data;

    // Add the new node to the start of the linked list.
    list_add(&new_node->list, &my_linked_list);
}

void remove_from_list(int data) {
    struct my_list *current_node;
    struct list_head *tmp, *pos;

    list_for_each_safe(pos, tmp, &my_linked_list) {
        current_node = list_entry(pos, struct my_list, list);
        if (current_node->data == data) {
            // Remove the node from the list.
            list_del(pos);
            // Free the memory associated with the node.
            kfree(current_node);
            return;
        }
    }
}

void print_list(void) {
    struct my_list *node;

    list_for_each_entry(node, &my_linked_list, list) {
        printk(KERN_INFO "%d\n", node->data);
    }
}

static int __init list_module_init(void) {
    printk(KERN_INFO "List module loaded\n");

    add_to_list(1);
    add_to_list(2);
    add_to_list(3);

    print_list();

    return 0;
}

static void __exit list_module_exit(void) {
    struct my_list *current_node;
    struct list_head *tmp, *pos;

    list_for_each_safe(pos, tmp, &my_linked_list) {
        current_node = list_entry(pos, struct my_list, list);
        list_del(pos);
        kfree(current_node);
    }

    printk(KERN_INFO "List module unloaded\n");
}

module_init(list_module_init);
module_exit(list_module_exit);
```

### 哈希链表

哈希链表也是一个侵入式的结构。数据需要在其定义中添加一个`hlist_node`。

示例：

``` c
#include <linux/types.h>
#include <linux/list.h>

struct my_struct {
    int data;
    struct hlist_node my_hash_list;
};

#define HASH_SIZE  128
static struct hlist_head my_hash_table[HASH_SIZE];

unsigned int my_hash_function(int data) {
    // Simple hash function that returns an index based on the data.
    return data % HASH_SIZE;
}

void add_to_hash_table(int data) {
    struct my_struct *new_item;
    unsigned int hash_index;

    // Allocate a new structure instance.
    new_item = kmalloc(sizeof(*new_item), GFP_KERNEL);
    if (!new_item)
        return;

    // Assign data to the new item.
    new_item->data = data;

    // Calculate the hash index.
    hash_index = my_hash_function(data);

    // Add the new item to the hash table.
    hlist_add_head(&new_item->my_hash_list, &my_hash_table[hash_index]);
}

void delete_from_hash_table(int data) {
    struct my_struct *item;
    struct hlist_node *tmp;
    unsigned int hash_index = my_hash_function(data);
    int bkt;

    // Iterate over the hash list at the calculated index and remove the item.
    hlist_for_each_entry_safe(item, tmp, &my_hash_table[hash_index], my_hash_list) {
        if (item->data == data) {
            hlist_del(&item->my_hash_list);
            kfree(item);
            return;
        }
    }
}

void init_hash_table(void) {
    int i;

    // Initialize all heads in the hash table.
    for (i = 0; i < HASH_SIZE; i++) {
        INIT_HLIST_HEAD(&my_hash_table[i]);
    }
}
```

## 并发相关

并发在linux内核编程下是一个比较复杂的话题，因为代码运行的上下文不同，可能持有不同的锁，比如netfilter的钩子就处于一个被RCU保护的锁里；同时，也有可能处于某个spinlock或者mutex里，如果处理不当可能导致一些并发相关的问题。

### Per-CPU

如果一个变量声明为percpu的，那么它就会为每一个cpu分配一次；每个cpu独享该变量，任何cpu都不能够访问其他cpu的值。下面的表描述了对于percpu变量的操作。

<figure>
<img
src="IPVS%20source-media/8ce8d83cd5e3dfa701fa9183d5b297ab915c7d6e.png"
title="wikilink" alt="Pasted image 20231106225654.png" />
<figcaption aria-hidden="true">Pasted image
20231106225654.png</figcaption>
</figure>

> percpu变量必须在非抢占式的上下文中进行。关于抢占和非抢占，可以参考现代操作系统(Modern Operating Systems)的2.4节。

### spinlock

自旋锁是最简单的锁机制，用于保护很短时间内的代码段。当一个核（CPU核心）尝试获取一个已经被另一个核持有的自旋锁时，它会处于循环等待状态，也就是“自旋”，直到锁被释放。这意味着它不会将当前的执行线程置于睡眠状态。

**特点：**

- 高效：对于那些仅需要锁定很短时间的情况，自旋锁很有用，因为它避免了线程调度的开销。
- 简单：自旋锁的实现比较简单。
- 忙等待：自旋锁不会使线程进入睡眠，因此在多处理器系统上不会引发线程调度。
- 不能递归：一个线程不能多次获取同一个自旋锁，否则会造成死锁。

**适用场景：**

- 锁持有时间非常短。
- 系统不希望在这种短时间操作中发生上下文切换。

### mutex

如果lock失败会主动让出控制权。  
互斥锁提供了一种睡眠等待的锁机制，如果一个线程尝试获取一个已经被持有的锁，这个线程会进入睡眠状态（也就是不占用 CPU 资源），等待直到这个锁被释放。

**特点：**

- 睡眠和唤醒：当锁不可用时，请求锁的线程会睡眠，释放 CPU，直到锁可用时才被唤醒。
- 可以递归：依赖具体实现，有些互斥锁允许同一个线程多次获取锁。
- 更复杂：互斥锁比自旋锁复杂，因为它们涉及到线程调度。
- 公平性：互斥锁通常具有某种形式的公平性或调度策略，来决定哪个线程应该获得锁。

**适用场景：**

- 锁持有时间相对较长。
- 系统希望在等待锁的时候可以做其他工作（CPU可以切换去执行其他任务）。

### RCU(Read, Copy, Update)

这篇博客[^7]讲的比较详细。

数据的读取只需要很小的开销，但是数据的更新需要较大的开销。适用于不常更新的场景，比如一些函数指针，预先的配置等。

`synchronize_rcu` 等待当前线程没有使用旧的结构的内存。  
`call_rcu`则是在`synchronize_rcu`的基础上再运行回调函数。  
`rcu_assign_pointer`执行RCU的更新操作。  
`rcu_dereference`解引用。  
`rcu_barrier`：等待所有回调函数结束执行。

在某些上下文中，由函数的调用方持有`rcu_read_lock`，因此就不需要重复调用；例如，`netfilter`的钩子中就处于读取的`critical section`中（因为hook函数指针本身就是被RCU保护的）。message[^8]列出了一些ipvs的上下文持有的锁。  
In IPVS we have the following contexts:  
- packet RX/TX: does not need locks because packets come from hooks  
- sync msg RX: backup server uses RCU locks while registering new conns  
- ip_vs_ctl.c: configuration get/set, RCU locks needed  
- xt_ipvs.c: It is a NF match

#### 示例：链表

例如，如果需要为链表提供RCU的支持，我们将`list_add`函数替换为`list_add_rcu`（该函数包装了`rcu_assign_pointer`）。注意`list_add_rcu`需要在锁的环境下运行，以防止对数据的更改同时发生；但是该函数可以与读取操作并发进行。

类似的，将链表的删除函数替换为：`list_del_rcu`

还有链表的遍历操作：  
`list_for_each_entry_rcu`：能够和`list_add_rcu`并发运行，但是要在`rcu_read_lock`环境下。

### 无锁结构

`atomic_t`

`atomic_t` 是一种提供原子操作的整数类型，它通常用于实现计数器，例如跟踪资源的使用次数，或者实现简单的统计和标记。原子操作保证了在多处理器系统中，任何时候只有一个处理器能够对变量进行操作，这样可以避免在并发访问时出现的竞态条件。

原子操作通常是通过硬件级别的指令来实现的，以保证操作的原子性。例如，在 x86 架构中，`inc` 和 `dec` 指令可以用来对内存中的值进行原子递增和递减。

在内核中，原子操作通过一组宏和函数提供，例如 `atomic_set()`，`atomic_read()`，`atomic_inc()` 和 `atomic_dec()` 等，这些操作用于 `atomic_t` 类型的变量。

### 引用计数

refcount_t

`refcount_t` 是一种特殊的原子类型，它专门用于实现引用计数。引用计数是一种内存管理技术，用于跟踪对象被引用的次数，以决定何时可以安全地释放对象。当引用计数降到零时，对象可以被回收。

`refcount_t` 和 `atomic_t` 的主要区别在于，`refcount_t` 提供了溢出检查和其他安全性检查，以防止引用计数的滥用和错误，这些错误可能导致内存泄漏或者双重释放。`refcount_t` 通过一组特定的函数操作，例如 `refcount_set()`，`refcount_inc()`，`refcount_dec_and_test()` 等。

使用 `refcount_t` 比 `atomic_t` 更安全，因为它可以防止引用计数变成负数或者因为溢出而变得不正确。这样可以减少由于错误的引用计数操作导致的内存损坏和安全问题。

在早期的内核版本中，引用计数通常使用 `atomic_t` 来实现，但由于上述的原因，引入了 `refcount_t` 作为替代，提供了更健壮的引用计数实现。

### 一些命名的惯例

put表示减少一个应用计数；hold表示增加一个引用计数；free表示若引用计数减少后为0，释放该数据。

## 哈希函数

### `jhash`

代表“Jenkins hash”，以其创造者Bob Jenkins命名。它是一个著名的哈希函数，可以高效地将任意长度的数据映射到32位值。jhash函数经常用于哈希表查找，其中需要均匀分布的哈希值以最小化碰撞。它在网络相关的哈希表中特别受欢迎，比如用于路由或过滤的哈希表。

ebpf map的布隆过滤器就用到了jhash[^9]，通过设置不同的seed实现k个哈希函数。

### `xxhash`

xxHash是一个高速哈希算法，最初并不是Linux内核的一部分(2017年一个commit[^10])。它以提供非常快速的哈希函数和优秀的分布性能和碰撞抗性而闻名。它由Yann Collet创建，并广泛用于各种软件项目。

xxHash的主要卖点是其速度和适用于性能敏感领域（如实时数据处理）的特点，在这些领域中，它可以高效地对数据进行哈希计算，并且开销很小。

这个[^11]patch讨论了不同哈希的速度。

> The experimental results (the experimental value is the average of the  
> measured values)  
> crc32c_intel: 1084.10ns  
> crc32c (no hardware acceleration): 7012.51ns  
> xxhash32: 2227.75ns  
> xxhash64: 1413.16ns  
> jhash2: 5128.30ns

## `struct sock` , `struct sk_buff`

> 参考资料  
> http://vger.kernel.org/~davem/skb_sk.html  
> http://vger.kernel.org/~davem/skb.html

`struct sk_buff`[^12]表示内核中的一个数据包，一般简称为`skb`。

当网卡接收数据时，`sk_buff`通常由网卡的驱动程序调用`netdev_alloc_skb`或`alloc_skb`来创建。当报文自底向上由协议栈处理时，由`skb_pull`减少数据报长度。

当需要发送数据时，由`sock_alloc_send_skb`和`sock_alloc_send_pskb`创建一个`sk_buff`。在报文自顶向下经过协议栈处理时，由`skb_push`来添加头部。

需要注意的是，skb本身并不保存报文当中的数据，但是有指向这些数据的指针:  
- head  
- data  
- tail  
- end

`struct sock`表示内核中经过udp、tcp协议处理后的一个`socket`，用于保存当前链接的状态。一般简称为`sk`。有一些比较重要的信息：  
- 通信双方的地址和端口  
- 链接状态  
- 函数指针

inet_create

sk_alloc

sk_common_release

### skb 和 sk的关系

sock是一个更上层的概念，接收到的报文需要经过传输层的处理后才会将一个skb和sock相关联（通过`__inet_lookup_skb`函数查找，也是一个查找哈希表的过程，`inet_hashtables.c`），如果本机并不处理该报文，例如作为路由器转发，那么就不会给skb关联一个sock结构。  
sock保存了一个传输层链接的状态，例如对于TCP而言，一个链接有`LISTEN`, `ESTABLISHED`, `CLOSE_WAIT`等一系列状态，这些状态保存在`struct tcp_sock`里面，还有一些链接的参数，比如滑动窗口值都保存在这里面。

这部分有几篇博客可以参考[^13][^14]

## kthread

一个kthread是一个调度的对象。

## 一些宏

`__builtin_constant_p` 检查某个变量是否是编译期确定的。

`INDIRECT_CALL_1` , 执行`f`，忽略掉`f1`（如果配置了`CONFIG_RETPOLINE`，会执行一些编译优化，防止函数指针被修改，比如直接执行f1而不通过函数指针间接执行）

``` c
#define INDIRECT_CALL_1(f, f1, ...) f(__VA_ARGS__)
```

`__read_mostly`： 缓存相关的优化

`unlikely` `likely`: 编译优化辅助的宏，指定条件语句向哪种条件优化。

## Hacks

### `do {} while(0)`

c语言的宏替换是发生在生成ast之前的，因此宏的不当使用可能会导致对语法树的污染。例如，下面的例子（来源[^15]）

    ＃define foo(x)  a(x); b(x)

看似正确，但是它们有不同的ast表示，例如如果某种语法结构仅接受一条语句，那么我们调用foo的时候，认为它是一个函数，但其实语法上是两条语句，这时就会发生错误，或者产生非预期的结果。例如，

    if (a > 0)
        foo(x);

这时函数b并不会处于if块内部。

因此在定义宏的时候必须非常小心；`do {} while(0)`可以被看作是唯一不会对语法树进行污染的宏。(不会产生副作用)

# Appendix (Terminology)

### 虚拟服务器

指ipvs对于外部主机的入口点。  
\### 真实服务器

指配置到ipvs上的服务器，ipvs将发往虚拟服务器的包根据网络情况分发到真实服务器上。

### DNAT（目的网络地址转换）

1.  **目的**：DNAT主要用于更改目标IP地址。
2.  **应用场景**：当外部网络的请求需要路由到内部网络的特定设备或服务时使用，如托管在内部网络中的网站或服务器。

### SNAT（源网络地址转换）

1.  **目的**：SNAT主要用于更改数据包的源IP地址。
2.  **应用场景**：当内部网络（如家庭或企业网络）中的设备想要访问外部网络（如互联网）时，SNAT会将这些设备的私有（内部）IP地址转换为公共（外部）IP地址。

# Bibliography

(书目)

(Tanenbaum and Bos
2015) 介绍了一些操作系统方面的理论知识，可以参考参考。  
(Bovet and Cesati 2006) 是一本比较全面的讲linux内核知识的书。  
(Benvenuti
2006) 描述了linux内核网络堆栈的一些知识；比较详细，不过个人感觉讲的太多了，可以选择性地看一些章节。  
(Seth, Venkatesulu, and Ajaykumar Venkatesulu
2008) 主要讲tcp ip在内核的设计和实现。  
(Rosen
2014) 这一本也是讲网络相关的。算比较全面，netfilter和ipv4相关的都有。  
(Stevens and Wright 1994) 这一本比较老，而且是基于`BSD`系统的。  
(Fall and Stevens
2012) 主要讲原理方面的，可以用来加深tcp ip协议的理解。  
(“The Linux Kernel Module Programming Guide”
n.d.) 开源书，讲了怎么开发一个内核模块，比较适合入门。

<div id="refs" class="references csl-bib-body hanging-indent"
entry-spacing="0">

<div id="ref-benvenutiUnderstandingLinuxNetwork2006" class="csl-entry">

Benvenuti, Christian. 2006. *Understanding Linux Network Internals*.
Nachdr. Guided Tour to Networking on Linux. Beijing Köln: O’Reilly.

</div>

<div id="ref-bovetUnderstandingLinuxKernel2006" class="csl-entry">

Bovet, Daniel P., and Marco Cesati. 2006. *Understanding the Linux
Kernel*. 3rd ed. Beijing ; Sebastopol, CA: O’Reilly.

</div>

<div id="ref-fallTCPIPIllustrated2012" class="csl-entry">

Fall, Kevin R., and W. Richard Stevens. 2012. *TCP/IP Illustrated*. 2nd
ed. Addison-Wesley Professional Computing Series. Upper Saddle River,
NJ: Addison-Wesley.

</div>

<div id="ref-rosenLinuxKernelNetworking2014" class="csl-entry">

Rosen, Rami. 2014. *Linux Kernel Networking: Implementation and Theory*.
The Expert’s Voice in Open Source. New York, NY: Apress.

</div>

<div id="ref-sethTCPIPArchitectureDesign2008" class="csl-entry">

Seth, Sameer, M. Ajaykumar Venkatesulu, and M. Ajaykumar Venkatesulu.
2008. *TCP-IP Architecture, Design and Implementation in Linux*.
Hoboken: Wiley.

</div>

<div id="ref-stevensTCPIPIllustrated1994" class="csl-entry">

Stevens, W. Richard, and Gary R. Wright. 1994. *TCP/IP Illustrated*.
Addison-Wesley Professional Computing Series. Reading, Mass:
Addison-Wesley Pub. Co.

</div>

<div id="ref-tanenbaumModernOperatingSystems2015" class="csl-entry">

Tanenbaum, Andrew S., and Herbert Bos. 2015. *Modern Operating Systems*.
Fourth edition, global edition. Always Learning. Boston Columbus
Indianapolis: Pearson.

</div>

<div id="ref-LinuxKernelModule" class="csl-entry">

“The Linux Kernel Module Programming Guide.” n.d. Accessed November 5,
2023. <https://sysprog21.github.io/lkmpg/>.

</div>

</div>

[^1]: https://switch-router.gitee.io/blog/netfilter1/

[^2]: https://wiki.nftables.org/wiki-nftables/index.php/Netfilter_hooks

[^3]: https://opengers.github.io/openstack/openstack-base-netfilter-framework-overview/

[^4]: https://www.zhihu.com/question/20159552/answer/14169729

[^5]: https://tldp.org/LDP/lkmpg/2.6/html/x323.html

[^6]: https://docs.kernel.org/networking/ipvs-sysctl.html

[^7]: https://zhuanlan.zhihu.com/p/89439043

[^8]: https://marc.info/?l=netfilter-devel&m=149562884514072&w=2

[^9]: https://github.com/torvalds/linux/blob/master/kernel/bpf/bloom_filter.c

[^10]: https://github.com/torvalds/linux/commit/5d2405227a9eaea48e8cc95756a06d407b11f141

[^11]: https://patchwork.kernel.org/project/linux-mm/patch/20180525011657.4qxrosmm3xjzo24w@xakep.localdomain/

[^12]: https://docs.kernel.org/networking/skbuff.html

[^13]: https://switch-router.gitee.io/blog/linux-net-stack/

[^14]: https://arthurchiao.art/blog/linux-net-stack-implementation-rx-zh/

[^15]: https://laoar.github.io/blogs/289/
