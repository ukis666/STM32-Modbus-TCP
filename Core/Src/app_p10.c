#include "app_p10.h"
#include "app_config.h"
#include "cmsis_os.h"
#include "stm32f4xx_hal.h"
#include "app_supervisor.h"

/*
 * HUB12 sürüş: saha için önerilen yöntem TIM+DMA'dır.
 * Bu projede kart/panel erişimi olmadığından, derlenebilir ve kolay taşınabilir
 * bir "task tabanli" sürüş iskeleti verildi.
 *
 * Pinler: app_config.h
 */

static uint16_t g_minutes = 0;
static uint16_t g_seconds = 0;

static inline void gpio_write(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState s)
{
  HAL_GPIO_WritePin(port, pin, s);
}

static void gpio_pulse(GPIO_TypeDef* port, uint16_t pin)
{
  port->BSRR = pin;
  __NOP(); __NOP();
  port->BSRR = (uint32_t)pin << 16U;
}

static void p10_gpio_init(void)
{
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  GPIO_InitTypeDef gi = {0};
  gi.Mode = GPIO_MODE_OUTPUT_PP;
  gi.Pull = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gi.Pin = APP_P10_GPIO_LAT_PIN | APP_P10_GPIO_OE_PIN | APP_P10_GPIO_A_PIN | APP_P10_GPIO_B_PIN;
  HAL_GPIO_Init(GPIOD, &gi);

  gi.Pin = APP_P10_GPIO_CLK_PIN | APP_P10_GPIO_DATA_PIN;
  HAL_GPIO_Init(GPIOE, &gi);

  /* Safe state: OE=1 (disable), LAT=0, CLK=0, DATA=0, A/B=0 */
  gpio_write(APP_P10_GPIO_OE_PORT, APP_P10_GPIO_OE_PIN, GPIO_PIN_SET);
  gpio_write(APP_P10_GPIO_LAT_PORT, APP_P10_GPIO_LAT_PIN, GPIO_PIN_RESET);
  gpio_write(APP_P10_GPIO_CLK_PORT, APP_P10_GPIO_CLK_PIN, GPIO_PIN_RESET);
  gpio_write(APP_P10_GPIO_DATA_PORT, APP_P10_GPIO_DATA_PIN, GPIO_PIN_RESET);
  gpio_write(APP_P10_GPIO_A_PORT, APP_P10_GPIO_A_PIN, GPIO_PIN_RESET);
  gpio_write(APP_P10_GPIO_B_PORT, APP_P10_GPIO_B_PIN, GPIO_PIN_RESET);
}

void APP_P10_Init(void)
{
  p10_gpio_init();
}

void APP_P10_SetTime(uint16_t minutes, uint16_t seconds)
{
  g_minutes = minutes;
  g_seconds = seconds;
}

/* Basit demo: MMM:SS metnini panelin ust satirina kaydir.
 * Gercek HUB12 tarama ayrintilari sahada panel varyantina gore netlestirilmelidir.
 */
static void p10_shift_word(uint16_t bits)
{
  for (int i = 15; i >= 0; --i) {
    GPIO_PinState s = (bits & (1u << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    gpio_write(APP_P10_GPIO_DATA_PORT, APP_P10_GPIO_DATA_PIN, s);
    gpio_pulse(APP_P10_GPIO_CLK_PORT, APP_P10_GPIO_CLK_PIN);
  }
}

static void p10_latch(void)
{
  gpio_pulse(APP_P10_GPIO_LAT_PORT, APP_P10_GPIO_LAT_PIN);
}

static void p10_show_once(void)
{
  /* OE aktif low varsayilir: 0 = enable */
  gpio_write(APP_P10_GPIO_OE_PORT, APP_P10_GPIO_OE_PIN, GPIO_PIN_SET);

  /* Row address 0 */
  gpio_write(APP_P10_GPIO_A_PORT, APP_P10_GPIO_A_PIN, GPIO_PIN_RESET);
  gpio_write(APP_P10_GPIO_B_PORT, APP_P10_GPIO_B_PIN, GPIO_PIN_RESET);

  /* Format: MMM:SS -> 6 karakter. Burada sadece bir demo pattern basiyoruz.
   * Sahada font/bitmap eklenecek.
   */
  uint16_t m = g_minutes % 1000;
  uint16_t s = g_seconds % 60;
  /* 2 panel yan yana ise 64 bit gerekir; burada sadece 32 bitlik demo var.
   * Iki panel ayni goruntu (ayna) ise yeterli.
   */
  uint16_t w1 = (uint16_t)((m / 10) & 0xFFFF);
  uint16_t w2 = (uint16_t)((s) & 0xFFFF);

  p10_shift_word(w1);
  p10_shift_word(w2);
  p10_latch();

  gpio_write(APP_P10_GPIO_OE_PORT, APP_P10_GPIO_OE_PIN, GPIO_PIN_RESET);
  /* Kisa bir sure aktif tut */
  for (volatile int i = 0; i < 2000; ++i) { __NOP(); }
  gpio_write(APP_P10_GPIO_OE_PORT, APP_P10_GPIO_OE_PIN, GPIO_PIN_SET);
}

void APP_P10_Task(void *argument)
{
  (void)argument;
  for (;;) {
    p10_show_once();
    APP_SupervisorKick(APP_KICK_P10);
    osDelay(1);
  }
}
