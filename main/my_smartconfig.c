/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_smartconfig.h"

#include "my_main.h"
#include "my_smartconfig.h"
#include "my_mesh.h"


/*******************************************************
 *                Variable Definitions
 *******************************************************/
static EventGroupHandle_t s_wifi_event_group;

static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const int GOT_INFO_BIT = BIT2;
static const char *TAG = "my_smartconfig";
// WiFi station网络层接口,用于SmartConfig
static esp_netif_t *netif_sta = NULL;
// SmartConfig获取到路由器SSID和密码信息
static smartconfig_event_got_ssid_pswd_t router_info = {0};

/*******************************************************
 *                Function Declarations
 *******************************************************/
static void smartconfig_task(void * parm);

/*******************************************************
 *                Function Definitions
 *******************************************************/
// 事件处理函数
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // wifi station start
    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        // 创建任务，开启smartconfig
        xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
        ESP_LOGI(TAG, "SmartConfig task created");
    }
    // 连上ap，获得ip
    else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        // 事件组置位
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    }
    // smartconfig相关事件
    else if (event_base == SC_EVENT) {
        switch (event_id)
        {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "Scan done");
            break;
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "Found channel");
            break;
        case SC_EVENT_GOT_SSID_PSWD:
            ESP_LOGI(TAG, "Got SSID and password");

            // 保存得到的路由器信息
            memcpy(&router_info, event_data, sizeof(smartconfig_event_got_ssid_pswd_t));
            // router_info = *((smartconfig_event_got_ssid_pswd_t *)event_data);

            // 对应事件组置位
            xEventGroupSetBits(s_wifi_event_group, GOT_INFO_BIT);
            break;
        case SC_EVENT_SEND_ACK_DONE:
            // 已经将smartconfig结果返回给手机，结束smartconfig
            xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
        }
    }
}

// smartconfig任务函数
static void smartconfig_task(void * parm)
{
    EventBits_t uxBits;
    // 设定smartconfig使用的方式
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    ESP_LOGW(TAG, "Start SmartConfig in task!");
    while (1) {
        // 等待事件发生，只要有一个事件发生即可，且退出时对应事件清零
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT | GOT_INFO_BIT,
        // uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT,
                                        true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }

        if(uxBits & GOT_INFO_BIT) {
            ESP_LOGI(TAG, " Into task GOT_INFO_BIT......");
            wifi_config_t wifi_config;
            char ssid[33] = { 0 };
            char password[65] = { 0 };

            // 从参数中得到路由器wifi信息
            bzero(&wifi_config, sizeof(wifi_config_t));
            memcpy(wifi_config.sta.ssid, router_info.ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, router_info.password, sizeof(wifi_config.sta.password));
            wifi_config.sta.bssid_set = router_info.bssid_set;
            if (wifi_config.sta.bssid_set == true) {
                memcpy(wifi_config.sta.bssid, router_info.bssid, sizeof(wifi_config.sta.bssid));
            }

            memcpy(ssid, router_info.ssid, sizeof(router_info.ssid));
            memcpy(password, router_info.password, sizeof(router_info.password));
            ESP_LOGI(TAG, "SSID:%s", ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", password);

            // 存储路由器信息
            nvs_handle_t wifi_handle;
            ESP_ERROR_CHECK( nvs_open(MESH_NVS_KEY_NAMESPACE, NVS_READWRITE, &wifi_handle) );
            ESP_ERROR_CHECK( nvs_set_i8(wifi_handle, MESH_NVS_KEY_ROUTER_SAVED, 1) );
            ESP_ERROR_CHECK( nvs_set_str(wifi_handle, MESH_NVS_KEY_ROUTER_SSID, ssid) );
            ESP_ERROR_CHECK( nvs_set_str(wifi_handle, MESH_NVS_KEY_ROUTER_PASSWORD, password) );
            ESP_ERROR_CHECK( nvs_commit(wifi_handle) );
            nvs_close(wifi_handle);
            ESP_LOGI(TAG, "Router info saved.\n");

            // 根据得到的信息连接wifi
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            ESP_LOGI(TAG, "wifi connect.");
        }

        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            // 停止smartconfig
            esp_smartconfig_stop();
            // 断开wifi
            esp_wifi_disconnect();
            // 停止wifi
            esp_wifi_stop();

            // 取消注册的事件
            esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
            esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler);
            esp_event_handler_unregister(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler);

            // 销毁创建的sta网络接口
            esp_netif_destroy(netif_sta);
            netif_sta = NULL;
            ESP_LOGI(TAG, "Destroy sta netif");

            // 连接成功，开始mesh
            mesh_start();
            // smartconfig结束，删除当前任务
            vTaskDelete(NULL);
        }
    }
}

void smartconfig_start(void)
{
    ESP_LOGI(TAG, "SC start!");
    s_wifi_event_group = xEventGroupCreate();
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    if(netif_sta == NULL){
        netif_sta = esp_netif_create_default_wifi_sta();
        ESP_LOGI(TAG, "netif create!");
    }

    if (main_get_wifi_init() != true){ // 未初始化过wifi
        // 初始化wifi
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&config));
        main_set_wifi_init(true);
        ESP_LOGI(TAG, "Wifi init!");
    }

    // 注册相关事件的处理函数
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "Wifi start!");
}
