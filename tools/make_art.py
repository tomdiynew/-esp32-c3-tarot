"""Draw the tarot art as SVG, render it with headless Edge/Chrome and pack it for the firmware.

Screen sprites are drawn in screen coordinates (viewBox 0 0 240 280); cards are drawn at (0, 0) and placed
by the C code. One page, one transparent screenshot -> anti-aliased sprites with real alpha.

Outputs:
    main/art/logo.rgb565   start screen: art/logo_src.png scaled to 240x280 (big-endian RGB565)
    main/art/bg.rgb565     table: starry background, title, card slots, position labels, text panel
    main/art/sprites.bin   sprites: w*h RGB565 (big-endian) followed by w*h alpha bytes, each
    main/sprites.h         sprite table + layout constants for the C code

    python tools/make_art.py [--preview art/preview.png]
"""
import argparse
import math
import os
import pathlib
import random
import subprocess
import tempfile

from PIL import Image

W, H = 240, 280
ROOT = pathlib.Path(__file__).resolve().parent.parent

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]

# ------------------------------------------------------------------------------------------------
# Layout shared with the C code (screen pixels)
# ------------------------------------------------------------------------------------------------
CARD_W, CARD_H = 68, 112
SLOT_X = (10, 86, 162)
SLOT_Y = 34
LABEL_Y = 157                                   # center of the 过去/现在/未来 labels
PANEL_X, PANEL_Y, PANEL_W, PANEL_H = 8, 170, 224, 104
BUTTON_CX, BUTTON_CY = 120, 256

GOLD = "#e8c46a"
FONT = "Microsoft YaHei"


# ------------------------------------------------------------------------------------------------
# Screen art
# ------------------------------------------------------------------------------------------------
def star4(cx, cy, r, fill="#fff3c4", opacity=1.0):
    k = r * 0.3
    return (f'<path d="M{cx} {cy - r} Q{cx + k} {cy - k} {cx + r} {cy} Q{cx + k} {cy + k} {cx} {cy + r} '
            f'Q{cx - k} {cy + k} {cx - r} {cy} Q{cx - k} {cy - k} {cx} {cy - r} Z" fill="{fill}" opacity="{opacity}"/>')


def background():
    rnd = random.Random(7)
    dots = "".join(f'<circle cx="{rnd.uniform(0, W):.1f}" cy="{rnd.uniform(0, H):.1f}" r="{rnd.choice((0.5, 0.7, 0.9, 1.2))}" '
                   f'fill="#fff" opacity="{rnd.uniform(0.25, 0.8):.2f}"/>' for _ in range(90))
    twinkles = "".join(star4(x, y, r, opacity=0.8) for x, y, r in ((22, 14, 4), (216, 18, 3.5), (200, 150, 3), (36, 150, 3)))
    rings = "".join(f'<circle cx="120" cy="92" r="{r}" fill="none" stroke="{GOLD}" stroke-width="0.8" opacity="{o}"/>'
                    for r, o in ((70, 0.18), (86, 0.12), (104, 0.08)))
    spokes = "".join(f'<line x1="{120 + 70 * math.cos(math.radians(a)):.1f}" y1="{92 + 70 * math.sin(math.radians(a)):.1f}" '
                     f'x2="{120 + 86 * math.cos(math.radians(a)):.1f}" y2="{92 + 86 * math.sin(math.radians(a)):.1f}" '
                     f'stroke="{GOLD}" stroke-width="0.8" opacity="0.15"/>' for a in range(0, 360, 15))
    slots = "".join(f'<rect x="{x + 0.5}" y="{SLOT_Y + 0.5}" width="{CARD_W - 1}" height="{CARD_H - 1}" rx="6" fill="#ffffff" '
                    f'fill-opacity="0.03" stroke="{GOLD}" stroke-width="1" stroke-dasharray="4 3" opacity="0.45"/>'
                    for x in SLOT_X)
    labels = "".join(f'<text x="{x + CARD_W / 2}" y="{LABEL_Y + 5}" text-anchor="middle" font-family="{FONT}" '
                     f'font-weight="bold" font-size="13" fill="#e2cc92">{t}</text>'
                     for x, t in zip(SLOT_X, ("过去", "现在", "未来")))
    return f"""
<defs>
  <linearGradient id="night" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#23124a"/><stop offset="0.6" stop-color="#140c33"/><stop offset="1" stop-color="#0a0820"/>
  </linearGradient>
  <linearGradient id="gold" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff0b8"/><stop offset="1" stop-color="#d9a441"/>
  </linearGradient>
</defs>
<rect width="{W}" height="{H}" fill="url(#night)"/>
{dots}{rings}{spokes}{twinkles}
<text x="120" y="23" text-anchor="middle" font-family="{FONT}" font-weight="bold" font-size="17" letter-spacing="3"
      fill="url(#gold)">星愿塔罗</text>
{star4(72, 17, 4, "#e8c46a")}{star4(168, 17, 4, "#e8c46a")}
{slots}{labels}
<rect x="{PANEL_X}" y="{PANEL_Y}" width="{PANEL_W}" height="{PANEL_H}" rx="10" fill="#0d0826" fill-opacity="0.88"
      stroke="{GOLD}" stroke-width="1.2" stroke-opacity="0.8"/>
"""


