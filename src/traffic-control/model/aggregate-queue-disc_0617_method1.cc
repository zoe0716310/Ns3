/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#include "aggregate-queue-disc.h"

#include "ns3/drop-tail-queue.h"
#include "ns3/log.h"
#include "ns3/object-factory.h"

#include "ns3/ppp-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/ethernet-header.h"
#include "ns3/tcp-header.h"
#include "iomanip"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/simulator.h"
#include "ns3/tcp-option-sack.h"
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <bitset>


int Mycount1 = 0;
int Mycount2 = 0;
int Mycount3 = 0;
int drop_count = 0;
int need_retrans_seq_path2 = 0;
int need_retrans_port_path1 = 0;
std::vector<std::string> Mydata1;
std::vector<std::string> Mydata2;
std::vector<int> Myports;

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AggregateQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(AggregateQueueDisc);

class aggregate_pkt {
    public:
        SequenceNumber32 seq;
        long long int payload = 0;

        aggregate_pkt(TcpHeader _tcpHeader, uint8_t *_payload){
            seq = _tcpHeader.GetSequenceNumber();
            for (int i = 0; i < 4; i++) {
                payload = (payload << 8) | _payload[i];
            }
        }
        ~aggregate_pkt(){

        }

};

////////////////////////////////////////////////////// global var
uint32_t WorkerNum = 10; // Workers per aggregate
uint32_t QueueLength = 50; // max packet number in aggregate table per worker
uint32_t AggregateSsh = 10; // threshold of aggregate
std::map<std::pair<Ipv4Address, uint16_t>, uint32_t> Workers_Tuple; // mapping ip/port to Worker number
std::map<uint32_t, std::pair<Ipv4Address, uint16_t>> Workers_Num; // mapping Worker number to ip/port
std::vector<std::vector<long long>> AggregateTable; // first row is seq, others are each worker's payload
std::map<SequenceNumber32, std::bitset<10>> CountMap; // map from seq to count
std::vector<std::vector<long long>>::iterator min_seq;
std::vector<std::vector<long long>>::iterator now_seq;
std::vector<std::vector<long long>>::iterator max_seq;
std::vector<int> diractlySentSeq;
std::map<int, std::pair<int, int>> diractlySentMap;
SequenceNumber32 AggregateSeq;
SequenceNumber32 AggregateAck;
uint8_t AggregateCount;
SequenceNumber32 LastSentSeq;
int psSeq = 1;
uint8_t hasSent[2];
std::map<SequenceNumber32, std::pair<Time, bool>> timeMap;
int count = 0;
//////////////////////////////////////////////////////

