#ifndef DART_NIC2DB_H
#define DART_NIC2DB_H

#define _GNU_SOURCE 
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <endian.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>

/* PSRDADA Headers */
#include "dada_hdu.h"
#include "dada_def.h"
#include "ipcio.h"
#include "ipcbuf.h"

// Protocol Constants
#define SPEAD_HEADER_SIZE 72
#define SPEAD_PAYLOAD_SIZE 8192
#define EXPECTED_UDP_SIZE 8264

// Default Configuration Values
#define DEFAULT_NUM_CHANNELS 96
#define DEFAULT_CPU_CORE -1
#define DEFAULT_OBS_TIME 0
#define CHANNEL_BYTE_RATE 3125000ULL // 3,125,000 bytes/sec per channel

// Configuration Structure for Argument Parsing
typedef struct {
    int num_channels;
    key_t dada_key;
    int udp_port;
    int cpu_core;
    int obs_time_minutes;
    char *interface_ip;
} dart_config_t;

#endif // DART_NIC2DB_H