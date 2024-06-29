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
#include <algorithm>


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
        int wid = 0;

        aggregate_pkt(TcpHeader _tcpHeader, uint8_t *_payload){
            seq = _tcpHeader.GetSequenceNumber();
            for (int i = 0; i < 4; i++) { //以後改4~199
                payload = (payload << 8) | _payload[i];
            }
            for (int i = 0; i < 4; i++) {
                wid = (wid << 8) | _payload[i];
            }
        }
        aggregate_pkt(){
            seq = SequenceNumber32(0);
            payload = 0;
            wid = 0;
        }
        ~aggregate_pkt(){

        }

};

class chase_set {
    public:
        bool chaseFlag = false;
        int chaseWid = 0;
        int chaseGap = 0;
        int lastBiggest = 0;
        chase_set(){
            chaseFlag = false;
            chaseWid = 0;
            chaseGap = 0;
            lastBiggest = 0;
        }
        ~chase_set(){
            
        }
};
////////////////////////////////////////////////////// global var
uint32_t WorkerNum = 10; // Workers per aggregate
int QueueLength = 50; // max packet number in aggregate table per worker
uint32_t AggregateSsh = 10; // threshold of aggregate
std::map<std::pair<Ipv4Address, uint16_t>, uint32_t> Workers_Tuple; // mapping ip/port to Worker number
std::map<uint32_t, std::pair<Ipv4Address, uint16_t>> Workers_Num; // mapping Worker number to ip/port
std::vector<std::vector<long long>> AggregateTable; // wid / seq_1 / seq_2 / ... / seq_Workers_Num / total weight / count
std::map<SequenceNumber32, std::bitset<10>> CountMap; // map from seq to count
std::vector<chase_set> Worker_chase(WorkerNum + 1); // times that worker has chased
std::vector<std::vector<long long>>::iterator min_wid;
std::vector<std::vector<long long>>::iterator now_wid;
std::vector<std::vector<long long>>::iterator max_wid;
std::vector<std::vector<long long>> diractlySentWid;
SequenceNumber32 AggregateSeq;
int AggregateAck = 0;
int dupAckCount = 0;
uint8_t AggregateCount;
SequenceNumber32 LastSentSeq;
int psSeq = 1;
uint8_t hasSent[2];
std::vector<std::array<uint8_t, 2>> hasSentVector;
std::map<SequenceNumber32, std::pair<Time, bool>> timeMap;
int count = 0;
int payloadSize = 200;
int totalPktNum = 20000;
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

void clearBuffer(uint8_t* buffer) {
    buffer[0] = 0;
    buffer[1] = 0;
}

bool UpdateDirectly(int wid, long long payload){ // find iterator in diractlySentWid
    for (auto it = diractlySentWid.begin(); it != diractlySentWid.end(); it++){
        if ((*it)[0] == wid){
            if ((*it)[2] < WorkerNum){
                (*it)[1] += payload;
                (*it)[2]++;
                return true;
            }
            else{
                return false;
            }
        }
    }
    return false;
}

bool CheckDirectly(int wid){
    for (auto it = diractlySentWid.begin(); it != diractlySentWid.end(); it++){
        if ((*it)[0] == wid - 1){
            diractlySentWid.erase(std::remove_if(diractlySentWid.begin(), diractlySentWid.end(), [wid](std::vector<long long int> x) { return x[0] < wid; }), diractlySentWid.end());
            return true;
        }
    }
    diractlySentWid.erase(std::remove_if(diractlySentWid.begin(), diractlySentWid.end(), [wid](std::vector<long long int> x) { return x[0] < wid; }), diractlySentWid.end());
    return false;
}

void Chase(aggregate_pkt pkt, int Worker){
    if (pkt.wid >= Worker_chase[Worker].chaseWid){
        if (!Worker_chase[Worker].chaseFlag && Worker_chase[Worker].chaseGap == 0){
            std::cout << "chase probe!\n";
            Worker_chase[Worker].chaseWid = (*max_wid)[0];
            Worker_chase[Worker].chaseFlag = true;
            Worker_chase[Worker].lastBiggest = (*max_wid)[0];
            std::cout << "chase " << Worker_chase[Worker].chaseWid << "!\n";
        }
        else if (Worker_chase[Worker].chaseFlag){
            std::cout << "chase start!\n";
            Worker_chase[Worker].chaseGap = (*max_wid)[0] - Worker_chase[Worker].lastBiggest;
            Worker_chase[Worker].chaseWid = (*max_wid)[0] + Worker_chase[Worker].chaseGap;
            Worker_chase[Worker].lastBiggest = (*max_wid)[0];
            std::cout << "chase " << Worker_chase[Worker].chaseWid << "!\n";
        }
        else {
            std::cout << "chase get!\n";
            Worker_chase[Worker].chaseWid = (*max_wid)[0] + Worker_chase[Worker].chaseGap;
            Worker_chase[Worker].lastBiggest = (*max_wid)[0];
            Worker_chase[Worker].chaseFlag = true;
            std::cout << "chase " << Worker_chase[Worker].chaseWid << "!\n";
        }
    }
}

