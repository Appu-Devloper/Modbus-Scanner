#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "ping/ping_sock.h"
#include "scanner_config.h"
#include "esp_eth.h"
#include "esp_wifi.h"
#include "esp_hosted.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tcp_scanner.h"
#include "tcp_config.h"
#include "modbus_tcp.h"

static const struct {
    const char *ip_prefix,*last_octets,*custom_ports;
    bool use_common_port;
    int timeout_ms;
} tcp_config={TCP_IP_PREFIX,TCP_LAST_OCTETS,TCP_CUSTOM_PORTS,TCP_USE_COMMON_PORT,TCP_TIMEOUT_MS};
static atomic_bool *busy,*cancel;
static atomic_bool network_ready;
static atomic_bool wifi_ready;
static atomic_bool wifi_started;
static bool any_network_ready(void) {
    return atomic_load(&network_ready) || atomic_load(&wifi_ready);
}
static atomic_bool link_up;
static atomic_bool ethernet_static;
static char static_scan_prefix[13];
static int64_t dhcp_deadline;
static const char *const unknown_ips[]=ETH_UNKNOWN_SCANNER_IPS;
_Static_assert(sizeof(unknown_ips)/sizeof(unknown_ips[0])>0,"At least one discovery address is required");
static int unknown_index=-1;
static bool discovery_had_reply;
typedef struct {unsigned revision;bool had_reply;} discovery_result_t;
static uint32_t selected_ip,selected_mask; // event loop only

