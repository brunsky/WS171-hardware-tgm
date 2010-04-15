/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * ISSUES:
 * - SMS retransmit (specifying TP-Message-ID)
 *
 */

/**
 * TODO
 *
 * Supp Service Notification (+CSSN)
 * GPRS PDP context deactivate notification
 *  
 */


#ifndef ANDROID_RIL_H 
#define ANDROID_RIL_H 1

#include <stdlib.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TGM_VERSION 1

typedef void * TGM_Token;

typedef enum {
    TGM_E_SUCCESS = 0,
    TGM_E_RADIO_NOT_AVAILABLE = 1,     /* If radio did not start or is resetting */
    TGM_E_GENERIC_FAILURE = 2,
    TGM_E_PASSWORD_INCORRECT = 3,      /* for PIN/PIN2 methods only! */
    TGM_E_SIM_PIN2 = 4,                /* Operation requires SIM PIN2 to be entered */
    TGM_E_SIM_PUK2 = 5,                /* Operation requires SIM PIN2 to be entered */
    TGM_E_REQUEST_NOT_SUPPORTED = 6,
    TGM_E_CANCELLED = 7,
    TGM_E_OP_NOT_ALLOWED_DURING_VOICE_CALL = 8, /* data ops are not allowed during voice
                                                   call on a Class C GPRS device */
    TGM_E_OP_NOT_ALLOWED_BEFORE_REG_TO_NW = 9,  /* data ops are not allowed before device
                                                   registers in network */

    TGM_E_SMS_SEND_FAIL_RETRY = 10,		/* fail to send sms and need retry */

    TGM_E_OP_NOT_ALLOWED_DURING_PPPD = 11
} TGM_Errno;

typedef enum {
    TGM_CALL_ACTIVE = 0,
    TGM_CALL_HOLDING = 1,
    TGM_CALL_DIALING = 2,    /* MO call only */
    TGM_CALL_ALERTING = 3,   /* MO call only */
    TGM_CALL_INCOMING = 4,   /* MT call only */
    TGM_CALL_WAITING = 5     /* MT call only */
} TGM_CallState;

typedef enum {
    TGM_STATE_OFF = 0,          /* Radio explictly powered off (eg CFUN=0) */
    TGM_STATE_UNAVAILABLE = 1,  /* Radio unavailable (eg, resetting or not booted) */
    TGM_STATE_SIM_NOT_READY = 2,      /* Radio is on, but the SIM interface is not ready */
    TGM_STATE_SIM_LOCKED_OR_ABSENT = 3, /* SIM PIN locked, PUK required, network
                               personalization locked, or SIM absent */
    TGM_STATE_SIM_READY = 4,          /* Radio is on and SIM interface is available */
    TGM_STATE_PPPD = 5,			/* Now the control is handover to pppd*/
    TGM_STATE_PPPD_READY = 6
} TGM_RadioState;

typedef struct {
    int             cid;        /* Context ID */
    int             active;     /* nonzero if context is active */
    char *          type;       /* X.25, IP, IPV6, etc. */
    char *          apn;
    char *          address;
} TGM_PDP_Context_Response;

/* See RIL_REQUEST_LAST_PDP_FAIL_CAUSE */
typedef enum {
    TGM_FAIL_BARRED = 8,         /* no retry; prompt user */
    TGM_FAIL_BAD_APN = 27,       /* no retry; prompt user */
    TGM_FAIL_USER_AUTHENTICATION = 29, /* no retry; prompt user */
    TGM_FAIL_SERVICE_OPTION_NOT_SUPPORTED = 32,  /*no retry; prompt user */
    TGM_FAIL_SERVICE_OPTION_NOT_SUBSCRIBED = 33, /*no retry; prompt user */
    TGM_FAIL_ERROR_UNSPECIFIED = 0xffff  /* This and all other cases: retry silently */
} TGM_LastPDPActivateFailCause;

/* Used by RIL_UNSOL_SUPP_SVC_NOTIFICATION */
typedef struct {
    int     notificationType;   /*
                                 * 0 = MO intermediate result code
                                 * 1 = MT unsolicited result code
                                 */
    int     code;               /* See 27.007 7.17
                                   "code1" for MO
                                   "code2" for MT. */
    int     index;              /* CUG index. See 27.007 7.17. */
    int     type;               /* "type" from 27.007 7.17 (MT only). */
    char *  number;             /* "number" from 27.007 7.17
                                   (MT only, may be NULL). */
} TGM_SuppSvcNotification;

