#include "airport.h"
#include "network_utils.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>

/** This is the main file in which you should implement the airport server code.
 *  There are many functions here which are pre-written for you. You should read
 *  the comments in the corresponding `airport.h` header file to understand what
 *  each function does, the arguments they accept and how they are intended to
 *  be used.
 *
 *  You are encouraged to implement your own helper functions to handle requests
 *  in airport nodes in this file. You are also permitted to modify the
 *  functions you have been given if needed.
 */

/* This will be set by the `initialise_node` function. */
static int AIRPORT_ID = -1;

/* This will be set by the `initialise_node` function. */
static airport_t *AIRPORT_DATA = NULL;

// create structure in this file just for queing connections


// queuer function


// de-queuer function use threads or something.

gate_t *get_gate_by_idx(int gate_idx) {
  if ((gate_idx) < 0 || (gate_idx > AIRPORT_DATA->num_gates))
    return NULL;
  else
    return &AIRPORT_DATA->gates[gate_idx];
}

time_slot_t *get_time_slot_by_idx(gate_t *gate, int slot_idx) {
  if ((slot_idx < 0) || (slot_idx >= NUM_TIME_SLOTS))
    return NULL;
  else
    return &gate->time_slots[slot_idx];
}

int check_time_slots_free(gate_t *gate, int start_idx, int end_idx) {
  time_slot_t *ts;
  int idx;
  for (idx = start_idx; idx <= end_idx; idx++) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 1)
      return 0;
  }
  return 1;
}

int set_time_slot(time_slot_t *ts, int plane_id, int start_idx, int end_idx) {
  if (ts->status == 1)
    return -1;
  ts->status = 1; /* Set to be occupied */
  ts->plane_id = plane_id;
  ts->start_time = start_idx;
  ts->end_time = end_idx;
  return 0;
}

int add_plane_to_slots(gate_t *gate, int plane_id, int start, int count) {
  int ret = 0, end = start + count;
  time_slot_t *ts = NULL;
  for (int idx = start; idx <= end; idx++) {
    ts = get_time_slot_by_idx(gate, idx);
    ret = set_time_slot(ts, plane_id, start, end);
    if (ret < 0) break;
  }
  return ret;
}

int search_gate(gate_t *gate, int plane_id) {
  int idx, next_idx;
  time_slot_t *ts = NULL;
  for (idx = 0; idx < NUM_TIME_SLOTS; idx = next_idx) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 0) {
      next_idx = idx + 1;
    } else if (ts->plane_id == plane_id) {
      return idx;
    } else {
      next_idx = ts->end_time + 1;
    }
  }
  return -1;
}

time_info_t lookup_plane_in_airport(int plane_id) {
  time_info_t result = {-1, -1, -1};
  int gate_idx, slot_idx;
  gate_t *gate;
  for (gate_idx = 0; gate_idx < AIRPORT_DATA->num_gates; gate_idx++) {
    gate = get_gate_by_idx(gate_idx);
    if ((slot_idx = search_gate(gate, plane_id)) >= 0) {
      result.start_time = slot_idx;
      result.gate_number = gate_idx;
      result.end_time = get_time_slot_by_idx(gate, slot_idx)->end_time;
      break;
    }
  }
  return result;
}

int assign_in_gate(gate_t *gate, int plane_id, int start, int duration, int fuel) {
  int idx, end = start + duration;
  for (idx = start; idx <= (start + fuel) && (end < NUM_TIME_SLOTS); idx++) {
    if (check_time_slots_free(gate, idx, end)) {
      add_plane_to_slots(gate, plane_id, idx, duration);
      return idx;
    }
    end++;
  }
  return -1;
}

time_info_t schedule_plane(int plane_id, int start, int duration, int fuel) {
  time_info_t result = {-1, -1, -1};
  gate_t *gate;
  int gate_idx, slot;
  for (gate_idx = 0; gate_idx < AIRPORT_DATA->num_gates; gate_idx++) {
    gate = get_gate_by_idx(gate_idx);
    if ((slot = assign_in_gate(gate, plane_id, start, duration, fuel)) >= 0) {
      result.start_time = slot;
      result.gate_number = gate_idx;
      result.end_time = slot + duration;
      break;
    }
  }
  return result;
}

airport_t *create_airport(int num_gates) {
  airport_t *data = NULL;
  size_t memsize = 0;
  if (num_gates > 0) {
    memsize = sizeof(airport_t) + (sizeof(gate_t) * (unsigned)num_gates);
    data = calloc(1, memsize);
  }
  if (data)
    data->num_gates = num_gates;
  return data;
}

void initialise_node(int airport_id, int num_gates, int listenfd) {
  AIRPORT_ID = airport_id;
  AIRPORT_DATA = create_airport(num_gates);
  if (AIRPORT_DATA == NULL)
    exit(1);
  airport_node_loop(listenfd);
}


// time to EAT

// helperssss cus i aint reeading all that yfeel

