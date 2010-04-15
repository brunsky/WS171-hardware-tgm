/*
 *  tgmd.c
 *  Untitled
 *
 *  Created by Steven Lin on 2009/10/25.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <datanetwork/tgm.h>
#include <tgmd.h>

#include <utils/Log.h>
#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <linux/capability.h>
#include <linux/prctl.h>

#include <time.h>

#include <private/android_filesystem_config.h>

#include <termios.h>

#include <paths.h>
#include <pthread.h>
#include <pathconf.h>


#include <signal.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>


#define LIB_PATH_PROPERTY   "tgmd.devicepath"
#define LIB_ARGS_PROPERTY   "tgmd.libargs"
#define MAX_LIB_ARGS        16
#define DEFAULT_SERIAL_DEVICE "/dev/ttyUSB0"
#define PPP_OPERSTATE_PATH "/sys/class/net/ppp0/operstate"

#define MAX_PPP_FAIL_LIMIT 5



#define SYSCHECK(c) do{if((c)<0){LOGD("system-error: '%s' (code: %d)", strerror(errno), errno);\
	return -1;}\
}while(0)

#define LOGTGM(lvl,f,...) do{if(lvl<=syslog_level){\
								if (logtofile){\
								  fprintf(tgmlogfile,"%d:%s(): " f "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__);\
								  fflush(tgmlogfile);}\
								else\
								  fprintf(stderr,"%d:%s(): " f "\n", __LINE__, __FUNCTION__, ##__VA_ARGS__);\
								}\
							}while(0)



static Serial serial;
static FILE* tgmlogfile;

static int main_exit_signal = 0;  /* 1:main() received exit signal */

static int pppd_process_id=0;
static int ppp_fail_times = 0;

static int logtofile = 0;
static int pin_code = -1;

static int syslog_level = LOG_DEBUG;
static int watchdog_time_interval = 5;


extern void TGM_register (const TGM_RadioFunctions *callbacks);

extern void TGM_onRequestComplete(TGM_Token t, TGM_Errno e,
                           void *response, size_t responselen);

extern void TGM_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen);

extern void TGM_requestTimedCallback (TGM_TimedCallback callback,
                               void *param, const struct timeval *relativeTime);


extern void TGM_startEventLoop();

static struct TGM_Env s_tgmEnv = {
    TGM_onRequestComplete,
    TGM_onUnsolicitedResponse,
    TGM_requestTimedCallback
};

pthread_mutex_t main_exit_signal_lock;

pthread_mutex_t syslogdump_lock;


int open_serial_device(Serial* serial)
{

	LOGD("Enter");
	//LOGTGM(LOG_DEBUG, "Enter");

	//unsigned int i;


	/* open the serial port */

	SYSCHECK(serial->fd = open(serial->devicename, O_RDWR | O_NOCTTY | O_NONBLOCK));

	if(serial->fd>=0){

		LOGD("Serial port opened successfully");

		serial->state=SERIAL_STATE_INITIALIZING;

	}else{

		serial->state=SERIAL_STATE_OPENING;

	}

	return 0;
}

/* 
 * Purpose:  Test if this device is an AT COMMAND compatible device if yes test whether it has PIN.
 * Input:    serial device representation
 * Return:   0
 */

