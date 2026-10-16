# Choose a mode

Edit `main/scanner_config.h`. Set exactly one mode to `true`:

```c
#define S7_ENABLED true
#define TCP_ENABLED false
#define PING_ENABLED false
#define RS485_ENABLED false
```

Multiple enabled modes or no enabled mode causes a build error.
Rebuild and flash after changing settings.

## What each mode does

**When `S7_ENABLED` is `true`, the Nano tries to read data from a Siemens PLC using the S7 protocol.** It connects to `S7_TARGET_IP` on TCP port 102, using `S7_RACK` and `S7_SLOT`, and reads one input byte selected by `S7_INPUT_BYTE`. With the current settings, it connects to `192.168.0.2`, uses rack 0 and slot 1, and reads input byte IB0. It prints the value if the read succeeds or an error if it fails. It does not write PLC data or start or stop the CPU. The PLC must allow the read, and IB0 is not necessarily the address of your ET 200SP inputs. This mode uses Settimino; its source information is in `components/settimino/README.md`.

**When `TCP_ENABLED` is `true`, the Nano checks TCP connections to devices on the Ethernet or Wi-Fi network.** This mode has two choices, controlled by `TCP_SCAN_IPS`.

When `TCP_SCAN_IPS` is `true`, the Nano checks a range of IP addresses and the selected ports. With the current settings, it checks `192.168.0.1` through `192.168.0.254` on ports 102 and 502, skipping its own address, `192.168.0.27`. It prints each open IP/port combination and a final count. This tells you which ports accept connections; it does not prove that the device will allow an S7 or Modbus read.

When `TCP_SCAN_IPS` is `false`, the Nano checks only `TCP_TARGET_IP` and `TCP_TARGET_PORT`. For example, `192.168.0.2` and port 502 test whether that CPU accepts a TCP connection on port 502. It prints success or the connection error, then closes the connection. It does not read PLC data in this single-target mode.

**When `PING_ENABLED` is `true`, the Nano sends four ping requests to `PING_TARGET_IP`.** It prints a success or timeout for each request and a final packet count. A reply shows that the device is reachable over IP. Ping does not check any TCP port or read PLC data. Some devices block ping, so a timeout does not prove that the device is disconnected.

**When `RS485_ENABLED` is `true`, the Nano scans for Modbus RTU devices through its RS-485 connection.** It uses the configured serial settings, device IDs, function code and register range. With `RS485_SCAN_ALL_SETTINGS` enabled, it tries the configured baud rates and parity/stop-bit combinations. It prints valid Modbus replies and any values returned. This mode uses the RS-485 wiring rather than an Ethernet IP address or TCP port.

## Choosing the TCP addresses and ports

`TCP_IP_PREFIX` supplies the first three parts of each address, such as `192.168.0.`. `TCP_LAST_OCTETS` supplies the last parts to try. For example, `"1-254"` checks the full range, `"2"` checks only address `192.168.0.2`, and `"2,10,20-25"` checks those selected addresses. These settings apply when `TCP_SCAN_IPS` is true and `TCP_AUTO_SUBNET` is false.

If you set `TCP_AUTO_SUBNET` to true, the Nano calculates the IP range from its active network address and subnet mask instead. In that case, it ignores `TCP_IP_PREFIX` and `TCP_LAST_OCTETS`.

`TCP_CUSTOM_PORTS` lists the ports to check during a scan, such as `"102,502"`. Keep `TCP_USE_COMMON_PORT` false to use that list. Setting `TCP_USE_COMMON_PORT` true overrides the list and checks only port 502. For a single-target test, use `TCP_TARGET_PORT` instead.

`TCP_READ_MODBUS` controls whether an IP scan also attempts Modbus reads. With it false, the Nano only checks which TCP ports are open. With it true, the Nano sends Modbus read requests to open ports using the shared device-ID, function-code and register settings. Select ports that actually serve Modbus, usually 502. This flag does not enable S7 reads, and it has no effect on the single-target TCP test.

## What happens after you select a mode

