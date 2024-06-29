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
        int metaSeq = 0;

        aggregate_pkt(TcpHeader _tcpHeader, uint8_t *_payload){
            seq = _tcpHeader.GetSequenceNumber();
            for (int i = 0; i < 4; i++) { //以後改4~199
                payload = (payload << 8) | _payload[i];
            }
            for (int i = 0; i < 4; i++) {
                metaSeq = (metaSeq << 8) | _payload[i];
            }
        }
        aggregate_pkt(){
            seq = SequenceNumber32(0);
            payload = 0;
            metaSeq = 0;
        }
        ~aggregate_pkt(){

        }

};

////////////////////////////////////////////////////// global var
uint32_t WorkerNum = 10; // Workers per aggregate
uint32_t QueueLength = 10; // max packet number in aggregate table per worker
double QueueSsh = 0.95; // threshold of buffer overflow directed sent
uint32_t AggregateSsh = 10; // threshold of aggregate
std::map<std::pair<Ipv4Address, uint16_t>, uint32_t> Workers_Tuple; // mapping ip/port to Worker number
std::map<uint32_t, std::pair<Ipv4Address, uint16_t>> Workers_Num; // mapping Worker number to ip/port
std::vector<std::vector<long long>> AggregateTable; // first row is seq, others are each worker's payload
std::map<SequenceNumber32, std::bitset<10>> CountMap; // map from seq to count
std::map<int, std::vector<aggregate_pkt>> AggregateMap; // map from metaSeq to pkts; 0~WorkerNum-1
std::map<int, std::map<int, int>> SentMap; // map from worker to already send pkts <ack metaSeq, ack num>; 1~WorkerNum
std::map<int, int> Worker_metaSeq; // the biggest metaSeq worker has sent
std::map<int, int> Worker_seq; // the biggest Seq worker has sent
std::map<int, std::vector<int>> Worker_dropSeq; // the Seqs worker be droped
SequenceNumber32 AggregateSeq;
SequenceNumber32 AggregateAck = SequenceNumber32(1);
int AggregateMetaSeq = 0;
SequenceNumber32 LastSentSeq;
int psSeq = 1;
uint8_t hasSent[2];
std::map<SequenceNumber32, std::pair<Time, bool>> timeMap;
int count = 0;
int ackSeq = 1;
int payloadSize = 200;
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

bool UpdateTable(aggregate_pkt pkt, u_int32_t Worker){
    // update Worker_metaSeq
    if (Worker_metaSeq.find(Worker) != Worker_metaSeq.end()){
        if (Worker_metaSeq[Worker] < pkt.metaSeq){
            Worker_metaSeq[Worker] = pkt.metaSeq;
        }
    }
    else {
        Worker_metaSeq[Worker] = pkt.metaSeq;
    }
    // update Worker_seq
    if (Worker_seq.find(Worker) != Worker_seq.end()){
        if (Worker_seq[Worker] < pkt.seq.GetValue()){
            Worker_seq[Worker] = pkt.seq.GetValue();
        }
    }
    else {
        Worker_seq[Worker] = pkt.seq.GetValue();
    }
    // put pkt into table
    if (AggregateMap.find(pkt.metaSeq) != AggregateMap.end()){ // if in
        std::cout << "seq in table!" << "\n";
        AggregateMap[pkt.metaSeq][Worker - 1] = pkt;
    }
    else{ // if not in
        std::cout << "seq not in table!" << "\n";
        if ((AggregateAck.GetValue() > pkt.seq.GetValue()) && (AggregateMetaSeq > pkt.metaSeq)){ // don't care
            std::cout << "don't care!" << "\n";
            if (Worker_dropSeq.find(Worker) != Worker_dropSeq.end()){
                Worker_dropSeq[Worker].push_back(pkt.seq.GetValue());
            }
            else{
                std::vector<int> vec;
                vec.push_back(pkt.seq.GetValue());
                Worker_dropSeq[Worker] = vec;
            }
            return true;
        }
        uint32_t NowSize = AggregateMap.size();
        if (NowSize < QueueLength){ // if table not full
            std::vector<aggregate_pkt> temp(10);
            temp[Worker - 1] = pkt;
            AggregateMap[pkt.metaSeq] = temp;
        }
        else {
            std::cout << "Table full! " << drop_count << "\n";
            return false;
        }
    }
    return true;
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
    for(int i = 1; i <= 10; i++){
        setBit(buffer, i);
    }
}

