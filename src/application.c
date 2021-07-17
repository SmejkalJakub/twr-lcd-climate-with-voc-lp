#include <application.h>

#define BATTERY_UPDATE_INTERVAL (7 * 60 * 60 * 1000) // 7 hodin
#define BATTERY_UPDATE_SERVICE_INTERVAL (10 * 60 * 1000) // 10 minut
#define BATTERY_UPDATE_INITIAL_INTERVAL (60 * 1000) // 1 minuta

#define SERVICE_INTERVAL_INTERVAL (10 * 60 * 1000)

#define TMP112_UPDATE_INTERVAL (2000)
#define VOC_LP_TAG_UPDATE_INTERVAL (2000)
#define HUMIDITY_TAG_UPDATE_INTERVAL (2000)

#define TEMPERATURE_TAG_PUB_VALUE_CHANGE 0.5f
#define HUMIDITY_TAG_PUB_VALUE_CHANGE 2
#define VOC_TAG_PUB_VALUE_CHANGE 1000

#define RADIO_SEND_INTERVAL 30 * 1000               // 30 sekund

#define MAX_PAGE_INDEX 2

#define PAGE_INDEX_MENU -1

twr_led_t led;
bool led_state = false;

bool active_mode = true;

twr_gfx_t *gfx;

bool first_battery_send = true;

static struct
{
    float_t temperature;
    float_t humidity;
    float_t tvoc;
    float_t battery_voltage;
    float_t battery_pct;

} values;

static const struct
{
    char *name0;
    char *format0;
    float_t *value0;
    char *unit0;
    char *name1;
    char *format1;
    float_t *value1;
    char *unit1;

} pages[] = {
    {"Temperature   ", "%.1f", &values.temperature, "\xb0" "C",
     "Humidity      ", "%.1f", &values.humidity, "%"},
    {"TVOC          ", "%.1f", &values.tvoc, "ppb"},
    {"Battery       ", "%.2f", &values.battery_voltage, "V",
     "Battery       ", "%.0f", &values.battery_pct, "%"},
};

static int page_index = 0;
static int menu_item = 0;

twr_led_t led_lcd_green;

static struct
{
    twr_tick_t next_update;
    bool mqtt;

} lcd;

void battery_event_handler(twr_module_battery_event_t event, void *event_param);

static void lcd_page_render();
static void humidity_tag_init(twr_tag_humidity_revision_t revision, twr_i2c_channel_t i2c_channel, humidity_tag_t *tag);

void lcd_event_handler(twr_module_lcd_event_t event, void *event_param);
void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param);
void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param);
void voc_lp_tag_event_handler(twr_tag_voc_lp_t *self, twr_tag_voc_lp_event_t event, void *event_param);

static void lcd_page_render()
{
    int w;
    char str[32];

    twr_system_pll_enable();

    twr_gfx_clear(gfx);

    if ((page_index <= MAX_PAGE_INDEX) && (page_index != PAGE_INDEX_MENU))
    {
        twr_gfx_set_font(gfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(gfx, 10, 5, pages[page_index].name0, true);

        twr_gfx_set_font(gfx, &twr_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format0, *pages[page_index].value0);
        w = twr_gfx_draw_string(gfx, 25, 25, str, true);
        twr_gfx_set_font(gfx, &twr_font_ubuntu_15);
        w = twr_gfx_draw_string(gfx, w, 35, pages[page_index].unit0, true);

        twr_gfx_set_font(gfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(gfx, 10, 55, pages[page_index].name1, true);

        twr_gfx_set_font(gfx, &twr_font_ubuntu_28);
        snprintf(str, sizeof(str), pages[page_index].format1, *pages[page_index].value1);
        w = twr_gfx_draw_string(gfx, 25, 75, str, true);
        twr_gfx_set_font(gfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(gfx, w, 85, pages[page_index].unit1, true);
    }

    snprintf(str, sizeof(str), "%d/%d", page_index + 1, MAX_PAGE_INDEX + 1);
    twr_gfx_set_font(gfx, &twr_font_ubuntu_13);
    twr_gfx_draw_string(gfx, 55, 115, str, true);

    twr_system_pll_disable();
}

static void humidity_tag_init(twr_tag_humidity_revision_t revision, twr_i2c_channel_t i2c_channel, humidity_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    if (revision == TWR_TAG_HUMIDITY_REVISION_R1)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == TWR_TAG_HUMIDITY_REVISION_R2)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R2_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == TWR_TAG_HUMIDITY_REVISION_R3)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT;
    }
    else
    {
        return;
    }

    if (i2c_channel == TWR_I2C_I2C1)
    {
        tag->param.channel |= 0x80;
    }

    twr_tag_humidity_init(&tag->self, revision, i2c_channel, TWR_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);

    twr_tag_humidity_set_update_interval(&tag->self, HUMIDITY_TAG_UPDATE_INTERVAL);

    twr_tag_humidity_set_event_handler(&tag->self, humidity_tag_event_handler, &tag->param);
}

