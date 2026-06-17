# P-OLSR 下一会话上下文交接

生成时间：2026-06-13  
项目路径：`~/Documents/software/vscode-ns-3.45/ns-3.45/scratch/P-OLSR`  
Windows 共享路径：`\\192.168.222.129\ns3\scratch\P-OLSR`  
GitHub 仓库：`https://github.com/hmwchaos/P-OLSR.git`

## 1. 对话背景与当前目标

当前项目是在 ns-3.45 下继续修改和调试 P-OLSR 协议，目标是尽量复现论文中 P-OLSR 相对 OLSR/OLSR-ETX 的性能提升，并围绕论文图 12 到图 15 做参数实验。

当前主要方向：

- 保留原版 OLSR 作为对照。
- 在 `scratch/P-OLSR` 内实现 P-OLSR 和 OLSR-ETX，不修改 ns-3 原版 `src/olsr`。
- 使用 `paper-dlr` 无线/传播模型近似论文中的距离相关丢包环境。
- 比较 OLSR、OLSR-ETX、P-OLSR 的 goodput、DLR、PDR、delay、outage time。
- 目前重点从 V1.6.0 的 HELLO 衰减周期实验，推进到 V1.7 的论文 Fig.12-14 outage 参数扫描实验。

## 2. 用户沟通与操作偏好

后续会话必须遵守：

- 使用中文回答。
- 先给结论，再解释原因。
- 修改代码前先说明：
  - 理解的目标；
  - 准备检查哪些文件/函数；
  - 保持哪些内容不变；
  - 是否存在歧义。
- 不大改、不重构、不擅自删除文件。
- 不擅自执行 `git reset --hard`、`git clean`、清理 build 或删除实验结果。
- 涉及 ns-3 修改后必须给构建/运行验证命令。
- 不要使用 `git add .`，避免把实验结果一起提交。
- 结果目录前缀要带版本号，例如 `V1.6.0_...`、`V1.7_...`。

共享目录访问规则：

- 普通沙箱访问 `\\192.168.222.129\ns3\` 经常会报 `Access is denied`。
- 不能直接把 UNC 路径当 PowerShell 工作目录，否则可能报：
  - `CreateProcessWithLogonW failed: 267`
- 正确方式：
  - 从本地目录启动命令，例如 `C:\Users\Lenovo`；
  - 把 UNC 路径作为参数访问；
  - 必要时使用非沙箱权限。
- 已验证非沙箱权限下可以读写 `\\192.168.222.129\ns3\scratch\P-OLSR`。

## 3. 当前关键文件

核心协议文件：

```text
scratch/P-OLSR/polsr-module/model/polsr-routing-protocol.cc
scratch/P-OLSR/polsr-module/model/polsr-routing-protocol.h
scratch/P-OLSR/polsr-module/model/polsr-header.cc
scratch/P-OLSR/polsr-module/model/polsr-header.h
```

实验与统计文件：

```text
scratch/P-OLSR/polsr_protocol_experiment.cc
scratch/P-OLSR/summarize_protocol_results.py
scratch/P-OLSR/plot_results.py
scratch/P-OLSR/run_v160_expected_arrival_deadline_three_protocols.sh
scratch/P-OLSR/run_v17_outage_sweeps.sh
```

上下文/说明文件：

```text
scratch/P-OLSR/CODE_ANALYSIS_CONTEXT.md
scratch/P-OLSR/POLSR_PROTOCOL_IMPLEMENTATION.md
scratch/P-OLSR/VERSION_NOTES.md
scratch/P-OLSR/V1.6.0_EXPERIMENT_NOTES.md
scratch/P-OLSR/NEXT_SESSION_CONTEXT.md
```

注意：`CODE_ANALYSIS_CONTEXT.md` 当前用 UTF-8 读取时有乱码，可能需要用 GBK 转码查看。

## 4. 已完成的 P-OLSR 协议修改

### 4.1 HELLO / TC 字段扩展

已在 P-OLSR 中补充论文相关字段：

- 经度、纬度、高度；
- 正向接收率 `phi`；
- 反向接收率 `rho`；
- 相对速度 `relativeSpeed`；
- `predictedCost / ETX cost`。

### 4.2 speed-weighted ETX

当前 P-OLSR 使用 speed-weighted ETX：

```text
ETX = exp(beta * relativeSpeed) / max(MinPredictionQuality, phi * rho)
```

参数含义：

- `PredictionAlpha`：HELLO 接收率平滑系数；
- `PredictionBeta`：速度加权 ETX 中的速度惩罚系数；
- `PredictionGamma`：相对速度平滑系数；
- `MinPredictionQuality`：避免 `phi*rho` 为 0；
- `MaxPredictionCost`：限制最大链路代价。

### 4.3 HELLO 丢失时的 phi 衰减

旧方案：

```text
收到 HELLO:
phi = alpha * 1 + (1 - alpha) * old_phi

