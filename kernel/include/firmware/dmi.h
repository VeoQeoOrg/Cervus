#ifndef KERNEL_FIRMWARE_DMI_H
#define KERNEL_FIRMWARE_DMI_H

#include <stdint.h>
#include <stdbool.h>

enum dmi_field {
    DMI_NONE,
    DMI_BIOS_VENDOR,
    DMI_BIOS_VERSION,
    DMI_BIOS_DATE,
    DMI_BIOS_RELEASE,
    DMI_EC_FIRMWARE_RELEASE,
    DMI_SYS_VENDOR,
    DMI_PRODUCT_NAME,
    DMI_PRODUCT_VERSION,
    DMI_PRODUCT_SERIAL,
    DMI_PRODUCT_UUID,
    DMI_PRODUCT_SKU,
    DMI_PRODUCT_FAMILY,
    DMI_BOARD_VENDOR,
    DMI_BOARD_NAME,
    DMI_BOARD_VERSION,
    DMI_BOARD_SERIAL,
    DMI_BOARD_ASSET_TAG,
    DMI_CHASSIS_VENDOR,
    DMI_CHASSIS_TYPE,
    DMI_CHASSIS_VERSION,
    DMI_CHASSIS_SERIAL,
    DMI_CHASSIS_ASSET_TAG,
    DMI_STRING_MAX,
};

void        dmi_init(void);
bool        dmi_available(void);
const char *dmi_get_system_info(int field);
const char *dmi_field_name(int field);
int         dmi_version(void);

#endif
