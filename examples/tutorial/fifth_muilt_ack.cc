/*
 * Copyright (c) 2018-20 NITK Surathkal
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
 * Authors: Aarti Nandagiri <aarti.nandagiri@gmail.com>
 *          Vivek Jain <jain.vivek.anand@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 */

// This program simulates the following topology:
//
//           1000 Mbps           10Mbps          1000 Mbps
//  Sender -------------- R1 -------------- R2 -------------- Receiver
//              5ms               10ms               5ms
//
// The link between R1 and R2 is a bottleneck link with 10 Mbps. All other
// links are 1000 Mbps.
//
// This program runs by default for 100 seconds and creates a new directory
// called 'bbr-results' in the ns-3 root directory. The program creates one
// sub-directory called 'pcap' in 'bbr-results' directory (if pcap generation
// is enabled) and three .dat files.
//
// (1) 'pcap' sub-directory contains six PCAP files:
//     * bbr-0-0.pcap for the interface on Sender
//     * bbr-1-0.pcap for the interface on Receiver
//     * bbr-2-0.pcap for the first interface on R1
//     * bbr-2-1.pcap for the second interface on R1
//     * bbr-3-0.pcap for the first interface on R2
//     * bbr-3-1.pcap for the second interface on R2
// (2) cwnd.dat file contains congestion window trace for the sender node
// (3) throughput.dat file contains sender side throughput trace
// (4) queueSize.dat file contains queue length trace from the bottleneck link
//
// BBR algorithm enters PROBE_RTT phase in every 10 seconds. The congestion
// window is fixed to 4 segments in this phase with a goal to achieve a better
// estimate of minimum RTT (because queue at the bottleneck link tends to drain
// when the congestion window is reduced to 4 segments).
//
// The congestion window and queue occupancy traces output by this program show
// periodic drops every 10 seconds when BBR algorithm is in PROBE_RTT phase.

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "tutorial-app.h"
#include "ns3/log.h"
#include <bitset>

using namespace ns3;

std::string dir;
std::map<FlowId, uint32_t> prevBytes;
uint32_t prev1 = 0;
std::map<FlowId, Time> prevTime;
Time prevTime1 = Seconds(0);
uint32_t prevTx1 = 0;
uint32_t prevTx2 = 0;
std::string cca[2] = {};
int payloadSize = 4;
int WorkerNum = 10;
std::set<int> receivedPackets[10];
std::vector<int> lastAck;
bool enableMulti = true;
bool enableUdpRetrans = false;
double SendingRate_min;
double SendingRate_max;
double Bandwidth_min = 30;
double Bandwidth_max = 30;
int AggregateSsh = 10; // threshold of aggregate
std::map<SequenceNumber32, uint32_t> receivedMap;
std::vector<std::pair<SequenceNumber32, std::bitset<10>>> unDoneMap;
SequenceNumber32 nextRxSeq = SequenceNumber32(0);
std::map<uint32_t, std::pair<uint32_t, uint32_t>> UdpMap;
std::vector<int> udp_cnt(WorkerNum);
int receivedUdp = 0;
int totalPkt = 0;
std::vector<Ptr<TutorialApp>> app(WorkerNum);
int lastSeq = 0;
int seed;
std::string outputPcapDir = "";
int needRetransCount = 0;
NS_LOG_COMPONENT_DEFINE("Example");
// Calculate throughput
static void
TraceThroughput(Ptr<FlowMonitor> monitor)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    auto itr = stats.begin();
    if (stats.begin() != stats.end()){
        for (; itr != stats.end(); itr++){
            Time curTime = Now();
            auto it = prevBytes.find(itr->first);
            if (it == prevBytes.end()){
                prevBytes[itr->first] = 0;
            }
            auto it2 = prevTime.find(itr->first);
            if (it2 == prevTime.end()){
                prevTime[itr->first] = Seconds(0);
            }
            std::ofstream thr1(dir + "/throughput" + std::to_string(itr->first) + ".dat", std::ios::out | std::ios::app);
            thr1 << curTime.GetSeconds() << " "
                << 8 * (itr->second.txBytes - prevBytes[itr->first]) /
                    (1024 * 1024 * (curTime.GetSeconds() - prevTime[itr->first].GetSeconds()))
                << std::endl;
            prevTime[itr->first] = curTime;
            prevBytes[itr->first] = itr->second.txBytes;
        }
    }
    Simulator::Schedule(Seconds(0.02), &TraceThroughput, monitor);
}

