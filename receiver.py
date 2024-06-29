from scapy.all import *

def receiver():
    src_ip = "127.0.0.1"
    src_port = 54321  # 設置來源端口為54321
    dst_ip = "127.0.0.1"
    dst_port = 12345

    # 監聽來自sender的封包
    while True:
        pkt = sniff(filter=f"tcp and src host {src_ip} and src port {src_port}",iface="lo", count=1)[0]
        
        # 提取收到的封包的seq和payload大小
        seq = pkt[TCP].seq
        payload_size = len(pkt[Raw])
        print(seq, payload_size)

        # # 計算ACK number，加上隨機數(1~10)
        # ack_num = seq + payload_size

        # # 回送ACK
        # ack_pkt = IP(src=dst_ip, dst=pkt[IP].src)/TCP(dport=pkt[TCP].sport, sport=dst_port, ack=ack_num, flags='A')
        # send(ack_pkt)

if __name__ == "__main__":
    receiver()
