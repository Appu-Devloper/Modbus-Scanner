#pragma once
#include <stdbool.h>
/* All settings are compiled into firmware. Rebuild and flash after editing.
 * Automatic scan on boot and selected-network changes; reset to repeat manually.
 */
/* Choose exactly ONE mode. Rebuild and flash after changing these flags. */
#define S7_ENABLED true
#define TCP_ENABLED false
#define PING_ENABLED false
#define RS485_ENABLED false

#if (S7_ENABLED + TCP_ENABLED + PING_ENABLED + RS485_ENABLED) != 1
#error "Enable exactly one mode: S7_ENABLED, TCP_ENABLED, PING_ENABLED or RS485_ENABLED"
#endif

/* S7 read: CPU IP, rack/slot, and one input byte (0 means IB0).
 * S7 uses TCP port 102. */
#define S7_TARGET_IP "192.168.0.2"
#define S7_RACK 0
#define S7_SLOT 1
#define S7_INPUT_BYTE 0

/* Ping: four requests to this one address. */
#define PING_TARGET_IP "192.168.0.2"

/* TCP: true scans the IP range/ports below; false tests one IP/port. */
#define TCP_SCAN_IPS true
#define TCP_TARGET_IP "192.168.0.2"
#define TCP_TARGET_PORT 502
#define TCP_CONNECT_TIMEOUT_MS 3000
/* Optional Modbus reads during IP scanning. false = TCP port checks only. */
#define TCP_READ_MODBUS false

/* Shared read request / scan range (inclusive, zero-based register offsets).
 * To read one device/block, set FIRST_ID=LAST_ID and START=LAST_START.
 * FC: 1 coils, 2 discrete inputs, 3 holding registers, 4 input registers.
 */
#define SCAN_FIRST_ID 1
#define SCAN_LAST_ID 247
#define SCAN_FUNCTION_CODE 4
#define SCAN_START_REGISTER 0
#define SCAN_LAST_START_REGISTER 0
#define SCAN_REGISTER_COUNT 1
#define SCAN_REGISTER_STEP 1

/* TCP target selection. true selects only 502; false uses CUSTOM_PORTS. */
/* Auto mode derives the full subnet from the selected interface's IP/mask.
 * Manual mode uses TCP_IP_PREFIX and TCP_LAST_OCTETS as before. */
#define TCP_AUTO_SUBNET false
#define TCP_AUTO_MAX_HOSTS 1024
#define TCP_IP_PREFIX "192.168.0."
#define TCP_LAST_OCTETS "1-254"
#define TCP_USE_COMMON_PORT false
#define TCP_CUSTOM_PORTS "102,502"
#define TCP_TIMEOUT_MS 500

/* Ethernet: DHCP first, then static address while link remains connected. */
/* Force static skips DHCP and candidate discovery. Otherwise multi-subnet
 * scans DHCP first, then every configured candidate regardless of replies. */
#define ETH_FORCE_STATIC true
#define ETH_SCAN_ALL_SUBNETS false
#define ETH_STATIC_FALLBACK_ENABLED true
#define ETH_DHCP_TIMEOUT_SECONDS 10
/* Nano address for the PLC LAN; must be unused. Auto scan uses this subnet. */
#define ETH_STATIC_IP "192.168.0.27"
#define ETH_STATIC_NETMASK "255.255.255.0"
#define ETH_STATIC_GATEWAY "0.0.0.0"

/* Unknown direct Ethernet: try these /24 scanner addresses after DHCP fails.
 * Addresses must be unused. Disable to use ETH_STATIC_IP only.
 * No passive packet sniffing or automatic address-conflict detection. */
#define ETH_UNKNOWN_NETWORK_ENABLED false
#define ETH_UNKNOWN_SCANNER_IPS {"10.9.9.253"}

/* Wi-Fi fallback uses the Nano's ESP32-C6 over SDIO. */
#define WIFI_FALLBACK_ENABLED false
#define WIFI_FALLBACK_DELAY_SECONDS 15
#define WIFI_SSID "HERIN-2.5"
#define WIFI_PASSWORD "1011@herin"

/* RS-485 serial configuration. ALL=true tries rates below and N/E/O, 1/2 stops. */
#define RS485_SCAN_ALL_SETTINGS true
#define RS485_SCAN_BAUD_RATES {9600,19200,38400,57600,115200,4800,2400,1200}
#define UART_BAUD_RATE 9600
#define RS485_PARITY 'N'
#define RS485_STOP_BITS 1
#define RS485_TIMEOUT_MS 250
#define RS485_RETRIES 1
#define UART_PORT_NUM UART_NUM_2
#define UART_TXD_PIN GPIO_NUM_3
#define UART_RXD_PIN GPIO_NUM_2
#define TXE_PIN GPIO_NUM_6
#define MODBUS_TX_LED GPIO_NUM_36
#define MODBUS_RX_LED GPIO_NUM_33
#define UART_BUF_SIZE 256
#define LED_ACTIVE_LEVEL 1
