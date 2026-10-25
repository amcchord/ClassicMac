/* ClassicMac guest display-control mailbox. GPL-2.0-or-later. */
#include "qemu/osdep.h"

#include "classicmac_host_display.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_CLASSICMAC_DISPLAY_CONTROL "classicmac-display-control"
OBJECT_DECLARE_SIMPLE_TYPE(ClassicMacDisplayControl, CLASSICMAC_DISPLAY_CONTROL)

struct ClassicMacDisplayControl {
  PCIDevice parent;
  MemoryRegion mmio;
  uint32_t width, height, serial, result, completed;
};
static ClassicMacDisplayControl *controller;

/* Called under the BQL. Repeated requests re-arm a size the guest has left. */
void classicmac_display_request(uint32_t width, uint32_t height) {
  if (!controller) {
    return;
  }
  controller->width = width;
  controller->height = height;
  if (++controller->serial == 0) {
    ++controller->serial;
  }
  controller->result = INT32_MAX;
}

static uint64_t control_read(void *opaque, hwaddr addr, unsigned size) {
  ClassicMacDisplayControl *s = opaque;
  switch (addr) {
  case 0:
    return 0x434d7273; /* CMrs, protocol version 1 */
  case 4:
    return s->width;
  case 8:
    return s->height;
  case 12:
    return s->serial;
  case 16:
    return s->result;
  case 20:
    return s->completed;
  default:
    return 0;
  }
}
static void control_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size) {
  ClassicMacDisplayControl *s = opaque;
  if (addr == 16) {
    s->result = value;
  } else if (addr == 20) {
    s->completed = value;
  }
}
static const MemoryRegionOps ops = {
    .read = control_read,
    .write = control_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};
static const VMStateDescription control_vmstate = {
    .name = TYPE_CLASSICMAC_DISPLAY_CONTROL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_PCI_DEVICE(parent, ClassicMacDisplayControl),
        VMSTATE_UINT32(width, ClassicMacDisplayControl),
        VMSTATE_UINT32(height, ClassicMacDisplayControl),
        VMSTATE_UINT32(serial, ClassicMacDisplayControl),
        VMSTATE_UINT32(result, ClassicMacDisplayControl),
        VMSTATE_UINT32(completed, ClassicMacDisplayControl),
        VMSTATE_END_OF_LIST()}};
static void control_realize(PCIDevice *dev, Error **errp) {
  ClassicMacDisplayControl *s = CLASSICMAC_DISPLAY_CONTROL(dev);
  if (controller) {
    error_setg(errp, "Only one ClassicMac display controller is supported");
    return;
  }
  memory_region_init_io(&s->mmio, OBJECT(dev), &ops, s,
                        TYPE_CLASSICMAC_DISPLAY_CONTROL, 4096);
  pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
  controller = s;
}
static void control_exit(PCIDevice *dev) {
  if (controller == CLASSICMAC_DISPLAY_CONTROL(dev)) {
    controller = NULL;
  }
}
static char *control_status(Object *o, Error **errp) {
  ClassicMacDisplayControl *s = CLASSICMAC_DISPLAY_CONTROL(o);
  return g_strdup_printf("requested=%ux%u serial=%u completed=%u result=%d",
                         s->width, s->height, s->serial, s->completed,
                         (int32_t)s->result);
}
static void control_init(Object *o) {
  object_property_add_str(o, "x-resize-status", control_status, NULL);
}
static void control_class(ObjectClass *klass, const void *data) {
  PCIDeviceClass *pci = PCI_DEVICE_CLASS(klass);
  pci->vendor_id = 0x1234; /* QEMU's emulated display vendor */
  pci->device_id = 0x1112; /* ClassicMac mailbox, distinct from VGA 0x1111 */
  pci->class_id = PCI_CLASS_OTHERS;
  pci->realize = control_realize;
  pci->exit = control_exit;
  DEVICE_CLASS(klass)->hotpluggable = false;
  DEVICE_CLASS(klass)->vmsd = &control_vmstate;
}
static const TypeInfo info = {
    .name = TYPE_CLASSICMAC_DISPLAY_CONTROL,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ClassicMacDisplayControl),
    .instance_init = control_init,
    .class_init = control_class,
    .interfaces = (InterfaceInfo[]){{INTERFACE_CONVENTIONAL_PCI_DEVICE}, {}}};
static void register_control(void) { type_register_static(&info); }
type_init(register_control)
