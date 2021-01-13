#include <mem.h>

#include "osapi.h"
#include "user_interface.h"
#include "gpio.h"
#include "espconn.h"

#include "mod_enums.h"

// Establishes WiFi session ID where ESP will try to connect to
#define WIFI_SSID								"ESP8266_AP_LED"
// Establishes ESP WiFi passphrase for client's connection
#define WIFI_PASSPHRASE							"ap_test5"
// Remote TCP Server socket port number to connect to
#define SERVER_SOCKET_PORT						1010

// Baud rate which will be used for debug logs UART output
#define UART_BAUD_RATE							115200

// Used to distinguish between client connection states. Used for internal LED indication:
// LED off - disconnected - WiFi session is not established
// LED blinking - WiFi session is established, but not connected to TCP socket yet
// LED on - TCP socket connection is established
#define STATE_DISCONNECTED						0
#define STATE_CLIENT_WIFI_CONNECTED				1
#define STATE_CLIENT_SOCKET_CONNECTED			2

// Sets timer period interval in ticks for different events (1 tick - 100ms)
#define TIMER_PERIOD_STATE_UPDATE				50
#define TIMER_PERIOD_WIFI_STATUS_LED			5
#define TIMER_PERIOD_RESET						1000000
#define TIMER_PERIOD_HEARTBEAT_SEND				300

// System partitions sizes definition
#define SYSTEM_PARTITION_RF_CAL_SZ				0x1000
#define SYSTEM_PARTITION_PHY_DATA_SZ			0x1000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_SZ	0x3000

// SPI memory size definition
#define SYSTEM_SPI_SIZE							0x400000

// System partitions sizes definition
#define SYSTEM_PARTITION_RF_CAL_ADDR			SYSTEM_SPI_SIZE - SYSTEM_PARTITION_SYSTEM_PARAMETER_SZ - SYSTEM_PARTITION_PHY_DATA_SZ - SYSTEM_PARTITION_RF_CAL_SZ
#define SYSTEM_PARTITION_PHY_DATA_ADDR			SYSTEM_SPI_SIZE - SYSTEM_PARTITION_SYSTEM_PARAMETER_SZ - SYSTEM_PARTITION_PHY_DATA_SZ
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR	SYSTEM_SPI_SIZE - SYSTEM_PARTITION_SYSTEM_PARAMETER_SZ

#define LABEL_BUFFER_SIZE						300
#define TX_BUFFER_SIZE							20

// Internal LED GPIO pin
static const uint8 GPIO_PIN_LED_INT = 2;
// External input GPIO pins used with buttons
static const uint8 GPIO_PIN_BUTTON_1 = 12;
static const uint8 GPIO_PIN_BUTTON_2 = 13;
static const uint8 GPIO_PIN_BUTTON_3 = 14;
// Timer used to refresh LEDs state bits
static os_timer_t start_timer;
// Timer invocation index number
static uint32 tick_index = 0;
// Indicates current connection state
static uint8 connection_state = STATE_DISCONNECTED;
// Indicates previous connection state (used for UART logging only)
static uint8 prev_connection_state = STATE_DISCONNECTED;
// Indicates previous ESP WiFi session state (used for UART logging only)
static uint8 prev_wifi_state = STATION_IDLE;
// Holds pointer to ESP connection resource
struct espconn* pespconn = NULL;
// Holds external buttons state (3 less-significant bits are used)
static uint8 buttons_state = 0;
// Indicates whether client is connected to TCP server socket
static bool is_tcp_socket_connected = false;
// Indicates whether client connection to TCP server socket is establishing at present moment
static bool is_tcp_socket_connecting = false;

