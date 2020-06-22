#ifndef __MY_SENSORIF_H__
#define __MY_SENSORIF_H__

// sensor操作模式
typedef enum {
    MY_SENSOR_MODE_NONE = 0,
    MY_SENSOR_MODE_READ,        /* 只读设备，注册后只能读取数据 */
    MY_SENSOR_MODE_RW,          /* 可读可写的设备，注册后可以写入数据进行配置 */

    MY_SENSOR_MODE_NUM,
} my_sensor_mode_t;

// sensor类型
typedef enum {
    MY_SENSOR_TYPE_NONE = 0,
    MY_SENSOR_TYPE_BIN,     /* 二进制类型，数值仅有0和1 */
    MY_SENSOR_TYPE_ONE,     /* 只采集一个数据 */
    MY_SENSOR_TYPE_MORE,    /* 采集多个数据 */

    MY_SENSOR_TYPE_NUM,
} my_sensor_type_t;

// 错误类型
typedef enum {
    MY_SENSOR_ERR_OK = 0,
    MY_SENSOR_ERR_ARGS,         /* 参数错误 */
    MY_SENSOR_ERR_OVER_CAP,     /* 超出可接入的数量 */
    MY_SENSOR_ERR_NOT_FOUND,    /* 未找到指定的sensor */
    MY_SENSOR_ERR_INVALID,      /* sensor无效（可能已经被注销） */

    MY_SENSOR_ERR_NUM,
} my_sensor_err_t;

// 采集数据的具体结构
typedef struct {
    uint8_t num;    /* 数据个数 */
    void    *data;  /* 具体数值 */
} my_sensorif_data_t;

// 获取指定sensor的控制信息
typedef struct {
    uint8_t sid;
    void    *ctrl;
} my_sensorif_ctrl_t;

typedef struct {
    my_sensor_mode_t mode;  /* sensor操作模式 */
    my_sensor_type_t type;  /* sensor类型 */

    my_sensor_err_t (*init)(void);  /* 注册时执行的函数 */
    my_sensor_err_t (*exits)(void); /* 注销时执行的函数 */
    my_sensor_err_t (*write)(void *arg);  /* 写入 */
    my_sensor_err_t (*read)(void *in, my_sensorif_data_t *out); /* 读取 */
    my_sensor_err_t (*read_default)(my_sensorif_data_t *out);   /* 无需写入参数的读取函数 */
} my_sensorif_t;

typedef struct {
    uint8_t  sid;           /* sensor id */
    bool     valid;         /* 当前sensor是否有效 */

    my_sensorif_t sif;
    my_sensorif_data_t data;
} my_sensor_t;

/** 
 * 功能：
 *  注册sensor
 * 参数：
 *  [in]sif: sensor接口参数
 *  [out]sid: 注册成功后返回的sensor id
 * 返回值：
 *  错误代码
 **/
my_sensor_err_t my_sensor_register(my_sensorif_t *sif, uint8_t *sid);

/** 
 * 功能：
 *  注销sensor
 * 参数：
 *  [in]sid: 取消注册的sensor的sensor id
 * 返回值：
 *  错误代码
 **/
my_sensor_err_t my_sensor_unregister(uint8_t sid);

/** 
 * 功能：
 *  读取指定sensor的数据
 * 参数：
 *  [in]sid:  sensor id
 *  [in]in:   传入给sensor的数据，如需要读取的数据地址等
 *  [out]out: sensor读取到的数据
 * 返回值：
 *  错误代码
 **/
my_sensor_err_t my_sensor_read(uint8_t sid, void *in, my_sensorif_data_t *out);

/** 
 * 功能：
 *  向指定sensor写入数据
 * 参数：
 *  [in]sid:  sensor id
 *  [in]args: 传入给sensor的数据
 * 返回值：
 *  错误代码
 **/
my_sensor_err_t my_sensor_write(uint8_t sid, void *args);

// sensor接口初始化,实际上创建了sensorif任务
void sensorif_init(void);
#endif