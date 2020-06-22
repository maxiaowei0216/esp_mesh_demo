#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/queue.h"

#include "my_main.h"
#include "my_mesh.h"
#include "my_smartconfig.h"
#include "my_sensorif.h"
#include "example_sensor.h"


/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MAIN_TAG = "app_main";
static bool wifi_inited = false;
// sensor接口任务使用的消息队列，接收其他任务发送的控制信息
static QueueHandle_t sensorif_queue;
// mesh任务使用的消息队列，主要接收sensor接口任务发送的sensor数据
static QueueHandle_t mesh_queue;

/*******************************************************
 *                Function Declarations
 *******************************************************/
static int router_info_check(void);

/*******************************************************
 *                Function Definitions
 *******************************************************/
static int router_info_check(void)
{
    nvs_handle_t nvs_handle;
    // 打开命名空间为"mesh_info"的部分
    esp_err_t err = nvs_open(MESH_NVS_KEY_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return -1;
    } else {
        // 读取数据
        int8_t router_info_saved = 0;
        err = nvs_get_i8(nvs_handle, MESH_NVS_KEY_ROUTER_SAVED, &router_info_saved);
        // printf("MAIN:MESH_NVS_KEY_ROUTER_SAVED=%d\n",router_info_saved);
        // 关闭NVS
        nvs_close(nvs_handle);

        if ((err == ESP_OK) && (router_info_saved == 1)) {
            //已经配置过路由器信息，开启mesh
            return 0;
        } else {
            // 读取失败或者未配置过路由器信息，开启smartconfig
            return -1;
        }
    }
}

bool main_get_wifi_init(void)
{
    return wifi_inited;
}

void main_set_wifi_init(bool init)
{
    wifi_inited = init;
}

QueueHandle_t main_get_sensorif_queue(void)
{
    return sensorif_queue;
}

QueueHandle_t main_get_mesh_queue(void)
{
    return mesh_queue;
}

void app_main(void)
{
    // 初始化NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 初始化失败，擦除数据后重新初始化
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    // 初始化tcp/ip
    ESP_ERROR_CHECK(esp_netif_init());

    // 初始化事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建消息队列
    /* 接收sensor控制信息的队列 */
    sensorif_queue = xQueueCreate(5, sizeof(my_sensorif_ctrl_t));
    if(sensorif_queue == 0) {
        ESP_LOGE(MAIN_TAG, "Sensorif queue create failed!");
    }
    /* 接收sensor采集到的数据的队列 */
    mesh_queue     = xQueueCreate(5, sizeof(my_sensorif_data_t));
    if(mesh_queue == 0) {
        ESP_LOGE(MAIN_TAG, "Mesh queue create failed!");
    }

    // 检查是否配置过mesh的路由器信息
    // 未配置的话启动smartconfig，利用手机app进行配置
    int ret = router_info_check();
    if (ret == 0) { // 已经配置过路由器信息
        ESP_LOGI(MAIN_TAG, "Get router info success, starting mesh!\n");
        mesh_start();
    } else {
        ESP_LOGI(MAIN_TAG, "Get router info failed, starting smartconfig!\n");
        smartconfig_start();
    }

    // 注册示例sensor到sensor接口
    example_sensor_init();

}
