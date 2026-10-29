#include "dart_nic2db.h"

// Lightweight ASCII Key-Value Parser
void parse_obs_config(const char *filename, dart_config_t *config) {
    if (!filename) return;
    
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Warning: Could not open config file '%s'. Using defaults.\n", filename);
        return;
    }
    
    char line[256];
    char key[64], val[64];
    
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63s %63s", key, val) == 2) {
            if (strcmp(key, "BW") == 0) config->bw = atof(val);
            else if (strcmp(key, "FREQ") == 0) config->freq = atof(val);
            else if (strcmp(key, "SOURCE") == 0) strncpy(config->source, val, 63);
            else if (strcmp(key, "RA") == 0) strncpy(config->ra, val, 63);
            else if (strcmp(key, "DEC") == 0) strncpy(config->dec, val, 63);
            else if (strcmp(key, "MODE") == 0) strncpy(config->mode, val, 63);
            else if (strcmp(key, "TSAMP") == 0) config->tsamp = atof(val);
        }
    }
    fclose(fp);
    fprintf(stdout, "Successfully parsed configuration from '%s'.\n", filename);
}

int main(int argc, char **argv)
{
    dart_config_t config = {
        .num_channels = DEFAULT_NUM_CHANNELS,
        .dada_key = DADA_DEFAULT_BLOCK_KEY,
        .udp_port = 0,
        .cpu_core = DEFAULT_CPU_CORE,
        .obs_time_minutes = DEFAULT_OBS_TIME,
        .interface_ip = NULL,
        .obs_offset = 0,
        .obs_config_file = NULL,
        // Default fallbacks if keys are missing from the config file
        .bw = 75.0, .freq = 175.0, .tsamp = 1.08,
        .source = "TPM_test", .ra = "00:00:00", .dec = "00:00:00", .mode = "PSR"
    };
    
    int arg = 0;
    while ((arg = getopt(argc, argv, "k:p:i:b:c:t:o:f:h")) != -1)
    {
        switch (arg)
        {
            case 'k':
                if (sscanf(optarg, "%x", &config.dada_key) != 1) return EXIT_FAILURE;
                break;
            case 'p': config.udp_port = atoi(optarg); break;
            case 'i': config.interface_ip = strdup(optarg); break;
            case 'b': config.num_channels = atoi(optarg); break;
            case 'c': config.cpu_core = atoi(optarg); break;
            case 't': config.obs_time_minutes = atoi(optarg); break;
            case 'o': config.obs_offset = strtoull(optarg, NULL, 10); break;
            case 'f': config.obs_config_file = strdup(optarg); break;
            case 'h':
                fprintf(stdout, "Usage: %s -k <key> -p <port> -f <config.txt> [options]\n", argv[0]);
                return EXIT_SUCCESS;
        }
    }

    if (config.udp_port == 0) return EXIT_FAILURE;

    // Parse the observation configuration file before doing anything else
    parse_obs_config(config.obs_config_file, &config);

    if (config.cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(config.cpu_core, &cpuset);
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
        
        if (numa_available() >= 0) {
            int numa_node = numa_node_of_cpu(config.cpu_core);
            if (numa_node >= 0) {
                struct bitmask *nodemask = numa_allocate_nodemask();
                numa_bitmask_setbit(nodemask, numa_node);
                numa_bind(nodemask); 
                numa_free_nodemask(nodemask);
            }
        }
    }

    uint64_t time_slice_size = (uint64_t)config.num_channels * SPEAD_PAYLOAD_SIZE;
    uint64_t total_target_bytes = (uint64_t)config.obs_time_minutes * 60 * config.num_channels * CHANNEL_BYTE_RATE;
    uint64_t total_session_bytes = 0;

    dada_hdu_t *hdu = dada_hdu_create(NULL);
    dada_hdu_set_key(hdu, config.dada_key);
    dada_hdu_connect(hdu);
    dada_hdu_lock_write(hdu);

    // ---------------------------------------------------------
    // WRITE FULL CONFIGURATION HEADER TO TRIGGER DADA_DBDISK
    // ---------------------------------------------------------
    uint64_t header_size = ipcbuf_get_bufsz(hdu->header_block);
    char *header_buf = ipcbuf_get_next_write(hdu->header_block);
    
    if (!header_buf) {
        fprintf(stderr, "Error: Could not get next header block\n");
        return EXIT_FAILURE;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    time_t now = tv.tv_sec;
    struct tm *t = gmtime(&now);
    char utc_start_str[64];
    strftime(utc_start_str, sizeof(utc_start_str), "%Y-%m-%d-%H:%M:%S", t);
    
    char utc_start_full[64];
    snprintf(utc_start_full, sizeof(utc_start_full), "%s.%06ld", utc_start_str, tv.tv_usec);

    double mjd_start = 40587.0 + (tv.tv_sec / 86400.0) + (tv.tv_usec / 86400000000.0);
    uint64_t picoseconds = (uint64_t)tv.tv_usec * 1000000ULL;
    uint64_t resolution = (uint64_t)config.num_channels * 2 * 2 * 1 * 2048; 
    
    uint64_t total_expected_heaps = 0;
    uint64_t expected_ndat = 0;
    uint64_t expected_file_size = header_size; 

    if (config.obs_time_minutes > 0) {
        total_expected_heaps = total_target_bytes / time_slice_size;
        expected_ndat = total_expected_heaps * 2048;
        expected_file_size = total_target_bytes + header_size;
    }
    
    memset(header_buf, 0, header_size);
    // Inject the dynamically parsed config fields directly into the DADA ASCII format
    snprintf(header_buf, header_size, 
        "HEADER       DADA\n"
        "HDR_VERSION  1.0\n"
        "HDR_SIZE     %" PRIu64 "\n"
        "DADA_VERSION 1.0\n"
        "BW           %.2f\n"
        "FREQ         %.2f\n"
        "TELESCOPE    DART\n"
        "RECEIVER     TPM\n"
        "INSTRUMENT   LFAASP\n"
        "SOURCE       %s\n"
        "RA           %s\n"
        "DEC          %s\n"
        "MODE         %s\n"
        "NBIT         8\n"
        "NCHAN        %d\n"
        "NDIM         2\n"
        "NPOL         2\n"
        "UTC_START    %s\n"
        "MJD_START    %.11f\n"
        "PICOSECONDS  %" PRIu64 "\n"
        "TSAMP        %.4f\n"
        "RESOLUTION   %" PRIu64 "\n"
        "OBS_OFFSET   %" PRIu64 "\n"
        "NDAT         %" PRIu64 "\n"
        "FILE_SIZE    %" PRIu64 "\n"
        "END\n",
        header_size, config.bw, config.freq, config.source, config.ra, 
        config.dec, config.mode, config.num_channels, utc_start_full, 
        mjd_start, picoseconds, config.tsamp, resolution, config.obs_offset, 
        expected_ndat, expected_file_size);

    ipcbuf_mark_filled(hdu->header_block, header_size);
    // ---------------------------------------------------------

    uint64_t block_size = ipcbuf_get_bufsz((ipcbuf_t *)hdu->data_block);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(config.udp_port);
    servaddr.sin_addr.s_addr = config.interface_ip ? inet_addr(config.interface_ip) : INADDR_ANY;
    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));

    char socket_buffer[EXPECTED_UDP_SIZE];
    uint64_t block_id = 0;
    char *open_block_pointer = NULL;
    
    uint32_t block_base_counter = 0;
    uint64_t bytes_written_to_block = 0;
    int is_new_block = 1;
    uint64_t current_time_index = 0;

    fprintf(stdout, "Listening on port %d, %d channels. Target: %" PRIu64 " bytes...\n", config.udp_port, config.num_channels, total_target_bytes);

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
            
            total_session_bytes += bytes_written_to_block;
            if (config.obs_time_minutes > 0 && total_session_bytes >= total_target_bytes) {
                fprintf(stdout, "\nTarget data volume reached for %d minutes. Terminating.\n", config.obs_time_minutes);
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
