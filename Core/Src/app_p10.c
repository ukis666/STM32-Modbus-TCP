#include "app_p10.h"
#include "app_config.h"
#include "app_regs.h"
#include "app_supervisor.h"

#include "cmsis_os.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_tim.h"

#include <string.h>

/*
 * HUB12 tek renk 32x16 panel surucu (2 panel yan yana, 64x16)
 *
 * Varsayim:
 * - Panel IN->OUT ile zincir (APP_P10_CHAIN = 2)
 * - DATA1 = ust yarim, DATA2 = alt yarim
 * - A/B satir adres
 * - C yoksa (APP_P10_HAS_C=0) 16 row'u 4 scan satirina OR fold ediyoruz.
 *
 * Eger panelinde C hatti varsa:
 *   APP_P10_HAS_C=1 yap, C pini tanimla ve kablola.
 */

// ---------------- config checks ----------------

#ifndef APP_P10_TIM_INSTANCE
#error "APP_P10_TIM_INSTANCE not defined in app_config.h"
#endif

#ifndef APP_P10_SCAN_IRQ_HZ
#error "APP_P10_SCAN_IRQ_HZ not defined in app_config.h"
#endif

// ---------------- internal geometry ----------------

#define P10_PANEL_W 32
#define P10_PANEL_H 16
#define P10_W (P10_PANEL_W * (APP_P10_CHAIN))
#define P10_H (P10_PANEL_H)

_Static_assert(P10_PANEL_H == 16, "Assumed 32x16 panels");

// ---------------- framebuffer: 1bpp, row bitmask ----------------

static volatile uint64_t g_fb[P10_H];

static inline void fb_clear(void)
{
  for (int y = 0; y < P10_H; ++y) g_fb[y] = 0;
}

static inline void fb_setpx(int x, int y, int on)
{
  if ((unsigned)x >= (unsigned)P10_W) return;
  if ((unsigned)y >= (unsigned)P10_H) return;
  uint64_t bit = (1ULL << (P10_W - 1 - x));
  if (on) g_fb[y] |= bit;
  else    g_fb[y] &= ~bit;
}

// ---------------- digits font (5x7) ----------------

// Each digit: 7 rows, 5 bits per row (MSB on the left)
static const uint8_t s_digit5x7[10][7] = {
  {0x1E,0x21,0x23,0x25,0x29,0x31,0x1E}, // 0
  {0x08,0x18,0x08,0x08,0x08,0x08,0x1C}, // 1
  {0x1E,0x21,0x01,0x06,0x18,0x20,0x3F}, // 2
  {0x3F,0x02,0x04,0x06,0x01,0x21,0x1E}, // 3
  {0x06,0x0A,0x12,0x22,0x3F,0x02,0x02}, // 4
  {0x3F,0x20,0x3E,0x01,0x01,0x21,0x1E}, // 5
  {0x0E,0x10,0x20,0x3E,0x21,0x21,0x1E}, // 6
  {0x3F,0x01,0x02,0x04,0x08,0x10,0x10}, // 7
  {0x1E,0x21,0x21,0x1E,0x21,0x21,0x1E}, // 8
  {0x1E,0x21,0x21,0x1F,0x01,0x02,0x1C}, // 9
};

static void draw_digit(int x0, int y0, int d, int scale)
{
  if (d < 0 || d > 9) return;
  for (int ry = 0; ry < 7; ++ry) {
    uint8_t row = s_digit5x7[d][ry];
    for (int rx = 0; rx < 5; ++rx) {
      int on = (row & (1U << (4 - rx))) != 0;
      for (int sy = 0; sy < scale; ++sy) {
        for (int sx = 0; sx < scale; ++sx) {
          fb_setpx(x0 + rx*scale + sx, y0 + ry*scale + sy, on);
        }
      }
    }
  }
}

// ---------------- layout / rendering ----------------

