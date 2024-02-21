/* 
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "nvs_wifi_connect_private.h"
#include "nvs_wifi_connect.h"


#include "jsmn.h"

static const char *TAG = "nvs_wifi_connect_http_server";

ESP_EVENT_DECLARE_BASE(NVS_WIFI_CONNECT_STOP_HTTPD);
ESP_EVENT_DEFINE_BASE(NVS_WIFI_CONNECT_STOP_HTTPD);
enum
{
    NVS_WIFI_CONNECT_STOP_HTTPD_EVENT
};

static int srv_restart = 0;
static nvs_wifi_connect_register_uri_handler_t srv_register_uri_handler;

// simple json parse -> only one parametr name/val
static esp_err_t json_to_str_parm(char *jsonstr, char *nameStr, char *valStr) // распаковать строку json в пару  name/val
{
    int r; // количество токенов
    jsmn_parser p;
    jsmntok_t t[5]; // только 2 пары параметров и obj

    jsmn_init(&p);
    r = jsmn_parse(&p, jsonstr, strlen(jsonstr), t, sizeof(t) / sizeof(t[0]));
    if (r < 2)
    {
        valStr[0] = 0;
        nameStr[0] = 0;
        return ESP_FAIL;
    }
    strncpy(nameStr, jsonstr + t[2].start, t[2].end - t[2].start);
    nameStr[t[2].end - t[2].start] = 0;
    if (r > 3)
    {
        strncpy(valStr, jsonstr + t[4].start, t[4].end - t[4].start);
        valStr[t[4].end - t[4].start] = 0;
    }
    else
        valStr[0] = 0;
    return ESP_OK;
}
static void send_json_string(char *str, httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)str;
    ws_pkt.len = strlen(str);
    httpd_ws_send_frame(req, &ws_pkt);
}
// write wifi data from ws to nvs
static void set_nvs_data(char *jsonstr, httpd_req_t *req)
{
    char key[16];
    char value[64];
    nvs_handle_t nvs_handle;
    nvs_open(NVS_STORAGE_NAME, NVS_READWRITE, &nvs_handle);
    esp_err_t err = json_to_str_parm(jsonstr, key, value); // decode json string to key/value pair
    if (err)
    {
        ESP_LOGE(TAG, "ERR jsonstr %s", jsonstr);
    }
    else
    {
        if (strncmp(key, NVS_COMPARE_KEY_PARAM, sizeof(NVS_COMPARE_KEY_PARAM)-1) == 0  ) // key/value -> wifi data
        {
            if (nvs_set_str(nvs_handle, key, value)) // write key/value to nvs
            {
                ESP_LOGE(TAG, "ERR WRITE key %s value %s", key, value);
            }
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }
        else if (strncmp(key, NVS_WIFI_RESTART_KEY, sizeof(NVS_WIFI_RESTART_KEY)) == 0)// key/value ->  restart or write
        {
            nvs_close(nvs_handle);
            // if value == NVS_WIFI_RESTART_VALUE_RESTART -> full restart esp32 regardless of value srv_restart
            // if srv_restart == NVS_WIFI_CONNECT_MODE_STAY_ACTIVE -> no operation
            // if srv_restart == NVS_WIFI_CONNECT_MODE_STOP_SERVER -> stop httpd
            // if srv_restart == NVS_WIFI_CONNECT_MODE_RESTART_ESP32 -> full restart esp32 regardless of value NVS_WIFI_RESTART_VALUE_RESTART
            if (srv_restart == NVS_WIFI_CONNECT_MODE_RESTART_ESP32 || strncmp(value, NVS_WIFI_RESTART_VALUE_RESTART, sizeof(NVS_WIFI_RESTART_VALUE_RESTART)) == 0)
            {
                esp_restart();
            }
            else if (srv_restart == NVS_WIFI_CONNECT_MODE_STOP_SERVER)
            {
                // stop httpd server
                ESP_ERROR_CHECK(esp_event_post(NVS_WIFI_CONNECT_STOP_HTTPD, NVS_WIFI_CONNECT_STOP_HTTPD_EVENT, NULL, 0, portMAX_DELAY));
            }
        }
    }
}
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        send_nvs_data(req); // read & send initial wifi data from nvs
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    if (ws_pkt.len)
    {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
    }
    set_nvs_data((char *)ws_pkt.payload, req);
    free(buf);
    return ret;
}
static esp_err_t get_handler(httpd_req_t *req)
{
    extern const unsigned char i2c_tools_html_html_start[] asm("_binary_i2c_tools_html_html_start");
    extern const unsigned char i2c_tools_html_html_end[] asm("_binary_i2c_tools_html_html_end");
    const size_t i2c_tools_html_html_size = (i2c_tools_html_html_end - i2c_tools_html_html_start);

    httpd_resp_send_chunk(req, (const char *)i2c_tools_html_html_start, i2c_tools_html_html_size);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
static const httpd_uri_t i2c_tools_gh = {
    .uri = "i2cTools",
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL};
static const httpd_uri_t i2c_tools_ws = {
    .uri = "i2c/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true};

esp_err_t i2c_tools_connect_register_uri_handler(httpd_handle_t server)
{
    esp_err_t ret = ESP_OK;
    ret = httpd_register_uri_handler(server, &i2c_tools_gh);
    if (ret)
        goto _ret;
    ret = httpd_register_uri_handler(server, &i2c_tools_ws);
    if (ret)
        goto _ret;
_ret:
    return ret;
}
