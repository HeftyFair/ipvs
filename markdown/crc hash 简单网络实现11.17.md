## P4内置Hash函数
`v1model.p4`中提供了以下8种哈希方法
```c
enum HashAlgorithm {
    crc32,
    crc32_custom,
    crc16,
    crc16_custom,
    random,
    identity,
    csum16,
    xor16
}
```
在`/root/P4/behavioral-model/src/bm_sim/calculations.cpp`中，实现了以上几种哈希方法

## 实验环境
- `cpu`: AMD Ryzen 7 3700X
- `内存`：16GB 2666MHz
- `系统`：Ubuntu 22.04.3 LTS

## 带宽测试
> - 仅使用基于端口的转发策略
> - 在debug模式下，behavior-model的带宽受限基本在160Mb/s左右
>  <img src = 'source\源码阅读\originalBw_debug.png'>
> - 重新对bmv2编译，不使用debug模式，用iperf3测量带宽，时间10s。可知虚拟网络带宽已不受限,在5.4Gb/s左右
> ```bash
> cd ~/p4-tools/
> git clone https://github.com/p4lang/behavioral-model.git bmv2-opt
> cd bmv2-opt
> git checkout 62a013a15ed2c42b1063c26331d73c2560d1e4d0
> ./autogen.sh
>./configure --without-nanomsg --disable-elogger --disable-logging-macros 'CFLAGS=-g -O2' 'CXXFLAGS=-g -O2'
> make -j 2
> sudo make install
> sudo ldconfig
> ```
> <img src = 'source\源码阅读\originalBw.png'>
>
> - 如果需要使用debug模式，可以使用以下命令恢复
> ```bash
> cd ~/p4-tools/bmv2
> sudo make install
> sudo ldconfig
> ```
## CRC16
> ### 带宽测试
> - 测试时间：1200s
> - 网络拓扑：同带宽测试
> - 转发策略：基于交换机端口的转发策略
> <img src = 'source\源码阅读\crc16Bw.png'>


#CRC32
> 如果对CRC32的每一个哈希值进行分配一个大小为 sizeof(int)的
> ## 带宽测试
> - 测试时间：1200s
> - 网络拓扑：同带宽测试
> - 转发策略：基于交换机端口的转发策略
> <img src = 'source\源码阅读\crc32Bw.png'>

### 哈希速度测试
 - 文件大小：4.72MB(测试中buffer无法超过10MB，暂未发现解决方案，减小文件读取带来的额外开销)
 - buffer：5MB
 - 对文件进行10次哈希，取平均值

<table>
       <caption>v1model 内置Hash速度</caption>
   <tr>
       <th>Hash</th>
       <th>Speed(MB/s)</th>
       <th>描述</th>
   </tr>
   </tr>
   
   <tr>
       <td>CRC16</td>
       <td>25.39</td>
       <td>16bit</td>
   </tr>
   <tr>
       <td>CRC32</td>
       <td>25.15</td>
       <td>32bit</td>
   </tr>
   <tr>
       <td>XOR16</td>
       <td>786.67</td>
       <td>16bit</td>
   </tr>
   <tr>
       <td>IDENTITY</td>
       <td>9731.96</td>
       <td>64bit;最多只将前8个字节相加</td>
   </tr>
   <tr>
       <td>CSUM16</td>
       <td>2950</td>
       <td>16bit</td>
   </tr>
</table>

---
# 11.26更新
## 改进网络拓扑
- `Host`:`h1`-`h6`
- `switch`: `s2`,`s3` 主要负责局域网内部的转发和充当网关作用， `s1`负责转发目的ip为特定网络(10.0.1.0/24, 10.0.2.0/24)的pkt 
- Address assignment strategy：mixed
  - 首先根据交换机创建的顺序分配switch_id
  - 根据switch_id分配网络号
  - 根据主机的id分配主机号
  - 交换机之间的链路，两个接口的ip地址单独分配，mac地址随机分配。


<img src = 'source\工作调研\topology_11_24.png'>

![networkinfo](source/工作调研/network%20info.png 'networkinfo')

## 目前遇到的问题
- 可以使用`P4utils` 添加普通交换机s2,s3，但是无法手动/自动设置转发表。
- 交换机之间的链路，两个端口的mac地址是随机的，转发规则不能写死。
- 如果全部使用P4交换机，并且设置自动arp
  - 此时在同一局域网内部是可以ping通的(h1 ping h2)
  ![h1 ping h2](source\工作调研\h1pingh2.png 'h1 ping h2') 
  但是跨局域网的ping是不通的(例如h1 ping h4)
  ![h1 ping h4](source/工作调研/h1%20ping%20h4.png 'h1 ping h4')
