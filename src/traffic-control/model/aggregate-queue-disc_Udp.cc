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
#include "ns3/udp-header.h"
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
int retrans_count = 0;
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
        long long int payload = 0;

        aggregate_pkt(uint8_t *_payload){
            for (int i = 0; i < 4; i++) {
                payload = (payload << 8) | _payload[i];
            }
        }
        ~aggregate_pkt(){

        }

};

////////////////////////////////////////////////////// global var
uint32_t WorkerNum = 4; // Workers per aggregate
uint32_t QueueLength = 50; // max packet number in aggregate table per worker
uint32_t AggregateSsh = 4; // threshold of aggregate
std::map<std::pair<Ipv4Address, uint16_t>, uint32_t> Workers_Tuple; // mapping ip/port to Worker number
std::map<uint32_t, std::pair<Ipv4Address, uint16_t>> Workers_Num; // mapping Worker number to ip/port
std::vector<std::vector<long long>> AggregateTable; // first row is seq, others are each worker's payload
std::map<SequenceNumber32, std::bitset<4>> CountMap; // map from seq to count
SequenceNumber32 AggregateSeq;
SequenceNumber32 AggregateAck;
uint8_t AggregateCount[1];
std::vector<uint32_t> aggregate_distribute[4];
std::vector<int> directlySent;
int payloadSize = 200;
int totalSend = 0;
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
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("QueueLength",
                          "The length of buffer",
                          UintegerValue(50),
                          MakeUintegerAccessor(&AggregateQueueDisc::SetQueueLength, &AggregateQueueDisc::GetQueueLength),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

AggregateQueueDisc::AggregateQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
    NS_LOG_FUNCTION(this);
    for (int i = 0; i < WorkerNum; i++){
        for (int j = 0; j < WorkerNum; j++){
            aggregate_distribute[i].push_back(0);
        }
    }
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

void AggregateQueueDisc::SetQueueLength(uint32_t Length){
    QueueLength = Length;
}

uint32_t AggregateQueueDisc::GetQueueLength() const{
    return QueueLength;
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
    if (pos < 1 || pos > 4) {
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
    std::cout << "setAllBit!" << "\n";
    for(int i = 1; i <= WorkerNum; i++){
        setBit(buffer, i);
    }
}

void clearBuffer(uint8_t* buffer) {
    buffer[0] = 0;
}

bool UpdateTable(aggregate_pkt pkt, u_int32_t Worker){
    // put pkt into table
    if (!AggregateTable.empty()){ // if table not empty
        std::cout << "table not empty!" << "\n";
        uint32_t ColumnNum = QueueLength;
        for (int i = 0; i < AggregateTable[0].size(); i++){ // check if seq in table
            if (AggregateTable[0][i] == (pkt.payload)){
                ColumnNum = i;
                break;
            }
        }
        if (ColumnNum < QueueLength){ // if in
            std::cout << "seq in table!" << "\n";
            AggregateTable[Worker][ColumnNum] = pkt.payload;
        }
        else{ // if not in
            std::cout << "seq not in table!" << "\n";
            uint32_t NowSize = AggregateTable[0].size();
            if (NowSize < QueueLength){ // if table not full
                for (int i = 0; i < WorkerNum + 1; i++){
                    AggregateTable[i].push_back(-1);
                }
                AggregateTable[0][NowSize] = pkt.payload;
                AggregateTable[Worker][NowSize] = pkt.payload;
            }
            else {
                std::cout << "Table full!" << "\n";
                return false;
            }
        }
        return true;
    }
    else { // if table empty
        std::cout << "table empty!" << "\n";
        for (int i = 0; i < WorkerNum + 1; i++){
            AggregateTable.push_back(std::vector<long long>{-1});
        }
        AggregateTable[0][0] = pkt.payload;
        AggregateTable[Worker][0] = pkt.payload;
        return true;
    }
}

std::pair<bool, long long> NeedAggregateOrNot(bool directlySent = false){
    for (int i = 0; i < AggregateTable[0].size(); i++){
        uint8_t WorkerArrive = 0;
        long long AggregatePayload = 0;
        std::vector<int> pos;
        for (int j = 1; j <= WorkerNum; j++){ // find column is need aggregate or not
            if (AggregateTable[j][i] != -1){
                AggregatePayload += AggregateTable[j][i];
                WorkerArrive++;
                pos.push_back(j);
            }
        }
        if (WorkerArrive >= AggregateSsh){ // need aggregate
            AggregateSeq = SequenceNumber32(AggregateTable[0][i]);
            for (int z = 0; z < WorkerNum; z++){
                aggregate_distribute[z][WorkerNum - 1]++;
            }
            for (int k = 0; k <= WorkerNum; k++){ // delete corresponding seq on table
                AggregateTable[k].erase(AggregateTable[k].begin() + i);
            }
            return std::pair<bool, long long> (true, AggregatePayload / WorkerNum);
        }
    }
    return std::pair<bool, long long> (false, 0); 
}

std::vector<uint32_t> DupAck(uint32_t AckNum){ // check which Worker need to be Acked
    std::vector<uint32_t> WorkersNeedAcked;
    auto HasSeq = CountMap.find(SequenceNumber32(AckNum));
    if (HasSeq != CountMap.end()){ // partial aggregation ack
        for (int i = 0; i < WorkerNum; i++){
            if (CountMap[SequenceNumber32(AckNum)][i] == 1){ // bitmap 1 mean not sent
                WorkersNeedAcked.push_back(i + 1);
            }
        }
        return WorkersNeedAcked;
    }
    for (int i = 0; i < AggregateTable[0].size(); i++){ // check if there are seq == ack on table
        if (AggregateTable[0][i] == AckNum){ // mean some one are loss
            for (int j = 1; j <= WorkerNum; j++){
                if (AggregateTable[j][i] == -1){
                    std::cout << "partial retransmition! : " << j << "\n";
                    WorkersNeedAcked.push_back(j);
                }
            }
            return WorkersNeedAcked;
        }
    }
    for (int i = 1; i <= WorkerNum; i++){ // mean no loss
        WorkersNeedAcked.push_back(i);
    }
    return WorkersNeedAcked;
}

bool UpdateMap(aggregate_pkt pkt, u_int32_t Worker){
    std::cout << "UpdateMap!\n";
    SequenceNumber32 seq = SequenceNumber32(pkt.payload);
    if (CountMap[seq][Worker - 1] == 1){
        std::cout << "diractly sent!\n";
        CountMap[seq][Worker - 1] = 0;
        if (CountMap[seq].none()){
            CountMap.erase(seq);
        }
        return true; // change src port & ip then sent
    }
    return false;
}

bool
AggregateQueueDisc::Enqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    Ptr<Packet> copy = item->GetPacket()->Copy();
    Ptr<Ipv4QueueDiscItem> ipItem = DynamicCast<Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();
    TcpHeader tcpHeader;
    UdpHeader udpHeader;
    Mycount1++;
    bool retval = false;
    bool SendorNot = true;
    SequenceNumber32 now_seq = SequenceNumber32(1);
    int globalPayload = 0;

    // // NS_LOG_INFO("going from " << ipHeader.GetSource() << " to " << ipHeader.GetDestination());
    // if ((Mycount1 == 200)){
    //     std::cout << "going from " << ipHeader.GetSource() << " to " << ipHeader.GetDestination() << "\n";
    //     return false;
    // }

    if (Mycount1 % 1 == 0){
        if (copy->PeekHeader(udpHeader) != 0) {
            copy->RemoveHeader(udpHeader);
            uint8_t *buffer = new uint8_t[copy->GetSize()];
            copy->CopyData(buffer, copy->GetSize ());
            // get 4-tuple
            std::pair<Ipv4Address, uint16_t> dst_info = std::pair<Ipv4Address, uint16_t> (ipHeader.GetDestination(), udpHeader.GetDestinationPort());
            std::pair<Ipv4Address, uint16_t> src_info = std::pair<Ipv4Address, uint16_t> (ipHeader.GetSource(), udpHeader.GetSourcePort());
            
            if (1){ // not deal with 3-way handshaking & control msg
                clearBuffer(AggregateCount);
                // check worker order
                u_int32_t Worker = MatchWorker(src_info); // TODO : 之後可能可以有多個receiver
                if (Worker > WorkerNum){
                    std::cout << "Worker more than prediction" << "\n";
                    return false;
                }
                // create object for incoming packets
                aggregate_pkt pkt(buffer);
                globalPayload = pkt.payload;
                // check if seq in CountMap
                // for (const auto& row : AggregateTable) { // print table
                //     for (int value : row) {
                //         std::cout << value << " ";
                //     }
                //     std::cout << std::endl;
                // }
                // check if need aggregate
                std::pair<bool, long long> AggregateParam;
                auto it = std::find(directlySent.begin(), directlySent.end(), pkt.payload);
                if (it != directlySent.end() || !UpdateTable(pkt, Worker)){
                    AggregateParam = std::pair<bool, long long> (true, pkt.payload);
                    directlySent.push_back(pkt.payload);
                    setBit(AggregateCount, Worker);
                }
                else{
                    AggregateParam = NeedAggregateOrNot();
                    setAllBit(AggregateCount);
                }
                if (AggregateParam.first){
                    int wk = AggregateCount[0];
                    std::cout << "send seq : " << AggregateParam.second << " worker : " << wk << "\n";
                    std::pair<Ipv4Address, uint16_t> LeaderTuple = Workers_Num[1];
                    // change int to uint8_t
                    uint8_t aggregate_payload[200]; 
                    aggregate_payload[0] = AggregateCount[0];
                    aggregate_payload[1] = 0;
                    aggregate_payload[2] = (AggregateParam.second >> 8) & 0xFF;
                    aggregate_payload[3] = AggregateParam.second & 0xFF;
                    // create new pkt
                    Ptr<Packet> newPacket = Create<Packet>(aggregate_payload, 200);
                    UdpHeader out_udpHeader = udpHeader;
                    out_udpHeader.SetSourcePort(LeaderTuple.second);
                    out_udpHeader.ForcePayloadSize(8 + payloadSize);
                    newPacket->AddHeader(out_udpHeader);
                    uint32_t ret = newPacket->GetSize();
                    Address addr = ipItem->GetAddress();
                    uint16_t protocol = ipItem->GetProtocol();
                    Ipv4Header newheader;
                    newheader.SetSource(LeaderTuple.first);
                    newheader.SetDestination(dst_info.first);
                    newheader.SetProtocol(17);
                    newheader.SetIdentification(0);
                    newheader.SetTtl(64);
                    newheader.SetPayloadSize(ret);
                    const Ipv4Header & header = newheader;
                    Ptr<Ipv4QueueDiscItem> itemptr = Create<Ipv4QueueDiscItem>(newPacket, addr, protocol, header);
                    ipItem = itemptr;
                }
                else {
                    return false;
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
            totalSend++;
            std::cout << "Total Send : " << totalSend << "\n";
        }
        else{
            std::cout << "drop2 : " << globalPayload << "\n";
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
