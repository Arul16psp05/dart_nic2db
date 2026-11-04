# CPU core pinning

bash launch_ingest.sh enp6s0f0 0 4660 4

# 100 MHz - 128 fbins
# Fchan * BYTE_per_Timeslice * numbers 
# 96 * 8192 * 100 = 78643200
# 128 * 8192 * 100 = 104857600

# Terminal 1
# Create buffer
dada_db -k dada -b 104857600 -n 16
dada_dbdisk -k dada -D /mnt/my-nvme512/dart_nic_data

## Terminal 2
./dart_nic2db -k dada -i 10.0.10.40 -p 4660 -b 128 -c 4 -f dart_observation_TPM.config -t 15

# Release the buffer
dada_db -d -k dada

#  Resume hardware interrupt requests (IRQs)
sudo systemctl start irqbalance 

