#pragma once
#include <stdbool.h>
#include <stdint.h>

int tcp_parse_list(const char *s,int lo,int hi,int *out,int cap);
bool tcp_prefix_valid(const char *s);

/* IPv4 values in host byte order. Reject invalid masks and oversized scans. */
bool tcp_subnet_range(uint32_t ip, uint32_t mask, uint32_t limit,
                      uint32_t *first, uint32_t *last);
