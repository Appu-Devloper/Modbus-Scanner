#include "modbus_tcp.h"
#include "modbus.h"
#include <string.h>
int mb_tcp_request(uint8_t out[12],uint16_t tid,int id,int fc,int start,int count) {
    uint8_t rtu[8];
    if(id<0 || id>255 || !mb_request(rtu,1,fc,start,count)) return 0;
    out[0]=tid>>8;out[1]=tid;out[2]=out[3]=out[4]=0;out[5]=6;out[6]=id;
    memcpy(out+7,rtu+1,5);return 12;
}
int mb_tcp_response(const uint8_t *p,size_t n,uint16_t tid,int id,int fc,int count) {
    if(n<9 || n>260 || p[0]!=(tid>>8) || p[1]!=(tid&255) || p[2] || p[3] ||
       (((size_t)p[4]<<8)|p[5])!=n-6 || p[6]!=id) return 0;
    if(p[7]==(fc|0x80)) return n==9 && p[8]!=0 ? 2:0;
    int bytes=fc<3?(count+7)/8:2*count;
    return p[7]==fc && p[8]==bytes && n==(size_t)bytes+9 ? 1:0;
}
