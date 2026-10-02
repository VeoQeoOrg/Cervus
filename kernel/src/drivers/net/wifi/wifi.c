#include "../../../../include/drivers/net/wifi.h"
#include "../../../compat/linuxkpi/kpi_pci.h"

int kpi_module_rtw_8723de_driver(void);

void wifi_init(void)
{
    kpi_module_rtw_8723de_driver();
}

void wifi_start(void)
{
    kpi_pci_start();
}
