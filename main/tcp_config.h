#pragma once
#include <stdbool.h>
#include <stdint.h>

int tcp_parse_list(const char *s,int lo,int hi,int *out,int cap);
bool tcp_prefix_valid(const char *s);
