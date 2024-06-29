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
      m_isVector(false)
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
    m_cwnd = 10;
    m_fastRecoveryCnt = 0;
    m_timeOut = Seconds(0.005);
    m_windowFirstSeq = 0;
    m_windowLastSeq = m_cwnd - 1;
    m_retransNumber = 0;
}

// void 
// TutorialApp::Setup(Ptr<Socket> socket,
//                    Address address,
//                    uint32_t packetSize,
//                    std::vector<uint32_t> udpPayload,
//                    DataRate dataRate)
// {
//     m_socket = socket;
//     m_peer = address;
//     m_packetSize = packetSize;
//     m_nPackets = udpPayload.size();
//     m_udpPayload = udpPayload;
//     m_dataRate = dataRate;
//     m_isVector = true;
// }

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

int retransmissionTimes = 0;

void 
TutorialApp::Ack(int seq, bool ecn)
{
    for(int i = 0; i < m_unAckedList.size(); i++){
        if (m_unAckedList[i].first == seq){
            m_unAckedList.erase(m_unAckedList.begin() + i);
        }
    }
    if (m_unAckedList.empty()){
        std::cout << "retransmission times : " << retransmissionTimes << "\n";
    }
}

void 
TutorialApp::CheckAck(int seq)
{
    for(int i = 0; i < m_unAckedList.size(); i++){
        if (m_unAckedList[i].first == seq){
            int threshold = 5;
            if (m_packetsSent >= m_nPackets){
                threshold = 20;
            }
            if (i < threshold){
                Retransmission(seq);
            }
            else{
                if (m_running)
                {
                    Time tNext(m_timeOut);
                    m_sendEvent = Simulator::Schedule(tNext, &TutorialApp::CheckAck, this, seq);
                }
            }
        }
    }
}

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
    m_socket->Send(packet);

    std::pair<uint32_t, Time> packetTag(m_packetsSent, Now());
    m_unAckedList.push_back(packetTag);
    ScheduleCheckAck(m_packetsSent);


    if (++m_packetsSent < m_nPackets)
    {
        ScheduleTx();
    }
}

void
TutorialApp::Retransmission(int seq)
{
    std::cout << "worker " << m_socket->GetNode()->GetId() << " retransmission : " << seq << "\n";
    retransmissionTimes++;
    uint8_t payload[200];

    payload[0] = 0;
    payload[1] = 0;
    payload[2] = (seq >> 8) & 0xFF;
    payload[3] = seq & 0xFF;

    Ptr<Packet> packet = Create<Packet>(payload, 200);
    m_socket->Send(packet);
    ScheduleCheckAck(seq);
}

void
TutorialApp::ScheduleCheckAck(int seq)
{
    if (m_running)
    {
        Time tNext(m_timeOut);
        m_sendEvent = Simulator::Schedule(tNext, &TutorialApp::CheckAck, this, seq);
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
