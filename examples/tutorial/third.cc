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

using namespace ns3;

std::string dir;
uint32_t prev1 = 0;
Time prevTime1 = Seconds(0);
uint32_t prev2 = 0;
Time prevTime2 = Seconds(0);
uint32_t prevTx1 = 0;
uint32_t prevTx2 = 0;
std::string cca[2] = {};
bool enableMulti = true;
NS_LOG_COMPONENT_DEFINE("Example");

// Calculate throughput
static void
TraceThroughput(Ptr<FlowMonitor> monitor)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    if (stats.begin() != stats.end()){
        auto itr1 = stats.begin();
        auto itr2 = itr1++;
        Time curTime = Now();
        std::ofstream thr1(dir + "/throughput" + cca[itr1->first - 1] + ".dat", std::ios::out | std::ios::app);
        thr1 << curTime.GetSeconds() << " "
            << 8 * (itr1->second.txBytes - prev1) /
                (1024 * 1024 * (curTime.GetSeconds() - prevTime1.GetSeconds()))
            << std::endl;
        prevTime1 = curTime;
        prev1 = itr1->second.txBytes;

        if (enableMulti){
            curTime = Now();
            std::ofstream thr2(dir + "/throughput" + cca[itr2->first - 1] + ".dat", std::ios::out | std::ios::app);
            thr2 << curTime.GetSeconds() << " "
                << 8 * (itr2->second.txBytes - prev2) /
                    (1024 * 1024 * (curTime.GetSeconds() - prevTime2.GetSeconds()))
                << std::endl;
            prevTime2 = curTime;
            prev2 = itr2->second.txBytes;
        }
    }
    Simulator::Schedule(Seconds(0.2), &TraceThroughput, monitor);
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
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream(dir + "/cwnd_" + cca[socketId] + "node" + std::to_string(nodeId) + ".dat");
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

void HandleRead (Ptr<Socket> socket, const Address & addr)
{
//   Ptr<Packet> packet;
//   Address from;
//   while ((packet = socket->RecvFrom (from)))
//   {
//     // 从接收到的 packet 中获取数据，这里假设数据是一个32位的整数
//     uint32_t receivedData;
//     packet->CopyData(reinterpret_cast<uint8_t*>(&receivedData), sizeof(receivedData));

//     NS_LOG_INFO ("Received packet with data: " << receivedData);

//     // 处理数据，这里加一
//     uint32_t newData = receivedData + 1;

//     // 创建新的 packet 并发送回去
//     Ptr<Packet> newPacket = Create<Packet> (&newData, sizeof(newData));
//     socket->SendTo (newPacket, 0, from);
//   }
}