int initialize_serial_device(Serial* serial){



	//LOGTGM(LOG_DEBUG, "Testing and configuring modem");

	char gsm_command[100];

	//check if communication with modem is online

	int tryout = 0;

	int response_code = 0;



	while((response_code=tgm_chat(serial->fd, "AT\r\n", 1)) < 0){

		tryout++;

		LOGTGM(LOG_WARNING, "Modem does not respond to AT commands, try it again");

		if(tryout == 3){

			LOGTGM(LOG_WARNING, "Modem does not respond to AT commands, reach maximum tryout now leave");

			return -1;
		}

	}


	if((response_code=tgm_chat(serial->fd, "ATZ\r\n", 3)) == RESPONSE_OK_CODE){

		LOGTGM(LOG_DEBUG, "CLEAR MODEM OK");

	}else {

		LOGTGM(LOG_DEBUG, "CLEAR MODEM FAILED, Return Now");

		return -1;

		//return 0;

	}

	if((response_code=tgm_chat(serial->fd, "ATE0\r\n", 1)) == RESPONSE_OK_CODE){

		LOGTGM(LOG_DEBUG, "TURN MODEM ECHO OK");

	}else {

		LOGTGM(LOG_DEBUG, "TURN MODEM ECHO FAILED, Return Now");

		return -1;
	}


	//TODO Check if PIN require
	response_code = tgm_chat(serial->fd, "AT+CPIN?\r\n", 3);

	switch(response_code){

	case RESPONSE_CPIN_READY_CODE:

		LOGTGM(LOG_INFO, "No PIN Required!!!");

		break;

	case RESPONSE_CPIN_SIM_PIN_CODE:

		LOGTGM(LOG_INFO, "PIN Required!!! return now");

		//return 0;


	case RESPONSE_CPIN_SIM_PUK_CODE:

		LOGTGM(LOG_INFO, "PUK Required!!! return now");

		//return 0;

	case RESPONSE_CME_ERROR_CODE:

		LOGTGM(LOG_INFO, "CME Error!!! return now");

		//return 0;

	default :

		return -1;

	}

	if((response_code = tgm_chat(serial->fd, "ATH0\r\n",3)) == RESPONSE_OK_CODE){

		LOGTGM(LOG_INFO, "HUNG UP OK !!! ");

	}else {

		LOGTGM(LOG_INFO, "HUNG UP FAILED !!! return now");

		return -1;
	}

	if((response_code=tgm_chat(serial->fd,"ATQ0 V1 E1 S0=0 &C1 &C2 +FCLASS=0\r\n",3))==RESPONSE_OK_CODE){

		LOGTGM(LOG_INFO, "Setting ATQ OK !!!");

	}else{

		LOGTGM(LOG_INFO, "HUNG UP FAILED !!! return now");

		return -1;
	}




//	if(tgm_chat(serial->fd, "AT+CGDCONT=1,\"IP\",\"internet\"\r\n",3)==RESPONSE_OK_CODE){
//
//		LOGD("SETTING PDP SUCCESS!!!");
//
//	}else{
//
//		LOGD("SETTING PDP FAILED!!!");
//
//	}

	while((response_code=tgm_chat(serial->fd, "AT+CGDCONT=1,\"IP\",\"internet\"\r\n",10))!=RESPONSE_OK_CODE){

		//We should set proper pdp before dial
		LOGD("SETTING PDP FAILED!!!, Now retry !!!");

	}



	if((response_code=tgm_chat(serial->fd, "ATD*99#\r\n",20)) == RESPONSE_CONNECT_CODE){

		LOGD("CONNECTED, TURNING TO PPPD");

		serial->state=SERIAL_STATE_OPERATING;

	}else{

		LOGD("Dial PDP Data Connection Failed, D");

		return -1;

	}


	//SYSCHECK(chat(serial->fd, "ATZ\r\n", 3));

	//SYSCHECK(chat(serial->fd, "ATE0\r\n", 1));

//	if (0)// additional siemens c35 init
//	{
//
//	SYSCHECK(snprintf(gsm_command, sizeof(gsm_command), "AT+IPR=%d\r\n", baud_rates[cmux_port_speed]));
//	SYSCHECK(chat(serial->fd, gsm_command, 1));
//	SYSCHECK(chat(serial->fd, "AT\r\n", 1));
//	SYSCHECK(chat(serial->fd, "AT&S0\r\n", 1));
//	SYSCHECK(chat(serial->fd, "AT\\Q3\r\n", 1));
//	}

//	if (pin_code >= 0)
//	{
//		LOGTGM(LOG_DEBUG, "send pin %04d", pin_code);
//		//Some modems, such as webbox, will sometimes hang if SIM code is given in virtual channel
//		SYSCHECK(snprintf(gsm_command, sizeof(gsm_command), "AT+CPIN=%04d\r\n", pin_code));
//		SYSCHECK(chat(serial->fd, gsm_command, 10));
//	}

	//serial->state = SERIAL_STATE_OPERATING;

	//sleep(5);

	return 0;


}


