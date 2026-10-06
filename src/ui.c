#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "irk_store.h"
#include "ui.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define HAS_LED 1
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;
static struct k_work_delayable clear_work;
#define HAS_BUTTON 1
#endif

/* LED patterns: advertising and not connected = short pulse once a second, connected = solid,
 * IRK captured = a few fast blinks on top of either.
 */
#define HEARTBEAT_ON_MS  100
#define HEARTBEAT_OFF_MS 900

static struct k_work_delayable blink_work;
static struct k_work_delayable heartbeat_work;
static bool connected;
static bool advertising;
static bool heartbeat_on;
static int blinks_left;

static void led_set(bool on)
{
#ifdef HAS_LED
	gpio_pin_set_dt(&led, on);
#endif
}

static void heartbeat_handler(struct k_work *work)
{
	if (connected || !advertising) {
		heartbeat_on = false;
		if (blinks_left == 0) {
			led_set(connected);
		}
		return;
	}

	heartbeat_on = !heartbeat_on;
	if (blinks_left == 0) {
		led_set(heartbeat_on);
	}
	k_work_schedule(&heartbeat_work, K_MSEC(heartbeat_on ? HEARTBEAT_ON_MS : HEARTBEAT_OFF_MS));
}

static void blink_handler(struct k_work *work)
{
	if (blinks_left == 0) {
		led_set(connected || (advertising && heartbeat_on));
		return;
	}

	/* Even remaining counts are "off" phases. */
	led_set(blinks_left % 2);
	blinks_left--;
	k_work_schedule(&blink_work, K_MSEC(120));
}

void ui_set_connected(bool is_connected)
{
	connected = is_connected;
	k_work_reschedule(&heartbeat_work, K_NO_WAIT);
}

void ui_set_advertising(bool is_advertising)
{
	advertising = is_advertising;
	k_work_reschedule(&heartbeat_work, K_NO_WAIT);
}

void ui_irk_captured(void)
{
	blinks_left = 6;
	k_work_schedule(&blink_work, K_NO_WAIT);
}

#ifdef HAS_BUTTON
static void clear_handler(struct k_work *work)
{
	LOG_INF("Button held, clearing all bonds");
	irk_store_clear_all();
	ui_irk_captured();
}

static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (gpio_pin_get_dt(&button)) {
		k_work_schedule(&clear_work, K_MSEC(UI_CLEAR_HOLD_MS));
	} else {
		k_work_cancel_delayable(&clear_work);
	}
}
#endif

void ui_init(void)
{
	k_work_init_delayable(&blink_work, blink_handler);
	k_work_init_delayable(&heartbeat_work, heartbeat_handler);

#ifdef HAS_LED
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}
#endif

#ifdef HAS_BUTTON
	k_work_init_delayable(&clear_work, clear_handler);
	if (gpio_is_ready_dt(&button)) {
		gpio_pin_configure_dt(&button, GPIO_INPUT);
		gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
		gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
		gpio_add_callback(button.port, &button_cb_data);
	}
#endif
}
