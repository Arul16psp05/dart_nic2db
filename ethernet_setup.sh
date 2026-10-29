ethernet_setup.sh
# By Arul on 23_10_2026
#

sudo ip link set dev enp6s0f0 mtu 9000

# PCIe devices
lspci | grep -i 'eth\|network'

# For detailed hardware info:
sudo lshw -C network

# To check the driver and firmware (if you already know the interface name, e.g., eth1):
ethtool -i enp6s0f0

driver: i40e
version: 5.15.0-139-generic
firmware-version: 8.10 0x800093f8 1.2829.0
expansion-rom-version: 
bus-info: 0000:06:00.0
supports-statistics: yes
supports-test: yes
supports-eeprom-access: yes
supports-register-dump: yes
supports-priv-flags: yes

# setup

The Intel XL710 controller uses the i40e driver. Because this card does not support Mellanoxs VMA library for kernel bypass, you must aggressively tune the i40e hardware settings and manually steer the UDP flow to prevent the Linux kernel from dropping packets at 40 Gbps.

#1. Maximize Hardware Ring Buffers
The XL710 typically supports a maximum of 4096 receive descriptors per queue. Expanding this gives the card the maximum possible hardware buffer before it must interrupt the CPU.

#Command: 
sudo ethtool -G <interface> rx 4096
sudo ethtool -G enp6s0f0 rx 4096

#Verification: 
Run 
ethtool -g <interface> 
and confirm the "Current hardware settings"  for RX shows 4096.

#ethtool -g enp6s0f0
(base) dsp@RRI-D-543:~$ ethtool -g enp6s0f0
Ring parameters for enp6s0f0:
Pre-set maximums:
RX:		4096
RX Mini:	0
RX Jumbo:	0
TX:		4096
Current hardware settings:
RX:		512
RX Mini:	0
RX Jumbo:	0
TX:		512

sudo ethtool -G enp6s0f0 rx 4096
sudo ethtool -G enp6s0f0 tx 4096

(base) dsp@RRI-D-543:~$ ethtool -g enp6s0f0
Ring parameters for enp6s0f0:
Pre-set maximums:
RX:		4096
RX Mini:	0
RX Jumbo:	0
TX:		4096
Current hardware settings:
RX:		4096
RX Mini:	0
RX Jumbo:	0
TX:		4096


# 2. Hardware Flow Steering (Bypass RSS)
Because your pulsar data is likely a single, massive UDP flow, default Receive Side Scaling (RSS) will not distribute the load. You need to force the NIC to send your specific UDP port traffic to a dedicated RX queue (e.g., queue 0) so you know exactly which CPU core is handling the hardware interrupts.

#Command: sudo ethtool -N <interface> flow-type udp4 dst-port <your_udp_port> action 0
sudo ethtool -N enp6s0f0 flow-type udp4 dst-port 4660 action 0

(base) dsp@RRI-D-543:~$ sudo ethtool -N enp6s0f0 flow-type udp4 dst-port 4660 action 0
[sudo] password for dsp: 
Added rule with ID 7679

Verification: Start your data stream and run watch -n 1 "cat /proc/interrupts | grep <interface>-TxRx-0". You should see the interrupt counter rising rapidly on one specific CPU column.

watch -n 1 "cat /proc/interrupts | grep enp6s0f0-TxRx-0"

# 3. Static Interrupt Coalescing
The i40e driver uses adaptive interrupt moderation by default, which can cause erratic latency under 40Gbps sustained loads. Disable the adaptive feature and force a static delay to batch incoming jumbo frames.

#Command: sudo ethtool -C <interface> adaptive-rx off rx-usecs 50
sudo ethtool -C enp6s0f0 adaptive-rx off rx-usecs 50

(base) dsp@RRI-D-543:~$ sudo ethtool -C enp6s0f0 adaptive-rx off rx-usecs 50
rx-usecs unmodified, ignoring

Verification: Run ethtool -c <interface> to confirm Adaptive RX: off and rx-usecs: 50.

ethtool -c enp6s0f0

#4. Pin the Ingestion Process
Once you know exactly which CPU core is processing the hardware interrupts for queue 0 .identified in Step 2), you must bind dada_nic2db to that exact same core. This prevents the OS scheduler from moving the process, keeping memory cache localized.

