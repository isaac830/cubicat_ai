#ifndef _BLE_OP_DESC_H_
#define _BLE_OP_DESC_H_
#include <map>
#include <string>
#include "ble_service_defines.h"


static std::map<int, std::string> OpDescCN = {
    {OP_GENERIC_SWITCH,     "用来控制灯的开关,值0-1"}, 
    {OP_LIGHT_COLOR,        "用来调节灯的颜色,值16进制RGB,0xFF0000代表红色,0x00FF00代表绿色,0x0000FF代表蓝色"}, 
    {OP_LIGHT_BRIGHTNESS,   "用来调节灯的亮度,值1-255"},
    {OP_LIGHT_BREATH,       "灯光呼吸效果,值0-1"},
    {OP_LIGHT_RAINBOW,      "彩虹跑马灯效果,值0-255,值越大转的越快"},
};


#endif