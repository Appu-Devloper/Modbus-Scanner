#pragma once
#include <stdint.h>
#include <stddef.h>
int mb_tcp_request(uint8_t out[12], uint16_t tid, int id, int fc, int start, int count);
/* Returns data=1, exception=2, invalid=0. */
int mb_tcp_response(const uint8_t *p, size_t n, uint16_t tid, int id, int fc, int count);
