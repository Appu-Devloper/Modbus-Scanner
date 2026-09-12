#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modbus.h"
static void crc(uint8_t *p,size_t n) {uint16_t c=mb_crc(p,n);p[n]=c;p[n+1]=c>>8;}
int main(void) {
    uint8_t req[8]; const uint8_t expected[]={1,3,0,0,0,10,0xc5,0xcd};
    assert(mb_request(req,1,3,0,10)==8);assert(!memcmp(req,expected,8));
    assert(!mb_request(req,0,3,0,1));assert(!mb_request(req,248,3,0,1));
    assert(!mb_request(req,1,6,0,1));assert(!mb_request(req,1,3,65535,2));
    assert(!mb_request(req,1,3,0,126));assert(mb_request(req,247,4,65535,1));
    assert(mb_request(req,1,1,0,2000));assert(!mb_request(req,1,1,0,2001));
    uint8_t data[]={1,3,4,0,42,0x12,0x34,0,0};crc(data,7);
    assert(mb_response(data,9,1,3,2)==1);
    for(size_t n=0;n<9;n++) assert(!mb_response(data,n,1,3,2));
    assert(!mb_response(data,9,2,3,2));assert(!mb_response(data,9,1,4,2));
    assert(!mb_response(data,9,1,3,1));data[4]^=1;assert(!mb_response(data,9,1,3,2));
    uint8_t ex[]={1,0x83,2,0,0};crc(ex,3);assert(mb_response(ex,5,1,3,1)==2);
    uint8_t bits[]={1,1,2,0xff,1,0,0};crc(bits,5);assert(mb_response(bits,7,1,1,9)==1);
    uint8_t max[255]={1,3,250};crc(max,253);assert(mb_response(max,255,1,3,125)==1);
    puts("Modbus protocol tests passed");
}