std::pair<bool, long long> NeedAggregateOrNot(bool directlySent = false){
    if (directlySent){
        long long AggregatePayload = 0;
        std::vector<int> pos;
        for (int i = 0; i < WorkerNum; i++){
            if (AggregateMap.begin()->second[i].seq.GetValue() != 0){ // who sent
                AggregatePayload += AggregateMap.begin()->second[i].payload;
                pos.push_back(i);
                SentMap[i + 1][AggregateMap.begin()->first + 1] = AggregateMap.begin()->second[i].seq.GetValue() + payloadSize;
            }
        }
        AggregateSeq = SequenceNumber32((AggregateMap.begin()->first * payloadSize) + 1);
        if (AggregateMetaSeq < AggregateMap.begin()->first){
            AggregateMetaSeq = AggregateMap.begin()->first;
        }
        for (auto num : pos){
            setBit(hasSent, num + 1);
        }
        AggregateMap.erase(AggregateMap.begin()->first); // delete corresponding seq on table
        // std::cout << "============AggregateMap: " << AggregateAck << "==============\n";
        // for (auto vec : AggregateMap){
        //     std::cout << vec.first << " : \t";
        //     for (auto pkt : vec.second){
        //         std::cout << pkt.seq << "\t";
        //     }
        //     std::cout << "\n";
        // }
        // std::cout << "=======================================\n";
        // std::cout << "================SentMap=================\n";
        // for (auto vec : SentMap){
        //     std::cout << vec.first << " : \t";
        //     for (auto pair : vec.second){
        //         std::cout << "(" << pair.first << ", " << pair.second << ")" << "\t";
        //     }
        //     std::cout << "\n";
        // }
        // std::cout << "=======================================\n";
        return std::pair<bool, long long> (true, AggregatePayload);
    }
    for (auto vec : AggregateMap){ // fully aggregate
        uint8_t WorkerArrive = 0;
        long long AggregatePayload = 0;
        for (auto pkts : vec.second){ // find column is need aggregate or not
            if (pkts.seq.GetValue() != 0){
                AggregatePayload += pkts.payload;
                WorkerArrive++;
            }
            else {
                break;
            }
        }
        if (WorkerArrive >= AggregateSsh){ // need aggregate
            AggregateSeq = SequenceNumber32((vec.first * payloadSize) + 1);
            if (AggregateMetaSeq < vec.first){
                AggregateMetaSeq = vec.first;
            }
            setAllBit(hasSent);
            for (int i = 1; i <= 10; i++){
                SentMap[i][vec.first + 1] = vec.second[i - 1].seq.GetValue() + payloadSize;
            }
            AggregateMap.erase(vec.first);
            // std::cout << "============AggregateMap==============\n";
            // for (auto vec : AggregateMap){
            //     std::cout << vec.first << " : \t";
            //     for (auto pkt : vec.second){
            //         std::cout << pkt.seq << "\t";
            //     }
            //     std::cout << "\n";
            // }
            // std::cout << "=======================================\n";
            // std::cout << "================SentMap=================\n";
            // for (auto vec : SentMap){
            //     std::cout << vec.first << " : \t";
            //     for (auto pair : vec.second){
            //         std::cout << "(" << pair.first << ", " << pair.second << ")" << "\t";
            //     }
            //     std::cout << "\n";
            // }
            // std::cout << "=======================================\n";
            return std::pair<bool, long long> (true, AggregatePayload);
        }
    }
    return std::pair<bool, long long> (false, 0); 
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

void clearBuffer(uint8_t* buffer) {
    buffer[0] = 0;
    buffer[1] = 0;
}

std::pair<int, int> GetAck(int worker, int ackNum){
    std::vector<int> temp;
    for (auto pairs : SentMap[worker]){
        int correspondingMeta = (ackNum - 1) / 200;
        if (correspondingMeta == pairs.first){
            for(auto meta : temp){
                SentMap[worker].erase(meta);
            }
            if (true){ // syncornize metaSeq
                auto maxPairIter = std::max_element(Worker_metaSeq.begin(), Worker_metaSeq.end(),
                                        [](const auto& a, const auto& b) {
                                            return a.second < b.second;
                                        });
                return std::pair<int, int>(maxPairIter->second, pairs.second);
            }
            return pairs;
        }
        else if (correspondingMeta >= pairs.first){
            temp.push_back(pairs.first);
        }
    }
    if (!Worker_dropSeq[worker].empty()){
        auto maxPairIter = std::max_element(Worker_metaSeq.begin(), Worker_metaSeq.end(),
                                [](const auto& a, const auto& b) {
                                    return a.second < b.second;
                                });
        int ack = Worker_dropSeq[worker][0] + payloadSize;
        Worker_dropSeq[worker].erase(Worker_dropSeq[worker].begin());
        return std::pair<int, int>(maxPairIter->second, ack);
    }
    else {
        return std::pair<int, int>(0, 0);
    }
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
    SequenceNumber32 now_seq = SequenceNumber32(1);

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
                    for (int WorkerNeedAcked = 1; WorkerNeedAcked <= WorkerNum; WorkerNeedAcked++){
                        std::pair<int, int> ackData = GetAck(WorkerNeedAcked, ack_num);
                        if (ackData.second == 0){
                            continue;
                        }
                        TcpHeader newTCPheader = tcpHeader;
                        uint8_t ack_payload[4]; 
                        ack_payload[0] = 0;
                        ack_payload[1] = 0;
                        ack_payload[2] = (ackData.first >> 8) & 0xFF;
                        ack_payload[3] = ackData.first & 0xFF;
                        Ptr<Packet> newPacket = Create<Packet>(ack_payload, 4);
                        newTCPheader.SetDestinationPort(Workers_Num[WorkerNeedAcked].second);
                        newTCPheader.SetSequenceNumber(SequenceNumber32(1));
                        newTCPheader.SetAckNumber(SequenceNumber32(ackData.second));
                        newPacket->AddHeader(newTCPheader);
                        uint32_t ret = newPacket->GetSize();
                        Address addr = ipItem->GetAddress();
                        uint16_t protocol = ipItem->GetProtocol();
                        Ipv4Header newheader;
                        newheader.SetSource(src_info.first);
                        newheader.SetDestination(Workers_Num[WorkerNeedAcked].first);
                        newheader.SetProtocol(6);
                        newheader.SetIdentification(0);
                        newheader.SetTtl(64);
                        newheader.SetPayloadSize(ret);
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
                    // ackSeq += 4;
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
                    std::cout << "Worker : " << Worker << " receive seq: " << pkt.seq << " meta: " << pkt.metaSeq << " !" << "\n";
                    // check if seq in CountMap
                    auto it = CountMap.find(pkt.seq);
                    if ((it != CountMap.end())){ // seq in CountMap
                        std::cout << "CountMap has seq!" << "\n";
                        return false;
                    }
                    else { // seq not in CountMap
                        // check if need aggregate
                        std::pair<bool, long long> AggregateParam;
                        if (!UpdateTable(pkt, Worker)){
                            AggregateParam = NeedAggregateOrNot(true);
                            UpdateTable(pkt, Worker);
                        }
                        else{
                            AggregateParam = NeedAggregateOrNot();
                        }
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
                            out_tcpHeader.SetSequenceNumber(AggregateSeq);
                            out_tcpHeader.SetAckNumber(SequenceNumber32(1));
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
            std::cout << "drop2 : " << now_seq << "\n";
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
