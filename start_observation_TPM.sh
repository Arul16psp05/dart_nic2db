bash launch_ingest.sh enp6s0f0 0 4660 4

# Terminal 1
# Create buffer
dada_db -k dada -b 78643200 -n 8
dada_dbdisk -k dada -D /mnt/my-nvme512/dart_nic_data

## Terminal 2
./dart_nic2db -k dada -i 10.0.10.40 -p 4660 -b 96 -c 4 -f dart_observation_TPM.config -t 25

# Release the buffer
dada_db -d -k dada

#  Resume hardware interrupt requests (IRQs)
sudo systemctl start irqbalance 


# Fchan * BYTE_per_Timeslice * numbers 
96 * 8192 * 100 = 78643200
128 * 8192 * 100 = 104857600

# [2026-10-30-14:36:30] 216032870400 bytes transferred in 607.643551s (339.056 MB/s)

# 100 MHz - 128 fbins

dada_db -k dada -b 104857600 -n 16
dada_dbdisk -k dada -D /mnt/my-nvme512/dart_nic_data
