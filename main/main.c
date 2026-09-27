/*
 * StarWish Tarot (星愿塔罗) on the 1.50" 240x280 GC9306/GC9307 panel.
 *
 * Start screen: logo with a pulsing "按 BOOT 开始占卜" button. A click shuffles the deck (fan out, riffle,
 * gather), deals three cards face down (past / present / future), then turns one over every 5 s (a click turns
 * the next one at once). When all three are up, the panel scrolls through the readings and a summary
 * (star ratings, lucky colour / number / tip). Click = read again, hold 1 s = back to the start screen.
 *
 * Art: tools/make_art.py, texts and fonts: tools/make_text.py. Frames are composed strip by strip into two
 * DMA buffers, like the other projects on this panel.
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "button.h"
#include "lcd.h"
#include "sprites.h"
#include "tarot_text.h"

static const char* TAG = "tarot";

#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES

#define LINES_PER_CHUNK   40
#define MAX_TRANSFER_SIZE (W * LINES_PER_CHUNK * sizeof(uint16_t))

#define SWAP16(v) ((uint16_t)(((v) >> 8) | ((v) << 8)))
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))  // true order

/* Timeline (ms) */
#define FAN_MS       700    // deck -> fan
#define RIFFLE_MS    300    // one shuffle step
#define RIFFLES      5
#define GATHER_MS    500
#define DEAL_MS      260    // one card flying to its slot
#define SINK_MS      320    // the rest of the deck sinks away
#define REVEAL_GAP   5000   // between two cards turning over
#define FLIP_MS      600
#define GLOW_MS      800
#define READING_WAIT 1800   // after the last card, before the reading starts
#define SCROLL_DELAY 1500   // reading holds still this long before it scrolls
#define SCROLL_PX_S  18
#define SCROLL_GAP   36     // blank space before the reading repeats

#define SHUFFLE_CARDS 7
#define FAN_STEP      24

/* Colours (true-order RGB565) */
#define C_GOLD  RGB(255, 217, 138)
#define C_HEAD  RGB(255, 231, 160)
#define C_TEXT  RGB(236, 230, 255)
#define C_DIM   RGB(154, 143, 192)
#define C_STAR  RGB(255, 204, 51)
#define C_NAME  RGB(255, 190, 120)

extern const uint8_t logo_start[] asm("_binary_logo_rgb565_start");
extern const uint8_t bg_start[] asm("_binary_bg_rgb565_start");
extern const uint8_t spr_start[] asm("_binary_sprites_bin_start");

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t         s_trans_done;

/* ---------------------------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------------------------*/

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float ease_in_out(float t)
{
    t = clampf(t, 0, 1);
    return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
}

