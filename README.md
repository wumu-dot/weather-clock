# Weather Clock - ESP32 OLED 天气时钟

基于 ESP32 + SSD1306 OLED (128×64 I2C) 的桌面天气时钟，实时显示时间、天气、温湿度信息。

```
┌──────────────────────┐
│ [WiFi]  13:13    100 │  ← 状态栏: WiFi + 时间 + 电池
│ bengbu               │  ← 城市名
│ 22C  [☁]  Cloud      │  ← 气温 + 天气图标 + 天气
│  H:94% F:25C         │  ← 湿度 + 体感温度
└──────────────────────┘
```

## 功能

| 功能 | 说明 |
|------|------|
| NTP 时间同步 | 开机自动同步北京时间 (UTC+8) |
| 实时天气 | open-meteo.com 免费 API，每 15 分钟更新 |
| 天气图标 | 内置晴/多云/雨/雪 16×16 位图图标 |
| WiFi 自动重连 | 断线自动重连 |
| 看门狗 | 60s 超时，时间窗口检测防假阳性 |
| 电池检测 | ADC 分压检测（可选，需外接电池） |

## 硬件需求

| 物料 | 规格 |
|------|------|
| 主控 | ESP32 (任何开发板) |
| 屏幕 | 0.96寸 SSD1306 OLED, 128×64, I2C |
| 接口 | SDA→GPIO21, SCL→GPIO22, VCC→3.3V, GND→GND |

## 快速开始

### 1. 配置 WiFi 和城市

编辑 `main/shared/config.h`:

```c
#define WIFI_SSID           "你的WiFi名"
#define WIFI_PASSWORD       "你的WiFi密码"
#define DEFAULT_CITY        "你的城市"
#define DEFAULT_LAT         32.9408   /* 蚌埠纬度 */
#define DEFAULT_LON         117.3608  /* 蚌埠经度 */
```

### 2. 编译烧录

```cmd
set IDF_PATH=C:\Espressif\v5.4\esp-idf
idf.py build flash monitor
```

## 项目结构

```
weather_clock/
├── CMakeLists.txt
├── sdkconfig.defaults         # mbedtls 证书包配置
├── main/
│   ├── main.c                 # 入口, 全局变量, 任务创建
│   ├── CMakeLists.txt
│   ├── shared/
│   │   ├── config.h           # 所有可配置参数
│   │   └── display_data.h     # 共享数据结构 + 信号量声明
│   ├── tasks/
│   │   ├── clock_task.c       # 时间走时 (1秒)
│   │   ├── display_task.c     # OLED 渲染
│   │   ├── network_task.c     # WiFi + NTP + 天气拉取
│   │   ├── power_task.c       # 电池 ADC 采样
│   │   └── watchdog_task.c    # 看门狗监控
│   ├── drivers/
│   │   ├── oled_ssd1306.c     # SSD1306 I2C 驱动
│   │   └── oled_ssd1306.h
│   ├── network/
│   │   ├── wifi.c             # WiFi 连接管理
│   │   ├── wifi.h
│   │   ├── http_client.c      # HTTPS GET + 证书验证
│   │   └── http_client.h
│   └── fonts/
│       ├── font_8x16.c        # ASCII 8×16 + 中文 16×16 + 图标
│       └── font_8x16.h
├── weather_clock/             # 原始代码 (迁移前有 bug 的版本)
│   └── main/...
├── BUGFIX.md                  # Bug 修复记录
├── LICENSE                    # MIT
└── README.md
```

## 依赖

| 组件 | 用途 |
|------|------|
| `esp_http_client` | HTTP/HTTPS 请求 |
| `mbedtls` | TLS 证书验证 (crt_bundle) |
| `esp_wifi` | WiFi 连接 |
| `esp_netif` + `nvs_flash` | 网络栈 + 非易失存储 |
| `json` (cJSON) | 天气 API JSON 解析 |
| `driver` (legacy I2C) | SSD1306 I2C 通信 |
| `esp_adc` | 电池电压采样 |

## 天气 API

使用 [open-meteo.com](https://open-meteo.com) 免费天气 API，无需注册、无 API Key。

如需修改城市坐标，更新 `config.h` 中 `DEFAULT_LAT`/`DEFAULT_LON` 即可。

## License

MIT
