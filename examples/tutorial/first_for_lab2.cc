/*
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
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

// Default Network Topology
//
//       10.1.1.0
// n0 -------------- n1
//    point-to-point
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FirstScriptExample");

int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    NodeContainer nodes;
    nodes.Create(3);

    PointToPointHelper pointToPoint1;
    pointToPoint1.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
    pointToPoint1.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper pointToPoint2;
    pointToPoint2.SetDeviceAttribute("DataRate", StringValue("3Mbps"));
    pointToPoint2.SetChannelAttribute("Delay", StringValue("2ms"));


    NetDeviceContainer CtoR1 = pointToPoint1.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer CtoR2 = pointToPoint2.Install(nodes.Get(0), nodes.Get(2));

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.0.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfacesCtoR1 = address.Assign(CtoR1);
    address.NewNetwork();
    Ipv4InterfaceContainer interfacesCtoR2 = address.Assign(CtoR2);

    UdpEchoServerHelper echoServerR1(99);
    UdpEchoServerHelper echoServerR2(98);

    ApplicationContainer serverAppsR1 = echoServerR1.Install(nodes.Get(1));
    serverAppsR1.Start(Seconds(1.0));
    serverAppsR1.Stop(Seconds(10.0));

    ApplicationContainer serverAppsR2 = echoServerR2.Install(nodes.Get(2));
    serverAppsR2.Start(Seconds(1.0));
    serverAppsR2.Stop(Seconds(10.0));

    UdpEchoClientHelper echoClientF1(interfacesCtoR1.GetAddress(1), 99);
    echoClientF1.SetAttribute("MaxPackets", UintegerValue(4));
    echoClientF1.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClientF1.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientAppsF1 = echoClientF1.Install(nodes.Get(0));
    clientAppsF1.Start(Seconds(2.0));
    clientAppsF1.Stop(Seconds(10.0));

    UdpEchoClientHelper echoClientF2(interfacesCtoR2.GetAddress(1), 98);
    echoClientF2.SetAttribute("MaxPackets", UintegerValue(4));
    echoClientF2.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClientF2.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientAppsF2 = echoClientF2.Install(nodes.Get(0));
    clientAppsF2.Start(Seconds(2.0));
    clientAppsF2.Stop(Seconds(10.0));

    // Create a new directory to store the output of the program
    std::string dir;
    dir = "bbr-results/lab2/";
    std::string dirToSave = "mkdir -p " + dir;
    if (system(dirToSave.c_str()) == -1)
    {
        exit(1);
    }

    // Generate PCAP traces if it is enabled
    if (1)
    {
        if (system((dirToSave + "/pcap/").c_str()) == -1)
        {
            exit(1);
        }
        pointToPoint1.EnablePcapAll(dir + "/pcap/bbr");
    }

    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