static float ease_out(float t)
{
    t = clampf(t, 0, 1);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

static int lerpi(int a, int b, float t)
{
    return a + (int)lroundf((b - a) * t);
}

/* Blend true-order `fg` over panel-order `bg`, alpha 0..256 */
static inline uint16_t blend(uint16_t bg, uint16_t fg, int a)
{
    const uint16_t b  = SWAP16(bg);
    const int      rb = b >> 11, gb = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int      r  = rb + ((((fg >> 11) - rb) * a + 128) >> 8);
    const int      g  = gb + (((((fg >> 5) & 0x3F) - gb) * a + 128) >> 8);
    const int      bl = bb + ((((fg & 0x1F) - bb) * a + 128) >> 8);
    return SWAP16((uint16_t)((r << 11) | (g << 5) | bl));
}

/* ---------------------------------------------------------------------------------------------
 * Sprites
 * -------------------------------------------------------------------------------------------*/

/* Sprite `id` with its top-left moved by (dx, dy) from the design position, alpha scaled by mul/256 */
static void blit(int id, int dx, int dy, int mul, uint16_t* strip, int y0, int y1)
{
    const sprite_t* sp = &s_sprites[id];
    const int       x = sp->x + dx, y = sp->y + dy;
    const int       ya = y > y0 ? y : y0, yb = y + sp->h < y1 ? y + sp->h : y1;
    const int       xa = x > 0 ? x : 0, xb = x + sp->w < W ? x + sp->w : W;
    const uint8_t*  color = spr_start + sp->off;
    const uint8_t*  alpha = color + (uint32_t)sp->w * sp->h * 2;
    for (int py = ya; py < yb; py++) {
        const int      ri  = (py - y) * sp->w;
        const uint8_t* ar  = alpha + ri;
        const uint8_t* cr  = color + ri * 2;
        uint16_t*      out = strip + (py - y0) * W;
        for (int px = xa; px < xb; px++) {
            const int i = px - x;
            int       a = ar[i];
            if (!a) {
                continue;
            }
            const uint16_t c = (uint16_t)(cr[2 * i] << 8 | cr[2 * i + 1]);
            if (mul < 256) {
                a = a * mul >> 8;
            }
            out[px] = a >= 255 ? SWAP16(c) : blend(out[px], c, a + (a >> 7));
        }
    }
}

/* A card squeezed horizontally to `scale`/256 of its width around `cx` (the flip), darkened as it turns */
static void blit_card(int id, int cx, int top, int scale, uint16_t* strip, int y0, int y1)
{
    const sprite_t* sp = &s_sprites[id];
    const int       w  = sp->w * scale / 256;
    if (w <= 0) {
        return;
    }
    const int      x = cx - w / 2;
    const int      ya = top > y0 ? top : y0, yb = top + sp->h < y1 ? top + sp->h : y1;
    const uint8_t* color = spr_start + sp->off;
    const uint8_t* alpha = color + (uint32_t)sp->w * sp->h * 2;
    const int      shade = 160 + scale * 96 / 256;  // 160..256
    for (int py = ya; py < yb; py++) {
        const int ri  = (py - top) * sp->w;
        uint16_t* out = strip + (py - y0) * W;
        for (int dx = 0; dx < w; dx++) {
            const int px = x + dx;
            if (px < 0 || px >= W) {
                continue;
            }
            const int si = ri + dx * sp->w / w;
            const int a  = alpha[si];
            if (!a) {
                continue;
            }
            uint16_t c = (uint16_t)(color[2 * si] << 8 | color[2 * si + 1]);
            if (shade < 256) {
                c = (uint16_t)((((c >> 11) * shade >> 8) << 11) | ((((c >> 5) & 0x3F) * shade >> 8) << 5) |
                               ((c & 0x1F) * shade >> 8));
            }
            out[px] = a >= 255 ? SWAP16(c) : blend(out[px], c, a + (a >> 7));
        }
    }
}

/* ---------------------------------------------------------------------------------------------
 * Text (UTF-8, fonts from tools/make_text.py)
 * -------------------------------------------------------------------------------------------*/

static uint32_t utf8_next(const char** p)
{
    const uint8_t* s = (const uint8_t*)*p;
    uint32_t       c = *s++;
    if (c >= 0xE0 && s[0] && s[1]) {
        c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        s += 2;
    } else if (c >= 0xC0 && s[0]) {
        c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
        s += 1;
    }
    *p = (const char*)s;
    return c;
}

static const cjk_glyph_t* glyph(const cjk_font_t* f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (f->cps[mid] == cp) {
            return &f->glyphs[mid];
        }
        if (f->cps[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

static int char_adv(const cjk_font_t* f, uint32_t cp)
{
    const cjk_glyph_t* g = glyph(f, cp);
    return g ? g->adv : f->line_h / 2;
}

static int text_width(const cjk_font_t* f, const char* s)
{
    int w = 0;
    while (*s) {
        w += char_adv(f, utf8_next(&s));
    }
    return w;
}

/* Draw `s` with its line top at (x, top), alpha mul/256, only inside rows [clip0, clip1) */
static void text_draw(const cjk_font_t* f, const char* s, int x, int top, uint16_t color, int mul, int clip0, int clip1,
                      uint16_t* strip, int y0, int y1)
{
    const int ra = clip0 > y0 ? clip0 : y0, rb = clip1 < y1 ? clip1 : y1;
    if (ra >= rb || top >= rb || top + f->line_h <= ra) {
        return;
    }
    while (*s) {
        const cjk_glyph_t* g = glyph(f, utf8_next(&s));
        if (!g) {
            x += f->line_h / 2;
            continue;
        }
        const int gx = x + g->x, gy = top + g->y, stride = (g->w + 1) / 2;
        const int ya = gy > ra ? gy : ra, yb = gy + g->h < rb ? gy + g->h : rb;
        for (int py = ya; py < yb; py++) {
            const uint8_t* src = f->bits + g->off + (py - gy) * stride;
            uint16_t*      out = strip + (py - y0) * W;
            for (int i = 0; i < g->w; i++) {
                const int px = gx + i;
                int       a  = (i & 1) ? src[i / 2] & 0x0F : src[i / 2] >> 4;
                if (!a || px < 0 || px >= W) {
                    continue;
                }
                a = a * 17 * mul >> 8;  // 0..255
                out[px] = blend(out[px], color, a + (a >> 7));
            }
        }
        x += g->adv;
    }
}

/* ---------------------------------------------------------------------------------------------
 * The reading: laid out once as a list of lines, then scrolled through the panel
 * -------------------------------------------------------------------------------------------*/

#define TEXT_X     (PANEL_X + 12)
#define TEXT_W     (PANEL_W - 24)
#define TEXT_TOP   (PANEL_Y + 6)
#define TEXT_BOT   (PANEL_Y + PANEL_H - 6)
#define LINE_STEP  21
#define MAX_LINES  48

typedef struct {
    const char*       txt;
    const cjk_font_t* font;
    uint16_t          color;
    int16_t           x, y;  // x on screen, y from the top of the reading
} line_t;

static char   s_pool[3072];
static int    s_pool_len;
static line_t s_lines[MAX_LINES];
static int    s_nlines;
static int    s_text_h;  // height of the whole reading

static const char* pool_add(const char* s, int len)
{
    if (s_pool_len + len + 1 > (int)sizeof(s_pool)) {
        len = sizeof(s_pool) - s_pool_len - 1;
    }
    char* d = s_pool + s_pool_len;
    memcpy(d, s, len);
    d[len]      = 0;
    s_pool_len += len + 1;
    return d;
}

static void add_line(const char* s, int len, const cjk_font_t* f, uint16_t color, int x)
{
    if (s_nlines < MAX_LINES) {
        s_lines[s_nlines++] = (line_t){pool_add(s, len), f, color, (int16_t)x, (int16_t)s_text_h};
    }
    s_text_h += LINE_STEP;
}

static bool is_closing_punct(uint32_t cp)
{
    return cp == 0xFF0C || cp == 0x3002 || cp == 0xFF01 || cp == 0x3001 || cp == 0xFF1B || cp == 0xFF1A ||
           cp == 0x300D || cp == 0xFF09 || cp == 0x2026;
}

/* Word-wrap `s` to the panel width (CJK: break anywhere, but never start a line with closing punctuation) */
static void add_wrapped(const char* s, const cjk_font_t* f, uint16_t color, int indent)
{
    const int max_w = TEXT_W - indent;
    while (*s) {
        const char* start = s;
        const char* cut   = s;
        int         w     = 0;
        while (*cut) {
            const char*    p   = cut;
            const uint32_t cp  = utf8_next(&p);
            const int      adv = char_adv(f, cp);
            if (w + adv > max_w && cut != start && !is_closing_punct(cp)) {
                break;
            }
            w += adv;
            cut = p;
        }
        add_line(start, (int)(cut - start), f, color, TEXT_X + indent);
        s = cut;
    }
}

static void add_gap(int px)
{
    s_text_h += px;
}

static struct {
    int  card[3];
    int  stars[4];
    int  closing, color, number, tip;
} s_draw;

static void build_reading(void)
{
    char buf[160];
    s_pool_len = s_nlines = s_text_h = 0;

    for (int i = 0; i < 3; i++) {
        const tarot_card_t* c = &s_cards[s_draw.card[i]];
        snprintf(buf, sizeof(buf), "【%s · %s】", s_positions[i], c->name);
        add_line(buf, strlen(buf), &font_head, C_HEAD, TEXT_X - 6);
        snprintf(buf, sizeof(buf), "%s  %s", c->numeral, c->keyword);
        add_line(buf, strlen(buf), &font_body, C_NAME, TEXT_X);
        add_wrapped(c->reading, &font_body, C_TEXT, 0);
        add_gap(8);
    }

    snprintf(buf, sizeof(buf), "★ %s ★", TXT_SUMMARY);
    add_line(buf, strlen(buf), &font_head, C_HEAD, TEXT_X + (TEXT_W - text_width(&font_head, buf)) / 2);
    for (int d = 0; d < 4; d++) {
        int n = snprintf(buf, sizeof(buf), "%s  ", s_dimensions[d]);
        for (int k = 0; k < 5; k++) {
            n += snprintf(buf + n, sizeof(buf) - n, "%s", k < s_draw.stars[d] ? "★" : "☆");
        }
        add_line(buf, n, &font_body, C_STAR, TEXT_X + 30);
    }
    add_gap(4);
    add_wrapped(s_closing[s_draw.closing], &font_body, C_TEXT, 0);
    add_gap(4);
    snprintf(buf, sizeof(buf), "%s%s", TXT_LUCKY_COLOR, s_lucky_colors[s_draw.color]);
    add_line(buf, strlen(buf), &font_body, C_GOLD, TEXT_X);
    snprintf(buf, sizeof(buf), "%s%d", TXT_LUCKY_NUMBER, s_draw.number);
    add_line(buf, strlen(buf), &font_body, C_GOLD, TEXT_X);
    snprintf(buf, sizeof(buf), "%s%s", TXT_LUCKY_TIP, s_lucky_tips[s_draw.tip]);
    add_wrapped(buf, &font_body, C_GOLD, 0);
    add_gap(8);
    add_wrapped(TXT_AGAIN, &font_body, C_DIM, 0);
    add_wrapped(TXT_FOOTER, &font_body, C_DIM, 0);
}

/* Pick three different cards and work out the summary */
static void new_draw(void)
{
    int deck[TAROT_CARDS];
    for (int i = 0; i < TAROT_CARDS; i++) {
        deck[i] = i;
    }
    for (int i = 0; i < 3; i++) {
        const int j = i + esp_random() % (TAROT_CARDS - i);
        const int t = deck[i];
        deck[i]     = deck[j];
        deck[j]     = t;
        s_draw.card[i] = deck[i];
    }
    int best = 0, best_sum = -1;
    for (int d = 0; d < 4; d++) {
        int sum = 0;
        for (int i = 0; i < 3; i++) {
            sum += s_cards[s_draw.card[i]].score[d];
        }
        int stars       = (sum + 2) / 3;  // average, rounded up
        s_draw.stars[d] = stars < 3 ? 3 : stars;
        if (sum > best_sum || (sum == best_sum && esp_random() & 1)) {
            best     = d;
            best_sum = sum;
        }
    }
    s_draw.closing = best;
    s_draw.color   = esp_random() % (sizeof(s_lucky_colors) / sizeof(s_lucky_colors[0]));
    s_draw.number  = 1 + esp_random() % 9;
    s_draw.tip     = esp_random() % (sizeof(s_lucky_tips) / sizeof(s_lucky_tips[0]));
    build_reading();
    ESP_LOGI(TAG, "draw: %s / %s / %s", s_cards[s_draw.card[0]].name, s_cards[s_draw.card[1]].name,
             s_cards[s_draw.card[2]].name);
}

/* ---------------------------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------------------------*/

typedef enum { ST_LOGO, ST_SHUFFLE, ST_TABLE } state_t;

static struct {
    state_t state;
    int64_t t0;                        // state start
    uint8_t perm[RIFFLES + 1][SHUFFLE_CARDS];  // fan position of each card after each riffle
    int64_t flip_t[3];                 // when each card started turning over (0 = not yet)
    int64_t next_flip;
    int64_t reading_t;                 // reading start (0 = not yet)
} s;

#define SHUFFLE_MS (FAN_MS + RIFFLES * RIFFLE_MS + GATHER_MS + 2 * DEAL_MS + SINK_MS)

static void start_shuffle(int64_t now)
{
    new_draw();
    for (int r = 0; r <= RIFFLES; r++) {
        for (int k = 0; k < SHUFFLE_CARDS; k++) {
            s.perm[r][k] = k;
        }
        if (r == 0) {
            continue;
        }
        for (int k = SHUFFLE_CARDS - 1; k > 0; k--) {
            const int j   = esp_random() % (k + 1);
            const int t   = s.perm[r][k];
            s.perm[r][k]  = s.perm[r][j];
            s.perm[r][j]  = t;
        }
    }
    memset(s.flip_t, 0, sizeof(s.flip_t));
    s.reading_t = 0;
    s.state     = ST_SHUFFLE;
    s.t0        = now;
}

static void flip_next(int64_t now)
{
    for (int i = 0; i < 3; i++) {
        if (!s.flip_t[i]) {
            s.flip_t[i] = now;
            s.next_flip = now + REVEAL_GAP;
            if (i == 2) {
                s.reading_t = now + FLIP_MS + READING_WAIT;
            }
            return;
        }
    }
}

static void on_click(int64_t now)
{
    switch (s.state) {
    case ST_LOGO:
        start_shuffle(now);
        break;
    case ST_SHUFFLE:
        break;  // let it finish
    case ST_TABLE:
        if (!s.flip_t[2]) {
            flip_next(now);  // don't wait, turn the next one now
        } else if (s.reading_t && now >= s.reading_t) {
            start_shuffle(now);
        }
        break;
    }
}

static void update(int64_t now)
{
    if (s.state == ST_SHUFFLE && now - s.t0 >= SHUFFLE_MS) {
        s.state     = ST_TABLE;
        s.t0        = now;
        s.next_flip = now + REVEAL_GAP;
    }
    if (s.state == ST_TABLE && !s.flip_t[2] && now >= s.next_flip) {
        flip_next(now);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------------------------*/

typedef struct {
    int16_t  x, y;
    uint8_t  big;
    uint16_t period;
    float    phase;
} twinkle_t;

static const twinkle_t s_logo_twinkles[] = {
    {180, 92, 1, 1500, 0.0f}, {214, 104, 0, 1300, 1.7f}, {206, 136, 1, 1900, 3.1f}, {172, 128, 0, 1100, 4.4f},
    {226, 60, 0, 2100, 2.3f}, {46, 180, 0, 1700, 5.0f},
};
static const twinkle_t s_table_twinkles[] = {
    {60, 12, 0, 1800, 0.5f}, {186, 10, 0, 2200, 2.4f}, {230, 150, 1, 2600, 4.2f}, {8, 140, 0, 1900, 1.1f},
};

static void draw_twinkles(const twinkle_t* t, int n, int64_t now, uint16_t* strip, int y0, int y1)
{
    for (int i = 0; i < n; i++) {
        const float v = sinf(now * (2 * (float)M_PI) / t[i].period + t[i].phase);
        if (v > 0) {
            blit(t[i].big ? SPR_SPARKLE_BIG : SPR_SPARKLE_SMALL, t[i].x - 20, t[i].y - 20, (int)(256 * v * v), strip,
                 y0, y1);
        }
    }
}

static struct {
    int64_t now;
} f;

static void render_logo(uint16_t* strip, int y0, int y1)
{
    memcpy(strip, logo_start + y0 * W * 2, (size_t)(y1 - y0) * W * 2);
    draw_twinkles(s_logo_twinkles, sizeof(s_logo_twinkles) / sizeof(s_logo_twinkles[0]), f.now, strip, y0, y1);
    blit(SPR_LOGO_TITLE, 0, 0, 256, strip, y0, y1);
    const float pulse = 0.5f + 0.5f * sinf(f.now * (2 * (float)M_PI) / 1400);
    blit(SPR_BUTTON, 0, 0, 170 + (int)(86 * pulse), strip, y0, y1);
}

/* Where shuffle card k is at time t (ms into the shuffle); returns alpha, 0 = gone */
static int shuffle_card_pos(int k, int t, int* x, int* y)
{
    const int deck_x = s_slot_x[1], deck_y = SLOT_Y;
    const int stack  = (SHUFFLE_CARDS - 1 - k);  // the top card (k = 6) sits highest
#define FAN_X(p) (s_slot_x[1] + ((p) - SHUFFLE_CARDS / 2) * FAN_STEP)
#define FAN_Y(p) (SLOT_Y + 6 + abs((p) - SHUFFLE_CARDS / 2) * 4)
    if (t < FAN_MS) {
        const float e = ease_out((float)t / FAN_MS);
        *x            = lerpi(deck_x, FAN_X(k), e);
        *y            = lerpi(deck_y - stack, FAN_Y(k), e);
        return 256;
    }
    t -= FAN_MS;
    if (t < RIFFLES * RIFFLE_MS) {
        const int   r  = t / RIFFLE_MS;
        const float e  = ease_in_out((float)(t % RIFFLE_MS) / RIFFLE_MS);
        const int   p0 = s.perm[r][k], p1 = s.perm[r + 1][k];
        *x             = lerpi(FAN_X(p0), FAN_X(p1), e);
        *y             = lerpi(FAN_Y(p0), FAN_Y(p1), e) - (p0 != p1 ? (int)(14 * sinf(e * (float)M_PI)) : 0);
        return 256;
    }
    t -= RIFFLES * RIFFLE_MS;
    const int pl = s.perm[RIFFLES][k];
    if (t < GATHER_MS) {
        const float e = ease_in_out((float)t / GATHER_MS);
        *x            = lerpi(FAN_X(pl), deck_x, e);
        *y            = lerpi(FAN_Y(pl), deck_y - stack, e);
        return 256;
    }
    t -= GATHER_MS;
    *x = deck_x;
    *y = deck_y - stack;
    if (k == SHUFFLE_CARDS - 1 || k == SHUFFLE_CARDS - 2) {  // dealt to the left / right slot
        const int   slot = k == SHUFFLE_CARDS - 1 ? 0 : 2;
        const int   t0   = k == SHUFFLE_CARDS - 1 ? 0 : DEAL_MS;
        const float e    = ease_out((float)(t - t0) / DEAL_MS);
        *x               = lerpi(deck_x, s_slot_x[slot], e);
        *y               = lerpi(deck_y - stack, SLOT_Y, e);
        return 256;
    }
    if (k == SHUFFLE_CARDS - 3) {  // becomes the middle card
        const float e = ease_out((float)(t - 2 * DEAL_MS) / SINK_MS);
        *y            = lerpi(deck_y - stack, SLOT_Y, e);
        return 256;
    }
    const float e = clampf((float)(t - 2 * DEAL_MS) / SINK_MS, 0, 1);  // the rest sinks and fades
    *y            = deck_y - stack + (int)(46 * e * e);
    return (int)(256 * (1 - e));
#undef FAN_X
#undef FAN_Y
}

static void render_panel_reveal(uint16_t* strip, int y0, int y1)
{
    const int64_t now = f.now;
    const char*   hd  = TXT_FOCUS;
    text_draw(&font_head, hd, W / 2 - text_width(&font_head, hd) / 2, PANEL_Y + 8, C_HEAD, 256, 0, H, strip, y0, y1);
    char buf[96];
    for (int i = 0; i < 3; i++) {
        const int top = PANEL_Y + 34 + i * 22;
        if (s.flip_t[i] && now - s.flip_t[i] >= FLIP_MS / 2) {
            const tarot_card_t* c = &s_cards[s_draw.card[i]];
            snprintf(buf, sizeof(buf), "%s · ", s_positions[i]);
            int x = TEXT_X;
            text_draw(&font_body, buf, x, top, C_DIM, 256, 0, H, strip, y0, y1);
            x += text_width(&font_body, buf);
            text_draw(&font_body, c->name, x, top, C_NAME, 256, 0, H, strip, y0, y1);
            x += text_width(&font_body, c->name);
            snprintf(buf, sizeof(buf), " · %s", c->keyword);
            text_draw(&font_body, buf, x, top, C_TEXT, 256, 0, H, strip, y0, y1);
        } else if (!s.flip_t[i] && (i == 0 || s.flip_t[i - 1])) {
            const int left = (int)((s.next_flip - now + 999) / 1000);
            snprintf(buf, sizeof(buf), "%s%s · %d %s", s_positions[i], TXT_CARD_OF, left < 0 ? 0 : left, TXT_REVEAL_IN);
            text_draw(&font_body, buf, TEXT_X, top, C_GOLD, 256, 0, H, strip, y0, y1);
        } else if (!s.flip_t[i]) {
            snprintf(buf, sizeof(buf), "%s%s", s_positions[i], TXT_CARD_OF);
            text_draw(&font_body, buf, TEXT_X, top, C_DIM, 160, 0, H, strip, y0, y1);
        }
    }
}

static void render_panel_reading(uint16_t* strip, int y0, int y1)
{
    const int64_t t      = f.now - s.reading_t;
    const int     period = s_text_h + SCROLL_GAP;
    int           off    = t < SCROLL_DELAY ? 0 : (int)((t - SCROLL_DELAY) * SCROLL_PX_S / 1000) % period;
    const int     fade   = 14;  // px at the top and bottom edges where lines fade out
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < s_nlines; i++) {
            const line_t* ln  = &s_lines[i];
            const int     top = TEXT_TOP + ln->y - off + rep * period;
            if (top >= TEXT_BOT || top + ln->font->line_h <= TEXT_TOP) {
                continue;
            }
            const int mid = top + LINE_STEP / 2;
            int       mul = 256;
            if (mid < TEXT_TOP + fade) {
                mul = (mid - TEXT_TOP) * 256 / fade;
            } else if (mid > TEXT_BOT - fade) {
                mul = (TEXT_BOT - mid) * 256 / fade;
            }
            if (mul > 0) {
                text_draw(ln->font, ln->txt, ln->x, top, ln->color, mul > 256 ? 256 : mul, TEXT_TOP, TEXT_BOT, strip, y0,
                          y1);
            }
        }
    }
}

static void render_table(uint16_t* strip, int y0, int y1)
{
    memcpy(strip, bg_start + y0 * W * 2, (size_t)(y1 - y0) * W * 2);
    draw_twinkles(s_table_twinkles, sizeof(s_table_twinkles) / sizeof(s_table_twinkles[0]), f.now, strip, y0, y1);
    const int64_t now = f.now;

    if (s.state == ST_SHUFFLE) {
        const int t = (int)(now - s.t0);
        for (int k = 0; k < SHUFFLE_CARDS; k++) {
            int       x, y;
            const int a = shuffle_card_pos(k, t, &x, &y);
            if (a > 0) {
                blit(SPR_BACK, x, y, a, strip, y0, y1);
            }
        }
        const char* msg = TXT_SHUFFLE;
        text_draw(&font_head, msg, W / 2 - text_width(&font_head, msg) / 2, PANEL_Y + PANEL_H / 2 - 12, C_HEAD, 256,
                  0, H, strip, y0, y1);
        return;
    }

    for (int i = 0; i < 3; i++) {
        const int64_t ft = s.flip_t[i] ? now - s.flip_t[i] : -1;
        if (ft >= FLIP_MS && ft < FLIP_MS + GLOW_MS) {  // glow once the card is up
            const float g = 1 - (float)(ft - FLIP_MS) / GLOW_MS;
            blit(SPR_GLOW, s_slot_x[i] - s_slot_x[0], 0, (int)(256 * g), strip, y0, y1);
        }
    }
    for (int i = 0; i < 3; i++) {
        const int     cx = s_slot_x[i] + CARD_W / 2;
        const int64_t ft = s.flip_t[i] ? now - s.flip_t[i] : -1;
        if (ft < 0) {
            blit(SPR_BACK, s_slot_x[i], SLOT_Y, 256, strip, y0, y1);
        } else if (ft < FLIP_MS) {
            const float p     = (float)ft / FLIP_MS;
            const int   scale = (int)(256 * fabsf(cosf(p * (float)M_PI)));
            const int   lift  = (int)(8 * sinf(p * (float)M_PI));
            blit_card(p < 0.5f ? SPR_BACK : SPR_CARD_FIRST + s_draw.card[i], cx, SLOT_Y - lift, scale, strip, y0, y1);
        } else {
            blit(SPR_CARD_FIRST + s_draw.card[i], s_slot_x[i], SLOT_Y, 256, strip, y0, y1);
        }
    }

    if (s.reading_t && now >= s.reading_t) {
        render_panel_reading(strip, y0, y1);
    } else {
        render_panel_reveal(strip, y0, y1);
    }
}

static void render_strip(uint16_t* strip, int y0, int y1)
{
    if (s.state == ST_LOGO) {
        render_logo(strip, y0, y1);
    } else {
        render_table(strip, y0, y1);
    }
}

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &woken);
    return woken == pdTRUE;
}

static void draw_frame(uint16_t* bufs[2])
{
    int  cur     = 0;
    bool pending = false;
    for (int y0 = 0; y0 < H; y0 += LINES_PER_CHUNK) {
        int y1 = y0 + LINES_PER_CHUNK < H ? y0 + LINES_PER_CHUNK : H;
        render_strip(bufs[cur], y0, y1);
        if (pending) {
            xSemaphoreTake(s_trans_done, portMAX_DELAY);
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y0, W, y1, bufs[cur]);
        pending = true;
        cur ^= 1;
    }
    xSemaphoreTake(s_trans_done, portMAX_DELAY);
}

/* ---------------------------------------------------------------------------------------------*/

void app_main(void)
{
    ESP_ERROR_CHECK(lcd_init(&s_panel, &s_io, MAX_TRANSFER_SIZE));

    s_trans_done      = xSemaphoreCreateBinary();
    uint16_t* bufs[2] = {heap_caps_malloc(MAX_TRANSFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                         heap_caps_malloc(MAX_TRANSFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)};
    assert(s_trans_done && bufs[0] && bufs[1]);
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL));

    s.state = ST_LOGO;
    f.now   = now_ms();
    draw_frame(bufs);
    lcd_backlight(true);
    button_init();
    ESP_LOGI(TAG, "ready: click BOOT to draw three cards, hold 1 s for the start screen");

    bool long_fired = false;  // the current press already went back to the start screen
    while (1) {
        const int64_t  now = now_ms();
        button_event_t ev;
        while ((ev = button_get(0)) != BUTTON_NONE) {
            if (ev == BUTTON_DOWN) {
                long_fired = false;
            } else if (ev == BUTTON_LONG) {  // BUTTON_LONG_MS = 1000
                long_fired = true;
                s.state    = ST_LOGO;
            } else if (ev == BUTTON_UP && !long_fired) {
                on_click(now);
            }
        }
        update(now);
        f.now = now;
        draw_frame(bufs);
        vTaskDelay(1);
    }
}