static const partition_item_t part_table[] =
{
	{ SYSTEM_PARTITION_RF_CAL,				SYSTEM_PARTITION_RF_CAL_ADDR,		SYSTEM_PARTITION_RF_CAL_SZ					},
	{ SYSTEM_PARTITION_PHY_DATA,			SYSTEM_PARTITION_PHY_DATA_ADDR,		SYSTEM_PARTITION_PHY_DATA_SZ				},
	{ SYSTEM_PARTITION_SYSTEM_PARAMETER,	SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR, SYSTEM_PARTITION_SYSTEM_PARAMETER_SZ	}
};

// Indicates whether client station is connected to ESP Server WiFi session
static bool is_wifi_session_established(void)
{
	return wifi_station_get_connect_status() == STATION_GOT_IP;
}

// Updates global connectivity state (WiFi session state + TCP socket connection state)
static void update_connection_state()
{
	if (is_wifi_session_established())
	{
		if (is_tcp_socket_connected)
		{
			connection_state = STATE_CLIENT_SOCKET_CONNECTED;
		}
		else
		{
			connection_state = STATE_CLIENT_WIFI_CONNECTED;
		}
	}
	else
	{
		connection_state = STATE_DISCONNECTED;
	}
}

// Returns uint8 value according to current state of buttons. 3 less-significant bits are used, 1 - means button is pushed, 0 - means button is released.
static uint8 get_buttons_state()
{
	return (gpio_input_get() >> GPIO_PIN_BUTTON_1) & 0x07;
}

// Transmits buttons state to server node
static void tx_buttons_state()
{
	char tx_buf[TX_BUFFER_SIZE];
	os_sprintf(tx_buf, "%d", buttons_state);
	if (pespconn != NULL)
	{
		OS_UART_LOG("[INFO] Transmitting buttons state: %s\n", tx_buf);
		espconn_send(pespconn, tx_buf, os_strlen(tx_buf));
	}
	else
	{
		OS_UART_LOG("[ERROR] Unable to transmit buttons state. ESPCONN is not ready.\n");
	}
}

// Optional UART logging in respect of WiFi session current state
static void log_wifi_session_state(void)
{
#ifdef UART_DEBUG_LOGS
	char label_state[LABEL_BUFFER_SIZE];
	uint8 state = wifi_station_get_connect_status();
	if (prev_wifi_state != state)
	{
		prev_wifi_state = state;
		lookup_station_status(label_state, state);
		OS_UART_LOG("\n[INFO] Current WiFi session state: %s\n", label_state);
	}
#endif
}

// System pre-init method. Used for partitions initialization.
void ICACHE_FLASH_ATTR user_pre_init(void)
{
	system_partition_table_regist(part_table, 3, SPI_FLASH_SIZE_MAP);
}

// Releases ESP connection resources
void release_espconn_memory(struct espconn* pconn)
{
	if (pconn)
	{
		if (pconn->proto.tcp)
		{
			os_free(pconn->proto.tcp);
			pconn->proto.tcp = NULL;
		}
		OS_UART_LOG("[INFO] TCP connection resources released\n");
		os_free(pconn);
		pespconn = NULL;
	}
}

// ON-SUCCESSFUL TCP DISCONNECT callback method (triggered upon socket connection is closed)
static void ICACHE_FLASH_ATTR on_tcp_client_close_callback(void* arg)
{
	OS_UART_LOG("[INFO] TCP connection closed\n");
	struct espconn* pconn = (struct espconn*)arg;
	release_espconn_memory(pconn);
	is_tcp_socket_connected = false;
	is_tcp_socket_connecting = false;
}

// ON-FAILED TCP CONNECT callback method (triggered in case of TCP connection cannot be established, used for re-try logic)
static void ICACHE_FLASH_ATTR on_tcp_client_failed_callback(void* arg, sint8 error_type)
{
#ifdef UART_DEBUG_LOGS
	char error_info[LABEL_BUFFER_SIZE];
	lookup_espconn_error(error_info, error_type);
	OS_UART_LOG("[ERROR] TCP connection error: %s\n", error_info);
#endif
	struct espconn* pconn = (struct espconn*)arg;
	release_espconn_memory(pconn);
	is_tcp_socket_connected = false;
	is_tcp_socket_connecting = false;
}

