# 星愿塔罗 esp32-c3-tarot

ESP32-C3 SuperMini + 1.50 寸 240×280 SPI 彩屏（GC9306 / GC9307）做的三张牌塔罗占卜，基于 **ESP-IDF v6.1**。
解读以财运、好运等积极方向为主，仅供娱乐。

![preview](art/preview.png)

预览图从左到右：开机画面 / 翻牌中的牌桌 / 部分牌面。

## 玩法

| 画面 | 单击 BOOT | 长按 BOOT 1 秒 |
|---|---|---|
| 开机 logo | 开始占卜 | — |
| 等待翻牌 | 立刻翻开下一张 | 回到开机画面 |
| 解读 | 再占一次 | 回到开机画面 |

1. 开机显示 logo，底部「按 BOOT 开始占卜」按钮呼吸闪烁，水晶球周围有星光闪烁。
2. 单击后洗牌：牌堆展开成扇形 → 随机交换位置洗 5 轮 → 收拢 → 发出 3 张背面朝上的牌（过去 / 现在 / 未来），
   剩下的牌沉下淡出。
3. 每隔 **5 秒**翻开一张（翻牌动画 + 金色光晕），下方面板显示倒计时和已翻开的牌名、关键词。
4. 三张都翻开后，面板循环滚动完整解读：每张牌的牌义，再加「综合运势」——财运 / 事业 / 感情 / 整体星级、
   一句结语，以及随机的幸运色、幸运数字和开运建议。

牌组是 22 张大阿卡纳，全部按正位、积极方向解读。全部牌义见 [`art/readings.txt`](art/readings.txt)。

## 硬件与接线

与 [esp32-c3-muyu](https://github.com/tomdiynew/esp32-c3-muyu) 相同：ESP32-C3 SuperMini（BOOT 键 = GPIO9）+
FPC-1502401（GC9306）或 FPC-1502403（GC9307），开机自动识别。引脚在 [`main/board.h`](main/board.h)。

| 屏 FPC 引脚 | 信号 | ESP32-C3 |
|---|---|---|
| 1 | LCD_RES | GPIO4 |
| 2, 21 | GND | GND |
| 4 | LCD_CS | GPIO10 |
| 5 | LCD_SCL | GPIO6 |
| 7 | LCD_SDA | GPIO7 |
| 24 | LCD_DC | GPIO5 |
| 8, 15, 16 | LCD_3.3V | 3V3 |
| 11, 12 | LED_A | 3V3 经 10Ω |
| 13, 14 | LED_K | GND（或 N-MOSFET，栅极接 GPIO3） |

## 编译和烧录

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
git clone https://github.com/tomdiynew/esp32-c3-tarot.git
cd esp32-c3-tarot
idf.py set-target esp32c3        # 首次
idf.py build
idf.py -p COMx flash monitor
```

生成好的图片和字库都已入库，直接编译即可。

## 资源占用

| | 大小 |
|---|---|
| 固件 | 约 1.2 MB（程序分区 3.9 MB，剩 71%） |
| logo + 牌桌背景 | 2 × 134 KB（Flash） |
| 精灵图（牌背、22 张牌面 68×112、光晕、按钮、标题、星光） | 约 600 KB（Flash） |
| 中文字库（雅黑 16 px 正文 575 字 + 17 px 粗体标题 221 字，只含用到的字） | 约 84 KB（Flash） |
| RAM | 静态约 64 KB + 两块 19 KB 送屏缓冲，不需要整帧缓冲 |

## 美术与文字

- [`tools/make_art.py`](tools/make_art.py)：logo 缩放（源图 `art/logo_src.png`）、牌桌背景、牌背、22 张牌面
  （每张一个象征图案）、光晕、按钮等，全部用 SVG 画，无头 Edge/Chrome 渲染成带 alpha 的精灵图。
- [`tools/make_text.py`](tools/make_text.py)：牌义、综合运势结语、幸运色 / 建议列表和界面文字，
  生成 `main/tarot_text.h`（文字 + 只含用到的字的字库），同时输出 `art/readings.txt` 方便校对。

修改后重新生成（需要 Windows + Python、Pillow，以及 Edge 或 Chrome）：

```powershell
python tools\make_art.py --preview art\preview.png
python tools\make_text.py
idf.py build
```

## 工程结构

```
main/
  main.c        状态机（logo → 洗牌发牌 → 翻牌 → 解读）、精灵 / 翻牌缩放绘制、UTF-8 中文排版与滚动
  lcd.c         SPI + GC9306/9307 初始化、自动识别
  button.c      BOOT 键事件
  board.h       引脚
  sprites.h     生成的精灵表和布局常量
  tarot_text.h  生成的牌义与字库
  art/          生成的图片资源
components/esp_lcd_gc9306/   屏驱动组件
tools/          资源生成脚本
art/            logo 源图、预览图、牌义清单
```
