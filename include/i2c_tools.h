
#include <esp_log.h>
#include <esp_http_server.h>

esp_err_t i2c_tools_register_uri_handlers(httpd_handle_t server);
esp_err_t i2c_tools_wifi_connect();
httpd_handle_t i2c_tools_ws_server(void);