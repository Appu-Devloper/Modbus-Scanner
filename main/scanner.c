#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus.h"
#include "tcp_scanner.h"
#include "scanner_config.h"

#define PORT UART_PORT_NUM
static atomic_bool busy, cancel_scan;
static int baud=UART_BAUD_RATE, stops=RS485_STOP_BITS, timeout_ms=RS485_TIMEOUT_MS, retries=RS485_RETRIES;
static char parity=RS485_PARITY;
typedef struct { int mode, id, last_id, fc, start, end, count, step, all; } job_t;
static job_t job;
static void serial_config(int b, char p, int s) {
    uart_config_t c={.baud_rate=b,.data_bits=UART_DATA_8_BITS,
        .parity=p=='E'?UART_PARITY_EVEN:p=='O'?UART_PARITY_ODD:UART_PARITY_DISABLE,
        .stop_bits=s==2?UART_STOP_BITS_2:UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(PORT,&c)); baud=b; parity=p; stops=s;
}
static void init_uart(int tx, int rx, int de) {
    serial_config(baud,parity,stops);
    ESP_ERROR_CHECK(uart_set_pin(PORT,tx,rx,de,UART_PIN_NO_CHANGE));
    // Driver ring buffer is larger than the 256-byte RTU frame buffer.
    ESP_ERROR_CHECK(uart_driver_install(PORT,UART_BUF_SIZE*4,0,0,NULL,0));
    ESP_ERROR_CHECK(uart_set_mode(PORT,de<0?UART_MODE_UART:UART_MODE_RS485_HALF_DUPLEX));
}
static void led(int pin, bool on) {
    gpio_set_level(pin,on?LED_ACTIVE_LEVEL:!LED_ACTIVE_LEVEL);
}
static int exchange(int id,int fc,int start,int count,uint8_t *rx) {
    uint8_t tx[8]; if (!mb_request(tx,id,fc,start,count)) return 0;
    for(int attempt=0;attempt<=retries && !atomic_load(&cancel_scan);attempt++) {
        // Conservative inter-frame silence; timeout also includes full response wire time.
        int gap_ms=baud>19200?2:(3500*(9+(parity!='N')+stops)+baud-1)/baud;
        vTaskDelay(pdMS_TO_TICKS(gap_ms+1));
        ESP_ERROR_CHECK(uart_flush_input(PORT));
        led(MODBUS_TX_LED,true);
        int sent=uart_write_bytes(PORT,tx,8);
        esp_err_t tx_status=uart_wait_tx_done(PORT,pdMS_TO_TICKS(1000));
        led(MODBUS_TX_LED,false);
        if(sent!=8 || tx_status!=ESP_OK) return 0;
        int wire_ms=((fc<3?(count+7)/8:count*2)+5)*12000/baud+1;
        int64_t deadline=esp_timer_get_time()+(int64_t)(timeout_ms+wire_ms)*1000;
        size_t n=0;
        while(esp_timer_get_time()<deadline && !atomic_load(&cancel_scan)) {
            int got=uart_read_bytes(PORT,rx+n,1,pdMS_TO_TICKS(10));
            if(got<=0) continue;
            led(MODBUS_RX_LED,true);
            n++;
            int result=mb_response(rx,n,id,fc,count);
            if(result) {led(MODBUS_RX_LED,false);return result;}
            if(n>=UART_BUF_SIZE) break;
        }
        led(MODBUS_RX_LED,false);
    }
    return 0;
}
static void report(int id,int fc,int start,int count,int result,uint8_t *rx) {
    printf("%s,%d,%d,8%c%d,%d,%d,%d",result==1?"DATA":"EXCEPTION",id,baud,parity,stops,fc,start,count);
    if(result==2) printf(",code=%u",rx[2]);
    else for(int i=0;i<count;i++) {
        unsigned v=fc<3?((rx[3+i/8]>>(i%8))&1):((unsigned)rx[3+2*i]<<8)|rx[4+2*i];
        printf(",%u",v);
    }
    puts("");
}
static void worker(void *unused) {
    (void)unused;
    job_t j=job; uint8_t rx[UART_BUF_SIZE]; unsigned found=0, probes=0;
    int old_b=baud, old_s=stops; char old_p=parity;
    if(j.mode==0) {
        const int rates[]=RS485_SCAN_BAUD_RATES;
        int combinations=j.all?(int)(sizeof(rates)/sizeof(rates[0]))*6:1;
        for(int c=0;c<combinations && !atomic_load(&cancel_scan);c++) {
            if(j.all) serial_config(rates[c/6],"NEO"[(c%6)/2],1+c%2);
            printf("Scanning %d 8%c%d, IDs %d..%d\n",baud,parity,stops,j.id,j.last_id);
            for(int id=j.id;id<=j.last_id && !atomic_load(&cancel_scan);id++) {
                for(int a=j.start;a<=j.end && !atomic_load(&cancel_scan);a+=j.step) {
                    int r=exchange(id,j.fc,a,j.count,rx); probes++;
                    if(r) {
                        r=exchange(id,j.fc,a,j.count,rx);
                        if(r) { report(id,j.fc,a,j.count,r,rx); found++; }
                    }
                    if(probes%100==0) printf("RS-485 progress: %u probes\n",probes);
                }
            }
        }
    } else {
        for(int a=j.start;a<=j.end && !atomic_load(&cancel_scan);a+=j.step) {
            if(j.count>65536-a) break;
            int r=exchange(j.id,j.fc,a,j.count,rx); probes++;
            if(r) {report(j.id,j.fc,a,j.count,r,rx); if(r==1) found++;}
            else if(j.mode==2) puts("No valid response (timeout, CRC, framing, or mismatched response).");
            if(probes%100==0) printf("Progress: %u probes, address %d\n",probes,a);
        }
    }
    serial_config(old_b,old_p,old_s);
    printf("%s: %u probes, %u matching results. Serial settings restored to %d 8%c%d.\n",
        atomic_load(&cancel_scan)?"Stopped":"Done",probes,found,baud,parity,stops);
    atomic_store(&busy,false); vTaskDelete(NULL);
}
void app_main(void) {
    puts(S7_ENABLED ? "Mode: S7 read." : TCP_ENABLED ? "Mode: TCP." :
        PING_ENABLED ? "Mode: ping." : "Mode: RS-485.");
    if (S7_ENABLED || TCP_ENABLED || PING_ENABLED) {
        puts("Mode: network. Ethernet first, Wi-Fi fallback if enabled; waiting for a network IP.");
        tcp_scanner_start(&busy,&cancel_scan);
        return;
    }
    puts("Mode: RS-485. Starting automatic scan.");
    job=(job_t){.mode=0,.id=SCAN_FIRST_ID,.last_id=SCAN_LAST_ID,
        .fc=SCAN_FUNCTION_CODE,.start=SCAN_START_REGISTER,.end=SCAN_LAST_START_REGISTER,
        .count=SCAN_REGISTER_COUNT,.step=SCAN_REGISTER_STEP,.all=RS485_SCAN_ALL_SETTINGS};
    uint8_t request[8];
    if(job.last_id<job.id || job.last_id>247 || job.end<job.start || job.end>65535 ||
       job.step<1 || job.step>65535 || job.count>65536-job.end ||
       !mb_request(request,job.id,job.fc,job.start,job.count) ||
       UART_BAUD_RATE<300 || UART_BAUD_RATE>1000000 ||
       !strchr("NEO",RS485_PARITY) || RS485_PARITY==0 ||
       RS485_STOP_BITS<1 || RS485_STOP_BITS>2 || RS485_TIMEOUT_MS<20 || RS485_TIMEOUT_MS>10000 ||
       RS485_RETRIES<0 || RS485_RETRIES>5) {
        puts("Invalid RS-485 settings in scanner_config.h; scan not started.");return;
    }
    const int rates[]=RS485_SCAN_BAUD_RATES;
    for(size_t i=0;i<sizeof(rates)/sizeof(rates[0]);i++) if(rates[i]<300 || rates[i]>1000000) {
        puts("Invalid baud rate in scanner_config.h; scan not started.");return;
    }
    gpio_config_t leds={.pin_bit_mask=(1ULL<<MODBUS_TX_LED)|(1ULL<<MODBUS_RX_LED),.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&leds));
    led(MODBUS_TX_LED,false);led(MODBUS_RX_LED,false);
    init_uart(UART_TXD_PIN,UART_RXD_PIN,TXE_PIN);
    atomic_store(&busy,true);
    if(xTaskCreate(worker,"scan",6144,NULL,5,NULL)!=pdPASS) {
        atomic_store(&busy,false);puts("Cannot start RS-485 scan worker.");
    }
}
