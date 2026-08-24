#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

// Hardware Pins
#define POT_ADC_CHANNEL    ADC1_CHANNEL_6 // GPIO 34
#define DHT_PIN            GPIO_NUM_15
#define RELAY_PIN          GPIO_NUM_26
#define LED2_PWM_PIN       GPIO_NUM_25
#define SERVO_PWM_PIN      GPIO_NUM_4

// Keypad GPIO definitions matching diagram.json
#define ROW1 GPIO_NUM_18
#define ROW2 GPIO_NUM_19
#define ROW3 GPIO_NUM_5
#define ROW4 GPIO_NUM_17

#define COL1 GPIO_NUM_16
#define COL2 GPIO_NUM_14
#define COL3 GPIO_NUM_12
#define COL4 GPIO_NUM_13

#define I2C_MASTER_SDA_IO 22
#define I2C_MASTER_SCL_IO 23
#define I2C_MASTER_NUM    0
#define LCD_ADDR          0x27 // Change to 0x3F if your LCD remains blank

// Target Modes
#define TARGET_EXPORT     5
#define TARGET_LOCAL      25

static volatile int targetMoisture = TARGET_EXPORT;

// PWM Configuration
#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_LED     LEDC_CHANNEL_0
#define LEDC_CHANNEL_SERVO   LEDC_CHANNEL_1

static void delay_us(uint32_t us) {
    uint64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < us);
}

// I2C & LCD Helpers
static esp_err_t lcd_send_cmd(uint8_t cmd) {
    uint8_t u = (cmd & 0xF0), l = ((cmd << 4) & 0xF0);
    uint8_t buf[4] = { u | 0x0C, u | 0x08, l | 0x0C, l | 0x08 };
    return i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, buf, 4, pdMS_TO_TICKS(50));
}

static esp_err_t lcd_send_data(uint8_t data) {
    uint8_t u = (data & 0xF0), l = ((data << 4) & 0xF0);
    uint8_t buf[4] = { u | 0x0D, u | 0x09, l | 0x0D, l | 0x09 };
    return i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, buf, 4, pdMS_TO_TICKS(50));
}

static void lcd_init(void) {
    vTaskDelay(pdMS_TO_TICKS(100));
    lcd_send_cmd(0x30); vTaskDelay(pdMS_TO_TICKS(10));
    lcd_send_cmd(0x30); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_send_cmd(0x32);
    lcd_send_cmd(0x28); // 4-bit, 2 lines
    lcd_send_cmd(0x0C); // Display ON, Cursor OFF
    lcd_send_cmd(0x06); // Entry mode
    lcd_send_cmd(0x01); // Clear Screen
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void lcd_set_cursor(uint8_t row, uint8_t col) {
    lcd_send_cmd((row == 0) ? (0x80 + col) : (0xC0 + col));
}

static void lcd_print(const char *str) {
    while (*str) lcd_send_data(*str++);
}

// DHT22 Driver
static bool read_dht22(float *temp, float *humidity) {
    uint8_t data[5] = {0};
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(DHT_PIN, 1);
    delay_us(30);
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    uint32_t timeout = 0;
    while (gpio_get_level(DHT_PIN) == 1) if (++timeout > 20000) return false;
    timeout = 0;
    while (gpio_get_level(DHT_PIN) == 0) if (++timeout > 20000) return false;
    timeout = 0;
    while (gpio_get_level(DHT_PIN) == 1) if (++timeout > 20000) return false;

    for (int i = 0; i < 40; i++) {
        timeout = 0;
        while (gpio_get_level(DHT_PIN) == 0) if (++timeout > 20000) return false;
        uint64_t t = esp_timer_get_time();
        timeout = 0;
        while (gpio_get_level(DHT_PIN) == 1) if (++timeout > 20000) return false;
        if ((esp_timer_get_time() - t) > 40) data[i / 8] |= (1 << (7 - (i % 8)));
    }

    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) return false;
    *humidity = ((data[0] << 8) | data[1]) * 0.1f;
    int16_t raw_temp = ((data[2] & 0x7F) << 8) | data[3];
    if (data[2] & 0x80) raw_temp = -raw_temp;
    *temp = raw_temp * 0.1f;
    return true;
}

// Keypad Scanner
static void keypad_init(void) {
    gpio_num_t rows[] = {ROW1, ROW2, ROW3, ROW4};
    gpio_num_t cols[] = {COL1, COL2, COL3, COL4};

    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(rows[i]);
        gpio_set_direction(rows[i], GPIO_MODE_OUTPUT);
        gpio_set_level(rows[i], 1);
    }
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(cols[i]);
        gpio_set_direction(cols[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(cols[i], GPIO_PULLUP_ENABLE);
    }
}

