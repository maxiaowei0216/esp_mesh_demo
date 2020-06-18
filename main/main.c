#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "my_mesh.h"
#include "my_smartconfig.h"


static const char *MAIN_TAG = "app_main";

static int router_info_check(void);


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
    tcpip_adapter_init();

    // 初始化事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 检查是否配置过mesh的路由器信息
    // 未配置的话启动smartconfig，利用手机app进行配置
    int ret = router_info_check();
    if (ret == 0) { // 已经配置过路由器信息
        ESP_LOGI(MAIN_TAG, "Get router info success, starting mesh!\n");
        mesh_start(false);
    } else {
        ESP_LOGI(MAIN_TAG, "Get router info failed, starting smartconfig!\n");
        smartconfig_start(false);
    }
}
