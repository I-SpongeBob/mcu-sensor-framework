# 构建与运行指南

从零开始，到看见界面跑起来。**不需要任何第三方库**——只要一个 C++11 编译器和 CMake。

---

## 0. 需要什么

| 依赖 | 版本 | 说明 |
|---|---|---|
| C++ 编译器 | 支持 C++11 即可 | GCC / Clang / MSVC / MinGW-w64 都行 |
| CMake | ≥ 3.16 | |
| 第三方库 | **无** | 测试框架也是自带的 60 行断言宏 |

## 1. Windows

### 方式 A：命令行（推荐，最快）

没装工具链的话，用 winget 一条命令装好（Win10/11 自带 winget）：

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT    # MinGW-w64 (GCC)
winget install Kitware.CMake
```

装完**重开一个终端**（让 PATH 生效），然后：

```powershell
cd <仓库目录>
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j
```

生成的可执行文件在 `build\` 下：

```powershell
build\host_demo.exe        # 四个演示场景，跑完自动退出
build\live_demo.exe        # 实时仪表盘，键盘交互，按 q 退出
ctest --test-dir build --output-on-failure    # 5 个测试套件
```

Windows 上的 exe 是**静态链接**的，不依赖任何运行时 DLL，可以直接双击或拷到别的机器上跑。

### 方式 B：Visual Studio

用 VS 2019/2022 直接「打开本地文件夹」选中仓库目录，VS 会自动识别 CMake 工程，
在工具栏选择启动项 `live_demo.exe` 后按 F5。

或者用 Developer Command Prompt：

```cmd
cmake -S . -B build
cmake --build build --config Release
build\Release\live_demo.exe
```

> 说明：MinGW（本地）和 Linux GCC（CI）是每次提交都实测的。
> 代码本身没有用任何 GCC 扩展，MSVC 分支在 CMake 里也配好了（`/W4`），
> 但没有在 MSVC 上实机验证过。

### 方式 C：CLion / VS Code

CLion 直接打开目录即可。VS Code 装 CMake Tools 扩展，打开目录后按提示选编译器。

## 2. Linux / macOS

```bash
sudo apt install build-essential cmake     # Debian/Ubuntu
# 或 brew install cmake                     # macOS

cmake -S . -B build
cmake --build build -j
./build/host_demo
./build/live_demo
ctest --test-dir build --output-on-failure
```

CI 每次提交都会在 ubuntu-latest 上跑完整的 build + test + demo，
配置见 [.github/workflows/ci.yml](../.github/workflows/ci.yml)。

---

## 3. 生成了哪些程序

| 程序 | 作用 | 建议先看哪个 |
|---|---|---|
| `live_demo` | **实时终端仪表盘**，可用键盘热切换滤波器、注入故障 | ← 从这个开始 |
| `host_demo` | 四个脚本化场景：滤波对比表、三消费者解耦、故障注入、驱动互换 | 第二个 |
| `test_filters` 等 5 个 | 单元测试，也可以单独运行看断言明细 | |

不想编译也能看效果：[DEMO.md](DEMO.md) 里是这两个程序的完整实际输出。

### live_demo 的按键

| 键 | 效果 |
|---|---|
| `1`~`7` | 运行时热切换滤波器，与 `host_demo` 对比表里的七种配置一一对应：<br>直通 / 滑动平均(8) / 中值(5) / EWMA / 卡尔曼 / 中值+EWMA+限斜率 / 野点门限+卡尔曼 |
| `f` | 注入 ADC 故障，看故障计数、退避重连、恒温器 fail-safe |
| `m` | 断开 MQTT 链路，看掉线只保留最新一条 |
| `+` `-` | 调整温度设定值 |
| `r` | 清空曲线 |
| `q` | 退出 |

建议按 `1` 和 `6` 来回切几次：同一段输入信号，曲线立刻变形，
而底下的传感器、驱动、MQTT、恒温器全程没有重启——这就是
`SensorService::setFilter()` 一行调用的效果。

### host_demo 双击会闪退，这是正常的

`host_demo` 不是交互程序：四个场景一口气打印完就自己退出，
所以双击时窗口会闪一下就关掉。想看输出用下面任一种：

```cmd
cmd /k build\host_demo.exe          :: 跑完窗口留着，可以往上滚
build\host_demo.exe > demo.txt      :: 输出较长，存文件更好读
```

Linux/macOS 下从终端直接运行即可，不存在这个问题。
需要交互界面请用 `live_demo`，它会一直运行到你按 `q`。

---

## 4. 交叉编译到 MCU

```bash
cmake -S . -B build -DSENSORFW_TARGET=stm32    # 或 esp32
```

这会定义 `SENSORFW_TARGET_STM32`，启用 [port/stm32/](../port/stm32/) 里的移植层。
完整的上板步骤（含 CubeIDE / ESP-IDF component 的接法）见 [PORTING.md](PORTING.md)。

## 5. 构建选项

| 选项 | 默认 | 作用 |
|---|---|---|
| `SENSORFW_TARGET` | `host` | 目标平台：`host` / `stm32` / `esp32` |
| `SENSORFW_BUILD_TESTS` | `ON` | 编译 5 个测试套件 |
| `SENSORFW_BUILD_EXAMPLE` | `ON` | 编译两个 demo |
| `SENSORFW_FREESTANDING` | `ON` | 用 `-fno-exceptions -fno-rtti` 编译（MCU 配置） |

## 6. 遇到问题

**`cmake` 或 `g++` 找不到** —— 装完工具链要重开终端，PATH 才会生效。

**提示缺少 `libstdc++-6.dll`** —— 说明用的是旧版本。当前版本已改为静态链接，
重新 `cmake -S . -B build` 配置一次再编译即可。

**仪表盘显示成一堆 `[0m[36m` 乱码** —— 终端不支持 ANSI 转义。
Windows 10 1511 之前的 conhost 会这样，换 Windows Terminal 即可。

**中文终端下字符错位** —— 界面全部是 ASCII 字符，不涉及编码问题；
如果错位，多半是终端窗口宽度不足 80 列，拉宽即可。

**`ctest` 报找不到测试** —— 检查配置时 `SENSORFW_BUILD_TESTS` 是否为 `ON`，
以及是否在仓库根目录执行。
