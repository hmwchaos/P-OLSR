# P-OLSR Code Analysis Context

本文档整理当前 `scratch/P-OLSR` 中与 P-OLSR / OLSR-ETX 代码分析和实验修改相关的上下文，方便后续继续调试、复现实验或切换聊天时快速接上。

## 1. 当前目标

当前目标是把 ns-3.45 下的 P-OLSR 协议实验逐步向论文图 15 的条件靠近，同时保持修改范围限制在：

```text
scratch/P-OLSR
```

不修改：

```text
src/olsr
scratch/olsr
ns-3 根目录核心模块
```

当前重点包括：

```text
1. 保留原版 OLSR 作为 native OLSR 对照组。
2. 新增 OLSR-ETX 对照组。
3. P-OLSR 默认不在 MPR tie-break 中使用 ETX cost。
4. 支持 10 次重复实验和平均值输出。
5. 输出 goodput 随时间变化曲线。
6. 加入更接近论文的无线/传播模型和距离相关 DLR 传播损耗。
```

## 2. 关键文件

主要代码和脚本：

```text
polsr_protocol_experiment.cc
plot_results.py
summarize_protocol_results.py
run_fig15_goodput_experiment.sh
polsr-module/model/polsr-routing-protocol.cc
polsr-module/model/polsr-routing-protocol.h
```

核心实验入口：

```text
scratch/P-OLSR/polsr_protocol_experiment
```

图 15 风格实验脚本：

```text
scratch/P-OLSR/run_fig15_goodput_experiment.sh
```

## 3. P-OLSR 现有核心机制

P-OLSR 当前使用 speed-weighted ETX：

```text
ETX = exp(beta * relativeSpeed) / max(MinPredictionQuality, phi * rho)
```

其中：

```text
phi: 本节点估计的“邻居 -> 本节点”方向 HELLO 接收率
rho: 邻居回报的“本节点 -> 邻居”方向接收率
relativeSpeed: 两节点距离变化率的平滑值
beta: 速度惩罚系数
```

收到 HELLO 时：

```text
phi = alpha * 1 + (1 - alpha) * old_phi
```

HELLO 丢失时：

```text
phi = (1 - alpha) * old_phi
```

当前 HELLO 丢失衰减逻辑在：

```text
polsr-module/model/polsr-routing-protocol.cc
RoutingProtocol::ApplyHelloLossDecay()
```

当前阈值：

```text
missedThreshold = 1.5
```

## 4. 不在 MPR 中使用 ETX cost

早期 P-OLSR 实现允许在 MPR 选择打平时用 predictedCost 作为 tie-break。

当前为了更接近论文，图 15 实验默认关闭：

```text
UsePredictionForMpr = false
```

实现位置：

```text
polsr_protocol_experiment.cc
ExperimentOptions::usePredictionForMpr
InstallRouting()
```

脚本中 P-OLSR 运行命令显式包含：

```bash
--usePredictionForMpr=false
```

含义：

```text
路由表仍然可以用 ETX / predictedCost 选路。
MPR 选择不再使用 ETX cost。
```

## 5. OLSR-ETX 是怎么实现的

当前实验支持三种协议名：

```text
OLSR
OLSR-ETX
P-OLSR
```

其中：

```text
OLSR: ns-3 原生 OlsrHelper，按原版 OLSR 逻辑运行。
OLSR-ETX: 使用 scratch/P-OLSR/polsr-module，但强制 beta = 0。
P-OLSR: 使用 scratch/P-OLSR/polsr-module，使用 speed-weighted ETX。
```

关键代码：

```cpp
else if (protocol == "OLSR-ETX" || protocol == "P-OLSR")
{
    PolsrHelper polsr;
    ...
    polsr.Set("PredictionBeta",
              DoubleValue(protocol == "OLSR-ETX" ? 0.0 : options.predictionBeta));
}
```

因此 OLSR-ETX 的代价退化为：

