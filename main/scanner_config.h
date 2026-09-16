#pragma once
#include <stdbool.h>
/* All settings are compiled into firmware. Rebuild and flash after editing.
 * Automatic scan on boot and selected-network changes; reset to repeat manually.
 */
#define TCP_ENABLED true

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
#define TCP_IP_PREFIX "192.168.0."
#define TCP_LAST_OCTETS "1-254"
#define TCP_USE_COMMON_PORT true
#define TCP_CUSTOM_PORTS "502,1502,2502"
#define TCP_TIMEOUT_MS 500

/* Ethernet: DHCP first, then static address while link remains connected. */
#define ETH_STATIC_FALLBACK_ENABLED true
#define ETH_DHCP_TIMEOUT_SECONDS 10
/* Change to "192.168.1.57" for the other LAN; scan prefix follows automatically. */
#define ETH_STATIC_IP "192.168.0.57"
#define ETH_STATIC_NETMASK "255.255.255.0"
#define ETH_STATIC_GATEWAY "0.0.0.0"

/* Wi-Fi fallback uses the Nano's ESP32-C6 over SDIO. */
#define WIFI_FALLBACK_ENABLED true
#define WIFI_FALLBACK_DELAY_SECONDS 15
#define WIFI_SSID "HERIN-2.5G"
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