// Calculate Tx
static void
TraceTx(Ptr<FlowMonitor> monitor)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    if (stats.begin() != stats.end()){
        auto itr1 = stats.begin();
        auto itr2 = itr1++;
        Time curTime = Now();
        std::ofstream thr1(dir + "/Tx" + cca[itr1->first - 1] + ".dat", std::ios::out | std::ios::app);
        thr1 << curTime.GetSeconds() << " "
            << itr1->second.txPackets - prevTx1
            << std::endl;
        prevTx1 = itr1->second.txPackets;
        if (enableMulti){
            curTime = Now();
            std::ofstream thr2(dir + "/Tx" + cca[itr2->first - 1] + ".dat", std::ios::out | std::ios::app);
            thr2 << curTime.GetSeconds() << " "
                << itr2->second.txPackets - prevTx2
                << std::endl;
            prevTx2 = itr2->second.txPackets;
        }
    }
    Simulator::Schedule(Seconds(1), &TraceTx, monitor);
}

// Trace congestion window
static void
CwndTracer(Ptr<OutputStreamWrapper> stream, uint32_t oldval, uint32_t newval)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " " << newval / 4.0 << std::endl;
}

void
TraceCwnd(uint32_t nodeId, uint32_t socketId)
{
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream(dir + "/cwnd_" + "node" + std::to_string(nodeId) + ".dat");
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(nodeId) +
                                      "/$ns3::TcpL4Protocol/SocketList/" +
                                      std::to_string(socketId) + "/CongestionWindow",
                                  MakeBoundCallback(&CwndTracer, stream));
}

// trace packet loss
void GetTotalReceivedPackets(Ptr<Application> app, uint32_t *totalPackets)
{
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(app);
    *totalPackets = sink->GetTotalRx();
    NS_LOG_UNCOND("Into GetTotalReceivedPackets");
}

std::vector<std::pair<int, int>> generateSackList(const std::set<int>& receivedPackets, int lastAck) {
    std::vector<std::pair<int, int>> sackList;
    auto it = receivedPackets.upper_bound(lastAck);
    while (it != receivedPackets.end()) {
        int start = *it;
        int end = start + payloadSize;
        // Find the end of the contiguous block
        auto nextIt = std::next(it);
        while (nextIt != receivedPackets.end() && *nextIt == end) {
            end += payloadSize;
            nextIt = std::next(nextIt);
        }
        sackList.emplace_back(start, end);
        it = nextIt;
    }
    return sackList;
}

std::vector<int> getSetBits(const uint8_t* buffer) {
    std::vector<int> setBits;

    for (int byteIndex = 0; byteIndex < 2; ++byteIndex) {
        for (int bitOffset = 0; bitOffset < 8; ++bitOffset) {
            if ((buffer[byteIndex] & (1 << bitOffset)) != 0) {
                setBits.push_back(byteIndex * 8 + bitOffset + 1);
            }
        }
    }

    return setBits;
}

void setBit(uint8_t* buffer, int pos) {
    if (pos < 1 || pos > 16) {
        std::cerr << "fifth : Invalid input. pos should be between 1 and 16." << std::endl;
        return;
    }

    // 計算要修改的位元在哪個 byte
    int byteIndex = (pos - 1) / 8;

    // 計算在該 byte 中的偏移量
    int bitOffset = (pos - 1) % 8;

    // 將對應的位元設為 1
    buffer[byteIndex] |= (1 << bitOffset);
}