std::pair<std::vector<std::pair<int, int>>, std::vector<std::pair<int, long long>>> UpdateTable(aggregate_pkt pkt, u_int32_t Worker){
    std::cout << "*****************************\n";
    for(auto vec : AggregateTable){
        for (auto num : vec){
            std::cout << num << "\t";
        }
        if(vec[0] == (*min_wid)[0]){
            std::cout << "<- min_wid";
        }
        if(vec[0] == (*max_wid)[0]){
            std::cout << "<- max_wid";
        }
        if(vec[0] == (*now_wid)[0]){
            std::cout << "<- now_wid";
        }
        std::cout << "\n";
    }
    std::cout << "*****************************\n";
    std::vector<std::pair<int, int>> ackList;
    std::vector<std::pair<int, long long>> pktList;
    // put pkt into table
    if (!AggregateTable.empty()){ // if table not empty
        std::cout << "table not empty!" << "\n";
        bool isDiactly = UpdateDirectly(pkt.wid, pkt.payload);
        if ((*max_wid)[0] - pkt.wid > QueueLength / 2){
            Chase(pkt, Worker);
        }
        if (pkt.wid < (*min_wid)[0]){
            // Directly sent ACK
            ackList.push_back({Worker, pkt.seq.GetValue()});
            if (isDiactly){
                // Directly sent Pkt
                pktList.push_back({pkt.wid, pkt.payload});
                setBit(hasSent, Worker);
                hasSentVector.push_back({hasSent[0], hasSent[1]});
                clearBuffer(hasSent);
            }
            return {ackList, pktList};
        }
        else if (pkt.wid > (*max_wid)[0]){ // new seq
            std::cout << "new seq!\n";
            auto gap = pkt.wid - (*max_wid)[0];
            if ((*max_wid)[0] == 0){
                gap = 1;
            }
            auto temp_wid = max_wid;
            for (int i = 1; i <= gap; i++){
                if ((temp_wid + 1) == AggregateTable.end()){
                    temp_wid = AggregateTable.begin();
                }
                else {
                    temp_wid++;
                }
                if (temp_wid == min_wid){
                    // overflow
                    std::cout << "buffer overflow!\n";
                    if (min_wid == now_wid){
                        // Directly sent min_wid
                        pktList.push_back({(*min_wid)[0], (*min_wid)[WorkerNum + 1]});
                        for (int j = 1; j <= 10; j++){
                            if ((*min_wid)[j] != 0){
                                setBit(hasSent, j);
                            }
                        }
                        hasSentVector.push_back({hasSent[0], hasSent[1]});
                        clearBuffer(hasSent);
                    }
                    // Update diractlySentWid
                    diractlySentWid.push_back({(*min_wid)[0], (*min_wid)[WorkerNum + 1], (*min_wid)[WorkerNum + 2]});
                    // std::cout << "++++++++diractlySentWid++++++++\n";
                    // for(auto vec : diractlySentWid){
                    //     for (auto num : vec){
                    //         std::cout << num << "\t";
                    //     }
                    //     std::cout << "\n";
                    // }
                    // std::cout << "++++++++++++++++++++++++++++++\n";
                    // Directly sent ACK
                    for (int j = 1; j <= 10; j++){
                        if ((*min_wid)[j] != 0){
                            ackList.push_back({j, (*min_wid)[j]});
                        }
                    }
                    // Update min_wid
                    (*min_wid) = std::vector<long long>(3 + WorkerNum);
                    if ((min_wid + 1) == AggregateTable.end()){
                        if(min_wid == now_wid){
                            now_wid = AggregateTable.begin();
                        }
                        if(min_wid == max_wid){
                            max_wid = AggregateTable.begin();
                        }
                        min_wid = AggregateTable.begin();
                    }
                    else {
                        if(min_wid == now_wid){
                            now_wid++;
                        }
                        if(min_wid == max_wid){
                            max_wid++;
                        }
                        min_wid++;
                    }
                }
                if (i == gap){ // new max_wid
                    (*temp_wid)[0] = pkt.wid;
                    (*temp_wid)[Worker] = pkt.seq.GetValue();
                    (*temp_wid)[WorkerNum + 1] = pkt.payload;
                    (*temp_wid)[WorkerNum + 2] = 1;
                    max_wid = temp_wid;
                }
                else{ // set 0
                    std::cout << "set 0!\n";
                    (*temp_wid)[0] = (*max_wid)[0] + i;
                    // std::cout << "*****************************\n";
                    // for(auto vec : AggregateTable){
                    //     for (auto num : vec){
                    //         std::cout << num << "\t";
                    //     }
                    //     if(vec[0] == (*min_wid)[0]){
                    //         std::cout << "<- min_wid";
                    //     }
                    //     if(vec[0] == (*max_wid)[0]){
                    //         std::cout << "<- max_wid";
                    //     }
                    //     if(vec[0] == (*now_wid)[0]){
                    //         std::cout << "<- now_wid";
                    //     }
                    //     std::cout << "\n";
                    // }
                    // std::cout << "*****************************\n";
                }
            }
            return {ackList, pktList};
        }
        else{
            Worker_chase[Worker].chaseFlag = false;
            for (auto it = min_wid; ; ){
                auto next = it;
                if ((next + 1) != AggregateTable.end()){
                    if (next != max_wid){
                        next++;
                    }
                }
                else{
                    if (next != max_wid){
                        next = AggregateTable.begin();
                    }
                }
                // std::cout << "(*it)[0] == pkt.wid => " << ((*it)[0] == pkt.wid) << "\n";
                if (((*it)[0] == pkt.wid)){
                    if ((*it)[WorkerNum + 2] != WorkerNum){
                        if ((*it)[Worker] == 0){
                            (*it)[WorkerNum + 1] += pkt.payload;
                            (*it)[WorkerNum + 2]++;
                        }
                        (*it)[Worker] = pkt.seq.GetValue();
                    }
                    if ((*it)[WorkerNum + 2] == WorkerNum && (*it)[0] >= (*now_wid)[0]){
                        // Aggergate
                        std::cout << "Aggergate!\n";
                        // Sent aggregate pkt
                        for (auto wid = now_wid; ;){
                            pktList.push_back({(*wid)[0], (*wid)[WorkerNum + 1]});
                            for (int j = 1; j <= 10; j++){
                                if ((*wid)[j] != 0){
                                    setBit(hasSent, j);
                                }
                            }
                            hasSentVector.push_back({hasSent[0], hasSent[1]});
                            clearBuffer(hasSent);
                            if (wid == it){
                                break;
                            }
                            if ((wid + 1) == AggregateTable.end()){
                                wid = AggregateTable.begin();
                            }
                            else {
                                wid++;
                            }
                        }
                        now_wid = next;
                        setAllBit(hasSent);
                        return {ackList, pktList};
                    }
                    else{
                        return {ackList, pktList};
                    }
                }
                if (it == max_wid){
                    break;
                }
                it = next;
            }
            return {ackList, pktList};
        }
    }
    else { // if table empty
        std::cout << "table empty!" << "\n";
        for (int i = 0; i < QueueLength; i++){
            AggregateTable.push_back(std::vector<long long>(3 + WorkerNum));
        }
        min_wid = now_wid = max_wid = AggregateTable.begin();
        (*min_wid)[0] = pkt.wid;
        (*min_wid)[Worker] = pkt.seq.GetValue();
        (*min_wid)[WorkerNum + 1] = pkt.payload;
        (*min_wid)[WorkerNum + 2] = 1;
        return {ackList, pktList};
    }
}

