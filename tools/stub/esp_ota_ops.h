#pragma once
#include <stdint.h>
#define ESP_OK 0
typedef struct { uint32_t address; uint32_t size; const char* label; } esp_partition_t;
const esp_partition_t* esp_ota_get_next_update_partition(const void*);
int esp_ota_set_boot_partition(const esp_partition_t*);
const esp_partition_t* esp_ota_get_running_partition();