def start_button():
    w, h = 176, 34
    x, y = BUTTON_CX - w / 2, BUTTON_CY - h / 2
    return f"""
<defs><linearGradient id="btn" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#5a2fa0"/><stop offset="1" stop-color="#2a1458"/></linearGradient></defs>
<rect x="{x - 3}" y="{y - 3}" width="{w + 6}" height="{h + 6}" rx="{h / 2 + 3}" fill="{GOLD}" opacity="0.25"/>
<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h / 2}" fill="url(#btn)" stroke="{GOLD}" stroke-width="1.6"/>
<text x="{BUTTON_CX}" y="{BUTTON_CY + 5.5}" text-anchor="middle" font-family="{FONT}" font-weight="bold" font-size="15"
      fill="#ffeaa8">按 BOOT 开始占卜</text>
{star4(x + 16, BUTTON_CY, 4.5, "#ffeaa8")}{star4(x + w - 16, BUTTON_CY, 4.5, "#ffeaa8")}
"""


def logo_title():
    chars = "星愿塔罗"
    t = "".join(f'<text x="19" y="{44 + i * 30}" text-anchor="middle" font-family="{FONT}" font-weight="bold" font-size="22" '
                f'fill="url(#tg)" filter="url(#glow)">{c}</text>' for i, c in enumerate(chars))
    return f"""
<defs>
  <linearGradient id="tg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff3c4"/><stop offset="1" stop-color="#e0a940"/></linearGradient>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
    <feGaussianBlur in="SourceAlpha" stdDeviation="2.2" result="b"/><feFlood flood-color="#7a4cff" flood-opacity="0.9"/>
    <feComposite in2="b" operator="in" result="g"/><feMerge><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>
{t}{star4(19, 14, 4, "#ffeaa8")}{star4(19, 164, 4, "#ffeaa8")}
"""


def glow():
    cx, cy = SLOT_X[0] + CARD_W / 2, SLOT_Y + CARD_H / 2
    return f"""
<defs><radialGradient id="glw" cx="{cx}" cy="{cy}" r="72" gradientUnits="userSpaceOnUse">
  <stop offset="0.45" stop-color="#ffe7a0" stop-opacity="0.9"/><stop offset="1" stop-color="#ffb84d" stop-opacity="0"/>
</radialGradient></defs>
<ellipse cx="{cx}" cy="{cy}" rx="{CARD_W / 2 + 20}" ry="{CARD_H / 2 + 20}" fill="url(#glw)"/>
"""


def sparkle(r):
    return star4(20, 20, r, "#fff6d0")