```text
ETX = 1 / max(MinPredictionQuality, phi * rho)
```

注意：

```text
OLSR-ETX 不是 ns-3 原生 OLSR。
它是本地 P-OLSR 模块的 ETX-only 模式。
它不使用 relativeSpeed 的速度惩罚项。
```

## 6. 10 次重复实验和平均值

图 15 脚本默认：

```bash
RUNS=10
```

运行逻辑：

```bash
for run in $(seq 1 "${RUNS}"); do
  run OLSR
  run OLSR-ETX
  run P-OLSR
done
```

每轮都会写入：

```text
run_index
rng_run
```

单次实验里调用：

```cpp
SeedManager::SetRun(options.rngRun);
```

默认 10 次实验会产生：

```text
10 次 OLSR
10 次 OLSR-ETX
10 次 P-OLSR
总共 30 次仿真
```

平均值由：

```text
summarize_protocol_results.py
```

生成：

```text
polsr_protocol_average.csv
polsr_goodput_timeseries_average.csv
```

平均字段包括：

```text
avg_goodput_mbps
std_goodput_mbps
avg_pdr
std_pdr
avg_dlr
std_dlr
avg_outage_time_s
std_outage_time_s
```

## 7. Goodput 时间序列图

单次实验会输出：

```text
polsr_goodput_timeseries.csv
```

每个窗口默认：

```text
1 s
```

goodput 计算：

```text
goodput_mbps = rx_bytes * 8 / windowDuration / 1e6
```

平均后的时间序列：

```text
polsr_goodput_timeseries_average.csv
```

绘图脚本：

```text
plot_results.py
```

输出图：

```text
protocol_goodput_timeseries.svg
```

当前三条线：

```text
红色星号: 原版 OLSR
蓝色方块: OLSR-ETX
绿色圆圈: P-OLSR
对应颜色虚线: 平均 goodput
```

## 8. 无线/传播模型修改

当前新增无线 profile：

```text
legacy
paper
paper-dlr
```

### legacy

保持旧模型：

```text
802.11b
DsssRate11Mbps
FriisPropagationLossModel
```

### paper

更接近论文无线条件：

```text
802.11n
5 GHz
20 MHz
HtMcs1 数据模式
HtMcs0 控制模式
LogDistancePropagationLossModel
NakagamiPropagationLossModel
```

### paper-dlr

在 `paper` 思路上加入论文 DLR 曲线形状的距离相关额外传播损耗：

```text
DLR(d) = 1 / (1 + exp(-(-8.9 + 0.025d)))
```

实现为自定义传播损耗模型：

```text
PaperDlrPropagationLossModel
```

它不是直接修改统计结果，而是改变接收功率：

```text
rxPower = txPower - FriisLoss - extraLossFromDlr
```

额外损耗最大值由参数控制：

```bash
--paperDlrMaxExtraLossDb=12.0
```

图 15 脚本当前默认使用：

```bash
--wirelessProfile=paper-dlr
--paperDlrMaxExtraLossDb=12.0
```

## 9. 当前推荐运行命令

先快速试跑 2 次：

```bash
cd ~/Documents/software/vscode-ns-3.45/ns-3.45

RUNS=2 bash scratch/P-OLSR/run_fig15_goodput_experiment.sh \
  scratch/P-OLSR/protocol-results/fig15_three_protocols_paper_dlr_test_2runs_v1
```

如果编译和运行正常，再跑 10 次：

```bash
cd ~/Documents/software/vscode-ns-3.45/ns-3.45

bash scratch/P-OLSR/run_fig15_goodput_experiment.sh \
  scratch/P-OLSR/protocol-results/fig15_three_protocols_paper_dlr_10runs_v1
```

脚本会拒绝覆盖已有输出目录中的 CSV。每次建议使用新目录。

## 10. 运行后产物

原始结果：

```text
polsr_protocol_summary.csv
polsr_goodput_timeseries.csv
```

平均结果：