static int close_devices(){

	LOGTGM(LOG_DEBUG,"Closing Devices");

	if (serial.fd >= 0)
	{
		LOGD("Closing serial devices");

		static const char* poff = "AT@POFF\r\n";
		//syslogdump(">s ", (unsigned char *)poff, strlen(poff));

		write(serial.fd, poff, strlen(poff));

		//close device file description
		SYSCHECK(close(serial.fd));

		//reset file descriptor to null
		serial.fd = -1;
	}

	//change serial state to off
	serial.state = SERIAL_STATE_OFF;

	return 0;
}




//static int chat(int serial_device_fd, char *cmd, int to)
//{
//
//
//	unsigned char buf[1024];
//	int sel;
//	int len;
//	int wrote = 0;
//
//	syslogdump(">s ", (unsigned char *) cmd, strlen(cmd));
//
//	SYSCHECK(wrote = write(serial_device_fd, cmd, strlen(cmd)));
//
//	LOGTGM(LOG_DEBUG, "Wrote %d bytes", wrote);
//
//
//	ioctl(serial_device_fd, TCSBRK, 1); //equivalent to tcdrain(). perhaps permanent replacement?
//
//	fd_set rfds;
//
//	FD_ZERO(&rfds);
//	FD_SET(serial_device_fd, &rfds);
//
//	struct timeval timeout;
//
//	timeout.tv_sec = to;
//	timeout.tv_usec = 0;
//
//	do
//	{
//		//wait for data until time out
//		SYSCHECK(sel = select(serial_device_fd + 1, &rfds, NULL, NULL, &timeout));
//
//		LOGTGM(LOG_DEBUG, "Selected %d", sel);
//
//		if (FD_ISSET(serial_device_fd, &rfds)) //if there is something to read
//		{
//			//clear the buffer
//			memset(buf, 0, sizeof(buf));
//
//			//read from device of data size
//			len = read(serial_device_fd,buf, sizeof(buf));
//
//			SYSCHECK(len);
//
//			LOGTGM(LOG_DEBUG, "Read %d bytes from serial device", len);
//
//			syslogdump("<s ", buf, len);
//
//			errno = 0;
//
//			//if the data contains OK then that's fine with it
//			if (memstr((char *) buf, len, "OK"))
//			{
//				LOGTGM(LOG_DEBUG, "Received OK");
//				return 0;
//			}
//
//			//contains error
//			if (memstr((char *) buf, len, "ERROR"))
//			{
//				LOGTGM(LOG_DEBUG, "Received ERROR");
//				return -1;
//			}
//		}
//	} while (sel);
//
//	return -1;
//}



