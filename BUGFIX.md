# Bug 修复记录

本文档记录了 weather_clock 项目从桌面迁移到 C 盘、重新编译烧录后遇到的所有 Bug 及其修复过程。原始有 Bug 的代码保留在 `weather_clock/` 目录下作为存档。

---

## Bug #1: Interrupt WDT Timeout — FreeRTOS 内核崩溃 (致命)

### 现象

```
Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU0).
Backtrace: ... vListInsert → xTaskDelayUntil → clock_task:53
```

ESP32 反复重启，Watchdog 超时触发 panic。

### 根因

`display_data.h` 声明 `QueueHandle_t g_display_queue`，`main.c` 创建为 FreeRTOS Queue，但 3 个任务文件中用 `xTaskNotifyGive((TaskHandle_t)g_display_queue)` 强制将 Queue 句柄当 Task 句柄使用。

`xTaskNotifyGive()` 会直接修改 Task 控制块 (TCB) 的 notification 字段。把一个 Queue 结构体的内存当 TCB 修改，破坏了 FreeRTOS 内核的延迟任务链表。当 `vTaskDelayUntil` 遍历被破坏的链表时，陷入死循环。

**为什么之前能跑？**

旧版 ESP-IDF 的堆分配器布局不同，被破坏的内存刚好没踩到链表关键节点。v5.4 的分配器布局变化导致损坏区域恰好覆盖了延迟任务链表。

### 修复

| 文件 | 改动 |
|------|------|
| `display_data.h` | `QueueHandle_t g_display_queue` → `TaskHandle_t g_display_task_h` |
| `main.c` | 删 `xQueueCreate`，`xTaskCreate` 第 6 参数获取 Task 句柄 |
| `clock_task.c` | 去掉强制转型，直接用 `g_display_task_h` |
| `network_task.c` | 同上 |
| `power_task.c` | 同上 |

---

## Bug #2: Watchdog Task 栈溢出 (致命)

### 现象

```
***ERROR*** A stack overflow in task watchdog has been detected.
Backtrace: ... vApplicationStackOverflowHook → vTaskSwitchContext
```

### 根因

`STACK_WATCHDOG = 1536` 字节，但 `esp_task_wdt_add()` + `ESP_LOGI` + 4×`ESP_LOGW` 的调用链在 ESP-IDF v5.4 中栈开销显著增大（日志子系统格式化 + 环形缓冲区），峰值栈深度约 2200~2500 字节。

### 修复

- `STACK_WATCHDOG`: 1536 → 2560
- 同时增大其他任务栈: Display 2048→4096, Clock 1536→2560, Network 4096→6144, Power 1024→3584

---

## Bug #3: Watchdog 假阳性误报 (稳定性)

### 现象

```
W (1622) wdog: ClockTask stall!
W (1622) wdog: NetworkTask stall!
W (1622) wdog: PowerTask stall!
```

每隔 2 秒刷一次，持续不断，但任务实际在正常运行。

### 根因

原始 watchdog 逻辑是"每 2 秒检查心跳计数是否变化"：

```c
if (g_heartbeat_clock != s_clk) s_clk = g_heartbeat_clock;
else ESP_LOGW(TAG, "ClockTask stall!");
```

但心跳计数放在循环**末尾**，而：

- ClockTask 在 `time_valid==false` 时 `continue` 跳过心跳
- NetworkTask 的 `wifi_connect()` 阻塞 15 秒，后续 `vTaskDelay` 900 秒
- PowerTask 的 `vTaskDelayUntil` 30 秒

导致任务每轮循环中大部分时间在睡眠，watchdog 必然误报。

### 修复（两阶段）

**阶段 1**: 心跳移到循环开头，长延迟拆分为小块：

```c
// PowerTask: 30s 拆成 30×1s
for (int i = 0; i < POWER_INTERVAL_S; i++) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    g_heartbeat_power++;
}
```

**阶段 2**: Watchdog 从"每 2s 检查变化"改为"时间窗口检测"：

```c
// 只在距离上次心跳变化超过 WDT_TIMEOUT 才报警
if (g_heartbeat_clock != s_clk) { s_clk = g_heartbeat_clock; t_clk = now; }
else if ((now - t_clk) > pdMS_TO_TICKS(WDT_TIMEOUT_S * 1000))
    ESP_LOGW(TAG, "ClockTask stall!");
```

- `WDT_TIMEOUT_S`: 10 → 60 秒

---

## Bug #4: OLED 黑屏 — I2C Buffer 为 0

### 现象

程序正常运行、无 crash、无 stall，但屏幕完全不显示。

### 根因

`oled_init()` 中 `i2c_driver_install(I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0)`。

旧版 ESP-IDF 对 tx/rx buffer 传 0 会分配默认大小的 buffer。v5.4 的 legacy I2C 驱动中传 0 真的不给 buffer，I2C 传输静默失败，OLED 收不到任何命令。

### 修复

```diff
- i2c_driver_install(I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0);
+ i2c_driver_install(I2C_MASTER_PORT, I2C_MODE_MASTER, 128, 128, 0);
```

---

## Bug #5: HTTPS TLS 连接失败

### 现象

```
E esp-tls-mbedtls: No server verification option set
E esp-tls-mbedtls: Failed to set client configurations, returned [0x8017]
E HTTP_CLIENT: Connection failed, sock < 0
```

