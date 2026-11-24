#pragma once
#include <cstddef>
struct esp_partition_t { size_t size; };
const esp_partition_t* esp_ota_get_next_update_partition(const void*);