# ------------------------------------------------------------------------------------------------
# Cards (drawn at 0,0)
# ------------------------------------------------------------------------------------------------
def card_back():
    rnd = random.Random(3)
    tiny = "".join(star4(rnd.uniform(9, CARD_W - 9), rnd.uniform(9, CARD_H - 9), rnd.choice((1.5, 2, 2.5)), GOLD, 0.45)
                   for _ in range(16))
    cx, cy = CARD_W / 2, CARD_H / 2
    return f"""
<defs>
  <linearGradient id="back" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#44227e"/><stop offset="1" stop-color="#1b0e44"/></linearGradient>
  <radialGradient id="ball" cx="{cx - 4}" cy="{cy - 5}" r="16" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="#e6f3ff"/><stop offset="0.5" stop-color="#7fb4ff"/><stop offset="1" stop-color="#3a4fb0"/></radialGradient>
</defs>
<rect x="0.8" y="0.8" width="{CARD_W - 1.6}" height="{CARD_H - 1.6}" rx="6" fill="url(#back)" stroke="{GOLD}" stroke-width="1.6"/>
<rect x="5" y="5" width="{CARD_W - 10}" height="{CARD_H - 10}" rx="3.5" fill="none" stroke="{GOLD}" stroke-width="0.8" opacity="0.8"/>
{tiny}
<circle cx="{cx}" cy="{cy}" r="21" fill="none" stroke="{GOLD}" stroke-width="0.8" opacity="0.7"/>
<circle cx="{cx}" cy="{cy}" r="13" fill="url(#ball)" stroke="{GOLD}" stroke-width="1.2"/>
{star4(cx, cy, 5, "#fff6d0")}
<path d="M{cx - 4} {cy - 31} a7 7 0 1 0 9 8 a5.5 5.5 0 1 1 -9 -8 Z" fill="{GOLD}"/>
<path d="M{cx - 9} {cy + 16} h18 l-3 5 h-12 Z" fill="{GOLD}"/>
"""


def heart(cx, cy, s, fill):
    return (f'<path d="M{cx} {cy + 7 * s} C{cx - 12 * s} {cy - 1 * s} {cx - 7 * s} {cy - 10 * s} {cx} {cy - 4 * s} '
            f'C{cx + 7 * s} {cy - 10 * s} {cx + 12 * s} {cy - 1 * s} {cx} {cy + 7 * s} Z" fill="{fill}" stroke="none"/>')


def star_poly(cx, cy, r, n=5, inner=0.45, fill="#ffe7a0"):
    pts = []
    for i in range(2 * n):
        rr = r if i % 2 == 0 else r * inner
        a = math.radians(-90 + i * 180 / n)
        pts.append(f"{cx + rr * math.cos(a):.1f},{cy + rr * math.sin(a):.1f}")
    return f'<polygon points="{" ".join(pts)}" fill="{fill}" stroke="none"/>'


L = "#ffe7a0"   # icon line colour
SUN = "#ffd34d"

