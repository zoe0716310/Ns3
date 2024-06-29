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
int payloadSize = 200;
int WorkerNum = 4;
std::vector<std::set<int>> receivedPackets;
std::vector<int> lastAck;
bool enableMulti = true;
bool enableUdpRetrans = false;
double SendingRate_min = 20;
double SendingRate_max = 20;
double Bandwidth_min = 40;
double Bandwidth_max = 40;
double outgoingBW = 500;
double mean = 10;
double standard_deviation = 1.0;
int AggregateSsh = 4; // threshold of aggregate
int QueueLength = 50;
std::map<SequenceNumber32, uint32_t> receivedMap;
std::vector<std::pair<SequenceNumber32, std::bitset<4>>> unDoneMap;
int pktSent = 20000;
std::vector<std::bitset<4>> workerHasSentMap(pktSent);
std::vector<std::bitset<4>> recordHasSentMap(pktSent);
SequenceNumber32 nextRxSeq = SequenceNumber32(0);
std::map<uint32_t, std::pair<uint32_t, uint32_t>> UdpMap;
std::vector<int> udp_cnt(WorkerNum);
int receivedUdp = 0;
int totalSent = 0;
float totalPkt = 0;
float successAggregate = 0;
std::vector<Ptr<TutorialApp>> app(WorkerNum);
int lastSeq = 0;
int seed = 10;
std::string outputPcapDir = "";
int needRetransCount = 0;
int timeout = 50;
bool endFlag = false;
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
                    (1000 * 1000 * (curTime.GetSeconds() - prevTime[itr->first].GetSeconds()))
                << std::endl;
            prevTime[itr->first] = curTime;
            prevBytes[itr->first] = itr->second.txBytes;
        }
    }
    Simulator::Schedule(Seconds(0.01), &TraceThroughput, monitor);
}

// trace packet loss
void GetTotalReceivedPackets(Ptr<Application> app, uint32_t *totalPackets)
{
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(app);
    *totalPackets = sink->GetTotalRx();
    NS_LOG_UNCOND("Into GetTotalReceivedPackets");
}

void SendAck(int payload, bool ecn, int worker){
    // std::cout << "worker : " << worker << " ack " << payload << "\n";
    app[worker]->Ack(payload, ecn);
    // std::cout << "Now : " << Now().GetSeconds() - 1 << "\n";
}

std::vector<int> getSetBits(const uint8_t* buffer) {
    std::vector<int> setBits;

    for (int byteIndex = 0; byteIndex < 2; ++byteIndex) {
        for (int bitOffset = 0; bitOffset < 8; ++bitOffset) {
            if ((buffer[byteIndex] & (1 << bitOffset)) != 0) {
                setBits.push_back(byteIndex * 8 + bitOffset);
            }
        }
    }

    return setBits;
}

