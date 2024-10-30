#include "airport.h"
#include "network_utils.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>

#define MAX_THREADS 4
#define MAX_QUEUE 16

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
typedef struct {
  int buf[MAX_QUEUE];
  int head;
  int tail;
  int len;
  pthread_mutex_t mutex;
  pthread_cond_t cond_notempty;
  pthread_cond_t cond_notfull;
} conn_queue_t;

static conn_queue_t conn_queue;

void queue_init(conn_queue_t *p) {
  p->head = 0;
  p->tail = -1;
  p->len = 0;
  pthread_mutex_init(&p->mutex, NULL);
  pthread_cond_init(&p->cond_notempty, NULL);
  pthread_cond_init(&p->cond_notfull, NULL);
}

// queuer function

void queue_please(conn_queue_t *p, int connfd) {
  pthread_mutex_lock(&p->mutex);
  while (p->len == MAX_QUEUE) {
    pthread_cond_wait(&p->cond_notfull, &p->mutex);
  }
  p->tail = (p->tail + 1) % MAX_QUEUE;
  p->buf[p->tail] = connfd;
  p->len++;
  pthread_cond_signal(&p->cond_notempty);
  pthread_mutex_unlock(&p->mutex);
}


// de-queuer function use threads or something.
int dequeue_please(conn_queue_t *p) {
  int connfd; 
  pthread_mutex_lock(&p->mutex);
  while (p->len == 0) {
    pthread_cond_wait(&p->cond_notempty, &p->mutex);
  }
  connfd = p->buf[p->head]; // dequeue the first one
  p->head = (p->head + 1) % MAX_QUEUE;
  p->len--;
  pthread_cond_signal(&p->cond_notfull);
  pthread_mutex_unlock(&p->mutex);
  return connfd;
}


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
  pthread_mutex_lock(&gate->lock);
  for (idx = 0; idx < NUM_TIME_SLOTS; idx = next_idx) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 0) {
      next_idx = idx + 1;
    } else if (ts->plane_id == plane_id) {
      pthread_mutex_unlock(&gate->lock);
      return idx;
    } else {
      next_idx = ts->end_time + 1;
    }
  }
  pthread_mutex_unlock(&gate->lock);
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
    time_slot_t *t = get_time_slot_by_idx(gate, slot_idx);
      result.end_time = t->end_time;
      break;
    }
  }
  return result;
}

// it cires and then does mutex stuff
int assign_in_gate(gate_t *gate, int plane_id, int start, int duration, int fuel) {
  int idx, end;
  int latest_start = start + fuel;
  if (latest_start >= NUM_TIME_SLOTS)
    latest_start = NUM_TIME_SLOTS - duration;
  pthread_mutex_lock(&gate->lock);
  for (idx = start; idx <= latest_start; idx++) {
    end = idx + duration - 1;
    if (end >= NUM_TIME_SLOTS)
      break;
    if (check_time_slots_free(gate, idx, end)) {
      add_plane_to_slots(gate, plane_id, idx, duration); // make sure to use duration instead of end
      pthread_mutex_unlock(&gate->lock);
      return idx;
    }
  }
  pthread_mutex_unlock(&gate->lock);
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
  
  if (data) {
    data->num_gates = num_gates;
    for (int i = 0; i < num_gates; i++) {
      pthread_mutex_init(&(data->gates[i].lock), NULL);
    }
  }
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
  if (args_n != 6) {
    send_response(connfd, "Error: Invalid number of arguments for SCHEDULE\n");
    return;
  }
  if (earliest_time < 0 || earliest_time >= NUM_TIME_SLOTS) {
    send_response(connfd, "Error: Invalid 'earliest' time (%d)\n", earliest_time);
    return;
  }
  if (duration < 0 || earliest_time + duration > NUM_TIME_SLOTS) {
    send_response(connfd, "Error: Invalid 'duration' value (%d)\n", duration);
    return;
  }
  time_info_t result = schedule_plane(plane_id, earliest_time, duration, fuel);
  if (result.start_time >= 0) {
    int start_hour = IDX_TO_HOUR(result.start_time);
    int start_min = IDX_TO_MINS(result.start_time);
    int end_hour = IDX_TO_HOUR(result.end_time);
    int end_min = IDX_TO_MINS(result.end_time);
    send_response(connfd, "SCHEDULED %d at GATE %d: %02d:%02d-%02d:%02d\n",
                  plane_id, result.gate_number, start_hour, start_min, end_hour, end_min);
  } else {
    send_response(connfd, "Error: Cannot schedule %d\n", plane_id);
  }
}



