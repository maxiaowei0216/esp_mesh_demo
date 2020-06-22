#ifndef __MY_MAIN_H__
#define __MY_MAIN_H__

/**
 * 一些wifi的API会返回ESP_ERR_WIFI_NOT_INIT，
 * 似乎可以利用它来判断wifi是否已经初始化，
 * 这样就不需要用专门的变量对此进行管理
 */
// 检查是否进行过wifi初始化
bool main_get_wifi_init(void);
void main_set_wifi_init(bool init);

// 获取队列
QueueHandle_t main_get_sensorif_queue(void);
QueueHandle_t main_get_mesh_queue(void);

#endif