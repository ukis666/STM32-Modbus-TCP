#include "app_p10.h"
#include "app_config.h"
#include "app_regs.h"
#include "app_supervisor.h"

#include "cmsis_os.h"
#include "stm32f4xx_hal.h"

#include <stdint.h>

/*
 * HUB12 single-color 32x16 driver (2 panels side-by-side => 64x16).
 *
 * Assumptions:
 * - Panels chained IN->OUT (APP_P10_CHAIN = 2)
 * - DATA1 = top half, DATA2 = bottom half
 * - A/B row address
 * - If no C line (APP_P10_HAS_C=0), we OR-fold 16 rows into 4 scan rows.
 */

// ---------------- internal geometry ----------------

#define P10_PANEL_W 32
#define P10_PANEL_H 16
#define P10_W (P10_PANEL_W * (APP_P10_CHAIN))
#define P10_H (P10_PANEL_H)

_Static_assert(P10_PANEL_H == 16, "Assumed 32x16 panels");
_Static_assert(P10_W <= 64, "Framebuffer assumes max width 64 bits");

// ---------------- framebuffer: 1bpp, row bitmask ----------------

static volatile uint64_t g_fb[P10_H];

static inline void fb_clear_g(void)
{
  for (int y = 0; y < P10_H; ++y) {
    g_fb[y] = 0;
  }
}

/*
 * Logical-to-physical X mapping.
 * These switches must work at runtime by only toggling macros in app_config.h.
 */
static inline int map_x(int x)
{
#if APP_P10_SWAP_PANELS
  if (x < P10_PANEL_W) x += P10_PANEL_W;
  else x -= P10_PANEL_W;
#endif

#if APP_P10_MIRROR_EACH_PANEL_X
  const int p  = x / P10_PANEL_W;
  const int lx = x % P10_PANEL_W;
  x = (p * P10_PANEL_W) + ((P10_PANEL_W - 1) - lx);
#endif

  return x;
}

static inline void fb_clear(uint64_t *fb)
{
  for (int y = 0; y < P10_H; ++y) {
    fb[y] = 0;
  }
}

static inline void fb_setpx(uint64_t *fb, int x, int y, int on)
{
  if ((unsigned)x >= (unsigned)P10_W) return;
  if ((unsigned)y >= (unsigned)P10_H) return;

  x = map_x(x);

  /* x=0 is leftmost, store it as MSB for a natural shift loop */
  const uint64_t bit = (1ULL << (P10_W - 1 - x));
  if (on) fb[y] |= bit;
  else    fb[y] &= ~bit;
}

// ---------------- digits font (5x7) ----------------

/* Each digit: 7 rows, 5 bits per row (MSB on the left) */
static const uint8_t s_digit5x7[10][7] = {
  {0x1E,0x21,0x23,0x25,0x29,0x31,0x1E}, /* 0 */
  {0x08,0x18,0x08,0x08,0x08,0x08,0x1C}, /* 1 */
  {0x1E,0x21,0x01,0x06,0x18,0x20,0x3F}, /* 2 */
  {0x3F,0x02,0x04,0x06,0x01,0x21,0x1E}, /* 3 */
  {0x06,0x0A,0x12,0x22,0x3F,0x02,0x02}, /* 4 */
  {0x3F,0x20,0x3E,0x01,0x01,0x21,0x1E}, /* 5 */
  {0x0E,0x10,0x20,0x3E,0x21,0x21,0x1E}, /* 6 */
  {0x3F,0x01,0x02,0x04,0x08,0x10,0x10}, /* 7 */
  {0x1E,0x21,0x21,0x1E,0x21,0x21,0x1E}, /* 8 */
  {0x1E,0x21,0x21,0x1F,0x01,0x02,0x1C}, /* 9 */
};

static void draw_digit(uint64_t *fb, int x0, int y0, int d, int scale)
{
  if (d < 0 || d > 9) return;

  for (int ry = 0; ry < 7; ++ry) {
    const uint8_t row = s_digit5x7[d][ry];
    for (int rx = 0; rx < 5; ++rx) {
      const int on = (row & (1U << (4 - rx))) != 0;
      for (int sy = 0; sy < scale; ++sy) {
        for (int sx = 0; sx < scale; ++sx) {
          fb_setpx(fb, x0 + rx * scale + sx, y0 + ry * scale + sy, on);
        }
      }
    }
  }
}

// ---------------- layout / rendering ----------------

