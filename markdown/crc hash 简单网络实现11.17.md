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
> <img src = 'source\源码阅读\originalBw.png'>
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