// ON-SUCCESSFUL TCP CONNECT callback method (triggered upon TCP connection is established)
static void ICACHE_FLASH_ATTR on_tcp_client_connected_callback(void* arg)
{
	struct espconn* pconn = (struct espconn*)arg;
	OS_UART_LOG("[INFO] TCP socket connection is established. Remote target address: %d.%d.%d.%d:%d.\n",
			*((uint8*)&pconn->proto.tcp->remote_ip),
			*((uint8*)&pconn->proto.tcp->remote_ip+1),
			*((uint8*)&pconn->proto.tcp->remote_ip+2),
			*((uint8*)&pconn->proto.tcp->remote_ip+3),
			pconn->proto.tcp->remote_port);
	espconn_regist_disconcb(pconn, on_tcp_client_close_callback);
	is_tcp_socket_connected = true;
	is_tcp_socket_connecting = false;
}

// Establishes client TCP connection to target TCP Server (10.0.0.1:1010)
void tcp_client_connect(void)
{
	is_tcp_socket_connecting = true;
	// Configuring ESP TCP client settings
	// Memory allocation for pespconn
	pespconn = (struct espconn*)os_zalloc(sizeof(struct espconn));
	// ESP connection setup for TCP
	pespconn->type = ESPCONN_TCP;
	pespconn->state = ESPCONN_NONE;
	pespconn->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
	// Target TCP Server IP address
	ip_addr_t target_ip;
	IP4_ADDR(&target_ip, 10, 0, 0, 1);
	os_memcpy(pespconn->proto.tcp->remote_ip, &target_ip.addr, 4);
	// Target TCP Server port
	pespconn->proto.tcp->remote_port = SERVER_SOCKET_PORT;
	// Sets connection callback methods
	espconn_regist_connectcb(pespconn, on_tcp_client_connected_callback);
	espconn_regist_reconcb(pespconn, on_tcp_client_failed_callback);
	// Triggers actual TCP connection
	sint8 res = espconn_connect(pespconn);
	OS_UART_LOG("[INFO] Establishing TCP socket connection ...\n");
	if (res != ESPCONN_OK)
	{
		release_espconn_memory(pespconn);
		is_tcp_socket_connecting = false;
#ifdef UART_DEBUG_LOGS
		char res_status[LABEL_BUFFER_SIZE];
		lookup_espconn_error(res_status, res);
		OS_UART_LOG("[ERROR] Unable to establish TCP connection: %s\n", res_status);
#endif
	}
}

// This method sets client station WiFi session parameters
void wifi_client_setup(void)
{
	char ssid[] = WIFI_SSID;
	char password[] = WIFI_PASSPHRASE;
	struct station_config sta_conf = { 0 };

	os_memcpy(sta_conf.ssid, ssid, sizeof(ssid));
	os_memcpy(sta_conf.password, password, sizeof(password));
	wifi_station_set_config(&sta_conf);
	wifi_station_set_auto_connect(1);
}

// Connects to pre-defined WiFi session, which is hosted by Server Application node
void wifi_client_connect(void)
{
	if (!is_wifi_session_established())
	{
		OS_UART_LOG("[INFO] Connecting to predefined WiFi SSID ...\n");
		wifi_client_setup();
		if (wifi_station_connect())
		{
			OS_UART_LOG("[INFO] \"WiFi session connect\" request has been submitted\n");
		}
		else
		{
			OS_UART_LOG("[ERROR] Unable to submit \"WiFi session connect\" request\n");
		}
	}
	else
	{
		OS_UART_LOG("[INFO] Already connected to WiFi session\n");
	}
}

