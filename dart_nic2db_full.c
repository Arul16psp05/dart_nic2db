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

#define SPEAD_HEADER_SIZE 72
#define SPEAD_PAYLOAD_SIZE 8192
#define EXPECTED_UDP_SIZE 8264

int main(int argc, char **argv)
{
    int num_channels = 96;             
    key_t dada_key = DADA_DEFAULT_BLOCK_KEY; 
    int udp_port = 0;
    int cpu_core = -1; 
    int obs_time_minutes = 0; 
    char *interface_ip = NULL;
    int arg = 0;
    
    while ((arg = getopt(argc, argv, "k:p:i:b:c:t:h")) != -1)
    {
        switch (arg)
        {
            case 'k':
                if (sscanf(optarg, "%x", &dada_key) != 1) return EXIT_FAILURE;
                break;
            case 'p':
                udp_port = atoi(optarg);
                break;
            case 'i':
                interface_ip = strdup(optarg);
                break;
            case 'b':
                num_channels = atoi(optarg);
                break;
            case 'c':
                cpu_core = atoi(optarg);
                break;
            case 't':
                obs_time_minutes = atoi(optarg);
                break;
            case 'h':
                fprintf(stdout, "Usage: %s -k <key> -i <ip> -p <port> -b <channels> -c <core> -t <minutes>\n", argv[0]);
                return EXIT_SUCCESS;
        }
    }

    if (udp_port == 0) return EXIT_FAILURE;

    if (cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    }

    // 1. Calculate strictly math-based boundaries based on the channel count
    uint64_t time_slice_size = (uint64_t)num_channels * SPEAD_PAYLOAD_SIZE;
    
    // 3,125,000 bytes/sec per channel (8192 bytes / 0.00262144 sec)
    uint64_t channel_byte_rate = 3125000ULL; 
    uint64_t total_target_bytes = (uint64_t)obs_time_minutes * 60 * num_channels * channel_byte_rate;
    uint64_t total_session_bytes = 0;

    dada_hdu_t *hdu = dada_hdu_create(NULL);
    dada_hdu_set_key(hdu, dada_key);
    dada_hdu_connect(hdu);
    dada_hdu_lock_write(hdu);

    uint64_t block_size = ipcbuf_get_bufsz((ipcbuf_t *)hdu->data_block);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(udp_port);
    servaddr.sin_addr.s_addr = interface_ip ? inet_addr(interface_ip) : INADDR_ANY;
    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));

    char socket_buffer[EXPECTED_UDP_SIZE];
    uint64_t block_id = 0;
    char *open_block_pointer = NULL;
    
    uint32_t block_base_counter = 0;
    uint64_t bytes_written_to_block = 0;
    int is_new_block = 1;
    uint64_t current_time_index = 0;

    fprintf(stdout, "Listening on port %d, %d channels. Target: %" PRIu64 " bytes...\n", udp_port, num_channels, total_target_bytes);

    while (1) 
    {
        ssize_t bytes_received = recvfrom(sockfd, socket_buffer, EXPECTED_UDP_SIZE, 0, NULL, NULL);
        if (bytes_received != EXPECTED_UDP_SIZE) continue; 

        uint16_t logical_channel = be16toh(*(uint16_t *)(socket_buffer + 10));
        uint32_t packet_counter = be32toh(*(uint32_t *)(socket_buffer + 12));

        if (is_new_block) {
            open_block_pointer = ipcio_open_block_write(hdu->data_block, &block_id);
            block_base_counter = packet_counter;
            memset(open_block_pointer, 0, block_size);
            is_new_block = 0;
            bytes_written_to_block = 0;
        }

        current_time_index = (uint64_t)(packet_counter - block_base_counter);
        uint64_t destination_offset = (current_time_index * time_slice_size) + (logical_channel * SPEAD_PAYLOAD_SIZE);

        if ((destination_offset + SPEAD_PAYLOAD_SIZE) > block_size) {
            ipcio_close_block_write(hdu->data_block, bytes_written_to_block);
            
            // 2. Add block size to total session tracker (0 CPU latency within packet loop)
            total_session_bytes += bytes_written_to_block;
            if (obs_time_minutes > 0 && total_session_bytes >= total_target_bytes) {
                fprintf(stdout, "\nTarget data volume reached for %d minutes. Terminating.\n", obs_time_minutes);
                open_block_pointer = NULL;
                break;
            }
            
            open_block_pointer = ipcio_open_block_write(hdu->data_block, &block_id);
            memset(open_block_pointer, 0, block_size);
            
            block_base_counter = packet_counter;
            current_time_index = 0;
            destination_offset = (logical_channel * SPEAD_PAYLOAD_SIZE);
            bytes_written_to_block = 0;
        }

        memcpy(open_block_pointer + destination_offset, 
               socket_buffer + SPEAD_HEADER_SIZE, 
               SPEAD_PAYLOAD_SIZE);
               
        if (destination_offset + SPEAD_PAYLOAD_SIZE > bytes_written_to_block) {
            bytes_written_to_block = destination_offset + SPEAD_PAYLOAD_SIZE;
        }
    }

    if (open_block_pointer) {
        ipcio_close_block_write(hdu->data_block, bytes_written_to_block);
    }
    
    dada_hdu_unlock_write(hdu);
    dada_hdu_disconnect(hdu);
    close(sockfd);
    
    return EXIT_SUCCESS;
}