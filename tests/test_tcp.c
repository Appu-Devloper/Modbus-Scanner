#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modbus_tcp.h"
#include "tcp_config.h"
int main(void) {
    uint8_t tx[12];const uint8_t expected[]={0x12,0x34,0,0,0,6,255,3,0,100,0,2};
    assert(mb_tcp_request(tx,0x1234,255,3,100,2)==12);assert(!memcmp(tx,expected,12));
    assert(mb_tcp_request(tx,0,0,4,65535,1));
    assert(!mb_tcp_request(tx,0,256,3,0,1));assert(!mb_tcp_request(tx,0,1,6,0,1));
    assert(!mb_tcp_request(tx,0,1,3,65535,2));assert(!mb_tcp_request(tx,0,1,3,0,126));
    uint8_t rx[]={0x12,0x34,0,0,0,7,255,3,4,0,42,0x12,0x34};
    assert(mb_tcp_response(rx,sizeof(rx),0x1234,255,3,2)==1);
    for(size_t n=0;n<sizeof(rx);n++) assert(!mb_tcp_response(rx,n,0x1234,255,3,2));
    assert(!mb_tcp_response(rx,sizeof(rx),0x1235,255,3,2));
    assert(!mb_tcp_response(rx,sizeof(rx),0x1234,1,3,2));
    assert(!mb_tcp_response(rx,sizeof(rx),0x1234,255,4,2));
    assert(!mb_tcp_response(rx,sizeof(rx),0x1234,255,3,1));
    rx[2]=1;assert(!mb_tcp_response(rx,sizeof(rx),0x1234,255,3,2));rx[2]=0;
    rx[5]=6;assert(!mb_tcp_response(rx,sizeof(rx),0x1234,255,3,2));rx[5]=7;
    uint8_t ex[]={0,1,0,0,0,3,0,0x84,2};assert(mb_tcp_response(ex,9,1,0,4,1)==2);
    uint8_t bits[]={0,1,0,0,0,5,1,1,2,255,1};assert(mb_tcp_response(bits,11,1,1,1,9)==1);
    uint8_t max[259]={0,1,0,0,0,253,1,3,250};assert(mb_tcp_response(max,259,1,1,3,125)==1);
    int values[254];
    assert(tcp_parse_list("1,5,10-12,5",1,254,values,254)==5);
    assert(values[0]==1 && values[1]==5 && values[2]==10 && values[4]==12);
    assert(tcp_parse_list("1-254",1,254,values,254)==254);
    assert(tcp_parse_list("502,1502,65535",1,65535,values,32)==3);
    const char *bad[]={"","0","255","5-1","1,","1,,2","-1","+1","1-","1-2-3"," 1","999999999999999999999"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(tcp_parse_list(bad[i],1,254,values,254)==-1);
    assert(tcp_parse_list("1-33",1,65535,values,32)==-1);
    assert(tcp_prefix_valid("10.73.21."));assert(tcp_prefix_valid("192.168.1."));
    const char *prefixes[]={"192.168.1","192.168.1.1","256.1.1.","1..1.","01.2.3.","1.2.3.x","", "9999999999999999999.1.1."};
    for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++) assert(!tcp_prefix_valid(prefixes[i]));
    uint32_t first,last;
    assert(tcp_subnet_range(0xc0a80139,0xffffff00,1024,&first,&last));
    assert(first==0xc0a80101 && last==0xc0a801fe);
    assert(tcp_subnet_range(0xc0a80339,0xfffffe00,1024,&first,&last));
    assert(first==0xc0a80201 && last==0xc0a803fe);
    assert(tcp_subnet_range(0xc0a80105,0xfffffffc,1024,&first,&last));
    assert(first==0xc0a80105 && last==0xc0a80106);
    assert(tcp_subnet_range(0xc0a80104,0xfffffffe,1024,&first,&last));
    assert(first==0xc0a80104 && last==0xc0a80105);
    assert(tcp_subnet_range(0xc0a80105,0xffffffff,1024,&first,&last));
    assert(first==last && first==0xc0a80105);
    assert(!tcp_subnet_range(0x0a000001,0xffff0000,1024,&first,&last));
    assert(!tcp_subnet_range(0xc0a80105,0xffff00ff,1024,&first,&last));
    assert(!tcp_subnet_range(0xc0a80105,0,1024,&first,&last));
    assert(!tcp_subnet_range(0,0xffffff00,1024,&first,&last));
    assert(!tcp_subnet_range(0xc0a80105,0xffffff00,253,&first,&last));
    assert(!tcp_subnet_range(0xc0a80100,0xffffff00,1024,&first,&last));
    assert(!tcp_subnet_range(0xc0a801ff,0xffffff00,1024,&first,&last));
    assert(tcp_next_candidate(-1,4,true,false,true)==0); // DHCP found a device: continue.
    assert(tcp_next_candidate(0,4,true,true,true)==1); // Static reply also continues.
    assert(tcp_next_candidate(2,4,true,true,false)==3);
    assert(tcp_next_candidate(3,4,true,true,false)==-1); // No cycling after last.
    assert(tcp_next_candidate(-1,4,false,false,false)==-1); // Legacy DHCP behavior.
    assert(tcp_next_candidate(0,4,false,true,true)==-1); // Legacy stop on response.
    assert(tcp_next_candidate(0,4,false,true,false)==1);
    assert(tcp_next_candidate(-1,0,true,false,false)==-1);
    puts("TCP framing and configuration tests passed");
}