static void render_time(uint16_t minutes, uint16_t seconds)
{
  // MMM on left panel, (blank,S,S) on right panel, right-aligned
  // Leave 1 column left and right blank.
  const int left_pad = 1;
  const int right_pad = 1;

  // Digit geometry (scaled)
  const int scale = 1;
  const int DIGIT_W = 5 * scale;
  const int DIGIT_H = 7 * scale;
  const int SPACE   = 1 * scale;

  // Vertical centering
  const int y0 = (P10_H - DIGIT_H) / 2;

  fb_clear();

  // MMM
  if (minutes > 999) minutes = 999;
  int m2 = (minutes / 100) % 10;
  int m1 = (minutes / 10) % 10;
  int m0 = minutes % 10;

  int x_mmm = left_pad;
  draw_digit(x_mmm + 0*(DIGIT_W+SPACE), y0, m2, scale);
  draw_digit(x_mmm + 1*(DIGIT_W+SPACE), y0, m1, scale);
  draw_digit(x_mmm + 2*(DIGIT_W+SPACE), y0, m0, scale);

  // SS (right side) : only 2 digits, but we leave a blank digit-space before it
  if (seconds > 59) seconds = 59;
  int s1 = (seconds / 10) % 10;
  int s0 = seconds % 10;

  // Two digits width
  int ss_width = 2*DIGIT_W + 1*SPACE;
  int x_ss = (P10_W - right_pad) - ss_width;
  draw_digit(x_ss + 0*(DIGIT_W+SPACE), y0, s1, scale);
  draw_digit(x_ss + 1*(DIGIT_W+SPACE), y0, s0, scale);
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
  // Fast toggle using BSRR for less jitter
  port->BSRR = (uint32_t)pin;
  port->BSRR = (uint32_t)pin << 16;
}

static void p10_gpio_init(void)
{
  // Enable commonly used GPIO clocks (safe even if already enabled)
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

  // DATA1
  GPIO_InitStruct.Pin = APP_P10_DATA1_Pin;
  HAL_GPIO_Init(APP_P10_DATA1_GPIO_Port, &GPIO_InitStruct);
  // DATA2
  GPIO_InitStruct.Pin = APP_P10_DATA2_Pin;
  HAL_GPIO_Init(APP_P10_DATA2_GPIO_Port, &GPIO_InitStruct);
  // CLK
  GPIO_InitStruct.Pin = APP_P10_CLK_Pin;
  HAL_GPIO_Init(APP_P10_CLK_GPIO_Port, &GPIO_InitStruct);
  // LAT
  GPIO_InitStruct.Pin = APP_P10_LAT_Pin;
  HAL_GPIO_Init(APP_P10_LAT_GPIO_Port, &GPIO_InitStruct);
  // OE
  GPIO_InitStruct.Pin = APP_P10_OE_Pin;
  HAL_GPIO_Init(APP_P10_OE_GPIO_Port, &GPIO_InitStruct);
  // A
  GPIO_InitStruct.Pin = APP_P10_A_Pin;
  HAL_GPIO_Init(APP_P10_A_GPIO_Port, &GPIO_InitStruct);
  // B
  GPIO_InitStruct.Pin = APP_P10_B_Pin;
  HAL_GPIO_Init(APP_P10_B_GPIO_Port, &GPIO_InitStruct);

#if APP_P10_HAS_C
  GPIO_InitStruct.Pin = APP_P10_C_Pin;
  HAL_GPIO_Init(APP_P10_C_GPIO_Port, &GPIO_InitStruct);
#endif

  // Safe default levels
  gpio_write(APP_P10_DATA1_GPIO_Port, APP_P10_DATA1_Pin, GPIO_PIN_RESET);
  gpio_write(APP_P10_DATA2_GPIO_Port, APP_P10_DATA2_Pin, GPIO_PIN_RESET);
  gpio_write(APP_P10_CLK_GPIO_Port,   APP_P10_CLK_Pin,   GPIO_PIN_RESET);
  gpio_write(APP_P10_LAT_GPIO_Port,   APP_P10_LAT_Pin,   GPIO_PIN_RESET);

#if APP_P10_OE_ACTIVE_LOW
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_SET);   // disable display
#else
  gpio_write(APP_P10_OE_GPIO_Port, APP_P10_OE_Pin, GPIO_PIN_RESET);
#endif

  gpio_write(APP_P10_A_GPIO_Port, APP_P10_A_Pin, GPIO_PIN_RESET);
  gpio_write(APP_P10_B_GPIO_Port, APP_P10_B_Pin, GPIO_PIN_RESET);
#if APP_P10_HAS_C
  gpio_write(APP_P10_C_GPIO_Port, APP_P10_C_Pin, GPIO_PIN_RESET);
#endif
}

// ---------------- TIMER (TIM7) ----------------
// NOTE:
// Do NOT use preprocessor comparisons like:
//   #if (APP_P10_TIM_INSTANCE == TIM7)
// because TIM7 is a pointer macro ("((TIM_TypeDef*)TIM7_BASE)") and the preprocessor
// requires an *integral* constant expression. That construct breaks compilation.
//
// This driver currently initializes and uses TIM7 internally.
TIM_HandleTypeDef htim7;

static uint8_t g_tim_started = 0;