// Timer callback method. Triggered 10 times per second.
void on_timer(void* arg)
{
	if (tick_index % TIMER_PERIOD_STATE_UPDATE == 0)
	{
		log_wifi_session_state();
		update_connection_state();
		if (connection_state != prev_connection_state)
		{
			prev_connection_state = connection_state;
			switch(connection_state)
			{
				case STATE_DISCONNECTED:
					OS_UART_LOG("[INFO] WiFi session is disconnected\n");
					break;
				case STATE_CLIENT_WIFI_CONNECTED:
					OS_UART_LOG("[INFO] WiFi session is connected. No TCP socket connection is established.\n");
					break;
				case STATE_CLIENT_SOCKET_CONNECTED:
					OS_UART_LOG("[INFO] WiFi session is connected. TCP socket connection is established.\n");
					break;
				default:
					OS_UART_LOG("[ERROR] Unrecognized state Id: %d\n", connection_state);
					break;
			}
		}

		if (connection_state == STATE_CLIENT_WIFI_CONNECTED && !is_tcp_socket_connecting)
		{
			tcp_client_connect();
		}
	}

	// WiFi status LED indication update
	if (tick_index % TIMER_PERIOD_WIFI_STATUS_LED == 0)
	{
		if (connection_state == STATE_CLIENT_SOCKET_CONNECTED)
		{
			gpio_output_set(0, (1 << GPIO_PIN_LED_INT), 0, 0);
		}
		else if (connection_state == STATE_CLIENT_WIFI_CONNECTED)
		{
			if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & (1 << GPIO_PIN_LED_INT))
			{
				gpio_output_set(0, (1 << GPIO_PIN_LED_INT), 0, 0);
			}
			else
			{
				gpio_output_set((1 << GPIO_PIN_LED_INT), 0, 0, 0);
			}
		}
		else
		{
			gpio_output_set((1 << GPIO_PIN_LED_INT), 0, 0, 0);
		}
	}

	// If connected - transmits buttons state (transmits changes only) - check is triggered each 100 ms
	if (connection_state == STATE_CLIENT_SOCKET_CONNECTED)
	{
		uint8 current_buttons_state = get_buttons_state();
		if (current_buttons_state != buttons_state)
		{
			buttons_state = current_buttons_state;
			tx_buttons_state();
		}
	}


	// Each 30 seconds - send heartbeat message (heartbeat presented as conventional 'buttons state update' message)
	if (tick_index % TIMER_PERIOD_HEARTBEAT_SEND == 0)
	{
		if (connection_state == STATE_CLIENT_SOCKET_CONNECTED)
		{
			buttons_state = get_buttons_state();
			tx_buttons_state();
		}
	}

	tick_index++;
	if (tick_index >= TIMER_PERIOD_RESET)
	{
		tick_index = 0;
	}
}

// Callback method is triggered upon ESP initialization completion
void on_user_init_completed(void)
{
	wifi_client_connect();
	OS_UART_LOG("[INFO] ESP Client Station initialization is completed\n");
	os_timer_setfn(&start_timer, (os_timer_func_t*)on_timer, NULL);
	os_timer_arm(&start_timer, 100, 1);
}

// Main user initialization method
void ICACHE_FLASH_ATTR user_init(void)
{
	// UART output initialization for logging output
	uart_init(UART_BAUD_RATE, UART_BAUD_RATE);
	gpio_init();
	// Input buttons pins initialization
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_MTDI_U);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_MTCK_U);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_MTMS_U);
	GPIO_DIS_OUTPUT(GPIO_PIN_BUTTON_1);
	GPIO_DIS_OUTPUT(GPIO_PIN_BUTTON_2);
	GPIO_DIS_OUTPUT(GPIO_PIN_BUTTON_3);
	// Internal LED pin initialization
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	gpio_output_set(0, 0, (1 << GPIO_PIN_LED_INT), 0);
	// Sets ESP to client station mode
	wifi_set_opmode(STATION_MODE);
	// on_user_init_completed callback triggered upon initialization is completed
	system_init_done_cb(on_user_init_completed);
}