static int tgm_chat(int serial_device_fd, char *cmd, int to)
{


	unsigned char buf[1024];

	int sel;
	int len;
	int wrote = 0;
	int result_value = 0;

	syslogdump(">s ", (unsigned char *) cmd, strlen(cmd));

	SYSCHECK(wrote = write(serial_device_fd, cmd, strlen(cmd)));

	LOGTGM(LOG_DEBUG, "Wrote %d bytes", wrote);

	// waits until all output written to the object referred to by fd has been transmitted.
	//equivalent to tcdrain(). perhaps permanent replacement?
	ioctl(serial_device_fd, TCSBRK, 1);

	fd_set rfds;

	FD_ZERO(&rfds);

	FD_SET(serial_device_fd, &rfds);

	struct timeval timeout;

	timeout.tv_sec = to;
	timeout.tv_usec = 0;

	do
	{

		//wait for data until time out
		SYSCHECK(sel = select(serial_device_fd + 1, &rfds, NULL, NULL, &timeout));

		if(sel==-1){

			LOGD("Something wrong with the device, leaving now");

			close_devices();

			serial.state = SERIAL_STATE_OPENING;

			return DEVICE_ERROR;

		}

		//LOGTGM(LOG_DEBUG, "Selected %d", sel);

		if (FD_ISSET(serial_device_fd, &rfds)) //if there is something to read
		{
			//clear the buffer
			memset(buf, 0, sizeof(buf));

			//read from device of data size
			len = read(serial_device_fd, buf, sizeof(buf));

			SYSCHECK(len);

			LOGTGM(LOG_DEBUG, "Read %d bytes from serial device", len);

			syslogdump("<s ", buf, len);

			errno = 0;


			/////////////////////////////////////////////////////////////////////

			//NO PIN REQUIRED
			if (memstr((char *) buf, len, RESPONSE_CPIN_READY))
			{
				LOGTGM(LOG_DEBUG, "Received %s",RESPONSE_CPIN_READY);

				result_value = RESPONSE_CPIN_READY_CODE;

			}

			//PIN REQUIRED
			if (memstr((char *) buf, len, RESPONSE_CPIN_SIM_PIN))
			{
				LOGTGM(LOG_DEBUG, "Received %s",RESPONSE_CPIN_SIM_PIN);

				result_value = RESPONSE_CPIN_SIM_PIN_CODE;

			}

			//PUK REQUIRED
			if (memstr((char *) buf, len, RESPONSE_CPIN_SIM_PUK))
			{
				LOGTGM(LOG_DEBUG, "Received %s",RESPONSE_CPIN_SIM_PUK);

				result_value = RESPONSE_CPIN_SIM_PUK_CODE;

			}

			//Response result may be sent with OK message

			//OK
			if (memstr((char *) buf, len, RESPONSE_OK))
			{
				LOGTGM(LOG_DEBUG, "Received OK");

				if(result_value > 0){

					LOGTGM(LOG_DEBUG, "that is the OK of pin query");

					return result_value;

				}else{

					return RESPONSE_OK_CODE;

				}

			}

			//ERROR
			if (memstr((char *) buf, len, RESPONSE_ERROR))
			{
				LOGTGM(LOG_DEBUG, "Received ERROR");
				return RESPONSE_ERROR_CODE;
			}

			//CME ERROR
			if (memstr((char *) buf, len, RESPONSE_CME_ERROR))
			{
				LOGTGM(LOG_DEBUG, "Received %s",RESPONSE_CME_ERROR);

				return RESPONSE_CME_ERROR_CODE;
			}

			//CONNECT
			if (memstr((char *) buf, len, RESPONSE_CONNECT))
			{
				LOGTGM(LOG_DEBUG, "Received CONNECT");
				return RESPONSE_CONNECT_CODE;
			}

			//NO CARRIER
			if (memstr((char *) buf, len, RESPONSE_NO_CARRIER))
			{
				LOGTGM(LOG_DEBUG, "Received NO CARRIER");
				return RESPONSE_NO_CARRIER_CODE;
			}

			LOGD("Unknown Message %s\n",buf);

		}

	} while (sel); // sel == 0 --> time out


	return -1;
}

//
// return length of response
//

static int tgm_respose_chat(int device_fd, char *cmd, char *response, int length, int to)
{
	LOGTGM(LOG_DEBUG, "Enter TGM Chat");

	//unsigned char buf[1024];

	int sel;
	int wrote = 0;

	syslogdump(">s ", (unsigned char *) cmd, strlen(cmd));

	SYSCHECK(wrote = write(device_fd, cmd, strlen(cmd)));

	LOGTGM(LOG_DEBUG, "Wrote %d bytes", wrote);

	ioctl(device_fd, TCSBRK, 1); //equivalent to tcdrain(). perhaps permanent replacement?

	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(device_fd, &rfds);

	struct timeval timeout;

	timeout.tv_sec = to;
	timeout.tv_usec = 0;

	do
	{
		//wait for data until time out
		SYSCHECK(sel = select(device_fd + 1, &rfds, NULL, NULL, &timeout));

		LOGTGM(LOG_DEBUG, "Selected %d", sel);

		if (FD_ISSET(device_fd, &rfds)) //if there is something to read
		{
			//clear the buffer
			memset(response, 0, sizeof(response));

			//read from device of data size
			return read(device_fd, response, sizeof(response));

		}

	} while (sel);

	return -1;
}




