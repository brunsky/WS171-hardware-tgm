/*
 * tgmloop.cpp
 *
 *  Created on: 2009/11/3
 *      Author: stevenlin
 */

#define LOG_TAG "TGMC"

#include <hardware_legacy/power.h>
#include <datanetwork/tgm.h>
#include <cutils/sockets.h>
#include <cutils/jstring.h>
#include <cutils/record_stream.h>
#include <utils/Log.h>
#include <utils/SystemClock.h>
#include <pthread.h>
#include <utils/Parcel.h>
#include <cutils/jstring.h>
#include <cutils/properties.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <alloca.h>
#include <sys/un.h>
#include <assert.h>
#include <netinet/in.h>
#include <tgm_event.h>


namespace android{

	#define DIALER_PROCESS "tgm"

	#define SOCKET_NAME_TGM "tgmd"

	#define MAX_COMMAND_BYTES (8 * 1024)

	#define RESPONSE_SOLICITED 0
	#define RESPONSE_UNSOLICITED 1

	#define PROPERTY_TGM_IMPL "gsm.version.tgm-impl"

	#define ANDROID_WAKE_LOCK_NAME "tgm-interface"

	static const struct timeval TIMEVAL_WAKE_TIMEOUT = {1,0};


	/* Negative values for private RIL errno's */
	#define TGM_ERRNO_INVALID_RESPONSE - 1

	#define NUM_ELEMS(a)     (sizeof (a) / sizeof (a)[0])

	enum WakeType {DONT_WAKE, WAKE_PARTIAL};

	typedef struct {

		int requestNumber;
		void (*dispatchFunction) (Parcel &p, struct RequestInfo *pRI);
		int(*responseFunction) (Parcel &p, void *response, size_t responselen);

	} CommandInfo;

	typedef struct RequestInfo {
		int32_t token;      //this is not RIL_Token
		CommandInfo *pCI;
		struct RequestInfo *p_next;
		char cancelled;
		char local;         // responses to local commands do not go back to command process
	} RequestInfo;

	typedef struct {
		int requestNumber;
		int (*responseFunction) (Parcel &p, void *response, size_t responselen);
		WakeType wakeType;
	} UnsolResponseInfo;

	typedef struct UserCallbackInfo{

		TGM_TimedCallback p_callback;
		void *userParam;
		struct tgm_event event;
		struct UserCallbackInfo *p_next;

	} UserCallbackInfo;

	//
	static void dispatchVoid (Parcel& p, RequestInfo *pRI);
	static void dispatchString (Parcel& p, RequestInfo *pRI);
	static void dispatchStrings (Parcel& p, RequestInfo *pRI);
	static void dispatchInts (Parcel& p, RequestInfo *pRI);
	static void dispatchRaw(Parcel& p, RequestInfo *pRI);

	static int responseInts(Parcel &p, void *response, size_t responselen);
	static int responseStrings(Parcel &p, void *response, size_t responselen);
	static int responseString(Parcel &p, void *response, size_t responselen);
	static int responseVoid(Parcel &p, void *response, size_t responselen);
	static int responseContexts(Parcel &p, void *response, size_t responselen);
	static int responseDialDataNetwork(Parcel &p, void *response, size_t responselen);
	static int responseDisconnectDataNetwork(Parcel &p, void *response, size_t responselen);



	static CommandInfo s_commands[] = {

		{0, NULL, NULL},//none

		{TGM_REQUEST_SET_PDP_CONTEXT, dispatchStrings, responseVoid},
		{TGM_REQUEST_SET_PIN_CODE, dispatchStrings, responseVoid},

		//Specific Command by Camangi
		{TGM_REQUEST_DIAL_DATA_NETWORK, dispatchVoid, responseDialDataNetwork},
		{TGM_REQUEST_DISCONNECT_DATA_NETWORK, dispatchVoid, responseDisconnectDataNetwork},

		{TGM_REQUEST_QUERY_SIGNAL_STRENGTH, dispatchVoid, responseInts},
		{TGM_REQUEST_QUERY_IMEI, dispatchVoid, responseString},
		{TGM_REQUEST_QUERY_MANUFACTURER, dispatchVoid, responseString},
		{TGM_REQUEST_QUERY_OPERATOR,  dispatchVoid, responseStrings},
		{TGM_REQUEST_QUERY_PDP_CONTEXT_LIST, dispatchVoid, responseContexts},

		{TGM_REQUEST_QUERY_MODEM_MODEL, dispatchVoid, responseString},

		{TGM_REQUEST_QUERY_SIM_STATUS,dispatchVoid, responseInts},

		{TGM_REQUEST_QUERY_IMSI, dispatchVoid, responseString},
		{TGM_REQUEST_QUERY_NUMBER,dispatchVoid,responseString},

		{TGM_REQUEST_QUERY_STATE,dispatchVoid, responseInts},
		{TGM_REQUEST_RESET_STACK,dispatchVoid, responseVoid},
		{TGM_REQUEST_SET_PPPD_AUTH,dispatchStrings, responseVoid}

	};


