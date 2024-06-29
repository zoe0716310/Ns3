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

#include "tutorial-app.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#include "ns3/applications-module.h"

using namespace ns3;

TutorialApp::TutorialApp()
    : m_socket(nullptr),
      m_peer(),
      m_packetSize(0),
      m_nPackets(0),
      m_dataRate(0),
      m_sendEvent(),
      m_running(false),
      m_packetsSent(0),
      m_isVector(false),
      m_workerPreviousTime(0),
      m_workerMovingAvg(0),
      m_switchPreviousTime(0),
      m_switchMovingAvg(0),
      m_switchPreviousWid(0),
      m_pktSendWid(0.0)
{
}

TutorialApp::~TutorialApp()
{
    m_socket = nullptr;
}

/* static */
TypeId
TutorialApp::GetTypeId()
{
    static TypeId tid = TypeId("TutorialApp")
                            .SetParent<Application>()
                            .SetGroupName("Tutorial")
                            .AddConstructor<TutorialApp>();
    return tid;
}

void
TutorialApp::Setup(Ptr<Socket> socket,
                   Address address,
                   uint32_t packetSize,
                   uint32_t nPackets,
                   DataRate dataRate)
{
    m_socket = socket;
    m_peer = address;
    m_packetSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;
}

void
TutorialApp::StartApplication()
{
    m_running = true;
    m_packetsSent = 0;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}

void
TutorialApp::StopApplication()
{
    m_running = false;

    if (m_sendEvent.IsRunning())
    {
        Simulator::Cancel(m_sendEvent);
    }

    if (m_socket)
    {
        m_socket->Close();
    }
}

uint32_t
TutorialApp::GetpacketsSent()
{
    return m_packetsSent;
}

void
TutorialApp::SetpacketsSent(int num)
{
    m_packetsSent = num;
}

void
TutorialApp::ResetpacketsSent()
{
    m_packetsSent = 0;
}

void
TutorialApp::ReSentPacket()
{
    SendPacket();
}

void
TutorialApp::ResetVector(std::vector<uint32_t> udpPayload)
{
    m_nPackets = udpPayload.size();
    m_udpPayload = udpPayload;
    m_isVector = true;
}

void
TutorialApp::SetWorkerRate()
{
    // std::cout << "Node : " << m_socket->GetNode()->GetId() << " SetWorkerRate!\n";
    if (m_socket->GetNode()->GetId() == 8){
        std::cout << Now().GetSeconds() << " send pkt wid : " << m_txbuffer[0] << "\n";
    }
    m_txbuffer.erase(m_txbuffer.begin());
    Time temp = Now();
    double gap = (temp - m_workerPreviousTime).GetDouble();
    m_workerMovingAvg = (gap + m_workerMovingAvg) / 2;
    m_workerPreviousTime = temp;
    // std::cout << "MovingAvg : " << m_workerMovingAvg << "\n";
}

void
TutorialApp::SetSwitchRate(int max_wid, int now_wid)
{
    // std::cout << "Node : " << m_socket->GetNode()->GetId() << " SetSwitchRate!\n";
    // if (m_socket->GetNode()->GetId() == 8){
    //     if (!m_txbuffer.empty()){
    //         std::cout << "max_wid : " << wid << "; m_txbuffer[0] : " << m_txbuffer[0] << "\n";
    //     }
    // }
    m_max_wid = max_wid;
    m_now_wid = now_wid;
    Time temp = Now();
    double gap = (temp - m_switchPreviousTime).GetDouble();
    if (max_wid != m_switchPreviousWid){
        double rate = gap / (max_wid - m_switchPreviousWid);
        m_switchMovingAvg = (rate + m_switchMovingAvg) / 2;
    }
    m_switchPreviousTime = temp;
    m_switchPreviousWid = max_wid;
    // std::cout << "MovingAvg : " << m_switchMovingAvg << "\n";
}

float
TutorialApp::GetChaseRate()
{
    if (m_switchMovingAvg == 0.0){
        return 1.0;
    }
    return m_workerMovingAvg / m_switchMovingAvg;
}

int count = 0;

void
TutorialApp::SendPacket()
{
    uint8_t payload[200];

    if (m_isVector){
        payload[0] = 0;
        payload[1] = 0;
        payload[2] = (m_udpPayload[m_packetsSent] >> 8) & 0xFF;
        payload[3] = m_udpPayload[m_packetsSent] & 0xFF;
    }
    else{
        payload[0] = 0;
        payload[1] = 0;
        payload[2] = (m_packetsSent >> 8) & 0xFF;
        payload[3] = m_packetsSent & 0xFF;
    }

    Ptr<Packet> packet = Create<Packet>(payload, 200);
    // m_txbuffer.push_back(m_packetsSent);
    m_socket->Send(packet);

    if (++m_packetsSent < m_nPackets)
    {
        // std::cout << "ID : " << m_socket->GetNode()->GetId() << " m_packetsSent : " << m_packetsSent << "\n";
        // if(m_socket->GetNode()->GetId() == 5){
        //     if (m_packetsSent > 5000 && count < 2500){
        //         m_dataRate = DataRate("10Mbps");
        //         count++;
        //     }
        //     else{
        //         m_dataRate = DataRate("30Mbps");
        //     }
        // }
        ScheduleTx();
    }
}

void
TutorialApp::ScheduleTx()
{
    if (m_running)
    {
        Time tNext(Seconds(m_packetSize * 8 / static_cast<double>(m_dataRate.GetBitRate())));
        //Time tNext(Seconds(0.05));
        m_sendEvent = Simulator::Schedule(tNext, &TutorialApp::SendPacket, this);
    }
}
