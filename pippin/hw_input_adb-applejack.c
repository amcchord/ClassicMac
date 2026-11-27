/*
 * Pippin ADB AppleJack controller emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/core/qdev-properties.h"
#include "hw/input/adb.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "ui/input.h"
#include "adb-internal.h"

#define APPLEJACK_DEFAULT_HANDLER_ID 0x01
#define APPLEJACK_HANDLER_ID         0x46

/*
 * AppleJack 18-bit internal button state (active high, matching AppleJack
 * hardware Talk Register 0 bit positions). Bits 0..15 are sent inverted in
 * Talk Register 0 bytes 2..3; triggers are also reflected in bytes 0..1 bit 7.
 */
enum {
    AJ_BTN_SQUARE      = 1U << 0,
    AJ_BTN_CIRCLE      = 1U << 1,
    AJ_BTN_TRIANGLE    = 1U << 2,
    AJ_BTN_BLUE        = 1U << 8,   /* top diamond button */
    AJ_BTN_YELLOW      = 1U << 9,   /* left diamond button */
    AJ_BTN_DPAD_UP     = 1U << 10,
    AJ_BTN_DPAD_LEFT   = 1U << 11,
    AJ_BTN_DPAD_RIGHT  = 1U << 12,
    AJ_BTN_DPAD_DOWN   = 1U << 13,
    AJ_BTN_RED         = 1U << 14,  /* bottom diamond button */
    AJ_BTN_GREEN       = 1U << 15,  /* right diamond button */
    AJ_BTN_RTRIGGER    = 1U << 16,  /* right ear trigger -> button 1 */
    AJ_BTN_LTRIGGER    = 1U << 17,  /* left ear trigger -> button 0 */
    AJ_BTN_VALID_MASK  = 0x3FF07U,
};

#define TYPE_ADB_APPLEJACK "adb-applejack"
OBJECT_DECLARE_TYPE(ADBAppleJackState, ADBAppleJackClass, ADB_APPLEJACK)

struct ADBAppleJackState {
    /*< private >*/
    ADBDevice parent_obj;

    /*< public >*/
    QemuInputHandlerState *mouse_hs;
    int dx, dy;
    bool mouse_btns[2];
    bool last_mouse_btns[2];
    uint32_t aj_buttons;
    bool buttons_changed;
    bool triggers_changed;
    bool has_mouse_data;
};

struct ADBAppleJackClass {
    /*< public >*/
    ADBDeviceClass parent_class;

    /*< private >*/
    DeviceRealize parent_realize;
};

static void applejack_mouse_event(DeviceState *dev, QemuConsole *src,
                                  InputEvent *evt)
{
    ADBAppleJackState *s = ADB_APPLEJACK(dev);

    adb_input_activity(ADB_BUS(qdev_get_parent_bus(dev)));

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL: {
        InputMoveEvent *move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            s->dx += move->value;
            s->has_mouse_data = true;
        } else if (move->axis == INPUT_AXIS_Y) {
            s->dy += move->value;
            s->has_mouse_data = true;
        }
        break;
    }
    case INPUT_EVENT_KIND_BTN: {
        InputBtnEvent *btn = evt->u.btn.data;
        if (btn->button == INPUT_BUTTON_LEFT) {
            s->mouse_btns[0] = btn->down;
            s->has_mouse_data = true;
        } else if (btn->button == INPUT_BUTTON_RIGHT) {
            s->mouse_btns[1] = btn->down;
            s->has_mouse_data = true;
        }
        break;
    }
    default:
        break;
    }
}

static bool applejack_adb_has_data(ADBDevice *d)
{
    ADBAppleJackState *s = ADB_APPLEJACK(d);

    return s->dx != 0 || s->dy != 0 ||
           s->mouse_btns[0] != s->last_mouse_btns[0] ||
           s->mouse_btns[1] != s->last_mouse_btns[1] ||
           s->has_mouse_data || s->triggers_changed ||
           (d->handler == APPLEJACK_HANDLER_ID && s->buttons_changed);
}