	static UnsolResponseInfo s_unsolResponses[] = {

		{TGM_UNSOLICITED_DEVICE_DETECTED, responseVoid,WAKE_PARTIAL},
		{TGM_UNSOLICITED_DEVICE_REMOVED, responseVoid,WAKE_PARTIAL},
		{TGM_UNSOLICITED_SIGNAL_STRENGTH, responseInts,WAKE_PARTIAL},
		{TGM_UNSOLICITED_RADIO_STATE_CHANGED, responseVoid,WAKE_PARTIAL},
		{TGM_UNSOLICITED_PPPD_CONNECTED, responseVoid,WAKE_PARTIAL},
		{TGM_UNSOLICITED_PPPD_DISCONNECTED, responseVoid,WAKE_PARTIAL},
		{TGM_UNSOLICITED_PPPD_FAILED, responseVoid,WAKE_PARTIAL}


	};

	//callbacks from specific vendor tgm
	TGM_RadioFunctions s_callbacks = {0, NULL, NULL, NULL, NULL, NULL};

	static int s_started = 0;

	//Thread related

	static pthread_t s_tid_dispatch;
	static pthread_t s_tid_reader;

	static UserCallbackInfo *s_last_wake_timeout_info = NULL;

	static pthread_mutex_t s_pendingRequestsMutex = PTHREAD_MUTEX_INITIALIZER;

	static pthread_mutex_t s_writeMutex = PTHREAD_MUTEX_INITIALIZER;

	static pthread_mutex_t s_startupMutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t s_startupCond = PTHREAD_COND_INITIALIZER;

	static pthread_mutex_t s_dispatchMutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t s_dispatchCond = PTHREAD_COND_INITIALIZER;

	static int s_registerCalled = 0;//register function call time

	static struct tgm_event s_commands_event;
	static struct tgm_event s_wakeupfd_event;
	static struct tgm_event s_listen_event;
	static struct tgm_event s_wake_timeout_event;

	static RequestInfo *s_pendingRequests = NULL;


	//File description definition

	static int s_fdWakeupRead;
	static int s_fdWakeupWrite;

	static int s_fdListen = -1;
	static int s_fdCommand = -1;

	static void *s_lastNITZTimeData = NULL;
	static size_t s_lastNITZTimeDataSize;

	//static int sock_fd = -1;


	//Method definition

	extern "C" void TGM_startEventLoop(void);

	extern "C" void TGM_register (const TGM_RadioFunctions *callbacks);

	extern "C" void TGM_onRequestComplete(TGM_Token t, TGM_Errno e, void *response, size_t responselen);

	extern "C" void TGM_onUnsolicitedResponse(int unsolResponse, void *data,size_t datalen);

	extern "C" void TGM_requestTimedCallback(TGM_TimedCallback callback, void *param, const struct timeval *relativeTime);

	static void *eventLoop(void *param);

	static void triggerEvLoop();

	static void listenCallback (int fd, short flags, void *param);

	static void tgmEventAddWakeup(struct tgm_event *ev);

	static void writeStringToParcel(Parcel &p, const char *s);

	static void invalidCommandBlock (RequestInfo *pRI);

	static void processCommandsCallback(int fd, short flags, void *param);

	static void processWakeupCallback(int fd, short flags, void *param);

	static int processCommandBuffer(void *buffer, size_t buflen);

	static int sendResponseRaw (const void *data, size_t dataSize);

	static int sendResponse (Parcel &p);

	static int blockingWrite(int fd, const void *buffer, size_t len);

	static void onNewCommandConnect();

	static void onCommandsSocketClosed();

	static char *strdupReadString(Parcel &p);

	const char *requestToString(int request);

	static void releaseWakeLock();

	static void grabPartialWakeLock();

	static int checkAndDequeueRequestInfo(struct RequestInfo *pRI);

	static UserCallbackInfo * internalRequestTimedCallback (TGM_TimedCallback callback, void *param,
		                                const struct timeval *relativeTime);

	static void wakeTimeoutCallback (void *param);

	static void userTimerCallback (int fd, short flags, void *param);




	//Will be called from tgmd

