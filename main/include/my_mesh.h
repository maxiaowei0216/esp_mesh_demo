#ifndef __MY_MESH_H__
#define __MY_MESH_H__

// nvs各个键名
#define MESH_NVS_KEY_NAMESPACE       "mesh_info"
#define MESH_NVS_KEY_ROUTER_SAVED    "rt_info_saved"
#define MESH_NVS_KEY_ROUTER_SSID     "rt_ssid"
#define MESH_NVS_KEY_ROUTER_PASSWORD "rt_pwd"


void mesh_start(void);
#endif