/* 
 * Purpose:  Poll a device (file descriptor) using select()
 *                if select returns data to be read. call a reading function for the particular device
 * Input:      vargp - a pointer to a Poll_Thread_Arg struct.
 * Return:    NULL if error
 */
void* poll_thread(void *vargp) {

	LOGD("Enter Pool Thread");

	Poll_Thread_Arg* poll_thread_arg = (Poll_Thread_Arg*)vargp;

	if( poll_thread_arg->fd== -1 ){

		LOGD("Serial port not initialized");

		goto terminate;
	}

	
	
	while (1) {

		fd_set fdr,fdw;

		FD_ZERO(&fdr);
		FD_ZERO(&fdw);

		FD_SET(poll_thread_arg->fd,&fdr);

		if (select((1+poll_thread_arg->fd),&fdr,&fdw,NULL,NULL)>0) {

			if (FD_ISSET(poll_thread_arg->fd,&fdr)) {

				if((*(poll_thread_arg->read_function_ptr))(poll_thread_arg->read_function_arg)!=0){ /*Call reading function*/

					LOGD("Device read function returned error");
					goto terminate;

				}
			}
		}

		else{ //No need to evaluate retval=0 case, since no use of timeout in select()

			switch (errno){

				case EINTR:
					LOGD("Interrupt signal EINTR caught");
					break;

				case EAGAIN:
					LOGD("Interrupt signal EAGAIN caught");
					break;

				case EBADF:
					LOGD("Interrupt signal EBADF caught");
					break;

				case EINVAL:
					LOGD("Interrupt signal EINVAL caught");
					break;

				default:
					LOGD("Unknown interrupt signal caught\n");

			}//End of switch

			goto terminate;
		}//End of if-else

	}//End of while

	goto terminate;

	terminate:{

		LOGD("Device polling thread terminated");

		//free the memory allocated for the thread args before exiting thread
		free(poll_thread_arg);  


		return NULL;

	}
}

void set_main_exit_signal(int signal){

	//new lock
	pthread_mutex_lock(&main_exit_signal_lock);
	main_exit_signal = signal;
	pthread_mutex_unlock(&main_exit_signal_lock);

}



void createPPPConnection(){

	if((pppd_process_id=fork())){

		//Parent Process
		//Should we abandon the device?

		//tgm_chat(serial->fd, "ATH0\r\n",3);

		sleep(5);

		serial.state=SERIAL_STATE_PPPD;


	}else{

		//Child Process
		//INVOKE PPPD

		system("setprop net.dns1 168.95.1.1");

		execl("/system/xbin/pppd","pppd","/dev/ttyUSB0","debug","nodetach","460800","defaultroute","usepeerdns",(char *)NULL);
	}

}




unsigned char stateBuff[64];

void checkPPPConnection(){

	memset(stateBuff,0,sizeof(stateBuff));

	int pppStatefd = open(PPP_OPERSTATE_PATH, O_RDONLY | O_NOCTTY | O_NONBLOCK);

	if( pppStatefd == -1 ){

		LOGD("PPP0 seems not setup yet !!!");

		ppp_fail_times++;

		return;

	}else{

		int len = read(pppStatefd,stateBuff,60);

		if(len>0){

			if(memstr((char *) stateBuff,len,"unknown")||memstr((char *) stateBuff,len,"up")){

				if(serial.fd != -1){

					LOGD("remove serial device control");

					close(serial.fd);

					serial.fd = -1;

				}

				LOGD("PPP Connection is up and running !!!");

				ppp_fail_times=0;

				//serial.state=SERIAL_STATE_PPPD;


			}

			if(memstr((char *) stateBuff,len,"down")){

				ppp_fail_times++;

			}


		}else{

			ppp_fail_times++;

		}

	}

	if(pppStatefd>=0){

		close(pppStatefd);

	}


}