std::vector<std::pair<int, int>> PacketAcked(int ackWid){
    std::cout << "PacketAcked :" << ackWid << "\n";
    std::vector<std::pair<int, int>> ackList;
    if (CheckDirectly(ackWid)){
        std::cout << "in directly map, ignore\n";
        return ackList;
    }
    for (auto it = min_wid; ; ){
        if ((*it)[0] < ackWid){
            for (int i = 1; i <= WorkerNum; i++){
                if ((*it)[i] != 0){
                    ackList.push_back({i, (*it)[i]});
                }
            }
            (*it) = std::vector<long long>(3 + WorkerNum);
        }
        else{
            return ackList;
        }
        if (it == max_wid){
            return ackList;
        }
        if ((it + 1) != AggregateTable.end()){
            it++;
        }
        else{
            it = AggregateTable.begin();
        }
        min_wid = it;
    }
    std::cout << "*****************************\n";
    for(auto vec : AggregateTable){
        for (auto num : vec){
            std::cout << num << "\t";
        }
        if(vec[0] == (*min_wid)[0]){
            std::cout << "<- min_wid";
        }
        if(vec[0] == (*max_wid)[0]){
            std::cout << "<- max_wid";
        }
        if(vec[0] == (*now_wid)[0]){
            std::cout << "<- now_wid";
        }
        std::cout << "\n";
    }
    std::cout << "*****************************\n";
}

