
#include <datanetwork/tgm.h>

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <termios.h>

#define LOG_TAG "TGM"

#include <utils/Log.h>

//event object from tgmd
static const struct TGM_Env *s_tgmEnv;

// response to the tgm event object
#define TGM_onRequestComplete(t, e, response, responselen) s_tgmEnv->OnRequestComplete(t,e, response, responselen)
#define TGM_onUnsolicitedResponse(a,b,c) s_tgmEnv->OnUnsolicitedResponse(a,b,c)
#define TGM_requestTimedCallback(a,b,c) s_tgmEnv->RequestTimedCallback(a,b,c)


// PPPD Related //20 Secs before consider failed
#define MAX_PPP_FAIL_LIMIT 20

#define PPP_OPERSTATE_PATH "/sys/class/net/ppp0/operstate"

static int pppd_process_id;

static int pppd_fail_times=0;


pthread_t s_tid_mainloop;

static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;

static int s_closed = 0;

static int fd = -1;

static char* username;
static char* password;

static char* cachedCarrier[3];
static char* cachedManufacturer;
static char* cachedModel;


static const char * s_device_path = NULL;

static const struct timeval TIMEVAL_SIMPOLL = {1,0};
static const struct timeval TIMEVAL_0 = {0,0};


static TGM_RadioState sState = TGM_STATE_UNAVAILABLE;

//method definition

static void *mainLoop(void *param);

static void onATReaderClosed();

static void onATTimeout();

static void onUnsolicited (const char *s, const char *sms_pdu);

static void onCancel (TGM_Token t);

static void onRequest (int request, void *data, size_t datalen, TGM_Token t);

static void onRadioPowerOn();

static void onSIMReady();

static int onSupports (int requestCode);

static void setRadioState(TGM_RadioState newState);

static void initializeCallback(void *param);

static void waitForClose();

static int isRadioOn();

static void pollSIMState (void *param);

static int getSIMStatus();

static TGM_RadioState currentState();

static const char * getVersion(void);

static void requestOperator(void *data, size_t datalen, TGM_Token t);

static void requestSignalStrength(void *data, size_t datalen, TGM_Token t);

static void requestSetupDefaultPDP(void *data, size_t datalen, TGM_Token t);

static void requestDialDataNetwork(void *data, size_t datalen, TGM_Token t);

static void requestDisconnectDataNetwork(void *data, size_t datalen, TGM_Token t);

static void requestEnterSIMPin(void *data, size_t datalen, TGM_Token t);

static void requestResetStack(void *data, size_t datalen, TGM_Token t);

static void requestSetPPPDAuth(void *data, size_t datalen, TGM_Token t);


static const TGM_RadioFunctions s_callbacks = {

    TGM_VERSION, //TGM Version in tgm.h
    onRequest,
    currentState,
    onSupports,
    onCancel,
    getVersion
};


static TGM_RadioState currentState()
{
    return sState;
}

static int onSupports (int requestCode)
{
    //@@@ todo

    return 1;
}

static void onCancel (TGM_Token t)
{
    //@@@todo

}

static const char * getVersion(void)
{
    return "camangi huawei 3G modem stack";
}