void ReceiveUDPPacket (Ptr<Socket> socket){
    totalSent++;
    // std::cout << "ReceiveUDPPacket\n";
    Ptr<Packet> packet;
    Address from;
    packet = socket->RecvFrom (from);
        
    // 获取payload
    uint8_t buffer[packet->GetSize ()];
    packet->CopyData(buffer, packet->GetSize ());
    long long int payload = 0;
    for (int i = 2; i < 4; i++) {
        payload = (payload << 8) | buffer[i];
    }
    uint8_t sent[2];
    sent[0] = buffer[0];
    sent[1] = buffer[1];
    // std::cout << "receve payload : " << payload;
    int sent0 = sent[0];
    int sent1 = sent[1];
    // std::cout << "sent0 : " << sent0 << "; sent1 : " << sent1 <<"\n";
    // getSetBits(sent);
    std::vector<int> workerHasSent = getSetBits(sent);
    // for (int i = 0; i < 4; i++){
    //     if ((buffer[0] & (1 << i)) != 0){
    //         workerHasSent.push_back(i);
    //     }
    // }
    // std::cout << " worker : ";
    // for (auto worker : workerHasSent){
    //     std::cout << worker << " ";
    // }
    // std::cout << "\n";
    // std::cout << "Now : " << Now().GetSeconds() - 1 << "\n";
    
    for (auto worker : workerHasSent){
        if (recordHasSentMap[payload][worker] == 0){
            recordHasSentMap[payload][worker] = 1;
        }
        if (workerHasSentMap[payload][worker] == 0){
            workerHasSentMap[payload][worker] = 1;
            if (workerHasSentMap[payload].all()){
                if(AggregateSsh == WorkerNum){
                    successAggregate++;
                    totalPkt++;
                    Simulator::Schedule(MicroSeconds(21000), &SendAck, payload, true, worker);
                }
                break;
            }
            else if(workerHasSentMap[payload].count() >= AggregateSsh){
                successAggregate++;
                for(int i = 0; i < workerHasSentMap[payload].size(); i++){
                    if (workerHasSentMap[payload][i] == 0){
                        totalPkt++;
                        Simulator::Schedule(MicroSeconds(21000), &SendAck, payload, true, i);
                    }
                }
                workerHasSentMap[payload].set();
            }
            totalPkt++;
            Simulator::Schedule(MicroSeconds(21000), &SendAck, payload, true, worker);
        }
    }
    float lossRate = 1 - (totalPkt / (pktSent * WorkerNum));
    // std::cout << "Packet loss rate = " << lossRate << "\n";
    float aggregateRate = (successAggregate / pktSent);
    // std::cout << "Packet aggregate rate = " << aggregateRate << "\n";
    if (lossRate == 0.0 && !endFlag){
        endFlag = true;
        std::cout << "End : " << Now().GetSeconds() << "\n";
        std::cout << "Total Send : " << totalSent << "\n";
        for (int i = 0; i < WorkerNum; i++){
            app[i]->TriggerStopApplication();
        }
        // int index = 0;
        // for (auto bitsets : recordHasSentMap){
        //     std::cout << index << " : ";
        //     for(int i = 0; i < WorkerNum; i++){
        //         if (bitsets[i] == 0){
        //             std::cout << "0";
        //         }
        //         else{
        //             std::cout << "1";
        //         }
        //     }
        //     index++;
        //     std::cout << std::endl;
        // }
    }
}

void SetCallback_RecvUDP (Ptr<Socket> socket, const Address& addr){
    socket->SetRecvCallback (MakeCallback (&ReceiveUDPPacket));
}

