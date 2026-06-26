# mp_hub75 — MicroPython HUB75 模組設計規格

## 1. 專案概述

基於 `esphome/esp-hub75` (`^0.3.6`) 的 MicroPython 原生 C 模組，在 ESP32-S3 / ESP32-P4 上驅動 HUB75 RGB LED 矩陣面板。

設計風格對標 `mp_jpeg`。

---

## 2. 專案結構

```
mp_hub75/
├── .gitignore
├── .github/
│   └── workflows/
│       └── ESP32.yml              # CI：多板子變體編譯驗證
├── CMakeLists.txt                  # ESP-IDF 組件註冊（stub）
├── idf_component.yml               # 依賴：esphome/esp-hub75: ^0.3.6
├── LICENSE                         # MIT
├── README.md
├── micropython.cmake               # MicroPython 模組構建腳本
├── examples/
│   └── benchmark.py                # 效能測試 / 使用範例
├── src/
│   ├── hub75_bridge.h     # C++ 橋接頭檔（C 可呼叫 API）
│   ├── hub75_bridge.cpp    # C++ 橋接實作（包裝 esp-hub75）
│   └── hub75_esp.c         # MicroPython 模組（純 C）
```

---

## 3. 依賴

```yaml
# idf_component.yml
dependencies:
  esphome/esp-hub75:
    version: "^0.3.6"
```

---

## 4. Python API

### 4.1 匯入

```python
import hub75
```

### 4.2 Display 類別

```python
display = hub75.Display(
    width=64,                          # 面板寬度
    height=64,                         # 面板高度
    pins=hub75.Pins(
        r1=25, g1=26, b1=27,           # 上半 RGB 資料
        r2=14, g2=12, b2=13,           # 下半 RGB 資料
        a=23, b=19, c=5, d=17, e=-1,   # 行地址（e 僅 1/32 scan 需要）
        lat=4, oe=15, clk=16           # 控制訊號
    ),
    pixel_format=hub75.RGB888,   # RGB888 / RGB565 / RGBX8888
    driver=hub75.GENERIC,             # GENERIC / FM6124 / FM6126A / ICN2038S / MBI5124 / DP3246
    scan=hub75.MOD8,                  # MOD8(1/16) / MOD16(1/32)
    brightness=128,                   # 0-255
    rotation=hub75.ROTATE_0,           # ROTATE_0 / ROTATE_90 / ROTATE_180 / ROTATE_270
    clock_speed=hub75.CLK_20M,        # 時脈（影響刷新率）
)
```

### 4.3 方法

| 方法 | 說明 |
|---|---|
| `display.begin()` | 初始化硬體，分配 DMA 緩衝，啟動循環刷新。失敗拋出 `MemoryError` |
| `display.show(buf)` | 將 RGB888/RGB565/RGBX8888 buffer 轉換為位平面格式，寫入 DMA active buffer，原子交換 front ↔ active |
| `display.end()` | 停止 DMA，釋放資源 |
| `display.set_brightness(v)` | 設定亮度 0-255 |
| `display.set_rotation(r)` | 設定旋轉 `hub75.ROTATE_0` / `ROTATE_90` / `ROTATE_180` / `ROTATE_270` |
| `display.width` | 屬性：顯示寬度（含旋轉） |
| `display.height` | 屬性：顯示高度（含旋轉） |
| `display.refresh_rate` | 屬性：實際刷新率 Hz（唯讀，`begin()` 後可用） |
| `display.buffer_info()` | 返回內部記憶體用量 dict：`{'dma_per_buffer': ..., 'dma_total': ..., 'overhead': ..., 'total': ...}` |
| `display.is_running()` | 返回是否正在運行 |

### 4.4 Context Manager

```python
with hub75.Display(...) as display:
    display.begin()
    display.show(buf)
# 自動呼叫 end()
```

### 4.5 模組級函數

```python
hub75.version()  # 返回 "esphome/esp-hub75 x.y.z"
```

---

## 5. 參數說明

### 5.0 必填 vs 選填

| 參數 | 必填？ | 預設值 | 說明 |
|---|---|---|---|
| `width` | 是 | — | 面板像素寬度 |
| `height` | 是 | — | 面板像素高度 |
| `pins` | 是 | — | GPIO pin 映射 |
| `pixel_format` | 否 | `RGB888` | 用戶 buffer 的像素格式 |
| `driver` | 否 | `GENERIC` | 面板驅動晶片型號 |
| `scan` | 否 | `MOD8` | 面板掃描模式 |
| `brightness` | 否 | `128` | 初始亮度 0-255 |
| `rotation` | 否 | `DEG_0` | 畫面旋轉 |
| `clock_speed` | 否 | `HZ_20M` | 像素輸出時脈（直接影響刷新率） |

最簡單的初始化（全部預設）：

