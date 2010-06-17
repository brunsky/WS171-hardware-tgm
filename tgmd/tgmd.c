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

#include <linux/netlink.h>


#define LOG_VOL(fmt, args...) \
    { LOGD(fmt , ## args); }

#define UEVENT_PARAMS_MAX 32

enum uevent_action { action_add, action_remove, action_change };

struct uevent {
    char *path;
    enum uevent_action action;
    char *subsystem;
    char *param[UEVENT_PARAMS_MAX];
    unsigned int seqnum;
};


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

static int ver_major = 1;
static int ver_minor = 0;

static int target_inserted = 0;

static int pid = -1;

static void dump_uevent(struct uevent *event)
{
    int i;

    LOG_VOL("[UEVENT] Sq: %u S: %s A: %d P: %s",
              event->seqnum, event->subsystem, event->action, event->path);
    for (i = 0; i < UEVENT_PARAMS_MAX; i++) {
        if (!event->param[i])
            break;
        LOG_VOL("%s", event->param[i]);
    }
}

static void run_child()
{
    LOGD("run_child");
    LOGD("sleep for a while");
    sleep(3);
    LOGD("wake and continue...");
    execl("/system/bin/usb_modeswitch", "usb_modeswitch", "-I", "-W", "-c", "/etc/usb_modeswitch.conf", (char *) 0);
}

static void check_uevent(struct uevent *event)
{
	int i;
    int child_state;

	if ( !strcmp(event->subsystem,"usb") && event->action == action_add && !target_inserted) {
        for (i = 0; i < UEVENT_PARAMS_MAX; i++) {
            if (!event->param[i])
                break;
            if (!strncmp(event->param[i], "PRODUCT=", strlen("PRODUCT="))) {
                char *a = event->param[i] + strlen("PRODUCT=");
                if (!strcmp(a, "12d1/1446/0")) {    // got you!   Huawei E161/E169u
                    LOG_VOL("Find target:%s => ready to call system", event->param[i]);
                    target_inserted = 1;
                    pid = fork();
                    if (!pid)
                        run_child();
                    else {
                        LOGD("waiting for usb_modeswitch terminated");
                        wait(&child_state);
                        pid = -1;
                        LOGD("ok now usb_modeswitch terminated");
                    }   
                    break;
                }
            }
            LOG_VOL("%s", event->param[i]);
        }
	}
    else if ( !strcmp(event->subsystem,"usb") && event->action == action_remove && target_inserted ) {
        target_inserted = 0;
    }
}

static void free_uevent(struct uevent *event)
{
    int i;
    free(event->path);
    free(event->subsystem);
    for (i = 0; i < UEVENT_PARAMS_MAX; i++) {
        if (!event->param[i])
            break;
        free(event->param[i]);
    }
    free(event);
}

int process_uevent_message(int socket)
{
    char buffer[64 * 1024]; // Thank god we're not in the kernel :)
    int count;
    char *s = buffer;
    char *end;
    struct uevent *event;
    int param_idx = 0;
    int i;
    int first = 1;
    int rc = 0;

    if ((count = recv(socket, buffer, sizeof(buffer), 0)) < 0) {
        LOGE("Error receiving uevent (%s)", strerror(errno));
        return -errno;
    }

    if (!(event = malloc(sizeof(struct uevent)))) {
        LOGE("Error allocating memory (%s)", strerror(errno));
        return -errno;
    }

    memset(event, 0, sizeof(struct uevent));

    end = s + count;
    while (s < end) {
        if (first) {
            char *p;
            for (p = s; *p != '@'; p++);
            p++;
            event->path = strdup(p);
            first = 0;
        } else {
            if (!strncmp(s, "ACTION=", strlen("ACTION="))) {
                char *a = s + strlen("ACTION=");
               
                if (!strcmp(a, "add"))
                    event->action = action_add;
                else if (!strcmp(a, "change"))
                    event->action = action_change;
                else if (!strcmp(a, "remove"))
                    event->action = action_remove;
            } else if (!strncmp(s, "SEQNUM=", strlen("SEQNUM=")))
                event->seqnum = atoi(s + strlen("SEQNUM="));
            else if (!strncmp(s, "SUBSYSTEM=", strlen("SUBSYSTEM=")))
                event->subsystem = strdup(s + strlen("SUBSYSTEM="));
            else
                event->param[param_idx++] = strdup(s);
        }
        s+= strlen(s) + 1;
    }
    check_uevent(event);
    //dump_uevent(event);
    
    free_uevent(event);
    return 0;
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

	int uevent_sock = -1;
    	struct sockaddr_nl nladdr;
    	int uevent_sz = 64 * 1024;

	LOGI("TGM Daemon version %d.%d", ver_major, ver_minor);
    
    system("mount -o rw -t usbfs none /proc/bus/usb"); 

	// Socket to listen on for uevent changes
    	memset(&nladdr, 0, sizeof(nladdr));
    	nladdr.nl_family = AF_NETLINK;
    	nladdr.nl_pid = getpid();
    	nladdr.nl_groups = 0xffffffff;

    	if ((uevent_sock = socket(PF_NETLINK,
                             SOCK_DGRAM,NETLINK_KOBJECT_UEVENT)) < 0) {
        	LOGE("Unable to create uevent socket: %s", strerror(errno));
        	exit(1);
    	}

    	if (setsockopt(uevent_sock, SOL_SOCKET, SO_RCVBUFFORCE, &uevent_sz,
                   	sizeof(uevent_sz)) < 0) {
       		LOGE("Unable to set uevent socket options: %s", strerror(errno));
        	exit(1);
    	}

	if (setsockopt(uevent_sock, SOL_SOCKET, SO_REUSEADDR, &uevent_sz,
                   	sizeof(uevent_sz)) < 0) {
       		LOGE("Unable to set uevent socket options: %s", strerror(errno));
        	exit(1);
    	}

    	if (bind(uevent_sock, (struct sockaddr *) &nladdr, sizeof(nladdr)) < 0) {
        	LOGE("Unable to bind uevent socket: %s", strerror(errno));
        	exit(1);
    	}


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
		//sleep(0x00ffffff);
		fd_set read_fds;
        	struct timeval to;
        	int max = 0;
        	int rc = 0;

        	to.tv_sec = (60 * 60);
        	to.tv_usec = 0;

		FD_ZERO(&read_fds);

		FD_SET(uevent_sock, &read_fds);
        	if (uevent_sock > max)
            		max = uevent_sock;

		if ((rc = select(max + 1, &read_fds, NULL, NULL, &to)) < 0) {
            		LOGE("select() failed (%s)", strerror(errno));
            		sleep(1);
            		continue;
        	}

        	if (!rc) {
            		continue;
        	}

		if (FD_ISSET(uevent_sock, &read_fds)) {
            		if ((rc = process_uevent_message(uevent_sock)) < 0) {
                		LOGE("Error processing uevent msg (%s)", strerror(errno));
            		}
       	 	}
	}

	return 0;

}