ICONS = [
    # 0 The Fool: sun over the peaks, a bird taking off
    f'<path d="M-19 17 L-6 -1 L1 7 L8 0 L19 17 Z" fill="{L}" stroke="none"/><circle cx="9" cy="-11" r="6" fill="{SUN}" stroke="none"/>'
    f'<path d="M-15 -9 q3 -3 6 0 q3 -3 6 0"/>',
    # I The Magician: infinity above a wand
    f'<path d="M0 -11 C-4 -17 -14 -17 -14 -11 C-14 -5 -4 -5 0 -11 C4 -17 14 -17 14 -11 C14 -5 4 -5 0 -11 Z"/>'
    f'<line x1="0" y1="-1" x2="0" y2="18" stroke-width="3"/><circle cx="0" cy="-1" r="3" fill="#fff" stroke="none"/>',
    # II The High Priestess: two pillars, crescent moon
    f'<rect x="-18" y="-17" width="6" height="34" fill="#2a1a40" stroke="{L}" stroke-width="1"/>'
    f'<rect x="12" y="-17" width="6" height="34" fill="{L}" stroke="none"/>'
    f'<path d="M4 -12 a12 12 0 1 0 0 24 a15 15 0 0 1 0 -24 Z" fill="#fff" stroke="none"/>',
    # III The Empress: crown and wheat
    f'<path d="M-14 -3 L-14 -15 L-7 -8 L0 -17 L7 -8 L14 -15 L14 -3 Z" fill="{SUN}" stroke="none"/>'
    f'<line x1="0" y1="1" x2="0" y2="18"/>'
    + "".join(f'<ellipse cx="{s * 4}" cy="{y}" rx="2.2" ry="4" transform="rotate({s * 30} {s * 4} {y})" fill="{L}" stroke="none"/>'
              for y in (5, 11) for s in (-1, 1)),
    # IV The Emperor: throne with ram horns
    '<path d="M-11 18 V-5 H11 V18 M-11 5 H11" stroke-width="2.4"/>'
    '<path d="M-11 -5 c-7 -1 -8 -11 -1 -12 c5 0 5 6 0 6 M11 -5 c7 -1 8 -11 1 -12 c-5 0 -5 6 0 6"/>'
    f'<path d="M-5 -9 L-5 -15 L-2 -12 L0 -16 L2 -12 L5 -15 L5 -9 Z" fill="{SUN}" stroke="none"/>',
    # V The Hierophant: triple cross in a halo
    f'<circle cx="0" cy="-8" r="11" stroke-width="1" opacity="0.7"/>'
    '<path d="M0 -18 V18 M-6 -12 H6 M-8 -6 H8 M-10 0 H10" stroke-width="2.6"/>',
    # VI The Lovers: two hearts
    heart(-6, -2, 1.1, "#ff7a9a") + heart(7, 3, 1.0, L),
    # VII The Chariot: star over a wheel
    star_poly(0, -9, 9, fill=SUN) + '<circle cx="0" cy="10" r="8"/>'
    + "".join(f'<line x1="0" y1="10" x2="{8 * math.cos(math.radians(a)):.1f}" y2="{10 + 8 * math.sin(math.radians(a)):.1f}" stroke-width="1.2"/>'
              for a in range(0, 360, 60)),
    # VIII Strength: infinity above a lion's mane
    '<path d="M0 -15 C-3 -19 -9 -19 -9 -15 C-9 -11 -3 -11 0 -15 C3 -19 9 -19 9 -15 C9 -11 3 -11 0 -15 Z" stroke-width="1.5"/>'
    + "".join(f'<ellipse cx="{11 * math.cos(math.radians(a)):.1f}" cy="{5 + 11 * math.sin(math.radians(a)):.1f}" rx="4.5" ry="2.6" '
              f'transform="rotate({a} {11 * math.cos(math.radians(a)):.1f} {5 + 11 * math.sin(math.radians(a)):.1f})" fill="#ff9f40" stroke="none"/>'
              for a in range(0, 360, 30))
    + f'<circle cx="0" cy="5" r="8" fill="{SUN}" stroke="none"/>',
    # IX The Hermit: lantern with a star
    f'<path d="M0 -18 V-12"/><path d="M-7 -12 H7 L9 10 H-9 Z" fill="#3a2a10" stroke-width="1.8"/>{star_poly(0, -1, 6, 6, 0.4, "#fff3b0")}'
    '<path d="M-9 10 H9 M-5 14 H5"/>',
    # X Wheel of Fortune
    '<circle cx="0" cy="0" r="17" stroke-width="2.2"/><circle cx="0" cy="0" r="11" stroke-width="1"/>'
    f'<circle cx="0" cy="0" r="4" fill="{SUN}" stroke="none"/>'
    + "".join(f'<line x1="{4 * math.cos(math.radians(a)):.1f}" y1="{4 * math.sin(math.radians(a)):.1f}" '
              f'x2="{17 * math.cos(math.radians(a)):.1f}" y2="{17 * math.sin(math.radians(a)):.1f}" stroke-width="1.3"/>'
              for a in range(0, 360, 45)),
    # XI Justice: scales
    '<path d="M0 -17 V16 M-8 16 H8 M-15 -10 H15" stroke-width="2"/>'
    '<path d="M-15 -10 L-19 1 M-15 -10 L-11 1 M15 -10 L11 1 M15 -10 L19 1" stroke-width="0.9"/>'
    f'<path d="M-20 1 a5 3 0 0 0 10 0 Z M10 1 a5 3 0 0 0 10 0 Z" fill="{SUN}" stroke="none"/>',
    # XII The Hanged Man: tau cross, a halo hanging below
    '<path d="M-15 -16 H15 M0 -16 V0" stroke-width="2.6"/><path d="M0 0 V6"/>'
    f'<circle cx="0" cy="11" r="6" fill="{SUN}" stroke="none"/><circle cx="0" cy="11" r="9.5" stroke-width="1" opacity="0.7"/>',
    # XIII Death: a white rose (new beginning)
    "".join(f'<circle cx="{7 * math.cos(math.radians(a)):.1f}" cy="{7 * math.sin(math.radians(a)):.1f}" r="6.5" fill="#fffaf0" stroke="#d8c8a8" stroke-width="0.8"/>'
            for a in range(-90, 270, 72)) + f'<circle cx="0" cy="0" r="4" fill="{SUN}" stroke="none"/>',
    # XIV Temperance: water flowing between two cups
    f'<path d="M-17 2 H-5 L-7 14 H-15 Z M5 -12 H17 L15 0 H7 Z" fill="{L}" stroke="none"/>'
    '<path d="M-6 3 Q0 -12 8 -9" stroke="#8fd3ff" stroke-width="2.4" stroke-dasharray="3 2"/>',
    # XV The Devil: a broken chain
    '<rect x="-17" y="-6" width="14" height="9" rx="4.5" transform="rotate(-30 -10 -1)" stroke-width="2.4"/>'
    '<rect x="3" y="-1" width="14" height="9" rx="4.5" transform="rotate(-30 10 4)" stroke-width="2.4"/>'
    f'<path d="M-2 -12 L0 -6 M5 -13 L3 -7 M-1 12 L1 7" stroke="{SUN}" stroke-width="1.6"/>',
    # XVI The Tower: tower struck by lightning
    f'<path d="M-7 18 V-8 H7 V18 Z" fill="#6b4a2a" stroke-width="1.5"/><path d="M-9 -8 L-9 -14 L-5 -11 L0 -16 L5 -11 L9 -14 L9 -8 Z" fill="{SUN}" stroke="none"/>'
    f'<path d="M14 -18 L6 -4 H11 L3 8" stroke="#fff6a0" stroke-width="2.2"/><rect x="-2.5" y="0" width="5" height="7" fill="{SUN}" stroke="none"/>',
    # XVII The Star
    star_poly(0, -2, 16, 8, 0.42, "#fff3b0") + star_poly(-14, 14, 4, 5, 0.45, L) + star_poly(15, 13, 3.5, 5, 0.45, L)
    + star_poly(15, -15, 3, 5, 0.45, L),
    # XVIII The Moon: crescent and falling light
    f'<path d="M4 -17 a17 17 0 1 0 0 30 a13 13 0 1 1 0 -30 Z" fill="#fff6d8" stroke="none"/>'
    + "".join(f'<circle cx="{x}" cy="{y}" r="1.6" fill="{L}" stroke="none"/>' for x, y in ((12, -10), (15, 0), (10, 9))),
    # XIX The Sun
    f'<circle cx="0" cy="0" r="10" fill="{SUN}" stroke="none"/>'
    + "".join(f'<line x1="{13 * math.cos(math.radians(a)):.1f}" y1="{13 * math.sin(math.radians(a)):.1f}" '
              f'x2="{(19 if a % 60 == 0 else 17) * math.cos(math.radians(a)):.1f}" y2="{(19 if a % 60 == 0 else 17) * math.sin(math.radians(a)):.1f}" '
              f'stroke="{SUN}" stroke-width="2.4"/>' for a in range(0, 360, 30)),
    # XX Judgement: trumpet with rays
    f'<path d="M-17 6 L6 -2 L16 -12 L16 8 L6 2 Z" fill="{SUN}" stroke="none"/><path d="M-17 6 L-10 8" stroke-width="2"/>'
    '<path d="M-6 -10 L-9 -17 M1 -12 L1 -19 M8 -14 L12 -19" stroke-width="1.4"/>',
    # XXI The World: laurel wreath around a star
    "".join(f'<ellipse cx="{15 * math.cos(math.radians(a)):.1f}" cy="{15 * math.sin(math.radians(a)):.1f}" rx="4" ry="2" '
            f'transform="rotate({a + 60} {15 * math.cos(math.radians(a)):.1f} {15 * math.sin(math.radians(a)):.1f})" fill="#9fe08a" stroke="none"/>'
            for a in range(0, 360, 24)) + star_poly(0, 1, 9, 5, 0.45, SUN),
]