/* 
 * Purpose:  The watchdog state machine restarted every x seconds
 * Input:      serial - the serial struct
 * Return:    1 if error, 0 if success
 */
static int watchdog(Serial * serial)
{
	LOGTGM(LOG_DEBUG, "Enter watch dog");

	int i;

	LOGTGM(LOG_DEBUG, "Serial state is %d", serial->state);

	switch (serial->state)

	{
		case SERIAL_STATE_OPENING:

			watchdog_time_interval = 5;

			if (open_serial_device(serial) != 0){

				LOGTGM(LOG_WARNING, "Could not open serial device, try again");


				//return 0;
			}else{

				LOGTGM(LOG_INFO,"Serial Device Opened!!!");

				serial->state=SERIAL_STATE_INITIALIZING;

				watchdog_time_interval = 1;

			}

			break;

		case SERIAL_STATE_INITIALIZING:


			if( initialize_serial_device(serial) < 0 ){

				LOGTGM(LOG_ERR,"Unable to initial device, will retry in a few second");

			}else{

				serial->state = SERIAL_STATE_OPERATING;
			}


			break;

		case SERIAL_STATE_OPERATING:

			//Try to open device

			createPPPConnection();

			//TODO call pppd to set up link
			//may be we shall release this device and handle it to pppd

			watchdog_time_interval = 5;

			break;

		case SERIAL_STATE_PPPD:

			checkPPPConnection();


			if( ppp_fail_times > MAX_PPP_FAIL_LIMIT){

				LOGD("PPP failed over the limits, now destroy everything and retry");

				kill(pppd_process_id,SIGTERM);

				close_devices();

				ppp_fail_times=0;

				serial->state=SERIAL_STATE_OPENING;

			}


			break;


		case SERIAL_STATE_CLOSING:

			close_devices();
			serial->state = SERIAL_STATE_OPENING;
			LOGTGM(LOG_DEBUG, "Switched Mux state to %d ",serial->state);

			break;

		default:
			LOGTGM(LOG_WARNING, "Don't know how to handle state %d", serial->state);
			break;
	}

	return 0;
}

static int memstr( const char *haystack, int length, const char *needle)
{
	int i;
	int j = 0;

	if (needle[0] == '\0')
		return 1;

	for (i = 0; i < length; i++){

		if (needle[j] == haystack[i]){
			j++;
			if (needle[j] == '\0') // Entire needle was found
				return 1;
		}else{
			j = 0;
		}


	}

	return 0;
}

/*
 * Purpose:  ascii/hexdump a byte buffer 
 * Input:	    prefix - string to printed before hex data on every line
 *                ptr - the string to be dumped
 *                length - the length of the string to be dumped
 * Return:    0
 */

static int syslogdump( const char *prefix, const unsigned char *ptr, unsigned int length)
{
	if (LOG_DEBUG>syslog_level) /*No need for all frame logging if it's not to be seen */
		return 0;

	char buffer[100];
	unsigned int offset = 0l;
	int i;

	pthread_mutex_lock(&syslogdump_lock); 	//new lock

	while (offset < length)
	{
		int off;
		strcpy(buffer, prefix);
		off = strlen(buffer);
		SYSCHECK(snprintf(buffer + off, sizeof(buffer) - off, "%08x: ", offset));
		off = strlen(buffer);

		for (i = 0; i < 16; i++)
		{
			if (offset + i < length){
				SYSCHECK(snprintf(buffer + off, sizeof(buffer) - off, "%02x%c", ptr[offset + i], i == 7 ? '-' : ' '));
			}
			else{
				SYSCHECK(snprintf(buffer + off, sizeof(buffer) - off, " .%c", i == 7 ? '-' : ' '));
			}
			off = strlen(buffer);
		}

		SYSCHECK(snprintf(buffer + off, sizeof(buffer) - off, " "));

		off = strlen(buffer);

		for (i = 0; i < 16; i++)
			if (offset + i < length)
		{
			SYSCHECK(snprintf(buffer + off, sizeof(buffer) - off, "%c", (ptr[offset + i] < ' ') ? '.' : ptr[offset + i]));
			off = strlen(buffer);
		}
		offset += 16;
		LOGTGM(LOG_DEBUG,"%s", buffer);
	}

	pthread_mutex_unlock(&syslogdump_lock);/*new lock*/

	return 0;
}

