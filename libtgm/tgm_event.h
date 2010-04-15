/*
 **
 ** Copyright 2008, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License"); 
 ** you may not use this file except in compliance with the License. 
 ** You may obtain a copy of the License at 
 **
 **     http://www.apache.org/licenses/LICENSE-2.0 
 **
 ** Unless required by applicable law or agreed to in writing, software 
 ** distributed under the License is distributed on an "AS IS" BASIS, 
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
 ** See the License for the specific language governing permissions and 
 ** limitations under the License.
 */


// Max number of fd's we watch at any one time.  Increase if necessary.

#define MAX_FD_EVENTS 8

typedef void (*tgm_event_cb)(int fd, short events, void *userdata);

struct tgm_event {

    struct tgm_event *next;
    struct tgm_event *prev;
	
    int fd;
    int index;
    bool persist;
    struct timeval timeout;
    tgm_event_cb func;
    void *param;
};

// Initialize internal data structs
void tgm_event_init();

// Initialize an event
void tgm_event_set(struct tgm_event * ev, int fd, bool persist, tgm_event_cb func, void * param);

// Add event to watch list
void tgm_event_add(struct tgm_event * ev);

// Add timer event
void tgm_timer_add(struct tgm_event * ev, struct timeval * tv);

// Remove event from watch list
void tgm_event_del(struct tgm_event * ev);

// Event loop
void tgm_event_loop();