	extern "C" void TGM_register (const TGM_RadioFunctions *callbacks) //TGM Radio Functions derived from libxxxx-ril.so
	{

		LOGE("---TGM_register---");

		int ret;

		int flags;

		if (callbacks == NULL || ! (callbacks->version == TGM_VERSION || callbacks->version == 1))
		{
			LOGE("TGM_register: TGM_RadioFunctions * null or invalid version"
				 " (expected %d)", TGM_VERSION);
			return;
		}

		if (s_registerCalled > 0) {

			LOGE("TGM_register has been called more than once. "
				 "Subsequent call ignored");
			return;
		}

		memcpy(&s_callbacks, callbacks, sizeof (TGM_RadioFunctions));

		s_registerCalled = 1;

		// Little self-check // making suret that all commands are in sequence...

		for (int i = 0; i < (int)NUM_ELEMS(s_commands) ; i++) {

			assert(i == s_commands[i].requestNumber);

		}

		//make sure all unoslResponse are in order..
		for (int i = 0; i < (int)NUM_ELEMS(s_unsolResponses) ; i++) {

			assert(i + TGM_UNSOL_RESPONSE_BASE
				   == s_unsolResponses[i].requestNumber);

		}

		// New rild impl calls TGM_startEventLoop() first
		// old standalone impl wants it here.

		if (s_started == 0) {

			LOGD("Enter TGM_startEventLoop()");

			TGM_startEventLoop();

			LOGD("Leave TGM_startEventLoop()");

		}

		// start listen socket

#if 1
		//ret = socket_local_server (SOCKET_NAME_TGM,ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

#define TMPSOCK "/tmp/tgmd"

		ret = socket_local_server (TMPSOCK,ANDROID_SOCKET_NAMESPACE_FILESYSTEM, SOCK_STREAM);

		if (ret < 0) {

			LOGE("Unable to bind socket errno:%d", errno);
			exit (-1);

		}

		LOGE("bind socket succesfully");
		s_fdListen = ret;

#else
		s_fdListen = android_get_control_socket(SOCKET_NAME_TGM);

		if (s_fdListen < 0) {

			LOGE("Failed to get socket '" SOCKET_NAME_TGM "'");

			exit(-1);
		}

		ret = listen(s_fdListen, 4);

		if (ret < 0) {

			LOGE("Failed to listen on control socket '%d': %s",s_fdListen, strerror(errno));
			exit(-1);

		}
#endif

		/* note: non-persistent so we can accept only one connection at a time */

		tgm_event_set (&s_listen_event, s_fdListen, false, listenCallback, NULL);

		tgmEventAddWakeup (&s_listen_event);

#if 1
		// start debug interface socket

//		s_fdDebug = android_get_control_socket(SOCKET_NAME_RIL_DEBUG);
//
//		if (s_fdDebug < 0) {
//
//			LOGE("Failed to get socket '" SOCKET_NAME_RIL_DEBUG "' errno:%d", errno);
//			exit(-1);
//
//		}
//
//		ret = listen(s_fdDebug, 4);
//
//		if (ret < 0) {
//			LOGE("Failed to listen on tgm debug socket '%d': %s",
//				 s_fdDebug, strerror(errno));
//			exit(-1);
//		}
//
//		tgm_event_set (&s_debug_event, s_fdDebug, true,
//					   debugCallback, NULL);
//
//		tgmEventAddWakeup (&s_debug_event);

#endif

	}


	extern "C" void TGM_startEventLoop(void)
	{

		int ret;

		pthread_attr_t attr;

		/* spin up eventLoop thread and wait for it to get started */
		s_started = 0;

		//Mutex protect only one thread will start the event loop
		pthread_mutex_lock(&s_startupMutex);

		pthread_attr_init (&attr);

		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		//Create another thread to run the event loop
		ret = pthread_create(&s_tid_dispatch, &attr, eventLoop, NULL);

		while (s_started == 0) {

			//wait until eventloop ready to take off
			pthread_cond_wait(&s_startupCond, &s_startupMutex);

		}

		pthread_mutex_unlock(&s_startupMutex);

		if (ret < 0) {
			LOGE("Failed to create dispatch thread errno:%d", errno);
			return;
		}
	}



	extern "C" void TGM_onRequestComplete(TGM_Token t, TGM_Errno e, void *response, size_t responselen)
	{

		LOGE("TGM_onRequestComplete");

		RequestInfo *pRI;
		int ret;
		size_t errorOffset;

		pRI = (RequestInfo *)t;

		if (!checkAndDequeueRequestInfo(pRI)) {
			LOGE ("TGM_onRequestComplete: invalid TGM_Token");
			return;
		}

		if (pRI->local > 0) {
			// Locally issued command...void only!
			// response does not go back up the command socket
			LOGD("C[locl]< %s", requestToString(pRI->pCI->requestNumber));

			goto done;
		}


		if (pRI->cancelled == 0) {
			Parcel p;

			p.writeInt32 (RESPONSE_SOLICITED);
			p.writeInt32 (pRI->token);
			errorOffset = p.dataPosition();

			p.writeInt32 (e);

			if (e == TGM_E_SUCCESS) {
				/* process response on success */
				ret = pRI->pCI->responseFunction(p, response, responselen);

				/* if an error occurred, rewind and mark it */
				if (ret != 0) {
					p.setDataPosition(errorOffset);
					p.writeInt32 (ret);
				}
			} else {
			}

			if (s_fdCommand < 0) {

				LOGD ("TGM onRequestComplete: Command channel closed");

			}

			sendResponse(p);
		}

	done:
		free(pRI);
	}