void lcd_event_handler(twr_module_lcd_event_t event, void *event_param)
{
    (void) event_param;

    if (event == TWR_MODULE_LCD_EVENT_LEFT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU))
        {
            // Key previous page
            page_index--;
            if (page_index < 0)
            {
                page_index = MAX_PAGE_INDEX;
                menu_item = 0;
            }
        }
        else
        {
            // Key menu down
            menu_item++;
            if (menu_item > 4)
            {
                menu_item = 0;
            }
        }

        static uint16_t left_event_count = 0;
        left_event_count++;
    }
    else if(event == TWR_MODULE_LCD_EVENT_BOTH_HOLD)
    {
        active_mode = !active_mode;
        if(active_mode)
        {
            first_battery_send = true;
            twr_module_battery_set_update_interval(BATTERY_UPDATE_INITIAL_INTERVAL);
            twr_scheduler_plan_now(0);
        }
        else
        {
            twr_gfx_clear(gfx);
            twr_gfx_update(gfx);
        }
    }
    else if(event == TWR_MODULE_LCD_EVENT_RIGHT_CLICK)
    {
        if ((page_index != PAGE_INDEX_MENU) || (menu_item == 0))
        {
            // Key next page
            page_index++;
            if (page_index > MAX_PAGE_INDEX)
            {
                page_index = 0;
            }
            if (page_index == PAGE_INDEX_MENU)
            {
                menu_item = 0;
            }
        }

        static uint16_t right_event_count = 0;
        right_event_count++;
    }
    else if(event == TWR_MODULE_LCD_EVENT_LEFT_HOLD)
    {
        static int left_hold_event_count = 0;
        left_hold_event_count++;

        twr_led_pulse(&led_lcd_green, 100);
    }
    else if(event == TWR_MODULE_LCD_EVENT_RIGHT_HOLD)
    {
        static int right_hold_event_count = 0;
        right_hold_event_count++;

        twr_led_pulse(&led_lcd_green, 100);

    }
    else if(event == TWR_MODULE_LCD_EVENT_BOTH_HOLD)
    {
        static int both_hold_event_count = 0;
        both_hold_event_count++;

        twr_led_pulse(&led_lcd_green, 100);
    }

    twr_scheduler_plan_now(0);
}


void voc_lp_tag_event_handler(twr_tag_voc_lp_t *self, twr_tag_voc_lp_event_t event, void *event_param)
{
    event_param_t *param = (event_param_t *)event_param;

    if (event == TWR_TAG_VOC_LP_EVENT_UPDATE)
    {
        uint16_t value;

        if (twr_tag_voc_lp_get_tvoc_ppb(self, &value))
        {
            param->value = value;

            if ((fabs(value - values.tvoc) >= VOC_TAG_PUB_VALUE_CHANGE))
            {
                int radio_tvoc = value;

                twr_radio_pub_int("voc-lp-sensor/0:0/tvoc", &radio_tvoc);
            }

            int radio_tvoc = value;

            values.tvoc = radio_tvoc;

            twr_scheduler_plan_now(0);
        }
    }
}

void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != TWR_TMP112_EVENT_UPDATE)
    {
        return;
    }

    if (twr_tmp112_get_temperature_celsius(self, &value))
    {
        param->value = value;

        if ((fabs(value - values.temperature) >= TEMPERATURE_TAG_PUB_VALUE_CHANGE))
        {
            twr_radio_pub_temperature(0, &value);
        }
        values.temperature = value;
        twr_scheduler_plan_now(0);
    }
}