Set the mode you want to true and the other three mode flags to false. A false mode flag means that mode will not run. For example, to scan TCP ports, set `TCP_ENABLED` true and set `S7_ENABLED`, `PING_ENABLED` and `RS485_ENABLED` false. Keep `TCP_SCAN_IPS` true and `TCP_READ_MODBUS` false for port checks only.

The IP address in `ETH_STATIC_IP` belongs to the Nano, not the PLC. The current Nano address is `192.168.0.27/24`. S7 uses `S7_TARGET_IP`, ping uses `PING_TARGET_IP`, and the single-target TCP test uses `TCP_TARGET_IP`. Changing one target setting does not change the others.

Network modes wait until a network connection is ready. S7, ping and the single-target TCP test run once; reset the Nano to repeat them. A TCP IP scan runs once and starts again when the selected network changes. The RS-485 scan runs once per boot. Results appear in the USB serial monitor at 115200 baud.

After changing `main/scanner_config.h`, rebuild and flash the firmware. Editing the file alone does not change the firmware already running on the Nano.

# ESP32-P4 Nano Modbus scanner

Find responding Modbus endpoints and read raw register values using a Waveshare ESP32-P4 Nano. Choose **Modbus TCP over Ethernet/Wi-Fi** or **Modbus RTU over RS-485**.

The board scans automatically. Results appear in the USB serial monitor with labeled TCP fields and a summary of responding unit IDs. Configuration is currently compiled into firmware: there is **no browser page or interactive terminal menu**. Edit [main/scanner_config.h](main/scanner_config.h), rebuild, and flash to change settings.

Current configuration: **S7 read diagnostic with forced-static Ethernet at
`192.168.0.27/24`**, targeting CPU `192.168.0.2`. DHCP and subnet scans are
bypassed. The Modbus TCP scanner examples below require `TCP_ENABLED=true`,
`TCP_SCAN_IPS=true` and `TCP_READ_MODBUS=true`, with the other modes false.

## Quick start

1. Connect the Nano to your computer through its USB programming connector.
2. Select exactly one mode as explained above. For a network mode, connect Ethernet to the device/network, or configure Wi-Fi fallback. For RS-485, connect the serial wiring.
3. Set the target for your chosen mode. For TCP scanning, choose a manual IP range or set `TCP_AUTO_SUBNET=true` to derive it from the network address and subnet mask.
4. Build and flash using ESP-IDF **5.5.x**, target **esp32p4**.
5. Open the serial monitor at **115200 baud** to see the selected mode and its results. Port scans print `TCP OPEN`; Modbus scans print `READ OK` for successful reads.

Scanning does not require the monitor to be open. TCP scans restart when the selected network changes, including after a completed scan. Multi-subnet mode also advances after each completed Ethernet subnet scan. With a stable network, a completed scan waits rather than repeating continuously. Reset the board to repeat manually. RTU performs one scan per boot.

## Network connection and fallback

| Condition | Automatic behavior |
|---|---|
| Ethernet cable connected | Try DHCP first, unless forced-static mode is enabled. |
| Ethernet link remains up without an IP | After about 10 seconds, begin unknown-network subnet trials, or use the fixed static fallback if discovery is disabled. |
| Ethernet has no IP for about 15 seconds | Start Wi-Fi fallback if enabled. |
| Ethernet gets an IP while Wi-Fi is active | Select Ethernet and restart scanning from the first target. |
| Selected network becomes unavailable | Wait for a usable network, then restart scanning. Wi-Fi can provide fallback. |
| Static Ethernet is unplugged | Clear the static configuration and try DHCP on the next connection. |

Ethernet is preferred whenever it has an IP address. TCP sockets are bound to the selected interface. Wi-Fi may remain connected in the background after Ethernet takes over. An in-flight request can finish or time out before the worker detects a network change and restarts.

These timers are separate: the Ethernet DHCP timeout starts on link-up; the Wi-Fi delay tracks how long Ethernet has lacked an IP. Static fallback normally completes first when the cable is connected at startup.

### Automatic subnet scanning