void ReceiveTCPPacket (Ptr<Socket> socket)
{
    std::cout << "ReceiveTCPPacket\n";
    Ptr<TcpSocketBase> tcpSocketBase = DynamicCast<TcpSocketBase>(socket);
    Ptr<Packet> packet;
    TcpHeader tcpHeader;
    Address from;
    if (tcpSocketBase->NeedPacketCopy.first){
        packet = tcpSocketBase->NeedPacketCopy.second;
        tcpSocketBase->NeedPacketCopy.second->RemoveHeader(tcpHeader);
    }
    else{
        packet = socket->RecvFrom (from);
        tcpHeader = tcpSocketBase->TcpHeaderCopy;
    }
        
    // 获取payload
    uint8_t buffer[packet->GetSize ()];
    packet->CopyData(buffer, packet->GetSize ());
    long long int payload = 0;
    for (int i = 2; i < payloadSize; i++) {
        payload = (payload << 8) | buffer[i];
    }
    // check which workers has sent
    uint8_t hasSent[2];
    hasSent[0] = buffer[0];
    hasSent[1] = buffer[1];
    std::vector<int> workersHasSent = getSetBits(hasSent);

    SequenceNumber32 currentSeq = tcpHeader.GetSequenceNumber();
    std::cout << "Received pkt seq: " << currentSeq << " payload: " << payload << " size: " << packet->GetSize () << "\n";
    // for (auto seq : unDoneMap){
    //     std::cout << seq.first << "    ";
    // }
    // std::cout << "\n";
    // for (auto seq : unDoneMap){
    //     int cnt = seq.second;
    //     std::cout << cnt << "    ";
    // }
    // std::cout << "\n";
    uint8_t * buf = new uint8_t[2];
    uint32_t i = 0;
    for (auto seq : unDoneMap){
        if (seq.first == currentSeq){
            std::cout << "In unDoneMap\n";
            if (unDoneMap[i].second.count() >= AggregateSsh){
                for (auto worker : workersHasSent){
                    buf[0] = 0;
                    buf[1] = 0;
                    setBit(buf, worker);
                    worker -= 1;
                    unDoneMap[i].second[worker] = 1;
                    receivedPackets[worker].insert(currentSeq.GetValue());
                    // Update lastAck if the packet fills a gap
                    while (receivedPackets[worker].find(lastAck[worker]) != receivedPackets[worker].end()) {
                        lastAck[worker] += payloadSize;
                    }
                    tcpSocketBase->SetSackList(generateSackList(receivedPackets[worker], lastAck[worker]));
                    if (unDoneMap[i].second.all()){
                        unDoneMap.erase(unDoneMap.begin() + i);
                    }
                    std::cout << "worker : " << worker <<  " seq : " << currentSeq << " ack: " << lastAck[worker] << "\n";
                    
                    tcpSocketBase->SendSpacificPacket(tcpSocketBase->TcpFlagCopy, SequenceNumber32(lastAck[worker]), buf, 2);
                }
                receivedMap[seq.first] += payload;
                return;
            }
            for (auto worker : workersHasSent){
                unDoneMap[i].second[worker - 1] = 1;
            }
            receivedMap[seq.first] += payload;
            if (unDoneMap[i].second.count() >= AggregateSsh){
                for (int j = 0; j < unDoneMap[i].second.size(); j++){
                    if (unDoneMap[i].second[j] == 1){
                        buf[0] = 0;
                        buf[1] = 0;
                        setBit(buf, j + 1);
                        receivedPackets[j].insert(currentSeq.GetValue());
                        // Update lastAck if the packet fills a gap
                        while (receivedPackets[j].find(lastAck[j]) != receivedPackets[j].end()) {
                            lastAck[j] += payloadSize;
                        }
                        tcpSocketBase->SetSackList(generateSackList(receivedPackets[j], lastAck[j]));
                        std::cout << "worker : " << j + 1 <<  " seq : " << currentSeq << " ack: " << lastAck[j] << "\n";
                        tcpSocketBase->SendSpacificPacket(tcpSocketBase->TcpFlagCopy, SequenceNumber32(lastAck[j]), buf, 2);
                    }
                }
            }
            if (unDoneMap[i].second.all()){
                unDoneMap.erase(unDoneMap.begin() + i);
            }
            return;
        }
        i++;
    }
    if (workersHasSent.size() >= AggregateSsh){
        for (auto worker : workersHasSent){
            buf[0] = 0;
            buf[1] = 0;
            setBit(buf, worker);
            // int temp = buf[0];
            // int temp1 = buf[1];
            // std::cout << "buf : " << temp << " : " << temp1 << "\n";
            worker -= 1;
            receivedPackets[worker].insert(currentSeq.GetValue());
            // Update lastAck if the packet fills a gap
            while (receivedPackets[worker].find(lastAck[worker]) != receivedPackets[worker].end()) {
                lastAck[worker] += payloadSize;
            }
            tcpSocketBase->SetSackList(generateSackList(receivedPackets[worker], lastAck[worker]));
            std::cout << "worker : " << worker <<  " seq : " << currentSeq << " ack: " << lastAck[worker] << "\n";
            tcpSocketBase->SendSpacificPacket(tcpSocketBase->TcpFlagCopy, SequenceNumber32(lastAck[worker]), buf, 2);
        }
        
        receivedMap[currentSeq] += payload;
        if (workersHasSent.size() != WorkerNum){
            std::bitset<10> tempBitset;
            tempBitset.reset();
            for (auto worker : workersHasSent){
                tempBitset[worker - 1] = 1;
            }
            unDoneMap.push_back(std::pair<SequenceNumber32, std::bitset<10>> (currentSeq, tempBitset));
            std::sort(unDoneMap.begin(), unDoneMap.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
        }
    }
    else{
        std::bitset<10> tempBitset;
        tempBitset.reset();
        for (auto worker : workersHasSent){
            tempBitset[worker - 1] = 1;
        }
        unDoneMap.push_back(std::pair<SequenceNumber32, std::bitset<10>> (currentSeq, tempBitset));
        std::sort(unDoneMap.begin(), unDoneMap.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        receivedMap[currentSeq] += payload;
    }
    
    std::cout << "ReceiveTCPPacket : " << payload << "\n";
}

void CheckResent(){
    double delayTime = 0.0016;
    double sendingTime = 0.000009;
    if (SendingRate_min < 30){
        sendingTime = (30.0 / SendingRate_min) * sendingTime;
    }
    std::cout << "CheckResent : " << needRetransCount << "\n";
    if (needRetransCount >= 3){
        Time curTime = Now();
        std::cout << "resent time = " << curTime << "\n";
        std::vector<uint32_t> temp;
        for (int i = 0; i < UdpMap.size(); i++){
            if (UdpMap[i].second != 0){
                temp.push_back(i);
                lastSeq = i;
            }
        }
        if (!temp.empty()){
            for (int i = 0; i < WorkerNum; i++){
                app[i]->ResetVector(temp);
                app[i]->ResetpacketsSent();
                app[i]->ReSentPacket();
            }
            Simulator::Schedule(Seconds(delayTime), &CheckResent);
        }
        else{
            std::cout << "finish time = " << Now().GetSeconds() << "\n";
        }
        for (auto seq : temp){
            std::cout << "loss seq : " << seq << "\n";
        }
        std::cout << "last seq : " << lastSeq << "\n";
        std::cout << "pkt remain : " << temp.size() << "\n";
        needRetransCount = 0;
        return;
    }
    needRetransCount++;
    Simulator::Schedule(Seconds(sendingTime), &CheckResent);
}

void ReceiveUDPPacket (Ptr<Socket> socket){
    std::cout << "Reset needRetransCount : " << needRetransCount << "\n";
    needRetransCount = 0;
    totalPkt += 1;
    Ptr<Packet> packet;
    UdpHeader tudpHeader;
    Address from;
    packet = socket->RecvFrom (from);
        
    // 获取payload
    uint8_t buffer[packet->GetSize ()];
    packet->CopyData(buffer, packet->GetSize ());
    long long int payload = 0;
    for (int i = 1; i < payloadSize; i++) {
        payload = (payload << 8) | buffer[i];
    }
    uint8_t Count = buffer[0];
    int cnt = Count;
    cnt = WorkerNum - cnt;
    UdpMap[payload].first -= 1;
    if (UdpMap[payload].second < cnt){
        UdpMap[payload].second = 0;
    }
    else{
        UdpMap[payload].second -= cnt;
    }
    receivedUdp += cnt;
    udp_cnt[cnt - 1]++;
    std::cout << "ReceiveUDPPacket : " << receivedUdp << "\n";
    std::cout << "1 : " << udp_cnt[0] << " 2 : " << udp_cnt[1] << " 3 : " << udp_cnt[2] << " 4 : " << udp_cnt[3] << "\n";
    std::cout << "UdpMap[" << lastSeq << "].second = " << UdpMap[lastSeq].second << "\n";
    if (totalPkt == 1 && enableUdpRetrans){
        CheckResent();
    }
    if (UdpMap[lastSeq].second == 0){
        needRetransCount = 3;
    }
}

void SetCallback_RecvTCP (Ptr<Socket> socket, const Address& addr){
    socket->SetRecvCallback (MakeCallback (&ReceiveTCPPacket));
}

void SetCallback_RecvUDP (Ptr<Socket> socket, const Address& addr){
    socket->SetRecvCallback (MakeCallback (&ReceiveUDPPacket));
}

bool BoolCallback (Ptr<Socket> socket, const Address& addr){
    return true;
}

int
main(int argc, char* argv[])
{
    // Naming the output directory using local system time
    //LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
    LogComponentEnable("AggregateQueueDisc", LOG_LEVEL_INFO);
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%d-%m-%Y-%I-%M-%S", timeinfo);
    std::string currentTime(buffer);

    std::string firstTcpTypeId = "TcpCubic";
    std::string secondTcpTypeId = "TcpCubic";
    std::string queueDisc = "AggregateQueueDisc";
    uint32_t delAckCount = 2;
    bool bql = true;
    bool enablePcap = true;
    Time stopTime = Seconds(10);
    LogComponentEnable("Example", LOG_LEVEL_INFO);

    CommandLine cmd(__FILE__);
    cmd.AddValue("firstTcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", firstTcpTypeId);
    cmd.AddValue("secondTcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", secondTcpTypeId);
    cmd.AddValue("delAckCount", "Delayed ACK count", delAckCount);
    cmd.AddValue("enableMulti", "Enable/Disable multi CCA", enableMulti);
    cmd.AddValue("enableUdpRetrans", "Enable/Disable Udp Retransmission", enableUdpRetrans);
    cmd.AddValue("SendingRate_min", "Min SendingRate", SendingRate_min);
    cmd.AddValue("SendingRate_max", "Max SendingRate", SendingRate_max);
    cmd.AddValue("Bandwidth_min", "Min Bandwidth", Bandwidth_min);
    cmd.AddValue("Bandwidth_max", "Max Bandwidth", Bandwidth_max);
    cmd.AddValue("seed", "Random Seed", seed);
    cmd.AddValue("enablePcap", "Enable/Disable pcap file generation", enablePcap);
    cmd.AddValue("outputPcapDir", "set pcap file diractory", outputPcapDir);
    cmd.AddValue("stopTime",
                 "Stop time for applications / simulation time will be stopTime + 1",
                 stopTime);
    cmd.Parse(argc, argv);

    queueDisc = std::string("ns3::") + queueDisc;

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::" + firstTcpTypeId));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue(QueueSize("1p")));
    Config::SetDefault(queueDisc + "::MaxSize", QueueSizeValue(QueueSize("100p")));

    cca[0] = firstTcpTypeId + "0";
    cca[1] = secondTcpTypeId + "1";

    //RngSeedManager::SetSeed(seed);
    for (int i = 0; i < WorkerNum; i++){
        lastAck.push_back(1);
    }
    std::cout << "lastAck.push_back\n";

    Ptr<UniformRandomVariable> random = CreateObject<UniformRandomVariable> ();
    random->SetAttribute ("Min", DoubleValue (Bandwidth_min));
    random->SetAttribute ("Max", DoubleValue (Bandwidth_max));

    NodeContainer sender;
    NodeContainer receiver;
    NodeContainer routers;
    sender.Create(WorkerNum);
    routers.Create(2);
    receiver.Create(1);

    // Create the point-to-point link helpers
    PointToPointHelper edgeLinks[WorkerNum + 1];
    for (int i = 0; i < WorkerNum; i++){
        if (i == 0){
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(30) + "Mbps"));
        }
        else if (i == WorkerNum - 2){
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(30) + "Mbps"));
        }
        else if (i == WorkerNum - 1){
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(60) + "Mbps"));
        }
        else{
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(30) + "Mbps"));
        }
        edgeLinks[i].SetChannelAttribute("Delay", StringValue("0.5ms"));
    }

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("500Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("0.5ms"));


    // Create NetDevice containers
    NetDeviceContainer senderEdges[WorkerNum];

    for (int i = 0; i < WorkerNum; i++){
        senderEdges[i] = edgeLinks[i].Install(sender.Get(i), routers.Get(0));
    }

    NetDeviceContainer r1r2 = bottleneckLink.Install(routers.Get(0), routers.Get(1));
    NetDeviceContainer receiverEdge = edgeLinks[WorkerNum].Install(routers.Get(1), receiver.Get(0));

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(0.0000001));
    //receiverEdge.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // Install Stack
    InternetStackHelper internet;
    internet.Install(sender);
    internet.Install(receiver);
    internet.Install(routers);

    // Configure the root queue discipline
    TrafficControlHelper tch;
    tch.SetRootQueueDisc(queueDisc);

    if (bql)
    {
        tch.SetQueueLimits("ns3::DynamicQueueLimits", "HoldTime", StringValue("1000ms"));
    }

    ////////////////////// install aggregate queue
    tch.Install(routers.Get(1)->GetDevice(0));
    tch.Install(routers.Get(1)->GetDevice(1));
    //tch.Install(routers.Get(0)->GetDevice(2));
    //////////////////////

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.0.0.0", "255.255.255.0");

    Ipv4InterfaceContainer i1i2 = ipv4.Assign(r1r2);
    ipv4.NewNetwork();

    Ipv4InterfaceContainer ifs[WorkerNum];
    for (int i = 0; i < WorkerNum; i++){
        ifs[i] = ipv4.Assign(senderEdges[i]);
        ipv4.NewNetwork();
    }

    Ipv4InterfaceContainer ir1 = ipv4.Assign(receiverEdge);

    // Populate routing tables
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Select receiver side port
    uint16_t first_port = 50001;

    //////////////////////////////////////////////////// UDP
    // Ptr<UniformRandomVariable> randomVariable = CreateObject<UniformRandomVariable> ();
    // randomVariable->SetAttribute ("Min", DoubleValue (SendingRate_min));
    // randomVariable->SetAttribute ("Max", DoubleValue (SendingRate_max));

    // std::vector<double> SendingRate;
    // for (int i = 0; i < 2; i++){
    //     SendingRate.push_back(randomVariable->GetValue());
    // }

    // // Install first application on the receiver
    // Ptr<Socket> ns3UdpSocket = Socket::CreateSocket (receiver.Get (0), UdpSocketFactory::GetTypeId ());
    // ns3UdpSocket->Bind (InetSocketAddress (Ipv4Address::GetAny (), first_port));
    // ns3UdpSocket->SetRecvCallback (MakeCallback (&ReceiveUDPPacket));

    // // Create Workers' apps
    // for (int i = 0; i < 4; i++){
    //     app[i] = CreateObject<TutorialApp>();
    // }

    // // Create a UDP client application
    // uint32_t packetSize = 34;
    // uint32_t maxPacketCount = 10000;
    // std::string sendingRate = "16Mbps"; // 200 Mbps

    // for (int i = 0; i < maxPacketCount; i++){
    //     UdpMap[i].first = 4;
    //     UdpMap[i].second = 4;
    // }
    // lastSeq = UdpMap.size() - 1;

    // Address sinkAddress(InetSocketAddress(ir1.GetAddress(1), first_port));

    // Ptr<Socket> ns3UdpSocket1 = Socket::CreateSocket(sender.Get(0), UdpSocketFactory::GetTypeId());
    // app[0]->Setup(ns3UdpSocket1, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate_min) + "Mbps"));
    // sender.Get(0)->AddApplication(app[0]);
    // app[0]->SetStartTime(Seconds(1.));
    // app[0]->SetStopTime(stopTime);

    // std::string rate1 = std::to_string(SendingRate[0]) + "Mbps";
    // std::string rate2 = std::to_string(SendingRate[1]) + "Mbps";
    // std::cout << "SendingRate " << 1 << " = " << rate1 << "\n";
    // std::cout << "SendingRate " << 2 << " = " << rate2 << "\n";
    // Ptr<Socket> ns3UdpSocket2 = Socket::CreateSocket(sender.Get(1), UdpSocketFactory::GetTypeId());
    // app[1]->Setup(ns3UdpSocket2, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate[0]) + "Mbps"));
    // sender.Get(1)->AddApplication(app[1]);
    // app[1]->SetStartTime(Seconds(1.));
    // app[1]->SetStopTime(stopTime);

    // Ptr<Socket> ns3UdpSocket3 = Socket::CreateSocket(sender.Get(2), UdpSocketFactory::GetTypeId());
    // app[2]->Setup(ns3UdpSocket3, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate[1]) + "Mbps"));
    // sender.Get(2)->AddApplication(app[2]);
    // app[2]->SetStartTime(Seconds(1.));
    // app[2]->SetStopTime(stopTime);

    // Ptr<Socket> ns3UdpSocket4 = Socket::CreateSocket(sender.Get(3), UdpSocketFactory::GetTypeId());
    // app[3]->Setup(ns3UdpSocket4, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate_max) + "Mbps"));
    // sender.Get(3)->AddApplication(app[3]);
    // app[3]->SetStartTime(Seconds(1.));
    // app[3]->SetStopTime(stopTime);
    ////////////////////////////////////////////////////

    //////////////////////////////////////////////////// TCP
    int packetNum = 500;
    Address sinkAddress(InetSocketAddress(ir1.GetAddress(1), first_port));

    // Install first application on the receiver
    Ptr<Socket> receiverSocket = Socket::CreateSocket (receiver.Get (0), TcpSocketFactory::GetTypeId ());
    receiverSocket->Bind (InetSocketAddress (Ipv4Address::GetAny (), first_port));
    receiverSocket->Listen ();
    receiverSocket->SetAcceptCallback(&BoolCallback, &SetCallback_RecvTCP);

    Ptr<Socket> ns3TcpSockets[WorkerNum];
    Ptr<TutorialApp> apps[WorkerNum];
    for (int i = 0; i < WorkerNum; i++){
        ns3TcpSockets[i] = Socket::CreateSocket(sender.Get(i), TcpSocketFactory::GetTypeId());
        apps[i] = CreateObject<TutorialApp>();
        apps[i]->Setup(ns3TcpSockets[i], sinkAddress, 58, packetNum, DataRate("30Mbps"));
        sender.Get(i)->AddApplication(apps[i]);
        apps[i]->SetStartTime(Seconds(0.));
        Simulator::Schedule(Seconds(0.1) + MilliSeconds(1), &TraceCwnd, i, 0);
        apps[i]->SetStopTime(stopTime);
    }
    /////////////////////////////////////////////////////////////////

    // Create a new directory to store the output of the program
    if (outputPcapDir != ""){
        dir = outputPcapDir;
    }
    else{
        dir = "bbr-results/" + currentTime + "/";
    }
    std::string dirToSave = "mkdir -p " + dir;
    if (system(dirToSave.c_str()) == -1)
    {
        exit(1);
    }

    // Generate PCAP traces if it is enabled
    if (enablePcap)
    {
        if (system((dirToSave + "/pcap/").c_str()) == -1)
        {
            exit(1);
        }
        edgeLinks[0].EnablePcapAll(dir + "/pcap/bbr");
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////// packet loss
    uint32_t totalBytes = 0;
    //Simulator::ScheduleDestroy(&GetTotalReceivedPackets, first_sinkApps.Get(0), &totalBytes);
    
    // Check for dropped packets using Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    Simulator::Schedule(Seconds(0 + 0.000001), &TraceThroughput, monitor);
    //Simulator::Schedule(Seconds(0 + 0.000001), &TraceTx, monitor);

    Simulator::Stop(stopTime + TimeStep(1));
    Simulator::Run();
    ///////////////////////////////////// Network Perfomance Calculation /////////////////////////////////////

    int j=0;
    float AvgThroughput = 0;
    Time Jitter;
    Time Delay;
    uint32_t SentPackets = 0;
    uint32_t ReceivedPackets = 0;
    uint32_t LostPackets = 0;

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);

        NS_LOG_UNCOND("----Flow ID:" <<iter->first);
        NS_LOG_UNCOND("Src Addr" <<t.sourceAddress << " Dst Addr "<< t.destinationAddress << " Src Port " << t.sourcePort);
        NS_LOG_UNCOND("Sent Packets=" <<iter->second.txPackets);
        NS_LOG_UNCOND("Received Packets =" <<iter->second.rxPackets);
        NS_LOG_UNCOND("Lost Packets =" <<iter->second.txPackets-iter->second.rxPackets);
        NS_LOG_UNCOND("Packet delivery ratio =" <<iter->second.rxPackets*100/iter->second.txPackets << "%");
        NS_LOG_UNCOND("Packet loss ratio =" << (iter->second.txPackets-iter->second.rxPackets)*100/iter->second.txPackets << "%");
        NS_LOG_UNCOND("Delay =" <<iter->second.delaySum);
        NS_LOG_UNCOND("Jitter =" <<iter->second.jitterSum);
        NS_LOG_UNCOND("Throughput =" <<iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024<<"Kbps");

        SentPackets = SentPackets +(iter->second.txPackets);
        ReceivedPackets = ReceivedPackets + (iter->second.rxPackets);
        LostPackets = LostPackets + (iter->second.txPackets-iter->second.rxPackets);
        AvgThroughput = AvgThroughput + (iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024);
        Delay = Delay + (iter->second.delaySum);
        Jitter = Jitter + (iter->second.jitterSum);

        j = j + 1;

    }

    AvgThroughput = AvgThroughput/j;
    NS_LOG_UNCOND("--------Total Results of the simulation----------"<<std::endl);
    NS_LOG_UNCOND("Total sent packets  =" << SentPackets);
    NS_LOG_UNCOND("Total Received Packets =" << ReceivedPackets);
    NS_LOG_UNCOND("Total Lost Packets =" << LostPackets);
    NS_LOG_UNCOND("Packet Loss ratio =" << ((LostPackets*100)/SentPackets)<< "%");
    NS_LOG_UNCOND("Packet delivery ratio =" << ((ReceivedPackets*100)/SentPackets)<< "%");
    NS_LOG_UNCOND("Average Throughput =" << AvgThroughput<< "Kbps");
    NS_LOG_UNCOND("End to End Delay =" << Delay);
    NS_LOG_UNCOND("End to End Jitter delay =" << Jitter);
    NS_LOG_UNCOND("Total Flod id " << j);
    monitor->SerializeToXmlFile("manet-routing.xml", true, true);
    std::vector<int> ans(4);
    std::map<uint32_t, std::pair<uint32_t, uint32_t>>::iterator it;
    for (it = UdpMap.begin(); it != UdpMap.end(); it++)
    {
        if (it->second.second == 0){
            ans[it->second.first]++;
        }
    }
    std::cout << "1/1/1/1 : " << ans[0] << ", 2/1/1 : " << ans[1] << ", 3/1 : " << ans[2] << ", 4 : " << ans[3] <<  "\n";
    std::cout << "success aggregate : " << ans[0] + ans[1] + ans[2] + ans[3] << "\n";

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    Simulator::Destroy();
    NS_LOG_UNCOND("Total Received Packets sink =" << totalBytes / 4);

    return 0;
}
