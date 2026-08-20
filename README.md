# 跨 MCU 平台 Sensor 框架（C++11 / 温度传感器实现）

[![build-and-test](https://github.com/I-SpongeBob/mcu-sensor-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/I-SpongeBob/mcu-sensor-framework/actions/workflows/ci.yml)
![C++11](https://img.shields.io/badge/C%2B%2B-11-blue)
![no heap](https://img.shields.io/badge/heap-none-brightgreen)
![license](https://img.shields.io/badge/license-MIT-lightgrey)

> A portable, allocation-free C++11 sensor framework for microcontrollers.
> One temperature pipeline — HAL → driver → filter chain → service → publisher —
> feeds a GUI, an MQTT reporter and a closed-loop thermostat that know nothing
> about each other or about the sensor. Ports for STM32 (CubeMX HAL) and
> ESP32 (ESP-IDF) included; the host port simulates an NTC divider and an LM75B
> so the whole stack builds, runs and is unit-tested on a PC.

一句话：**传感器怎么采、怎么滤、给谁用，三件事互不认识。**
换 MCU 只改 `port/`，换传感器只改 `drivers/`，换滤波只改一行组装代码，
加一个新的数据消费者（GUI / MQTT / 业务逻辑 / 数据记录）不需要动任何已有代码。

---

## 1. 快速开始

```bash
cmake -S . -B build            # 默认 host 目标
cmake --build build -j
./build/host_demo              # 四个演示场景（一次跑完就退出）
./build/live_demo              # 实时终端仪表盘，键盘可交互
ctest --test-dir build --output-on-failure
```

Windows 上加 `-G "MinGW Makefiles"`，生成的是 `build\live_demo.exe`（静态链接，
不依赖运行时 DLL，可直接双击）。**从零开始的完整步骤、按键说明和常见问题见
[docs/BUILD.md](docs/BUILD.md)。**

无第三方依赖，只要一个 C++11 编译器和 CMake ≥ 3.16。

**当前状态**（GCC 16.1，`-Wall -Wextra -Wshadow -Wconversion -Wold-style-cast`）：

```
20 个编译单元，0 warning
100% tests passed out of 5      test_filters   : 37 checks, 0 failures
                                test_publisher : 31 checks, 0 failures
                                test_drivers   : 31 checks, 0 failures
                                test_service   : 28 checks, 0 failures
                                test_app       : 50 checks, 0 failures
```

---

## 2. 分层架构

### 2.1 数据流总览

```
  HAL            DRIVER            SERVICE             APPLICATION
  ---            ------            -------             -----------

  IAdcChannel -> NtcThermistor -+
                                |
  II2cBus     -> Lm75Sensor ----+-> SensorService -+-> GUI display
                                |         |        |
  IClock      ------------------+         |        +-> MQTT reporter
                                          |        |
                                       IFilter     +-> Thermostat
                                          |
                                          v
                              median -> ewma -> slew
```

一次采样的完整路径：HAL 取原始值 → 驱动换算成物理量 → 服务层做调度与容错
→ 滤波链条件化 → `Publisher` 扇出给三个互不知情的消费者。

图中四个接口名就是四条解耦线，分别切开了四件事：

| 接口 | 切开了 | 换掉它意味着 |
|---|---|---|
| `IAdcChannel` / `II2cBus` / `IClock` | **芯片** | 换 MCU 只改 `port/` 下的三个实现 |
| `ISensor` | **器件** | NTC 换 LM75，上层零改动 |
| `IFilter` | **算法** | 加卡尔曼不动任何老代码 |
| `Publisher<Measurement>` | **用途** | 加第四个消费者（如数据记录）改 0 行框架代码 |

### 2.2 依赖层次

```
+--------------------------------------------------------------+
|  APPLICATION LAYER                                           |
|  TemperatureView      MqttReporter       Thermostat          |
+----------^-------------------^-------------------^-----------+
           |                   |                   |  subscribe(Callback)
+----------+-------------------+-------------------+-----------+
|  DECOUPLING POINT   <-- the only seam in the system          |
|  Publisher<Measurement>   fixed array, no heap               |
+------------------------------^-------------------------------+
                               |  publish(Measurement)
+--------------------------------------------------------------+
|  SERVICE LAYER                                               |
|  SensorService   scheduling / plausibility / fault retry     |
+------------------------------^-------------------------------+
                               |  update(value, timestamp)
+--------------------------------------------------------------+
|  FILTER LAYER                                                |
|  IFilter   median / moving-average / EWMA / Kalman /         |
|  slew-limiter / outlier-gate    FilterChain composes         |
+------------------------------^-------------------------------+
                               |  read(Sample)
+--------------------------------------------------------------+
|  DRIVER LAYER                                                |
|  ISensor   NtcThermistorSensor / Lm75Sensor                  |
+------------------------------^-------------------------------+
                               |  readRaw() / writeRead() / nowMs()
+--------------------------------------------------------------+
|  HAL LAYER                                                   |
|  IAdcChannel      II2cBus      IClock                        |
+------------------------------^-------------------------------+
                               |
+--------------------------------------------------------------+
|  PORT LAYER         <-- swapping MCU only touches this       |
|  port/stm32    port/esp32    port/host (simulation)          |
+--------------------------------------------------------------+
```

图内保持纯 ASCII（避免中英文混排在不同字体下错位），中文对照如下：

| 图中 | 中文 | 职责 |
|---|---|---|
| APPLICATION LAYER | 应用层 | 三个互不知情的消费者：GUI 显示 / MQTT 上报 / 恒温控制 |
| DECOUPLING POINT | 解耦点 | `Publisher<Measurement>` 发布订阅，静态数组无堆 |
| SERVICE LAYER | 服务层 | 定时采样 / 合理性校验 / 故障重试 |
| FILTER LAYER | 滤波层 | 中值 / 滑动平均 / EWMA / 卡尔曼 / 限斜率 / 野点门限，`FilterChain` 任意串联 |
| DRIVER LAYER | 驱动层 | `ISensor` 实现：NTC 热敏电阻 / LM75 数字传感器 |
| HAL LAYER | HAL 抽象层 | 三个接口：`IAdcChannel` `II2cBus` `IClock` |
| PORT LAYER | 平台移植层 | 换芯片只改这一层 |


每一层只依赖它下面一层的**接口**，不依赖实现。所有装配都集中在
[examples/host_demo/main.cpp](examples/host_demo/main.cpp) 这一个「组装根」里，
框架代码本身没有任何 `new`、任何全局单例、任何硬编码的依赖。

---

## 3. 灵活的滤波：策略 + 组合

`IFilter` 只有三个方法（`update / reset / name`），已实现 7 种：

| 滤波器 | 文件 | 适合处理 | 代价 |
|---|---|---|---|
| `PassThroughFilter` | [pass_through.hpp](include/sensorfw/filter/pass_through.hpp) | 产测/标定原始值 | 0 |
| `MovingAverageFilter<N>` | [moving_average.hpp](include/sensorfw/filter/moving_average.hpp) | 零均值白噪声，衰减 √N | N 个采样的群延迟 |
| `MedianFilter<N>` | [median.hpp](include/sensorfw/filter/median.hpp) | **脉冲干扰**（ADC 毛刺、I2C 误码） | N² 排序，N 很小 |
| `EwmaFilter` | [ewma.hpp](include/sensorfw/filter/ewma.hpp) | 慢变量的通用一阶低通 | 一次乘法，2 个状态字 |
| `Kalman1dFilter` | [kalman1d.hpp](include/sensorfw/filter/kalman1d.hpp) | 增益自适应：启动快、稳态平 | 一次除法 |
| `SlewRateLimiter` | [slew_rate_limiter.hpp](include/sensorfw/filter/slew_rate_limiter.hpp) | 物理限速兜底（℃/s） | 常数 |
| `OutlierGate` | [outlier_gate.hpp](include/sensorfw/filter/outlier_gate.hpp) | 野点剔除，**带连续拒绝上限** | 常数 |

`FilterChain` 本身也是 `IFilter`，所以可以任意串联、甚至嵌套：

```cpp
MedianFilter<5> median;                                  // 先杀毛刺
EwmaFilter      smooth = EwmaFilter::withTimeConstant(1200);  // 再平滑白噪声
SlewRateLimiter slew(3.0f);                              // 最后限物理变化率
FilterChain chain;
chain.append(&median);
chain.append(&smooth);
chain.append(&slew);

SensorService service(sensor, chain, clock, publisher);  // 就这一行是"选择"
```

运行时切换（例如「响应优先」和「省电平滑」两套 profile）：
`service.setFilter(otherChain);` —— 驱动、服务、应用层一行不动。

### 实测对比（`./build/host_demo` 场景 1）

同一段输入信号：22 ℃ 基线 + 0.6 ℃/60s 缓慢漂移 + **t=40s 时 −3 ℃ 的真实阶跃**
+ ±0.45 ℃ 白噪声 + 每 17 个采样一次 ±9 ℃ 毛刺。与仿真器才知道的真值比较：

```
  raw (no filter)             RMSE  2.234 degC   worst  9.426 degC
  moving-average(8)           RMSE  0.802 degC   worst  2.670 degC
  median(5)                   RMSE  0.230 degC   worst  2.702 degC
  ewma(tau=1.5s)              RMSE  0.438 degC   worst  2.698 degC
  kalman(Q=0.05,R=0.09)       RMSE  0.765 degC   worst  2.219 degC
  median(5)+ewma+slew         RMSE  0.343 degC   worst  3.065 degC
  outlier-gate+kalman         RMSE  0.255 degC   worst  2.909 degC
```

两点值得读一下：

- **滑动平均反而比中值差 3.5 倍**（0.802 vs 0.230）。因为毛刺不是白噪声，
  平均只会把 9 ℃ 的毛刺摊成 9/N ℃ 的持续偏差，中值直接把它丢掉。
  这就是"多种滤波方式"必须可选、而不能内置一种的原因。
- **所有滤波行的 worst 都在 2.7 ℃ 左右，而且这不是毛刺**——是 t=40s 那个
  真实阶跃的跟踪暂态。去噪和跟随真实变化本来就是同一个折中，
  所以框架把这个折中交给集成者按产品选，而不是替他做主。

---

## 4. 应用层解耦：三个互不知情的消费者

`Publisher<Measurement>` 是全系统**唯一**的耦合点。三个消费者各自订阅：

| 消费者 | 关心什么 | 自己的策略 |
|---|---|---|
| [`TemperatureView`](app/gui/temperature_view.hpp) | 人眼能看到的变化 | 量化到 0.1 ℃ 去重 + 刷新限流；故障状态**跳过限流**立即显示 |
| [`MqttReporter`](app/mqtt/mqtt_reporter.hpp) | 云端需要知道的变化 | 死区 + 心跳（report-by-exception）；掉线只保留**最新一条**待发 |
| [`Thermostat`](app/logic/thermostat.hpp) | 控制决策 | 滞回 + 最短启停时间 + 传感器失效 fail-safe + 超温闭锁 |

同一条数据，三种消费策略，实测（场景 2，70 秒 @10 Hz）：

```
  samples accepted : 700     采集了 700 条
  GUI redraws      : 7       (限流 + 变化检测)
  MQTT messages    : 18      (死区 + 心跳, 682 条被抑制)
  relay switches   : 1       (滞回 + 最短启停：只在 t=40s 降温后启动一次)
```

**加第四个消费者（比如 SD 卡记录、Modbus 从站）要改的代码量：**
写一个类 + 一行 `attachTo(publisher)`。框架、驱动、滤波、其它消费者，零改动。

订阅回调用的是自己实现的 `Callback<T>`（[callback.hpp](include/sensorfw/core/callback.hpp)）：
两个指针，成员函数指针作为模板参数，编译期去虚化——
`std::function` 会堆分配且拖进异常机制，在固件里基本不能用。

### 实时仪表盘：`./build/live_demo`

同一套代码实时跑，键盘可交互。下面是真实截取的一帧（`t=74s`，刚过 40s 的 −3 ℃ 阶跃，
加热继电器已经动作）：

```
  GUI  (TemperatureView -> IDisplay, unmodified)
  +----------------------------------------------+
  | Temperature   19.7 C                         |
  | [#######.............]                       |
  | src=ntc-10k@adc1  t=74s                      |
  +----------------------------------------------+

  CHART  . raw  # filtered   filter: median+ewma+slew
   20.1 |   .                                 .
        |                                  .               . .    .
        |               .             .               .  .  .   ..
        |     ..  .         .  .    .     .               .   .   #####
        |          .               .             .            ####   .
        | .       #####       . .       ..  ########       ###
        |  .######  .  #####     ########### .  .  .#######         . .
        |### .        .     #####      .
        |            . . ...                .     .  .  .
        |       .                .   .
   18.9 |.                   .                                 .
        +--------------------------------------------------------------

  SENSOR  healthy    accepted 534    rejected 0    errors 0    retries 0
  MQTT    link UP    sent 20     suppressed 514    dropped 0
  CTRL    heater ON         setpoint 21.0 C  switches 1
  LAST    raw  19.37 C   filtered  19.73 C   status Ok

  [1-5] filter   [f] sensor fault   [m] mqtt link   [+/-] setpoint   [r] reset   [q] quit
```

`.` 是原始值，`#` 是滤波后。按 `1`~`7` 可以**运行时热切换滤波器**（和上面对比表里的
七种配置一一对应）看曲线立刻变化，
按 `f` 拔掉传感器看故障保护和自动重连，按 `m` 断开 MQTT 看掉线只留最新一条。

两个细节：

- **Y 轴按滤波后的曲线定标，不按原始值。** 否则一个 ±9 ℃ 的毛刺就会把坐标轴
  撑到 20 ℃ 量程，真正想看的信号被压成一条线。落在窗口外的毛刺被钉在
  顶行/底行显示——看得见，但不再左右刻度。
- **仪表盘本身就是第四个订阅者。** `ChartRecorder` 只是又 `attachTo(publisher)` 了一次，
  GUI、MQTT、恒温器和框架**一行没改**——「加消费者零成本」这句话在这里是被执行的，
  不是被声称的。上面那个 GUI 方框里的内容，是 `TemperatureView` 照常画进
  `IDisplay` 的字符缓冲，仪表盘只是把它读出来嵌进整帧里。

---

## 5. 跨 MCU 移植：只需要实现三个接口

框架对硬件的全部要求就是三个接口，加起来不到 20 行声明：

```cpp
class IClock      { virtual TimestampMs nowMs() const = 0; };
class IAdcChannel { virtual Status readRaw(uint16_t&) = 0; ... };   // 模拟量传感器需要
class II2cBus     { virtual Status write(...), writeRead(...) = 0; };// 数字传感器需要
```

已提供的移植层：

| 平台 | 文件 | 底层 API |
|---|---|---|
| STM32 | [port/stm32/stm32_platform.hpp](port/stm32/stm32_platform.hpp) | `HAL_GetTick` / `HAL_ADC_*` / `HAL_I2C_Mem_Read` |
| ESP32 | [port/esp32/esp32_platform.hpp](port/esp32/esp32_platform.hpp) | `esp_timer_get_time` / `adc_oneshot_read` / `i2c_master_transmit_receive` |
| Host（仿真） | [port/host/](port/host/) | 虚拟时钟 + NTC 分压反解 + LM75B 寄存器模拟 |

移植到一块新板子的完整步骤见 [docs/PORTING.md](docs/PORTING.md)，实际就是：

1. 写三个类实现上面三个接口（约 80 行，抄 `port/stm32` 改寄存器名）；
2. 在组装根里 new 出来（或静态定义）并注入；
3. 编译时 `-DSENSORFW_TARGET=stm32`。

`drivers/`、`include/sensorfw/`、`app/` 三个目录**一行都不用改**——
demo 场景 4 就是同一套上层代码，把模拟 NTC 换成 I2C 的 LM75B，只改了一个对象的类型。

---

## 6. 面向 MCU 的工程约束

这些不是风格偏好，是能不能上产品的问题：

| 约束 | 做法 | 在哪里体现 |
|---|---|---|
| **零动态内存** | 所有容器容量是模板参数或宏，成员数组 | `RingBuffer<T,N>`、`Publisher<T,Cap>`、`FilterChain` |
| **无异常 / 无 RTTI** | 错误是返回值 `Status`，编译加 `-fno-exceptions -fno-rtti` | 全局；CMake 里 `SENSORFW_FREESTANDING` |
| **不为 vtable 多付钱** | 接口用 `protected: ~I…() {}` 非虚析构（框架从不 delete 基类指针） | 所有 `I*` 接口 |
| **不拉入浮点 printf** | 显示/JSON 用整数（十分度、千分度）手工格式化 | `temperature_view.cpp`、`mqtt_reporter.cpp` |
| **49.7 天 tick 回绕** | 所有时间差走无符号减法 `elapsedMs()` | [core/time.hpp](include/sensorfw/core/time.hpp)，有专门单测 |
| **主循环不阻塞** | `poll()` 到点才动作；慢器件返回 `NotReady` 下次再来 | `SensorService::poll()` |
| **float 可换定点** | `Real` 一个 typedef 集中定义 | [config.hpp](include/sensorfw/config.hpp) |
| **移植性优先于取巧** | LM75 符号扩展手写，不依赖有符号右移的实现定义行为 | `lm75.cpp` |

### 实测内存占用

x86-64 实测（指针 8 字节）；Cortex-M 一栏是按 4 字节指针推算的估计值：

| 对象 | x86-64 实测 | Cortex-M 估计 |
|---|---|---|
| `Measurement`（一条数据） | 32 B | ~20 B |
| `Callback<const Measurement&>`（一个订阅者） | 16 B | 8 B |
| `Publisher<Measurement, 8>` | 136 B | ~68 B |
| `EwmaFilter` / `Kalman1dFilter` / `MedianFilter<5>` | 32 B | ~24 B |
| `FilterChain`（6 级容量） | 64 B | ~28 B |
| `SensorService` | 112 B | ~64 B |
| `TemperatureView` / `Thermostat` | 72 / 56 B | ~48 / ~40 B |
| `MqttReporter`（含 160 B payload 缓冲） | 224 B | ~192 B |

整条链路的静态 RAM 在 Cortex-M 上约 **0.5 KB**，堆用量恒为 **0**。

---

## 7. 测试

5 个测试套件，全部在 host 上跑，不需要硬件：

| 套件 | 覆盖 |
|---|---|
| [test_filters](tests/test_filters.cpp) | 7 种滤波的行为：DC 增益、毛刺抑制、时间常数与采样率解耦、卡尔曼收敛、野点门限的重同步、链的容量上限 |
| [test_publisher](tests/test_publisher.cpp) | 发布订阅：全员送达、注册顺序、容量拒绝、空回调拒绝、环形缓冲、**tick 回绕** |
| [test_drivers](tests/test_drivers.cpp) | NTC 换算（25 ℃ 基准点、单调性、分压拓扑、开短路）、LM75 **数据手册用例**（含负温、未定义低位）、总线错误传播 |
| [test_service](tests/test_service.cpp) | 采样周期与驱动下限钳位、开机首采样、越界剔除、`NotReady` 不算错、连续错误 → 故障 → 退避重试 → 恢复、运行时换滤波器 |
| [test_app](tests/test_app.cpp) | GUI 去重/限流/故障优先、MQTT 死区/心跳/掉线只留最新/JSON 内容、恒温器滞回/最短启停/失效 fail-safe/超温闭锁 |

测试用的是自带的 60 行断言宏（[test_support.hpp](tests/test_support.hpp)），
不引入 GoogleTest——框架是 `-fno-exceptions` 编译的，而且要保证离线可构建。

---

## 8. 目录结构

```
include/sensorfw/          可移植框架（纯接口 + 模板，头文件为主）
  config.hpp               编译期配置：Real 类型、容量上限
  core/                    types / time / callback / publisher / ring_buffer
  hal/                     clock.hpp  adc.hpp  i2c.hpp   ← 移植只认这三个
  filter/                  IFilter + 7 种实现 + FilterChain
  sensor/                  ISensor / ITemperatureSensor
  service/                 SensorService
src/                       非模板实现（types.cpp / sensor_service.cpp）
drivers/
  ntc/                     10k/B3950 热敏电阻（ADC + Beta 公式）
  lm75/                    LM75B / TMP75（I2C，11 bit）
app/
  gui/                     IDisplay + TemperatureView
  mqtt/                    IMqttClient + MqttReporter
  logic/                   ISwitchOutput + Thermostat（滞回控制）
port/
  host/                    虚拟时钟、NTC 分压仿真、LM75 总线仿真、终端外设、
                           ANSI 仪表盘（终端控制 + 键盘 + 图表订阅者）
  stm32/                   CubeMX HAL 移植
  esp32/                   ESP-IDF 移植
examples/host_demo/        组装根 + 四个演示场景（确定性，CI 里跑）
examples/live_demo/        实时终端仪表盘（ANSI，键盘交互）
tests/                     5 个测试套件
docs/BUILD.md              构建与运行指南（含 Windows/Linux 完整步骤）
docs/PORTING.md            移植指南
```

## 9. 怎么扩展

**加一个新传感器**（比如 SHT40 温湿度）：在 `drivers/` 下实现 `ISensor`，
`info()` 里填量程和最快采样周期。上层不用改。

**加一个新滤波**（比如 Butterworth 二阶）：实现 `IFilter` 的三个方法，
`chain.append(&yourFilter)`。其它滤波器和所有上层不用改。

**加一个新消费者**（比如数据记录、Modbus、报警蜂鸣器）：
写个类带 `void onX(const Measurement&)`，
`publisher.subscribe(Publisher<Measurement>::Subscriber::bind<C, &C::onX>(&obj))`。
零改动。

**加一种新物理量**（湿度、PM2.5）：`Quantity` 加一个枚举值，
再加一个像 `ITemperatureSensor` 那样的空标记接口保证类型安全。
`Publisher`、`SensorService`、滤波器全都是量纲无关的，直接复用。

## 10. 设计取舍与已知限制

诚实地列一下：

- **用了运行时多态（虚函数）而不是模板静态多态。** 每个接口多一个 vtable 指针、
  每次调用多一次间接跳转。换来的是可以在运行时换滤波器、换驱动，
  以及所有上层代码只编译一份。对 Cortex-M0 上极端受限的场景，
  这些接口都可以换成 CRTP 模板而不改变分层结构——但那样就没法运行时切换了。
- **`Publisher::publish()` 在采样上下文里同步调用所有订阅者。** 好处是零缓冲、
  确定性；代价是订阅者必须自己保证不阻塞（`MqttReporter` 因此只交给网络任务、
  不自己发包）。如果要跨 RTOS 任务边界，正确的做法是在这一层后面挂一个队列，
  框架没有内置，因为队列实现和 RTOS 强相关。
- **线程安全没有内置。** 当前假设一个传感器归一个上下文（主循环或一个任务）。
  多任务共享时需要在 `poll()` 外面加锁，或者用上面说的队列方案。
- **`SensorService` 一个实例管一个传感器。** 多传感器就多实例（各自的 filter 和
  publisher，或共享一个 publisher）；没有做「传感器管理器」，因为那通常
  是把不该耦合的东西又耦合回去。
- **host 仿真里的噪声是均匀分布不是高斯分布。** 对比较滤波器足够，
  但别把 RMSE 数字当成真实器件的性能指标。
- **MQTT 掉线只保留最新一条**，不做离线缓存补传。对温度这种状态量是对的
  （旧值没有价值），对需要完整历史的量（累计电量）就需要另加缓存层。

## 11. 许可

MIT，见 [LICENSE](LICENSE)。