```c
#define TCP_AUTO_SUBNET true
#define TCP_AUTO_MAX_HOSTS 1024
```

The scanner reads the active interface's IPv4 address and subnet mask. For `192.168.10.57/24`, it scans `192.168.10.1–254`, excluding itself. The same logic supports other contiguous masks, including /23 and /30. Network and broadcast addresses are excluded on conventional subnets; /31 and /32 are handled without subtracting them. A /32 has no other host to scan.

Automatic mode ignores `TCP_IP_PREFIX` and `TCP_LAST_OCTETS`. It keeps the configured ports, unit IDs, function, and register range. It also applies to Wi-Fi and fixed static Ethernet. If the range exceeds `TCP_AUTO_MAX_HOSTS` (default 1,024 host addresses), the scanner explains why it did not scan; it does not silently truncate the network. Choose a manual range or increase the limit, up to 65,534, and rebuild. Scans remain sequential, so large ranges take time.

A change to the selected interface's address or mask restarts the scan, even if the interface itself has not changed.

### Multiple Ethernet subnets on the same switch

```c
#define ETH_FORCE_STATIC false
#define ETH_SCAN_ALL_SUBNETS true
#define ETH_UNKNOWN_NETWORK_ENABLED true
#define ETH_UNKNOWN_SCANNER_IPS {"192.168.1.253", "10.24.26.253", "192.168.0.253", "172.16.0.253"}
```

With these settings, the Nano scans the DHCP-assigned subnet first, then stops DHCP and tries **every candidate subnet**, even if earlier scans found devices. Each candidate is a temporary scanner address with mask `255.255.255.0` and no gateway. For example, a DHCP address of `192.168.1.40` does not prevent a later trial using `10.24.26.253` to reach a PLC at `10.24.26.5`.

The ports must share the same VLAN/broadcast domain for this direct communication. Candidate scanner addresses must be unused. Changing the Nano's IP does not change the PLC or switch configuration. The scanner uses one Ethernet address at a time, not simultaneous connections to all subnets. A candidate matching the DHCP subnet is scanned again; results can therefore repeat across trials.

If DHCP fails, discovery starts at the first candidate after `ETH_DHCP_TIMEOUT_SECONDS`. Each candidate scan has its own summary. After the last candidate, the scanner reports completion (or **DISCOVERY INCONCLUSIVE** if no Modbus replies were found anywhere), keeps the final candidate address, and waits. It does not automatically restore DHCP or retain access to previously scanned subnets. Unplugging/resetting restarts the process.

Set `ETH_SCAN_ALL_SUBNETS=false` to restore the earlier behavior: candidate trials only follow DHCP failure and stop at the first subnet producing a valid data or exception response. Set `ETH_UNKNOWN_NETWORK_ENABLED=false` to disable candidate trials entirely.

This is a finite configurable shortlist, not exhaustive discovery of all private IP addresses. It does not sniff packets or detect IP conflicts. Each /24 may take roughly two minutes for connection attempts, plus time for unit-ID reads. A silent device, an unlisted subnet, a different port, or unsuitable read settings may still require manual configuration or a manufacturer discovery tool.

### Fixed static fallback and manual targets

For a known PLC subnet, enable forced-static mode to bypass DHCP and candidate trials:

```c
#define ETH_FORCE_STATIC true
#define ETH_UNKNOWN_NETWORK_ENABLED false
#define ETH_STATIC_FALLBACK_ENABLED true
#define ETH_DHCP_TIMEOUT_SECONDS 10
#define ETH_STATIC_IP "192.168.1.57"
#define ETH_STATIC_NETMASK "255.255.255.0"
#define ETH_STATIC_GATEWAY "0.0.0.0"
```

`ETH_FORCE_STATIC=true` uses the fixed address on every connection, even if a DHCP server is available. To use it only as a fallback, set `ETH_FORCE_STATIC=false` and leave candidate discovery disabled. Set `ETH_STATIC_IP` to an unused address compatible with your device's subnet, such as `192.168.0.57` for a `192.168.0.x/24` LAN. A gateway of `0.0.0.0` is sufficient for directly reachable devices on the same subnet. For PLC `10.24.26.5/24`, an example is `ETH_STATIC_IP "10.24.26.253"` if unused, with mask `255.255.255.0`. The scanner assigns only its own address; it does not run a DHCP server or configure other devices.