没收到 HELLO:
phi = (1 - alpha) * old_phi
```

早期阈值是超过约 `1.5 * HelloInterval` 后衰减。

V1.5 后改为 Expected Arrival Time 方案：

- 每条 `LinkTuple` 保存：
  - `nextExpectedHelloTime`
  - `helloDeadlineTime`
  - `helloDeadlineValid`
- 收到 HELLO 后刷新该邻居链路 deadline；
- 超时后只衰减该邻居的 `phi`；
- `rho` 仍由邻居 HELLO 回告更新，不单独本地衰减。

### 4.4 MPR 与路由

MPR：

- 当前实验中 `UsePredictionForMpr=false`。
- 这样不会让 ETX cost 过度改变 RFC 3626 的 MPR 选择。
- 若启用，当前实现只把 predicted cost 作为 tie-break，不完全推翻原 MPR 规则。

路由：

- `RoutingTableComputation()` 中如果启用预测路由，会进入 `PredictiveRoutingTableComputation()`。
- 一跳边使用 `LinkTuple.predictedCost`。
- TC 拓扑边使用 `TopologyTuple.predictedCost`。
- 用类 Dijkstra 的方式选总代价最小路径。
- 写入路由表时 `distance` 仍是 hop 数，ETX 只用于选路。

### 4.5 OLSR-ETX

当前 OLSR-ETX 是通过 P-OLSR 模块实现的对照协议：

- 仍使用 ETX 链路代价；
- `PredictionBeta = 0.0`，不使用速度项；
- 不应混入 GPS/相对速度预测；
- 用来近似论文中的 `OLSR with link-quality extension` / OLSR-ETX 类基线。

## 5. 版本与实验记录

### V1.4

主题：新增可切换无线/传播模型。

主要内容：

- 增加 `wirelessProfile`。
- 增加 `paper-dlr` 距离相关丢包近似模型。
- 用于缩小 ns-3 与论文 EMANE/真实无线环境差异。

### V1.5

主题：Expected Arrival Time 替换 HELLO 周期衰减。

主要内容：

- 按链路记录 HELLO 期望到达时间与 deadline。
- 超过 deadline 后衰减 `phi`。

### V1.6

主题：新增可配置 `HelloDeadlineFactor`。

主要内容：

- 新增 `--helloDeadlineFactor` 命令行参数。
- 脚本支持 `HELLO_DEADLINE_FACTOR`。
- 用于测试 `1.0HI`、`1.2HI`、`1.4HI`、`1.5HI` 等不同衰减周期。

### V1.6.0

主题：不同 HELLO deadline factor 对 P-OLSR 的影响。

已分析结果目录来自：

```text
F:\Lenovo\Documents\无人机集群自组网\result
```

包含：

```text
V1.6.0_deadline_1.0HI_three_protocols_10runs
V1.6.0_deadline_1.1HI_three_protocols_10runs
V1.6.0_deadline_1.2HI_three_protocols_10runs
V1.6.0_deadline_1.3HI_three_protocols_10runs
V1.6.0_deadline_1.4HI_three_protocols_10runs
V1.6.0_deadline_1.5HI_three_protocols_10runs
V1.6.0_deadline_1.6HI_three_protocols_10runs
```

主要比较指标：

- `avg_dlr`
- `avg_goodput_mbps`
- `avg_mean_delay_ms`
- `avg_outage_time_s`
- `avg_pdr`

V1.6.0 关键结论：

- 综合最好：`1.4HI`
- DLR / outage 最好：`1.3HI`
- goodput 最高：`1.4HI`
- `1.6HI` 不推荐，提升很小且 delay 恶化。

核心结果：

```text
论文 Fig.15 goodput 提升:
OLSR/OLSR-ETX 类基线 0.83 Mbps -> P-OLSR 0.95 Mbps
提升约 +14.46%
```

V1.6.0 本地结果：

```text
1.0HI: goodput +14.59%, DLR 降低 30.84%, outage 降低 24.74%, delay +10.87%
1.1HI: goodput +14.59%, DLR 降低 30.84%, outage 降低 24.74%, delay +10.87%
1.2HI: goodput +14.59%, DLR 降低 30.84%, outage 降低 24.74%, delay +10.87%
1.3HI: goodput +14.70%, DLR 降低 31.88%, outage 降低 25.62%, delay +13.73%
1.4HI: goodput +14.87%, DLR 降低 30.09%, outage 降低 22.89%, delay +5.93%
1.5HI: goodput +8.38%,  DLR 降低 13.00%, outage 降低 8.33%,  delay -5.93%
1.6HI: goodput +0.54%,  DLR 降低 3.80%,  outage 降低 1.05%,  delay +15.25%
```

推荐结论：

```text
如果以 goodput 为主：选 1.4HI
如果以 DLR/outage 为主：选 1.3HI
综合论文对比、吞吐量、稳定性、时延：推荐 1.4HI
```

### V1.7

主题：论文 Fig.12-14 outage 参数扫描。

当前新增脚本：

```text
scratch/P-OLSR/run_v17_outage_sweeps.sh
```

目标：

- 同一个 HI 下生成一张总图；
- 图中包含三个 panel：
  - alpha sweep
  - beta sweep
  - gamma sweep
- 主要指标是 `outage_time_s`。

当前默认参数已改为：

```text
alpha = 0.2
beta = 0.2
gamma = 0.08
HI = 1.0
HelloDeadlineFactor = 1.4
```

已修改默认参数位置：

```text
polsr_protocol_experiment.cc
polsr-module/model/polsr-routing-protocol.cc
```

V1.7 脚本支持参数：

```text
--alpha
--beta
--gamma
--gama
--hi
--hello-deadline-factor
--runs
--alpha-values
--beta-values
--gamma-values
--output-dir
--version-prefix
```

`--gama` 和 `--gamma` 等价，推荐正式使用 `--gamma`。

注意：

- `--alpha` 是 beta/gamma sweep 中固定 alpha 的值；
- `--alpha-values` 是 alpha sweep 要扫描的值；
- beta/gamma 同理。

## 6. V1.7 脚本最近修复记录

用户运行时出现报错：

```text
Invalid command-line argument: --notes=V1.7_alpha_0.05_OLSR-ETX
```

原因：

- `polsr_protocol_experiment` 不支持 `--notes` 参数；
- 脚本错误地把 `--notes` 传给了 ns-3 程序。

已修复：

- 去掉传给实验程序的 `--notes`；
- 改为脚本自己生成：

```text
polsr_outage_sweep_cases.csv
```

该文件记录：

```text
run_index,rng_run,sweep,value,protocol,alpha,beta,gamma
```

用于后处理时把每次 run 归类到 alpha/beta/gamma sweep。

用户运行命令还出现了另一个输入问题：

```bash
--alpha-values "0.05 0.1 0.2 0.3 0.4" \
> 0.05
```

原因：

- 行尾 `\` 表示下一行继续命令；
- 用户下一行只输入了 `0.05`；
- 脚本把 `0.05` 当作输出目录；
- 日志中出现：

```text
--outputDir=0.05
```

正确做法：

- 一行写完；
- 或者每个 `\` 后面必须继续写完整参数，不要单独输入一个数。

## 7. V1.7 推荐运行命令

先做语法检查：

```bash
cd ~/Documents/software/vscode-ns-3.45/ns-3.45
bash -n scratch/P-OLSR/run_v17_outage_sweeps.sh
```

构建：

```bash
./ns3 build scratch/P-OLSR/polsr_protocol_experiment
```

最小测试：

```bash
bash scratch/P-OLSR/run_v17_outage_sweeps.sh --runs 1 --version-prefix V1.7_test --hi 1.0 --alpha 0.2 --beta 0.2 --gamma 0.08 --hello-deadline-factor 1.4 --alpha-values "0.2" --beta-values "0.2" --gamma-values "0.08"
```

这会运行：

```text
3 个 sweep * 1 个值 * 2 个协议 * 1 次 = 6 次 ns-3 run
```

正式运行：

```bash
bash scratch/P-OLSR/run_v17_outage_sweeps.sh --runs 10 --version-prefix V1.7 --hi 1.0 --alpha 0.2 --beta 0.2 --gamma 0.08 --hello-deadline-factor 1.4 --alpha-values "0.05 0.1 0.2 0.3 0.4" --beta-values "0.05 0.1 0.2 0.3 0.4" --gamma-values "0.02 0.04 0.08 0.12 0.16"
```

这会运行：

```text
(5 + 5 + 5) 个参数点 * 2 个协议 * 10 次 = 300 次 ns-3 run
```

输出目录：

```text
scratch/P-OLSR/protocol-results/V1.7_outage_sweeps_HI_1.0_deadline_1.4HI_10runs/
scratch/P-OLSR/figures/V1.7_outage_sweeps_HI_1.0_deadline_1.4HI_10runs/outage_sweeps.svg
```

## 8. 论文实验对应关系

论文：Rosati et al., Dynamic Routing for Flying Ad Hoc Networks.

Fig.12：

```text
指标：Average outage time
扫描：gamma
固定：alpha=0.2, beta=0.2
HI = 0.5 / 1.0 / 2.0
```

Fig.13：

```text
指标：Average outage time
扫描：beta
固定：alpha=0.2, gamma=0.08
HI = 0.5 / 1.0 / 2.0
```

Fig.14：

```text
指标：Average outage time
扫描：alpha 和 HI
固定：beta=0.2, gamma=0.08
```

当前 V1.7 脚本实现的是：

```text
固定同一个 HI
同一张图中比较 alpha/beta/gamma 三个 sweep
```

这与论文图 12-14 不是完全同一张图格式，但用于当前阶段比较相同 HI 下三个参数的敏感性。

若后续要更接近论文，需要扩展：

```text
HI_VALUES="0.5 1.0 2.0"
每个 HI 分别生成曲线
```

## 9. 与论文仍未百分百一致的地方

当前 ns-3 实现与论文还有差异：

- 论文使用真实飞行/EMANE/OLSRd/P-OLSRd 环境；当前是 ns-3 近似复现。
- 当前使用 ns-3 `MobilityModel` 坐标代替真实 GPS。
- 没有完整 GPS 误差模型。
- `paper-dlr` 只是近似距离相关丢包，不等价于论文完整 802.11 TGn/EMANE 环境。
- TC 中当前携带 `predictedCost/phi/rho/relativeSpeed` 等字段，论文不一定这样设计。
- HELLO 字段顺序、magic/flags 等是工程兼容实现，不是论文原始格式。
- Expected Arrival Time / `HelloDeadlineFactor` 是本项目工程化增强，不是论文原始 OLSRd 实现的逐字复刻。
- OLSR-ETX 是在 P-OLSR 模块中近似实现，不是直接移植 OLSRd 的 link-quality 插件。

因此报告表述应使用：

```text
ns-3 近似复现
OLSR-ETX 类基线
相对提升趋势接近论文
```

避免写成：

```text
完全复现论文原始实验
```

## 10. Git 与仓库状态注意事项

正确 P-OLSR 仓库路径：

```bash
cd ~/Documents/software/vscode-ns-3.45/ns-3.45/scratch/P-OLSR
git rev-parse --show-toplevel
```

应该输出：

```text
/home/hmw/Documents/software/vscode-ns-3.45/ns-3.45/scratch/P-OLSR
```

如果输出 ns-3 根目录：

```text
/home/hmw/Documents/software/vscode-ns-3.45/ns-3.45
```

说明当前不是独立 P-OLSR 仓库。

当前分支曾使用：

```text
codex/expected-arrival-hello-decay
```

V1.6 提交：

```text
4c53829 V1.6 Add configurable HELLO deadline factor
```

不要随便：

```bash
git add .
git clean
git reset --hard
```

实验结果、figures、protocol-results 经常是未跟踪文件。

## 11. VS Code / CMake 注意事项

不要把 `scratch` 当成 CMake 根目录配置。

错误示例：

```text
-S /home/.../ns-3.45/scratch
Unknown CMake command "build_exec"
```

正确方式：

```bash
cd ~/Documents/software/vscode-ns-3.45/ns-3.45
./ns3 configure
./ns3 build scratch/P-OLSR/polsr_protocol_experiment
```

如果 `scratch` 下有 P-OLSR 备份目录，ns-3 可能重复扫描导致 CMake 冲突。备份目录应移到 `scratch` 外。

## 12. 其他本会话涉及但非 P-OLSR 核心的话题

### 12.1 Windows 卸载 ima.copilot

Windows 上 `D:\ima.copilot` 的卸载器位置：

```text
D:\ima.copilot\uninstall\ImaUninstall.exe
```

卸载日志显示：

```text
uninstall_completed
result_code = 0
```

说明主卸载成功。

残留目录删不掉主要因为：

```text
ima_context_menu.dll
```

可能被资源管理器加载。

处理建议：

```powershell
Stop-Process -Name explorer -Force
Remove-Item -LiteralPath "D:\ima.copilot" -Recurse -Force
Start-Process explorer.exe
```

`m365copilot_autostarter` 位于：

```text
C:\Program Files\WindowsApps\Microsoft.MicrosoftOfficeHub_...
```

它是 Microsoft 365 Copilot 相关进程，不是 IMA，不应误删。

### 12.2 Word 文档：BPSK 调制解调

文档结论：

- 实现了简化版载波同步/相位补偿；
- 没有实现真正符号同步；
- 默认已知每比特采样点和码元边界。

建议报告表述：

```text
系统实现了基于参数开关的载波频偏校正和相位补偿；
但当前默认接收端已知码元周期和码元边界，尚未实现独立的符号定时同步或码元边界恢复算法。
```

### 12.3 Word 文档：无人机组网报告

看过链路质量路由和加权分簇过程。

核心结论：

- 路由算法本质是带链路质量权重的 Dijkstra。
- 链路质量路由综合距离和链路到期时间 LET。
- 加权分簇综合：
  - 平均节点度差；
  - 速度相似度；
  - 平均邻居距离。
- 文档中 `M` 实际是速度相似度，不应直接叫链路质量。

## 13. 尚未完成 / 下次优先事项

下次会话建议按顺序继续：

1. 检查 `run_v17_outage_sweeps.sh` 当前内容，确认 `--notes` 已完全移除。
2. 在虚拟机中运行：

```bash
cd ~/Documents/software/vscode-ns-3.45/ns-3.45
bash -n scratch/P-OLSR/run_v17_outage_sweeps.sh
./ns3 build scratch/P-OLSR/polsr_protocol_experiment
```

3. 跑 V1.7 最小测试：

```bash
bash scratch/P-OLSR/run_v17_outage_sweeps.sh --runs 1 --version-prefix V1.7_test --hi 1.0 --alpha 0.2 --beta 0.2 --gamma 0.08 --hello-deadline-factor 1.4 --alpha-values "0.2" --beta-values "0.2" --gamma-values "0.08"
```

4. 检查是否生成：

```text
polsr_protocol_summary.csv
polsr_protocol_average.csv
polsr_outage_sweep_cases.csv
polsr_outage_sweep_average.csv
outage_sweeps.svg
```

5. 如果最小测试正常，再跑正式 V1.7：

```bash
bash scratch/P-OLSR/run_v17_outage_sweeps.sh --runs 10 --version-prefix V1.7 --hi 1.0 --alpha 0.2 --beta 0.2 --gamma 0.08 --hello-deadline-factor 1.4 --alpha-values "0.05 0.1 0.2 0.3 0.4" --beta-values "0.05 0.1 0.2 0.3 0.4" --gamma-values "0.02 0.04 0.08 0.12 0.16"
```

6. 对 V1.7 结果重点分析：

```text
avg_outage_time_s
avg_dlr
avg_goodput_mbps
avg_mean_delay_ms
avg_pdr
```

7. 后续如果要更接近论文 Fig.12-14，应扩展为多 HI：

```text
HI = 0.5, 1.0, 2.0
```

并分别画 gamma/beta/alpha 对 outage 的影响曲线。

## 14. 参数设置总表

当前推荐默认：

```text
PredictionAlpha = 0.2
PredictionBeta = 0.2
PredictionGamma = 0.08
HelloInterval = 1.0
HelloDeadlineFactor = 1.4
UsePredictionForMpr = false
wirelessProfile = paper-dlr
paperDlrMaxExtraLossDb = 12.0
outageWindow = 1.0
outageDlrThreshold = 0.2
maxDeliveryDelay = 5.0
```

历史常用参数：

```text
早期 P-OLSR:
PredictionAlpha = 0.05
PredictionBeta = 0.2
PredictionGamma = 0.04

论文 Fig.15 对标:
HI = 1.0
alpha = 0.1
beta = 0.2
gamma = 0.08

论文 Fig.12/13 outage 对标:
alpha = 0.2
beta = 0.2
gamma = 0.08
HI = 0.5 / 1.0 / 2.0
```

用户曾提到 `eta=0.2`。当前代码中没有明确 `eta` 参数，可能是指 `alpha=0.2` 或误写。下次如果再次出现 `eta`，需要确认它具体指哪个参数。

