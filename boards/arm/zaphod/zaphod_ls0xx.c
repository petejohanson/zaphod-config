/*
 * Copyright (c) 2020 Rohit Gujarathi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sharp memory LCD driver for the Zaphod, based on Zephyr's
 * drivers/display/ls0xx.c (which is turned off with CONFIG_LS0XX=n).
 *
 * Two differences from the Zephyr driver:
 *
 * - VCOM is toggled in software. The panel needs the polarity of its common
 *   electrode flipped about once a second, or the liquid crystal sees a DC
 *   voltage and pixels ghost, flicker and eventually burn in. Zephyr only
 *   does this through the EXTCOMIN pin, which the Zaphod doesn't connect, so
 *   here every command carries the VCOM bit and a timer sends a "hold"
 *   command that flips it twice a second.
 * - Chip select gets a setup/hold delay. The panel wants 6 us between CS
 *   rising and the first clock, and 2 us after the last; Zephyr uses 0.
 * - Updates are held back until the keys have been quiet for a moment. Rows
 *   sent while a key is going down or coming up can land on the wrong line
 *   (the display hangs off pogo pins, and the panel can't be read back to
 *   check), which left a stray row above the layer name after every layer
 *   change. The driver keeps its own copy of the screen, and sends the rows
 *   that changed once no key has moved for LS0XX_KEY_QUIET_MS.
 */

#define DT_DRV_COMPAT sharp_ls0xx

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zaphod_ls0xx, CONFIG_DISPLAY_LOG_LEVEL);

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

/* Note:
 * -> high/1 means white, low/0 means black
 * -> Display expects LSB first
 */

#define LS0XX_PANEL_WIDTH DT_INST_PROP(0, width)
#define LS0XX_PANEL_HEIGHT DT_INST_PROP(0, height)

#define LS0XX_PIXELS_PER_BYTE 8U
/* Adding 2 for the line number and dummy byte
 * line_buf format for each row.
 * +-------------------+-------------------+----------------+
 * | line num (8 bits) | data (WIDTH bits) | dummy (8 bits) |
 * +-------------------+-------------------+----------------+
 */
#define LS0XX_BYTES_PER_LINE ((LS0XX_PANEL_WIDTH / LS0XX_PIXELS_PER_BYTE) + 2)

#define LS0XX_BIT_WRITECMD 0x01
#define LS0XX_BIT_VCOM 0x02
#define LS0XX_BIT_CLEAR 0x04

/* Flip VCOM this often: a 1 Hz square wave, the panel's typical rate. */
#define LS0XX_VCOM_TOGGLE_MS 500

/* Send changed rows only once no key has moved for this long... */
#define LS0XX_KEY_QUIET_MS 120
/* ...but don't hold an update back longer than this while someone types. */
#define LS0XX_MAX_HOLD_MS 600

/* Microseconds between CS going high and the first clock, and between the
 * last clock and CS going low. */
#define LS0XX_CS_DELAY_US 10

struct ls0xx_config {
    struct spi_dt_spec bus;
#if DT_INST_NODE_HAS_PROP(0, disp_en_gpios)
    struct gpio_dt_spec disp_en_gpio;
#endif
};

/* One SPI transaction at a time: screen updates come from the display thread
 * and VCOM toggles from the system work queue. SPI_LOCK_ON doesn't keep them
 * apart because both use the same SPI config. */
static K_MUTEX_DEFINE(ls0xx_lock);
static uint8_t ls0xx_vcom;
static const struct device *ls0xx_dev;

/* What the screen should be showing, one row per line. */
static uint8_t ls0xx_shadow[LS0XX_PANEL_HEIGHT][LS0XX_PANEL_WIDTH / LS0XX_PIXELS_PER_BYTE];
/* Lines (numbered from 1) waiting to be sent; first == 0 means none. */
static uint16_t ls0xx_dirty_first;
static uint16_t ls0xx_dirty_last;
/* Uptime when the oldest waiting line was drawn, and when a key last moved. */
static int64_t ls0xx_dirty_since;
static int64_t ls0xx_last_key;

static int ls0xx_blanking_off(const struct device *dev) {
#if DT_INST_NODE_HAS_PROP(0, disp_en_gpios)
    const struct ls0xx_config *config = dev->config;

    return gpio_pin_set_dt(&config->disp_en_gpio, 1);
#else
    return -ENOTSUP;
#endif
}

static int ls0xx_blanking_on(const struct device *dev) {
#if DT_INST_NODE_HAS_PROP(0, disp_en_gpios)
    const struct ls0xx_config *config = dev->config;

    return gpio_pin_set_dt(&config->disp_en_gpio, 0);
#else
    return -ENOTSUP;
#endif
}

static int ls0xx_cmd(const struct device *dev, uint8_t *buf, uint8_t len) {
    const struct ls0xx_config *config = dev->config;
    struct spi_buf cmd_buf = {.buf = buf, .len = len};
    struct spi_buf_set buf_set = {.buffers = &cmd_buf, .count = 1};

    return spi_write_dt(&config->bus, &buf_set);
}