ESP_EVENT_DEFINE_BASE(SCANNER_NETWORK_EVENT);
static bool ethernet_started, ethernet_attempted;
static esp_netif_t *netif,*wifi_netif;
static atomic_uint network_revision;
static atomic_int preferred_network; // 0: unavailable, 1: Ethernet, 2: Wi-Fi
// Called only from the serialized event loop, after readiness changes.
static void select_network(void) {
    int selected=atomic_load(&network_ready)?1:(atomic_load(&wifi_ready)?2:0);
    if(selected) {
        esp_err_t err=esp_netif_set_default_netif(selected==1?netif:wifi_netif);
        if(err!=ESP_OK) printf("Default network selection failed: %s\n",esp_err_to_name(err));
    }
    esp_netif_ip_info_t current={0};
    if(selected) esp_netif_get_ip_info(selected==1?netif:wifi_netif,&current);
    bool address_changed=current.ip.addr!=selected_ip || current.netmask.addr!=selected_mask;
    selected_ip=current.ip.addr;selected_mask=current.netmask.addr;
    if(atomic_exchange(&preferred_network,selected)!=selected || address_changed) {
        atomic_fetch_add(&network_revision,1);
        puts(selected==1?"Active network: Ethernet.":
            selected==2?"Active network: Wi-Fi fallback.":"No network IP; waiting.");
    }
}
static uint16_t transaction;
typedef struct {int id,last_id,fc,start,end,count,step; bool scan; char ip[16];int port;} tcp_job_t;
static tcp_job_t job;
static void static_fallback_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;(void)base;
    if(id==1) {
        if(!data) return;
        const discovery_result_t *result=data;
        if(ETH_FORCE_STATIC || !ETH_UNKNOWN_NETWORK_ENABLED ||
           !atomic_load(&link_up) || result->revision!=atomic_load(&network_revision)) return;
        discovery_had_reply|=result->had_reply;
        int next_index=tcp_next_candidate(unknown_index,sizeof(unknown_ips)/sizeof(unknown_ips[0]),
            ETH_SCAN_ALL_SUBNETS,atomic_load(&ethernet_static),result->had_reply);
        if(next_index<0) {
            puts(discovery_had_reply?
                "Subnet scanning complete; Modbus responses were found. Keeping the current Ethernet address.":
                "DISCOVERY INCONCLUSIVE: no Modbus replies on the configured subnets. Other subnets, ports or read settings may be needed.");
            return;
        }
        esp_netif_ip_info_t next={0};
        esp_netif_str_to_ip4(unknown_ips[next_index],&next.ip);
        esp_netif_str_to_ip4("255.255.255.0",&next.netmask);
        esp_err_t err=esp_netif_dhcpc_stop(netif);
        if(err!=ESP_OK && err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            printf("Cannot stop DHCP for subnet trial: %s\n",esp_err_to_name(err));return;
        }
        err=esp_netif_set_ip_info(netif,&next);
        if(err!=ESP_OK) {printf("Discovery address change failed: %s\n",esp_err_to_name(err));esp_netif_dhcpc_start(netif);return;}
        unknown_index=next_index;
        atomic_store(&ethernet_static,true);
        printf("Subnet trial %d/%u: scanner IP %s/24 (DHCP stopped)\n",unknown_index+1,
            (unsigned)(sizeof(unknown_ips)/sizeof(unknown_ips[0])),unknown_ips[unknown_index]);
        unsigned previous=atomic_load(&network_revision);
        select_network();
        // A candidate can equal the current address; still start its scan.
        if(previous==atomic_load(&network_revision)) atomic_fetch_add(&network_revision,1);
        return;
    }
    if(ETH_FORCE_STATIC || !ETH_STATIC_FALLBACK_ENABLED || !atomic_load(&link_up) ||
       atomic_load(&network_ready) || !dhcp_deadline || esp_timer_get_time()<dhcp_deadline) return;
    esp_netif_ip_info_t info={0};
    // A DHCP lease may be ready while its GOT_IP event is still queued.
    if(esp_netif_get_ip_info(netif,&info)==ESP_OK && info.ip.addr!=0) return;
    if(unknown_index<0) unknown_index=0;
    const char *fallback_ip=ETH_UNKNOWN_NETWORK_ENABLED?unknown_ips[unknown_index]:ETH_STATIC_IP;
    const char *fallback_mask=ETH_UNKNOWN_NETWORK_ENABLED?"255.255.255.0":ETH_STATIC_NETMASK;
    const char *fallback_gateway=ETH_UNKNOWN_NETWORK_ENABLED?"0.0.0.0":ETH_STATIC_GATEWAY;
    if(esp_netif_str_to_ip4(fallback_ip,&info.ip)!=ESP_OK ||
       esp_netif_str_to_ip4(fallback_mask,&info.netmask)!=ESP_OK ||
       esp_netif_str_to_ip4(fallback_gateway,&info.gw)!=ESP_OK) {
        puts("Invalid Ethernet static IP settings.");dhcp_deadline=0;return;
    }
    esp_err_t err=esp_netif_dhcpc_stop(netif);
    if(err==ESP_OK || err==ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        err=esp_netif_set_ip_info(netif,&info);
        if(err==ESP_OK) {
            atomic_store(&ethernet_static,true);
            atomic_store(&network_ready,true);
            printf("DHCP timed out; Ethernet static IP: %s, mask: %s, gateway: %s\n",
                fallback_ip,fallback_mask,fallback_gateway);
            select_network();dhcp_deadline=0;return;
        }
        esp_netif_dhcpc_start(netif);
    }
    printf("Ethernet static fallback failed: %s; retrying later.\n",esp_err_to_name(err));
    dhcp_deadline=esp_timer_get_time()+ETH_DHCP_TIMEOUT_SECONDS*1000000LL;
}
static void network_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==IP_EVENT && id==IP_EVENT_ETH_GOT_IP) {
        if(!atomic_load(&link_up)) return;
        ip_event_got_ip_t *e=data;
        printf("Ethernet IP: " IPSTR ", netmask: " IPSTR ", gateway: " IPSTR "\n",
            IP2STR(&e->ip_info.ip),IP2STR(&e->ip_info.netmask),IP2STR(&e->ip_info.gw));
        atomic_store(&network_ready,true);
    } else if(base==ETH_EVENT && id==ETHERNET_EVENT_CONNECTED) {
        atomic_store(&link_up,true);
        dhcp_deadline=esp_timer_get_time()+ETH_DHCP_TIMEOUT_SECONDS*1000000LL;
        puts(ETH_FORCE_STATIC?"Ethernet link UP; using forced static address.":"Ethernet link UP; waiting for DHCP address.");
    } else if(base==ETH_EVENT && (id==ETHERNET_EVENT_DISCONNECTED || id==ETHERNET_EVENT_STOP)) {
        atomic_store(&link_up,false);atomic_store(&network_ready,false);
        dhcp_deadline=0;unknown_index=-1;discovery_had_reply=false;
        if(!ETH_FORCE_STATIC && atomic_exchange(&ethernet_static,false)) {
            esp_netif_ip_info_t empty={0};
            ESP_ERROR_CHECK(esp_netif_set_ip_info(netif,&empty));
            ESP_ERROR_CHECK(esp_netif_dhcpc_start(netif));
        }
        puts(id==ETHERNET_EVENT_STOP?"Ethernet stopped.":"Ethernet link DOWN.");
    } else if(base==IP_EVENT && id==IP_EVENT_ETH_LOST_IP) {
        atomic_store(&network_ready,false);
        dhcp_deadline=esp_timer_get_time()+ETH_DHCP_TIMEOUT_SECONDS*1000000LL;
        puts("Ethernet IP address lost; waiting for a new DHCP address.");
    }
    select_network();
}
static int start_ethernet(void) {
    if(ethernet_started) {puts(atomic_load(&network_ready)?"Ethernet ready":"Waiting for link/DHCP");return 0;}
    if(ethernet_attempted) {puts("Ethernet initialization failed; check board/PHY configuration and reset to retry.");return 1;}
    ethernet_attempted=true;
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_inherent_config_t eth_base=ESP_NETIF_INHERENT_DEFAULT_ETH();
    eth_base.route_prio=150; // Prefer Ethernet over the default Wi-Fi station (100).
    esp_netif_config_t cfg=ESP_NETIF_DEFAULT_ETH();cfg.base=&eth_base;netif=esp_netif_new(&cfg);
    if(!netif) {puts("Cannot allocate network interface; reset and retry.");return 1;}
    eth_mac_config_t mc=ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac=ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_phy_config_t pc=ETH_PHY_DEFAULT_CONFIG();pc.phy_addr=1;pc.reset_gpio_num=51;
    esp_eth_mac_t *mac=esp_eth_mac_new_esp32(&emac,&mc);
    esp_eth_phy_t *phy=esp_eth_phy_new_ip101(&pc);
    if(!mac || !phy) {puts("Cannot allocate Ethernet driver; reset and retry.");return 1;}
    esp_eth_config_t ec=ETH_DEFAULT_CONFIG(mac,phy);esp_eth_handle_t handle=NULL;
    esp_err_t result=esp_eth_driver_install(&ec,&handle);
    if(result!=ESP_OK) {
        printf("Ethernet initialization failed: %s. Verify Nano PHY wiring and reset to retry.\n",esp_err_to_name(result));
        mac->del(mac);phy->del(phy);esp_netif_destroy(netif);netif=NULL;
        return 1;
    }
    ESP_ERROR_CHECK(esp_netif_attach(netif,esp_eth_new_netif_glue(handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT,ESP_EVENT_ANY_ID,network_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_ETH_GOT_IP,network_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_ETH_LOST_IP,network_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SCANNER_NETWORK_EVENT,ESP_EVENT_ANY_ID,static_fallback_event,NULL));
    if(ETH_FORCE_STATIC) {
        esp_netif_ip_info_t fixed={0};
        ESP_ERROR_CHECK(esp_netif_str_to_ip4(ETH_STATIC_IP,&fixed.ip));
        ESP_ERROR_CHECK(esp_netif_str_to_ip4(ETH_STATIC_NETMASK,&fixed.netmask));
        ESP_ERROR_CHECK(esp_netif_str_to_ip4(ETH_STATIC_GATEWAY,&fixed.gw));
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif,&fixed));
        atomic_store(&ethernet_static,true);
    }
    ESP_ERROR_CHECK(esp_eth_start(handle));ethernet_started=true;
    puts(ETH_FORCE_STATIC?"Ethernet started with forced static configuration.":"Ethernet started (Nano IP101, MDC31 MDIO52 RESET51, PHY1); waiting for DHCP.");return 0;
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) {
        esp_err_t err=esp_wifi_connect();
        if(err!=ESP_OK) printf("Wi-Fi connect failed: %s\n",esp_err_to_name(err));
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e=data;
        atomic_store(&wifi_ready,false);
        printf("Wi-Fi disconnected (reason %u); reconnecting...\n",e->reason);
        esp_err_t err=esp_wifi_connect();
        if(err!=ESP_OK) printf("Wi-Fi reconnect failed: %s\n",esp_err_to_name(err));
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e=data;
        printf("Wi-Fi IP: " IPSTR ", netmask: " IPSTR ", gateway: " IPSTR "\n",
            IP2STR(&e->ip_info.ip),IP2STR(&e->ip_info.netmask),IP2STR(&e->ip_info.gw));
        atomic_store(&wifi_ready,true);
    } else if(base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        atomic_store(&wifi_ready,false);
        puts("Wi-Fi IP address lost; waiting for DHCP.");
    }
    select_network();
}
static esp_err_t start_wifi(void) {
    printf("Starting Wi-Fi fallback: %s\n",WIFI_SSID);
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK) return err;
    if(esp_hosted_init()!=0 || esp_hosted_connect_to_slave()!=0) return ESP_FAIL;
    wifi_netif=esp_netif_create_default_wifi_sta();
    if(!wifi_netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init);if(err!=ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_LOST_IP,wifi_event,NULL));
    wifi_config_t config={.sta={.ssid=WIFI_SSID,.password=WIFI_PASSWORD}};
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(err!=ESP_OK) return err;
    err=esp_wifi_set_mode(WIFI_MODE_STA);if(err!=ESP_OK) return err;
    err=esp_wifi_set_config(WIFI_IF_STA,&config);if(err!=ESP_OK) return err;
    err=esp_wifi_start();if(err==ESP_OK) atomic_store(&wifi_started,true);
    return err;
}
static void network_monitor(void *unused) {
    (void)unused;
    unsigned unavailable=0;
    bool attempted=false;
    for(;;) {
        if(ethernet_started) esp_event_post(SCANNER_NETWORK_EVENT,0,NULL,0,0);
        if(atomic_load(&network_ready)) unavailable=0;
        else if(unavailable<WIFI_FALLBACK_DELAY_SECONDS) unavailable++;
        if(WIFI_FALLBACK_ENABLED && !attempted && unavailable>=WIFI_FALLBACK_DELAY_SECONDS) {
            attempted=true;
            esp_err_t err=start_wifi();
            if(err!=ESP_OK)
                printf("Wi-Fi initialization failed: %s. Check C6 ESP-Hosted firmware; reset to retry. Ethernet remains available.\n",esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
static int wait_socket(int fd,bool write_ready,int64_t deadline) {
    while(!atomic_load(cancel) && esp_timer_get_time()<deadline) {
        fd_set fds;FD_ZERO(&fds);FD_SET(fd,&fds);
        int64_t left=deadline-esp_timer_get_time();if(left<=0) break;
        struct timeval tv={.tv_sec=0,.tv_usec=left<50000?left:50000};
        int r=select(fd+1,write_ready?NULL:&fds,write_ready?&fds:NULL,NULL,&tv);
        if(r>0) return 1;
        if(r<0 && errno!=EINTR) break;
    }
    return 0;
}
static int connect_peer(const char *ip,int port,int *socket_network) {
    while(!atomic_load(&preferred_network) && !atomic_load(cancel)) vTaskDelay(pdMS_TO_TICKS(250));
    if(atomic_load(cancel)) return -1;
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port)};
    if(inet_pton(AF_INET,ip,&address.sin_addr)!=1) return -1;
    int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(fd<0) return -1;
    *socket_network=atomic_load(&preferred_network);
    esp_netif_t *selected=*socket_network==1?netif:wifi_netif;
    struct ifreq iface={0};
    if(!selected || !*socket_network || esp_netif_get_netif_impl_name(selected,iface.ifr_name)!=ESP_OK ||
       setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,&iface,sizeof(iface))<0) {
        puts("Cannot bind TCP socket to selected network.");close(fd);return -1;
    }
    if(fcntl(fd,F_SETFL,O_NONBLOCK)<0) {close(fd);return -1;}
    int r=connect(fd,(struct sockaddr *)&address,sizeof(address));
    if(r<0) {
        if(errno!=EINPROGRESS || !wait_socket(fd,true,esp_timer_get_time()+(TCP_READ_MODBUS?tcp_config.timeout_ms:TCP_CONNECT_TIMEOUT_MS)*1000LL)) {close(fd);return -1;}
        int err=0;socklen_t len=sizeof(err);
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&len)<0 || err) {close(fd);return -1;}
    }
    return fd;
}
static bool transfer(int fd,uint8_t *p,size_t n,bool sending,int64_t deadline) {
    while(n && !atomic_load(cancel)) {
        if(!wait_socket(fd,sending,deadline)) return false;
        int r=sending?send(fd,p,n,0):recv(fd,p,n,0);
        if(r<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) continue;
        if(r<=0) return false;
        p+=r;n-=r;
    }
    return n==0;
}
static int exchange(int fd,int id,int start,uint8_t *rx) {
    uint8_t tx[12];uint16_t tid=++transaction;
    if(!mb_tcp_request(tx,tid,id,job.fc,start,job.count)) return 0;
    int64_t deadline=esp_timer_get_time()+tcp_config.timeout_ms*1000LL;
    if(!transfer(fd,tx,12,true,deadline) || !transfer(fd,rx,6,false,deadline)) return 0;
    size_t len=((size_t)rx[4]<<8)|rx[5];
    if(len<3 || len>254 || !transfer(fd,rx+6,len,false,deadline)) return 0;
    return mb_tcp_response(rx,len+6,tid,id,job.fc,job.count);
}
static void report(const char *ip,int port,int id,int start,int r,uint8_t *rx) {
    printf("%s | IP %s | Port %d | Unit ID %d | FC%02d | Register offset %d | Count %d",
        r==1?"READ OK":"MODBUS EXCEPTION",ip,port,id,job.fc,start,job.count);
    if(r==2) printf(" | Exception code %u (device responded, requested data unavailable)",rx[8]);
    else for(int i=0;i<job.count;i++) printf(i==0?" | Values: %u":", %u",job.fc<3?(rx[9+i/8]>>(i%8))&1:((unsigned)rx[9+2*i]<<8)|rx[10+2*i]);
    puts("");
}
static void worker(void *arg) {
    (void)arg;uint8_t rx[260];
scan_again:
    while(!atomic_load(&preferred_network) && !atomic_load(cancel)) vTaskDelay(pdMS_TO_TICKS(250));
    if(atomic_load(cancel)) {atomic_store(busy,false);vTaskDelete(NULL);return;}
    unsigned revision=atomic_load(&network_revision);
    unsigned matches=0,probes=0,endpoints=0,unit_responses=0,checked=0;
    int64_t scan_started=esp_timer_get_time();
    atomic_store(busy,true);
    int octets[254],ports[32];
    int no=tcp_parse_list(tcp_config.last_octets,1,254,octets,254);
    int np=tcp_config.use_common_port?(ports[0]=502,1):tcp_parse_list(tcp_config.custom_ports,1,65535,ports,32);
    esp_netif_ip_info_t scan_info={0};
    int scan_network=atomic_load(&preferred_network);
    if(esp_netif_get_ip_info(scan_network==1?netif:wifi_netif,&scan_info)!=ESP_OK) goto wait_for_change;
    uint32_t self_ip=ntohl(scan_info.ip.addr),first_ip=0,last_ip=0;
    bool auto_range=job.scan && (TCP_AUTO_SUBNET ||
        (scan_network==1 && atomic_load(&ethernet_static) && ETH_UNKNOWN_NETWORK_ENABLED && !ETH_FORCE_STATIC));
    if(auto_range) {
        if(!tcp_subnet_range(self_ip,ntohl(scan_info.netmask.addr),TCP_AUTO_MAX_HOSTS,&first_ip,&last_ip)) {
            printf("Automatic scan range unavailable: invalid mask or more than %u hosts. Set a manual target range or adjust TCP_AUTO_MAX_HOSTS; no partial scan was performed.\n",(unsigned)TCP_AUTO_MAX_HOSTS);
            goto wait_for_change;
        }
        no=(int)(last_ip-first_ip+1);
        struct in_addr first={.s_addr=htonl(first_ip)},last={.s_addr=htonl(last_ip)};
        char a[16],z[16];inet_ntop(AF_INET,&first,a,sizeof(a));inet_ntop(AF_INET,&last,z,sizeof(z));
        printf("Automatic target range: %s–%s (scanner's own IP skipped)\n",a,z);
    }
    if(!job.scan) {no=np=1;ports[0]=job.port;}
    for(int o=0;o<no && !atomic_load(cancel);o++) for(int p=0;p<np && !atomic_load(cancel);p++) {
        if(revision!=atomic_load(&network_revision)) goto scan_again;
        const char *prefix=atomic_load(&preferred_network)==1 && atomic_load(&ethernet_static)?
            static_scan_prefix:tcp_config.ip_prefix;
        char ip[16];
        if(auto_range) {
            uint32_t target=first_ip+(uint32_t)o;
            if(target==self_ip) continue;
            struct in_addr addr={.s_addr=htonl(target)};
            inet_ntop(AF_INET,&addr,ip,sizeof(ip));
        } else if(job.scan) snprintf(ip,sizeof(ip),"%s%d",prefix,octets[o]);
        else strcpy(ip,job.ip);
        struct in_addr target_address;
        if(inet_pton(AF_INET,ip,&target_address)==1 && ntohl(target_address.s_addr)==self_ip) continue;
        checked++;
        if(checked==1 || checked%10==0)
            printf("Scan progress: checking endpoint %u/%d | %u open endpoints found\n",checked,no*np,endpoints);
        int socket_network=0;
        int fd=connect_peer(ip,ports[p],&socket_network);
        if(fd<0) continue;
        if(!TCP_READ_MODBUS) {
            printf("TCP OPEN: %s:%d\n",ip,ports[p]);
            endpoints++;
            close(fd);
            continue;
        }
        printf("Port open: %s:%d | Checking unit IDs %d–%d...\n",ip,ports[p],job.id,job.last_id);
        bool responding_ids[256]={0};
        unsigned endpoint_replies=0;
        for(int id=job.id;id<=job.last_id && !atomic_load(cancel);id++) {
            for(int a=job.start;a<=job.end && !atomic_load(cancel);a+=job.step) {
                if(revision!=atomic_load(&network_revision)) {
                    if(fd>=0) close(fd);
                    puts("Network changed; restarting target scan on the selected network.");
                    goto scan_again;
                }
                if(fd>=0 && socket_network!=atomic_load(&preferred_network)) {
                    close(fd);fd=-1;
                    puts("Network changed; reconnecting Modbus TCP on the preferred interface.");
                }
                if(fd<0) fd=connect_peer(ip,ports[p],&socket_network);
                if(fd<0) break;
                int r=exchange(fd,id,a,rx);probes++;
                if(r) {report(ip,ports[p],id,a,r,rx);matches++;endpoint_replies++;responding_ids[id]=true;}
                else {close(fd);fd=-1;if(!job.scan) printf("TCP_NO_RESPONSE,%s,%d,%d,%d\n",ip,ports[p],id,a);}
                if(probes%100==0) printf("TCP progress: %u requests\n",probes);
            }
        }
        if(fd>=0) close(fd);
        if(endpoint_replies) {
            endpoints++;
            printf("FOUND | %s:%d | Responding unit IDs: ",ip,ports[p]);
            bool first=true;
            for(int id=job.id;id<=job.last_id;id++) {
                if(!responding_ids[id]) continue;
                int last=id;
                while(last<job.last_id && responding_ids[last+1]) last++;
                printf(first?"%d":", %d",id);
                if(last>id) printf("–%d",last);
                unit_responses+=(unsigned)(last-id+1);id=last;first=false;
            }
            puts("\nResponding IDs do not necessarily represent separate physical devices.");
        } else puts("Port was open, but no valid Modbus reply was received for the configured reads.");
    }
    if(!TCP_READ_MODBUS) {
        printf("TCP PORT SCAN %s: %u endpoints checked, %u open.\n",
            atomic_load(cancel)?"STOPPED":"COMPLETE",checked,endpoints);
        puts("An open TCP port does not confirm application-protocol access.");
        goto wait_for_change;
    }
    printf("\nSCAN %s | %u Modbus endpoints | %u responding IP/port/unit combinations | %.1f seconds\n",
        atomic_load(cancel)?"STOPPED":"COMPLETE",endpoints,unit_responses,
        (esp_timer_get_time()-scan_started)/1000000.0);
    if(!matches) puts("No Modbus replies found. Check device power, target subnet, TCP port, and supported register/function.");
    puts("No reply to this read does not prove that a device is absent.");
    printf("TCP %s: %u requests, %u validated responses\n",atomic_load(cancel)?"stopped":"done",probes,matches);
    if(!atomic_load(cancel) && scan_network==1 && !ETH_FORCE_STATIC && ETH_UNKNOWN_NETWORK_ENABLED &&
       (ETH_SCAN_ALL_SUBNETS || atomic_load(&ethernet_static))) {
        discovery_result_t result={revision,matches>0};
        esp_err_t err=esp_event_post(SCANNER_NETWORK_EVENT,1,&result,sizeof(result),pdMS_TO_TICKS(100));
        if(err!=ESP_OK) printf("Cannot advance subnet discovery: %s\n",esp_err_to_name(err));
    }
wait_for_change:
    atomic_store(busy,false);
    while(!atomic_load(cancel) && revision==atomic_load(&network_revision)) vTaskDelay(pdMS_TO_TICKS(250));
    if(!atomic_load(cancel)) goto scan_again;
    vTaskDelete(NULL);
}
static void ping_success(esp_ping_handle_t ping, void *args) {
    (void)args;
    uint16_t seq=0;
    uint32_t elapsed=0;
    esp_ping_get_profile(ping,ESP_PING_PROF_SEQNO,&seq,sizeof(seq));
    esp_ping_get_profile(ping,ESP_PING_PROF_TIMEGAP,&elapsed,sizeof(elapsed));
    printf("PING SUCCESS: %s seq=%u time=%lu ms\n",PING_TARGET_IP,seq,(unsigned long)elapsed);
}
static void ping_timeout(esp_ping_handle_t ping, void *args) {
    (void)args;
    uint16_t seq=0;
    esp_ping_get_profile(ping,ESP_PING_PROF_SEQNO,&seq,sizeof(seq));
    printf("PING TIMEOUT: %s seq=%u\n",PING_TARGET_IP,seq);
}
static void ping_end(esp_ping_handle_t ping, void *args) {
    (void)args;
    uint32_t sent=0,received=0;
    esp_ping_get_profile(ping,ESP_PING_PROF_REQUEST,&sent,sizeof(sent));
    esp_ping_get_profile(ping,ESP_PING_PROF_REPLY,&received,sizeof(received));
    printf("PING COMPLETE: %lu sent, %lu received. Reset to repeat.\n",
        (unsigned long)sent,(unsigned long)received);
    if(!received) puts("Some PLCs block ICMP; failed ping does not prove the PLC is unreachable.");
    esp_ping_delete_session(ping);
    atomic_store(busy,false);
}
static void ping_plc(void) {
    esp_ping_config_t config=ESP_PING_DEFAULT_CONFIG();
    if(!ipaddr_aton(PING_TARGET_IP,&config.target_addr)) {
        puts("Invalid PLC ping address.");atomic_store(busy,false);return;
    }
    config.count=4;
    config.interval_ms=1000;
    config.timeout_ms=1000;
    esp_netif_t *selected=atomic_load(&preferred_network)==1?netif:wifi_netif;
    int index=selected?esp_netif_get_netif_impl_index(selected):-1;
    if(index<=0) {
        puts("Cannot select ping network interface.");atomic_store(busy,false);return;
    }
    config.interface=(uint32_t)index;
    esp_ping_callbacks_t callbacks={.on_ping_success=ping_success,
        .on_ping_timeout=ping_timeout,.on_ping_end=ping_end};
    esp_ping_handle_t ping=NULL;
    printf("Pinging %s: 4 requests, interval 1000 ms, timeout 1000 ms\n",PING_TARGET_IP);
    esp_err_t err=esp_ping_new_session(&config,&callbacks,&ping);
    if(err==ESP_OK) {
        err=esp_ping_start(ping);
        if(err!=ESP_OK) esp_ping_delete_session(ping);
    }
    if(err!=ESP_OK) {
        printf("Cannot start PLC ping: %s\n",esp_err_to_name(err));
        atomic_store(busy,false);
    }
}
extern void s7_settimino_test(const char *target,esp_netif_t *netif);
static void test_plc_port(void) {
    int sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int error=0;
    if(sock<0) {
        error=errno;
        printf("TCP socket error=%d (%s)\n",error,strerror(error));
        return;
    }
    struct sockaddr_in destination={.sin_family=AF_INET,.sin_port=htons(TCP_TARGET_PORT)};
    if(inet_pton(AF_INET,TCP_TARGET_IP,&destination.sin_addr)!=1) {
        puts("Invalid PLC address.");close(sock);return;
    }
    esp_netif_t *selected=atomic_load(&preferred_network)==1?netif:wifi_netif;
    struct ifreq iface={0};
    if(!selected || esp_netif_get_netif_impl_name(selected,iface.ifr_name)!=ESP_OK) {
        puts("Cannot select TCP test interface.");close(sock);return;
    }
    if(setsockopt(sock,SOL_SOCKET,SO_BINDTODEVICE,&iface,sizeof(iface))<0 ||
       fcntl(sock,F_SETFL,O_NONBLOCK)<0) {
        error=errno;
        printf("TCP socket setup error=%d (%s)\n",error,strerror(error));
        close(sock);return;
    }
    printf("Testing TCP %s:%d (connect timeout %d ms)\n",
        TCP_TARGET_IP,TCP_TARGET_PORT,TCP_CONNECT_TIMEOUT_MS);
    int64_t deadline=esp_timer_get_time()+TCP_CONNECT_TIMEOUT_MS*1000LL;
    int result=connect(sock,(struct sockaddr *)&destination,sizeof(destination));
    if(result<0) {
        error=errno;
        if(error==EINPROGRESS) {
            for(;;) {
                int64_t remaining=deadline-esp_timer_get_time();
                if(remaining<=0) {error=ETIMEDOUT;break;}
                fd_set write_fds,error_fds;
                FD_ZERO(&write_fds);FD_SET(sock,&write_fds);
                FD_ZERO(&error_fds);FD_SET(sock,&error_fds);
                struct timeval timeout={.tv_sec=remaining/1000000,.tv_usec=remaining%1000000};
                int ready=select(sock+1,NULL,&write_fds,&error_fds,&timeout);
                if(ready<0) {error=errno;if(error==EINTR) continue;break;}
                if(ready==0) {error=ETIMEDOUT;break;}
                socklen_t size=sizeof(error);
                if(getsockopt(sock,SOL_SOCKET,SO_ERROR,&error,&size)<0) error=errno;
                break;
            }
        }
    }
    if(!error) {
        printf("SUCCESS: TCP port %d accepts connections.\n",TCP_TARGET_PORT);

    }
    else {
        printf("TCP connect error=%d (%s)\n",error,strerror(error));
        if(error==ECONNRESET) puts("Connection reset by peer; check PLC service, client limits and access restrictions.");
        else if(error==ECONNREFUSED) puts("Connection refused; port may be closed or actively rejected.");
        else if(error==ETIMEDOUT) puts("Connection timed out; no TCP response within 3 seconds. Filtering or packet loss is possible.");
    }
    close(sock);
    puts("TCP test complete. Reset to repeat.");
}
static void automatic_scan(void *unused) {
    (void)unused;
    unsigned waits=0;
    while(!any_network_ready()) {
        if(waits++%10==0) {
            if(atomic_load(&wifi_started)) puts("Waiting for Wi-Fi connection/DHCP or Ethernet IP...");
            else if(atomic_load(&link_up))
                puts("Ethernet link is UP, but no DHCP address. Waiting for DHCP or configured static fallback.");
            else
                puts("Waiting for Ethernet link: check RJ45 cable, powered switch/device, and link LEDs.");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if(S7_ENABLED) {
        s7_settimino_test(S7_TARGET_IP,atomic_load(&preferred_network)==1?netif:wifi_netif);
        atomic_store(busy,false);
        vTaskDelete(NULL);
        return;
    }
    if((TCP_ENABLED && !TCP_SCAN_IPS)) {
        test_plc_port();
        atomic_store(busy,false);
        vTaskDelete(NULL);
        return;
    }
    if(PING_ENABLED) {
        ping_plc();
        vTaskDelete(NULL);
        return;
    }
    puts(TCP_READ_MODBUS?"Network ready. Starting Modbus TCP scan.":
        "Network ready. Starting TCP IP/port scan.");
    worker(NULL); // Scans again whenever the selected network changes.
}
void tcp_scanner_start(atomic_bool *b,atomic_bool *c) {
    busy=b;cancel=c;
    if(TCP_ENABLED && (TCP_CONNECT_TIMEOUT_MS<20 || TCP_CONNECT_TIMEOUT_MS>10000 ||
       (!TCP_SCAN_IPS && (TCP_TARGET_PORT<1 || TCP_TARGET_PORT>65535)))) {
        puts("Invalid TCP port or connection timeout.");return;
    }
    if(S7_ENABLED && (S7_RACK<0 || S7_RACK>7 || S7_SLOT<0 || S7_SLOT>31 ||
       S7_INPUT_BYTE<0 || S7_INPUT_BYTE>65535)) {
        puts("Invalid S7 rack, slot or input byte.");return;
    }

    if(TCP_AUTO_MAX_HOSTS<2 || TCP_AUTO_MAX_HOSTS>65534 ||
       (ETH_UNKNOWN_NETWORK_ENABLED && !ETH_STATIC_FALLBACK_ENABLED)) {
        puts("Invalid automatic discovery settings; scan not started.");return;
    }
    if(ETH_UNKNOWN_NETWORK_ENABLED) {
        for(size_t i=0;i<sizeof(unknown_ips)/sizeof(unknown_ips[0]);i++) {
            esp_ip4_addr_t ip;
            if(esp_netif_str_to_ip4(unknown_ips[i],&ip)!=ESP_OK ||
               (ntohl(ip.addr)&255)==0 || (ntohl(ip.addr)&255)==255) {
                puts("Invalid unknown-network candidate address; scan not started.");return;
            }
        }
    }
    if(ETH_STATIC_FALLBACK_ENABLED || ETH_FORCE_STATIC) {
        esp_netif_ip_info_t info={0};
        const char *last_dot=strrchr(ETH_STATIC_IP,'.');
        size_t prefix_len=last_dot?(size_t)(last_dot-ETH_STATIC_IP)+1:0;
        if(!prefix_len || prefix_len>=sizeof(static_scan_prefix) ||
           esp_netif_str_to_ip4(ETH_STATIC_IP,&info.ip)!=ESP_OK ||
           esp_netif_str_to_ip4(ETH_STATIC_NETMASK,&info.netmask)!=ESP_OK ||
           esp_netif_str_to_ip4(ETH_STATIC_GATEWAY,&info.gw)!=ESP_OK) {
            puts("Invalid Ethernet static address settings; scan not started.");return;
        }
        memcpy(static_scan_prefix,ETH_STATIC_IP,prefix_len);
        static_scan_prefix[prefix_len]=0;
    }
    if(TCP_ENABLED && TCP_SCAN_IPS) {
        printf("TCP scan: %s | IP range %s%s | ports %s\n",
            TCP_READ_MODBUS?"Modbus reads":"port checks only",
            TCP_AUTO_SUBNET?"automatic subnet":"",TCP_AUTO_SUBNET?"":TCP_IP_PREFIX TCP_LAST_OCTETS,
            TCP_USE_COMMON_PORT?"502":TCP_CUSTOM_PORTS);
        job=(tcp_job_t){.scan=true,.id=SCAN_FIRST_ID,.last_id=SCAN_LAST_ID,
            .fc=SCAN_FUNCTION_CODE,.start=SCAN_START_REGISTER,.end=SCAN_LAST_START_REGISTER,
            .count=SCAN_REGISTER_COUNT,.step=SCAN_REGISTER_STEP};
        uint8_t request[12];int octets[254],ports[32];
        if(job.id<0 || job.last_id<job.id || job.last_id>255 || job.end<job.start || job.end>65535 ||
           job.step<1 || job.step>65535 || job.count>65536-job.end ||
           !mb_tcp_request(request,0,job.id,job.fc,job.start,job.count) ||
           !tcp_prefix_valid(tcp_config.ip_prefix) ||
           (ETH_STATIC_FALLBACK_ENABLED && (!tcp_prefix_valid(static_scan_prefix) || ETH_DHCP_TIMEOUT_SECONDS<1)) ||
           tcp_parse_list(tcp_config.last_octets,1,254,octets,254)<1 ||
           (!tcp_config.use_common_port && tcp_parse_list(tcp_config.custom_ports,1,65535,ports,32)<1) ||
           tcp_config.timeout_ms<20 || tcp_config.timeout_ms>10000) {
            puts("Invalid TCP settings in scanner_config.h; scan not started.");return;
        }
        printf("TCP targets: %s, ports=%s, IDs=%d..%d, FC=%d, starts=%d..%d, count=%d\n",
            TCP_AUTO_SUBNET?"automatic from interface IP/netmask":"manual (scanner_config.h)",tcp_config.use_common_port?"502":tcp_config.custom_ports,
            job.id,job.last_id,job.fc,job.start,job.end,job.count);
    }
    if(start_ethernet()!=0 && !WIFI_FALLBACK_ENABLED) return;
    if(xTaskCreate(network_monitor,"network_monitor",8192,NULL,5,NULL)!=pdPASS) {
        puts("Cannot start network fallback monitor.");return;
    }
    atomic_store(busy,true);
    if(xTaskCreate(automatic_scan,"tcp_auto",8192,NULL,5,NULL)!=pdPASS) {
        atomic_store(busy,false);puts("Cannot start TCP scan worker.");
    }
}
