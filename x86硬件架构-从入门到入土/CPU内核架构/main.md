# CPU内核架构

虽然题目叫做“x86硬件架构”，但是套到别的ISA上去也八九不离十，现代CPU内核架构的原理基本大同小异，不过以后会不会又出现什么合久必分的奇怪东西呢？

## 开头先来推点材料

这里我首先要推一篇文章（[链接](http://www.lighterra.com/papers/modernmicroprocessors/)），极好的入门材料，很详细地介绍了CPU的发展历程，填补了计组课本和现代CPU之间的巨大差距，并且我也默认你已经看过了，这样我就能少打点字了（逃

还有就是Agner's Optimization Manuals（[链接](https://www.agner.org/optimize/)），其中的microarchitecture篇对内核架构也有详细的描述，而且是按照时间顺序来编的，可以很好地对比历代内核架构的发展（指牙膏厂花式挤牙膏），不过看这个稍微有点门槛，可能得多看几轮才比较好理解

如果你确实有兴趣深入了解，甚至想自己动手试试的话，Intel® 64 and IA-32 Architectures Optimization Reference Manual（[链接](https://software.intel.com/content/www/us/en/develop/download/intel-64-and-ia-32-architectures-optimization-reference-manual.html)），鸿篇巨制，堪称圣经

## 再来看看架构框图

这方面我觉得WikiChip画的图就很不错，下面是Intel的Sunny Cove内核以及AMD的Zen2内核的图

![p1](https://en.wikichip.org/w/images/thumb/2/2d/sunny_cove_block_diagram.svg/1900px-sunny_cove_block_diagram.svg.png)

![p2](https://en.wikichip.org/w/images/thumb/f/f2/zen_2_core_diagram.svg/1800px-zen_2_core_diagram.svg.png)

有没有一种“我看懂了箭头，别的都没懂”的感觉？（逃

WikiChip本身也会对架构有个解析，拿来参考也挺不错的，不过WikiChip的缺点就是更新老慢了

## 分支预测单元

学过计组的都知道，分支对于流水线CPU来说是个大问题，因为分支指令的取向和目标地址需要等到执行阶段才能确认，为了不浪费前面的流水线阶段，可以用分支预测赌它一把

假设跳转事件的产生至少需要该指令走完取指-解码流程，比如在解码阶段识别出这是无条件跳转并且立刻算出跳转目标，那么解码阶段前的流水线都要被清空，影响流水线利用率，像x86这种在这里还有几级流水线的影响就更大了

那么有没有解决办法呢？当然是有的，还是**预测**，于是分支预测单元实际上的任务有：

1. 预测下一条指令是否可能发生跳转（包括无条件跳转和条件分支）
2. 预测跳转目标地址
3. 最后才是一般意义上的，对条件分支的取向进行预测

