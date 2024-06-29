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
                   DataRate dataRate,
                   int timeout)
{
    m_socket = socket;
    m_peer = address;
    m_packetSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;
    m_cwnd = 10;
    m_fastRecoveryCnt = 0;
    m_nextCwnd = m_cwnd;
    m_rtt = MilliSeconds(1.2);
    m_timeOut = MilliSeconds(timeout);
    m_windowFirstSeq = 0;
    m_windowLastSeq = m_cwnd - 1;
    m_mdFlag = false;
    m_ssFlag = false;
    m_ssSsh = 1000;
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
    m_cwnd = 0;

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

void 
TutorialApp::Ack(int seq, bool ecn)
{
    if (seq != m_unAckedList[0].first){
        m_fastRecoveryCnt++;
    }
    else{
        m_fastRecoveryCnt = 0;
    }

    for(int i = 0; i < m_unAckedList.size(); i++){
        if (m_unAckedList[i].first == seq){
            if (i < m_retransNumber){
                m_retransNumber--;
            }
            m_rtt = (m_rtt + (Now() - m_unAckedList[i].second)) / 2;
            m_unAckedList.erase(m_unAckedList.begin() + i);
        }
    }

    if (m_fastRecoveryCnt >= 3){
        if (m_unAckedList[m_retransNumber].first <= seq - 3){
            if (m_unAckedList[m_retransNumber].first >= m_windowFirstSeq){
                m_mdFlag = true;
            }
            Retransmission(m_unAckedList[m_retransNumber].first, false);
            m_unAckedList[m_retransNumber].second = Now();
            m_retransNumber++;
            // std::cout << "worker " << m_socket->GetNode()->GetId() << " : Fast Recovery Trigger\n";
        }
    }
    if(ecn){
        m_mdFlag = true;
        // std::cout << "worker " << m_socket->GetNode()->GetId() << " : ECN Trigger\n";
    }

    if (seq >= m_windowLastSeq){
        if(m_mdFlag){
            try{
                m_cwnd = m_cwnd / 2;
                if (m_ssFlag){
                    m_ssFlag = false;
                }
                else{
                    m_ssSsh = m_cwnd;
                }
                // m_ssFlag = false;
                m_mdFlag = false;
                if (m_cwnd == 0){
                    m_cwnd = 1;
                }
            }
            catch (std::exception& e){
                // std::cout << "m_mdFlag m_cwnd = " << m_cwnd << "\n";
            }
        }
        else if(m_cwnd < m_ssSsh){
            try{
                m_cwnd = m_cwnd * 2;
            }
            catch (std::exception& e){
                // std::cout << "m_ssFlag m_cwnd = " << m_cwnd << "\n";
            }
        }
        else{
            try{
                m_cwnd = m_cwnd + 1;
            }
            catch (std::exception& e){
                // std::cout << "Else m_cwnd = " << m_cwnd << "\n";
            }
        }
        m_windowFirstSeq = m_windowLastSeq;
        m_windowLastSeq = m_windowLastSeq + m_cwnd;
        // std::cout << "worker " << m_socket->GetNode()->GetId() << "m_windowFirstSeq = " << m_windowFirstSeq << "; m_windowLastSeq = " << m_windowLastSeq << "\n";
    }
    // std::cout << "worker " << m_socket->GetNode()->GetId() << " m_cwnd = " << m_cwnd << "\n";
}

int 
TutorialApp::GetCwnd()
{
    return m_cwnd;
}

void 
TutorialApp::CheckAck(int seq)
{
    for(int i = 0; i < m_unAckedList.size(); i++){
        if (m_unAckedList[i].first == seq){
            if (i == 0){
                Retransmission(seq, true);
                m_unAckedList[i].second = Now();
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
TutorialApp::TriggerStopApplication()
{
    StopApplication();
}

void
TutorialApp::SendPacket()
{
    if (m_unAckedList.size() >= m_cwnd){
        ScheduleTx();
        return;
    }
    uint8_t payload[200];

    payload[0] = 0;
    payload[1] = 0;
    payload[2] = (m_packetsSent >> 8) & 0xFF;
    payload[3] = m_packetsSent & 0xFF;

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
TutorialApp::Retransmission(int seq, bool isTimeout)
{
    if (isTimeout){
        m_cwnd = 2;
        m_ssFlag = true;
    }
    uint8_t payload[200];

    payload[0] = 0;
    payload[1] = 0;
    payload[2] = (seq >> 8) & 0xFF;
    payload[3] = seq & 0xFF;

    Ptr<Packet> packet = Create<Packet>(payload, 200);
    m_socket->Send(packet);
    ScheduleCheckAck(seq);
    // std::cout << "worker " << m_socket->GetNode()->GetId() << " : Retransmission : " << seq << "\n";
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
        Time tNext(m_rtt / m_cwnd);
        //Time tNext(Seconds(0.05));
        m_sendEvent = Simulator::Schedule(tNext, &TutorialApp::SendPacket, this);
    }
}