void schedule_please(int connfd, char *buf) {
  char command[MAXLINE];
  int airport_num, plane_id, earliest_time, duration, fuel;
  int args_n = sscanf(buf, "%s %d %d %d %d %d",
                      command, &airport_num, &plane_id, &earliest_time, &duration, &fuel);
  if (args_n !=6) {
    send_response(connfd, "Error: Invalid number of arguments for SCHEDULE\n");
    return;
  }
  // if earliest time wrong TODO
  // if duration wrong TODO

  time_info_t result = schedule_plane(plane_id, earliest_time, duration, fuel);
  if (result.start_time >= 0) {
    char response[MAXLINE];
    int start_hour = IDX_TO_HOUR(result.start_time);
    int start_min = IDX_TO_MINS(result.start_time);
    int end_hour = IDX_TO_HOUR(result.end_time);
    int end_min = IDX_TO_HOUR(result.end_time);
    send_response(connfd, "SCHEDULED %d at GATE 5D: %02d:%02d-%02d:%02d\n",
                         plane_id, result.gate_number, start_hour, start_min, end_hour, end_min);
  } else {
    send_response(connfd, "Error: Cannot schedule %d\n", plane_id);
  }
}

void plane_status(int connfd, char *buf) {
  char command[MAXLINE];
  int airport_num, plane_id;
  int args_n = sscanf(buf, "%s %d %d", command, &airport_num, &plane_id);
  // look that shi up
  time_info_t result = lookup_plane_in_airport(plane_id);
  if (result.start_time >= 0) {
    int start_hour = IDX_TO_HOUR(result.start_time);
    int start_min = IDX_TO_MINS(result.start_time);
    int end_hour = IDX_TO_HOUR(result.end_time);
    int end_min = IDX_TO_MINS(result.end_time);
    send_response(connfd, "PLANE %d scheduled at GATE %d: %02d:%02d-%02d:%02d\n",
                  plane_id, result.gate_number, start_hour, start_min, end_hour, end_min);
  } else {
    send_response(connfd, "PLANE %d not scheduled at airport %d\n", plane_id, AIRPORT_ID);
  }
}

void time_status(int connfd, char *buf) {
  char command[MAXLINE];
  int airport_num, gate_num, start_idx, duration;
  int args_n = sscanf(buf, "%s %d %d %d %d",
                      command, &airport_num, &gate_num, &start_idx, &duration);
  // TSA
  // too many or too litte lugage
  if (args_n != 5) {
    send_response(connfd, "Error: Invalid number of arguments for TIME_STATUS\n");
    return;
  } 
  if (gate_num < 0 || gate_num >= AIRPORT_DATA->num_gates) {
    send_response(connfd, "Error: Invalid 'gate' value (%d)\n", gate_num);
    return;
  }
  if (start_idx < 0 || start_idx >= NUM_TIME_SLOTS) {
    send_response(connfd, "Error: Invalid 'start_idx' value (%d)\n", start_idx);
    return;
  }
  if (duration <= 0 || start_idx + duration >= NUM_TIME_SLOTS) {
    send_response(connfd, "Error: Invalid 'duration' value (%d)\n", duration);
    return;
  }

    // procces
  gate_t *gate = get_gate_by_idx(gate_num);
  if (gate == NULL) {
    send_response(connfd, "Error: Invalid 'gate' value (%d)\n", gate_num);
    return;
  }
  int end_idx = start_idx + duration;
  if (start_idx < 0 || end_idx >= NUM_TIME_SLOTS) {
    send_response(connfd, "Error: Invalid 'start_idx' or 'duration\n");
    return;
  }
  // lock thread
  pthread_mutex_lock(&gate->lock);
  int i = start_idx;
  while (i <= end_idx) {
    time_slot_t *time_slot = get_time_slot_by_idx(gate, i);
    char status = time_slot->status ? 'A' : 'F';
    int flight_id = time_slot->status ? time_slot->plane_id : 0;
    int hour = IDX_TO_HOUR(i);
    int min = IDX_TO_MINS(i);
    send_response(connfd, "AIRPORT %d GATE %d %02d:%02d: %c - %d\n",
                  AIRPORT_ID, gate_num, hour, min, status, flight_id);
    pthread_mutex_unlock(&gate->lock);
  }
}

void process_commands(int connfd) {
  char buf[MAXLINE];
  size_t n;
  rio_t rio;
  rio_readinitb(&rio, connfd);
  while ((n = rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    char command[MAXLINE];
    int args_n = sscanf(buf, "%s", command);

    if (args_n < 1) {
        send_response(connfd, "Error: Invalid request provided\n");
        close(connfd);
        return;
    }

    if (strcmp(command, "SCHEDULE") == 0) {
      schedule_please(connfd, buf);
    } else if (strcmp(command, "PLANE_STATUS") == 0) {
      plane_status(connfd, buf);
    } else if (strcmp(command, "TIME_STATUS") == 0) {
      time_status(connfd, buf);
    } else {
      send_response(connfd, "Error: Invalid request provided\n");
    }
  }
  close(connfd);
}

void airport_thread(void *arg) {
}

void airport_node_loop(int listenfd) {
  /** TODO: implement the main server loop for an individual airport node here. */

// always listen cus we dont know how to speak
  while (1) {
    int connfd;
    struct sockaddr_storage clientaddr; // store mamangers address
    socklen_t clientlen = sizeof(struct sockaddr_storage);
     // we're lonely so we accept the first connection we recieve without security
    connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) { // something happened and the connection was unable to be established
        fprintf(stderr, "[Airport %d] Accept error: %s\n", AIRPORT_ID, strerror(errno));
        continue;
    }
    // put connection into queue ie run the threads
    
    // process comands in loop for testing
    
    process_commands(connfd);
  }
}


