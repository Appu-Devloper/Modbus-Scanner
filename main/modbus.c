#include "modbus.h"
uint16_t mb_crc(const uint8_t *p, size_t n) {
    uint16_t c = 0xffff;
    while (n--) { c ^= *p++; for (int i=0;i<8;i++) c = (c>>1) ^ ((c&1)?0xa001:0); }
    return c;
}
int mb_request(uint8_t out[8], int id, int fc, int start, int count) {
    if (id<1 || id>247 || fc<1 || fc>4 || start<0 || start>65535 ||
        count<1 || count>(fc<3?2000:125) || count>65536-start) return 0;
    out[0]=id; out[1]=fc; out[2]=start>>8; out[3]=start;
    out[4]=count>>8; out[5]=count;
    uint16_t crc=mb_crc(out,6); out[6]=crc; out[7]=crc>>8; return 8;
}
int mb_response(const uint8_t *p, size_t n, int id, int fc, int count) {
    if (n<5 || p[0]!=id || mb_crc(p,n)!=0) return 0;
    if (p[1]==(fc|0x80) && n==5 && p[2]!=0) return 2;
    int bytes=fc<3?(count+7)/8:count*2;
    return p[1]==fc && n==(size_t)bytes+5 && p[2]==bytes ? 1 : 0;
}