/* Send a two-byte command with no pixel data (clear, or just hold the image). */
static int ls0xx_short_cmd(const struct device *dev, uint8_t mode) {
    const struct ls0xx_config *config = dev->config;
    uint8_t cmd[2] = {mode, 0};
    int err;

    k_mutex_lock(&ls0xx_lock, K_FOREVER);
    cmd[0] |= ls0xx_vcom;
    err = ls0xx_cmd(dev, cmd, sizeof(cmd));
    spi_release_dt(&config->bus);
    k_mutex_unlock(&ls0xx_lock);

    return err;
}

static void ls0xx_vcom_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ls0xx_vcom_work, ls0xx_vcom_work_cb);

static void ls0xx_vcom_work_cb(struct k_work *work) {
    k_mutex_lock(&ls0xx_lock, K_FOREVER);
    ls0xx_vcom ^= LS0XX_BIT_VCOM;
    k_mutex_unlock(&ls0xx_lock);

    ls0xx_short_cmd(ls0xx_dev, 0);
    k_work_schedule(&ls0xx_vcom_work, K_MSEC(LS0XX_VCOM_TOGGLE_MS));
}

/* Send lines (numbered from 1) from the shadow copy. Call with the lock held. */
static int ls0xx_send_lines(const struct device *dev, uint16_t start_line, uint16_t num_lines) {
    const struct ls0xx_config *config = dev->config;
    uint8_t write_cmd[1] = {LS0XX_BIT_WRITECMD | ls0xx_vcom};
    uint8_t ln = start_line;
    uint8_t dummy = 0;
    struct spi_buf line_buf[3] = {
        {
            .len = sizeof(ln),
            .buf = &ln,
        },
        {
            .len = LS0XX_BYTES_PER_LINE - 2,
        },
        {
            .len = sizeof(dummy),
            .buf = &dummy,
        },
    };
    struct spi_buf_set line_set = {
        .buffers = line_buf,
        .count = ARRAY_SIZE(line_buf),
    };
    int err;

    LOG_DBG("Lines %d to %d", start_line, start_line + num_lines - 1);

    err = ls0xx_cmd(dev, write_cmd, sizeof(write_cmd));

    /* Send each line to the screen including
     * the line number and dummy bits
     */
    for (; ln <= start_line + num_lines - 1; ln++) {
        line_buf[1].buf = ls0xx_shadow[ln - 1];
        err |= spi_write_dt(&config->bus, &line_set);
    }

    /* Send another trailing 8 bits for the last line.
     * These can be any bits, it does not matter.
     */
    err |= ls0xx_cmd(dev, &dummy, sizeof(dummy));

    spi_release_dt(&config->bus);

    return err;
}

static void ls0xx_flush_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ls0xx_flush_work, ls0xx_flush_work_cb);

static void ls0xx_flush_work_cb(struct k_work *work) {
    int64_t now = k_uptime_get();
    int64_t quiet_for = now - ls0xx_last_key;

    if (quiet_for < LS0XX_KEY_QUIET_MS && now - ls0xx_dirty_since < LS0XX_MAX_HOLD_MS) {
        k_work_schedule(&ls0xx_flush_work, K_MSEC(LS0XX_KEY_QUIET_MS - quiet_for));
        return;
    }

    k_mutex_lock(&ls0xx_lock, K_FOREVER);
    if (ls0xx_dirty_first != 0) {
        ls0xx_send_lines(ls0xx_dev, ls0xx_dirty_first, ls0xx_dirty_last - ls0xx_dirty_first + 1);
        ls0xx_dirty_first = 0;
    }
    k_mutex_unlock(&ls0xx_lock);
}

static int ls0xx_update_display(const struct device *dev, uint16_t start_line, uint16_t num_lines,
                                const uint8_t *data) {
    uint16_t last_line = start_line + num_lines - 1;

    k_mutex_lock(&ls0xx_lock, K_FOREVER);
    memcpy(ls0xx_shadow[start_line - 1], data, num_lines * sizeof(ls0xx_shadow[0]));

    if (ls0xx_dirty_first == 0) {
        ls0xx_dirty_first = start_line;
        ls0xx_dirty_last = last_line;
        ls0xx_dirty_since = k_uptime_get();
    } else {
        ls0xx_dirty_first = MIN(ls0xx_dirty_first, start_line);
        ls0xx_dirty_last = MAX(ls0xx_dirty_last, last_line);
    }
    k_mutex_unlock(&ls0xx_lock);

    /* No delay when the keys are already quiet; the work still runs after
     * LVGL has handed over every part of this redraw. */
    k_work_schedule(&ls0xx_flush_work, K_NO_WAIT);

    return 0;
}