	extern "C" void TGM_onUnsolicitedResponse(int unsolResponse, void *data, size_t datalen)
	{


		LOGD("Receive Unsolicited Response: %d",unsolResponse);

		int unsolResponseIndex;
		int ret;


		bool shouldScheduleTimeout = false;

		if (s_registerCalled == 0) {
			// Ignore RIL_onUnsolicitedResponse before TGM_register
			LOGW("TGM_onUnsolicitedResponse called before TGM_register");
			return;
		}

		unsolResponseIndex = unsolResponse - TGM_UNSOLICITED_BASE;

		if ((unsolResponseIndex < 0)
			|| (unsolResponseIndex >= (int32_t)NUM_ELEMS(s_unsolResponses))) {
			LOGE("unsupported unsolicited response code %d", unsolResponse);
			return;
		}

		// Grab a wake lock if needed for this reponse,
		// as we exit we'll either release it immediately
		// or set a timer to release it later.
		switch (s_unsolResponses[unsolResponseIndex].wakeType) {
			case WAKE_PARTIAL:
				grabPartialWakeLock();
				shouldScheduleTimeout = true;
			break;

			case DONT_WAKE:
			default:
				// No wake lock is grabed so don't set timeout
				shouldScheduleTimeout = false;
				break;
		}



		Parcel p;

		p.writeInt32 (RESPONSE_UNSOLICITED);
		p.writeInt32 (unsolResponse);

		ret = s_unsolResponses[unsolResponseIndex].responseFunction(p, data, datalen);

		if (ret != 0) {
			// Problem with the response. Don't continue;
			goto error_exit;
		}

		// some things get more payload

		switch(unsolResponse) {

			case TGM_UNSOLICITED_RADIO_STATE_CHANGED:

				p.writeInt32(s_callbacks.onStateRequest());

			break;

		}

		ret = sendResponse(p);


		// For now, we automatically go back to sleep after TIMEVAL_WAKE_TIMEOUT
		// FIXME The java code should handshake here to release wake lock

		if (shouldScheduleTimeout) {
			// Cancel the previous request
			if (s_last_wake_timeout_info != NULL) {
				s_last_wake_timeout_info->userParam = (void *)1;
			}

			s_last_wake_timeout_info = internalRequestTimedCallback(wakeTimeoutCallback, NULL,
												&TIMEVAL_WAKE_TIMEOUT);
		}

		// Normal exit
		return;

	error_exit:
		// There was an error and we've got the wake lock so release it.
		if (shouldScheduleTimeout) {
			releaseWakeLock();
		}

	}

	extern "C" void TGM_requestTimedCallback (TGM_TimedCallback callback, void *param, const struct timeval *relativeTime)
	{

		LOGE("TGM_requestTimedCallback");
		internalRequestTimedCallback (callback, param, relativeTime);
	}


	//This will run by thead created by TGM_startEventLoop
	static void *eventLoop(void *param)
	{
		int ret;
		int filedes[2];

		tgm_event_init();

		pthread_mutex_lock(&s_startupMutex);

		s_started = 1;

		pthread_cond_broadcast(&s_startupCond);

		pthread_mutex_unlock(&s_startupMutex);

		ret = pipe(filedes);

		if (ret < 0) {

			LOGE("Error in pipe() errno:%d", errno);
			return NULL;

		}

		s_fdWakeupRead = filedes[0];
		s_fdWakeupWrite = filedes[1];

		fcntl(s_fdWakeupRead, F_SETFL, O_NONBLOCK);

		//Initial a wakeup event, processWakeupCallback will be called when it is waked up
		tgm_event_set (&s_wakeupfd_event, s_fdWakeupRead, true, processWakeupCallback, NULL);

		tgmEventAddWakeup (&s_wakeupfd_event);

		// Only returns on error
		tgm_event_loop();

		LOGE ("error in event_loop_base errno:%d", errno);

		return NULL;
	}


	static void tgmEventAddWakeup(struct tgm_event *ev)
	{
		tgm_event_add(ev);

		triggerEvLoop();
	}

	static void triggerEvLoop()
	{
		int ret;

		if (!pthread_equal(pthread_self(), s_tid_dispatch)) {

			/* trigger event loop to wakeup. No reason to do this,
			 * if we're in the event loop thread */

			do {
				ret = write (s_fdWakeupWrite, " ", 1);

			} while (ret < 0 && errno == EINTR);
		}
	}


	static void dispatchVoid (Parcel& p, RequestInfo *pRI)
	{

		LOGE("dispatchVoid request Num: %d",pRI->pCI->requestNumber);
		//clearPrintBuf;
		//printRequest(pRI->token, pRI->pCI->requestNumber);

		//callback from libxxx_ril.so, request ril command to it
		s_callbacks.onRequest(pRI->pCI->requestNumber, NULL, 0, pRI);
	}