```python
display = hub75.Display(
    width=64, height=64,
    pins=hub75.Pins(r1=25, g1=26, b1=27, r2=14, g2=12, b2=13,
                    a=23, b=19, c=5, d=17, e=-1, lat=4, oe=15, clk=16),
)
```

### 5.1 clock_speed — 像素輸出時脈

LCD_CAM 外設輸出每個 HUB75 像素（16-bit word）的頻率。這是**用戶唯一能控制刷新率的參數**。

```
時脈越高 → 刷新率越高，但訊號衰減風險越大
時脈越低 → 更穩定，抗干擾能力更好

實際刷新率 ≈ clock_speed ÷ (dma_width × 2^bit_depth × N_rows)
  64×64, 8-bit, 20MHz → 約 38 Hz
  64×64, 8-bit, 32MHz → 約 61 Hz

20 MHz → 預設，平衡速度和穩定性
10-16 MHz → 排線較長或訊號品質差時降低
8 MHz → 問題排查用
```

如果畫面出現雜訊或亂色點，先嘗試降低 clock_speed。
刷新率低於 60 Hz 時畫面可能閃爍，提高 clock_speed 或降低 bit_depth（編譯期）可改善。

---

## 6. 列舉型別速查表

以下值可在 MicroPython REPL 中透過 `help(hub75)` 或 `dir(hub75)` 查詢。

### 6.1 PixelFormat — 用戶 buffer 的像素格式

```python
import hub75
dir(hub75)  # RGB888, RGB565, RGBX8888
```

| 值 | bytes/px | 記憶體 (64x64) | 說明 | 適用場景 |
|---|---|---|---|---|
| `RGB888` | 3 | 12,288 B | 24-bit 全彩，R/G/B 各 8-bit | 預設，一般使用 |
| `RGB565` | 2 | 8,192 B | 16-bit 高彩，R5/G6/B5 | 節省 PSRAM / 傳輸頻寬 |
| `RGBX8888` | 4 | 16,384 B | 32-bit，末 byte 忽略 (X=padding) | 與 32-bit framebuffer 對接 |

### 6.2 Driver — 面板驅動晶片

```python
dir(hub75)  # GENERIC, FM6124, FM6126A, ICN2038S, MBI5124, DP3246
```

| 值 | 說明 |
|---|---|
| `GENERIC` | 標準 74HC595 移位暫存器，**大多數面板用這個** |
| `FM6124` | FM6124 晶片 |
| `FM6126A` | FM6126A / ICN2038S（現代面板常見） |
| `ICN2038S` | 同上，FM6126A 別名 |
| `MBI5124` | MBI5124（需要正時脈邊緣） |
| `DP3246` | DP3246 / SM5368（特殊時序） |

> **如何確認**：看面板 PCB 上的晶片型號。不確定的話先用 `GENERIC`，畫面異常再換。

### 6.3 Scan — 掃描模式

```python
dir(hub75)  # MOD8, MOD16
```

| 值 | 掃描率 | 行地址線 | 適用面板高度 |
|---|---|---|---|
| `MOD8` | 1/16 | A, B, C, D | **<=32 行**（64x32 面板） |
| `MOD16` | 1/32 | A, B, C, D, E | **64 行**（64x64 面板） |

> **判斷方法**：64x32 面板用 MOD8，64x64 面板用 MOD16。看面板有幾條 row address pin。

### 6.4 Rotation — 畫面旋轉

```python
dir(hub75)  # ROTATE_0, ROTATE_90, ROTATE_180, ROTATE_270
```

| 值 | 角度 | 效果 |
|---|---|---|
| `ROTATE_0` | 0度 | 不旋轉 |
| `ROTATE_90` | 90度 | 順時針 90度，width <-> height 交換 |
| `ROTATE_180` | 180度 | 上下左右顛倒 |
| `ROTATE_270` | 270度 | 逆時針 90度，width <-> height 交換 |

### 6.5 ClockSpeed — 像素時脈選項

```python
dir(hub75)  # CLK_8M, CLK_10M, ..., CLK_32M
```

| 值 | 實際頻率 |
|---|---|
| `CLK_8M` | 8 MHz |
| `CLK_10M` | 10 MHz |
| `CLK_16M` | 16 MHz |
| `CLK_18M` | 18 MHz |
| `CLK_20M` | **20 MHz（預設）** |
| `CLK_23M` | 23 MHz |
| `CLK_27M` | 27 MHz |
| `CLK_32M` | 32 MHz |

> 實際頻率由 PLL 160MHz / 整數除頻器得出。20MHz = 160/8。

---

## 7. 快速上手（第一次使用）