void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != TWR_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (twr_tag_humidity_get_humidity_percentage(self, &value))
    {
        param->value = value;

        if ((fabs(value - values.humidity) >= HUMIDITY_TAG_PUB_VALUE_CHANGE))
        {
            twr_radio_pub_humidity(0, &value);
        }

        values.humidity = value;
        twr_scheduler_plan_now(0);
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;
    int percentage;

    if(event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (twr_module_battery_get_voltage(&voltage) && active_mode)
        {
            if(first_battery_send)
            {
                twr_module_battery_set_update_interval(BATTERY_UPDATE_SERVICE_INTERVAL);
                twr_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);
            }
            values.battery_voltage = voltage;
            twr_radio_pub_battery(&values.battery_voltage);
        }

        if (twr_module_battery_get_charge_level(&percentage))
        {
            values.battery_pct = percentage;
        }
    }
}

void send_data_over_radio()
{
    if(active_mode)
    {
        twr_radio_pub_temperature(0, &values.temperature);

        int radio_tvoc = values.tvoc;
        twr_radio_pub_int("voc-lp-sensor/0:0/tvoc", &radio_tvoc);

        twr_radio_pub_humidity(0, &values.humidity);
    }
    twr_scheduler_plan_current_from_now(RADIO_SEND_INTERVAL);
}

void switch_to_normal_mode_task(void *param)
{
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);
    twr_scheduler_unregister(twr_scheduler_get_current_task_id());
}

void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    twr_radio_init(TWR_RADIO_MODE_NODE_SLEEPING);

    // Temperature
    static twr_tmp112_t temperature;
    static event_param_t temperature_event_param = { .next_pub = 0, .channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_ALTERNATE };
    twr_tmp112_init(&temperature, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&temperature, tmp112_event_handler, &temperature_event_param);
    twr_tmp112_set_update_interval(&temperature, TMP112_UPDATE_INTERVAL);

    // Hudmidity
    static humidity_tag_t humidity_tag_0_0;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R1, TWR_I2C_I2C0, &humidity_tag_0_0);

    static humidity_tag_t humidity_tag_0_2;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R2, TWR_I2C_I2C0, &humidity_tag_0_2);

    static humidity_tag_t humidity_tag_0_4;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C0, &humidity_tag_0_4);

    static humidity_tag_t humidity_tag_1_0;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R1, TWR_I2C_I2C1, &humidity_tag_1_0);

    static humidity_tag_t humidity_tag_1_2;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R2, TWR_I2C_I2C1, &humidity_tag_1_2);

    static humidity_tag_t humidity_tag_1_4;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C1, &humidity_tag_1_4);

    // VOC-LP
    static twr_tag_voc_lp_t voc_lp;
    static event_param_t voc_lp_event_param = { .next_pub = 0 };
    twr_tag_voc_lp_init(&voc_lp, TWR_I2C_I2C0);
    twr_tag_voc_lp_set_event_handler(&voc_lp, voc_lp_tag_event_handler, &voc_lp_event_param);
    twr_tag_voc_lp_set_update_interval(&voc_lp, VOC_LP_TAG_UPDATE_INTERVAL);

    // LCD
    twr_module_lcd_init();
    twr_module_lcd_set_event_handler(lcd_event_handler, NULL);
    gfx = twr_module_lcd_get_gfx();
    twr_module_lcd_set_button_hold_time(1000);

    // Battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_SERVICE_INTERVAL);

    twr_radio_pairing_request("air-quality-monitor", VERSION);

    twr_led_pulse(&led, 2000);

    twr_scheduler_register(send_data_over_radio, NULL, RADIO_SEND_INTERVAL);
    twr_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);

}

void application_task(void)
{
    if (!twr_gfx_display_is_ready(gfx))
    {
        return;
    }

    if (!lcd.mqtt && active_mode)
    {
        lcd_page_render();
    }
    else
    {
        twr_scheduler_plan_current_relative(500);
    }

    twr_gfx_update(gfx);
}