WiFi 已连接、IP 已获取，但所有 HTTPS 请求失败。

### 根因

ESP-IDF v5.4 的 mbedtls TLS 客户端强制要求证书验证配置。旧代码只设置了 `.url` 和 `.event_handler`，没有提供任何证书验证方式。

### 修复

```c
// 1. CMakeLists.txt REQUIRES 加 mbedtls
// 2. http_client.c:
#include "esp_crt_bundle.h"

esp_http_client_config_t cfg = {
    .url = url,
    .event_handler = handler,
    .timeout_ms = 10000,
    .crt_bundle_attach = esp_crt_bundle_attach,  // 使用 IDF 内置 Mozilla CA 证书包
};
```

`sdkconfig.defaults` 需确保：
```
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
```

---

## Bug #6: 时间比实际早 8 小时

### 现象

NTP 同步成功，但显示时间比北京时间慢 8 小时。

### 根因

`time()` 返回 UTC 时间，`clock_set_time()` 直接存入秒计数器，没有加时区偏移。

### 修复

```c
// config.h:
#define TZ_OFFSET_SECONDS (8 * 3600)  /* UTC+8 */

// clock_task.c:
s_utc = ts + TZ_OFFSET_SECONDS;
```

---

## Bug #7: CMake 缓存不刷新

### 现象

修改了代码，编译时 ELF SHA256 不变，新代码没编进去。

### 根因

项目从桌面移到 C 盘，CMake 缓存中文件时间戳混乱，增量编译判断所有文件未变更，复用旧 .o 文件。

### 修复

```cmd
rd /s /q build
idf.py build flash monitor
```

删 build 目录重编译即可。

---

## Bug #8: 并行编译 OOM

### 现象

```
cc1plus.exe: out of memory allocating 65536 bytes
```

全量编译 1000+ 文件时并行数太高，内存耗尽。

### 修复

```cmd
idf.py -j2 build flash monitor
```

限制并行编译数为 2。

---

## Bug #9: 城市名被 IP 定位覆盖

### 现象

配置文件写的是 "bengbu"，但屏幕显示 "anshan" 或其他城市。

### 根因

`network_task.c` 调用 `get_loc()` 通过 `ipapi.co` 做 IP 地理定位，返回的城市名覆盖了 `DEFAULT_CITY`。而且 `ipapi.co` 经常超时/被墙，每次返回不同城市。

### 修复

删除 `get_loc()` 函数和 IP 定位逻辑，城市和坐标完全由 `DEFAULT_CITY`/`DEFAULT_LAT`/`DEFAULT_LON` 决定：

```c
strncpy(g_display_data.city, DEFAULT_CITY, 31);
get_weather(DEFAULT_LAT, DEFAULT_LON);
```

---

## Bug #10: 布局重叠 & 中文乱码

### 现象

- 时间 "13:00%" — 电池的 `%` 与时间数字重叠
- 第三行中文字符显示为乱码方块
- 底部 ASCII 文字画在中文上方，互相覆盖

### 根因

1. 4 行布局的 Y 坐标分配不合理，温度 (y=28) 和中文 (y=48) 之间有空隙，底部 (y=56) 溢出屏幕
2. 字库 `font_chinese_16x16[]` 中 16×16 位图数据不正确
3. 湿度+体感文字 (y=48, 16px高) 和天气中文 (y=48, 16px高) 完全重叠

### 修复

改用精确 4 行布局 (y=0/16/32/48)，每行 16px：

```
y=0:  [WiFi]     13:13        100
y=16: bengbu
y=32: 22C  [天气图标]  Rain
y=48:   H:94% F:25C
```

中文改为 16×16 天气图标 + 英文标签，不依赖破损字库。

---

## 完整修复文件清单

| 文件 | 涉及 Bug |
|------|---------|
| `main/shared/display_data.h` | #1 Type 混淆 |
| `main/shared/config.h` | #3 WDT、#6 TZ、#9 城市 |
| `main/main.c` | #1 Queue→TaskHandle |
| `main/tasks/clock_task.c` | #1 Notify、#3 心跳、#6 时区 |
| `main/tasks/display_task.c` | #10 布局 |
| `main/tasks/network_task.c` | #1 Notify、#3 心跳、#9 删除 IP 定位 |
| `main/tasks/power_task.c` | #1 Notify、#3 心跳+拆分 |
| `main/tasks/watchdog_task.c` | #2 栈、#3 时间窗口 |
| `main/drivers/oled_ssd1306.c` | #4 I2C buffer |
| `main/network/http_client.c` | #5 TLS 证书 |
| `main/network/wifi.c` | 重复初始化修复 |
| `main/CMakeLists.txt` | #5 mbedtls 依赖 |
| `sdkconfig.defaults` | #5 证书包配置 |

---

## 教训

1. **ESP-IDF v5.4 比旧版本严格**: 类型安全 (QueueHandle≠TaskHandle)、buffer 不能为 0、TLS 必须配证书——旧代码靠"巧合"能跑在新版不行
2. **移动项目要删 build**: CMake 缓存路径硬编码，时间戳混乱导致增量编译失败
3. **看门狗检测逻辑要匹配任务周期**: 长周期任务不能用"每次检查都要变化"的规则
4. **OLED 布局要精确计算 Y 坐标**: 128×64 像素空间有限，每像素都要规划好
