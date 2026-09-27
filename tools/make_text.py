"""Generate main/tarot_text.h: the 22 Major Arcana readings (upright, positive: fortune, wealth, luck),
the summary templates, and two anti-aliased Microsoft YaHei fonts holding just the characters used.

Fonts: 4-bit coverage (2 px per byte, high nibble first), glyphs cropped to their ink box and looked up by
code point (sorted table, binary search in C). Also writes art/readings.txt for proofreading.

    python tools/make_text.py
"""
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "main" / "tarot_text.h"
TXT = ROOT / "art" / "readings.txt"

FONTS = [  # C name, file, px
    ("font_body", r"C:\Windows\Fonts\msyh.ttc", 16),
    ("font_head", r"C:\Windows\Fonts\msyhbd.ttc", 17),
]

# numeral, name, English, keyword, (wealth, career, love, luck) 1..5, reading
CARDS = [
    ("0", "愚者", "The Fool", "新的开始", (4, 3, 4, 5),
     "勇敢迈出第一步，意想不到的机会正在向你招手。轻装上阵，好运会一路相随。"),
    ("I", "魔术师", "The Magician", "心想事成", (5, 5, 3, 4),
     "成功所需的资源已握在你手中，灵感与行动力兼备，正是开创财路的好时机。"),
    ("II", "女祭司", "The High Priestess", "直觉敏锐", (3, 4, 4, 4),
     "相信内心的声音，直觉会带你避开风险、抓住暗藏的机遇，静待花开。"),
    ("III", "皇后", "The Empress", "丰盛富足", (5, 3, 5, 4),
     "财富与幸福如花园般蓬勃生长，付出的心血将迎来丰厚回报，生活富足美好。"),
    ("IV", "皇帝", "The Emperor", "稳固掌控", (4, 5, 3, 4),
     "你拥有掌控全局的能力，事业根基稳固，贵人赏识，地位与收入稳步提升。"),
    ("V", "教皇", "The Hierophant", "贵人指引", (3, 4, 4, 4),
     "前辈与贵人将给予关键指点，遵循正道稳扎稳打，福气自然水到渠成。"),
    ("VI", "恋人", "The Lovers", "美好结合", (4, 3, 5, 4),
     "良缘与好的合作一同到来，做出顺从本心的选择，人和财旺，双喜临门。"),
    ("VII", "战车", "The Chariot", "勇往直前", (4, 5, 3, 5),
     "凭借坚定的意志冲破阻碍，你将在竞争中脱颖而出，胜利与荣誉近在眼前。"),
    ("VIII", "力量", "Strength", "内在力量", (4, 4, 4, 4),
     "温柔而坚定的力量让你化解难题，耐心经营终有所成，好运稳稳站在你这边。"),
    ("IX", "隐士", "The Hermit", "智慧之光", (3, 4, 3, 4),
     "沉淀与思考让你看清方向，内在的智慧如明灯指路，下一步将更加从容笃定。"),
    ("X", "命运之轮", "Wheel of Fortune", "时来运转", (5, 4, 3, 5),
     "命运之轮正向你转动，转机与好运接连而来。把握当下，财运节节高升。"),
    ("XI", "正义", "Justice", "公平回报", (4, 4, 4, 4),
     "你的努力将得到公正的回报，签约、谈判顺利，付出与收获成正比。"),
    ("XII", "倒吊人", "The Hanged Man", "转换视角", (3, 3, 4, 4),
     "换个角度看问题，会发现新的出路；短暂的等待是在为更大的收获蓄力。"),
    ("XIII", "死神", "Death", "破旧立新", (3, 4, 3, 4),
     "旧的阶段圆满结束，崭新的篇章即将开启。放下过去，迎接脱胎换骨的蜕变。"),
    ("XIV", "节制", "Temperance", "平衡和谐", (4, 3, 4, 4),
     "张弛有度，收支平衡，身心和谐，细水长流的好运正在慢慢积累。"),
    ("XV", "恶魔", "The Devil", "突破束缚", (5, 3, 3, 3),
     "看清诱惑与束缚，你将挣脱限制、释放潜能，把欲望化作前进的动力，收获满满。"),
    ("XVI", "高塔", "The Tower", "突破重生", (3, 4, 3, 4),
     "意外的变化打破僵局，推倒旧墙才能建起新楼，惊喜往往藏在变化之中。"),
    ("XVII", "星星", "The Star", "希望之光", (4, 4, 4, 5),
     "希望之星为你闪耀，心中的愿望有望实现，灵感充沛，前途一片光明。"),
    ("XVIII", "月亮", "The Moon", "灵感涌现", (4, 3, 4, 4),
     "丰富的想象力带来创意与灵感，跟随直觉穿过迷雾，意外之财可能悄然而至。"),
    ("XIX", "太阳", "The Sun", "成功喜悦", (5, 5, 5, 5),
     "阳光普照，万事顺遂！事业与财运双双闪耀，喜悦与成功都属于你。"),
    ("XX", "审判", "Judgement", "觉醒新生", (4, 5, 4, 4),
     "好消息即将传来，过去的付出获得认可，一次重要的升级机会就在眼前。"),
    ("XXI", "世界", "The World", "圆满成就", (5, 5, 5, 5),
     "完美的圆满：目标达成、心愿成真，世界正向你敞开更大的舞台。"),
]