static void render_time(uint16_t minutes, uint16_t seconds)
{
  /*
   * Target layout (locked):
   * - Left panel : MMM
   * - Right panel: [blank digit] + SS
   *
   * Use large digits (scale=2) for readability.
   */

  uint64_t fb[P10_H];

  const int scale = 2;
  const int DIGIT_W = 5 * scale;
  const int DIGIT_H = 7 * scale;
  const int SPACE   = 0;

  const int y0 = (P10_H - DIGIT_H) / 2;

  if (minutes > 999u) minutes = 999u;
  if (seconds > 59u)  seconds = 59u;

  const int m2 = (minutes / 100u) % 10u;
  const int m1 = (minutes / 10u)  % 10u;
  const int m0 = (minutes % 10u);

  const int s1 = (seconds / 10u) % 10u;
  const int s0 = (seconds % 10u);

  fb_clear(fb);

  /* Left panel (0..31): MMM fits as 3*10px = 30px, keep 1px margin */
  const int x_left = 1;
  draw_digit(fb, x_left + 0 * (DIGIT_W + SPACE), y0, m2, scale);
  draw_digit(fb, x_left + 1 * (DIGIT_W + SPACE), y0, m1, scale);
  draw_digit(fb, x_left + 2 * (DIGIT_W + SPACE), y0, m0, scale);

  /* Right panel (32..63): [blank] + SS => total 3 digits (30px), keep 1px margin */
  const int x_right = P10_PANEL_W + 1;
  draw_digit(fb, x_right + 1 * (DIGIT_W + SPACE), y0, s1, scale);
  draw_digit(fb, x_right + 2 * (DIGIT_W + SPACE), y0, s0, scale);

  /*
   * Atomic commit against the scan ISR:
   * render into a local buffer, then copy under IRQ lock.
   */
  __disable_irq();
  for (int y = 0; y < P10_H; ++y) {
    g_fb[y] = fb[y];
  }
  __enable_irq();
}

void APP_P10_SetTime(uint16_t minutes, uint16_t seconds)
{
  render_time(minutes, seconds);
}

// ---------------- GPIO helpers ----------------

static inline void gpio_write(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState st)
{
  HAL_GPIO_WritePin(port, pin, st);
}

static inline void pulse(GPIO_TypeDef* port, uint16_t pin)
{
  /* Fast toggle using BSRR for less jitter */
  port->BSRR = (uint32_t)pin;
  port->BSRR = (uint32_t)pin << 16;
}

static void p10_gpio_init(void)
{
  /* Enable commonly used GPIO clocks (safe even if already enabled) */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  /* DATA1 */
  GPIO_InitStruct.Pin = APP_P10_DATA1_Pin;
  HAL_GPIO_Init(APP_P10_DATA1_GPIO_Port, &GPIO_InitStruct);

  /* DATA2 */
  GPIO_InitStruct.Pin = APP_P10_DATA2_Pin;
  HAL_GPIO_Init(APP_P10_DATA2_GPIO_Port, &GPIO_InitStruct);

  /* CLK */
  GPIO_InitStruct.Pin = APP_P10_CLK_Pin;
  HAL_GPIO_Init(APP_P10_CLK_GPIO_Port, &GPIO_InitStruct);

  /* LAT */
  GPIO_InitStruct.Pin = APP_P10_LAT_Pin;
  HAL_GPIO_Init(APP_P10_LAT_GPIO_Port, &GPIO_InitStruct);

  /* OE */
  GPIO_InitStruct.Pin = APP_P10_OE_Pin;
  HAL_GPIO_Init(APP_P10_OE_GPIO_Port, &GPIO_InitStruct);

  /* A */
  GPIO_InitStruct.Pin = APP_P10_A_Pin;
  HAL_GPIO_Init(APP_P10_A_GPIO_Port, &GPIO_InitStruct);

  /* B */
  GPIO_InitStruct.Pin = APP_P10_B_Pin;
  HAL_GPIO_Init(APP_P10_B_GPIO_Port, &GPIO_InitStruct);

#if APP_P10_HAS_C
  GPIO_InitStruct.Pin = APP_P10_C_Pin;
  HAL_GPIO_Init(APP_P10_C_GPIO_Port, &GPIO_InitStruct);
#endif

  /* Safe default levels */
  gpio_write(APP_P10_DATA1_GPIO_Port, APP_P10_DATA1_Pin, GPIO_PIN_RESET);
  gpio_write(APP_P10_DATA2_GPIO_Port, APP_P10_DATA2_Pin, GPIO_PIN_RESET);
  gpio_write(APP_P10_CLK_GPIO_Port,   APP_P10_CLK_Pin,   GPIO_PIN_RESET);
  gpio_write(APP_P10_LAT_GPIO_Port,   APP_P10_LAT_Pin,   GPIO_PIN_RESET);

#if APP_P10_OE_ACTIVE_LOW
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_SET);   /* disable display */
#else
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_RESET);
#endif

  gpio_write(APP_P10_A_GPIO_Port, APP_P10_A_Pin, GPIO_PIN_RESET);
  gpio_write(APP_P10_B_GPIO_Port, APP_P10_B_Pin, GPIO_PIN_RESET);