#Command: taskset -c <CORE_ID> dada_nic2db [your normal arguments]

# taskset -c 0 dada_nic2db [your normal arguments]

Verification: While ingestion is running, open htop, press 1 to view individual CPU threads, and confirm that dada_nic2db is heavily utilizing only the specified <CORE_ID>.

##################

# Check Maximum Receive Buffer:
sysctl net.core.rmem_max

# Check Default Receive Buffer:
sysctl net.core.rmem_default

# Check All Receive Buffer Settings:
sysctl -a | grep rmem

# Before change:
(base) dsp@RRI-D-543:/data/TPM/pycode$ sudo sysctl -a | grep rmem
net.core.rmem_default = 212992
net.core.rmem_max = 212992
net.ipv4.tcp_rmem = 4096	131072	6291456
net.ipv4.udp_rmem_min = 4096

echo "net.core.rmem_max=67108864" | sudo tee -a /etc/sysctl.conf
echo "net.core.rmem_default=67108864" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

# After change:
(base) dsp@RRI-D-543:/data/TPM/pycode$ sudo sysctl -a | grep rmem
net.core.rmem_default = 67108864
net.core.rmem_max = 67108864
net.ipv4.tcp_rmem = 4096	131072	6291456
net.ipv4.udp_rmem_min = 4096


# gulp Test:

pcapfile_prefix='40G_test'
session_time=$(date +%d_%m_%Y_%H_%M)
Rampdisk_name=Ramdisk_100GB
Rampdisk_dir=$HOME/$Rampdisk_name
Ethernet_interface=enp6s0f0
gulp_buffer_size=1000      # MB
gulp_filesize=2                       # (Default = 2).    gulp_buffer_size * gulp_filesize     ==>  MB
number_of_pcapfiles=3     # files to keep in Ringbuffer

sudo taskset -c 0 gulp -i $Ethernet_interface -f "udp and udp[4:2] == 8272" -r $gulp_buffer_size -C $gulp_filesize -n $pcapfile_prefix -o $Rampdisk_dir

(base) dsp@RRI-D-543:~/Ramdisk_100GB$ sudo taskset -c 0 gulp -i $Ethernet_interface -f "udp and udp[4:2] == 8272" -r $gulp_buffer_size -C $gulp_filesize -n $pcapfile_prefix -o $Rampdisk_dir
^C
12633833 packets captured
ring buffer use: 0.5% of 1000 MB

#############################


Optimized 300-Channel Buffer Strategies for 128GB RAM

Since your 300-channel time slice size is exactly 2,457,600 bytes, you can scale the number of blocks (-n) and block sizes (-b) safely to utilize your memory pool:

Option 1: The 16 GB Total Buffer Pool (Recommended Balance)

This allocates about 1/8th of your total system memory to the ring buffer. It provides enough space to absorb up to 3.2 seconds of complete data backlogs without dropping network packets.
• Block Size (-b): 270336000 (110 time slices \(\approx \) 257.8 MB)
• Number of Blocks (-n): 64
• Allocation Command:bash
dada_db -k dada -n 64 -b 270336000
Use code with caution.

Option 2: The 32 GB Total Buffer Pool (Maximum Safety Cushion)

If you are streaming into a heavy processing consumer that occasionally hits high-latency overheads, you can expand the pool to 32 GB. This holds up to 6.4 seconds of continuous 40G data stream.
• Block Size (-b): 540672000 (220 time slices \(\approx \) 515.6 MB)
• Number of Blocks (-n): 64
• Allocation Command:bash
dada_db -k dada -n 64 -b 540672000
Use code with caution.

🛠️ Important Kernel Adjustments for 128GB RAM

To allow PSRDADA to claim these large memory segments, ensure your Linux kernel settings (/etc/sysctl.conf) are configured to allow up to 64 GB for individual allocations.
Run the following commands to update your limits live:
bash
# Allow up to 64 GB for a single shared memory segment
sudo sysctl -w kernel.shmmax=68719476736

# Set total shared memory allocation limits system-wide (measured in 4KB pages)
sudo sysctl -w kernel.shmall=16777216