static void onRequest (int request, void *data, size_t datalen, TGM_Token t){

	ATResponse *p_response;
	int err;

	LOGD("onRequest: %d",request);

	if((sState == TGM_STATE_OFF || sState == TGM_STATE_UNAVAILABLE)){

		if(request != TGM_REQUEST_QUERY_SIM_STATUS && request != TGM_REQUEST_QUERY_STATE && request != TGM_REQUEST_RESET_STACK){

			LOGD("Command is remove due to invalid status: state_off , state_unavailable");
			//These two basic command to determine the stack is usable~~or not

			TGM_onRequestComplete(t,TGM_E_RADIO_NOT_AVAILABLE,NULL,0);

			return;

		}
	}

	if(sState==TGM_STATE_PPPD || sState == TGM_STATE_PPPD_READY){

		if(request != TGM_REQUEST_QUERY_STATE && request != TGM_REQUEST_RESET_STACK && request!=TGM_REQUEST_DISCONNECT_DATA_NETWORK){

			switch(request){

			case TGM_REQUEST_QUERY_OPERATOR:

				LOGD("Returning Cached Operator %s, %s, %s",cachedCarrier[0],cachedCarrier[1],cachedCarrier[2]);

				TGM_onRequestComplete(t, TGM_E_SUCCESS, cachedCarrier, sizeof(cachedCarrier));

				return ;
			case TGM_REQUEST_QUERY_MODEM_MODEL:

				LOGD("Returning Cached Modem Model %s",cachedModel);

				TGM_onRequestComplete(t, TGM_E_SUCCESS, cachedModel, sizeof(char *));

				return;

			case TGM_REQUEST_QUERY_MANUFACTURER:

				LOGD("Returning Cached Manufacturer %s",cachedManufacturer);

				TGM_onRequestComplete(t, TGM_E_SUCCESS, cachedManufacturer, sizeof(char *));

				return;

			default:

				LOGD("Command is remove due to invalid status : pppd");
				TGM_onRequestComplete(t,TGM_E_OP_NOT_ALLOWED_DURING_PPPD,NULL,0);
				return;

			}
		}

	}


	switch (request) {

	case TGM_REQUEST_SET_PDP_CONTEXT:
	{
		requestSetupDefaultPDP(data, datalen, t);

		break;
	}

	case TGM_REQUEST_SET_PIN_CODE :

		LOGD("TGM_REQUEST_SET_PIN_CODE");
		requestEnterSIMPin(data, datalen, t);

		break;

	case TGM_REQUEST_DIAL_DATA_NETWORK :

		LOGD("TGM_REQUEST_DIAL_DATA_NETWORK");

		if(sState!=TGM_STATE_SIM_READY){
			return;
		}

		requestDialDataNetwork(data,datalen,t);

		break;

	case TGM_REQUEST_DISCONNECT_DATA_NETWORK :

		if(sState!=TGM_STATE_PPPD&&sState!=TGM_STATE_PPPD_READY){

			return ;

		}

		LOGD("TGM_REQUEST_DISCONNECT_DATA_NETWORK");

		requestDisconnectDataNetwork(data,datalen,t);

		break;


	case TGM_REQUEST_QUERY_SIGNAL_STRENGTH :

		LOGD("TGM_REQUEST_QUERY_SIGNAL_STRENGTH");

		requestSignalStrength(data, datalen, t);

		break;

	case TGM_REQUEST_QUERY_IMEI :

		LOGD("TGM_REQUEST_QUERY_IMEI");

		p_response = NULL;

		err = at_send_command_numeric("AT+CGSN", &p_response);

		if (err < 0 || p_response->success == 0) {

			TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

		} else {

			TGM_onRequestComplete(t, TGM_E_SUCCESS, p_response->p_intermediates->line, sizeof(char *));



			property_set("gsm.imei", p_response->p_intermediates->line);

		}

		at_response_free(p_response);


		break;

	case TGM_REQUEST_QUERY_NUMBER:

		LOGD("TGM_REQUEST_QUERY_NUMBER");

		p_response = NULL;

		err = at_send_command_singleline("AT+CNUM","+CNUM:", &p_response);

		if (err < 0 || p_response->success == 0) {

			TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

		} else {

			TGM_onRequestComplete(t, TGM_E_SUCCESS, p_response->p_intermediates->line, sizeof(char *));

		}

		at_response_free(p_response);


		break;

	case TGM_REQUEST_QUERY_MANUFACTURER :

		LOGD("TGM_REQUEST_QUERY_MANUFACTURER");

		p_response = NULL;

		err = at_send_command_singleline("AT+CGMI","", &p_response);

		if (err < 0 || p_response->success == 0) {

			TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

		} else {

			//clear cachedModel
			memset(cachedManufacturer, 0, 128);

			size_t copylen =strlen(p_response->p_intermediates->line);

			if(copylen <= 128){

				strcpy(cachedManufacturer,p_response->p_intermediates->line);

			}

			LOGD("Cached Manufacturer: %s", cachedManufacturer);

			TGM_onRequestComplete(t, TGM_E_SUCCESS, p_response->p_intermediates->line, sizeof(char *));

		}

		at_response_free(p_response);

		break;

	case TGM_REQUEST_QUERY_MODEM_MODEL:

		LOGD("TGM_REQUEST_QUERY_MODEM_MODEL");

		p_response = NULL;

		err = at_send_command_singleline("AT+CGMM","", &p_response);

		if (err < 0 || p_response->success == 0) {

			TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

		} else {

			//clear cachedModel
			memset(cachedModel, 0, 128);

			size_t copylen = strlen(p_response->p_intermediates->line);

			if(copylen <= 128){

				strcpy(cachedModel, p_response->p_intermediates->line);

			}

			LOGD("Cached Model: %s", cachedModel);

			TGM_onRequestComplete(t, TGM_E_SUCCESS, p_response->p_intermediates->line, sizeof(char *));

		}

		at_response_free(p_response);

		break;

	case TGM_REQUEST_QUERY_OPERATOR :

		LOGD("TGM_REQUEST_QUERY_OPERATOR");

		requestOperator(data,datalen,t);

		break;

	case TGM_REQUEST_QUERY_SIM_STATUS:
	{
		LOGD("TGM_REQUEST_QUERY_SIM_STATUS");

		int simStatus;
		simStatus = getSIMStatus();
		TGM_onRequestComplete(t, TGM_E_SUCCESS, &simStatus, sizeof(simStatus));

		break;
	}

	case TGM_REQUEST_QUERY_IMSI:

		LOGD("TGM_REQUEST_QUERY_IMSI");

		p_response = NULL;
		err = at_send_command_numeric("AT+CIMI", &p_response);

		if (err < 0 || p_response->success == 0) {
			TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);
		} else {
			TGM_onRequestComplete(t, TGM_E_SUCCESS,
				p_response->p_intermediates->line, sizeof(char *));
		}
		at_response_free(p_response);

		break;

	case TGM_REQUEST_QUERY_STATE:

		LOGD("TGM_REQUEST_QUERY_STATE");

		TGM_onRequestComplete(t, TGM_E_SUCCESS, &sState, sizeof(sState));

		break;

	case TGM_REQUEST_RESET_STACK:

		LOGD("TGM_REQUEST_RESET_STACK");

		requestResetStack(data,datalen,t);

		break;

	case TGM_REQUEST_SET_PPPD_AUTH:

		requestSetPPPDAuth(data,datalen,t);
		LOGD("TGM_REQUEST_SET_PPPD_AUTH");

		break;


	default:
		//TODO unsupport request here
		break;

	}
}

