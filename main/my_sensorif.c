#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "my_sensorif.h"
#include "my_main.h"

/*******************************************************
 *                Constants
 *******************************************************/
#define SENSOR_NUM_MAX  (5)
#define AUTO_READ       (1)

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static my_sensor_t sensors[SENSOR_NUM_MAX] = {0};
static char sensor_num = 0;
static uint8_t sensor_id = 1;
static const char *SENSORIF_TAG = "Sensorif";

/*******************************************************
 *                Function Declarations
 *******************************************************/
static void sensorif_task(void *args);

/*******************************************************
 *                Function Definitions
 *******************************************************/
static void sensorif_task(void *args)
{
    unsigned char i = 0;
    BaseType_t ret;
    my_sensorif_data_t data = {0};
    my_sensorif_ctrl_t ctrl = {0};

    while (1)
    {
        // 从队列中读取数据，看是否需要单独读取某一sensor的数据，等待1s
        ret = xQueueReceive(main_get_sensorif_queue(), &ctrl, (1000 / portTICK_PERIOD_MS));
        // 从队列获取到消息
        if (ret == pdTRUE) {
            ESP_LOGI(SENSORIF_TAG, "Some data received from sensorif queue!");
            for(i = 0; i < SENSOR_NUM_MAX; i++) {
                if((sensors[i].sid == ctrl.sid) && (sensors[i].valid == true)) {
                    // 读取信息
                    sensors[i].sif.read(ctrl.ctrl, &data);
                    memcpy(&sensors[i].data, &data, sizeof(my_sensorif_data_t));
                    // 向mesh任务队列发送数据，队列满无限等待
                    xQueueSend(main_get_mesh_queue(), &sensors[i].data, portMAX_DELAY);
                    ESP_LOGW(SENSORIF_TAG, "Send data(10) to mesh queue!");
                    break;
                }
            }
        }
        else { /* 没有从队列获取到消息 */
        #if AUTO_READ
            ESP_LOGI(SENSORIF_TAG, "No data received from sensorif queue!");
            // 循环读取各个sensor的数据并发送给mesh任务，
            // 由mesh任务发送数据到服务器端
            for(i = 0; i < SENSOR_NUM_MAX; i++) {
                if(sensors[i].valid == true){
                    // 读取sensor获取的数据
                    sensors[i].sif.read_default(&data);
                    memcpy(&sensors[i].data, &data, sizeof(my_sensorif_data_t));
                    // 向mesh任务队列发送数据，队列满无限等待
                    xQueueSend(main_get_mesh_queue(), &sensors[i].data, portMAX_DELAY);
                    ESP_LOGW(SENSORIF_TAG, "Send data(5) to mesh queue!");
                    // 每读完一个sensor延时100ms
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
            }
        #endif
        }
    }
    vTaskDelete(NULL);
}

my_sensor_err_t my_sensor_register(my_sensorif_t *sif, uint8_t *sid)
{
    my_sensor_err_t ret = MY_SENSOR_ERR_OK;

    if((sif == NULL) || (sid == NULL)){
        ret = MY_SENSOR_ERR_ARGS;
    }
    else if(sensor_num >= SENSOR_NUM_MAX) {
        *sid = 0;
        ret = MY_SENSOR_ERR_OVER_CAP;
    }
    else {
        // 寻找剩余的可存放sensor信息的空间
        for(unsigned char i = 0; i < SENSOR_NUM_MAX; i++) {
            if(sensors[i].valid == false){
                sensors[i].valid = true;        /* sensor设置为有效 */
                sensors[i].sid = sensor_id;     /* 分配一个sensor id */
                *sid = sensor_id;               /* 传出分配的sid */
                sensor_num++;                   /* 当前系统的sensor数量+1 */
                sensor_id++;
                // 复制sensor的各个参数
                memcpy(&sensors[i].sif, sif, sizeof(my_sensorif_t));
                // 执行注册时初始化函数
                ret = sensors[i].sif.init();
                break;
            }
        }
    }

    return ret;
}

my_sensor_err_t my_sensor_unregister(uint8_t sid)
{
    my_sensor_err_t ret = MY_SENSOR_ERR_OK;

    if (sid == 0){
        ret = MY_SENSOR_ERR_ARGS;
    }
    else {
        for(unsigned char i = 0; i < SENSOR_NUM_MAX; i++) {
            // 找到需要注销的sensor
            if(sensors[i].sid == sid) {
                // sensor有效，即还没被注销，执行注销程序
                if(sensors[i].valid == true) {
                    sensors[i].valid = false;   /* 设置为无效 */
                    sensor_num--;               /* sensor数量调整 */
                    ret = sensors[i].sif.exits();
                }
                else {
                    // 指定的sensor已经注销，不需要额外操作
                    ret = MY_SENSOR_ERR_OK;
                }
                break;
            }
            else {
                ret = MY_SENSOR_ERR_NOT_FOUND;
            }
        }
    }

    return ret;
}

my_sensor_err_t my_sensor_read(uint8_t sid, void *in, my_sensorif_data_t *out)
{
    my_sensor_err_t ret = MY_SENSOR_ERR_OK;

    if (sid == 0){
        ret = MY_SENSOR_ERR_ARGS;
    }
    else {
        for(unsigned char i = 0; i < SENSOR_NUM_MAX; i++) {
            // 找到对应的sensor
            if(sensors[i].sid == sid) {
                if(sensors[i].valid == true) {
                    // 调用对应的读取函数
                    ret = sensors[i].sif.read(in, out);
                }
                else {
                    ret = MY_SENSOR_ERR_INVALID;
                }
                break;
            }
            else {
                ret = MY_SENSOR_ERR_NOT_FOUND;
            }
        }
    }

    return ret;
}

my_sensor_err_t my_sensor_write(uint8_t sid, void *args)
{
    my_sensor_err_t ret = MY_SENSOR_ERR_OK;

    if (sid == 0){
        ret = MY_SENSOR_ERR_ARGS;
    }
    else {
        for(unsigned char i = 0; i < SENSOR_NUM_MAX; i++) {
            // 找到对应的sensor
            if(sensors[i].sid == sid) {
                if(sensors[i].valid == true) {
                    // 调用对应的读取函数
                    ret = sensors[i].sif.write(args);
                }
                else {
                    ret = MY_SENSOR_ERR_INVALID;
                }
                break;
            }
            else {
                ret = MY_SENSOR_ERR_NOT_FOUND;
            }
        }
    }

    return ret;
}

void sensorif_init(void)
{
    // 创建sensorif任务
    xTaskCreate(sensorif_task, "sensorif_task", 3072, NULL, 4, NULL);
}