void plane_status(int connfd, char *buf) {
  char command[MAXLINE];
  int airport_num, plane_id;
  int args_n = sscanf(buf, "%s %d %d", command, &airport_num, &plane_id);
  if (args_n != 3) {
    send_response(connfd, "Error: Invalid number of arguments for PLANE_STATUS\n");
    return;
  }
  time_info_t result = lookup_plane_in_airport(plane_id);
  if (result.gate_number >= 0) {
    int start_hour = IDX_TO_HOUR(result.start_time);
    int start_min = IDX_TO_MINS(result.start_time);
    int end_hour = IDX_TO_HOUR(result.end_time);
    int end_min = IDX_TO_MINS(result.end_time);
    send_response(connfd, "PLANE %d scheduled at GATE %d: %02d:%02d-%02d:%02d\n",
                  plane_id, result.gate_number, start_hour, start_min, end_hour, end_min);
    fprintf(stderr, "%d %d %d %d", result.start_time, result.end_time, end_hour, end_min);
  } else {
    send_response(connfd, "PLANE %d not scheduled at airport %d\n", plane_id, AIRPORT_ID);
  }
}



void time_status(int connfd, char *buf) {
  char command[MAXLINE];
  int airport_num, gate_num, start_idx, duration;
  int args_n = sscanf(buf, "%s %d %d %d %d",
                      command, &airport_num, &gate_num, &start_idx, &duration);
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
  if (duration <= 0 || start_idx + duration > NUM_TIME_SLOTS) {
    send_response(connfd, "Error: Invalid 'duration' value (%d)\n", duration);
    return;
  }
  gate_t *gate = get_gate_by_idx(gate_num);
  if (gate == NULL) {
    send_response(connfd, "Error: Invalid 'gate' value (%d)\n", gate_num);
    return;
  }
  // int end_idx = start_idx + duration;
  pthread_mutex_lock(&gate->lock);
  for (int i = 0; i <= duration; i++) {
    time_slot_t *time_slot = get_time_slot_by_idx(gate, start_idx+i);
    char status = time_slot->status ? 'A' : 'F';
    int flight_id = time_slot->plane_id;
    int hour = IDX_TO_HOUR(start_idx+i);
    int min = IDX_TO_MINS(start_idx+i);
    send_response(connfd, "AIRPORT %d GATE %d %02d:%02d: %c - %d\n",
                  AIRPORT_ID, gate_num, hour, min, status, flight_id);
  }
  pthread_mutex_unlock(&gate->lock);
}


void process_commands(int connfd) {
  char buf[MAXLINE];
  rio_t rio;
  rio_readinitb(&rio, connfd);
  ssize_t n = rio_readlineb(&rio, buf, MAXLINE);
  if (n <= 0) {
    close(connfd);
    return;
  }
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
  close(connfd);
}

static void *airport_thread(void *arg) {
  while (1) {
    int connfd = dequeue_please(&conn_queue);
    process_commands(connfd);
  }
  return NULL;
}

void create_worker_threads(conn_queue_t *queue, int num_threads) {
  pthread_t thread_interface[MAX_THREADS];
  queue_init(&conn_queue);
  for (int i = 0; i < MAX_THREADS; i++) {
      pthread_create(&thread_interface[i], NULL, airport_thread, NULL);
      pthread_detach(thread_interface[i]);
  }
}


void airport_node_loop(int listenfd) {
// start making threads
create_worker_threads(&conn_queue, MAX_THREADS);

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
    queue_please(&conn_queue, connfd);
  }
}