```text
polsr_protocol_average.csv
polsr_goodput_timeseries_average.csv
```

图：

```text
scratch/P-OLSR/figures/<输出目录名>/protocol_goodput_timeseries.svg
```

## 11. 已知注意事项

1. 当前 ns-3 实现仍不是论文原始 EMANE + OLSRd/P-OLSRd 环境。
2. OLSR-ETX 是本地 P-OLSR 模块的 ETX-only 模式，不是原生 OLSR 文件夹改出来的。
3. `paper-dlr` 是 DLR-shaped propagation loss，不是对每个包直接按论文 DLR 概率删除。
4. 新无线模型会显著改变 OLSR、OLSR-ETX、P-OLSR 的结果，必须使用新输出目录。
5. 如果第一次编译遇到 `ChannelSettings`、`HtMcs1` 或 `PaperDlrPropagationLossModel` 相关错误，需要根据 ns-3.45 实际 API 调整。
6. 当前 Windows 侧只能做代码和脚本检查；真正编译运行要在 Linux VM 的 ns-3 根目录执行。

## 12. 已做过的基础检查

已做：

```text
python -m py_compile plot_results.py summarize_protocol_results.py
git diff --check
```

未做：

```text
Linux VM 中的 ./ns3 run 编译和仿真验证
```

建议下一步先跑：

```bash
RUNS=2 bash scratch/P-OLSR/run_fig15_goodput_experiment.sh \
  scratch/P-OLSR/protocol-results/fig15_three_protocols_paper_dlr_test_2runs_v1
```

如果失败，把完整编译错误从第一个 `error:` 或 `CMake Error` 开始贴出来。

## 13. HELLO / TC 中接收率字段的上下文

当前 `polsr-header.h` 中有两类报文字段带有接收率默认值：

```cpp
double forwardReceivingRatio{1.0};
double reverseReceivingRatio{1.0};
```

它们分别出现在：

```text
MessageHeader::Hello::LinkMessage::NeighborLinkQuality
MessageHeader::Tc::NeighborPrediction
```

含义是：当 HELLO 或 TC 扩展字段对象被创建但还没有被显式赋值时，默认按理想链路质量 `1.0` 处理。这是报文结构体的默认值，不等于真实链路状态一开始就是 1.0。

真实链路预测状态主要保存在 `polsr-repositories.h` 的 `LinkTuple` 中：

```cpp
double phi{0.0};
double rho{0.0};
bool predictionValid{false};
```

所以新链路刚创建时，预测字段还没有有效测量值。收到 HELLO 后，`RoutingProtocol::UpdatePredictionFromHello()` 会在标准 OLSR 的 `LinkSensing()` 完成后更新预测字段：

```text
phi = (1 - alpha) * old_phi + alpha
rho = 邻居 HELLO 中回告的 forwardReceivingRatio
predictedCost = exp(beta * relativeSpeed) / max(MinPredictionQuality, phi * rho)
```

随后通过 `m_state.UpdateLinkPrediction(...)` 写回 `LinkTuple`。

发送 HELLO 时，如果启用 prediction，并且 HELLO 携带 GPS 信息，代码会把当前 `LinkTuple` 的值写入报文：

```cpp
quality.forwardReceivingRatio = link_tuple->phi;
quality.reverseReceivingRatio = link_tuple->rho;
quality.qualityValid = link_tuple->predictionValid;
```

发送 TC 时，如果启用 prediction，会通过：

```text
GetNeighborForwardReceivingRatio()
GetNeighborReverseReceivingRatio()
```

从有效链路中取当前 `phi/rho`，填入 `Tc::NeighborPrediction`。

需要注意：当 `predictionValid == false` 或 TC 没有携带预测字段时，路由和拓扑逻辑中经常会退化使用 `1.0` 作为默认 cost 或默认接收率。这表示“没有可用预测信息时按普通 OLSR/理想链路质量回退”，不是实际测得的接收率。
