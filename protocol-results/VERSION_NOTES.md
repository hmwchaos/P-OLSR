# P-OLSR Version Notes

本文档用于记录 P-OLSR 各版本的主要修改内容、实验含义和注意事项。

## V1.6

版本主题：新增可配置的 HELLO deadline factor。

主要内容：

- 新增 `HelloDeadlineFactor` 参数，用于控制 Expected Arrival Time 机制中的 HELLO 超时截止时间。
- 支持通过命令行参数 `--helloDeadlineFactor` 修改 HELLO 衰减周期。
- 支持在脚本中通过环境变量 `HELLO_DEADLINE_FACTOR` 设置不同衰减周期。
- 该版本用于比较不同 HELLO deadline factor 对 P-OLSR 性能的影响。

注意事项：

- `HelloDeadlineFactor` 只影响 HELLO 丢失后的 `phi` 衰减触发时机。
- `rho` 仍然由邻居 HELLO 回告更新，不单独做本地衰减。
- 运行实验时应在输出目录名中保留版本号和 deadline 参数，例如 `V1.6_deadline_1.0HI_three_protocols_10runs`。

## V1.5

版本主题：使用 Expected Arrival Time 替换原有 HELLO 周期衰减。

主要内容：

- 按每条 `LinkTuple` 保存下一次 HELLO 的期望到达时间和截止时间。
- 收到 HELLO 后刷新该邻居链路的 deadline。
- 超过 deadline 后只衰减该邻居链路的 `phi`。
- 保持 `rho` 的更新逻辑不变，仍由邻居回告信息驱动。

注意事项：

- 该版本改变了 HELLO 丢失检测方式，会影响 P-OLSR 的链路质量估计和路由结果。
- 该版本仍保持 `UsePredictionForMpr=false` 的实验配置，以避免 ETX cost 过度影响 MPR 选择。

## V1.4

版本主题：新增可切换无线/传播模型。

主要内容：

- 新增可切换的无线/传播模型配置。
- 支持更接近论文实验的距离相关丢包或 DLR 模型。
- 为后续 P-OLSR、OLSR-ETX、OLSR 对比实验提供统一实验环境。

注意事项：

- 该版本主要改变实验信道条件，会直接影响吞吐量、PDR、DLR 和 outage time。
- 与论文实验相比，仍属于 ns-3 近似复现，不等同于 EMANE/OLSRd 原始实验环境。

## V1.3

版本主题：加入 OLSR-ETX 对比和多次重复实验。

主要内容：

- 在不修改 ns-3 原版 `src/olsr` 的前提下，通过 P-OLSR 模块实现 OLSR-ETX 对比。
- OLSR-ETX 使用 ETX 链路代价，但不使用 GPS 相对速度加权。
- 增加 10 次重复实验与平均值统计流程。

注意事项：

- OLSR-ETX 应作为普通 ETX 路由对比对象，不应混入 P-OLSR 的速度预测项。
- 重复实验结果需要使用版本化输出目录保存，避免覆盖旧结果。

## V1.2

版本主题：调整 HELLO 丢失时的 `phi` 衰减周期。

主要内容：

- 将 `phi` 的 HELLO 丢失衰减触发周期调整为约 1.5 个 HELLO interval。
- 收到 HELLO 时平滑更新 `phi`。
- 未收到 HELLO 且超过阈值后，对该链路 `phi` 进行衰减。

注意事项：

- 该版本属于旧的周期衰减方案，后续 V1.5 已改为 Expected Arrival Time 方案。