	/** Callee expects const char * */
	static void dispatchString (Parcel& p, RequestInfo *pRI)
	{
		status_t status;
		size_t datalen;
		size_t stringlen;
		char *string8 = NULL;

		string8 = strdupReadString(p);

		s_callbacks.onRequest(pRI->pCI->requestNumber, string8,
							  sizeof(char *), pRI);

#ifdef MEMSET_FREED
		memsetString(string8);
#endif

		free(string8);
		return;
	invalid:
		invalidCommandBlock(pRI);
		return;
	}



	/** Callee expects const char ** */
	static void dispatchStrings (Parcel &p, RequestInfo *pRI)
	{
		int32_t countStrings;
		status_t status;
		size_t datalen;
		char **pStrings;

		status = p.readInt32 (&countStrings);

		if (status != NO_ERROR) {
			goto invalid;
		}

		if (countStrings == 0) {
			// just some non-null pointer
			pStrings = (char **)alloca(sizeof(char *));
			datalen = 0;
		} else if (((int)countStrings) == -1) {
			pStrings = NULL;
			datalen = 0;
		} else {
			datalen = sizeof(char *) * countStrings;

			pStrings = (char **)alloca(datalen);

			for (int i = 0 ; i < countStrings ; i++) {
				pStrings[i] = strdupReadString(p);
				//appendPrintBuf("%s%s,", printBuf, pStrings[i]);
			}
		}

		//printRequest(pRI->token, pRI->pCI->requestNumber);

		s_callbacks.onRequest(pRI->pCI->requestNumber, pStrings, datalen, pRI);

		if (pStrings != NULL) {
			for (int i = 0 ; i < countStrings ; i++) {
#ifdef MEMSET_FREED
				memsetString (pStrings[i]);
#endif
				free(pStrings[i]);
			}

#ifdef MEMSET_FREED
			memset(pStrings, 0, datalen);
#endif
		}

		return;
	invalid:
		invalidCommandBlock(pRI);
		return;
	}

	/** Callee expects const int * */
	static void dispatchInts (Parcel &p, RequestInfo *pRI)
	{
		int32_t count;
		status_t status;
		size_t datalen;
		int *pInts;

		status = p.readInt32 (&count);

		if (status != NO_ERROR || count == 0) {
			goto invalid;
		}

		datalen = sizeof(int) * count;
		pInts = (int *)alloca(datalen);

		for (int i = 0 ; i < count ; i++) {
			int32_t t;

			status = p.readInt32(&t);
			pInts[i] = (int)t;


			if (status != NO_ERROR) {
				goto invalid;
			}
		}

		s_callbacks.onRequest(pRI->pCI->requestNumber, const_cast<int *>(pInts),
							  datalen, pRI);

#ifdef MEMSET_FREED
		memset(pInts, 0, datalen);
#endif

		return;
	invalid:
		invalidCommandBlock(pRI);
		return;
	}

	static void dispatchRaw(Parcel &p, RequestInfo *pRI)
	{
		int32_t len;
		status_t status;
		const void *data;

		status = p.readInt32(&len);

		if (status != NO_ERROR) {
			goto invalid;
		}

		// The java code writes -1 for null arrays
		if (((int)len) == -1) {
			data = NULL;
			len = 0;
		}

		data = p.readInplace(len);



		s_callbacks.onRequest(pRI->pCI->requestNumber, const_cast<void *>(data), len, pRI);

		return;
	invalid:
		invalidCommandBlock(pRI);
		return;
	}


	static int responseInts(Parcel &p, void *response, size_t responselen)
	{
		int numInts;

		if (response == NULL && responselen != 0) {
			LOGE("invalid response: NULL");
			return TGM_ERRNO_INVALID_RESPONSE;
		}
		if (responselen % sizeof(int) != 0) {
			LOGE("invalid response length %d expected multiple of %d\n",
				 (int)responselen, (int)sizeof(int));
			return TGM_ERRNO_INVALID_RESPONSE;
		}

		int *p_int = (int *) response;

		numInts = responselen / sizeof(int *);
		p.writeInt32 (numInts);

		/* each int*/
		for (int i = 0 ; i < numInts ; i++) {

			p.writeInt32(p_int[i]);
		}


		return 0;
	}

	/** response is a char **, pointing to an array of char *'s */
	static int responseStrings(Parcel &p, void *response, size_t responselen)
	{
		int numStrings;

		if (response == NULL && responselen != 0) {
			LOGE("invalid response: NULL");
			return TGM_ERRNO_INVALID_RESPONSE;
		}
		if (responselen % sizeof(char *) != 0) {
			LOGE("invalid response length %d expected multiple of %d\n",
				 (int)responselen, (int)sizeof(char *));
			return TGM_ERRNO_INVALID_RESPONSE;
		}

		if (response == NULL) {
			p.writeInt32 (0);
		} else {
			char **p_cur = (char **) response;

			numStrings = responselen / sizeof(char *);
			p.writeInt32 (numStrings);

			/* each string*/

			for (int i = 0 ; i < numStrings ; i++) {

				writeStringToParcel (p, p_cur[i]);
			}


		}
		return 0;
	}