static char read_keypad(void) {
    gpio_num_t rows[] = {ROW1, ROW2, ROW3, ROW4};
    gpio_num_t cols[] = {COL1, COL2, COL3, COL4};
    char keys[4][4] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'}
    };

    for (int r = 0; r < 4; r++) {
        gpio_set_level(rows[r], 0);
        vTaskDelay(pdMS_TO_TICKS(2));
        for (int c = 0; c < 4; c++) {
            if (gpio_get_level(cols[c]) == 0) {
                gpio_set_level(rows[r], 1);
                vTaskDelay(pdMS_TO_TICKS(50));
                return keys[r][c];
            }
        }
        gpio_set_level(rows[r], 1);
    }
    return '\0';
}

void app_main(void) {
    // I2C Init
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(200));
    lcd_init();
    keypad_init();

    // Digital GPIOs
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 0);

    // ADC Pin Setup
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(POT_ADC_CHANNEL, ADC_ATTEN_DB_12);

    // LED PWM Setup
    ledc_timer_config_t led_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&led_timer);

    ledc_channel_config_t led_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_LED,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = LED2_PWM_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&led_channel);

    // Servo PWM Setup
    ledc_timer_config_t servo_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&servo_timer);

    ledc_channel_config_t servo_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_SERVO,
        .timer_sel = LEDC_TIMER_1,
        .gpio_num = SERVO_PWM_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&servo_channel);

    char input_buf[3] = {0};
    uint8_t buf_idx = 0;
    char buf[32];

    while (1) {
        // 1. Process Keypad Input
        char key = read_keypad();
        if (key != '\0') {
            if (key >= '0' && key <= '9') {
                if (buf_idx < 2) {
                    input_buf[buf_idx++] = key;
                    input_buf[buf_idx] = '\0';
                }
            } else if (key == '#') {
                if (buf_idx > 0) {
                    int val = atoi(input_buf);
                    if (val >= 5 && val <= 50) {
                        targetMoisture = val;
                    }
                    buf_idx = 0;
                    input_buf[0] = '\0';
                }
            } else if (key == '*') {
                buf_idx = 0;
                input_buf[0] = '\0';
            }
        }

        // 2. Read Sensors
        float temp = 33.1f, humidity = 50.0f;
        read_dht22(&temp, &humidity);

        int raw_adc = adc1_get_raw(POT_ADC_CHANNEL);
        float currentMoisture = 5.0f + ((float)raw_adc / 4095.0f) * 45.0f;

        // 3. Actuator Control & LCD Output
        if (currentMoisture > targetMoisture) {
            gpio_set_level(RELAY_PIN, 1);

            float factor = (50.0f - temp) / 30.0f;
            if (factor > 1.0f) factor = 1.0f;
            if (factor < 0.15f) factor = 0.15f;

            uint32_t ledDuty = (uint32_t)(factor * 255.0f);
            uint32_t servoDuty = 25 + (uint32_t)(factor * 100.0f);

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LED, ledDuty);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LED);

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_SERVO, servoDuty);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_SERVO);

            lcd_set_cursor(0, 0);
            if (buf_idx > 0) {
                snprintf(buf, sizeof(buf), "M:%2.0f%% Set:%s_   ", currentMoisture, input_buf);
            } else {
                snprintf(buf, sizeof(buf), "M:%2.0f%% T:%2d%%   ", currentMoisture, targetMoisture);
            }
            lcd_print(buf);

            lcd_set_cursor(1, 0);
            snprintf(buf, sizeof(buf), "Fan:%3d%% T:%.1fC", (int)(factor * 100.0f), temp);
            lcd_print(buf);
        } else {
            gpio_set_level(RELAY_PIN, 0);

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LED, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LED);

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_SERVO, 25);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_SERVO);

            lcd_set_cursor(0, 0);
            if (buf_idx > 0) {
                snprintf(buf, sizeof(buf), "M:%2.0f%% Set:%s_   ", currentMoisture, input_buf);
            } else {
                snprintf(buf, sizeof(buf), "M:%2.0f%% T:%2d%%   ", currentMoisture, targetMoisture);
            }
            lcd_print(buf);

            lcd_set_cursor(1, 0);
            lcd_print("Status: READY!  ");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