const TGM_RadioFunctions *TGM_Init(const struct TGM_Env *env, int argc, char **argv){

	s_tgmEnv = env;

	username = malloc(32 * sizeof(char));
	password = malloc(32 * sizeof(char));

	cachedManufacturer =  malloc(128 * sizeof(char));
	cachedModel = malloc(128 * sizeof(char));

	cachedCarrier[0] = malloc(3 * 128 * sizeof(char));
	cachedCarrier[1] = (char *) &cachedCarrier[128];
	cachedCarrier[2] = (char *) &cachedCarrier[256];


	int ret;
	int opt;

	pthread_attr_t attr;

	s_tgmEnv = env;

	s_device_path="/dev/ttyUSB0";

	if( s_device_path == NULL ){
		return NULL;
	}

	//TODO ADD S_path here

	pthread_attr_init (&attr);

	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	ret = pthread_create(&s_tid_mainloop, &attr, mainLoop, NULL);

	return &s_callbacks;
}


static unsigned char stateBuff[64];

static int checkPPPConnection(){

	if(pppd_process_id < 0){

		LOGD("checkPPPConnection pppd_process_id < 0");

		pppd_fail_times = MAX_PPP_FAIL_LIMIT;

		return 0;
	}

	memset(stateBuff, 0, sizeof(stateBuff));

	int pppStatefd = open(PPP_OPERSTATE_PATH, O_RDONLY | O_NOCTTY | O_NONBLOCK);

	int success = 0;

	if( pppStatefd == -1 ){

		LOGD("PPP0 seems not setup yet !!!");

		success = 0;

	}else{

		int len = read(pppStatefd,stateBuff,60);

		if(len>0){

			if( 0 == strncmp((char *)stateBuff,"unknown",7) || 0 == strncmp((char *) stateBuff,"up",2)){

				LOGI("PPP Connection is up and running !!!");

				success = 1;

			}else if(0 == strncmp((char *)stateBuff,"down",4)){

				success = 0;
			}

		}else{

			pppd_fail_times++;

			success = 0;

		}

	}

	if( pppStatefd >= 0 ){

		close(pppStatefd);

	}

	return success;

}

pthread_t s_tid_waitForPPPD;

pthread_attr_t attr_waitForPPPD;

static pthread_mutex_t s_pppd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_pppd_cond = PTHREAD_COND_INITIALIZER;



static void *waitForPPPDTerminated(){

	int child_state;

	LOGD("waitForPPPDTerminated START");

	int err = waitpid(pppd_process_id,&child_state,0);

	LOGD("waitForPPPDTerminated waitpid done");

	pthread_mutex_lock(&s_pppd_mutex);

	LOGD("waitForPPPDTerminated pthread_mutex_lock");

	pppd_process_id = -1;

	pthread_cond_broadcast(&s_pppd_cond);

	LOGD("waitForPPPDTerminated pthread_cond_broadcast");

	pthread_mutex_unlock(&s_pppd_mutex);

	LOGD("waitForPPPDTerminated END");

	return 0;


}

