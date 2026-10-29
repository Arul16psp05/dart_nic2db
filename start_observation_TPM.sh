bash launch_ingest.sh enp6s0f0 0 4660 4

# Terminal 1
dada_db -k dada -b 78643200 -n 8
dada_dbdisk -k dada -D /mnt/my-nvme512/dart_nic_data

## Terminal 2
./dart_nic2db -k dada -i 10.0.10.40 -p 4660 -b 96 -c 4 -f dart_observation_TPM.config -t 25

sudo systemctl start irqbalance #  Resume hardware interrupt requests (IRQs)
