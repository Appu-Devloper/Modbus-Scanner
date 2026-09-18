#include "tcp_config.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
static int number(const char *s,int lo,int hi) {
    if(!*s) return -1;
    for(const char *p=s;*p;p++) if(*p<'0' || *p>'9') return -1;
    char *end;errno=0;long v=strtol(s,&end,10);
    return errno || *end || v<lo || v>hi ? -1:(int)v;
}
int tcp_parse_list(const char *s,int lo,int hi,int *out,int cap) {
    if(!*s || strlen(s)>=128) return -1;
    char copy[128];strcpy(copy,s);int n=0;char *part=copy;
    while(part) {
        char *next=strchr(part,',');if(next) *next++=0;
        char *dash=strchr(part,'-');if(dash) *dash++=0;
        int first=number(part,lo,hi),last=dash?number(dash,lo,hi):first;
        if(first<0 || last<first) return -1;
        for(int v=first;v<=last;v++) {
            bool seen=false;for(int k=0;k<n;k++) if(out[k]==v) seen=true;
            if(!seen) {if(n==cap) return -1;out[n++]=v;}
        }
        part=next;
    }
    return n;
}
bool tcp_prefix_valid(const char *s) {
    if(strlen(s)>12) return false;
    for(int i=0;i<3;i++) {
        const char *start=s;unsigned value=0;
        while(*s>='0' && *s<='9') {value=value*10+(*s++-'0');if(value>255) return false;}
        if(s==start || s-start>3 || (s-start>1 && *start=='0') || *s++!='.') return false;
    }
    return !*s;
}

bool tcp_subnet_range(uint32_t ip,uint32_t mask,uint32_t limit,
                      uint32_t *first,uint32_t *last) {
    uint32_t hosts=~mask;
    if(!first || !last || !mask || !ip || (hosts & (hosts+1U))) return false;
    uint32_t lo=ip & mask,hi=lo | hosts;
    if(hosts>1) {lo++;hi--;}
    if(ip<lo || ip>hi || (uint64_t)hi-lo+1>limit) return false;
    *first=lo;*last=hi;return true;
}
