import random
from scapy.all import *

def sender():
    src_ip = "127.0.0.1"
    src_port = 54321  # 設置來源端口為54321
    dst_ip = "127.0.0.1"
    dst_port = 12345
    seq = 1

    for _ in range(10):
        packet = IP(src=src_ip, dst=dst_ip)/TCP(sport=src_port, dport=dst_port, seq=seq)/Raw(load=b"1234")
        
        # 發送封包
        send(packet)
        
        # # 等待ACK
        ack = sniff(filter=f"tcp and src host {dst_ip} and src port {dst_port}",iface="lo" , count=1)[0]
        ack_num = ack[TCP].ack
        print(ack)

        # # 根據ACK的ack number決定下一個seq
        seq = ack_num
        seq = seq + 4

if __name__ == "__main__":
    sender()