static int ls0xx_key_listener(const zmk_event_t *eh) {
    ls0xx_last_key = k_uptime_get();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zaphod_ls0xx, ls0xx_key_listener);
ZMK_SUBSCRIPTION(zaphod_ls0xx, zmk_position_state_changed);

/* Buffer width should be equal to display width */
static int ls0xx_write(const struct device *dev, const uint16_t x, const uint16_t y,
                       const struct display_buffer_descriptor *desc, const void *buf) {
    LOG_DBG("X: %d, Y: %d, W: %d, H: %d", x, y, desc->width, desc->height);

    if (buf == NULL) {
        LOG_WRN("Display buffer is not available");
        return -EINVAL;
    }

    if (desc->width != LS0XX_PANEL_WIDTH) {
        LOG_ERR("Width not a multiple of %d", LS0XX_PANEL_WIDTH);
        return -EINVAL;
    }

    if (desc->pitch != desc->width) {
        LOG_ERR("Unsupported mode");
        return -ENOTSUP;
    }

    if ((y + desc->height) > LS0XX_PANEL_HEIGHT) {
        LOG_ERR("Buffer out of bounds (height)");
        return -EINVAL;
    }

    if (x != 0) {
        LOG_ERR("X-coordinate has to be 0");
        return -EINVAL;
    }

    /* Adding 1 since line numbering on the display starts with 1 */
    return ls0xx_update_display(dev, y + 1, desc->height, buf);
}

static int ls0xx_read(const struct device *dev, const uint16_t x, const uint16_t y,
                      const struct display_buffer_descriptor *desc, void *buf) {
    return -ENOTSUP;
}

static void *ls0xx_get_framebuffer(const struct device *dev) { return NULL; }

static int ls0xx_set_brightness(const struct device *dev, const uint8_t brightness) {
    return -ENOTSUP;
}

static int ls0xx_set_contrast(const struct device *dev, uint8_t contrast) { return -ENOTSUP; }

static void ls0xx_get_capabilities(const struct device *dev, struct display_capabilities *caps) {
    memset(caps, 0, sizeof(struct display_capabilities));
    caps->x_resolution = LS0XX_PANEL_WIDTH;
    caps->y_resolution = LS0XX_PANEL_HEIGHT;
    caps->supported_pixel_formats = PIXEL_FORMAT_MONO01;
    caps->current_pixel_format = PIXEL_FORMAT_MONO01;
    caps->screen_info = SCREEN_INFO_X_ALIGNMENT_WIDTH;
}

static int ls0xx_set_orientation(const struct device *dev,
                                 const enum display_orientation orientation) {
    return -ENOTSUP;
}

static int ls0xx_set_pixel_format(const struct device *dev, const enum display_pixel_format pf) {
    if (pf == PIXEL_FORMAT_MONO01) {
        return 0;
    }

    return -ENOTSUP;
}

static int ls0xx_init(const struct device *dev) {
    const struct ls0xx_config *config = dev->config;

    if (!spi_is_ready_dt(&config->bus)) {
        LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
        return -ENODEV;
    }

#if DT_INST_NODE_HAS_PROP(0, disp_en_gpios)
    if (!gpio_is_ready_dt(&config->disp_en_gpio)) {
        LOG_ERR("DISP port device not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&config->disp_en_gpio, GPIO_OUTPUT_HIGH);
#endif

    ls0xx_dev = dev;
    k_work_schedule(&ls0xx_vcom_work, K_MSEC(LS0XX_VCOM_TOGGLE_MS));

    /* Clear display else it shows random data */
    return ls0xx_short_cmd(dev, LS0XX_BIT_CLEAR);
}

static const struct ls0xx_config ls0xx_config = {
    .bus = SPI_DT_SPEC_INST_GET(0,
                                SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_LSB |
                                    SPI_CS_ACTIVE_HIGH | SPI_HOLD_ON_CS | SPI_LOCK_ON,
                                LS0XX_CS_DELAY_US),
#if DT_INST_NODE_HAS_PROP(0, disp_en_gpios)
    .disp_en_gpio = GPIO_DT_SPEC_INST_GET(0, disp_en_gpios),
#endif
};

static struct display_driver_api ls0xx_driver_api = {
    .blanking_on = ls0xx_blanking_on,
    .blanking_off = ls0xx_blanking_off,
    .write = ls0xx_write,
    .read = ls0xx_read,
    .get_framebuffer = ls0xx_get_framebuffer,
    .set_brightness = ls0xx_set_brightness,
    .set_contrast = ls0xx_set_contrast,
    .get_capabilities = ls0xx_get_capabilities,
    .set_pixel_format = ls0xx_set_pixel_format,
    .set_orientation = ls0xx_set_orientation,
};

DEVICE_DT_INST_DEFINE(0, ls0xx_init, NULL, NULL, &ls0xx_config, POST_KERNEL,
                      CONFIG_DISPLAY_INIT_PRIORITY, &ls0xx_driver_api);