/* see RIL_REQUEST_GET_SIM_STATUS */
#define TGM_SIM_ABSENT      		0
#define TGM_SIM_NOT_READY   		1
/* RIL_SIM_READY means that the radio state is RADIO_STATE_SIM_READY. 
 * This is more
 * than "+CPIN: READY". It also means the radio is ready for SIM I/O
 */
#define TGM_SIM_READY       		2
#define TGM_SIM_PIN         		3
#define TGM_SIM_PUK         		4
#define TGM_SIM_NETWORK_PERSONALIZATION 5

/* The result of a SIM refresh, returned in data[0] of RIL_UNSOL_SIM_REFRESH */
typedef enum {
    /* A file on SIM has been updated.  data[1] contains the EFID. */
    SIM_FILE_UPDATE = 0,
    /* SIM initialized.  All files should be re-read. */
    SIM_INIT = 1,
    /* SIM reset.  SIM power required, SIM may be locked and all files should be re-read. */
    SIM_RESET = 2
} TGM_SimRefreshResult;

/* No restriction at all including voice/SMS/USSD/SS/AV64 and packet data. */
#define TGM_RESTRICTED_STATE_NONE           0x00
/* Block emergency call due to restriction. But allow all normal voice/SMS/USSD/SS/AV64. */
#define TGM_RESTRICTED_STATE_CS_EMERGENCY   0x01
/* Block all normal voice/SMS/USSD/SS/AV64 due to restriction. Only Emergency call allowed. */
#define TGM_RESTRICTED_STATE_CS_NORMAL      0x02
/* Block all voice/SMS/USSD/SS/AV64	including emergency call due to restriction.*/
#define TGM_RESTRICTED_STATE_CS_ALL         0x04
/* Block packet data access due to restriction. */
#define TGM_RESTRICTED_STATE_PS_ALL         0x10




/***************
 *
 * TGM Functionality
 *
 */


#define TGM_REQUEST_SET_PDP_CONTEXT 1
#define TGM_REQUEST_SET_PIN_CODE 2
#define TGM_REQUEST_DIAL_DATA_NETWORK 3
#define TGM_REQUEST_DISCONNECT_DATA_NETWORK 4

#define TGM_REQUEST_QUERY_SIGNAL_STRENGTH 5
#define TGM_REQUEST_QUERY_IMEI 6
#define TGM_REQUEST_QUERY_MANUFACTURER 7
#define TGM_REQUEST_QUERY_OPERATOR 8
#define TGM_REQUEST_QUERY_PDP_CONTEXT_LIST 9
#define TGM_REQUEST_QUERY_MODEM_MODEL 10
#define TGM_REQUEST_QUERY_SIM_STATUS 11
#define TGM_REQUEST_QUERY_IMSI 12 //International mobile subscriber identity
#define TGM_REQUEST_QUERY_NUMBER 13 //Request subscriber number
#define TGM_REQUEST_QUERY_STATE 14 //query current tgm stack state
#define TGM_REQUEST_RESET_STACK 15 //give java side a change to reset stack while huawei device halts frequently
#define TGM_REQUEST_SET_PPPD_AUTH 16 //setting pppd authentication data


/////////////////////////////////////////////////////////////////////////////////////////////////////

#define TGM_UNSOLICITED_BASE 1000
#define TGM_UNSOLICITED_DEVICE_DETECTED 1000
#define TGM_UNSOLICITED_DEVICE_REMOVED 1001
#define TGM_UNSOLICITED_SIGNAL_STRENGTH 1002
#define TGM_UNSOLICITED_RADIO_STATE_CHANGED 1003
#define TGM_UNSOLICITED_PPPD_CONNECTED 1004
#define TGM_UNSOLICITED_PPPD_DISCONNECTED 1005
#define TGM_UNSOLICITED_PPPD_FAILED 1006


/***********************************************************************/


/**
 * RIL_Request Function pointer
 *
 * @param request is one of RIL_REQUEST_*
 * @param data is pointer to data defined for that RIL_REQUEST_*
 *        data is owned by caller, and should not be modified or freed by callee
 * @param t should be used in subsequent call to RIL_onResponse
 * @param datalen the length of data
 *
 */
typedef void (*TGM_RequestFunc) (int request, void *data,
                                    size_t datalen, TGM_Token t);

/**
 * This function should return the current radio state synchronously
 */
typedef TGM_RadioState (*TGM_RadioStateRequest)();

/**
 * This function returns "1" if the specified RIL_REQUEST code is
 * supported and 0 if it is not
 *
 * @param requestCode is one of RIL_REQUEST codes
 */