void onInterruptSignal(int signal){

	LOGD("ON INTERRUPT");

	//try to close all devices
	close_devices();

	set_main_exit_signal(SIGINT);

	//signal(SIGINT, SIG_DFL);

}

#define SOCKET_NAME_TGM "tgmd"
static int sock_fd = -1;

int openSocket(){

#if 1

	sock_fd = socket_local_server(SOCKET_NAME_TGM,ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

	if(sock_fd >= 0){

		LOGD("Local Socket Open successfully!!!");

	}else {

		LOGD("Failed to create local socket");

	}

#else

	sock_fd = android_get_control_socket(SOCKET_NAME_TGM);

	    if (sock_fd < 0) {

	        LOGE("Failed to get socket '" SOCKET_NAME_TGM "'");

	        exit(-1);

	    }else{

	    	LOGD("Socket successfully created!!!");

	    }

	    int ret = listen(sock_fd, 4);

	    if (ret < 0) {

	    	LOGE("Failed to listen on control socket '%d': %s",sock_fd, strerror(errno));

	    	return -1;

	        //exit(-1);
	    }

#endif
	    return 1;

}




int main(int argc,char* args[]){

	//TODO read device name from args

	char **tgmArgv;
	void *dlHandle;

	const TGM_RadioFunctions *(*tgmInit)(const struct TGM_Env *, int, char **); //define a varible that hold a pointer to init function
	const TGM_RadioFunctions *funcs; // define a varbile that hold a pointer to TGM_RadioFunctions structure

	char libPath[PROPERTY_VALUE_MAX];

	unsigned char hasLibArgs = 0;

	//Should we read something here?
	const char * tgmLibPath = "/system/lib/libhuawei-tgm.so";


	 //OPEN LIBRARY
	 dlHandle = dlopen(tgmLibPath, RTLD_NOW);

	 if (dlHandle == NULL) {
		 fprintf(stderr, "dlopen failed: %s\n", dlerror());
		 exit(-1);
	 }

	TGM_startEventLoop();

	tgmInit = (const TGM_RadioFunctions *(*)(const struct TGM_Env *, int, char **))dlsym(dlHandle, "TGM_Init");



	if (tgmInit == NULL) {
		fprintf(stderr, "RIL_Init not defined or exported in %s\n", tgmLibPath);
		exit(-1);
	}

	funcs = tgmInit(&s_tgmEnv, argc, tgmArgv);

	TGM_register(funcs);

	 while(1) {
		// sleep(UINT32_MAX) seems to return immediately on bionic
		sleep(0x00ffffff);
	}



//	// below is old version
//	serial.devicename = DEFAULT_SERIAL_DEVICE;
//	serial.state = SERIAL_STATE_OPENING;
//	serial.fd = -1;
//
//	//Register signal
//
//	signal(SIGINT,onInterruptSignal);
//
//
//
//
//	//openSocket();
//
//	//Main Loop if watch dog not success or exit signal = 1
//	while((main_exit_signal==0) && (watchdog(&serial)==0)){
//
//		//LOGD("Main Looping State:%d ",serial.state);
//
//		sleep(watchdog_time_interval);
//	}
//
//
//	LOGD("Exiting Main Loop");
//
//	SYSCHECK(close_devices());
//
//
////	while(1) {
////		// sleep(UINT32_MAX) seems to return immediately on bionic
////		sleep(0x00ffffff);
////
////	}

	return 0;

}