void SendUDPData(Ptr<Socket> socket, uint32_t size){
    app[socket->GetNode()->GetId()]->SetTimeOut();
    // std::cout << "Worker : " << socket->GetNode()->GetId() + 1 << " send packet size : " << size << "\n";
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
    bool enablePcap = false;
    Time stopTime = Seconds(80);
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
    cmd.AddValue("AggregateSsh", "Aggregation threshold", AggregateSsh);
    cmd.AddValue("QueueLength", "Queue Length", QueueLength);
    cmd.AddValue("mean", "mean", mean);
    cmd.AddValue("standard_deviation", "standard deviation", standard_deviation);
    cmd.AddValue("outgoingBW", "outgoing BandWidth", outgoingBW);
    cmd.AddValue("timeout", "timeout (ms)", timeout);
    cmd.AddValue("seed", "Random Seed", seed);
    cmd.AddValue("enablePcap", "Enable/Disable pcap file generation", enablePcap);
    cmd.AddValue("outputPcapDir", "set pcap file diractory", outputPcapDir);
    cmd.AddValue("stopTime",
                 "Stop time for applications / simulation time will be stopTime + 1",
                 stopTime);
    cmd.Parse(argc, argv);

    queueDisc = std::string("ns3::") + queueDisc;

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::" + firstTcpTypeId));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(6291456));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(6291456));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue(QueueSize("1p")));
    Config::SetDefault(queueDisc + "::MaxSize", QueueSizeValue(QueueSize("50p")));
    Config::SetDefault("ns3::AggregateQueueDisc::AggregateSsh", UintegerValue(AggregateSsh));
    Config::SetDefault("ns3::AggregateQueueDisc::QueueLength", UintegerValue(QueueLength));

    cca[0] = firstTcpTypeId + "0";
    cca[1] = secondTcpTypeId + "1";

    RngSeedManager::SetSeed(seed);

    // set lastAck and receivedPackets with workerNum
    for (int i = 0; i < WorkerNum; i++){
        lastAck.push_back(1);
        std::set<int> temp;
        receivedPackets.push_back(temp);
    }

    Ptr<UniformRandomVariable> random = CreateObject<UniformRandomVariable> ();
    random->SetAttribute ("Min", DoubleValue (Bandwidth_min));
    random->SetAttribute ("Max", DoubleValue (Bandwidth_max));
    std::vector<double> workerBw;

    NodeContainer sender;
    NodeContainer receiver;
    NodeContainer routers;
    sender.Create(WorkerNum);
    routers.Create(2);
    receiver.Create(1);

    // Create the point-to-point link helpers
    PointToPointHelper edgeLinks[WorkerNum + 1];
    for (int i = 0; i < WorkerNum + 1; i++){
        if (i == 0){
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(Bandwidth_max) + "Mbps"));
            workerBw.push_back(Bandwidth_max);
            std::cout << "Worker " << i + 1 << " : " << Bandwidth_max << "Mbps\n";
        }
        else if (i == WorkerNum - 1){
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(Bandwidth_min) + "Mbps"));
            workerBw.push_back(Bandwidth_min);
            std::cout << "Worker " << i + 1 << " : " << Bandwidth_min << "Mbps\n";
        }
        else if (i == WorkerNum){
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(outgoingBW) + "Mbps"));
        }
        else{
            double tempMbps = random->GetValue();
            std::cout << "Worker " << i + 1 << " : " << tempMbps << "Mbps\n";
            edgeLinks[i].SetDeviceAttribute("DataRate", StringValue(std::to_string(tempMbps) + "Mbps"));
            workerBw.push_back(tempMbps);
        }
        edgeLinks[i].SetChannelAttribute("Delay", StringValue("7ms"));
    }

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("500Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("7ms"));


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
    Ptr<NormalRandomVariable> randomVariable = CreateObject<NormalRandomVariable> ();
    randomVariable->SetAttribute("Mean", DoubleValue(mean));
    randomVariable->SetAttribute("Variance", DoubleValue(standard_deviation * standard_deviation));

    std::vector<double> SendingRate;
    for (int i = 0; i < WorkerNum; i++){
        double temp = randomVariable->GetValue();
        SendingRate.push_back(temp);
        // std::cout << "SendingRate " << i << " = " << temp << "\n";
    }

    // std::vector<double> SendingRate;
    // for (int i = 0; i < (WorkerNum - 2); i++){
    //     // if (i == (WorkerNum - 3)){
    //     //     SendingRate.push_back(2);
    //     // }
    //     // else{
    //     //     SendingRate.push_back(randomVariable->GetValue());
    //     // }
    //     SendingRate.push_back(randomVariable->GetValue());
    // }

    // Install first application on the receiver
    Ptr<Socket> ns3UdpSocket = Socket::CreateSocket (receiver.Get (0), UdpSocketFactory::GetTypeId ());
    ns3UdpSocket->Bind (InetSocketAddress (Ipv4Address::GetAny (), first_port));
    ns3UdpSocket->SetRecvCallback (MakeCallback (&ReceiveUDPPacket));

    // Create Workers' apps
    for (int i = 0; i < WorkerNum; i++){
        app[i] = CreateObject<TutorialApp>();
    }

    // Create a UDP client application
    uint32_t packetSize = 230;
    uint32_t maxPacketCount = pktSent;

    Address sinkAddress(InetSocketAddress(ir1.GetAddress(1), first_port));
    std::vector<Ptr<Socket>> UdpSocket(WorkerNum);

    for (int i = 0; i < WorkerNum; i++){
        double SetSendingRate;
        Ptr<Socket> ns3UdpSocket = Socket::CreateSocket(sender.Get(i), UdpSocketFactory::GetTypeId());
        ns3UdpSocket->SetDataSentCallback(MakeCallback (&SendUDPData));
        // if (i == 0){
        //     SetSendingRate = SendingRate_min;
        // }
        // else if (i == (WorkerNum - 1)){
        //     SetSendingRate = SendingRate_max;
        // }
        // else{
        //     SetSendingRate = SendingRate[i - 1];
        // }
        SetSendingRate = SendingRate[i];
        std::cout << "SendingRate " << i << " = " << SetSendingRate << "\n";
        app[i]->Setup(ns3UdpSocket, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SetSendingRate) + "Mbps"), timeout);
        sender.Get(i)->AddApplication(app[i]);
        app[i]->SetStartTime(Seconds(0.));
        app[i]->SetStopTime(stopTime);
        UdpSocket[i] = ns3UdpSocket;
    }

    // Ptr<Socket> ns3UdpSocket1 = Socket::CreateSocket(sender.Get(0), UdpSocketFactory::GetTypeId());
    // ns3UdpSocket1->SetDataSentCallback(MakeCallback (&SendUDPData));
    // app[0]->Setup(ns3UdpSocket1, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate_min) + "Mbps"), timeout);
    // sender.Get(0)->AddApplication(app[0]);
    // app[0]->SetStartTime(Seconds(1.));
    // app[0]->SetStopTime(stopTime);

    // std::string rate1 = std::to_string(SendingRate[0]) + "Mbps";
    // std::string rate2 = std::to_string(SendingRate[1]) + "Mbps";
    // std::cout << "SendingRate " << 1 << " = " << rate1 << "\n";
    // std::cout << "SendingRate " << 2 << " = " << rate2 << "\n";
    // Ptr<Socket> ns3UdpSocket2 = Socket::CreateSocket(sender.Get(1), UdpSocketFactory::GetTypeId());
    // ns3UdpSocket2->SetDataSentCallback(MakeCallback (&SendUDPData));
    // app[1]->Setup(ns3UdpSocket2, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate[0]) + "Mbps"), timeout);
    // sender.Get(1)->AddApplication(app[1]);
    // app[1]->SetStartTime(Seconds(1.));
    // app[1]->SetStopTime(stopTime);

    // Ptr<Socket> ns3UdpSocket3 = Socket::CreateSocket(sender.Get(2), UdpSocketFactory::GetTypeId());
    // ns3UdpSocket3->SetDataSentCallback(MakeCallback (&SendUDPData));
    // app[2]->Setup(ns3UdpSocket3, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate[1]) + "Mbps"), timeout);
    // sender.Get(2)->AddApplication(app[2]);
    // app[2]->SetStartTime(Seconds(1.));
    // app[2]->SetStopTime(stopTime);

    // Ptr<Socket> ns3UdpSocket4 = Socket::CreateSocket(sender.Get(3), UdpSocketFactory::GetTypeId());
    // ns3UdpSocket4->SetDataSentCallback(MakeCallback (&SendUDPData));
    // app[3]->Setup(ns3UdpSocket4, sinkAddress, packetSize, maxPacketCount, DataRate(std::to_string(SendingRate_max) + "Mbps"), timeout);
    // sender.Get(3)->AddApplication(app[3]);
    // app[3]->SetStartTime(Seconds(1.));
    // app[3]->SetStopTime(stopTime);
    ////////////////////////////////////////////////////

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
    // std::cout << "1/1/1/1 : " << ans[0] << ", 2/1/1 : " << ans[1] << ", 3/1 : " << ans[2] << ", 4 : " << ans[3] <<  "\n";
    // std::cout << "success aggregate : " << ans[0] + ans[1] + ans[2] + ans[3] << "\n";

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    Simulator::Destroy();
    NS_LOG_UNCOND("Total Received Packets sink =" << totalBytes / 4);

    return 0;
}
