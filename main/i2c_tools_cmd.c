/* 
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include <nvs_flash.h>
#include "esp_netif.h"
#include "esp_http_server.h"

#include "i2c_tools.h"


#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE  /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ    /*!< I2C master read */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */


static const char *TAG = "i2c_tools_cmd";

// cfg
#define I2C_PORT_HTML "i2cPortConfig"
#define I2C_SCL_HTML "i2cSCLConfig"
#define I2C_SDA_HTML "i2cSDAConfig"
#define I2C_FREQ_HTML "i2cFREQConfig"
#define I2C_TRIG_HTML "i2cTrigConfig"

#define I2C_CHIP_HTML "i2cChipAddress"
#define i2C_CHIP_SIZE_HTML "i2cDumpSize"

#define I2C_READ_HTML "i2cRegisterGetAddress"
#define i2C_READ_SIZE_HTML "i2cRegisterGetSize"

#define I2C_WRITE_HTML "i2cRegisterSetAddress"
#define i2C_WRITE_SIZE_HTML "i2cRegisterSetSize"
#define i2C_WRITE_DATA_HTML "i2cRegisterSetData"

// cmd
#define I2C_SCAN_CMD "ScanCmd"
#define I2C_DUMP_CMD "DumpCmd"
#define I2C_READ_CMD "GetCmd"
#define I2C_WRITE_CMD "SetCmd"


typedef struct i2c_tools_cfg
{
    int trig_pin;
    int port;
    int scl;
    int sda;
    int freq;
    int chip;
    int dump_size;
    int reg_read;
    int read_size;
    int reg_write;
    int write_size
    uint8_t write_data[32];
} i2c_tools_cfg_t;

static i2c_tools_cfg_t i2c_cfg;


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
static esp_err_t i2c_master_driver_initialize(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = i2c_cfg.sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = i2c_cfg.scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = i2c_cfg.freq,
        // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };
    return i2c_param_config(i2c_cfg.port, &conf);
}
static int i2c_scan(httpd_req_t *req)
{
    i2c_driver_install(i2c_cfg.port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_master_driver_initialize();
    uint8_t address;
    char adrstr[8] = {0};
    char sendstr[64] = "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  ";
    send_json_string(sendstr,req);
    for (int i = 0; i < 128; i += 16) {
        sprintf(adrstr,"%02x: ", i);
        strcpy(sendstr,adrstr);
        for (int j = 0; j < 16; j++) {
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_PERIOD_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                sprintf(adrstr,"%02x ", address);
                strcat(sendstr,adrstr);
            } else if (ret == ESP_ERR_TIMEOUT) {
                strcat(sendstr,"UU ");
            } else {
                strcat(sendstr,"-- ");
            }
        }
        send_json_string(sendstr,req);
    }
    i2c_driver_delete(i2c_cfg.port);
    return 0;
}




// write wifi data from ws to nvs
static void set_i2c_tools_data(char *jsonstr, httpd_req_t *req)
{
    char key[16];
    char value[64];
    esp_err_t err = json_to_str_parm(jsonstr, key, value); // decode json string to key/value pair
    if (err)
    {
        ESP_LOGE(TAG, "ERR jsonstr %s", jsonstr);
        return;
    }
    if (strncmp(key, I2C_PORT_HTML, sizeof(I2C_PORT_HTML)-1) == 0  ) 
    {
        i2c_cfg.port = atoi(value);
    }
    else if (strncmp(key, I2C_SCL_HTML, sizeof(I2C_SCL_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.scl = atoi(value);
    }
    else if (strncmp(key, I2C_SDA_HTML, sizeof(I2C_SDA_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.sda = atoi(value);
    }
    else if (strncmp(key, I2C_FREQ_HTML, sizeof(I2C_FREQ_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.freq = atoi(value);
    }
    else if (strncmp(key, I2C_TRIG_HTML, sizeof(I2C_TRIG_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.trig_pin = atoi(value);
    }

    else if (strncmp(key, I2C_CHIP_HTML, sizeof(I2C_CHIP_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.chip = atoi(value);
    }
    else if (strncmp(key, I2C_CHIP_SIZE_HTML, sizeof(I2C_CHIP_SIZE_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.dump_size = atoi(value);
    }

    else if (strncmp(key, I2C_READ_HTML, sizeof(I2C_READ_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.reg_read = atoi(value);
    }
    else if (strncmp(key, I2C_READ_SIZE_HTML, sizeof(I2C_READ_SIZE_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.read_size = atoi(value);
    }

    else if (strncmp(key, I2C_WRITE_HTML, sizeof(I2C_WRITE_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.reg_write  = atoi(value);
    }
    else if (strncmp(key, I2C_WRITE_SIZE_HTML, sizeof(I2C_WRITE_SIZE_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.write_size = atoi(value);
    }
    else if (strncmp(key, I2C_WRITE_DATA_HTML, sizeof(I2C_WRITE_DATA_HTML)-1) == 0)// key/value ->  restart or write
    {
        i2c_cfg.write_data[0] = atoi(value); //tmp
    }

// cmd

    else if (strncmp(key, I2C_SCAN_CMD, sizeof(I2C_SCAN_CMD)-1) == 0)// key/value ->  restart or write
    {
        i2c_scan(req);
    }
    else if (strncmp(key, I2C_DUMP_CMD, sizeof(I2C_DUMP_CMD)-1) == 0)// key/value ->  restart or write
    {
        i2c_dump(req);
    }
    else if (strncmp(key, I2C_READ_CMD, sizeof(I2C_READ_CMD)-1) == 0)// key/value ->  restart or write
    {
        i2c_read(req);
    }
    else if (strncmp(key, I2C_READ_CMD, sizeof(I2C_READ_CMD)-1) == 0)// key/value ->  restart or write
    {
        i2c_write(req);
    }

    else 
    {
        ESP_LOGE(TAG, "ERR cmd %s", jsonstr);
    }

    return;
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
    ESP_LOGI(TAG,"get cmd %s",(char *)ws_pkt.payload);
    set_i2c_tools_data((char *)ws_pkt.payload, req);
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
