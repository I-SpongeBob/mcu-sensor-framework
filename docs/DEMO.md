# 演示输出

这两份输出是**直接从程序里截下来的**，不是手写的示意。想自己复现见
[BUILD.md](BUILD.md)；两个程序都不需要任何第三方依赖。

- [1. live_demo：实时仪表盘](#1-live_demo实时仪表盘)
- [2. host_demo：四个脚本化场景](#2-host_demo四个脚本化场景)

---

## 1. live_demo：实时仪表盘

`./build/live_demo`，实时刷新，键盘可交互。下面是 `t=84s` 的一帧
（已过 40s 的 −3 ℃ 阶跃，加热继电器已动作）。实机上 `#` 是青色、`.` 是灰色、
继电器 ON 时红色，这里为了贴进 Markdown 去掉了颜色转义：

```
 MCU sensor framework - live dashboard   t=  84.0s  frame 598

  GUI  (TemperatureView -> IDisplay, unmodified)
  +----------------------------------------------+
  | Temperature   19.5 C                         |
  | [#######.............]                       |
  | src=ntc-10k@adc1  t=82s                      |
  +----------------------------------------------+

  CHART  . raw  # filtered   filter: median+ewma+slew
   20.2 |       .                                 .
        | .    .      .   .
        |.    .    . .                                   .
        |        #### ###   ..             ..           .    .
        |########    #   #######  ..                             .
        |  .      .    ..       ###                         . .   #
        |   .       .              #######   .   .        . ###### ####
        |        .       .     .     .  . ##################   ..   . .
        |                       .        .             .
        |                  .  .     . .         .     .              .
   19.0 |                        .                  .              .
        +--------------------------------------------------------------

  SENSOR  healthy    accepted 598    rejected 0    errors 0    retries 0
  MQTT    link UP    sent 22     suppressed 576    dropped 0
  CTRL    heater ON         setpoint 21.0 C  switches 1
  LAST    raw  19.35 C   filtered  19.47 C   status Ok

  [1-7] filter   [f] sensor fault             [m] mqtt link   [+/-] setpoint   [r] reset   [q] quit
```

**怎么读这张图：**

- `.` 是原始值，`#` 是滤波后。两条曲线画在同一坐标系里，一眼能看出
  「滤波器太慢」和「传感器太吵」的区别——这也是 `Measurement` 里同时带
  `raw` 和 `filtered` 两个字段的原因。
- Y 轴按**滤波后**的曲线定标，不按原始值。否则一个 ±9 ℃ 的毛刺就会把坐标轴
  撑到 20 ℃ 量程，真正要看的信号被压成一行。落在窗口外的毛刺被钉在顶行/底行，
  看得见，但不左右刻度。
- 最上面那个方框是 `TemperatureView` 照常画进 `IDisplay` 的字符缓冲，
  仪表盘只是读出来嵌进整帧。上板子时这个方框会变成 SPI OLED 上的真实画面，
  而 `TemperatureView` 一行不用改。
- 曲线本身是**第四个订阅者**（`ChartRecorder`），加它的时候
  GUI、MQTT、恒温器和框架一行没改。

**按键：** `1`~`7` 热切换滤波器（与下面对比表的七种配置一一对应）、
`f` 注入传感器故障、`m` 断开 MQTT、`+`/`-` 调设定值、`r` 清曲线、`q` 退出。

建议按 `1` 和 `6` 来回切几次：同一段输入信号，曲线立刻变形，
而底层的传感器、驱动、MQTT、恒温器全程没有重启。

---

## 2. host_demo：四个脚本化场景

`./build/host_demo`，确定性输出，跑完自动退出（Windows 下双击会闪退，
用 `cmd /k build\host_demo.exe` 或重定向到文件）。CI 里每次提交都会跑这个。

```
  MCU sensor framework - host demo
  Layers: HAL | driver | filter | service | publisher | application

============================================================
 1. Filter comparison - identical input, seven configurations
============================================================
  Signal: 22 degC with a 0.6 degC/60s drift, a -3 degC step at t=40s,
          +/-0.45 degC white noise and a +/-9 degC spike every 17 samples.
  Scored against the ground truth the firmware cannot see.

  raw (no filter)             RMSE  2.234 degC   worst  9.426 degC   n=670
  moving-average(8)           RMSE  0.802 degC   worst  2.670 degC   n=670
  median(5)                   RMSE  0.230 degC   worst  2.702 degC   n=670
  ewma(tau=1.5s)              RMSE  0.438 degC   worst  2.698 degC   n=670
  kalman(Q=0.05,R=0.09)       RMSE  0.765 degC   worst  2.219 degC   n=670
  median(5)+ewma+slew         RMSE  0.343 degC   worst  3.065 degC   n=670
  outlier-gate+kalman         RMSE  0.255 degC   worst  2.909 degC   n=670

  Swapping any of these into a running system is one call to
  SensorService::setFilter(). No driver or application code changes.

============================================================
 2. One measurement channel, three independent consumers
============================================================
  publisher: 3/8 subscribers registered (GUI, MQTT, thermostat)

  +----------------------------------------------+
  | Temperature   22.2 C                         |
  | [#########...........]                       |
  | src=ntc-10k@adc1  t=0s                       |
  +----------------------------------------------+
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":22.220,"raw":22.220,"ts":0,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":22.472,"raw":22.588,"ts":9400,"status":"Ok"}
  +----------------------------------------------+
  | Temperature   22.5 C                         |
  | [#########...........]                       |
  | src=ntc-10k@adc1  t=10s                      |
  +----------------------------------------------+
  +----------------------------------------------+
  | Temperature   22.6 C                         |
  | [##########..........]                       |
  | src=ntc-10k@adc1  t=20s                      |
  +----------------------------------------------+
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":22.207,"raw":22.176,"ts":29000,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":21.947,"raw":21.982,"ts":30200,"status":"Ok"}
  +----------------------------------------------+
  | Temperature   22.0 C                         |
  | [#########...........]                       |
  | src=ntc-10k@adc1  t=30s                      |
  +----------------------------------------------+
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":21.689,"raw":21.506,"ts":35500,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":21.295,"raw":18.470,"ts":40200,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":20.897,"raw":18.513,"ts":40400,"status":"Ok"}
  +----------------------------------------------+
  | Temperature   20.7 C                         |
  | [########............]                       |
  | src=ntc-10k@adc1  t=40s                      |
  +----------------------------------------------+
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":20.538,"raw":18.126,"ts":40600,"status":"Ok"}
  [CTRL] heater-relay -> ON
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":20.227,"raw":18.362,"ts":40800,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":19.923,"raw":18.083,"ts":41000,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":19.567,"raw":18.384,"ts":41300,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":19.252,"raw":18.233,"ts":41700,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":18.987,"raw":18.857,"ts":42500,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":18.723,"raw":18.040,"ts":43600,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":18.467,"raw":17.997,"ts":45400,"status":"Ok"}
  +----------------------------------------------+
  | Temperature   18.4 C                         |
  | [######..............]                       |
  | src=ntc-10k@adc1  t=50s                      |
  +----------------------------------------------+
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":18.731,"raw":18.470,"ts":54400,"status":"Ok"}
  +----------------------------------------------+
  | Temperature   19.0 C                         |
  | [#######.............]                       |
  | src=ntc-10k@adc1  t=60s                      |
  +----------------------------------------------+
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":18.982,"raw":19.072,"ts":60500,"status":"Ok"}
  [MQTT] blueair/dev-0001/sensor/temperature (qos=0 retain=1)
         {"src":"ntc-10k@adc1","unit":"degC","value":19.264,"raw":28.397,"ts":64500,"status":"Ok"}

  --- counters after 70 s of run time -------------------
  samples accepted : 700
  GUI redraws      : 7   (rate limited + change detected)
  MQTT messages    : 18   (dead band + heartbeat, 682 suppressed)
  relay switches   : 1   (hysteresis + dwell times)
  Same data, three consumption policies, zero coupling.

============================================================
 3. Fault handling - broken sensor, fail-safe output, offline link
============================================================
  t=0s     healthy operation, heater should start
  [CTRL] heater-relay -> ON
           heater=ON  service.healthy=yes

  t=3s     ADC starts returning BusError (connector pulled out)
  t=3s     MQTT link also goes down
  [CTRL] heater-relay -> OFF
           service.healthy=no  read errors=9  reinit attempts=2
           thermostat.sensorLost=yes  heater=OFF  <-- fail-safe
           MQTT dropped=9 (kept as pending, not queued per sample)

  t=8s     connector plugged back in, link restored
  [CTRL] heater-relay -> ON
           service.healthy=yes  heater=ON  MQTT sent=18
           last payload: {"src":"ntc-10k@adc1","unit":"degC","value":23.142,"raw":22.198,"ts":13730,"status":"Ok"}

============================================================
 4. Analog NTC -> digital LM75B: one line changes
============================================================
  init: Ok   device probed over I2C, 1 transfers so far
  +----------------------------------------------+
  | Temperature   22.0 C                         |
  | [#########...........]                       |
  | src=lm75b@i2c1-0x48  t=0s                    |
  +----------------------------------------------+
  +----------------------------------------------+
  | Temperature   22.2 C                         |
  | [#########...........]                       |
  | src=lm75b@i2c1-0x48  t=5s                    |
  +----------------------------------------------+
  +----------------------------------------------+
  | Temperature   22.4 C                         |
  | [#########...........]                       |
  | src=lm75b@i2c1-0x48  t=10s                   |
  +----------------------------------------------+
  samples=60  last payload: {"src":"lm75b@i2c1-0x48","unit":"degC","value":22.407,"raw":22.500,"ts":9800,"status":"Ok"}

  Device missing on the bus:
  init: BusError   <-- detected at boot, not in the field

  Done. Run the unit tests with: ctest --test-dir build --output-on-failure
```

---

## 几处值得看的地方

**场景 1 的对比表**：滑动平均(8) 的 RMSE 是 0.802，反而比中值(5) 的 0.230 差
3.5 倍。因为毛刺不是白噪声，平均只会把 9 ℃ 的毛刺摊成 9/N ℃ 的持续偏差，
中值直接把它丢掉。这就是滤波必须可插拔、不能内置一种的原因。

**所有滤波行的 worst 都在 2.7 ℃ 左右，而且那不是毛刺**——是 t=40s 那个真实
阶跃的跟踪暂态。去噪和跟随真实变化本来就是同一个折中，框架把它交给集成者
按产品选。

**场景 2 的计数**：700 条采样，GUI 只重绘 7 次，MQTT 只发 18 条，继电器只动
1 次。同一条数据，三种消费策略，互不知情。

**场景 3 的故障链路**：注入 ADC 故障后，连续错误累计到阈值 → 服务标记故障并
停止读总线 → 退避重试 → 恒温器在超时后强制关加热（fail-safe）→ 恢复供电后
自动重连、继电器重新动作。MQTT 掉线期间只保留最新一条，不做逐条排队。

**场景 4 的驱动互换**：模拟 NTC 换成 I2C 的 LM75B，只改了一个对象的类型，
滤波、服务、GUI、MQTT、恒温器全部复用。最后还演示了设备不在总线上时，
`init()` 在开机阶段就返回 `BusError`——问题在出厂前暴露，不是在客户现场。
