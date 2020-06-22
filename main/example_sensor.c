#include "esp_log.h"
#include "example_sensor.h"
#include "my_sensorif.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static uint8_t sid = 0;
static uint8_t read_data = 0;
static const char *TAG = "Example_sensor";

/*******************************************************
 *                Function Definitions
 *******************************************************/
static my_sensor_err_t init(void)
{
    ESP_LOGW(TAG, "Example sensor init!");
    return MY_SENSOR_ERR_OK;
}

static my_sensor_err_t exits(void)
{
    ESP_LOGW(TAG, "Example sensor exit!");
    return MY_SENSOR_ERR_OK;
}

static my_sensor_err_t write(void *arg)
{
    // 假设写入的是一个整形
    ESP_LOGW(TAG, "Example sensor write data = %d!", *(int *)arg);
    return MY_SENSOR_ERR_OK;
}

static my_sensor_err_t read(void *in, my_sensorif_data_t *out)
{
    ESP_LOGW(TAG, "Example sensor read!");
    // 与mesh任务中假设发送的控制数据 "1" 对应
    if(*(uint8_t *)in == 1) {
        read_data = 10;         /* 构造的sensor读取出来的数值 */
        out->num = 1;    /* 传输的数据个数为1 */
        out->data = &read_data;
    }
    return MY_SENSOR_ERR_OK;
}

static my_sensor_err_t read_default(my_sensorif_data_t *out)
{
    ESP_LOGW(TAG, "Example sensor read_default!");
    // 用于sensorif任务中循环读取
    read_data = 5;         /* 构造的sensor读取出来的数值 */
    out->num = 1;    /* 传输的数据个数为1 */
    out->data = &read_data;

    return MY_SENSOR_ERR_OK;
}

void example_sensor_init(void)
{
    my_sensorif_t sif = {
        .mode = MY_SENSOR_MODE_READ,
        .type = MY_SENSOR_TYPE_ONE,
        .init = init,
        .exits = exits,
        .write = write,
        .read = read,
        .read_default = read_default,
    };

    my_sensor_register(&sif, &sid);
    ESP_LOGW(TAG, "Example sensor registered OK, sid = %d",sid);

}