	/**
	 * NULL strings are accepted
	 * FIXME currently ignores responselen
	 */
	static int responseString(Parcel &p, void *response, size_t responselen)
	{
		/* one string only */

		//appendPrintBuf("%s%s", printBuf, (char*)response);

		writeStringToParcel(p, (const char *)response);

		return 0;
	}

	static int responseVoid(Parcel &p, void *response, size_t responselen)
	{

		return 0;
	}

	static int responseContexts(Parcel &p, void *response, size_t responselen)
	{
		if (response == NULL && responselen != 0) {

			LOGE("invalid response: NULL");

			return TGM_ERRNO_INVALID_RESPONSE;

		}

		if (responselen % sizeof(TGM_PDP_Context_Response) != 0) {

			LOGE("invalid response length %d expected multiple of %d",
				 (int)responselen, (int)sizeof(TGM_PDP_Context_Response));

			return TGM_ERRNO_INVALID_RESPONSE;
		}

		int num = responselen / sizeof(TGM_PDP_Context_Response);
		p.writeInt32(num);

		TGM_PDP_Context_Response *p_cur = (TGM_PDP_Context_Response *) response;

		int i;

		for (i = 0; i < num; i++) {
			p.writeInt32(p_cur[i].cid);
			p.writeInt32(p_cur[i].active);
			writeStringToParcel(p, p_cur[i].type);
			writeStringToParcel(p, p_cur[i].apn);
			writeStringToParcel(p, p_cur[i].address);

		}

		return 0;
	}

	static int responseRaw(Parcel &p, void *response, size_t responselen)
	{
		if (response == NULL && responselen != 0) {
			LOGE("invalid response: NULL with responselen != 0");
			return TGM_ERRNO_INVALID_RESPONSE;
		}

		// The java code reads -1 size as null byte array
		if (response == NULL) {
			p.writeInt32(-1);
		} else {
			p.writeInt32(responselen);
			p.write(response, responselen);
		}

		return 0;
	}

	static int responseDialDataNetwork(Parcel &p, void *response, size_t responselen){


		return 0;

	}

	static int responseDisconnectDataNetwork(Parcel &p, void *response, size_t responselen){


		return 0;

	}

	static void writeStringToParcel(Parcel &p, const char *s)
	{
		char16_t *s16;
		size_t s16_len;
		s16 = strdup8to16(s, &s16_len);
		p.writeString16(s16, s16_len);
		free(s16);
	}

	static void invalidCommandBlock (RequestInfo *pRI)
	{
		LOGE("invalid command block for token %d request %s",
			 pRI->token, requestToString(pRI->pCI->requestNumber));
	}

	static char *strdupReadString(Parcel &p)
	{
		size_t stringlen;
		const char16_t *s16;

		s16 = p.readString16Inplace(&stringlen);

		return strndup16to8(s16, stringlen);
	}

	const char *requestToString(int request){

		return "TEST NOW";

	}

	static void listenCallback (int fd, short flags, void *param)
	{

		LOGE("Enter Listen CallBack");

		int ret;
		int err;
		int is_phone_socket;
		RecordStream *p_rs;

		struct sockaddr_un peeraddr;
		socklen_t socklen = sizeof (peeraddr);

		struct ucred creds;
		socklen_t szCreds = sizeof(creds);

		struct passwd *pwd = NULL;

		assert (s_fdCommand < 0);
		assert (fd == s_fdListen);

		s_fdCommand = accept(s_fdListen, (sockaddr *) &peeraddr, &socklen); //will this block until there is a message?

		if (s_fdCommand < 0 ) {
			LOGE("Error on accept() errno:%d", errno);
			/* start listening for new connections again */
			tgmEventAddWakeup(&s_listen_event);
			return;
		}

//		/* check the credential of the other side and only accept socket from
//		 * phone process
//		 */
//		errno = 0;
//		is_phone_socket = 0;
//
//		err = getsockopt(s_fdCommand, SOL_SOCKET, SO_PEERCRED, &creds, &szCreds);
//
//		if (err == 0 && szCreds > 0) {
//
//			errno = 0;
//
//			pwd = getpwuid(creds.uid);
//
//			if (pwd != NULL) {
//				if (strcmp(pwd->pw_name, DIALER_PROCESS) == 0) {
//					is_phone_socket = 1;
//				} else {
//					LOGE("TGMD can't accept socket from process %s", pwd->pw_name);
//				}
//			} else {
//				LOGE("Error on getpwuid() errno: %d", errno);
//			}
//
//		} else {
//			LOGD("Error on getsockopt() errno: %d", errno);
//		}
//
//		if ( !is_phone_socket ) {
//
//			LOGE("TGMD must accept socket from %s", DIALER_PROCESS);
//
//			close(s_fdCommand);
//
//			s_fdCommand = -1;
//
//			onCommandsSocketClosed();
//
//			/* start listening for new connections again */
//			tgmEventAddWakeup(&s_listen_event);
//
//			return;
//		}

		ret = fcntl(s_fdCommand, F_SETFL, O_NONBLOCK);

		if (ret < 0) {
			LOGE ("Error setting O_NONBLOCK errno:%d", errno);
		}

		LOGI("libtgm: new connection");

		p_rs = record_stream_new(s_fdCommand, MAX_COMMAND_BYTES);

		tgm_event_set (&s_commands_event, s_fdCommand, 1,
					   processCommandsCallback, p_rs);

		tgmEventAddWakeup (&s_commands_event);

		onNewCommandConnect();
	}