static void *mainLoop(void *param)
{

    int ret, notifyDeviceRemoved, notifyDeviceDetected, notifyPPPDConnected, notifyPPPDDisconnected;

    AT_DUMP("==", "entering Loop()", -1 );

    at_set_on_reader_closed(onATReaderClosed);

    at_set_on_timeout(onATTimeout);

    for (;;) {

    	LOGD("Main Loop Radio State: %d",sState);

    	notifyDeviceRemoved = notifyDeviceDetected = notifyPPPDConnected = notifyPPPDDisconnected = 0;

    	if( sState == TGM_STATE_PPPD ){

    		//weird solution? cause at channel non-blocking them at_reader_close will shut at channel automatically
    		LOGD("Making FD NON-BLOCKING!!!");

    		fcntl(fd, F_SETFL, O_NDELAY);

    		//at_close();

    		pppd_process_id = fork();

    		if(pppd_process_id){

    			//Create another thread to wait for pppd terminated!!!;
    			pthread_attr_init (&attr_waitForPPPD);

    			pthread_attr_setdetachstate(&attr_waitForPPPD, PTHREAD_CREATE_DETACHED);

    			pthread_create(&s_tid_waitForPPPD,&attr_waitForPPPD,waitForPPPDTerminated,NULL);

    			//original thread just wait here to check pppd connection is good;
    			pppd_fail_times = 0;

    			int pppd_success = 0;

    			do{
    				LOGD("Checking PPP Connection");

    				pppd_success = checkPPPConnection();

    				if(pppd_success){

    					//Setting connection type to 3g
    					property_set("gsm.network.type","UMTS");

    					//send connect success
    					TGM_onUnsolicitedResponse(TGM_UNSOLICITED_PPPD_CONNECTED,NULL,0);

    					//set state to pppd ready
    					setRadioState(TGM_STATE_PPPD_READY);

    					pppd_fail_times = 0;

    					break;

    				}else{

    					pppd_fail_times++;

    				}

    				if(pppd_process_id < 0){

    					LOGD("LOOP pppd_process_id < 0");
    					break;

    				}

    				sleep(1);

    			}while( pppd_fail_times < MAX_PPP_FAIL_LIMIT);

    			//SEND PPPD FAILED MESSAGE
    			if( pppd_fail_times >= MAX_PPP_FAIL_LIMIT){

    				TGM_onUnsolicitedResponse(TGM_UNSOLICITED_PPPD_FAILED, NULL, 0);

    				//Should we terminate pppd process here?

    				if(pppd_process_id >= 0){

    					kill(pppd_process_id,SIGTERM);

    				}
    			}

    			//if pppd process still exists, then just wait for it

    			while( pppd_process_id >= 0){

    				pthread_cond_wait(&s_pppd_cond, &s_pppd_mutex);

    			}

    			if(sState == TGM_STATE_PPPD_READY){

    				TGM_onUnsolicitedResponse(TGM_UNSOLICITED_PPPD_DISCONNECTED,NULL,0);

    			}

    			setRadioState(TGM_STATE_UNAVAILABLE);


			}else{

				//Child Process
				//INVOKE PPPD

				LOGD("PPPD Process Starts");

				system("setprop net.dns1 168.95.1.1");

				if(strlen(username)>0){

					if(strlen(password)>0){

						LOGD("PPPD START WITH COMPLETE AUTH");

						execl("/system/xbin/pppd","pppd","/dev/ttyUSB0","debug","nodetach","460800","defaultroute","usepeerdns","user",username,"password", password,(char *)NULL);

					}else{

						execl("/system/xbin/pppd","pppd","/dev/ttyUSB0","debug","nodetach","460800","defaultroute","usepeerdns","user",username,(char *)NULL);
					}

				}

				execl("/system/xbin/pppd","pppd","/dev/ttyUSB0","debug","nodetach","460800","defaultroute","usepeerdns",(char *)NULL);
			}

    		continue;
    	}

    	if(fd >= 0){

    		//shall we close the device here?
    		LOGD("Making FD NON-BLOCKING!!!");

    		fcntl(fd, F_SETFL, O_NDELAY);

    		//at_close();
    	}

    	fd = -1;

        while  (fd < 0) {

        	if (s_device_path != NULL) {

                //TODO CHANGES IN HERE
        		fd = open (s_device_path, O_RDWR);

                if ( fd >= 0 && !memcmp( s_device_path, "/dev/ttyS", 9 ) ) {
                    /* disable echo on serial ports */
                    struct termios  ios;
                    tcgetattr( fd, &ios );
                    ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
                    tcsetattr( fd, TCSANOW, &ios );


                    if(!notifyDeviceDetected){

                    	TGM_onUnsolicitedResponse(TGM_UNSOLICITED_DEVICE_DETECTED,NULL,0);

                    	notifyDeviceDetected = 1;
                    }

                    setRadioState(TGM_STATE_UNAVAILABLE);

                }
            }

            if (fd < 0) {

            	if(!notifyDeviceRemoved){

            		TGM_onUnsolicitedResponse(TGM_UNSOLICITED_DEVICE_REMOVED,NULL,0);

            		notifyDeviceRemoved = 1;
            	}

            	setRadioState (TGM_STATE_OFF);

                perror ("opening AT interface. retrying...");

                sleep(6);

                /* never returns */
            }
        }

        s_closed = 0;

        ret = at_open(fd, onUnsolicited);

        if (ret < 0) {

            LOGE ("AT error %d on at_open\n", ret);
            return 0;

        }

        //When device is found just initial the device
        TGM_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

        // Give initializeCallback a chance to dispatched, since
        // we don't presently have a cancellation mechanism
        sleep(5);

        waitForClose();

    }

}


    /* Called on command or reader thread */
	static void onATReaderClosed()
	{

		LOGI("AT channel closed\n");

		at_close();

		s_closed = 1;

		fd = -1;

		if( sState != TGM_STATE_PPPD && sState != TGM_STATE_PPPD_READY){

			setRadioState (TGM_STATE_UNAVAILABLE);

		}

	}



    /* Called on command thread */
    static void onATTimeout()
    {

    	exit(1);
    	//LOGI("AT channel timeout; closing\n");

    	//fcntl(fd, F_SETFL, O_NDELAY);

		at_close();

		s_closed = 1;

		/* FIXME cause a radio reset here */
		if( sState != TGM_STATE_PPPD && sState != TGM_STATE_PPPD_READY ){

			 setRadioState (TGM_STATE_UNAVAILABLE);

		}
    }

    static void onUnsolicited (const char *s, const char *sms_pdu)
    {

    	//AT Channel Reader call this after receive unsolicited Message
    	//TODO Maybe there is nothing to do with 3g Modem

    }

    static void setRadioState(TGM_RadioState newState)
    {

    	LOGD("Set Radio State :%d ",newState);

    	TGM_RadioState oldState;

        pthread_mutex_lock(&s_state_mutex);

        oldState = sState;

        if (s_closed > 0) {

            // This is here because things on the main thread
            // may attempt to change the radio state after the closed
            // event happened in another thread

        	//MODIFY HERE
        	if(newState != TGM_STATE_PPPD && newState!=TGM_STATE_OFF && newState != TGM_STATE_PPPD_READY){

        		newState = TGM_STATE_UNAVAILABLE;

        	}
        }

        if (sState != newState || s_closed > 0) {

        	LOGD("State Actual Changes");

        	sState = newState;

        	if(sState != oldState){

				LOGD("NOTIFY State Change");

				TGM_onUnsolicitedResponse (TGM_UNSOLICITED_RADIO_STATE_CHANGED,NULL, 0);

			}

            pthread_cond_broadcast (&s_state_cond);
        }

        pthread_mutex_unlock(&s_state_mutex);

        //FIXME there is a race condition here

        /* do these outside of the mutex */
        if (sState != oldState) {

            /* FIXME onSimReady() and onRadioPowerOn() cannot be called
             * from the AT reader thread
             * Currently, this doesn't happen, but if that changes then these
             * will need to be dispatched on the request thread
             */

            if (sState == TGM_STATE_SIM_READY) {

            	onSIMReady();

            } else if (sState == TGM_STATE_SIM_NOT_READY) {

                onRadioPowerOn();

            }else if(sState == TGM_STATE_OFF){


            }else if(sState == TGM_STATE_UNAVAILABLE){

            }
        }
    }

    static void onSIMReady()
	{

		//TODO shall 3g modem implements this

    	//The following is SMS Related Functionality // Should we keep it here

    	at_send_command_singleline("AT+CSMS=1", "+CSMS:", NULL);

		//at_send_command("AT+CNMI=1,2,2,1,1", NULL);
	}

    /** do post-AT+CFUN=1 initialization */
	static void onRadioPowerOn()
	{

		pollSIMState(NULL);
	}



    static void initializeCallback(void *param)
    {

    	//TODO Remove 3G Modem unrelated initialization, and add something needed
        ATResponse *p_response = NULL;

        int err;

        setRadioState(TGM_STATE_UNAVAILABLE);

        at_handshake();

        /* note: we don't check errors here. Everything important will
           be handled in onATTimeout and onATReaderClosed */

        /*  atchannel is tolerant of echo but it must */
        /*  have verbose result codes */


        at_send_command("ATE0Q0V1", NULL);

        /*  No auto-answer */
        //at_send_command("ATS0=0", NULL);

        //sleep(2);

        /*  Extended errors */
        err = at_send_command("AT+CMEE=1", NULL);

        /*  Network registration events */
        //err = at_send_command("AT+CREG=2", &p_response);

        err = at_send_command("AT+CREG=1", NULL);

//        /* some handsets -- in tethered mode -- don't support CREG=2 */
//        if (err < 0 || p_response->success == 0) {
//
//        	at_send_command("AT+CREG=1", NULL);
//
//        }

        at_response_free(p_response);

        /*  GPRS registration events */
        //at_send_command("AT+CGREG=1", NULL);

        /*  Call Waiting notifications */
        //at_send_command("AT+CCWA=1", NULL);

        /*  Alternating voice/data off */
        //at_send_command("AT+CMOD=0", NULL);

        /*  Not muted */
        //at_send_command("AT+CMUT=0", NULL); //not supported

        /*  +CSSU unsolicited supp service notifications */
        //at_send_command("AT+CSSN=0,1", NULL);

        /*  no connected line identification */
        //at_send_command("AT+COLP=0", NULL);


        //this will cause reader loop escape
        //at_send_command("ATQ0 V1 E1 S0=0 &C1 &D2 +FCLASS=0",NULL);


        /*  HEX character set */
        //at_send_command("AT+CSCS=\"HEX\"", NULL); // not supported by huawei

        /*  USSD unsolicited */
        //at_send_command("AT+CUSD=1", NULL);

        //Sometimes We will just hanging here
        /*  Enable +CGEV GPRS event notifications, but don't buffer */
        //at_send_command("AT+CGEREP=1,0", NULL);

        /*  SMS PDU mode */
        //at_send_command("AT+CMGF=0", NULL);

        /* assume radio is off on error */

        //Stupid Huawei Dongle may just hang while query some network at command...
        //So we wait 2-3 Second here

        //sleep(3);

        if (isRadioOn() > 0) {

        	LOGI("Radio OK!!");

            setRadioState (TGM_STATE_SIM_NOT_READY);
        }



    }

    static void waitForClose()
    {
        pthread_mutex_lock(&s_state_mutex);

        while (s_closed == 0) {

            pthread_cond_wait(&s_state_cond, &s_state_mutex);

        }

        pthread_mutex_unlock(&s_state_mutex);
    }

    static int isRadioOn()
    {
        ATResponse *p_response = NULL;
        int err;
        char *line;
        char ret;


        //This is Set phone functionality? should we keep it here?
        err = at_send_command_singleline("AT+CFUN?", "+CFUN:", &p_response);


        if (err < 0 || p_response->success == 0) {

        	//MODIFY HERE
        	//Return Ok with Invalid response, we assume radio is OK
        	if(err==AT_ERROR_INVALID_RESPONSE){

        		return 1;
        	}

        	// assume radio is off
            goto error;
        }

        line = p_response->p_intermediates->line;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextbool(&line, &ret);

        if (err < 0) goto error;

        at_response_free(p_response);

        return (int)ret;

    error:

        at_response_free(p_response);
        return -1;
    }

    static void requestDialDataNetwork(void *data, size_t datalen, TGM_Token t){


    	//Dial the default context network...

    	//int err=0;

    	int err = at_send_command("ATD*99***1#", NULL);

    	//main loop just halt to the waiting to close loop, just set s_close = 1 and set radio state to
    	//continue


    	if(err<0){

    		TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

    		//cause waitForClose mutex wait escapse
    		s_closed = 1;

    		setRadioState(TGM_STATE_UNAVAILABLE);

    	}else{

    		//Success

    		TGM_onRequestComplete(t, TGM_E_SUCCESS, NULL, 0);

    		s_closed = 1;

    		setRadioState( TGM_STATE_PPPD ); // waitForClose is block by mutex condition...

    	}

    }

    static void requestDisconnectDataNetwork(void *data, size_t datalen, TGM_Token t){

    	if(pppd_process_id>=0){

    		kill(pppd_process_id,SIGTERM);

    	}

    	TGM_onRequestComplete(t, TGM_E_SUCCESS, NULL, 0);


    }

    static void requestSetupDefaultPDP(void *data, size_t datalen, TGM_Token t)
    {
        const char *apn;
        char *cmd;
        int err;

        ATResponse *p_response = NULL;

        apn = ((const char **)data)[0];


        LOGD("requesting data connection to APN '%s'", apn);


		asprintf(&cmd, "AT+CGDCONT=1,\"IP\",\"%s\",,0,0", apn);

		//FIXME check for error here

		err = at_send_command(cmd, NULL);

		free(cmd);

		// Set required QoS params to default
		err = at_send_command("AT+CGQREQ=1", NULL);

		// Set minimum QoS params to default
		err = at_send_command("AT+CGQMIN=1", NULL);

		// packet-domain event reporting
		err = at_send_command("AT+CGEREP=1,0", NULL);

		//Do not dial here, we are not done yet

		// Start data on PDP context 1
		//err = at_send_command("ATD*99***1#", &p_response);

		if (err < 0) {
			goto error;
		}

        TGM_onRequestComplete(t, TGM_E_SUCCESS, NULL, 0);

        at_response_free(p_response);

        return;

    error:
        TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);
        at_response_free(p_response);

    }

    static void requestEnterSIMPin(void *data, size_t datalen, TGM_Token t){

    	ATResponse   *p_response = NULL;
    	int           err;
    	char*         cmd = NULL;
		const char**  strings = (const char**)data;

		if ( datalen == sizeof(char*) ) {

			asprintf(&cmd, "AT+CPIN=%s", strings[0]);

		} else if ( datalen == 2*sizeof(char*) ) {

			asprintf(&cmd, "AT+CPIN=%s,%s", strings[0], strings[1]);

		} else{

			goto error;

		}


		err = at_send_command_singleline(cmd, "+CPIN:", &p_response);

		free(cmd);

		if (err < 0 || p_response->success == 0) {

			LOGD("err: %d",err);
			//AT+CPIN Return OK But no intermediate result
			if(err==AT_ERROR_INVALID_RESPONSE){
				goto success;
			}

		error:
			TGM_onRequestComplete(t, TGM_E_PASSWORD_INCORRECT, NULL, 0);

		} else {

		success:

			TGM_onRequestComplete(t, TGM_E_SUCCESS, NULL, 0);

			TGM_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

		}

		at_response_free(p_response);
    }

    static void requestOperator(void *data, size_t datalen, TGM_Token t)
	{
		int err;
		int i;
		int skip;
		ATLine *p_cur;
		char *response[3];

		memset(response, 0, sizeof(response));

		ATResponse *p_response = NULL;

		err = at_send_command_multiline(
			"AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?",
			"+COPS:", &p_response);

		/* we expect 3 lines here:
		 * +COPS: 0,0,"T - Mobile"
		 * +COPS: 0,1,"TMO"
		 * +COPS: 0,2,"310170"
		 */

		if (err != 0) goto error;

		for (i = 0, p_cur = p_response->p_intermediates
				; p_cur != NULL
				; p_cur = p_cur->p_next, i++
		) {
			char *line = p_cur->line;

			err = at_tok_start(&line);
			if (err < 0) goto error;

			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;

			// If we're unregistered, we may just get
			// a "+COPS: 0" response
			if (!at_tok_hasmore(&line)) {
				response[i] = NULL;
				continue;
			}

			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;

			// a "+COPS: 0, n" response is also possible
			if (!at_tok_hasmore(&line)) {
				response[i] = NULL;
				continue;
			}

			err = at_tok_nextstr(&line, &(response[i]));

			if(err>=0){

				memset(cachedCarrier[i],0,128);

				size_t copylen=strlen(response[i]);

				if(copylen<=128){

					strcpy(cachedCarrier[i],response[i]);

				}

				property_set("gsm.operator.alpha", cachedCarrier);



			}else{

				if (err < 0) goto error;

			}

		}

		if (i != 3) {
			/* expect 3 lines exactly */
			goto error;
		}

		LOGD("Cached Carrier: %s, %s, %s",cachedCarrier[0],cachedCarrier[1],cachedCarrier[2]);

		TGM_onRequestComplete(t, TGM_E_SUCCESS, response, sizeof(response));

		at_response_free(p_response);

		return;

	error:

		LOGE("requestOperator must not return error when radio is on");

		TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

		at_response_free(p_response);

	}

	static void requestSignalStrength(void *data, size_t datalen, TGM_Token t)
	{
		ATResponse *p_response = NULL;
		int err;
		int response[2];
		char *line;

		err = at_send_command_singleline("AT+CSQ", "+CSQ:", &p_response);

		if (err < 0 || p_response->success == 0) {
			TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);
			goto error;
		}

		line = p_response->p_intermediates->line;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &(response[0]));
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &(response[1]));
		if (err < 0) goto error;

		TGM_onRequestComplete(t, TGM_E_SUCCESS, response, sizeof(response));

		at_response_free(p_response);
		return;

	error:

		LOGE("requestSignalStrength must never return an error when radio is on");

		TGM_onRequestComplete(t, TGM_E_GENERIC_FAILURE, NULL, 0);

		at_response_free(p_response);

	}

	static void requestResetStack(void *data, size_t datalen, TGM_Token t){

		LOGD("Making fd non-blocking");

		if( fd > 0 ){
			fcntl(fd, F_SETFL, O_NDELAY);
		}

		setRadioState(TGM_STATE_UNAVAILABLE);

		TGM_onRequestComplete(t, TGM_E_SUCCESS, NULL, 0);
	}

	//For pppd authentication


	static void requestSetPPPDAuth(void *data, size_t datalen, TGM_Token t){

		int err;

		const char**  strings = (const char**)data;

		if ( datalen == sizeof(char*) ) {

			//Only username is used
			memset(username,0,32);

			size_t namelen = strlen(strings[0]);

			strcpy(username,strings[0]);

		} else if ( datalen == 2*sizeof(char*) ) {

			//Both username and password are used
			memset(username, 0, 32);

			memset(password, 0, 32);

			size_t namelen = strlen(strings[0]);

			size_t passlen = strlen(strings[1]);

			if( namelen > 0 && namelen < 32 ){

				strcpy(username,strings[0]);

			}

			if( passlen > 0 && passlen < 32 ){

				strcpy(password,strings[1]);

			}

			LOGD("SETTING PPPD AUTH USERNAME: %s, PASSWORD: %s",username, password);

		} else{

		}



	}



    static void pollSIMState (void *param)
    {
        ATResponse *p_response;
        int ret;

        if (sState != TGM_STATE_SIM_NOT_READY) {
            // no longer valid to poll
            return;
        }

        switch(getSIMStatus()) {

            case TGM_SIM_ABSENT:
            case TGM_SIM_PIN:
            case TGM_SIM_PUK:
            case TGM_SIM_NETWORK_PERSONALIZATION:
            default:
                setRadioState(TGM_STATE_SIM_LOCKED_OR_ABSENT);
            return;

            case TGM_SIM_NOT_READY:
                TGM_requestTimedCallback (pollSIMState, NULL, &TIMEVAL_SIMPOLL);
            return;

            case TGM_SIM_READY:
                setRadioState(TGM_STATE_SIM_READY);
            return;
        }
    }




    /** returns one of RIM_SIM_*. Returns RIL_SIM_NOT_READY on error */
   static int getSIMStatus()
   {

	   LOGD("TGM_REQUEST_QUERY_SIM_STATUS");
	   ATResponse *p_response = NULL;
	   int err;
	   int ret;
	   char *cpinLine;
	   char *cpinResult;

	   if (sState == TGM_STATE_OFF || sState == TGM_STATE_UNAVAILABLE) {

		if (isRadioOn() > 0) {
			LOGD("isRadioOn > 0 sState: %d",sState);

			setRadioState (TGM_STATE_SIM_NOT_READY);

		}else{

			LOGD("isRadioOn < 0 sState: %d",sState);

			ret = TGM_SIM_NOT_READY;
			goto done;

		}


	   }

	   err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);

	   if (err != 0) {

		   LOGD("err == %d",err);
		   ret = TGM_SIM_NOT_READY;
		   goto done;
	   }

	   switch (at_get_cme_error(p_response)) {

		   case CME_SUCCESS:
			   LOGD("CME_SUCCESS");
			   break;

		   case CME_SIM_NOT_INSERTED:

			   LOGD("CME_SIM_NOT_INSERTED");
			   ret = TGM_SIM_ABSENT;
			   goto done;

		   case CME_SIM_FAILURE:
			   LOGD("CME_SIM_FAILURE");
			   ret = TGM_SIM_ABSENT;
			   goto done;

		   default:
			   LOGD("Default %d",at_get_cme_error(p_response));
			   ret = TGM_SIM_NOT_READY;
			   goto done;
	   }

	   /* CPIN? has succeeded, now look at the result */

	   cpinLine = p_response->p_intermediates->line;

	   err = at_tok_start (&cpinLine);

	   if (err < 0) {
		   ret = TGM_SIM_NOT_READY;
		   goto done;
	   }

	   err = at_tok_nextstr(&cpinLine, &cpinResult);

	   if (err < 0) {
		   ret = TGM_SIM_NOT_READY;
		   goto done;
	   }

	   if (0 == strcmp (cpinResult, "SIM PIN")) {
		   ret = TGM_SIM_PIN;
		   goto done;
	   } else if (0 == strcmp (cpinResult, "SIM PUK")) {

		   ret = TGM_SIM_PUK;
		   goto done;

	   } else if (0 == strcmp (cpinResult, "PH-NET PIN")) {

		   return TGM_SIM_NETWORK_PERSONALIZATION;

	   } else if (0 != strcmp (cpinResult, "READY"))  {
		   /* we're treating unsupported lock types as "sim absent" */
		   ret = TGM_SIM_ABSENT;
		   goto done;
	   }

	   at_response_free(p_response);
	   p_response = NULL;
	   cpinResult = NULL;

	   ret = TGM_SIM_READY;

   done:
	   at_response_free(p_response);
	   return ret;
   } /** returns one of RIM_SIM_*. Returns RIL_SIM_NOT_READY on error */
