# 移植指南 / Porting Guide

把这个框架搬到一块新板子上，要写的代码只有三个类。
下面按「先做什么」的顺序写，末尾附 STM32 和 ESP32 的完整例子索引。

## 0. 先确认你需要哪几个接口

| 接口 | 什么时候需要 | 声明 |
|---|---|---|
| `hal::IClock` | **总是需要** | [include/sensorfw/hal/clock.hpp](../include/sensorfw/hal/clock.hpp) |
| `hal::IAdcChannel` | 用模拟传感器（NTC、热电偶、气体传感器）时 | [hal/adc.hpp](../include/sensorfw/hal/adc.hpp) |
| `hal::II2cBus` | 用 I2C 数字传感器时 | [hal/i2c.hpp](../include/sensorfw/hal/i2c.hpp) |

SPI / OneWire / UART 传感器同理：照着 `II2cBus` 的样子加一个接口，
框架其它部分不需要知道它存在。

## 1. 实现时钟（5 行）

```cpp
class MyClock : public sensorfw::hal::IClock {
public:
    sensorfw::TimestampMs nowMs() const { return my_get_tick_ms(); }
};
```

要求：**单调递增**的毫秒计数。允许 32 位回绕——框架所有时间差都走
`elapsedMs()` 的无符号减法，回绕是安全的（`test_publisher` 里有专门用例）。
不要用 RTC 墙上时间（会被 NTP 校正跳变）。

## 2. 实现 ADC 通道 / I2C 总线

照抄 [port/stm32/stm32_platform.hpp](../port/stm32/stm32_platform.hpp)，
把寄存器/HAL 函数名换成你芯片的。两个约定：

- **`readRaw()` 返回原始计数值，不做任何物理量换算。** 换算逻辑属于驱动
  （`NtcThermistorSensor::convert()`），这样它能在 PC 上被单元测试，
  而移植层保持「读一个寄存器」这种不会写错的程度。
- **错误要映射成 `Status`**，不要吞掉。`Timeout` 和 `BusError` 在
  `SensorService` 里走的是同一条故障计数路径，但日志里能区分是谁的锅。

如果你的 ADC 走 DMA + 硬件过采样（推荐，NTC 分压阻抗高、噪声大），
在 `readRaw()` 里返回 DMA 缓冲区里最新的那个值即可，接口不用变。

## 3. 组装

组装根就是你的 `main()`。参考
[examples/host_demo/main.cpp](../examples/host_demo/main.cpp) 场景 2：

```cpp
MyClock       clock;
MyAdcChannel  adc(&hadc1, ADC_CHANNEL_4);

drivers::NtcConfig ntc;          // 从原理图填：R0 / B 值 / 分压电阻 / 拓扑
ntc.seriesResistance = 10000.0f;
ntc.betaCoefficient  = 3950.0f;
drivers::NtcThermistorSensor sensor(adc, clock, ntc, "ntc@adc1");

MedianFilter<5> median;
EwmaFilter      smooth = EwmaFilter::withTimeConstant(1200u);
FilterChain     chain;
chain.append(&median);
chain.append(&smooth);

MeasurementPublisher publisher;
SensorService        service(sensor, chain, clock, publisher);

MyDisplay        panel;      TemperatureView gui(panel);       gui.attachTo(publisher);
MyMqttClient     mqtt;       MqttReporter    rep(mqtt);        rep.attachTo(publisher);
MyRelay          relay;      ThermostatEventPublisher events;
Thermostat       thermostat(relay, events);                    thermostat.attachTo(publisher);

service.begin();
for (;;) {
    service.poll();          // 到点才采样，不阻塞
    my_idle_or_wfi();
}
```

**检查 `attachTo()` 的返回值**：订阅表满了会返回 `Status::NoSpace`，
这是开机时就该发现的装配错误，不是运行时才暴露的问题。
容量不够就改 `SENSORFW_MAX_SUBSCRIBERS`（[config.hpp](../include/sensorfw/config.hpp)）。

## 4. 编译

```bash
cmake -S . -B build -DSENSORFW_TARGET=stm32
```

或者直接把这些文件加进你已有的工程（CubeIDE / IDF / Makefile）：

- 必需源文件：`src/core/types.cpp`、`src/service/sensor_service.cpp`、
  用到的 `drivers/*/*.cpp`、用到的 `app/*/*.cpp`
- include 路径：仓库根目录 和 `include/`
- 建议编译选项：`-std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics`

ESP-IDF 用户：把仓库做成一个 component，`CMakeLists.txt` 里
`idf_component_register(SRCS ... INCLUDE_DIRS "." "include")`，
并 `-DSENSORFW_TARGET_ESP32`。

## 5. RTOS 下怎么放

三种都行，取决于你的架构：

| 方式 | 做法 | 注意 |
|---|---|---|
| 裸机主循环 | `for(;;) { service.poll(); }` | 最简单，订阅者别做耗时操作 |
| 独立采样任务 | 任务里 `poll()` + `vTaskDelay(10)` | 订阅者回调运行在采样任务上下文 |
| 定时器回调 | 软件定时器里 `poll()` | 回调栈通常很小，GUI 重绘不要放这里 |

框架**不内置线程安全**。跨任务消费数据时，正确的做法是让一个订阅者只做
「把 `Measurement` 塞进 RTOS 队列」，消费任务从队列里取——
队列实现和 RTOS 强相关，所以留给移植方决定。

## 6. 验证移植是否正确

按这个顺序查，能定位 90% 的问题：

1. **时钟**：打印 `clock.nowMs()`，看是不是每秒涨 1000。
2. **原始值**：打印 `adc.readRaw()`。NTC 接地拓扑下，
   手捏住热敏电阻，计数值应该**下降**（温度升 → 阻值降 → 分压降）。
   反了就是 `ntcToGround` 填错了。
3. **换算**：常温下应该在 20~30 ℃。差很多先查 R0/B 值和分压电阻。
4. **服务**：`service.stats()` 看 `samplesAccepted` 是否按周期增长，
   `samplesRejected` 不为 0 说明超量程（多半是接线或换算问题）。
5. **应用层**：这一层在 PC 上已经被单元测试覆盖过了，一般不是它的问题。