	static int processCommandBuffer(void *buffer, size_t buflen)
	{

		LOGE("processCommandBuffer");
		Parcel p;
		status_t status;
		int32_t request;
		int32_t token;
		RequestInfo *pRI;
		int ret;

		p.setData((uint8_t *) buffer, buflen);

		// status checked at end
		status = p.readInt32(&request);
		status = p.readInt32 (&token);

		if (status != NO_ERROR) {
			LOGE("invalid request block");
			return 0;
		}

		if (request < 1 || request >= (int32_t)NUM_ELEMS(s_commands)) {
			LOGE("unsupported request code %d token %d", request, token);
			// FIXME this should perhaps return a response
			return 0;
		}


		pRI = (RequestInfo *)calloc(1, sizeof(RequestInfo));

		pRI->token = token;
		pRI->pCI = &(s_commands[request]);

		ret = pthread_mutex_lock(&s_pendingRequestsMutex);
		assert (ret == 0);

		pRI->p_next = s_pendingRequests;
		s_pendingRequests = pRI;

		ret = pthread_mutex_unlock(&s_pendingRequestsMutex);
		assert (ret == 0);

		/*    sLastDispatchedToken = token; */

		pRI->pCI->dispatchFunction(p, pRI);

		return 0;
	}

	static void processWakeupCallback(int fd, short flags, void *param)
	{
		char buff[16];
		int ret;

		LOGE("processWakeupCallback");

		/* empty our wakeup socket out */
		do {
			ret = read(s_fdWakeupRead, &buff, sizeof(buff));
		} while (ret > 0 || (ret < 0 && errno == EINTR));
	}

	static void processCommandsCallback(int fd, short flags, void *param)
	{

		LOGE("processCommandsCallback");

		RecordStream *p_rs;
		void *p_record;
		size_t recordlen;
		int ret;

		assert(fd == s_fdCommand);

		p_rs = (RecordStream *)param;

		for (;;) {
			/* loop until EAGAIN/EINTR, end of stream, or other error */
			ret = record_stream_get_next(p_rs, &p_record, &recordlen);

			if (ret == 0 && p_record == NULL) {
				/* end-of-stream */
				break;
			} else if (ret < 0) {
				break;
			} else if (ret == 0) { /* && p_record != NULL */
				processCommandBuffer(p_record, recordlen);
			}
		}

		if (ret == 0 || !(errno == EAGAIN || errno == EINTR)) {
			/* fatal error or end-of-stream */
			if (ret != 0) {

				LOGE("error on reading command socket errno:%d\n", errno);

			} else {

				LOGW("EOS.  Closing command socket.");

			}

			close(s_fdCommand);

			//MODIFY
			//exit(1);

			s_fdCommand = -1;

			tgm_event_del(&s_commands_event);

			record_stream_free(p_rs);

			/* start listening for new connections again */
			tgmEventAddWakeup(&s_listen_event);

			onCommandsSocketClosed();
		}
	}



	static void onNewCommandConnect()
	{

		LOGE("onNewCommandConnect");
		// implicit radio state changed
		TGM_onUnsolicitedResponse(TGM_UNSOLICITED_RADIO_STATE_CHANGED, NULL, 0);

		// Send last NITZ time data, in case it was missed
//		if (s_lastNITZTimeData != NULL) {
//
//			sendResponseRaw(s_lastNITZTimeData, s_lastNITZTimeDataSize);
//
//			free(s_lastNITZTimeData);
//
//			s_lastNITZTimeData = NULL;
//		}

		// Get version string
		if (s_callbacks.getVersion != NULL) {
			const char *version;
			version = s_callbacks.getVersion();
			LOGI("TGM Daemon version: %s\n", version);

			property_set(PROPERTY_TGM_IMPL, version);

		} else {
			LOGI("TGM Daemon version: unavailable\n");
			property_set(PROPERTY_TGM_IMPL, "unavailable");
		}

	}

	static void onCommandsSocketClosed()
	{

		LOGE("onCommandsSocketClosed()");
		int ret;
		RequestInfo *p_cur;

		/* mark pending requests as "cancelled" so we dont report responses */

		ret = pthread_mutex_lock(&s_pendingRequestsMutex);
		assert (ret == 0);

		p_cur = s_pendingRequests;

		for (p_cur = s_pendingRequests
			 ; p_cur != NULL
			 ; p_cur  = p_cur->p_next
			 ) {
			p_cur->cancelled = 1;
		}

		ret = pthread_mutex_unlock(&s_pendingRequestsMutex);
		assert (ret == 0);
	}