TypeId
AggregateQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::AggregateQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<AggregateQueueDisc>()
            .AddAttribute("MaxSize",
                          "The max queue size",
                          QueueSizeValue(QueueSize("1000p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("AggregateSsh",
                          "The aggregate threshold",
                          UintegerValue(10),
                          MakeUintegerAccessor(&AggregateQueueDisc::SetAggregateSsh, &AggregateQueueDisc::GetAggregateSsh),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

AggregateQueueDisc::AggregateQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
    NS_LOG_FUNCTION(this);
}

AggregateQueueDisc::~AggregateQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void AggregateQueueDisc::SetAggregateSsh(uint32_t threshold){
    AggregateSsh = threshold;
}

uint32_t AggregateQueueDisc::GetAggregateSsh() const{
    return AggregateSsh;
}

uint32_t MatchWorker(const std::pair<Ipv4Address, uint16_t>& key) {
    auto it = Workers_Tuple.find(key);

    if (it != Workers_Tuple.end()) {
        return it->second;
    }
    else {
        uint32_t newValue = static_cast<uint32_t>(Workers_Tuple.size()) + 1;
        Workers_Tuple[key] = newValue;
        Workers_Num[newValue] = key;
        return newValue;
    }
}

void setBit(uint8_t* buffer, int pos) {
    if (pos < 1 || pos > 16) {
        std::cerr << "disc : Invalid input. pos should be between 1 and 16." << pos << std::endl;
        return;
    }

    // 計算要修改的位元在哪個 byte
    int byteIndex = (pos - 1) / 8;

    // 計算在該 byte 中的偏移量
    int bitOffset = (pos - 1) % 8;

    // 將對應的位元設為 1
    buffer[byteIndex] |= (1 << bitOffset);
}

void setAllBit(uint8_t* buffer){
    for(int i = 1; i <= WorkerNum; i++){
        setBit(buffer, i);
    }
}

std::pair<bool, long long> UpdateTable(aggregate_pkt pkt, u_int32_t Worker){
    // std::cout << "*****************************\n";
    // for(auto vec : AggregateTable){
    //     for (auto num : vec){
    //         std::cout << num << "\t";
    //     }
    //     if(vec[0] == (*min_seq)[0]){
    //         std::cout << "<- min_seq";
    //     }
    //     if(vec[0] == (*max_seq)[0]){
    //         std::cout << "<- max_seq";
    //     }
    //     if(vec[0] == (*now_seq)[0]){
    //         std::cout << "<- now_seq";
    //     }
    //     std::cout << "\n";
    // }
    // std::cout << "*****************************\n";
    // put pkt into table
    if (!AggregateTable.empty()){ // if table not empty
        // std::cout << "table not empty!" << "\n";
        bool isDiactly = std::find(diractlySentSeq.begin(), diractlySentSeq.end(), pkt.seq.GetValue()) != diractlySentSeq.end();
        if (pkt.seq.GetValue() < AggregateAck.GetValue()){
            return std::pair<bool, long long> (false, 0);
        }
        else if ((pkt.seq.GetValue() < (*now_seq)[0]) || isDiactly){ // retransmission
            // std::cout << "retransmission pkt!\n";
            diractlySentMap[pkt.seq.GetValue()].first += pkt.payload;
            diractlySentMap[pkt.seq.GetValue()].second++;
            if (diractlySentMap[pkt.seq.GetValue()].second == WorkerNum){
                // std::cout << pkt.seq.GetValue() << "retransmission ok!" << "\n";
                setAllBit(hasSent);
                // diractlySentMap.erase(pkt.seq.GetValue());
                return std::pair<bool, long long> (true, diractlySentMap[pkt.seq.GetValue()].first);
            }
            // setBit(hasSent, Worker);
            // return std::pair<bool, long long> (true, pkt.payload);
            return std::pair<bool, long long> (false, 0);
        }
        else if (pkt.seq.GetValue() > (*max_seq)[0]){ // new seq
            auto temp_seq = max_seq;
            if ((temp_seq + 1) == AggregateTable.end()){
                temp_seq = AggregateTable.begin();
            }
            else {
                temp_seq++;
            }
            if (temp_seq == min_seq){ // buffer overflow
                // diracty sent
                // std::cout << "buffer overflow!\n";
                // diractlySentSeq.push_back(pkt.seq.GetValue()); // Temp
                // setBit(hasSent, Worker);
                // return std::pair<bool, long long> (true, pkt.payload);
                return std::pair<bool, long long> (false, 0);
            }
            else{ //TODO : set 0
                (*temp_seq)[0] = pkt.seq.GetValue();
                (*temp_seq)[1] = pkt.payload;
                (*temp_seq)[2] = 1;
                max_seq = temp_seq;
                return std::pair<bool, long long> (false, 0);
            }
        }
        else{
            for (auto it = now_seq; ; ){
                auto next = it;
                if ((next + 1) != AggregateTable.end()){
                    if (next != max_seq){
                        next++;
                    }
                }
                else{
                    if (next != max_seq){
                        next = AggregateTable.begin();
                    }
                }
                if ((*it)[0] == pkt.seq.GetValue()){
                    (*it)[1] += pkt.payload;
                    (*it)[2]++;
                    if ((*it)[2] == WorkerNum){
                        // Aggergate
                        // std::cout << "Aggergate!\n";
                        now_seq = next;
                        setAllBit(hasSent);
                        return std::pair<bool, long long> (true, (*it)[1]);
                    }
                    else{
                        return std::pair<bool, long long> (false, 0);
                    }
                }
                if (it == max_seq){
                    break;
                }
                it = next;
            }
            // out-of-order pkt
            // diracty sent
            // std::cout << "out-of-order!\n";
            diractlySentSeq.push_back(pkt.seq.GetValue());
            diractlySentMap[pkt.seq.GetValue()] = std::pair<int, int>(pkt.payload, 1);
            // setBit(hasSent, Worker);
            // return std::pair<bool, long long> (true, pkt.payload);
            return std::pair<bool, long long> (false, 0);
        }
    }
    else { // if table empty
        // std::cout << "table empty!" << "\n";
        for (int i = 0; i < QueueLength; i++){
            AggregateTable.push_back(std::vector<long long>{0, 0, 0});
        }
        min_seq = now_seq = max_seq = AggregateTable.begin();
        (*min_seq)[0] = pkt.seq.GetValue();
        (*min_seq)[1] = pkt.payload;
        (*min_seq)[2] = 1;
        return std::pair<bool, long long> (false, 0);
    }
}

void PacketAcked(int ackNum){
    // std::cout << "PacketAcked :" << ackNum << "\n";
    diractlySentSeq.erase(std::remove_if(diractlySentSeq.begin(), diractlySentSeq.end(), [ackNum](int x) { return x < ackNum; }), diractlySentSeq.end());
    for(auto it = diractlySentMap.begin(); it != diractlySentMap.end(); ) {
        if(it->first < ackNum) {
            it = diractlySentMap.erase(it); // 刪除元素並更新迭代器
        } else {
            ++it; // 移動到下一個元素
        }
    }
    for (auto it = min_seq; ; ){
        if ((*it)[0] < ackNum){
            (*it)[0] = 0;
            (*it)[1] = 0;
            (*it)[2] = 0;
        }
        else{
            return;
        }
        if (it == max_seq){
            return;
        }
        if ((it + 1) != AggregateTable.end()){
            it++;
        }
        else{
            it = AggregateTable.begin();
        }
        min_seq = it;
    }
    // std::cout << "*****************************\n";
    // for(auto vec : AggregateTable){
    //     for (auto num : vec){
    //         std::cout << num << "\t";
    //     }
    //     if(vec[0] == (*min_seq)[0]){
    //         std::cout << "<- min_seq";
    //     }
    //     if(vec[0] == (*max_seq)[0]){
    //         std::cout << "<- max_seq";
    //     }
    //     std::cout << "\n";
    // }
    // std::cout << "*****************************\n";
}

std::vector<uint32_t> getSetBits(const uint8_t* buffer) {
    std::vector<uint32_t> setBits;

    for (int byteIndex = 0; byteIndex < 2; ++byteIndex) {
        for (int bitOffset = 0; bitOffset < 8; ++bitOffset) {
            if ((buffer[byteIndex] & (1 << bitOffset)) != 0) {
                setBits.push_back(byteIndex * 8 + bitOffset + 1);
            }
        }
    }

    return setBits;
}

std::vector<uint32_t> DupAck(uint8_t * buffer){ // check which Worker need to be Acked
    std::vector<uint32_t> WorkersNeedAcked;
    uint8_t tempBuf[2];
    tempBuf[0] = buffer[0];
    tempBuf[1] = buffer[1];
    WorkersNeedAcked = getSetBits(tempBuf);
    return WorkersNeedAcked;
}

void clearBuffer(uint8_t* buffer) {
    buffer[0] = 0;
    buffer[1] = 0;
}


bool
AggregateQueueDisc::Enqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    Ptr<Packet> copy = item->GetPacket()->Copy();
    Ptr<Ipv4QueueDiscItem> ipItem = DynamicCast<Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();
    TcpHeader tcpHeader;
    Mycount1++;
    bool retval = false;
    bool SendorNot = true;

    // // NS_LOG_INFO("going from " << ipHeader.GetSource() << " to " << ipHeader.GetDestination());
    // if ((Mycount1 == 200)){
    //     std::cout << "going from " << ipHeader.GetSource() << " to " << ipHeader.GetDestination() << "\n";
    //     return false;
    // }

    if (Mycount1 % 1 == 0){
        if (copy->PeekHeader(tcpHeader) != 0) {
            copy->RemoveHeader(tcpHeader);
            uint8_t *buffer = new uint8_t[copy->GetSize()];
            int size = copy->CopyData(buffer, copy->GetSize ());
            unsigned int seq_num = tcpHeader.GetSequenceNumber().GetValue();
            unsigned int ack_num = tcpHeader.GetAckNumber().GetValue();
            // get 4-tuple
            std::pair<Ipv4Address, uint16_t> dst_info = std::pair<Ipv4Address, uint16_t> (ipHeader.GetDestination(), tcpHeader.GetDestinationPort());
            std::pair<Ipv4Address, uint16_t> src_info = std::pair<Ipv4Address, uint16_t> (ipHeader.GetSource(), tcpHeader.GetSourcePort());
            
            if (seq_num != 0 && !(seq_num == 1 && ack_num == 1 && copy->GetSize () == 0)){ // not deal with 3-way handshaking & control msg
                if (src_info.first == "10.0.11.2") { // if packet is ACK
                    if (AggregateAck.GetValue() < ack_num){
                        AggregateAck = SequenceNumber32(ack_num);
                    }
                    PacketAcked(ack_num);
                    for (int WorkerNeedAcked = 1; WorkerNeedAcked <= WorkerNum; WorkerNeedAcked++){
                        // std::cout << "sent ack to worker : " << WorkerNeedAcked << "; SetDestinationPort" << Workers_Num[WorkerNeedAcked].second <<  "\n";
                        TcpHeader newTCPheader = tcpHeader;
                        Ptr<Packet> newPacket = Create<Packet>(0);
                        newTCPheader.SetDestinationPort(Workers_Num[WorkerNeedAcked].second);
                        newPacket->AddHeader(newTCPheader);
                        Address addr = ipItem->GetAddress();
                        uint16_t protocol = ipItem->GetProtocol();
                        Ipv4Header newheader;
                        newheader.SetSource(src_info.first);
                        newheader.SetDestination(Workers_Num[WorkerNeedAcked].first);
                        newheader.SetProtocol(6);
                        newheader.SetIdentification(0);
                        newheader.SetTtl(64);
                        newheader.SetPayloadSize(newTCPheader.GetSerializedSize());
                        const Ipv4Header & header = newheader;
                        Ptr<Ipv4QueueDiscItem> itemptr = Create<Ipv4QueueDiscItem>(newPacket, addr, protocol, header);
                        ipItem = itemptr;
                        ///////////////////////////// enqueue
                        m_stats.nTotalReceivedPackets++;
                        m_stats.nTotalReceivedBytes += ipItem->GetSize();
                        retval = DoEnqueue(ipItem);

                        if (retval)
                        {
                            ipItem->SetTimeStamp(Simulator::Now());
                        }
                    }
                    SendorNot = false;
                }
                else{
                    // clear hasSent buffer
                    clearBuffer(hasSent);
                    // check worker order
                    u_int32_t Worker = MatchWorker(src_info); // TODO : 之後可能可以有多個receiver
                    if (Worker > WorkerNum){
                       // std::cout << "Worker more than prediction" << "\n";
                        return false;
                    }
                    // create object for incoming packets
                    aggregate_pkt pkt(tcpHeader, buffer);
                    // std::cout << "Worker : " << Worker << "; receive seq: " << pkt.seq  << " !" << "\n";
                    // check if need aggregate
                    std::pair<bool, long long> AggregateParam = UpdateTable(pkt, Worker);
                    if (AggregateParam.first){
                        //std::cout << "sent aggregate pkt!" << "\n";
                        std::pair<Ipv4Address, uint16_t> LeaderTuple = Workers_Num[1];
                        // change int to uint8_t
                        uint8_t aggregate_payload[200]; 
                        aggregate_payload[0] = hasSent[0];
                        aggregate_payload[1] = hasSent[1];
                        aggregate_payload[2] = (AggregateParam.second >> 8) & 0xFF;
                        aggregate_payload[3] = AggregateParam.second & 0xFF;
                        // create new pkt
                        Ptr<Packet> newPacket = Create<Packet>(aggregate_payload, 200);
                        TcpHeader out_tcpHeader = tcpHeader;
                        out_tcpHeader.SetSourcePort(LeaderTuple.second);
                        // out_tcpHeader.SetSequenceNumber(AggregateSeq);
                        newPacket->AddHeader(out_tcpHeader);
                        uint32_t ret = newPacket->GetSize();
                        Address addr = ipItem->GetAddress();
                        uint16_t protocol = ipItem->GetProtocol();
                        Ipv4Header newheader;
                        newheader.SetSource(LeaderTuple.first);
                        newheader.SetDestination(dst_info.first);
                        newheader.SetProtocol(6);
                        newheader.SetIdentification(0);
                        newheader.SetTtl(64);
                        newheader.SetPayloadSize(ret);
                        const Ipv4Header & header = newheader;
                        Ptr<Ipv4QueueDiscItem> itemptr = Create<Ipv4QueueDiscItem>(newPacket, addr, protocol, header);
                        ipItem = itemptr;
                        int x = hasSent[0];
                        //std::cout << "send seq : " << pkt.seq << "worker" << x << "\n";
                        Time averageOccupy;
                        int aggregate_count = 0;
                        for (auto times : timeMap){
                            if (times.second.second){
                                averageOccupy += times.second.first;
                                aggregate_count++;
                            }
                        }
                        averageOccupy = averageOccupy / aggregate_count;
                        // std::cout << "*********averageOccupy********* : " << averageOccupy << "\n";
                        // std::cout << "********* # of aggregate seq********* : " << aggregate_count << "\n";
                    }
                    else {
                        return false;
                    }
                }
            }
        }
    }

    ///////////////////////////// enqueue
    if (SendorNot){
        m_stats.nTotalReceivedPackets++;
        m_stats.nTotalReceivedBytes += ipItem->GetSize();

        retval = DoEnqueue(ipItem);

        if (retval)
        {
            ipItem->SetTimeStamp(Simulator::Now());
        }
        else{
            // std::cout << "drop!\n";
        }
    }
    // DoEnqueue may return false because:
    // 1) the internal queue is full
    //    -> the DropBeforeEnqueue method of this queue disc is automatically called
    //       because QueueDisc::AddInternalQueue sets the trace callback
    // 2) the child queue disc dropped the packet
    //    -> the DropBeforeEnqueue method of this queue disc is automatically called
    //       because QueueDisc::AddQueueDiscClass sets the trace callback
    // 3) it dropped the packet
    //    -> DoEnqueue has to explicitly call DropBeforeEnqueue
    // Thus, we do not have to call DropBeforeEnqueue here.

    // check that the received packet was either enqueued or dropped
    NS_ASSERT(m_stats.nTotalReceivedPackets ==
              m_stats.nTotalDroppedPacketsBeforeEnqueue + m_stats.nTotalEnqueuedPackets);
    NS_ASSERT(m_stats.nTotalReceivedBytes ==
              m_stats.nTotalDroppedBytesBeforeEnqueue + m_stats.nTotalEnqueuedBytes);

    return retval;
}

bool
AggregateQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    if (GetCurrentSize() + item > GetMaxSize())
    {
        NS_LOG_LOGIC("Queue full -- dropping pkt");
        DropBeforeEnqueue(item, LIMIT_EXCEEDED_DROP);
        return false;
    }

    bool retval = GetInternalQueue(0)->Enqueue(item);

    // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
    // internal queue because QueueDisc::AddInternalQueue sets the trace callback

    NS_LOG_LOGIC("Number packets " << GetInternalQueue(0)->GetNPackets());
    NS_LOG_LOGIC("Number bytes " << GetInternalQueue(0)->GetNBytes());

    return retval;
}