int
main(int argc, char* argv[])
{
    // Naming the output directory using local system time
    RngSeedManager::SetSeed(2);
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
    Time stopTime = Seconds(20);
    LogComponentEnable("Example", LOG_LEVEL_INFO);

    CommandLine cmd(__FILE__);
    cmd.AddValue("firstTcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", firstTcpTypeId);
    cmd.AddValue("secondTcpTypeId", "Transport protocol to use: TcpNewReno, TcpBbr", secondTcpTypeId);
    cmd.AddValue("delAckCount", "Delayed ACK count", delAckCount);
    cmd.AddValue("enableMulti", "Enable/Disable multi CCA", enableMulti);
    cmd.AddValue("enablePcap", "Enable/Disable pcap file generation", enablePcap);
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
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(4));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue(QueueSize("1p")));
    Config::SetDefault(queueDisc + "::MaxSize", QueueSizeValue(QueueSize("100p")));

    cca[0] = firstTcpTypeId + "0";
    cca[1] = secondTcpTypeId + "1";

    NodeContainer sender;
    NodeContainer receiver;
    NodeContainer routers;
    sender.Create(2);
    routers.Create(2);
    receiver.Create(1);

    // Create the point-to-point link helpers
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("0.5ms"));

    PointToPointHelper edgeLink;
    edgeLink.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
    edgeLink.SetChannelAttribute("Delay", StringValue("0.5ms"));

    PointToPointHelper edgeLink2;
    edgeLink2.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    edgeLink2.SetChannelAttribute("Delay", StringValue("0.5ms"));

    PointToPointHelper edgeLink3;
    edgeLink3.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    edgeLink3.SetChannelAttribute("Delay", StringValue("0.5ms"));

    // Create NetDevice containers
    NetDeviceContainer senderEdge1 = edgeLink.Install(sender.Get(0), routers.Get(0));
    NetDeviceContainer senderEdge2 = edgeLink2.Install(sender.Get(1), routers.Get(0));
    NetDeviceContainer r1r2 = bottleneckLink.Install(routers.Get(0), routers.Get(1));
    NetDeviceContainer receiverEdge = edgeLink3.Install(routers.Get(1), receiver.Get(0));

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
    Ipv4InterfaceContainer is1 = ipv4.Assign(senderEdge1);

    ipv4.NewNetwork();
    Ipv4InterfaceContainer is2 = ipv4.Assign(senderEdge2);

    ipv4.NewNetwork();
    Ipv4InterfaceContainer ir1 = ipv4.Assign(receiverEdge);

    // Populate routing tables
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Select receiver side port
    uint16_t first_port = 50001;

    //////////////////////////////////////////////////// UDP
    // UdpServerHelper server (first_port);

    // ApplicationContainer serverApp = server.Install (receiver.Get(0));
    // serverApp.Start (Seconds (1.0));
    // serverApp.Stop (stopTime);

    // // Create a UDP client application
    // uint32_t packetSize = 1024;
    // uint32_t maxPacketCount = 50000;
    // uint32_t maxPacketRate = 200 * 1024 * 1024; // 200 Mbps

    // UdpClientHelper client1 (ir1.GetAddress(1), first_port);
    // client1.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
    // client1.SetAttribute ("PacketSize", UintegerValue (packetSize));
    // client1.SetAttribute ("Interval", TimeValue (MicroSeconds(180)));

    // ApplicationContainer clientApp1 = client1.Install (sender.Get(0));
    // clientApp1.Start (Seconds (2.0));
    // clientApp1.Stop (stopTime);

    // UdpClientHelper client2 (ir1.GetAddress(1), first_port);
    // client2.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
    // client2.SetAttribute ("PacketSize", UintegerValue (packetSize));
    // client2.SetAttribute ("Interval", TimeValue (MicroSeconds(3600)));

    // ApplicationContainer clientApp2 = client2.Install (sender.Get(0));
    // clientApp2.Start (Seconds (2.1));
    // clientApp2.Stop (stopTime);
    ////////////////////////////////////////////////////

    //////////////////////////////////////////////////// TCP
    Address sinkAddress1(InetSocketAddress(ir1.GetAddress(1), first_port));
    Ptr<Socket> ns3TcpSocket1 = Socket::CreateSocket(sender.Get(0), TcpSocketFactory::GetTypeId());
    Ptr<TutorialApp> app1 = CreateObject<TutorialApp>();
    app1->Setup(ns3TcpSocket1, sinkAddress1, 0.5, 15, DataRate("100Mbps"));
    sender.Get(0)->AddApplication(app1);
    app1->SetStartTime(Seconds(1.));
    Simulator::Schedule(Seconds(0.1) + MilliSeconds(1), &TraceCwnd, 0, 0);
    app1->SetStopTime(stopTime);

    // Install first application on the receiver
    Ptr<Socket> recvSocket = Socket::CreateSocket (receiver.Get (0), TcpSocketFactory::GetTypeId ());
    recvSocket->Bind (InetSocketAddress (Ipv4Address::GetAny (), first_port));
    recvSocket->Listen ();
    PacketSinkHelper first_sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), first_port));
    ApplicationContainer first_sinkApps = first_sink.Install(receiver.Get(0));
    first_sinkApps.Start(Seconds(0.0));
    first_sinkApps.Stop(stopTime);

    


    /////////////////////////////////////////////////////////////////

    // Create a new directory to store the output of the program
    dir = "bbr-results/" + currentTime + "/";
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
        edgeLink.EnablePcapAll(dir + "/pcap/bbr");
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////////// packet loss
    uint32_t totalBytes = 0;
    //Simulator::ScheduleDestroy(&GetTotalReceivedPackets, first_sinkApps.Get(0), &totalBytes);
    
    // Check for dropped packets using Flow Monitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    Simulator::Schedule(Seconds(0 + 0.000001), &TraceThroughput, monitor);
    Simulator::Schedule(Seconds(0 + 0.000001), &TraceTx, monitor);

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

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    Simulator::Destroy();
    NS_LOG_UNCOND("Total Received Packets sink =" << totalBytes / 4);

    return 0;
}