PANEL_COLORS = [  # (top, bottom) per card
    ("#6fb3ff", "#2b4d9a"), ("#b565ff", "#4b1f8a"), ("#3a5dbf", "#141d55"), ("#5fbf6a", "#1f6a33"),
    ("#e0583a", "#7a1f14"), ("#c9a04a", "#6a4a14"), ("#ff8fb0", "#a13a6a"), ("#4a8fd8", "#1a3a7a"),
    ("#ffb347", "#b35a14"), ("#5a5a8a", "#1c1c3c"), ("#9a6aff", "#3a1a8a"), ("#6ac0c0", "#1f5a6a"),
    ("#4ab0a0", "#14504a"), ("#8a8aa0", "#2a2a40"), ("#6ac8ff", "#2a6aa0"), ("#c04a6a", "#4a1428"),
    ("#6a5aa0", "#1a1440"), ("#3a6ad8", "#0e1a5a"), ("#4a4ab0", "#10103a"), ("#ffb030", "#d0601a"),
    ("#e07a5a", "#7a2a3a"), ("#5ab07a", "#1a4a5a"),
]


def card_face(i, numeral, name):
    top, bot = PANEL_COLORS[i]
    px, py, pw, ph = 5, 17, CARD_W - 10, 70
    return f"""
<defs>
  <linearGradient id="paper{i}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fbf0d2"/><stop offset="1" stop-color="#ecd7a4"/></linearGradient>
  <linearGradient id="panel{i}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bot}"/></linearGradient>
</defs>
<rect x="0.8" y="0.8" width="{CARD_W - 1.6}" height="{CARD_H - 1.6}" rx="6" fill="url(#paper{i})" stroke="#a87a2e" stroke-width="1.6"/>
<text x="{CARD_W / 2}" y="13" text-anchor="middle" font-family="Times New Roman" font-weight="bold" font-size="11" fill="#7a4a12">{numeral}</text>
<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="3" fill="url(#panel{i})" stroke="#a87a2e" stroke-width="1"/>
<g transform="translate({px + pw / 2} {py + ph / 2})" fill="none" stroke="{L}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  {ICONS[i]}
</g>
<line x1="10" y1="{py + ph + 4}" x2="{CARD_W - 10}" y2="{py + ph + 4}" stroke="#a87a2e" stroke-width="0.8"/>
<text x="{CARD_W / 2}" y="{py + ph + 18}" text-anchor="middle" font-family="{FONT}" font-weight="bold"
      font-size="{12 if len(name) < 4 else 11}" fill="#5a2a0a">{name}</text>
"""


