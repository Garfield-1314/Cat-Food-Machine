/* 模拟器桩：手动投喂（原实现在固件 main.c，这里仅模拟动作并打印日志） */
#include <stdint.h>
#include <stdio.h>

void manual_feeding_start(uint8_t slots)
{
    printf("[sim] manual_feeding_start: %u slot(s)\n", slots);
}