With `TCP_AUTO_SUBNET=true`, the static address and mask determine the full scan range. To narrow targets, set `TCP_AUTO_SUBNET=false`: DHCP Ethernet and Wi-Fi then use `TCP_IP_PREFIX` plus `TCP_LAST_OCTETS`, while fixed static Ethernet derives its prefix from the first three octets of `ETH_STATIC_IP` and uses `TCP_LAST_OCTETS`. Unknown-network mode always scans each candidate's full /24, even if manual target mode is selected.

All these settings are compiled into firmware. Rebuild and flash after editing.

### Wi-Fi settings

```c
#define WIFI_FALLBACK_ENABLED true
#define WIFI_FALLBACK_DELAY_SECONDS 15
#define WIFI_SSID "your-2.4GHz-network"
#define WIFI_PASSWORD "your-password"
```

Replace these example credentials in `main/scanner_config.h`. Credentials are compiled into firmware; the application does not print the password. Wi-Fi reconnects after disconnection. Wi-Fi initialization failure requires a reset to retry; Ethernet remains available.

The Nano's ESP32-C6 supplies Wi-Fi through ESP-Hosted over SDIO. It needs compatible coprocessor firmware and a 2.4 GHz access point. Hosted initialization is deferred until fallback is needed.

## TCP discovery settings

Current scan defaults:

```c
#define TCP_ENABLED true
#define TCP_AUTO_SUBNET true
#define TCP_AUTO_MAX_HOSTS 1024
#define TCP_IP_PREFIX "192.168.1."
#define TCP_LAST_OCTETS "1-254"
#define TCP_USE_COMMON_PORT true
#define TCP_CUSTOM_PORTS "502,1502,2502"
#define TCP_TIMEOUT_MS 500
```

For manual target mode (`TCP_AUTO_SUBNET=false`), the prefix must include its trailing dot. Last octets accept individual hosts, lists, and inclusive ranges, such as `"201"` or `"1,5,10-20"`. Host values are 1–254.

`TCP_USE_COMMON_PORT=true` checks only port 502. Set it to `false` to use `TCP_CUSTOM_PORTS`, which supports up to 32 distinct ports. An open TCP port is followed by Modbus requests; it is not itself proof of a Modbus device.

For every open IP/port, the scanner tries all configured unit IDs, continuing after a successful reply. Multiple unit IDs can respond behind the same IP. Connections are reused where possible; failed exchanges close the connection before another attempt.

Scanning is currently sequential. Parallel IP probing and separate connection/response timeout settings have **not** been implemented. `TCP_TIMEOUT_MS` applies to connection waiting and each Modbus exchange. Narrowing the target range is the simplest way to shorten a scan without reducing timeouts for slow devices.

## Unit IDs and register reads

These settings apply to both transports:

```c
#define SCAN_FIRST_ID 1
#define SCAN_LAST_ID 247
#define SCAN_FUNCTION_CODE 4
#define SCAN_START_REGISTER 0
#define SCAN_LAST_START_REGISTER 0
#define SCAN_REGISTER_COUNT 1
#define SCAN_REGISTER_STEP 1
```

The defaults try **unit IDs 1–247**, reading **one input register at offset 0 (FC04)**. ID and starting-offset bounds are inclusive. `SCAN_REGISTER_STEP` controls the distance between starting offsets; `SCAN_REGISTER_COUNT` is the number of items in each request.

| Function | Data read | Maximum items per request |
|---|---|---|
| FC01 | Coils | 2,000 bits |
| FC02 | Discrete inputs | 2,000 bits |
| FC03 | Holding registers | 125 registers |
| FC04 | Input registers | 125 registers |

Register offsets are zero-based wire addresses. A manual's holding register 40001 often corresponds to FC03 offset 0; verify the device's register map. Blocks cannot extend past offset 65535. Configurable unit-ID limits are 0–255 for TCP and 1–247 for RTU.