NUMERALS = ["0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI",
            "XVII", "XVIII", "XIX", "XX", "XXI"]
NAMES = ["愚者", "魔术师", "女祭司", "皇后", "皇帝", "教皇", "恋人", "战车", "力量", "隐士", "命运之轮", "正义", "倒吊人",
         "死神", "节制", "恶魔", "高塔", "星星", "月亮", "太阳", "审判", "世界"]


def sprites():
    """(name, svg body) in the order of the C enum"""
    out = [("back", card_back()), ("glow", glow()), ("button", start_button()), ("logo_title", logo_title()),
           ("sparkle_big", sparkle(7)), ("sparkle_small", sparkle(4))]
    out += [(f"card_{i}", card_face(i, NUMERALS[i], NAMES[i])) for i in range(22)]
    return out


# ------------------------------------------------------------------------------------------------
# Rendering
# ------------------------------------------------------------------------------------------------
def render(cells, png_path, browser):
    cols = 6
    rows = (len(cells) + cols - 1) // cols
    divs = ""
    for i, body in enumerate(cells):
        x, y = (i % cols) * W, (i // cols) * H
        divs += (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="{W}" height="{H}" '
                 f'viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">{body}</svg>\n')
    html = (f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:transparent;'
            f'overflow:hidden}}</style></head><body>{divs}</body></html>')
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "art.html")
        with open(page, "w", encoding="utf-8") as f:
            f.write(html)
        subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                        f"--window-size={cols * W},{rows * H}", "--default-background-color=00000000",
                        f"--user-data-dir={os.path.join(tmp, 'profile')}", f"--screenshot={os.path.abspath(png_path)}",
                        "--virtual-time-budget=500", pathlib.Path(page).as_uri()],
                       check=True, capture_output=True, timeout=120)
    sheet = Image.open(png_path).convert("RGBA")
    return [sheet.crop(((i % cols) * W, (i // cols) * H, (i % cols + 1) * W, (i // cols + 1) * H))
            for i in range(len(cells))]


def rgb565_be(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return bytes((v >> 8, v & 0xFF))


def write_rgb565(img, path):
    raw = img.convert("RGB").tobytes()
    path.write_bytes(b"".join(rgb565_be(*raw[i:i + 3]) for i in range(0, len(raw), 3)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="save a composed preview PNG here")
    args = ap.parse_args()
    browser = next((b for b in BROWSERS if os.path.exists(b)), None)
    if not browser:
        raise SystemExit("Edge/Chrome not found")

    art = ROOT / "main" / "art"
    art.mkdir(exist_ok=True)
    logo = Image.open(ROOT / "art" / "logo_src.png").convert("RGB").resize((W, H), Image.LANCZOS)
    write_rgb565(logo, art / "logo.rgb565")

    spr = sprites()
    with tempfile.TemporaryDirectory() as tmp:
        imgs = render([background()] + [body for _, body in spr], os.path.join(tmp, "sheet.png"), browser)

    bg = Image.new("RGB", (W, H), (20, 12, 51))
    bg.paste(imgs[0], mask=imgs[0].split()[3])
    write_rgb565(bg, art / "bg.rgb565")

    blob, table = bytearray(), []
    for (name, _), im in zip(spr, imgs[1:]):
        box = im.split()[3].getbbox()
        crop = im.crop(box)
        w, h = crop.size
        raw = crop.tobytes()
        table.append((name, len(blob), w, h, box[0], box[1]))
        blob += b"".join(rgb565_be(*raw[i:i + 3]) for i in range(0, len(raw), 4))
        blob += raw[3::4]
    (art / "sprites.bin").write_bytes(blob)
    for name, _, w, h, x, y in table:
        if name.startswith("card_") or name == "back":
            assert (w, h, x, y) == (CARD_W, CARD_H, 0, 0), (name, w, h, x, y)

    enum = "\n".join(f"    SPR_{n.upper()}," for n, *_ in table)
    rows = "\n".join(f"    {{{off:7d}, {w:3d}, {h:3d}, {x:3d}, {y:3d}}},  // {n}" for n, off, w, h, x, y in table)
    hdr = f"""/* Generated by tools/make_art.py - do not edit. */
#pragma once

#include <stdint.h>

typedef struct {{
    uint32_t off;   // into sprites.bin: w*h RGB565 (big-endian), then w*h alpha bytes
    uint16_t w, h;
    int16_t  x, y;  // top-left on screen at the design position (cards: 0, 0)
}} sprite_t;

enum {{
{enum}
    SPR_COUNT,
}};

#define SPR_CARD_FIRST SPR_CARD_0

#define CARD_W  {CARD_W}
#define CARD_H  {CARD_H}
#define SLOT_Y  {SLOT_Y}
static const int16_t s_slot_x[3] = {{{", ".join(map(str, SLOT_X))}}};

#define PANEL_X {PANEL_X}
#define PANEL_Y {PANEL_Y}
#define PANEL_W {PANEL_W}
#define PANEL_H {PANEL_H}

static const sprite_t s_sprites[SPR_COUNT] = {{
{rows}
}};
"""
    (ROOT / "main" / "sprites.h").write_text(hdr, encoding="utf-8")
    print(f"logo/bg 2x{W * H * 2} bytes, sprites.bin {len(blob)} bytes, {len(table)} sprites")

    if args.preview:
        by = {n: im for (n, _), im in zip(spr, imgs[1:])}
        start = logo.convert("RGBA")
        start.alpha_composite(by["button"])
        start.alpha_composite(by["logo_title"])
        table_img = bg.convert("RGBA")
        table_img.alpha_composite(by["glow"], (SLOT_X[2] - SLOT_X[0], 0))
        for x, card in zip(SLOT_X, ("card_10", "card_19", "back")):
            table_img.alpha_composite(by[card], (x, SLOT_Y))
        faces = Image.new("RGBA", (W, H), (20, 12, 51, 255))
        for k, i in enumerate((0, 3, 6, 13, 16, 21)):
            faces.alpha_composite(by[f"card_{i}"], (8 + (k % 3) * 76, 20 + (k // 3) * 128))
        sheet = Image.new("RGB", (W * 3 + 20, H), "white")
        for k, im in enumerate((start, table_img, faces)):
            sheet.paste(im, (k * (W + 10), 0))
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