```python
import hub75

# 1. 基本設定（只填必需的）
display = hub75.Display(
    width=64, height=64,
    pins=hub75.Pins(
        r1=25, g1=26, b1=27, r2=14, g2=12, b2=13,
        a=23, b=19, c=5, d=17, e=-1,
        lat=4, oe=15, clk=16
    ),
    scan=hub75.MOD16,  # 64 行面板用 MOD16
)

# 2. 初始化
display.begin()

# 3. 準備 buffer
buf = bytearray(64 * 64 * 3)  # RGB888: width x height x 3

# 4. 畫圖（直接操作 buf）
for i in range(64):
    buf[i * 3] = 255      # R
    buf[i * 3 + 1] = 0    # G
    buf[i * 3 + 2] = 0    # B

# 5. 顯示
display.show(buf)
```

## 8. 內部運作流程

### 8.1 begin() 做的事

```
1. 驗證參數（width/height > 0, pins 有效）
2. 分配 DMA 雙緩衝：heap_caps_calloc(total_buffer_size, MALLOC_CAP_DMA) × 2
   → 失敗拋出 MemoryError("DMA buffer allocation failed: N bytes required")
3. 初始化 Hub75Driver(config).begin()
   → 設定 LCD_CAM + GDMA，建立描述符循環鏈
4. DMA 開始持續掃描 buffer[front] 到面板
```

### 8.2 show(buf) 做的事

```
1. 根據 pixel_format 解讀 buf
2. 逐行逐像素：RGB → LUT → 位平面格式
3. 寫入 dma_buffers_[active_idx_]
4. 原子交換：front_idx_ ↔ active_idx_
   → 下一輪 DMA 循環就顯示新內容
```

### 8.3 DMA 循環（硬體層，不經 CPU）

```
描述符鏈：
  desc_row0_bit0 → desc_row0_bit1 → ... → desc_row0_bit7
  → desc_row1_bit0 → ... → desc_rowN_bit7
  → 回到 desc_row0_bit0 （循環）

每個 desc = dma_width_ × 2 bytes（uint16_t HUB75 原生格式）

位平面 uint16_t 佈局：
  [15-13:--] [12:OE] [11:LAT] [10-6:ADDR] [5:R2] [4:G2] [3:B2] [2:R1] [1:G1] [0:B1]
```

---

## 9. 記憶體用量公式（8-bit depth, standard scan, 雙緩衝）

```
dma_width = panel_width × layout_cols
N_rows = panel_height ÷ 2
B = bit_depth = 8

per_buffer = N_rows × dma_width × B × 2
descriptors = N_rows × B × 12 × 2
metadata = N_rows × 16 × 2

total = per_buffer × 2 + descriptors + metadata
```

| 面板 | per_buffer | total |
|---|---|---|
| 64×32 | 16,384 | ~36 KB |
| 64×64 | 32,768 | ~72 KB |
| 128×64 (×2) | 65,536 | ~145 KB |
| 256×64 (×4) | 131,072 | ~291 KB |

全部從 `MALLOC_CAP_DMA`（內部 SRAM）分配，不可用 PSRAM。

---

## 10. show(buf) 對不同 pixel_format 的處理

```
RGB888:   buf[i*3+0]=R, buf[i*3+1]=G, buf[i*3+2]=B
          → LUT[R], LUT[G], LUT[B] → 寫入位平面

RGB565:   pixel = buf[i*2] | (buf[i*2+1] << 8)
          → R = (pixel >> 11) & 0x1F, 展開到 8-bit
          → G = (pixel >> 5) & 0x3F,  展開到 8-bit
          → B = pixel & 0x1F,          展開到 8-bit
          → LUT → 寫入位平面

RGBX8888: buf[i*4+0]=R, buf[i*4+1]=G, buf[i*4+2]=B, buf[i*4+3]=X(忽略)
          → LUT[R], LUT[G], LUT[B] → 寫入位平面
```

---

## 11. C 實作方案

採用 `.c` + `extern "C"` 橋接層：

- `hub75_esp.c`：MicroPython 綁定（純 C，對標 mp_jpeg 風格）
- 編譯時連結 `esphome/esp-hub75` C++ 庫
- 在 `.c` 中使用 `extern "C"` 宣告 `Hub75Driver` 的包裝函數，由 C++ 編譯器處理連結

---

## 12. CI/CD

- GitHub Actions：對 ESP32-S3、ESP32-P4 變體編譯驗證
- 觸發條件：push 到 `src/**` 或 workflow 檔案
- 參考 `mp_jpeg` 的 `ESP32.yml`

---

## 13. 編譯期設定（micropython.cmake）

```cmake
# 預設值（用戶可覆蓋）
# -DHUB75_BIT_DEPTH=8       # 4-12
# -DHUB75_GAMMA_MODE=1      # 0=LINEAR, 1=CIE1931, 2=GAMMA_2_2
# -DHUB75_CIE_SHIFT=8
```