	static int sendResponseRaw (const void *data, size_t dataSize)
	{
		int fd = s_fdCommand;
		int ret;
		uint32_t header;

		if (s_fdCommand < 0) {
			return -1;
		}

		if (dataSize > MAX_COMMAND_BYTES) {

			LOGE("TGM: packet larger than %u (%u)", MAX_COMMAND_BYTES, (unsigned int )dataSize);

			return -1;
		}


		// FIXME is blocking here ok? issue #550970

		pthread_mutex_lock(&s_writeMutex);

		header = htonl(dataSize);

		ret = blockingWrite(fd, (void *)&header, sizeof(header));

		if (ret < 0) {

			return ret;

		}

		blockingWrite(fd, data, dataSize);

		if (ret < 0) {
			return ret;
		}

		pthread_mutex_unlock(&s_writeMutex);

		return 0;
	}

	static int sendResponse (Parcel &p)
	{

		return sendResponseRaw(p.data(), p.dataSize());
	}



	static int blockingWrite(int fd, const void *buffer, size_t len)
	{
		size_t writeOffset = 0;
		const uint8_t *toWrite;

		toWrite = (const uint8_t *)buffer;

		while (writeOffset < len) {

			ssize_t written;
			do {
				written = write (fd, toWrite + writeOffset,
								 len - writeOffset);
			} while (written < 0 && errno == EINTR);

			if (written >= 0) {

				writeOffset += written;

			} else {   // written < 0

				LOGE ("TGM Response: unexpected error on write errno:%d", errno);

				close(fd);

				//TODO should we start listen again? //MODIFY
				//tgm_event_set (&s_listen_event, s_fdListen, false, listenCallback, NULL);

				LOGE ("Writing to socket failed, reset stack");

				exit(1);

				return -1;
			}
		}

		return 0;
	}



	static void releaseWakeLock()
	{
	    release_wake_lock(ANDROID_WAKE_LOCK_NAME);
	}

	static void grabPartialWakeLock()
	{
		acquire_wake_lock(PARTIAL_WAKE_LOCK, ANDROID_WAKE_LOCK_NAME);
	}



	static int checkAndDequeueRequestInfo(struct RequestInfo *pRI)
	{
	    int ret = 0;

	    if (pRI == NULL) {
	        return 0;
	    }

	    pthread_mutex_lock(&s_pendingRequestsMutex);

	    for(RequestInfo **ppCur = &s_pendingRequests
	        ; *ppCur != NULL
	        ; ppCur = &((*ppCur)->p_next)
	    ) {
	        if (pRI == *ppCur) {
	            ret = 1;

	            *ppCur = (*ppCur)->p_next;
	            break;
	        }
	    }

	    pthread_mutex_unlock(&s_pendingRequestsMutex);

	    return ret;
	}

	static UserCallbackInfo * internalRequestTimedCallback (TGM_TimedCallback callback, void *param,
	                                const struct timeval *relativeTime)
	{
		LOGE("internalRequestTimedCallback ");
	    struct timeval myRelativeTime;
	    UserCallbackInfo *p_info;

	    p_info = (UserCallbackInfo *) malloc (sizeof(UserCallbackInfo));

	    p_info->p_callback = callback;
	    p_info->userParam = param;

	    if (relativeTime == NULL) {
	        /* treat null parameter as a 0 relative time */
	        memset (&myRelativeTime, 0, sizeof(myRelativeTime));

	    } else {
	        /* FIXME I think event_add's tv param is really const anyway */
	        memcpy (&myRelativeTime, relativeTime, sizeof(myRelativeTime));
	    }

	    tgm_event_set(&(p_info->event), -1, false, userTimerCallback, p_info);

	    tgm_timer_add(&(p_info->event), &myRelativeTime);

	    triggerEvLoop();
	    return p_info;
	}

	static void wakeTimeoutCallback (void *param)
	{
		// We're using "param != NULL" as a cancellation mechanism
		if (param == NULL) {
			//LOGD("wakeTimeout: releasing wake lock");

			releaseWakeLock();

		} else {
			//LOGD("wakeTimeout: releasing wake lock CANCELLED");
		}
	}

	static void userTimerCallback (int fd, short flags, void *param)
	{

		UserCallbackInfo *p_info;

		p_info = (UserCallbackInfo *)param;

		p_info->p_callback(p_info->userParam);


		// FIXME generalize this...there should be a cancel mechanism
		if (s_last_wake_timeout_info != NULL && s_last_wake_timeout_info == p_info) {
			s_last_wake_timeout_info = NULL;
		}

		free(p_info);

	}


}//end of namespace android