For example, to focus on IP `192.168.1.201`, units 2–3, and one register:

```c
#define TCP_AUTO_SUBNET false
#define ETH_UNKNOWN_NETWORK_ENABLED false
#define TCP_IP_PREFIX "192.168.1."
#define TCP_LAST_OCTETS "201"
#define SCAN_FIRST_ID 2
#define SCAN_LAST_ID 3
#define SCAN_START_REGISTER 0
#define SCAN_LAST_START_REGISTER 0
#define SCAN_REGISTER_COUNT 1
```

When static Ethernet fallback is used, set `ETH_STATIC_IP` to an unused address on that same subnet too.

## Reading TCP results

Illustrative output:

```text
Active network: Ethernet. TCP connections will use Ethernet.
Port open: 192.168.1.201:502 | Checking unit IDs 1–247...
READ OK | IP 192.168.1.201 | Port 502 | Unit ID 2 | FC03 | Register offset 0 | Count 1 | Values: 14
READ OK | IP 192.168.1.201 | Port 502 | Unit ID 3 | FC03 | Register offset 0 | Count 1 | Values: 29
FOUND | 192.168.1.201:502 | Responding unit IDs: 2–3
```

| Message | Meaning |
|---|---|
| `Port open` | TCP connected; Modbus validation is still required. |
| `READ OK` | A valid Modbus data response was received for the labeled request. |
| `MODBUS EXCEPTION` | The endpoint replied with a Modbus exception. Communication works, but the requested data was not returned. |
| `FOUND` | Summary of responding IDs for this IP/port, including exception responses. |
| `Scan progress` | Current endpoint position and number of Modbus endpoints found; printed at the first endpoint and every ten endpoints. |
| `SCAN COMPLETE` | Totals for responding IP/port endpoints, IP/port/unit combinations, and elapsed time. |

Unavailable TCP connections are omitted from normal output. The final request/response totals include only Modbus exchanges, not failed TCP connection attempts. Results stream to UART0; there is no persistent result storage or export UI. The labeled TCP format replaces the earlier `TCP_DATA,...` lines. RTU retains its CSV-style output.

### Responding IDs are not a physical-device count

If one IP responds with value `500` for IDs 1–247, the scanner has received 247 valid replies. That does **not** establish that 247 physical slaves exist. The endpoint may ignore unit IDs or map them to the same data. Separate devices can also legitimately return identical values, so the scanner keeps checking every configured ID.

A missing reply does not prove that a unit is absent: the register, function, count, timeout, port, or network settings may be unsuitable. This scanner reports raw values, not device model names, engineering units, scaling, signed values, or float/word order. Use the device's documentation to interpret the data.

## RS-485 mode and wiring

Set `TCP_ENABLED false` to use UART2 Modbus RTU instead of Ethernet/Wi-Fi.

```c
#define RS485_SCAN_ALL_SETTINGS true
#define RS485_SCAN_BAUD_RATES {9600,19200,38400,57600,115200,4800,2400,1200}
#define UART_BAUD_RATE 9600
#define RS485_PARITY 'N'
#define RS485_STOP_BITS 1
#define RS485_TIMEOUT_MS 250
#define RS485_RETRIES 1
```

With `RS485_SCAN_ALL_SETTINGS=true`, try each listed baud rate with no/even/odd parity and 1/2 stop bits. With `false`, use only the configured baud rate, parity, and stop bits. Data length is eight bits. One retry means up to two attempts; discovery candidates also receive an independent confirmation request.

| Signal | Default GPIO |
|---|---|
| UART2 TX → transceiver DI | 3 |
| UART2 RX ← transceiver RO | 2 |
| Direction enable → tied DE and /RE | 6 |
| TX activity LED | 36 |
| RX activity LED | 33 |

Use a 3.3 V logic-compatible RS-485 transceiver. Do not connect A/B directly to GPIO. Wire A/B and reference ground as required, with appropriate termination and bias. For an automatic-direction transceiver, set `TXE_PIN` to `-1`; it must suppress local echo. LEDs default to active-high.

