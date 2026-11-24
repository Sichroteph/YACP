#pragma once

#include <cstddef>

// YACP's X3 Home date, using CrossPoint's already-local RTC value.
// The caller owns the buffer. No allocation, extra polling or timezone offset.
bool formatYacpHeaderDate(char* buffer, size_t length);
