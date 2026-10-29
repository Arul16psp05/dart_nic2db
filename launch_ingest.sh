#!/bin/bash
# Arul_and_Gemini_25_10_2026 

# Configuration
INTERFACE=$1       # Replace with your 40GbE interface name - enp6s0f0
QUEUE=$2              # The hardware queue you steered traffic to via ethtool - 0
UDP_PORT=$3        # Your pulsar data UDP port - 4660
CPU_CORE=$4    # Manual CPU core selection - 4

# 1. Automatically find the IRQ number for the specific steered hardware queue
# The regex looks for something like "eth1-TxRx-0" which is standard for Intel i40e
IRQ=$(grep "${INTERFACE}-.*-${QUEUE}" /proc/interrupts | awk -F: '{print $1}' | tr -d ' ')

if [ -z "$IRQ" ]; then
    echo "Error: Could not find IRQ for ${INTERFACE} queue ${QUEUE}. Is the interface up?"
    exit 1
fi

# Manually Fix CPU core 
sudo systemctl stop irqbalance
echo $CPU_CORE | sudo tee /proc/irq/${IRQ}/smp_affinity_list

# 2. Automatically read the CPU core affinity for that specific IRQ
# smp_affinity_list returns the exact core ID (e.g., "4" or "4-7")
CORE_LIST=$(cat /proc/irq/${IRQ}/smp_affinity_list)

# If it returns a range (e.g., 4-7) or comma-separated list, grab just the first core
CORE=$(echo "$CORE_LIST" | awk -F'[,-]' '{print $1}')

echo "Hardware Isolation: ${INTERFACE} Queue ${QUEUE} -> IRQ ${IRQ} -> CPU Core ${CORE}"
echo "Resume dymnamic IRQ selection by running ... sudo systemctl start irqbalance" 
echo " "

# 3. Launch the C ingestion utility, automatically passing the detected core
echo "Launching dart_nic2db on Core ${CORE}..."
#./dart_nic2db -p ${UDP_PORT} -b 96 -c ${CORE}


#core=4
# sudo systemctl stop irqbalance
# echo 4 | sudo tee /proc/irq/66/smp_affinity_list
#sudo systemctl stop irqbalance
#echo $core | sudo tee /proc/irq/${IRQ}/smp_affinity_list

