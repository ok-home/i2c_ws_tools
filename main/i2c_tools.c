#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "i2c_tools.h"

void app_main(void)
{
    i2c_tools_wifi_connect();
    i2c_tools_ws_server();
}