Ptr<QueueDiscItem>
AggregateQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    Ptr<QueueDiscItem> item = GetInternalQueue(0)->Dequeue();

    if (!item)
    {
        NS_LOG_LOGIC("Queue empty");
        return nullptr;
    }

    return item;
}

Ptr<const QueueDiscItem>
AggregateQueueDisc::DoPeek()
{
    NS_LOG_FUNCTION(this);

    Ptr<const QueueDiscItem> item = GetInternalQueue(0)->Peek();

    if (!item)
    {
        NS_LOG_LOGIC("Queue empty");
        return nullptr;
    }

    return item;
}

bool
AggregateQueueDisc::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("AggregateQueueDisc cannot have classes");
        return false;
    }

    if (GetNPacketFilters() > 0)
    {
        NS_LOG_ERROR("AggregateQueueDisc needs no packet filter");
        return false;
    }

    if (GetNInternalQueues() == 0)
    {
        // add a DropTail queue
        AddInternalQueue(
            CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>("MaxSize",
                                                                     QueueSizeValue(GetMaxSize())));
    }

    if (GetNInternalQueues() != 1)
    {
        NS_LOG_ERROR("AggregateQueueDisc needs 1 internal queue");
        return false;
    }

    return true;
}

void
AggregateQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);
}

} // namespace ns3
