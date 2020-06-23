/* Mesh Internal Communication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "freertos/task.h"

#include "my_main.h"
#include "my_mesh.h"
#include "my_smartconfig.h"
#include "my_sensorif.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t *netif_mesh_sta, *netif_mesh_ap;  /* mesh网络层handle */

#if CONFIG_MESH_ENABLE_TIMEOUT
static esp_timer_handle_t mesh_timer;   /* 定时器handle */
#endif

/*******************************************************
 *                Function Declarations
 *******************************************************/
static void my_mesh_task(void *arg);
static esp_err_t my_mesh_task_start(void);
static void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data);

#if CONFIG_MESH_ENABLE_TIMEOUT
static void mesh_timeout_callback(void* arg);
#endif

/*******************************************************
 *                Function Definitions
 *******************************************************/
static void my_mesh_task(void *arg)
{
    BaseType_t ret;
    uint8_t count = 0;
    my_sensorif_ctrl_t ctrl = {0};  /* 需要发送的sensor控制数据 */
    my_sensorif_data_t data = {0};  /* 接收到的sensor数据 */
    uint8_t sensor_ctrl = 1;        /* (假设的)控制sensor读取需要的数值 */

#if CONFIG_MESH_DATA_SEND_TO_SERVER
    mesh_data_t mesh_data;
#endif

    while(1) {
        // 从队列中读取sensorif发送的数据，无数据不等待
        ret = xQueueReceive(main_get_mesh_queue(), &data, 0);
        // 接收到sensor数据
        if(ret == pdTRUE) {
            ESP_LOGI(MESH_TAG, "Some data received from mesh queue!");
            // 向服务器发送采集到的数据
        #if CONFIG_MESH_DATA_SEND_TO_SERVER
            // 向服务器(1.2.3.4:80)发送数据
            mesh_data.proto = MESH_PROTO_HTTP;
            mesh_data.tos   = MESH_TOS_P2P;
            mesh_data.size  = (data.num + 1) * sizeof(uint8_t);

            // 申请内存存放mesh数据包
            uint8_t *ptr = pvPortMalloc(mesh_data.size);    

            ptr[0] = data.num;
            // 复制sensor读取到的数据
            memcpy(ptr+1, data.data, data.num * sizeof(uint8_t));
            mesh_data.data = ptr;
            // 配置外部网络地址
            mesh_addr_t mesh_addr;
            IP4_ADDR(&mesh_addr.mip.ip4,1,2,3,4);
            mesh_addr.mip.port = 80;
            // 发送到外部网络
            esp_mesh_send(&mesh_addr, &mesh_data, MESH_DATA_TODS, NULL, 0);

            // 释放申请的内存
            vPortFree(ptr);
        #else
            // 没有服务器，此处直接打印出来
            for(uint8_t i = 0; i < data.num; i++){
                // 此处假设传递的数据为 uint8_t 类型
                uint8_t dt = *(uint8_t *)(data.data + i*sizeof(uint8_t));
                ESP_LOGW(MESH_TAG, "data[%d] : %d", i, dt);
            }
        #endif
        }
        else {
            ESP_LOGI(MESH_TAG, "No data received from mesh queue!");
        }

        /* XXX: 实际应用中需要修改
         * 手动读取指定sensor的数据，
         * 实际使用中应该读取其他任务发送的队列消息
         */
    #if 1
        count++;
        // 约每5秒手动查询某一sensor的数值
        if( (count % 50) == 0 ) {
            // 向sensorif队列发送控制数据，等待1s
            // 使用的是read函数，获取到的数值为10
            ctrl.sid = 1;
            ctrl.ctrl = &sensor_ctrl;
            xQueueSend(main_get_sensorif_queue(), &ctrl, (1000 / portTICK_PERIOD_MS));
            ESP_LOGW(MESH_TAG, "Send data to sensorif queue!");
        }
    #else
        ret = xQueueReceive(mesh_ctrl_queue, &ctrl, 0);
        if(ret == pdTRUE){
            if(ctrl.sid != 0) {
                xQueueSend(main_get_sensorif_queue(), &ctrl, (1000 / portTICK_PERIOD_MS));
            }
        }
    #endif
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

static esp_err_t my_mesh_task_start(void)
{
    static bool is_task_started = false;
    if (!is_task_started) {
        is_task_started = true;
        xTaskCreate(my_mesh_task, "MPTX", 3072, NULL, 5, NULL);
        // 创建sensorif任务,使之发送sensor数据到mesh任务中
        sensorif_init();
    }
    return ESP_OK;
}

static void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint8_t last_layer = 0;

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
#if CONFIG_MESH_ENABLE_TIMEOUT
        /* 设定并启动超时定时器*/
        // 定时器参数
        const esp_timer_create_args_t mesh_timer_args = {
            .callback = &mesh_timeout_callback,
            .name = "mesh-timeout"
        };
        // 创建定时器
        ESP_ERROR_CHECK(esp_timer_create(&mesh_timer_args, &mesh_timer));
        // 启动定时器
        ESP_ERROR_CHECK(esp_timer_start_once(mesh_timer, (CONFIG_MESH_TIMEOUT_TIME)*1000*1000));
#endif
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        last_layer = mesh_layer;
        is_mesh_connected = true;
        if (esp_mesh_is_root()) {
            // 开启dhcp
            ESP_ERROR_CHECK (esp_netif_dhcpc_start(netif_mesh_sta) );
        }
    #if CONFIG_MESH_ENABLE_TIMEOUT
        // 停止并删除定时器
        esp_timer_stop(mesh_timer);
        esp_timer_delete(mesh_timer);
        ESP_LOGI(MESH_TAG, "mesh timer deleted.");
    #endif
        // 创建mesh任務
        // my_mesh_task_start();
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        /* new root */
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    case MESH_EVENT_PS_PARENT_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
    }
    break;
    case MESH_EVENT_PS_CHILD_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, "MACSTR", duty:%d", ps_duty->child_connected.aid-1,
                MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
    }
    break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event_id);
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
    // 获取到IP，此时可以连接到外部网络，创建mesh任务
    my_mesh_task_start();
}