## 问题解决方案
- 需要解决跨局域网无法通信的问题。首先在调试模式下，使用`h1 ping h4`
  发现数据包在到达`s3`时被drop，而mac地址为00:01:0a:00:01:03(h1的网关mac地址)
  ![h1 ping h4](source/工作调研/h1%20ping%20h4%20failure%20log.png 'h1 ping h4 falure')
  猜测arp表只保存在各个host，而p4交换机与协议无关，交换机不支持arp协议。因此在同一局域网内部的通信，由于主机可以直接通过arp完成地址转换，因此通信不受影响；而跨局域网的通信，由于没有arp的支持，因此mac地址在到达目的网络之后不会被转换，进而被drop。
- 协议无关的特性可以让我们不关注交换机之间的链路。
- 为了完成地址转换，在`s1`的controller中实现了一个`类arp`的功能
  - 在p4程序中添加一个新的表`ip2Addr`，匹配字段`dstIP`，匹配成功则将pkt的目的mac转换为目的主机的mac。
  - 在controller启动后，通过网络拓扑获取到与s1直接相连的局域网
    - 对于每个局域网，进行结点筛选，将host结点筛选出来
    - 将host的ip地址和mac地址的映射关系添加到表`ip2Addr`中
- 至此，两个局域网之间的通信已经解决
   ![pingall success](source/工作调研/pingall%20success.png 'pingall success')

---
#12.4更新
## 利用Count-Min Sketch 完成流的数据包数量统计
- 架构设计
  - pkt进入数据层面，哈希后进入相应的桶进行统计
    - Count-Min sketch的数据结构使用Register实现
  - Controller读取P4交换机的Register，完成统计。
  ![count-min_sketch_architeture](source/工作调研/Count-Min%20Sketch%20包数量统计架构.png 'Architecture')
###控制器读取register
- 首先要关注`thrift_API`中关于register中的操作
  重置register
  ```python 
  def reset_registers(register_name):
    """
      args:
        register_name(str):name of the register
    """
  ```
  读取register。
  ```python
  def register_read(self, register_name, index=None, show=False):
    """Read register value
      args:
        register_name(str): name of the register
        index(int):         index in the array of registers (if **None**, the whole array will be read)
        show(bool):         enable verbose output
      Returns:
        int or list:register value of list of values
    """
  ```
  如果指定了`index`，则只返回`int`类型的数据；否则将返回一个元素`int`的列表。无论如何，每个桶的具体值(无论指定多少bits)都会被映射为`int`类型。值得注意的是，在64位的机器上， python的`int`长度为8字节，而我们最多需要6个字节，所以提取出来8字节的长度是完全够的。
  ![bytes one bucket needs](source/工作调研/nuber%20of%20Bytes%20one%20bucket%20needs.png)

  写入register。index可以是`int`类型或者是`list`
  ```python
  def register_write(self, register_name. index, value)
  """Read register value
      args:
        register_name(str):       name of the register
        index(int or list):       index in the array of registers or ``[star_index, end_index]``
        value(int):               value to write
      Returns:
        int or list:register value of list of values
    """
  ```
- **读取的方案**
  - 如果使用`crc16_custom`或者`crc32_custom`, 可以通过声明时的顺序与register_name（register的变量名）进行绑定，在controller中通过
    ```python
    get_crc_calcs_num()
    ```
  来获得自定义crc哈希使用的次数
  - 通用方法
    - 直接获取交换机中的register信息
    ```python
    get_register_array()
    ```
    该方法返回一个字典，键为`register_name`，值为`RegisterArray`对象，该对象包括的属性有
    - `name`：register name
    要注意，这里的`name`返回的并不是我们在p4程序中定义的register变量名，其格式为`Pipeline_name.register_name`
    举个例子，在p4中，我们在`MyIngress`这个控制块中，定义了一个名为`sketch1`的register
      ```
      control MyIngress(...){
        register<64>(28) sketch1;
        ....
      }
      ```
      则通过`get_register_array()`方法获得的name为
      ```
      "MyIngress.sketch1"
      ```
      因此在读取之前需要进一步处理得到register_name
    - `id`: register id
    - `size`: register size
    - `bitwidth`: register cell size(Sketch bucket size)
  - **目前进展**
    - 已经在交换机的p4程序中实现了count-min sketch，正在controller中实现统计内容的提取工作（在controller中重新实现identity算法+读register+结果呈现）
  - **下一步工作**
    - 需要升级p4编译器为`20200408`版本以上。目前在使用的`20180101`版本的P4编译器对register的读写时传入参数的位宽有限制（32bits，而identity的hash结果为64bits）
    这是写入寄存器的，可以看到对寄存器的索引有一个32位的限制。
    ![version_error1](source/工作调研/version%2020180101(1).png)
    读取寄存器同样
    ![version_error2](source/工作调研/version%2020180101(2).png)
    - 在controller中完成流包数量统计的读取，主要是检验在controller实现identity hash和p4内置identity是否一致。后续在这个基础上进一步改进sketch。
