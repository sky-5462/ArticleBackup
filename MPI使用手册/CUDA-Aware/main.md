# CUDA-Aware

## 一些相关的理论

### CUDA-Aware MPI的原理

CUDA支持统一虚拟地址(UVA, Unified Virtual Addressing)，这是CUDA-Aware MPI的必要条件，因为MPI的指针接口只接收指针的数值，在不支持UVA的情况下该数值可能指向内存或显存的多个位置，而在支持UVA的情况下该数值指向全局唯一的位置

CUDA-Aware MPI中的"Aware"表示MPI是否能从传入地址推断出是内存位置还是显存位置，如果MPI认为地址空间内只有内存，使用CPU访存操作不加区分地处理，那么当指针指向显存位置时必然产生非法访存，于是开发者需要手动在内存和显存之间拷贝数据

```c
cudaMemcpy(host buf, device buf, size, cudaMemcpyDeviceToDevice);
MPI Isend(host buf, size, MPI CHAR, 1, 100, MPI COMM WORLD, req);
```

而CUDA-Aware MPI可以正确推断出相应位置并使用相应的API进行处理

```c
MPI Isend(device buf, size, MPI CHAR, 1, 100, MPI COMM WORLD, req);
```

[参考链接](https://developer.nvidia.com/blog/introduction-cuda-aware-mpi/)

### GPU直接通信

通过内存进行GPU之间的通信容易产生瓶颈，于是老黄又搞出了各种GPU直接通信的技术

- GPUDirect P2P：不经过内存，直接GPU之间通信
- GPUDirect RDMA(GDR)：GPU直接通过网卡进行RDMA，不启用的话就还是要经过内存
- GDRCopy: 用了上面GDR的API进行节点内GPU通信的技术，只有官方github的介绍，看起来优势在于低延迟（这个页面写的似乎又跟节点间通信有关，不是特别懂）
- （插播一条）GPUDirect Storage：GPU访问存储设备不经过内存

[参考链接](https://developer.nvidia.com/gpudirect)

这些直连技术能不能开起来取决于GPU之间的拓扑关系

- 有NVLink直连的，那好极了，P2P肯定没问题
- 走pcie并且挂在同一块CPU下，那八成也没问题（严格来说可能还会受到pcie switch影响，这种复杂的问题就不讨论了，反正我写的也是“八成”）
- 走pcie并且挂在不同CPU的，通信需要经过CPU之间的互联，pcie的DMA会被阻断，只能通过内存中转

以上说的只局限于MPI接口的通信，不包括NCCL这种支持卡间中转的

这些拓扑关系可以用nvidia-smi topo -m来看，比如TH2K上的就长这样

```text
        GPU0    GPU1    GPU2    GPU3    mlx5_0  mlx5_1  mlx5_2  mlx5_3  CPU Affinity
GPU0     X      NV2     NV1     NODE    PIX     PIX     NODE    NODE    14-27
GPU1    NV2      X      NODE    NV2     PIX     PIX     NODE    NODE    14-27
GPU2    NV1     NODE     X      NV2     NODE    NODE    PIX     PIX     14-27
GPU3    NODE    NV2     NV2      X      NODE    NODE    PIX     PIX     14-27
mlx5_0  PIX     PIX     NODE    NODE     X      PIX     NODE    NODE
mlx5_1  PIX     PIX     NODE    NODE    PIX      X      NODE    NODE
mlx5_2  NODE    NODE    PIX     PIX     NODE    NODE     X      PIX
mlx5_3  NODE    NODE    PIX     PIX     NODE    NODE    PIX      X

Legend:

  X    = Self
  SYS  = Connection traversing PCIe as well as the SMP interconnect between NUMA nodes (e.g., QPI/UPI)
  NODE = Connection traversing PCIe as well as the interconnect between PCIe Host Bridges within a NUMA node
  PHB  = Connection traversing PCIe as well as a PCIe Host Bridge (typically the CPU)
  PXB  = Connection traversing multiple PCIe switches (without traversing the PCIe Host Bridge)
  PIX  = Connection traversing a single PCIe switch
  NV#  = Connection traversing a bonded set of # NVLinks
```

还能节点抽奖得到一个威力加强版，多了张网卡

```text

        GPU0    GPU1    GPU2    GPU3    mlx5_0  mlx5_1  mlx5_2  mlx5_3  mlx5_4  CPU Affinity
GPU0     X      NV2     NV1     NODE    PIX     PIX     NODE    NODE    PIX     14-27
GPU1    NV2      X      NODE    NV2     PIX     PIX     NODE    NODE    PIX     14-27
GPU2    NV1     NODE     X      NV2     NODE    NODE    PIX     PIX     NODE    14-27
GPU3    NODE    NV2     NV2      X      NODE    NODE    PIX     PIX     NODE    14-27
mlx5_0  PIX     PIX     NODE    NODE     X      PIX     NODE    NODE    PIX
mlx5_1  PIX     PIX     NODE    NODE    PIX      X      NODE    NODE    PIX
mlx5_2  NODE    NODE    PIX     PIX     NODE    NODE     X      PIX     NODE
mlx5_3  NODE    NODE    PIX     PIX     NODE    NODE    PIX      X      NODE
mlx5_4  PIX     PIX     NODE    NODE    PIX     PIX     NODE    NODE     X

Legend:

  X    = Self
  SYS  = Connection traversing PCIe as well as the SMP interconnect between NUMA nodes (e.g., QPI/UPI)
  NODE = Connection traversing PCIe as well as the interconnect between PCIe Host Bridges within a NUMA node
  PHB  = Connection traversing PCIe as well as a PCIe Host Bridge (typically the CPU)
  PXB  = Connection traversing multiple PCIe switches (without traversing the PCIe Host Bridge)
  PIX  = Connection traversing a single PCIe switch
  NV#  = Connection traversing a bonded set of # NVLinks
```

这里槽点有点多，对角的GPU是没有NVLink的，网卡1, 3没开，网卡2只有25Gb以太网，多出来的网卡4还是挂在GPU0/1那边。。。总之，看清楚自己用的设备的状况，以免被坑

### 节点内P2P测试

可以使用cuda-samples里面的p2pBandwidthLatencyTest进行简单测试

```text
P2P Connectivity Matrix
     D\D     0     1     2     3
     0       1     1     1     1
     1       1     1     1     1
     2       1     1     1     1
     3       1     1     1     1
Unidirectional P2P=Disabled Bandwidth Matrix (GB/s)
   D\D     0      1      2      3
     0 737.33   9.80  11.03  11.02
     1   9.83 746.45  11.02  11.02
     2  11.09  11.04 743.78   9.84
     3  11.10  11.07   9.85 746.49
Unidirectional P2P=Enabled Bandwidth (P2P Writes) Matrix (GB/s)
   D\D     0      1      2      3
     0 734.95  48.34  24.22   9.79
     1  48.34 746.14   9.79  48.34
     2  24.22   9.67 746.09  38.30
     3   9.44  48.34  48.34 748.73
Bidirectional P2P=Disabled Bandwidth Matrix (GB/s)
   D\D     0      1      2      3
     0 746.60  10.34  18.83  18.69
     1  10.39 746.49  18.43  18.99
     2  18.75  18.74 749.49  10.37
     3  18.64  18.68  10.35 750.07
Bidirectional P2P=Enabled Bandwidth Matrix (GB/s)
   D\D     0      1      2      3
     0 747.83  96.39  48.36  18.61
     1  96.41 745.60  18.39  96.43
     2  48.37  18.82 750.84  79.61
     3  18.82  96.21  78.99 752.13
P2P=Disabled Latency Matrix (us)
   GPU     0      1      2      3
     0   1.99  18.43  17.49  17.67
     1  18.49   2.00  18.47  18.47
     2  18.45  18.46   1.90  18.46
     3  17.71  17.42  17.50   1.95

   CPU     0      1      2      3
     0   3.54   8.82   8.63   8.87
     1   8.78   3.42   8.81   8.70
     2   8.71   8.50   3.40   8.60
     3   9.38   8.60   8.56   3.39
P2P=Enabled Latency (P2P Writes) Matrix (us)
   GPU     0      1      2      3
     0   1.98   2.24   2.22   2.73
     1   2.27   1.99   2.77   2.27
     2   1.81   2.72   1.90   1.80
     3   2.73   2.22   2.20   1.97

   CPU     0      1      2      3
     0   3.53   2.47   2.47   2.47
     1   2.39   3.48   2.40   2.36
     2   2.31   2.29   3.44   2.28
     3   2.34   2.44   2.47   3.45
```

这里看到GPU之间都能开启P2P，不过走pcie的就很难看了，甚至还有开P2P带宽反降的现象，找到一篇paper里面有类似的情况，可能是pcie switch的问题吧

[参考链接1](https://github.com/NVIDIA/cuda-samples/tree/master/Samples)

[参考链接2](https://arxiv.org/pdf/1903.04611.pdf)

## OpenMPI

虽然文档里面给出了一个CUDA-Aware的检测方法，不过这个东西似乎只跟编译时配置有关，没法运行时检测，并且只判定MPI本身，如果MPI里面用了UCX之类的东西是不会有变动的，简而言之并没有什么用

[参考链接1](https://www.open-mpi.org/faq/?category=runcuda)

[参考链接2](https://github.com/open-mpi/ompi/issues/7963)

[参考链接3](https://bitbucket.org/mpi4py/mpi4py/pull-requests/18)

所以这里就把可能的情况都实测一下吧

### 节点内点对点通信

这里可以用一个开了CUDA支持的osu-benchmarks来测

```bash
mpirun -n 2 osu_bibw D D
```

测试了这几种组合：

- MPI不带CUDA，不使用UCX：不行
- MPI带CUDA，不使用UCX：行
- MPI不带CUDA，UCX不带CUDA：不行
- MPI不带CUDA，UCX带CUDA：行，加入--mca pml ^ucx参数关掉UCX后就不行了
- MPI带CUDA，UCX不带CUDA：不行，加入–mca pml ^ucx参数关掉UCX后就行了
- MPI带CUDA，UCX带CUDA：行

讲道理，真的会有装了MPI+UCX但只有其中一个开了CUDA-Aware这种情况吗。。。

### 节点内集合通信

把osu里面的集合通信测试都来一下

```bash
mpirun -n 3 osu_allgather -d cuda
mpirun -n 3 osu_allgatherv -d cuda
mpirun -n 3 osu_allreduce -d cuda
mpirun -n 3 osu_alltoall -d cuda
mpirun -n 3 osu_alltoallv -d cuda
mpirun -n 3 osu_bcast -d cuda
mpirun -n 3 osu_gather -d cuda
mpirun -n 3 osu_gatherv -d cuda
mpirun -n 3 osu_reduce -d cuda
mpirun -n 3 osu_reduce_scatter -d cuda
mpirun -n 3 osu_scatter -d cuda
mpirun -n 3 osu_scatterv -d cuda
```

结果：

- MPI带CUDA，不使用UCX：行
- MPI不带CUDA，UCX带CUDA：只有osu_bcast能跑，其它全部段错误
- MPI带CUDA，UCX不带CUDA：不行
- MPI带CUDA，UCX带CUDA：行

这里可以得出一个结论：**MPI本身是一定要打开CUDA-Aware支持的，如果使用了UCX那么UCX也要打开CUDA-Aware支持**


## MPI节点内测试

虽然OpenMPI的文档里面给出了一个CUDA-Aware的[检测方法](https://www.open-mpi.org/faq/?category=runcuda)，不过这个东西似乎只跟编译时配置有关，没法运行时检测（[link1](https://github.com/open-mpi/ompi/issues/7963), [link2](https://bitbucket.org/mpi4py/mpi4py/pull-requests/18)），并且只判定MPI本身，如果MPI里面用了UCX之类的东西是不影响的，简而言之并没有什么用

### OpenMPI

#### 点对点通信

最简单的测试CUDA-Aware的方法其实是直接跑一个开了CUDA支持的osu-benchmarks

```bash
mpirun -n 2 osu_bibw D D
```

能跑起来就是OK，否则直接段错误，非常可靠

测试了这几种组合：

- MPI不带CUDA，不使用UCX：不行
- MPI带CUDA，不使用UCX：行
- MPI不带CUDA，UCX不带CUDA：不行
- MPI不带CUDA，UCX带CUDA：行，加入--mca pml ^ucx参数关掉UCX后就不行了，不过真的会有人搞出这种一半带CUDA一半不带的东西来用吗
- MPI带CUDA，UCX不带CUDA：不行，加入–mca pml ^ucx参数关掉UCX后就行，同样的迷惑搭配
- MPI带CUDA，UCX带CUDA：行

#### 集合通信

把osu里面的集合通信测试都来一下

```bash
mpirun -n 3 osu_allgather -d cuda
mpirun -n 3 osu_allgatherv -d cuda
mpirun -n 3 osu_allreduce -d cuda
mpirun -n 3 osu_alltoall -d cuda
mpirun -n 3 osu_alltoallv -d cuda
mpirun -n 3 osu_bcast -d cuda
mpirun -n 3 osu_gather -d cuda
mpirun -n 3 osu_gatherv -d cuda
mpirun -n 3 osu_reduce -d cuda
mpirun -n 3 osu_reduce_scatter -d cuda
mpirun -n 3 osu_scatter -d cuda
mpirun -n 3 osu_scatterv -d cuda
```

结果：

- MPI带CUDA，不使用UCX：行
- MPI不带CUDA，UCX带CUDA：只有osu_bcast能跑，其它全部段错误
- MPI带CUDA，UCX不带CUDA：不行
- MPI带CUDA，UCX带CUDA：行

得出结论：**MPI和UCX要么都不开CUDA，要么都开CUDA，开一半的请自裁**

### MVAPICH2

显而易见的结果

MPI不带CUDA：不行
MPI带CUDA：行
不过默认是不打开CUDA支持的，需要设置环境变量`MV2_USE_CUDA=1`

另外，这里跑出来的点对点通信延迟很高，带宽巨低，有点问题

```text
# OSU MPI-CUDA Latency Test v5.6.3
# Send Buffer on DEVICE (D) and Receive Buffer on DEVICE (D)
# Size          Latency (us)
0                       0.95
1                      21.07
2                      21.74
4                      21.88
8                      21.82
16                     21.77
32                     21.84
64                     21.71
128                    21.83
256                    21.82
512                    21.85
1024                   21.82
2048                   21.87
4096                   21.95
8192                   22.19
16384                  35.98
32768                  37.17
65536                  37.03
131072                 38.56
262144                 40.94
524288                 46.43
1048576                73.79
2097152               114.20
4194304               220.10

----------------------------------------------

# OSU MPI-CUDA Bi-Directional Bandwidth Test v5.6.3
# Send Buffer on DEVICE (D) and Receive Buffer on DEVICE (D)
# Size      Bandwidth (MB/s)
1                       0.09
2                       0.18
4                       0.36
8                       0.72
16                      1.44
32                      2.88
64                      5.76
128                    11.55
256                    22.89
512                    45.84
1024                   91.61
2048                  182.41
4096                  366.05
8192                  728.17
16384                1008.63
32768                1931.85
65536                3780.85
131072               7359.82
262144              13306.94
524288              22668.70
1048576             25992.82
2097152             27229.46
4194304             26555.62
```

搜了一下User Guide，在feature里面发现这么一行

```text
Updated to sm_20 kernel optimizations for MPI Datatypes
```

感觉这个意思是很久之前刚出CUDA-Aware的时候做的支持，后面就不管了，能用，但是很菜

大概MVAPICH2-GDR的性能会正常点？