static int applejack_adb_poll(ADBDevice *d, uint8_t *obuf)
{
    ADBAppleJackState *s = ADB_APPLEJACK(d);
    bool b0 = s->mouse_btns[0] || (s->aj_buttons & AJ_BTN_LTRIGGER);
    bool b1 = s->mouse_btns[1] || (s->aj_buttons & AJ_BTN_RTRIGGER);
    int dx, dy;

    if (!applejack_adb_has_data(d)) {
        return 0;
    }

    dx = CLAMP(s->dx, -63, 63);
    dy = CLAMP(s->dy, -63, 63);
    s->dx -= dx;
    s->dy -= dy;
    s->last_mouse_btns[0] = s->mouse_btns[0];
    s->last_mouse_btns[1] = s->mouse_btns[1];
    if (s->dx == 0 && s->dy == 0) {
        s->has_mouse_data = false;
    }
    s->triggers_changed = false;

    obuf[0] = (b0 ? 0x00 : 0x80) | ((uint8_t)dy & 0x7F);
    obuf[1] = (b1 ? 0x00 : 0x80) | ((uint8_t)dx & 0x7F);

    if (d->handler == APPLEJACK_HANDLER_ID && s->buttons_changed) {
        uint16_t inverted = (uint16_t)(~s->aj_buttons);
        obuf[2] = (uint8_t)(inverted >> 8);
        obuf[3] = (uint8_t)(inverted & 0xFF);
        s->buttons_changed = false;
        return 4;
    }

    return 2;
}

static int applejack_adb_request(ADBDevice *d, uint8_t *obuf,
                                 const uint8_t *buf, int len)
{
    ADBAppleJackState *s = ADB_APPLEJACK(d);
    int cmd, reg;

    if ((buf[0] & 0x0F) == ADB_FLUSH) {
        s->dx = 0;
        s->dy = 0;
        s->mouse_btns[0] = s->last_mouse_btns[0];
        s->mouse_btns[1] = s->last_mouse_btns[1];
        s->has_mouse_data = false;
        s->buttons_changed = false;
        s->triggers_changed = false;
        return 0;
    }

    cmd = buf[0] & 0x0C;
    reg = buf[0] & 0x03;

    switch (cmd) {
    case ADB_WRITEREG:
        switch (reg) {
        case 3:
            if (len != 3) {
                return 0;
            }
            if (buf[2] == ADB_CMD_SELF_TEST) {
                return 0;
            }
            if (buf[2] == ADB_CMD_CHANGE_ID_AND_ENABLE ||
                buf[2] == ADB_CMD_CHANGE_ID ||
                buf[2] == ADB_CMD_CHANGE_ID_AND_ACT) {
                d->devaddr = buf[1] & 0x0F;
                return 0;
            }
            d->devaddr = buf[1] & 0x0F;
            if (buf[2] == APPLEJACK_HANDLER_ID ||
                buf[2] == APPLEJACK_DEFAULT_HANDLER_ID ||
                buf[2] == 0x02) {
                d->handler = buf[2];
            }
            break;
        default:
            break;
        }
        return 0;

    case ADB_READREG:
        switch (reg) {
        case 0:
            return applejack_adb_poll(d, obuf);
        case 3:
            obuf[0] = (d->devaddr & 0x0F) | 0x20;
            obuf[1] = d->handler;
            return 2;
        default:
            return 0;
        }

    default:
        return 0;
    }
}

static void adb_applejack_reset_hold(Object *obj, ResetType type)
{
    ADBAppleJackState *s = ADB_APPLEJACK(obj);
    ADBDevice *d = ADB_DEVICE(obj);

    d->handler = APPLEJACK_DEFAULT_HANDLER_ID;
    d->devaddr = ADB_DEVID_MOUSE;
    s->dx = 0;
    s->dy = 0;
    s->mouse_btns[0] = false;
    s->mouse_btns[1] = false;
    s->last_mouse_btns[0] = false;
    s->last_mouse_btns[1] = false;
    s->aj_buttons = 0;
    s->buttons_changed = false;
    s->triggers_changed = false;
    s->has_mouse_data = false;
}