typedef int (*TGM_Supports)(int requestCode);

/**
 * This function is called from a separate thread--not the 
 * thread that calls RIL_RequestFunc--and indicates that a pending
 * request should be cancelled.
 * 
 * On cancel, the callee should do its best to abandon the request and
 * call RIL_onRequestComplete with RIL_Errno CANCELLED at some later point.
 *
 * Subsequent calls to  RIL_onRequestComplete for this request with
 * other results will be tolerated but ignored. (That is, it is valid
 * to ignore the cancellation request)
 *
 * RIL_Cancel calls should return immediately, and not wait for cancellation
 *
 * Please see ITU v.250 5.6.1 for how one might implement this on a TS 27.007 
 * interface
 *
 * @param t token wants to be canceled
 */

typedef void (*TGM_Cancel)(TGM_Token t);

typedef void (*TGM_TimedCallback) (void *param);

/**
 * Return a version string for your RIL implementation
 */
typedef const char * (*TGM_GetVersion) (void);

typedef struct {
    int version;        /* set to RIL_VERSION */
    TGM_RequestFunc onRequest;
    TGM_RadioStateRequest onStateRequest;
    TGM_Supports supports;
    TGM_Cancel onCancel;
    TGM_GetVersion getVersion;
} TGM_RadioFunctions;

struct TGM_Env {
    /**
     * "t" is parameter passed in on previous call to RIL_Notification
     * routine.
     *
     * If "e" != SUCCESS, then response can be null/is ignored
     *
     * "response" is owned by caller, and should not be modified or 
     * freed by callee
     *
     * RIL_onRequestComplete will return as soon as possible
     */
    void (*OnRequestComplete)(TGM_Token t, TGM_Errno e,
                           void *response, size_t responselen);

    /**
     * "unsolResponse" is one of RIL_UNSOL_RESPONSE_*
     * "data" is pointer to data defined for that RIL_UNSOL_RESPONSE_*
     *
     * "data" is owned by caller, and should not be modified or freed by callee
     */

    void (*OnUnsolicitedResponse)(int unsolResponse, const void *data, 
                                    size_t datalen);

    /**
     * Call user-specifed "callback" function on on the same thread that 
     * RIL_RequestFunc is called. If "relativeTime" is specified, then it specifies
     * a relative time value at which the callback is invoked. If relativeTime is
     * NULL or points to a 0-filled structure, the callback will be invoked as
     * soon as possible
     */

    void (*RequestTimedCallback) (TGM_TimedCallback callback,
                                   void *param, const struct timeval *relativeTime);   
};


/** 
 *  RIL implementations must defined RIL_Init 
 *  argc and argv will be command line arguments intended for the RIL implementation
 *  Return NULL on error
 *
 * @param env is environment point defined as RIL_Env
 * @param argc number of arguments
 * @param argv list fo arguments
 *
 */
const TGM_RadioFunctions *TGM_Init(const struct TGM_Env *env, int argc, char **argv);


/**
 * Call this once at startup to register notification routine
 *
 * @param callbacks user-specifed callback function
 */
void TGM_register (const TGM_RadioFunctions *callbacks);


/**
 *
 * RIL_onRequestComplete will return as soon as possible
 *
 * @param t is parameter passed in on previous call to RIL_Notification
 *          routine.
 * @param e error code
 *          if "e" != SUCCESS, then response can be null/is ignored
 * @param response is owned by caller, and should not be modified or
 *                 freed by callee
 * @param responselen the length of response in byte
 */
void TGM_onRequestComplete(TGM_Token t, TGM_Errno e,
                           void *response, size_t responselen);

/**
 * @param unsolResponse is one of RIL_UNSOL_RESPONSE_*
 * @param data is pointer to data defined for that RIL_UNSOL_RESPONSE_*
 *     "data" is owned by caller, and should not be modified or freed by callee
 * @param datalen the length of data in byte
 */

void TGM_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen);


/**
 * Call user-specifed "callback" function on on the same thread that 
 * RIL_RequestFunc is called. If "relativeTime" is specified, then it specifies
 * a relative time value at which the callback is invoked. If relativeTime is
 * NULL or points to a 0-filled structure, the callback will be invoked as
 * soon as possible
 *
 * @param callback user-specifed callback function
 * @param param parameter list
 * @param relativeTime a relative time value at which the callback is invoked
 */

void TGM_requestTimedCallback (TGM_TimedCallback callback,
                               void *param, const struct timeval *relativeTime);

#ifdef __cplusplus
}
#endif

#endif


