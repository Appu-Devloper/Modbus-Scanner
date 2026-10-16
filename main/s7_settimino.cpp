#include "Settimino.h"
#include "scanner_config.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <cstdio>
extern "C" void s7_settimino_test(const char *target,esp_netif_t *netif) {
    IPAddress address;
    in_addr parsed={};
    if(inet_pton(AF_INET,target,&parsed)!=1 || !netif ||
       esp_netif_get_netif_impl_name(netif,settimino_interface)!=ESP_OK) {
        puts("Invalid Settimino target or interface.");return;
    }
    address.addr=parsed.s_addr;
    S7Client client;
    client.RecvTimeout=3000;
    client.SetConnectionType(OP);
    printf("Settimino 2.1.0: connecting to %s:102, rack %d, slot %d, OP\n",target,S7_RACK,S7_SLOT);
    int result=client.ConnectTo(address,S7_RACK,S7_SLOT);
    printf("Settimino ConnectTo result=0x%04X\n",result);
    if(result==0) {
        uint8_t input=0;
        printf("Settimino: reading IB%d once.\n",S7_INPUT_BYTE);
        result=client.ReadArea(S7AreaPE,0,S7_INPUT_BYTE,1,&input);
        printf("Settimino ReadArea result=0x%04X\n",result);
        if(result==0) printf("S7 READ SUCCESS: IB%d=0x%02X (%u)\n",S7_INPUT_BYTE,input,input);
        else puts("No input data retrieved. See PLC response error above, if present.");
    }
    client.Disconnect();
    puts("Settimino read-only test complete. Reset to repeat.");
}
