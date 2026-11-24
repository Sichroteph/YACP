#pragma once
#include <cstdint>
struct esp_app_desc_t { uint32_t magic_word, secure_version, reserved[2]; char version[32]; };
