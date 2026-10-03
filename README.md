# Zephyr Industrial Edge Node

基于 Zephyr RTOS 的工业传感器监测与自动化测试项目。
通过 QEMU 模拟运行，验证传感器数据队列、安全状态机和线程协作。

## 已实现功能

- 模拟温度、振动和供电电压数据，每 200 ms 生成一个样本。
- 使用容量为 8 的消息队列传递数据，队列满时拒绝新样本并记录告警。
- 安全状态机包含 NORMAL、WARNING、FAULT、RECOVERY 四种状态。
- 恢复到 NORMAL 需要连续 5 个正常样本。
- 支持配置安全线程处理延迟，用于注入队列过载。

## 自动化测试

| 检查 | 最近验证结果 |
| --- | --- |
| 安全状态机 | 10/10 通过 |
| 传感器队列 | 5/5 通过 |
| 数据处理链路 | 3/3 通过 |
| 30 秒正常运行冒烟 | PASS |
| 30 秒持续队列过载 | PASS |

测试使用 Python 自动构建应用、启动 QEMU、解析日志并生成 JSON 和 Markdown 报告。
PowerShell 统一入口串联全部检查，失败时立即停止。

## 运行方法

准备 Windows、Zephyr 工具链、安装 west 的 Python 环境及 QEMU。
根据本机环境修改 scripts/run_all_checks.ps1 中的 Python、Zephyr 和 QEMU 路径。

在项目根目录执行：

```powershell
.\scripts\run_all_checks.ps1
```

全部通过时输出：

```text
ALL CHECKS PASSED
```

## 项目目录

- app/include/：数据结构和头文件
- app/src/：传感器模拟与安全监测代码
- tests/：安全状态机、队列和处理链路测试
- scripts/：自动化测试脚本
- docs/test-results/：已归档的通过报告
- build/：构建产物和每次运行生成的报告

详细覆盖范围、环境路径和报告链接见 [测试说明](docs/TESTING.md)。

## 验证范围

当前测试基于 QEMU，尚未验证真实硬件时序、长期稳定性及过载解除后的恢复。
项目名称中的 secure 不代表已经实现或验证安全启动、加密通信等安全功能。
