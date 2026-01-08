
# EnglishTeacher 板级说明

本目录为 `BOARD_TYPE_English_Teacher`（EnglishTeacher）板级适配代码，主要特性：

- 墨水屏（EPD）：基于 Arduino GxEPD2（本仓库以组件形式 vendored）
- SD 卡：基于 SdFat（同样以组件形式 vendored）
- EPD 与 SD 共享同一条 SPI 总线
- 按键：全部物理按键直接由板级 `Button` 成员对象持有并绑定回调（不经过 ButtonManager）

---

## 关键文件

- `english-teacher.h/.cc`
	- 板级类 `EnglishTeacherBoard`，负责初始化：Arduino、共享 SPI、SD、EPD、按键和工具外设
- `config.h`
	- 引脚与外设参数配置（SPI/EPD/SD/按键/I2S/LED 等）
- `custom_epd_display.h/.cc`
	- EPD 封装：`Epd`（GxEPD2 驱动）与 `CustomEpdDisplay`（带互斥锁、兼容 DisplayLockGuard）
- `custom_sd_fat.h/.cc`
	- SD 封装：`CustomSdFat`（ReadFile/OpenMp3/OpenImage 等便捷接口）
- `test_demo.cc`
	- EPD/SD 等外设的演示与测试代码（用于调试，不一定参与主流程）

---

## 启用方式

该板子通过 Kconfig 的 `BOARD_TYPE_English_Teacher` 选择启用。

工程构建层面：

- 选择该板后，`main/CMakeLists.txt` 会：
	- 自动把 `main/eteacher/*.cc` 加入编译
	- 添加依赖组件：`arduino-esp32`、`SdFat`、`GxEPD2`

提示：本目录还有 `config.json`，用于某些构建配置（例如目标芯片、sdkconfig_append）。

---

## 硬件连接与引脚

引脚定义集中在 `config.h`，典型包含：

### 1) SPI（EPD/SD 共享）

- `SPI_PIN_NUM_MOSI`
- `SPI_PIN_NUM_MISO`
- `SPI_PIN_NUM_CLK`
- `SD_PIN_NUM_CS`
- `EPD_PIN_NUM_CS / DC / RST / BUSY`

板级初始化中会先对 CS/DC/RST 等做 `pinMode/digitalWrite`，然后调用 `SPI.begin(...)`：

- 代码位置：`EnglishTeacherBoard::InitializeArduinoAndSharedSpi()`（见 `english-teacher.cc`）

### 2) 按键

按键 GPIO 定义同样在 `config.h`，包含方向键、A/B/C/D、Select/Start、音量键等。

当前绑定逻辑：

- 方向键/Select/Start/音量键：映射到 `AppManager::HandleButton(AppButton::...)`
- A(BOOT)：单击切换聊天/配网；长按直接进入配网
- B(TOUCH)：按下/抬起都触发 `AppButton::Ptt`（按住说话）
- C/D：仅打印日志（占位）

代码位置：`EnglishTeacherBoard::InitializeButtons()`

---

## EPD（墨水屏）

### 1) 面板型号

当前默认面板驱动在 `custom_epd_display.h` 中固定为：

- Good Display 4.2" b/w 400x300（`GxEPD2_420_GDEY042T81`，SSD1683）

如果你的硬件面板不是该型号：

- 需要替换 `custom_epd_display.h` 里对应的 include/driver 类型
- 同时确保 `config.h` 中面板选择宏与你的硬件一致


### 2) 初始化顺序

EPD 初始化流程：

1. 先完成 Arduino 初始化与 SPI 总线准备（与 SD 共用）
2. 调用 `CustomEpdDisplay::Begin(SPI, spi_hz)`

代码位置：`EnglishTeacherBoard::InitializeEpd()`

### 3) epd API接口

1.Driver用于提供gxepd2的对象
2.DrawUtf8 用于显示特定字库字符串
---

## EpdManager（刷新调度）

为避免业务侧直接高频刷新 EPD，板级在 EPD 初始化成功后会启动 `EpdManager`：

- 代码位置：`EnglishTeacherBoard::InitializeEpd()` 中 `EpdManager::GetInstance().Init(&display_)`

`EpdManager` 的详细策略（局刷/快刷/全刷、最小刷新间隔、局刷 N 次强制快刷等）请见：

- `main/eteacher/epd_manager/readme.md`

---

## SD 卡

SD 初始化流程：

- 通过 `SdSpiConfig` 指定 CS、SPI 总线类型、SPI 频率与 SPI 对象
- 调用 `CustomSdFat::Begin(cfg)`

代码位置：`EnglishTeacherBoard::InitializeSdCard()`

---

## 常见问题

### 1) 为什么要先 pinMode 再 SPI.begin？

GxEPD2/SdFat 基于 Arduino，部分路径会在 init 过程调用 `digitalWrite()`；若引脚模式未提前设置，会出现日志报错或行为不稳定。

### 2) EPD 与 SD 共用 SPI 会冲突吗？

只要：

- CS 管脚正确
- 访问时序不并发（或正确做互斥/锁）

即可共存。本板级把显示相关操作包装进 `DisplayLockGuard`，并采用“单任务刷新”策略降低并发风险。