Scan with the existing Modbus master disconnected. Only read functions are sent, but scanning occupies the bus. Multiple serial formats may work on a tolerant receiver; duplicate RTU slave addresses cannot be separated reliably.

RTU example:

```text
DATA,1,19200,8E1,3,100,2,2301,500
```

Columns are status, unit ID, baud, serial format, function, starting offset, count, and values or exception code.

## Build, flash, and monitor

Install ESP-IDF **5.5.x** and its ESP32-P4 toolchain. The component manifest currently requires `>=5.5.0,<5.6.0` and pins `esp_wifi_remote` to `1.6.4` and `esp_hosted` to `3.0.7`. The initial build downloads managed dependencies and requires network access.

Run these commands from the project directory in an ESP-IDF-enabled terminal. On macOS/Linux, activate your installation first:

```sh
source /path/to/esp-idf/export.sh
```

For a fresh checkout:

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

For subsequent configuration changes:

```sh
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

Replace `YOUR_SERIAL_PORT` with your board's actual port, such as `/dev/cu.usbmodem...` on macOS, `/dev/ttyUSB0` on Linux, or `COM5` on Windows. On Windows, use the ESP-IDF terminal instead of the shell activation command above.

Close an existing monitor with **Ctrl+]** before flashing. To view output without flashing, run `idf.py -p YOUR_SERIAL_PORT monitor`. The Nano's Type-C CH343 programming connection uses UART0 at 115200 baud. Reflash bootloader and application when changing console settings.

## Troubleshooting

| Symptom | What to check |
|---|---|
| `Waiting for Ethernet link` | Cable, connected device/switch power, and link LEDs. Static IP cannot fix a missing physical link. |
| Link up, no DHCP address | Wait for subnet discovery or fixed static fallback, or check the DHCP server. |
| Wi-Fi initialization fails | Compatible ESP-Hosted firmware on the C6, board wiring/configuration, and reset to retry. |
| Wi-Fi repeatedly disconnects | SSID, password, 2.4 GHz availability, and signal strength; the log includes a reason code. |
| No Modbus replies | Target subnet, device power, port, unit IDs, function, register offset, and count. Try a known device/block first. |
| Every unit returns the same value | The server may map all IDs to the same data. Check the device or simulator configuration. |
| `main_task: Returned from app_main()` | Normal: scanning runs in a separate FreeRTOS task. |
| Cannot open the serial port | Close the other monitor/application using it and check the actual port name. |
| Scan takes a long time | Reduce IPs, ports, IDs, register offsets, or RTU serial combinations. Full combinations can take hours. |

Ethernet hardware defaults are IP101 PHY address 1, MDC GPIO31, MDIO GPIO52, and reset GPIO51. C6 SDIO defaults are CLK18, CMD19, D0–D3 GPIO14–17, and reset GPIO54. These are Nano-specific; other boards may require changes.

Observed board logs have demonstrated Wi-Fi DHCP operation and Ethernet static fallback at `192.168.1.57` with valid Modbus replies. These observations do not validate every handover, device, or RS-485 configuration. Automatic subnet calculation has host tests. Unknown-network transitions and discovery on actual devices still need hardware verification after flashing.

## Developer checks

Host tests cover Modbus framing, response validation, and TCP configuration parsing. They do not exercise ESP-IDF networking or physical hardware.

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I main main/modbus.c tests/test_modbus.c -o /tmp/modscan-test
/tmp/modscan-test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I main main/modbus.c main/modbus_tcp.c main/tcp_config.c tests/test_tcp.c -o /tmp/modscan-tcp-test
/tmp/modscan-tcp-test
```

## References

- [Waveshare Nano documentation](https://docs.waveshare.com/ESP32-P4-NANO)
- [Waveshare Nano FAQ](https://docs.waveshare.com/ESP32-P4-NANO/FAQ)
- [Espressif UART driver](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/peripherals/uart.html)
- [Modbus application protocol](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [Modbus TCP implementation guide](https://modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf)