#if APP_P10_HAS_C
  gpio_write(APP_P10_C_GPIO_Port, APP_P10_C_Pin, GPIO_PIN_RESET);
#endif
}

// ---------------- scan engine ----------------

static volatile uint8_t g_scan_row = 0;

static inline void set_addr(uint8_t r)
{
  gpio_write(APP_P10_A_GPIO_Port, APP_P10_A_Pin, (r & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  gpio_write(APP_P10_B_GPIO_Port, APP_P10_B_Pin, (r & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#if APP_P10_HAS_C
  gpio_write(APP_P10_C_GPIO_Port, APP_P10_C_Pin, (r & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  (void)r;
#endif
}

static inline void oe_disable(void)
{
#if APP_P10_OE_ACTIVE_LOW
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_SET);
#else
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_RESET);
#endif
}

static inline void oe_enable(void)
{
#if APP_P10_OE_ACTIVE_LOW
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_RESET);
#else
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_SET);
#endif
}

void APP_P10_ScanISR(void)
{
  oe_disable();

  const uint8_t scan_rows = (APP_P10_HAS_C ? 8 : 4);
  uint8_t r = g_scan_row;
  if (r >= scan_rows) r = 0;
  set_addr(r);

  const int row_top = r;
  const int row_bot = r + 8;

  uint64_t bits_top = 0;
  uint64_t bits_bot = 0;

#if APP_P10_HAS_C
  bits_top = g_fb[row_top];
  bits_bot = g_fb[row_bot];
#else
  const int y1 = row_top;
  const int y2 = row_top + 4;
  const int y3 = row_bot;
  const int y4 = row_bot + 4;

  bits_top = (g_fb[y1] | g_fb[y2]);
  bits_bot = (g_fb[y3] | g_fb[y4]);
#endif

#if !APP_P10_SHIFT_MSB_FIRST
  uint64_t rt = 0, rb = 0;
  for (int i = 0; i < P10_W; ++i) {
    rt <<= 1; rb <<= 1;
    rt |= (bits_top >> i) & 1ULL;
    rb |= (bits_bot >> i) & 1ULL;
  }
  bits_top = rt;
  bits_bot = rb;
#endif

  for (int i = 0; i < P10_W; ++i) {
    const uint8_t b1 = (uint8_t)((bits_top >> (P10_W - 1 - i)) & 1ULL);
    const uint8_t b2 = (uint8_t)((bits_bot >> (P10_W - 1 - i)) & 1ULL);

    gpio_write(APP_P10_DATA1_GPIO_Port, APP_P10_DATA1_Pin, b1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    gpio_write(APP_P10_DATA2_GPIO_Port, APP_P10_DATA2_Pin, b2 ? GPIO_PIN_SET : GPIO_PIN_RESET);

    pulse(APP_P10_CLK_GPIO_Port, APP_P10_CLK_Pin);
  }

  pulse(APP_P10_LAT_GPIO_Port, APP_P10_LAT_Pin);
  oe_enable();

  g_scan_row = (uint8_t)(r + 1);
  if (g_scan_row >= scan_rows) g_scan_row = 0;
}

// ---------------- public init/task ----------------

void APP_P10_Init(void)
{
  p10_gpio_init();
  fb_clear_g();
  APP_P10_SetTime(0, 0);
}

void APP_P10_Task(void *argument)
{
  (void)argument;

  uint16_t m = 0, s = 0;
  APP_RegsGetTime(&m, &s);
  APP_P10_SetTime(m, s);

  for (;;)
  {
    APP_SupervisorKick(APP_KICK_P10);

    /* Update display as soon as PLC changes MMM/SS */
    if (APP_RegsConsumeChangedTime(&m, &s)) {
      APP_P10_SetTime(m, s);
    }

    osDelay(50);
  }
}