#if CONFIG_MESH_ENABLE_TIMEOUT
static void mesh_timeout_callback(void* arg)
{
    // 停止并删除定时器
    esp_timer_stop(mesh_timer);
    esp_timer_delete(mesh_timer);
    
    // 时间到仍然没有连上mesh网络
    if(is_mesh_connected == false){
        // 取消注册的事件
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);
        esp_event_handler_unregister(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler);

        // 关闭mesh网络
        ESP_ERROR_CHECK (esp_mesh_stop() );
        ESP_ERROR_CHECK (esp_mesh_deinit());

        // 关闭wifi
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_stop());
        // 取消Wifi初始化
        ESP_ERROR_CHECK(esp_wifi_deinit());
        main_set_wifi_init(false);

        // 销毁创建的mesh网络接口
        esp_netif_destroy(netif_mesh_sta);
        esp_netif_destroy(netif_mesh_ap);
        netif_mesh_sta = NULL;
        netif_mesh_ap  = NULL;

        // 启动smartconfig
        smartconfig_start();
    }
}
#endif

void mesh_start(void)
{
    // 从NVS中获取路由器wifi信息
    char ssid[33] = { 0 };
    char password[65] = { 0 };
    size_t len_ssid = 0;
    size_t len_pswd = 0;

    nvs_handle_t wifi_handle;
    ESP_ERROR_CHECK( nvs_open(MESH_NVS_KEY_NAMESPACE, NVS_READWRITE, &wifi_handle) );
    // 获取存储的ssid和password的长度
    nvs_get_str(wifi_handle, MESH_NVS_KEY_ROUTER_SSID, NULL, &len_ssid);
    nvs_get_str(wifi_handle, MESH_NVS_KEY_ROUTER_PASSWORD, NULL, &len_pswd);
    // 获取ssid和password的值
    ESP_ERROR_CHECK( nvs_get_str(wifi_handle, MESH_NVS_KEY_ROUTER_SSID, ssid, &len_ssid) );
    ESP_ERROR_CHECK( nvs_get_str(wifi_handle, MESH_NVS_KEY_ROUTER_PASSWORD, password, &len_pswd) );
    nvs_close(wifi_handle);
    // printf("Read router success,ssid=%s,psw=%s\n",ssid,password);

    // 为mesh创建网络接口
    if(netif_mesh_sta == NULL && netif_mesh_ap == NULL) {
        ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_mesh_sta, &netif_mesh_ap));
    }

    // wifi未初始化
    if (main_get_wifi_init() != true){
        // 初始化wifi
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&config));
        main_set_wifi_init(true);
    }
    
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    // 设定mesh拓扑结构
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    // mesh网络最大层数
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    // 根节点投票阈值，默认为0.9
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    // mesh接收队列长度
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(64));

#ifdef CONFIG_MESH_ENABLE_PS
    /* Enable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    /* better to increase the associate expired time, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    /* better to increase the announce interval to avoid too much management traffic, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    /* 禁用mesh Power Save功能 */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    /* 超时时间，子节点超过此时间无数据则被剔除 */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
#endif

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = len_ssid;
    memcpy((uint8_t *) &cfg.router.ssid, ssid, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, password, len_pswd);
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());

#ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:12, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:12, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif

    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s<%d>%s, ps:%d\n",  esp_get_minimum_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());
}
