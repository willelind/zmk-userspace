/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT elpekenin_behavior_default_layer

#include <zephyr/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(elpekenin, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>

#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>


struct default_layer_settings_t {
    uint8_t usb[ZMK_ENDPOINT_USB_COUNT];
    uint8_t ble[ZMK_ENDPOINT_BLE_COUNT];
};

static struct default_layer_settings_t default_layers = {0};

static int save_default_layer_setting(uint8_t layer, struct zmk_endpoint_instance endpoint) {
    if (layer >= ZMK_KEYMAP_LAYERS_LEN) {
        return -EINVAL;
    }

    switch (endpoint.transport) {
        case ZMK_TRANSPORT_USB:
            __ASSERT(ZMK_ENDPOINT_USB_COUNT == 1, "Unreachable");
            default_layers.usb[0] = layer;
            break;

        case ZMK_TRANSPORT_BLE:
            __ASSERT(endpoint.ble.profile_index < ZMK_ENDPOINT_BLE_COUNT, "Unreachable");
            default_layers.ble[endpoint.ble.profile_index] = layer;
            break;
    }

    int ret = settings_save_one("default_layer/settings", &default_layers, sizeof(default_layers));
    if (ret < 0) {
        LOG_WRN("Could not update the settings.");
        return ret;
    }

    if (endpoint.transport == ZMK_TRANSPORT_USB) {
        LOG_INF("Updated default layer (%d) for USB endpoint.", layer);
    } else {
        LOG_INF("Updated default layer (%d) for BLE endpoint %d.", layer, endpoint.ble.profile_index);
    }
    return 0;
}

// TODO: Use default layer setter when (if) zmk/#2222 gets merged
static int apply_default_layer_config(struct zmk_endpoint_instance endpoint) {
    uint8_t layer = 0;

    switch (endpoint.transport) {
        case ZMK_TRANSPORT_USB:
            __ASSERT(ZMK_ENDPOINT_USB_COUNT == 1, "Unreachable");
            layer = default_layers.usb[0];
            break;

        case ZMK_TRANSPORT_BLE:
            __ASSERT(endpoint.ble.profile_index < ZMK_ENDPOINT_BLE_COUNT, "Unreachable");
            layer = default_layers.ble[endpoint.ble.profile_index];
            break;
    }

    int ret = zmk_keymap_layer_to(layer);
    if (ret < 0) {
        LOG_WRN("Could not apply default layer from settings. Perhaps number of layers changed since configuration was saved.");
        return ret;
    }

    LOG_INF("Activated default layer (%d) for the current endpoint.", layer);
    return 0;
}

static int default_layer_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) { 
    const char *next;
    int rc;

    if (settings_name_steq(name, "settings", &next) && !next) {
        if (len != sizeof(default_layers)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &default_layers, sizeof(default_layers));
        if (rc >= 0) {
            return 0;
        }

        return rc;
    }

    return -ENOENT;
}

struct settings_handler default_layer_conf = {
    .name = "default_layer",
    .h_set = default_layer_set,
};

static int default_layer_init(void) {
    settings_subsys_init();

    int ret = settings_register(&default_layer_conf);
    if (ret) {
        LOG_ERR("Could not register default layer settings (%d).", ret);
        return ret;
    }

    settings_load_subtree("default_layer");

    return apply_default_layer_config(zmk_endpoints_selected());
}
SYS_INIT(default_layer_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

// ^ configuration-related code
// -----
// v behavior

static int behavior_default_layer_init(const struct device *dev) {
    return 0; // no-op
}

static int on_keymap_binding_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event
) {
    int ret = 0;
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();

    ret = save_default_layer_setting(binding->param1, endpoint);
    if (ret < 0) {
        return ret;
    }

    ret = apply_default_layer_config(endpoint);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int on_keymap_binding_released(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event
) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_default_layer_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(
    0,
    behavior_default_layer_init,
    NULL,
    NULL,
    NULL,
    POST_KERNEL,
    CONFIG_KSCAN_INIT_PRIORITY,
    &behavior_default_layer_driver_api
);

// ^ behavior
// -----
// v listener for endpoint changes

static int endpoint_changed_cb(const zmk_event_t *eh) {
    struct zmk_endpoint_changed *evt = as_zmk_endpoint_changed(eh);

    if (evt != NULL) {
        apply_default_layer_config(evt->endpoint);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(endpoint, endpoint_changed_cb);
ZMK_SUBSCRIPTION(endpoint, zmk_endpoint_changed);