static void p10_tim_start(void)
{
  if (g_tim_started) return;

  // TIM7 clock enable
  __HAL_RCC_TIM7_CLK_ENABLE();

  RCC_ClkInitTypeDef clkconfig;
  uint32_t pFLatency;
  HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

  uint32_t uwTimclock;
  if (clkconfig.APB1CLKDivider == RCC_HCLK_DIV1) {
    uwTimclock = HAL_RCC_GetPCLK1Freq();
  } else {
    uwTimclock = 2UL * HAL_RCC_GetPCLK1Freq();
  }

  // 1MHz base
  uint32_t presc = (uwTimclock / 1000000U);
  if (presc == 0) presc = 1;
  presc -= 1U;

  uint32_t period = (1000000U / (uint32_t)APP_P10_SCAN_IRQ_HZ);
  if (period == 0) period = 1;
  period -= 1U;

  htim7.Instance = TIM7;
  htim7.Init.Prescaler = presc;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = period;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_Base_Init(&htim7) != HAL_OK) {
    // If timer init fails, we keep display blank.
    return;
  }

  // NVIC: priority must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
  HAL_NVIC_SetPriority(TIM7_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);

  (void)HAL_TIM_Base_Start_IT(&htim7);
  g_tim_started = 1;
}

// ---------------- scan engine ----------------

static volatile uint8_t g_scan_row = 0;

static inline void set_addr(uint8_t r)
{
  // A/B/C lines
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
  // 1) Blank
  oe_disable();

  // 2) Select row
  const uint8_t scan_rows = (APP_P10_HAS_C ? 8 : 4);
  uint8_t r = g_scan_row;
  if (r >= scan_rows) r = 0;
  set_addr(r);

  // 3) Shift out one row
  // For standard HUB12 mono: DATA1=upper half, DATA2=lower half.
  // If panel is 1/4 scan without C, we fold rows (OR) to cover full 16 rows.
  const int row_top = r;
  const int row_bot = r + 8;

  uint64_t bits_top = 0;
  uint64_t bits_bot = 0;

#if APP_P10_HAS_C
  bits_top = g_fb[row_top];
  bits_bot = g_fb[row_bot];
#else
  // fold: (0 with 4), (1 with 5), (2 with 6), (3 with 7) ...
  int y1 = row_top;
  int y2 = row_top + 4;
  int y3 = row_bot;
  int y4 = row_bot + 4;

  bits_top = (g_fb[y1] | g_fb[y2]);
  bits_bot = (g_fb[y3] | g_fb[y4]);
#endif

#if !APP_P10_SHIFT_MSB_FIRST
  // if LSB-first is needed, reverse bit order in 64-bit (simple loop)
  uint64_t rt = 0, rb = 0;
  for (int i = 0; i < P10_W; ++i) {
    rt <<= 1; rb <<= 1;
    rt |= (bits_top >> i) & 1ULL;
    rb |= (bits_bot >> i) & 1ULL;
  }
  bits_top = rt;
  bits_bot = rb;
#endif

  // Shift P10_W bits
  for (int i = 0; i < P10_W; ++i) {
    uint8_t b1 = (uint8_t)((bits_top >> (P10_W - 1 - i)) & 1ULL);
    uint8_t b2 = (uint8_t)((bits_bot >> (P10_W - 1 - i)) & 1ULL);

    gpio_write(APP_P10_DATA1_GPIO_Port, APP_P10_DATA1_Pin, b1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    gpio_write(APP_P10_DATA2_GPIO_Port, APP_P10_DATA2_Pin, b2 ? GPIO_PIN_SET : GPIO_PIN_RESET);

    pulse(APP_P10_CLK_GPIO_Port, APP_P10_CLK_Pin);
  }

  // 4) Latch
  pulse(APP_P10_LAT_GPIO_Port, APP_P10_LAT_Pin);

  // 5) Display enable
  oe_enable();

  // 6) Next row
  g_scan_row = (uint8_t)(r + 1);
  if (g_scan_row >= scan_rows) g_scan_row = 0;
}

// ---------------- public init/task ----------------

void APP_P10_Init(void)
{
  p10_gpio_init();
  fb_clear();
  // default screen: 000 / 00
  APP_P10_SetTime(0, 0);
}

void APP_P10_Task(void *argument)
{
  (void)argument;

  // Start scan timer
  p10_tim_start();

  // On boot, render current registers once
  uint16_t m=0, s=0;
  APP_RegsGetTime(&m, &s);
  APP_P10_SetTime(m, s);

  for (;;) {
    APP_SupervisorKick(APP_KICK_P10);
    osDelay(250);
  }
}
