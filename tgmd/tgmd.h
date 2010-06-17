/*
 *  tgmd.h
 *  Untitled
 *
 *  Created by Steven Lin on 2009/10/25.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include <time.h>

#define LOG_TAG "TGMD"


typedef enum SerialStates
{
	SERIAL_STATE_OPENING,
	SERIAL_STATE_INITIALIZING,
    SERIAL_STATE_OPERATING,
    SERIAL_STATE_PPPD,
    SERIAL_STATE_PPPD_OK,
	SERIAL_STATE_CLOSING,
	SERIAL_STATE_OFF

} SerialStates;

typedef enum ModemStates
{
    MODEM_STATE_OFF,
    MODEM_STATE_NEED_PIN,
    MODEM_STATE_OPERATING

}ModemStates;

/* Struct is used for representing a serial device. */

typedef struct Serial
{
	char *devicename;
	int fd;
	unsigned char *adv_frame_buf;
	time_t frame_receive_time;
	int ping_number;
    SerialStates state;
    ModemStates modem_states;

} Serial;

/* Struct is used for passing fd, read function and read funtion arg to a device polling thread */

typedef struct Poll_Thread_Arg
{
    int fd;
    int (*read_function_ptr)(void *);
    void * read_function_arg;

}Poll_Thread_Arg;

//just dummy defines since were not including syslog.h.
#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7


#define RESPONSE_OK "OK"
#define RESPONSE_ERROR "ERROR"
#define RESPONSE_CONNECT "CONNECT"
#define RESPONSE_NO_CARRIER "NO CARRIER"
#define RESPONSE_CPIN_READY "+CPIN: READY"
#define RESPONSE_CPIN_SIM_PIN "+CPIN: SIM PIN"
#define RESPONSE_CPIN_SIM_PUK "+CPIN: SIM PUK"
#define RESPONSE_CME_ERROR "+CME ERROR"
#define RESPONSE_COMMAND_NOT_SUPPORT "COMMAND NOT SUPPORT"

#define DEVICE_ERROR -1
#define RESPONSE_NOT_DEFINED 0
#define RESPONSE_OK_CODE 1
#define RESPONSE_ERROR_CODE 2
#define RESPONSE_CPIN_READY_CODE 3
#define RESPONSE_CPIN_SIM_PIN_CODE 4
#define RESPONSE_CPIN_SIM_PUK_CODE 5
#define RESPONSE_CME_ERROR_CODE 6
#define RESPONSE_CONNECT_CODE 7
#define RESPONSE_NO_CARRIER_CODE 8
#define RESPONSE_COMMAND_NOT_SUPPORT_CODE 9

int open_serial_device(Serial* serial);

static int close_devices();

static int chat(int serial_device_fd, char *cmd, int to);

static int tgm_chat(int serial_device_fd, char *cmd, int to);

static int tgm_respose_chat(int device_fd, char *cmd, char *response, int length, int timeout);

void* poll_thread(void *vargp);

void set_main_exit_signal(int signal);

static int watchdog(Serial * serial);

static int syslogdump( const char *prefix, const unsigned char *ptr, unsigned int length);

static int memstr( const char *haystack, int length, const char *needle);

int openSocket();

void onInterruptSignal(int signal);