static const QemuInputHandler applejack_mouse_handler = {
    .name  = "QEMU Pippin AppleJack Pointer",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = applejack_mouse_event,
};

static void adb_applejack_realizefn(DeviceState *dev, Error **errp)
{
    ADBAppleJackState *s = ADB_APPLEJACK(dev);
    ADBAppleJackClass *ajc = ADB_APPLEJACK_GET_CLASS(dev);
    ADBDevice *d = ADB_DEVICE(dev);

    ajc->parent_realize(dev, errp);

    d->devaddr = ADB_DEVID_MOUSE;
    d->handler = APPLEJACK_DEFAULT_HANDLER_ID;
    s->mouse_hs = qemu_input_handler_register(dev, &applejack_mouse_handler);
}

static void adb_applejack_initfn(Object *obj)
{
    ADBDevice *d = ADB_DEVICE(obj);

    d->devaddr = ADB_DEVID_MOUSE;
    d->handler = APPLEJACK_DEFAULT_HANDLER_ID;
}

static void applejack_get_buttons_prop(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    ADBAppleJackState *s = ADB_APPLEJACK(obj);
    uint32_t value = s->aj_buttons;

    visit_type_uint32(v, name, &value, errp);
}

static void applejack_set_buttons_prop(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    ADBAppleJackState *s = ADB_APPLEJACK(obj);
    uint32_t value = 0;
    uint32_t diff;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    value &= AJ_BTN_VALID_MASK;
    diff = s->aj_buttons ^ value;
    if (!diff) {
        return;
    }

    s->aj_buttons = value;
    if (diff & (AJ_BTN_LTRIGGER | AJ_BTN_RTRIGGER)) {
        s->triggers_changed = true;
    }
    if (diff & 0xFFFFU) {
        s->buttons_changed = true;
    }
    if (DEVICE(s)->realized) {
        adb_input_activity(ADB_BUS(qdev_get_parent_bus(DEVICE(s))));
    }
}

static const VMStateDescription vmstate_adb_applejack = {
    .name = "adb_applejack",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, ADBAppleJackState, 0,
                       vmstate_adb_device, ADBDevice),
        VMSTATE_INT32(dx, ADBAppleJackState),
        VMSTATE_INT32(dy, ADBAppleJackState),
        VMSTATE_BOOL_ARRAY(mouse_btns, ADBAppleJackState, 2),
        VMSTATE_BOOL_ARRAY(last_mouse_btns, ADBAppleJackState, 2),
        VMSTATE_UINT32(aj_buttons, ADBAppleJackState),
        VMSTATE_END_OF_LIST()
    }
};

static void adb_applejack_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    ADBDeviceClass *adc = ADB_DEVICE_CLASS(oc);
    ADBAppleJackClass *ajc = ADB_APPLEJACK_CLASS(oc);

    device_class_set_parent_realize(dc, adb_applejack_realizefn,
                                    &ajc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    rc->phases.hold = adb_applejack_reset_hold;
    dc->vmsd = &vmstate_adb_applejack;
    adc->devreq = applejack_adb_request;
    adc->devhasdata = applejack_adb_has_data;

    object_class_property_add(oc, "buttons", "uint32",
                              applejack_get_buttons_prop,
                              applejack_set_buttons_prop,
                              NULL, NULL);
    object_class_property_set_description(oc, "buttons",
        "Active-high Pippin AppleJack controller button bitmask");
}

static const TypeInfo adb_applejack_info = {
    .name          = TYPE_ADB_APPLEJACK,
    .parent        = TYPE_ADB_DEVICE,
    .instance_size = sizeof(ADBAppleJackState),
    .instance_init = adb_applejack_initfn,
    .class_init    = adb_applejack_class_init,
    .class_size    = sizeof(ADBAppleJackClass),
};

static void applejack_register_types(void)
{
    type_register_static(&adb_applejack_info);
}

type_init(applejack_register_types)