POSITIONS = ["过去", "现在", "未来"]
DIMENSIONS = ["财运", "事业", "感情", "整体"]
# closing line by the strongest dimension
CLOSING = [
    "财星高照，正财稳健、偏财有喜，适合规划理财，钱包会越来越鼓。",
    "事业运强劲，贵人相助，大胆展示才华，升职加薪指日可待。",
    "人缘桃花俱旺，真诚待人会收获温暖的关系与意想不到的助力。",
    "整体运势上扬，心想事成，保持积极心态，好运会接连不断。",
]
LUCKY_COLORS = ["金色", "红色", "紫色", "天蓝色", "翠绿色", "橙色", "白色", "粉色"]
LUCKY_TIPS = ["早起晒晒太阳", "整理一下钱包", "对遇到的人微笑", "穿一件亮色衣服",
              "喝一杯热茶", "写下三个愿望", "给老朋友发个问候", "把书桌收拾整洁"]

# fixed UI strings used by main.c (everything here goes into the fonts)
UI = {
    "title": "星愿塔罗",
    "shuffle": "正在洗牌……",
    "focus": "静心默念你的问题",
    "reveal_in": "秒后翻开",           # "<位置>之牌 · N 秒后翻开"
    "card_of": "之牌",
    "summary": "综合运势",
    "lucky_color": "幸运色：",
    "lucky_number": "幸运数字：",
    "lucky_tip": "开运建议：",
    "again": "单击再占一次 · 长按回首页",
    "skip": "单击可立即翻牌",
    "footer": "塔罗仅供娱乐，好运靠自己创造",
}
EXTRA = "★☆·…—：，。！【】（）、；0123456789"


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build_font(path, size, chars):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    pad = size
    glyphs, bits = [], bytearray()
    for ch in chars:
        img = Image.new("L", (size * 3, size * 3), 0)
        ImageDraw.Draw(img).text((pad, pad + ascent), ch, fill=255, font=font, anchor="ls")
        adv = round(font.getlength(ch))
        box = img.getbbox()
        if not box:
            glyphs.append((ord(ch), len(bits), 0, 0, 0, 0, adv))
            continue
        x0, y0, x1, y1 = box
        px = img.load()
        off = len(bits)
        for y in range(y0, y1):
            row = [min(15, (px[x, y] + 8) // 17) for x in range(x0, x1)]
            if len(row) % 2:
                row.append(0)
            bits.extend((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2))
        glyphs.append((ord(ch), off, x1 - x0, y1 - y0, x0 - pad, y0 - pad, adv))
    return ascent, descent, glyphs, bits


def main():
    text = "".join(c[1] + c[3] + c[5] for c in CARDS) + "".join(POSITIONS + DIMENSIONS + CLOSING + LUCKY_COLORS
                                                                  + LUCKY_TIPS + list(UI.values())) + EXTRA
    chars = sorted({c for c in text if ord(c) > 126} | {chr(c) for c in range(32, 127)})
    head_chars = sorted({c for c in "".join(c[1] + c[3] for c in CARDS) + "".join(POSITIONS) + UI["summary"]
                         + UI["title"] + "【】·—★☆"} | {chr(c) for c in range(32, 127)})

    L = ["/* Generated by tools/make_text.py - do not edit. */", "#pragma once", "", "#include <stdint.h>", "",
         "typedef struct {",
         "    const char* numeral;",
         "    const char* name;",
         "    const char* en;",
         "    const char* keyword;",
         "    uint8_t     score[4];  // wealth, career, love, luck: 1..5",
         "    const char* reading;",
         "} tarot_card_t;", "",
         f"#define TAROT_CARDS {len(CARDS)}",
         "static const tarot_card_t s_cards[TAROT_CARDS] = {"]
    for num, name, en, kw, sc, rd in CARDS:
        L.append(f"    {{{c_str(num)}, {c_str(name)}, {c_str(en)}, {c_str(kw)}, {{{', '.join(map(str, sc))}}}, {c_str(rd)}}},")
    L += ["};", ""]
    for name, items in (("s_positions", POSITIONS), ("s_dimensions", DIMENSIONS), ("s_closing", CLOSING),
                        ("s_lucky_colors", LUCKY_COLORS), ("s_lucky_tips", LUCKY_TIPS)):
        L.append(f"static const char* const {name}[{len(items)}] = {{{', '.join(c_str(s) for s in items)}}};")
    L.append("")
    for k, v in UI.items():
        L.append(f"#define TXT_{k.upper():<13} {c_str(v)}")
    L += ["", "typedef struct {",
          "    uint32_t off;  // into bits: rows of (w+1)/2 bytes, 4-bit coverage, high nibble first",
          "    uint8_t  w, h;",
          "    int8_t   x, y;  // ink box offset from the pen x / line top",
          "    uint8_t  adv;",
          "} cjk_glyph_t;", "",
          "typedef struct {",
          "    uint8_t            line_h, ascent;",
          "    uint16_t           count;",
          "    const uint16_t*    cps;     // sorted code points",
          "    const cjk_glyph_t* glyphs;",
          "    const uint8_t*     bits;",
          "} cjk_font_t;", ""]
    for (cname, path, size), cs in zip(FONTS, (chars, head_chars)):
        ascent, descent, glyphs, bits = build_font(path, size, cs)
        L.append(f"/* {pathlib.Path(path).name} {size} px: {len(glyphs)} glyphs, {len(bits)} bytes */")
        L.append(f"static const uint16_t {cname}_cps[{len(glyphs)}] = {{")
        for i in range(0, len(glyphs), 16):
            L.append("    " + ", ".join(f"0x{g[0]:04X}" for g in glyphs[i:i + 16]) + ",")
        L.append("};")
        L.append(f"static const uint8_t {cname}_bits[] = {{")
        for i in range(0, len(bits), 32):
            L.append("    " + ",".join(f"0x{b:02X}" for b in bits[i:i + 32]) + ",")
        L.append("};")
        L.append(f"static const cjk_glyph_t {cname}_glyphs[{len(glyphs)}] = {{")
        L += [f"    {{{off}, {w}, {h}, {x}, {y}, {adv}}},  // U+{cp:04X}" for cp, off, w, h, x, y, adv in glyphs]
        L.append("};")
        L.append(f"static const cjk_font_t {cname} = {{{ascent + descent + 2}, {ascent}, {len(glyphs)}, {cname}_cps, "
                 f"{cname}_glyphs, {cname}_bits}};")
        L.append("")
        print(f"{cname}: {len(glyphs)} glyphs, {len(bits)} bytes")
    OUT.write_text("\n".join(L), encoding="utf-8", newline="\n")

    lines = ["星愿塔罗 · 牌义（正位，积极解读）", ""]
    for num, name, en, kw, sc, rd in CARDS:
        lines += [f"{num:>5}  {name}（{en}）· {kw}   财运{sc[0]} 事业{sc[1]} 感情{sc[2]} 整体{sc[3]}", f"       {rd}", ""]
    lines += ["综合运势结语（按最高的一项选）："] + [f"  {d}：{c}" for d, c in zip(DIMENSIONS, CLOSING)]
    lines += ["", "幸运色：" + "、".join(LUCKY_COLORS), "开运建议：" + "、".join(LUCKY_TIPS), ""]
    TXT.write_text("\n".join(lines), encoding="utf-8-sig")


if __name__ == "__main__":
    main()
