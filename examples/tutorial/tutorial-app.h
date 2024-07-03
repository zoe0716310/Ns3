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

#ifndef TUTORIAL_APP_H
#define TUTORIAL_APP_H

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

namespace ns3
{

class Application;

/**
 * Tutorial - a simple Application sending packets.
 */
class TutorialApp : public Application
{
  public:
    TutorialApp();
    ~TutorialApp() override;

    /**
     * Register this type.
     * \return The TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Setup the socket.
     * \param socket The socket.
     * \param address The destination address.
     * \param packetSize The packet size to transmit.
     * \param nPackets The number of packets to transmit.
     * \param dataRate the data rate to use.
     */
    // void Setup(Ptr<Socket> socket, //ATP
    //            Address address,
    //            uint32_t packetSize,
    //            uint32_t nPackets,
    //            DataRate dataRate,
    //            int appNumber);
    void Setup(Ptr<Socket> socket,
               Address address,
               uint32_t packetSize,
               uint32_t nPackets,
               DataRate dataRate,
               int timeout);

    /**
     * Setup the socket.
     * \param socket The socket.
     * \param address The destination address.
     * \param packetSize The packet size to transmit.
     * \param udpPayload UDP payload seq number vector.
     * \param dataRate the data rate to use.
     */
    // void Setup(Ptr<Socket> socket,
    //            Address address,
    //            uint32_t packetSize,
    //            std::vector<uint32_t> udpPayload,
    //            DataRate dataRate);

    void ResetpacketsSent();

    void ReSentPacket();

    void ResetVector(std::vector<uint32_t> udpPayload);

    void Ack(int seq, bool ecn);

    /// Get m_packetsSent
    uint32_t GetpacketsSent();

    /// Set m_packetsSent
    void SetpacketsSent(int num);

    void SetWorkerRate();

    void SetSwitchRate(int max_wid, int now_wid);

    float GetChaseRate();

    void SetTimeOut();
    
    void TriggerStopApplication();

    int GetCwnd();

    void PopTxbuffer();

  private:
    void StartApplication() override;
    void StopApplication() override;

    /// Schedule a new transmission.
    void ScheduleTx();
    /// Send a packet.
    void SendPacket();
    /// Retransmission a specific seq packet.
    void Retransmission(int seq, bool isTimeout);
    /// Check ack arrive or not
    void CheckAck(int seq);
    /// Schedule CheckAck.
    void ScheduleCheckAck(int seq);

    Ptr<Socket> m_socket;   //!< The transmission socket.
    Address m_peer;         //!< The destination address.
    uint32_t m_packetSize;  //!< The packet size.
    uint32_t m_nPackets;    //!< The number of packets to send.
    DataRate m_dataRate;    //!< The data rate to use.
    EventId m_sendEvent;    //!< Send event.
    bool m_running;         //!< True if the application is running.
    int m_packetsSent; //!< The number of packets sent.
    bool m_isVector;          //!< True if use vector as UDP packet paload
    std::vector<uint32_t> m_udpPayload; //!< UDP payload seq number vector
    std::vector<std::pair<uint32_t, Time>> m_unAckedList; //!< Packet hasn't be acked vector with clock
    int m_cwnd;             //!< congestion window
    uint32_t m_lastAck;             //!< last ack
    uint32_t m_fastRecoveryCnt;     //!< count of fast recovery
    Time m_timeOut;     //!< time out
    uint32_t m_nextCwnd;     //!< first seq of next cwnd
    bool m_mdFlag;     //!< muiltiple divide flge
    Time m_rtt;          //!< average RTT
    bool m_ssFlag;          //!< slow start flag
    int m_ssSsh;          //!< slow start threshold
    int m_windowLastSeq; //!< last sequence of this window
    int m_windowFirstSeq; //!< first sequence of this window
    int m_appNumber;   //!< app number
    int m_retransNumber;   //!< retransmission number
    Time m_workerPreviousTime; //!<time that worker sent Previous pkt
    double m_workerMovingAvg; //!<moving average of rate of worker
    Time m_switchPreviousTime; //!<time that switch sent Previous pkt
    double m_switchMovingAvg; //!<moving average of rate of switch
    int m_switchPreviousWid; //!<wid that Previous pkt have
    std::vector<int> m_txbuffer; //!< pkt in tx buffer
    int m_max_wid; //!< max_wid
    int m_now_wid; //!< now_wid
    double m_pktSendWid; //!< wid in double type should send
    int m_retransmissionTimes; //!< retransmission times
    bool m_ecnFlag;
};

} // namespace ns3

#endif /* TUTORIAL_APP_H */
