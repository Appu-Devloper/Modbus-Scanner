#pragma once
#include <stdatomic.h>
void tcp_scanner_start(atomic_bool *busy,atomic_bool *cancel);
