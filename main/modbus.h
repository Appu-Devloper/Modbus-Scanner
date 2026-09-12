#pragma once
#include <stddef.h>
#include <stdint.h>
uint16_t mb_crc(const uint8_t *p, size_t n);
int mb_request(uint8_t out[8], int id, int fc, int start, int count);
/* 1 = data, 2 = exception, 0 = incomplete/invalid. */
int mb_response(const uint8_t *p, size_t n, int id, int fc, int count);
