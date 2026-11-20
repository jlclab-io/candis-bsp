# Candis 综合测试板马达

使用综合测试板的 M1（Z3OC1T8219731）、TG28-ESP BLDO2（2.7 V）和 GPIO5
经 Q1 驱动的 20 kHz PWM。启动时 PWM 为零，BLDO2 保留初始状态。
确认 M1 实装且 USB 供电后，在板载 OTG CDC 串口执行以下命令。

| 命令 | 行为 |
| --- | --- |
| `STATUS` | 回读 BLDO2 开关和电压设定 |
| `PULSE 75` / `PULSE 100` | 运行最长 600 ms 的脉冲 |
| `STOP` | 将 GPIO5 拉低，结束脉冲并恢复 BLDO2 |

超时同样停止输出并恢复 BLDO2。日志给出 PWM 设定、BLDO2 读回和释放结果。
电压读回是设定值。启动电流、板上压降和温升须在马达支路独立测量。

```sh
idf.py --preview -C examples/motor build
```