std::vector<std::pair<int, long long>> Retransmission(int ackWid){
    std::cout << "Retransmission!\n";
    std::vector<std::pair<int, long long>> pktList;
    if (ackWid < (*min_wid)[0]){ // in directly_map
        std::cout << "in directly_map!\n";
        for (auto it = diractlySentWid.begin(); it != diractlySentWid.end(); it++){
            if ((*it)[0] == ackWid){
                pktList.push_back({(*it)[0], (*it)[1]});
                for (int j = 1; j <= (*it)[2]; j++){ // TODO 
                    setBit(hasSent, j);
                }
                hasSentVector.push_back({hasSent[0], hasSent[1]});
                clearBuffer(hasSent);
            }
        }
    }
    else{ // in aggregateTable
        std::cout << "in aggregateTable!\n";
        if ((*min_wid)[0] == ackWid){
            pktList.push_back({(*min_wid)[0], (*min_wid)[WorkerNum + 1]});
            for (int j = 1; j <= 10; j++){
                if ((*min_wid)[j] != 0){
                    setBit(hasSent, j);
                }
            }
            hasSentVector.push_back({hasSent[0], hasSent[1]});
            clearBuffer(hasSent);
        }
    }
    return pktList;
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
            int ttl = ipHeader.GetTtl();
            
            if (seq_num != 0 && !(seq_num == 1 && ack_num == 1 && copy->GetSize () == 0) && (ttl != 48)){ // not deal with 3-way handshaking & control msg
                hasSentVector.clear();
                clearBuffer(hasSent);
                if (src_info.first == "10.0.11.2") { // if packet is ACK
                    std::cout << "get ack :" << ack_num << " from " << src_info.first << " to " << dst_info.first << " TTL = " << ttl << "\n";
                    std::vector<std::pair<int, int>> ackList;
                    std::vector<std::pair<int, long long>> pktList;
                    int ackWid = (ack_num - 1) / payloadSize;
                    if (AggregateAck < ack_num){
                        AggregateAck = ack_num;
                        dupAckCount = 0;
                        ackList = PacketAcked(ackWid);
                    }
                    else if(AggregateAck == ack_num){ // dup ack
                        dupAckCount++;
                        if (dupAckCount == 3 || dupAckCount == 30){
                            // retransmission
                            pktList = Retransmission(ackWid);
                        }
                        else{ // ignore
                            return false;
                        }
                    }
                    for (auto ackData : ackList){
                        std::cout << "<= sent ack " << ackData.second + payloadSize << " to worker : " << ackData.first << "\n";
                        TcpHeader newTCPheader = tcpHeader;
                        int returnWid = std::max(ackWid, Worker_chase[ackData.first].chaseWid);
                        returnWid = std::min(returnWid, totalPktNum - QueueLength);
                        uint8_t ack_payload[4]; 
                        ack_payload[0] = 0;
                        ack_payload[1] = 0;
                        ack_payload[2] = (returnWid >> 8) & 0xFF;
                        ack_payload[3] = returnWid & 0xFF;
                        Ptr<Packet> newPacket = Create<Packet>(ack_payload, 4);
                        newTCPheader.SetDestinationPort(Workers_Num[ackData.first].second);
                        newTCPheader.SetSequenceNumber(SequenceNumber32(1));
                        newTCPheader.SetAckNumber(SequenceNumber32(ackData.second + payloadSize));
                        newPacket->AddHeader(newTCPheader);
                        uint32_t ret = newPacket->GetSize();
                        Address addr = ipItem->GetAddress();
                        uint16_t protocol = ipItem->GetProtocol();
                        Ipv4Header newheader;
                        newheader.SetSource(src_info.first);
                        newheader.SetDestination(Workers_Num[ackData.first].first);
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
                    for (auto pkt : pktList){
                        // setAllBit(hasSent);
                        //std::cout << "sent aggregate pkt!" << "\n";
                        std::pair<Ipv4Address, uint16_t> LeaderTuple = Workers_Num[1];
                        // change int to uint8_t
                        uint8_t aggregate_payload[200]; 
                        // aggregate_payload[0] = hasSent[0];
                        // aggregate_payload[1] = hasSent[1];
                        aggregate_payload[0] = hasSentVector[0][0];
                        aggregate_payload[1] = hasSentVector[0][1];
                        hasSentVector.erase(hasSentVector.begin());
                        aggregate_payload[2] = (pkt.second >> 8) & 0xFF;
                        aggregate_payload[3] = pkt.second & 0xFF;
                        // create new pkt
                        Ptr<Packet> newPacket = Create<Packet>(aggregate_payload, 200);
                        TcpHeader out_tcpHeader = tcpHeader;
                        out_tcpHeader.SetSourcePort(LeaderTuple.second);
                        out_tcpHeader.SetDestinationPort(tcpHeader.GetSourcePort());
                        out_tcpHeader.SetSequenceNumber(SequenceNumber32((pkt.first * payloadSize) + 1));
                        std::cout << "<= sent seq " << (pkt.first * payloadSize) + 1 << " to PS\n";
                        out_tcpHeader.SetAckNumber(SequenceNumber32(1));
                        newPacket->AddHeader(out_tcpHeader);
                        uint32_t ret = newPacket->GetSize();
                        Address addr = ipItem->GetAddress();
                        uint16_t protocol = ipItem->GetProtocol();
                        Ipv4Header newheader;
                        newheader.SetSource(LeaderTuple.first);
                        newheader.SetDestination(src_info.first);
                        newheader.SetProtocol(6);
                        newheader.SetIdentification(0);
                        newheader.SetTtl(50);
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
                    std::cout << "Worker : " << Worker << "; receive seq: " << pkt.seq  << "; wid : " << pkt.wid <<  " !" << "\n";
                    // check if need aggregate
                    std::pair<std::vector<std::pair<int, int>>, std::vector<std::pair<int, long long>>> AggregateParam = UpdateTable(pkt, Worker);
                    std::vector<std::pair<int, int>> ackList = AggregateParam.first;
                    std::vector<std::pair<int, long long>> pktList = AggregateParam.second;
                    int ackWid = (AggregateAck - 1) / 200;
                    for (auto ackData : ackList){
                        std::cout << "=> sent ack " << ackData.second + payloadSize << " to worker : " << ackData.first << "\n";
                        TcpHeader newTCPheader = tcpHeader;
                        int returnWid = std::max(ackWid, Worker_chase[ackData.first].chaseWid);
                        returnWid = std::min(returnWid, totalPktNum - QueueLength);
                        uint8_t ack_payload[4]; 
                        ack_payload[0] = 0;
                        ack_payload[1] = 0;
                        ack_payload[2] = (returnWid >> 8) & 0xFF;
                        ack_payload[3] = returnWid & 0xFF;
                        Ptr<Packet> newPacket = Create<Packet>(ack_payload, 4);
                        newTCPheader.SetDestinationPort(Workers_Num[ackData.first].second);
                        newTCPheader.SetSourcePort(tcpHeader.GetDestinationPort());
                        newTCPheader.SetSequenceNumber(SequenceNumber32(1));
                        newTCPheader.SetAckNumber(SequenceNumber32(ackData.second + payloadSize));
                        newPacket->AddHeader(newTCPheader);
                        uint32_t ret = newPacket->GetSize();
                        Address addr = ipItem->GetAddress();
                        uint16_t protocol = ipItem->GetProtocol();
                        Ipv4Header newheader;
                        newheader.SetSource(dst_info.first);
                        newheader.SetDestination(Workers_Num[ackData.first].first);
                        newheader.SetProtocol(6);
                        newheader.SetIdentification(0);
                        newheader.SetTtl(50);
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
                    for (auto pkts : pktList){
                        // setAllBit(hasSent);
                        //std::cout << "sent aggregate pkt!" << "\n";
                        std::pair<Ipv4Address, uint16_t> LeaderTuple = Workers_Num[1];
                        // change int to uint8_t
                        uint8_t aggregate_payload[200]; 
                        // aggregate_payload[0] = hasSent[0];
                        // aggregate_payload[1] = hasSent[1];
                        aggregate_payload[0] = hasSentVector[0][0];
                        aggregate_payload[1] = hasSentVector[0][1];
                        hasSentVector.erase(hasSentVector.begin());
                        aggregate_payload[2] = (pkts.second >> 8) & 0xFF;
                        aggregate_payload[3] = pkts.second & 0xFF;
                        // create new pkt
                        Ptr<Packet> newPacket = Create<Packet>(aggregate_payload, 200);
                        TcpHeader out_tcpHeader = tcpHeader;
                        out_tcpHeader.SetSourcePort(LeaderTuple.second);
                        out_tcpHeader.SetSequenceNumber(SequenceNumber32((pkts.first * payloadSize) + 1));
                        std::cout << "=> sent seq " << (pkts.first * payloadSize) + 1 << " to PS\n";
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
            std::cout << "drop!\n";
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
