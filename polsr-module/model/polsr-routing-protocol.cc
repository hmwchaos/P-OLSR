/*
 * Copyright (c) 2004 Francisco J. Ros
 * Copyright (c) 2007 INESC Porto
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Francisco J. Ros  <fjrm@dif.um.es>
 *          Gustavo J. A. M. Carneiro <gjc@inescporto.pt>
 */

///
/// @brief Implementation of POLSR agent and related classes.
///
/// This is the main file of this software because POLSR's behaviour is
/// implemented here.
///

#define NS_LOG_APPEND_CONTEXT                                                                      \
    if (GetObject<Node>())                                                                         \
    {                                                                                              \
        std::clog << "[node " << GetObject<Node>()->GetId() << "] ";                               \
    }

#include "polsr-routing-protocol.h"

#include "polsr-repositories.h"

#include "ns3/boolean.h"
#include "ns3/enum.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-packet-info-tag.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/names.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

/********** Useful macros **********/

///
/// @brief Gets the delay between a given time and the current time.
///
/// If given time is previous to the current one, then this macro returns
/// 一个接近 0 的时间值。该宏用于把事件调度到指定时刻。
///
#define DELAY(time)                                                                                \
    (((time) < (Simulator::Now())) ? Seconds(0.000001)                                             \
                                   : (time - Simulator::Now() + Seconds(0.000001)))

///
/// @brief 节点必须重新通告每条链路和每个邻居的周期。
///
/// 这里只使用该值来定义 POLSR_NEIGHB_HOLD_TIME。
///
#define POLSR_REFRESH_INTERVAL m_helloInterval

/********** 保持时间 **********/

/// 邻居信息保持时间。
#define POLSR_NEIGHB_HOLD_TIME Time(3 * POLSR_REFRESH_INTERVAL)
/// 拓扑信息保持时间。
#define POLSR_TOP_HOLD_TIME Time(3 * m_tcInterval)
/// Dup holding time.
#define POLSR_DUP_HOLD_TIME Seconds(30)
/// MID holding time.
#define POLSR_MID_HOLD_TIME Time(3 * m_midInterval)
/// HNA holding time.
#define POLSR_HNA_HOLD_TIME Time(3 * m_hnaInterval)

/********** 其他常量 **********/

/// Maximum allowed jitter.
#define POLSR_MAXJITTER (m_helloInterval.GetSeconds() / 4)
/// Maximum allowed sequence number.
#define POLSR_MAX_SEQ_NUM 65535
/// Random number between [0-POLSR_MAXJITTER] used to jitter POLSR packet transmission.
#define JITTER (Seconds(m_uniformRandomVariable->GetValue(0, POLSR_MAXJITTER)))

/// Maximum number of messages per packet.
#define POLSR_MAX_MSGS 64

/// Maximum number of hellos per message (4 possible link types * 3 possible nb types).
#define POLSR_MAX_HELLOS 12

/// Maximum number of addresses advertised on a message.
#define POLSR_MAX_ADDRS 64

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PolsrRoutingProtocol");

namespace polsr
{

/**
 * @ingroup polsr
 *
 * POLSR link types.
 * See \RFC{3626} section 18.5.
 */
enum class LinkType : uint8_t
{
    UNSPEC_LINK = 0, //!< 未指定链路类型
    ASYM_LINK = 1,   //!< 非对称链路类型
    SYM_LINK = 2,    //!< 对称链路类型
    LOST_LINK = 3,   //!< 丢失链路类型
};

/**
 * Stream insertion operator for POLSR link type.
 *
 * @param os Output stream.
 * @param linkType POLSR link type.
 * @return A reference to the output stream.
 */
inline std::ostream&
operator<<(std::ostream& os, LinkType linkType)
{
    switch (linkType)
    {
    case LinkType::UNSPEC_LINK:
        return (os << "UNSPEC_LINK");
    case LinkType::ASYM_LINK:
        return (os << "ASYM_LINK");
    case LinkType::SYM_LINK:
        return (os << "SYM_LINK");
    case LinkType::LOST_LINK:
        return (os << "LOST_LINK");
    default:
        return (os << "Unknown link type");
    }
}

/**
 * @ingroup polsr
 *
 * POLSR neighbor types.
 * See \RFC{3626} section 18.6.
 */
enum class NeighborType : uint8_t
{
    NOT_NEIGH = 0, //!< Not neighbor type
    SYM_NEIGH = 1, //!< Symmetric neighbor type
    MPR_NEIGH = 2, //!< Asymmetric neighbor type
};

/**
 * Stream insertion operator for POLSR link type.
 *
 * @param os Output stream.
 * @param neighborType POLSR neighbor type.
 * @return A reference to the output stream.
 */
inline std::ostream&
operator<<(std::ostream& os, NeighborType neighborType)
{
    switch (neighborType)
    {
    case NeighborType::NOT_NEIGH:
        return (os << "NOT_NEIGH");
    case NeighborType::SYM_NEIGH:
        return (os << "SYM_NEIGH");
    case NeighborType::MPR_NEIGH:
        return (os << "MPR_NEIGH");
    default:
        return (os << "Unknown neighbor type");
    }
}

/********** POLSR class **********/

NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

/* see https://www.iana.org/assignments/service-names-port-numbers */
const uint16_t RoutingProtocol::POLSR_PORT_NUMBER = 698;

TypeId
RoutingProtocol::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::polsr::RoutingProtocol")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("Polsr")
            .AddConstructor<RoutingProtocol>()
            .AddAttribute("HelloInterval",
                          "HELLO messages emission interval.",
                          TimeValue(Seconds(2)),
                          MakeTimeAccessor(&RoutingProtocol::m_helloInterval),
                          MakeTimeChecker())
            .AddAttribute("TcInterval",
                          "TC messages emission interval.",
                          TimeValue(Seconds(5)),
                          MakeTimeAccessor(&RoutingProtocol::m_tcInterval),
                          MakeTimeChecker())
            .AddAttribute("MidInterval",
                          "MID messages emission interval.  Normally it is equal to TcInterval.",
                          TimeValue(Seconds(5)),
                          MakeTimeAccessor(&RoutingProtocol::m_midInterval),
                          MakeTimeChecker())
            .AddAttribute("HnaInterval",
                          "HNA messages emission interval.  Normally it is equal to TcInterval.",
                          TimeValue(Seconds(5)),
                          MakeTimeAccessor(&RoutingProtocol::m_hnaInterval),
                          MakeTimeChecker())
            .AddAttribute("Willingness",
                          "Willingness of a node to carry and forward traffic for other nodes.",
                          EnumValue(Willingness::DEFAULT),
                          MakeEnumAccessor<Willingness>(&RoutingProtocol::m_willingness),
                          MakeEnumChecker(Willingness::NEVER,
                                          "never",
                                          Willingness::LOW,
                                          "low",
                                          Willingness::DEFAULT,
                                          "default",
                                          Willingness::HIGH,
                                          "high",
                                          Willingness::ALWAYS,
                                          "always"))
            /*
             * P-OLSR Attribute 说明：
             *
             * EnablePrediction 是总开关。关闭时，HELLO/TC 不附带预测字段，
             * 路由表仍按原 OLSR 跳数逻辑计算。
             *
             * UsePredictionForRouting 控制是否真的用 predictedCost 选路。
             * UsePredictionForMpr 只在原 MPR 规则打平时作为辅助选择条件，
             * 避免直接推翻 RFC 3626 的 MPR 主流程。
             */
            .AddAttribute("EnablePrediction",
                          "Enable P-OLSR GPS and ETX link prediction.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoutingProtocol::m_enablePrediction),
                          MakeBooleanChecker())
            .AddAttribute("UsePredictionForRouting",
                          "Use predicted link cost instead of hop-count expansion.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoutingProtocol::m_usePredictionForRouting),
                          MakeBooleanChecker())
            .AddAttribute("UsePredictionForMpr",
                          "Use predicted ETX cost as an MPR tie-breaker.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoutingProtocol::m_usePredictionForMpr),
                          MakeBooleanChecker())
            .AddAttribute("PredictionAlpha",
                          "Smoothing factor for HELLO reception quality.",
                          DoubleValue(0.2),
                          MakeDoubleAccessor(&RoutingProtocol::m_predictionAlpha),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("PredictionBeta",
                          "Exponential penalty factor for positive relative speed in weighted ETX.",
                          DoubleValue(0.2),
                          MakeDoubleAccessor(&RoutingProtocol::m_predictionBeta),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("PredictionGamma",
                          "Smoothing factor for relative speed.",
                          DoubleValue(0.08),
                          MakeDoubleAccessor(&RoutingProtocol::m_predictionGamma),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("HelloDeadlineFactor",
                          "HELLO deadline multiplier used by Expected Arrival Time decay.",
                          DoubleValue(1.4),
                          MakeDoubleAccessor(&RoutingProtocol::m_helloDeadlineFactor),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("MinPredictionQuality",
                          "Lower bound for phi*rho in ETX.",
                          DoubleValue(0.01),
                          MakeDoubleAccessor(&RoutingProtocol::m_minPredictionQuality),
                          MakeDoubleChecker<double>(0.000001, 1.0))
            .AddAttribute("MaxPredictionCost",
                          "Upper bound for P-OLSR per-link routing cost.",
                          DoubleValue(1000.0),
                          MakeDoubleAccessor(&RoutingProtocol::m_maxPredictionCost),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("PredictionValidityTime",
                          "Validity time for GPS information learned from HELLO messages.",
                          TimeValue(Seconds(6)),
                          MakeTimeAccessor(&RoutingProtocol::m_predictionValidityTime),
                          MakeTimeChecker())
            .AddTraceSource("Rx",
                            "Receive POLSR packet.",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_rxPacketTrace),
                            "ns3::polsr::RoutingProtocol::PacketTxRxTracedCallback")
            .AddTraceSource("Tx",
                            "Send POLSR packet.",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_txPacketTrace),
                            "ns3::polsr::RoutingProtocol::PacketTxRxTracedCallback")
            .AddTraceSource("RoutingTableChanged",
                            "The POLSR routing table has changed.",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_routingTableChanged),
                            "ns3::polsr::RoutingProtocol::TableChangeTracedCallback");
    return tid;
}

RoutingProtocol::RoutingProtocol()
    : m_routingTableAssociation(nullptr),
      m_enablePrediction(false),
      m_usePredictionForRouting(false),
      m_usePredictionForMpr(false),
      m_predictionAlpha(0.2),
      m_predictionBeta(0.2), //链路质量老化
      m_predictionGamma(0.08),
      m_minPredictionQuality(0.01),
      m_maxPredictionCost(1000.0),
      m_predictionValidityTime(Seconds(6)),
      m_helloDeadlineFactor(1.4),
      m_ipv4(nullptr),
      m_helloTimer(Timer::CANCEL_ON_DESTROY),
      m_tcTimer(Timer::CANCEL_ON_DESTROY),
      m_midTimer(Timer::CANCEL_ON_DESTROY),
      m_hnaTimer(Timer::CANCEL_ON_DESTROY),
      m_queuedMessagesTimer(Timer::CANCEL_ON_DESTROY)
{
    m_uniformRandomVariable = CreateObject<UniformRandomVariable>();

    m_hnaRoutingTable = Create<Ipv4StaticRouting>();
}

RoutingProtocol::~RoutingProtocol()
{
}

void
RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_ASSERT(ipv4);
    NS_ASSERT(!m_ipv4);
    NS_LOG_DEBUG("Created polsr::RoutingProtocol");
    m_helloTimer.SetFunction(&RoutingProtocol::HelloTimerExpire, this);
    m_tcTimer.SetFunction(&RoutingProtocol::TcTimerExpire, this);
    m_midTimer.SetFunction(&RoutingProtocol::MidTimerExpire, this);
    m_hnaTimer.SetFunction(&RoutingProtocol::HnaTimerExpire, this);
    m_queuedMessagesTimer.SetFunction(&RoutingProtocol::SendQueuedMessages, this);

    m_packetSequenceNumber = POLSR_MAX_SEQ_NUM;
    m_messageSequenceNumber = POLSR_MAX_SEQ_NUM;
    m_ansn = POLSR_MAX_SEQ_NUM;

    m_linkTupleTimerFirstTime = true;

    m_ipv4 = ipv4;

    m_hnaRoutingTable->SetIpv4(ipv4);
}

void
RoutingProtocol::DoDispose()
{
    m_ipv4 = nullptr;
    m_hnaRoutingTable = nullptr;
    m_routingTableAssociation = nullptr;

    if (m_recvSocket)
    {
        m_recvSocket->Close();
        m_recvSocket = nullptr;
    }

    for (auto iter = m_sendSockets.begin(); iter != m_sendSockets.end(); iter++)
    {
        iter->first->Close();
    }
    m_sendSockets.clear();
    m_table.clear();

    Ipv4RoutingProtocol::DoDispose();
}

void
RoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    std::ostream* os = stream->GetStream();
    // Copy the current ostream state
    std::ios oldState(nullptr);
    oldState.copyfmt(*os);

    *os << std::resetiosflags(std::ios::adjustfield) << std::setiosflags(std::ios::left);

    *os << "Node: " << m_ipv4->GetObject<Node>()->GetId() << ", Time: " << Now().As(unit)
        << ", Local time: " << m_ipv4->GetObject<Node>()->GetLocalTime().As(unit)
        << ", POLSR Routing table" << std::endl;

    *os << std::setw(16) << "Destination";
    *os << std::setw(16) << "NextHop";
    *os << std::setw(16) << "Interface";
    *os << "Distance" << std::endl;

    for (auto iter = m_table.begin(); iter != m_table.end(); iter++)
    {
        std::ostringstream dest;
        std::ostringstream nextHop;
        dest << iter->first;
        nextHop << iter->second.nextAddr;
        *os << std::setw(16) << dest.str();
        *os << std::setw(16) << nextHop.str();
        *os << std::setw(16);
        if (!Names::FindName(m_ipv4->GetNetDevice(iter->second.interface)).empty())
        {
            *os << Names::FindName(m_ipv4->GetNetDevice(iter->second.interface));
        }
        else
        {
            *os << iter->second.interface;
        }
        *os << iter->second.distance << std::endl;
    }
    *os << std::endl;

    // Also print the HNA routing table
    if (m_hnaRoutingTable->GetNRoutes() > 0)
    {
        *os << "HNA Routing Table:" << std::endl;
        m_hnaRoutingTable->PrintRoutingTable(stream, unit);
    }
    else
    {
        *os << "HNA Routing Table: empty" << std::endl << std::endl;
    }
    // Restore the previous ostream state
    (*os).copyfmt(oldState);
}

void
RoutingProtocol::DoInitialize()
{
    if (m_mainAddress == Ipv4Address())
    {
        Ipv4Address loopback("127.0.0.1");
        for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
        {
            // Use primary address, if multiple
            Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
            if (addr != loopback)
            {
                m_mainAddress = addr;
                break;
            }
        }

        NS_ASSERT(m_mainAddress != Ipv4Address());
    }

    NS_LOG_DEBUG("Starting POLSR on node " << m_mainAddress);

    Ipv4Address loopback("127.0.0.1");

    bool canRunPolsr = false;
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
    {
        Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
        if (addr == loopback)
        {
            continue;
        }

        if (addr != m_mainAddress)
        {
            // Create never expiring interface association tuple entries for our
            // own network interfaces, so that GetMainAddress () works to
            // translate the node's own interface addresses into the main address.
            IfaceAssocTuple tuple;
            tuple.ifaceAddr = addr;
            tuple.mainAddr = m_mainAddress;
            AddIfaceAssocTuple(tuple);
            NS_ASSERT(GetMainAddress(addr) == m_mainAddress);
        }

        if (m_interfaceExclusions.find(i) != m_interfaceExclusions.end())
        {
            continue;
        }

        // Create a socket to listen on all the interfaces
        if (!m_recvSocket)
        {
            m_recvSocket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
            m_recvSocket->SetAllowBroadcast(true);
            InetSocketAddress inetAddr(Ipv4Address::GetAny(), POLSR_PORT_NUMBER);
            m_recvSocket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvPolsr, this));
            if (m_recvSocket->Bind(inetAddr))
            {
                NS_FATAL_ERROR("Failed to bind() POLSR socket");
            }
            m_recvSocket->SetRecvPktInfo(true);
            m_recvSocket->ShutdownSend();
        }

        // Create a socket to send packets from this specific interfaces
        Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
        socket->SetAllowBroadcast(true);
        socket->SetIpTtl(1);
        InetSocketAddress inetAddr(m_ipv4->GetAddress(i, 0).GetLocal(), POLSR_PORT_NUMBER);
        socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvPolsr, this));
        socket->BindToNetDevice(m_ipv4->GetNetDevice(i));
        if (socket->Bind(inetAddr))
        {
            NS_FATAL_ERROR("Failed to bind() POLSR socket");
        }
        socket->SetRecvPktInfo(true);
        m_sendSockets[socket] = m_ipv4->GetAddress(i, 0);

        canRunPolsr = true;
    }

    if (canRunPolsr)
    {
        HelloTimerExpire();
        TcTimerExpire();
        MidTimerExpire();
        HnaTimerExpire();

        NS_LOG_DEBUG("POLSR on node " << m_mainAddress << " started");
    }
}

void
RoutingProtocol::SetMainInterface(uint32_t interface)
{
    m_mainAddress = m_ipv4->GetAddress(interface, 0).GetLocal();
}

void
RoutingProtocol::SetInterfaceExclusions(std::set<uint32_t> exceptions)
{
    m_interfaceExclusions = exceptions;
}

//
// \brief Processes an incoming %POLSR packet following \RFC{3626} specification.
void
RoutingProtocol::RecvPolsr(Ptr<Socket> socket)
{
    Ptr<Packet> receivedPacket;
    Address sourceAddress;
    receivedPacket = socket->RecvFrom(sourceAddress);

    Ipv4PacketInfoTag interfaceInfo;
    if (!receivedPacket->RemovePacketTag(interfaceInfo))
    {
        NS_ABORT_MSG("No incoming interface on POLSR message, aborting.");
    }
    uint32_t incomingIf = interfaceInfo.GetRecvIf();
    Ptr<Node> node = this->GetObject<Node>();
    Ptr<NetDevice> dev = node->GetDevice(incomingIf);
    uint32_t recvInterfaceIndex = m_ipv4->GetInterfaceForDevice(dev);

    if (m_interfaceExclusions.find(recvInterfaceIndex) != m_interfaceExclusions.end())
    {
        return;
    }

    InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom(sourceAddress);
    Ipv4Address senderIfaceAddr = inetSourceAddr.GetIpv4();

    int32_t interfaceForAddress = m_ipv4->GetInterfaceForAddress(senderIfaceAddr);
    if (interfaceForAddress != -1)
    {
        NS_LOG_LOGIC("Ignoring a packet sent by myself.");
        return;
    }

    Ipv4Address receiverIfaceAddr = m_ipv4->GetAddress(recvInterfaceIndex, 0).GetLocal();
    NS_ASSERT(receiverIfaceAddr != Ipv4Address());
    NS_LOG_DEBUG("POLSR node " << m_mainAddress << " received a POLSR packet from " << senderIfaceAddr
                              << " to " << receiverIfaceAddr);

    // All routing messages are sent from and to port RT_PORT,
    // so we check it.
    NS_ASSERT(inetSourceAddr.GetPort() == POLSR_PORT_NUMBER);

    Ptr<Packet> packet = receivedPacket;

    polsr::PacketHeader polsrPacketHeader;
    packet->RemoveHeader(polsrPacketHeader);
    NS_ASSERT(polsrPacketHeader.GetPacketLength() >= polsrPacketHeader.GetSerializedSize());
    uint32_t sizeLeft = polsrPacketHeader.GetPacketLength() - polsrPacketHeader.GetSerializedSize();

    MessageList messages;

    while (sizeLeft)
    {
        MessageHeader messageHeader;
        if (packet->RemoveHeader(messageHeader) == 0)
        {
            NS_ASSERT(false);
        }

        sizeLeft -= messageHeader.GetSerializedSize();

        NS_LOG_DEBUG("Polsr Msg received with type "
                     << std::dec << int(messageHeader.GetMessageType())
                     << " TTL=" << int(messageHeader.GetTimeToLive())
                     << " origAddr=" << messageHeader.GetOriginatorAddress());
        messages.push_back(messageHeader);
    }

    m_rxPacketTrace(polsrPacketHeader, messages);

    for (auto messageIter = messages.begin(); messageIter != messages.end(); messageIter++)
    {
        const MessageHeader& messageHeader = *messageIter;
        // If ttl is less than or equal to zero, or
        // the receiver is the same as the originator,
        // the message must be silently dropped
        if (messageHeader.GetTimeToLive() == 0 ||
            messageHeader.GetOriginatorAddress() == m_mainAddress)
        {
            packet->RemoveAtStart(messageHeader.GetSerializedSize() -
                                  messageHeader.GetSerializedSize());
            continue;
        }

        // If the message has been processed it must not be processed again
        bool do_forwarding = true;
        DuplicateTuple* duplicated =
            m_state.FindDuplicateTuple(messageHeader.GetOriginatorAddress(),
                                       messageHeader.GetMessageSequenceNumber());

        // Get main address of the peer, which may be different from the packet source address
        //       const IfaceAssocTuple *ifaceAssoc = m_state.FindIfaceAssocTuple
        //       (inetSourceAddr.GetIpv4 ()); Ipv4Address peerMainAddress; if (ifaceAssoc != NULL)
        //         {
        //           peerMainAddress = ifaceAssoc->mainAddr;
        //         }
        //       else
        //         {
        //           peerMainAddress = inetSourceAddr.GetIpv4 () ;
        //         }

        if (duplicated == nullptr)
        {
            switch (messageHeader.GetMessageType())
            {
            case polsr::MessageHeader::HELLO_MESSAGE:
                NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                             << " POLSR node " << m_mainAddress << " received HELLO message of size "
                             << messageHeader.GetSerializedSize());
                ProcessHello(messageHeader, receiverIfaceAddr, senderIfaceAddr);
                break;

            case polsr::MessageHeader::TC_MESSAGE:
                NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                             << " POLSR node " << m_mainAddress << " received TC message of size "
                             << messageHeader.GetSerializedSize());
                ProcessTc(messageHeader, senderIfaceAddr);
                break;

            case polsr::MessageHeader::MID_MESSAGE:
                NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                             << " POLSR node " << m_mainAddress << " received MID message of size "
                             << messageHeader.GetSerializedSize());
                ProcessMid(messageHeader, senderIfaceAddr);
                break;
            case polsr::MessageHeader::HNA_MESSAGE:
                NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                             << " POLSR node " << m_mainAddress << " received HNA message of size "
                             << messageHeader.GetSerializedSize());
                ProcessHna(messageHeader, senderIfaceAddr);
                break;

            default:
                NS_LOG_DEBUG("POLSR message type " << int(messageHeader.GetMessageType())
                                                  << " not implemented");
            }
        }
        else
        {
            NS_LOG_DEBUG("POLSR message is duplicated, not reading it.");

            // If the message has been considered for forwarding, it should
            // not be retransmitted again
            for (auto it = duplicated->ifaceList.begin(); it != duplicated->ifaceList.end(); it++)
            {
                if (*it == receiverIfaceAddr)
                {
                    do_forwarding = false;
                    break;
                }
            }
        }

        if (do_forwarding)
        {
            // HELLO messages are never forwarded.
            // TC and MID messages are forwarded using the default algorithm.
            // Remaining messages are also forwarded using the default algorithm.
            if (messageHeader.GetMessageType() != polsr::MessageHeader::HELLO_MESSAGE)
            {
                ForwardDefault(messageHeader,
                               duplicated,
                               receiverIfaceAddr,
                               inetSourceAddr.GetIpv4());
            }
        }
    }

    // After processing all POLSR messages, we must recompute the routing table
    RoutingTableComputation();
}

///
/// @brief This auxiliary function (defined in \RFC{3626}) is used for calculating the MPR Set.
///
/// @param tuple the neighbor tuple which has the main address of the node we are going to calculate
/// its degree to.
/// @return the degree of the node.
///
int
RoutingProtocol::Degree(const NeighborTuple& tuple)
{
    int degree = 0;
    for (auto it = m_state.GetTwoHopNeighbors().begin(); it != m_state.GetTwoHopNeighbors().end();
         it++)
    {
        const TwoHopNeighborTuple& nb2hop_tuple = *it;
        if (nb2hop_tuple.neighborMainAddr == tuple.neighborMainAddr)
        {
            const NeighborTuple* nb_tuple =
                m_state.FindNeighborTuple(nb2hop_tuple.twoHopNeighborAddr);
            if (nb_tuple == nullptr)
            {
                degree++;
            }
        }
    }
    return degree;
}

namespace
{
///
/// @brief Remove all covered 2-hop neighbors from N2 set.
/// This is a helper function used by MprComputation algorithm.
///
/// @param neighborMainAddr Neighbor main address.
/// @param N2 Reference to the 2-hop neighbor set.
///
void
CoverTwoHopNeighbors(Ipv4Address neighborMainAddr, TwoHopNeighborSet& N2)
{
    // first gather all 2-hop neighbors to be removed
    std::set<Ipv4Address> toRemove;
    for (auto twoHopNeigh = N2.begin(); twoHopNeigh != N2.end(); twoHopNeigh++)
    {
        if (twoHopNeigh->neighborMainAddr == neighborMainAddr)
        {
            toRemove.insert(twoHopNeigh->twoHopNeighborAddr);
        }
    }
    // Now remove all matching records from N2
    for (auto twoHopNeigh = N2.begin(); twoHopNeigh != N2.end();)
    {
        if (toRemove.find(twoHopNeigh->twoHopNeighborAddr) != toRemove.end())
        {
            twoHopNeigh = N2.erase(twoHopNeigh);
        }
        else
        {
            twoHopNeigh++;
        }
    }
}
} // unnamed namespace

void
RoutingProtocol::MprComputation()
{
    NS_LOG_FUNCTION(this);

    // MPR 计算应针对每个接口分别进行，参见 RFC 3626 第 8.3.1 节。
    // 具体规则见 RFC 3626。
    MprSet mprSet;

    // N 表示本节点邻居集合中的一个子集，
    // 即“接口 I 上的邻居”。
    NeighborSet N;
    for (auto neighbor = m_state.GetNeighbors().begin(); neighbor != m_state.GetNeighbors().end();
         neighbor++)
    {
        if (neighbor->status == NeighborTuple::STATUS_SYM) // I think that we need this check
        {
            N.push_back(*neighbor);
        }
    }

    // N2 表示从“接口 I 上的邻居”可以到达的二跳邻居集合，
    // 但需要排除以下情况：
    // (i) 只能通过 willingness 为 Willingness::NEVER 的 N 成员到达的节点；
    // (ii) 正在执行 MPR 计算的本节点；
    // (iii) 所有对称一跳邻居，也就是那些已经存在对称链路的节点。
    // 这些节点已经通过某个接口与本节点存在对称链路。
    TwoHopNeighborSet N2;
    for (auto twoHopNeigh = m_state.GetTwoHopNeighbors().begin();
         twoHopNeigh != m_state.GetTwoHopNeighbors().end();
         twoHopNeigh++)
    {
        // 排除：
        // (ii) 正在执行 MPR 计算的本节点；
        if (twoHopNeigh->twoHopNeighborAddr == m_mainAddress)
        {
            continue;
        }

        //  排除：
        // (i) 只能通过 willingness 为 Willingness::NEVER 的 N 成员到达的节点；
        bool ok = false;
        for (auto neigh = N.begin(); neigh != N.end(); neigh++)
        {
            if (neigh->neighborMainAddr == twoHopNeigh->neighborMainAddr)
            {
                ok = (neigh->willingness != Willingness::NEVER);
                break;
            }
        }
        if (!ok)
        {
            continue;
        }

        // 排除：
        // (iii) 所有对称一跳邻居，也就是那些已经存在对称链路的节点。
        // 这些节点已经通过某个接口与本节点存在对称链路。
        for (auto neigh = N.begin(); neigh != N.end(); neigh++)
        {
            if (neigh->neighborMainAddr == twoHopNeigh->twoHopNeighborAddr)
            {
                ok = false;
                break;
            }
        }

        if (ok)
        {
            N2.push_back(*twoHopNeigh);
        }
    }

#ifdef NS3_LOG_ENABLE
    {
        std::ostringstream os;
        os << "[";
        for (auto iter = N2.begin(); iter != N2.end(); iter++)
        {
            auto next = iter;
            next++;
            os << iter->neighborMainAddr << "->" << iter->twoHopNeighborAddr;
            if (next != N2.end())
            {
                os << ", ";
            }
        }
        os << "]";
        NS_LOG_DEBUG("N2: " << os.str());
    }
#endif // NS3_LOG_ENABLE

    // 1. 初始 MPR 集合先放入 N 中所有 willingness 为 WILL_ALWAYS 的邻居。
    // 这些邻居的 N_willingness 等于 Willingness::ALWAYS。
    for (auto neighbor = N.begin(); neighbor != N.end(); neighbor++)
    {
        if (neighbor->willingness == Willingness::ALWAYS)
        {
            mprSet.insert(neighbor->neighborMainAddr);
            // (not in RFC but I think is needed: remove the 2-hop
            // 从 N2 中移除该 MPR 能够到达的二跳邻居。
            CoverTwoHopNeighbors(neighbor->neighborMainAddr, N2);
        }
    }

    // 2. 对 N 中每个节点 y 计算 D(y)。
    // 这里稍后再计算。

    // 3. 将 N 中唯一能到达某些二跳邻居的节点加入 MPR 集合。
    // 也就是说，如果某个二跳邻居只能通过一个一跳邻居到达，该一跳邻居必须成为 MPR。
    std::set<Ipv4Address> coveredTwoHopNeighbors;
    for (auto twoHopNeigh = N2.begin(); twoHopNeigh != N2.end(); twoHopNeigh++)
    {
        bool onlyOne = true;
        // try to find another neighbor that can reach twoHopNeigh->twoHopNeighborAddr
        for (auto otherTwoHopNeigh = N2.begin(); otherTwoHopNeigh != N2.end(); otherTwoHopNeigh++)
        {
            if (otherTwoHopNeigh->twoHopNeighborAddr == twoHopNeigh->twoHopNeighborAddr &&
                otherTwoHopNeigh->neighborMainAddr != twoHopNeigh->neighborMainAddr)
            {
                onlyOne = false;
                break;
            }
        }
        if (onlyOne)
        {
            NS_LOG_LOGIC("Neighbor " << twoHopNeigh->neighborMainAddr
                                     << " is the only that can reach 2-hop neigh. "
                                     << twoHopNeigh->twoHopNeighborAddr << " => select as MPR.");

            mprSet.insert(twoHopNeigh->neighborMainAddr);

            // 记录所有能够通过新选出的 MPR 到达的二跳邻居。
            for (auto otherTwoHopNeigh = N2.begin(); otherTwoHopNeigh != N2.end();
                 otherTwoHopNeigh++)
            {
                if (otherTwoHopNeigh->neighborMainAddr == twoHopNeigh->neighborMainAddr)
                {
                    coveredTwoHopNeighbors.insert(otherTwoHopNeigh->twoHopNeighborAddr);
                }
            }
        }
    }
    // Remove the nodes from N2 which are now covered by a node in the MPR set.
    for (auto twoHopNeigh = N2.begin(); twoHopNeigh != N2.end();)
    {
        if (coveredTwoHopNeighbors.find(twoHopNeigh->twoHopNeighborAddr) !=
            coveredTwoHopNeighbors.end())
        {
            // 这里之所以能正确工作，是因为已知 twoHopNeigh 只通过
            // 一个邻居可达，因此 N2 中每个这样的节点只会有一条记录。
            // 这里删除的正是这条记录。
            NS_LOG_LOGIC("2-hop neigh. " << twoHopNeigh->twoHopNeighborAddr
                                         << " is already covered by an MPR.");
            twoHopNeigh = N2.erase(twoHopNeigh);
        }
        else
        {
            twoHopNeigh++;
        }
    }

    // 4. 只要 N2 中仍存在未被至少一个 MPR 覆盖的节点，
    // 就继续执行选择过程。
    while (N2.begin() != N2.end())
    {
#ifdef NS3_LOG_ENABLE
        {
            std::ostringstream os;
            os << "[";
            for (auto iter = N2.begin(); iter != N2.end(); iter++)
            {
                auto next = iter;
                next++;
                os << iter->neighborMainAddr << "->" << iter->twoHopNeighborAddr;
                if (next != N2.end())
                {
                    os << ", ";
                }
            }
            os << "]";
            NS_LOG_DEBUG("Step 4 iteration: N2=" << os.str());
        }
#endif // NS3_LOG_ENABLE

        // 4.1. 对 N 中每个节点计算 reachability，
        // 即该节点还能覆盖多少尚未被当前 MPR 集合覆盖的 N2 节点。
        // 这些节点必须能通过该候选一跳邻居到达。
        // 也就是通过这个一跳邻居可达。
        std::map<int, std::vector<const NeighborTuple*>> reachability;
        std::set<int> rs;
        for (auto it = N.begin(); it != N.end(); it++)
        {
            const NeighborTuple& nb_tuple = *it;
            int r = 0;
            for (auto it2 = N2.begin(); it2 != N2.end(); it2++)
            {
                const TwoHopNeighborTuple& nb2hop_tuple = *it2;
                if (nb_tuple.neighborMainAddr == nb2hop_tuple.neighborMainAddr)
                {
                    r++;
                }
            }
            rs.insert(r);
            reachability[r].push_back(&nb_tuple);
        }

        // 4.2. 在仍能覆盖未覆盖二跳邻居的节点中，优先选择 N_willingness 最大的节点作为 MPR。
        // 如果多个节点的 willingness 相同，
        // 则选择可达性更高的节点。
        // 也就是优先覆盖更多 N2 节点的候选节点。
        // 如果覆盖数量仍然相同，
        // 则选择 D(y) 更大的节点作为 MPR。
        // 选定后，从 N2 中移除已经被该 MPR 覆盖的二跳邻居。
        // 这些节点已经由 MPR 集合保证可达。
        const NeighborTuple* max = nullptr;
        int max_r = 0;
        for (auto it = rs.begin(); it != rs.end(); it++)
        {
            int r = *it;
            if (r == 0)
            {
                continue;
            }
            for (auto it2 = reachability[r].begin(); it2 != reachability[r].end(); it2++)
            {
                const NeighborTuple* nb_tuple = *it2;
                if (max == nullptr || nb_tuple->willingness > max->willingness)
                {
                    max = nb_tuple;
                    max_r = r;
                }
                else if (nb_tuple->willingness == max->willingness)
                {
                    if (r > max_r)
                    {
                        max = nb_tuple;
                        max_r = r;
                    }
                    else if (r == max_r)
                    {
                        int degree = Degree(*nb_tuple);
                        int maxDegree = Degree(*max);
                        if (degree > maxDegree)
                        {
                            max = nb_tuple;
                            max_r = r;
                        }
                        else if (m_enablePrediction && m_usePredictionForMpr &&
                                 degree == maxDegree &&
                                 GetNeighborPredictionCost(nb_tuple->neighborMainAddr) <
                                     GetNeighborPredictionCost(max->neighborMainAddr))
                        {
                            /*
                             * P-OLSR MPR 辅助选择。
                             *
                             * 原始 OLSR 已经按 willingness、reachability、degree 做了选择。
                             * 只有这些标准条件完全打平时，才用预测代价更低的邻居作为
                             * tie-break。这样既能让稳定链路更容易成为 MPR，又不会改变
                             * OLSR 对二跳覆盖和 willingness 的基本约束。
                             */
                            max = nb_tuple;
                            max_r = r;
                        }
                    }
                }
            }
        }

        if (max != nullptr)
        {
            mprSet.insert(max->neighborMainAddr);
            CoverTwoHopNeighbors(max->neighborMainAddr, N2);
            NS_LOG_LOGIC(N2.size() << " 2-hop neighbors left to cover!");
        }
    }

#ifdef NS3_LOG_ENABLE
    {
        std::ostringstream os;
        os << "[";
        for (auto iter = mprSet.begin(); iter != mprSet.end(); iter++)
        {
            auto next = iter;
            next++;
            os << *iter;
            if (next != mprSet.end())
            {
                os << ", ";
            }
        }
        os << "]";
        NS_LOG_DEBUG("Computed MPR set for node " << m_mainAddress << ": " << os.str());
    }
#endif // NS3_LOG_ENABLE

    m_state.SetMprSet(mprSet);
}

Ipv4Address
RoutingProtocol::GetMainAddress(Ipv4Address iface_addr) const
{
    const IfaceAssocTuple* tuple = m_state.FindIfaceAssocTuple(iface_addr);

    if (tuple != nullptr)
    {
        return tuple->mainAddr;
    }
    else
    {
        return iface_addr;
    }
}

Vector
RoutingProtocol::LookupOwnPosition() const
{
    Ptr<Node> node = GetObject<Node>();
    if (!node)
    {
        return Vector();
    }
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    if (!mobility)
    {
        return Vector();
    }
    return mobility->GetPosition();
}

Vector
RoutingProtocol::LookupOwnVelocity() const
{
    Ptr<Node> node = GetObject<Node>();
    if (!node)
    {
        return Vector();
    }
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    if (!mobility)
    {
        return Vector();
    }
    return mobility->GetVelocity();
}

double
RoutingProtocol::ComputeRadialRelativeSpeed(const Vector& localPosition,
                                            const Vector& localVelocity,
                                            const Vector& neighborPosition,
                                            const Vector& neighborVelocity) const
{
    double dx = neighborPosition.x - localPosition.x;
    double dy = neighborPosition.y - localPosition.y;
    double dz = neighborPosition.z - localPosition.z;
    double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance <= 0.0)
    {
        return 0.0;
    }

    double dvx = neighborVelocity.x - localVelocity.x;
    double dvy = neighborVelocity.y - localVelocity.y;
    double dvz = neighborVelocity.z - localVelocity.z;
    double radialSpeed = (dx * dvx + dy * dvy + dz * dvz) / distance;
    return std::isfinite(radialSpeed) ? radialSpeed : 0.0;
}

void
RoutingProtocol::UpdatePredictionFromHello(const polsr::MessageHeader& msg,
                                           const polsr::MessageHeader::Hello& hello,
                                           const Ipv4Address& receiverIface,
                                           const Ipv4Address& senderIface)
{
    /*
     * P-OLSR HELLO 处理入口。
     *
     * 调用位置在 ProcessHello() 的 LinkSensing() 之后：
     * 1. LinkSensing() 先按原始 OLSR 规则确认或创建 LinkTuple；
     * 2. 这里再把 HELLO 中的运动信息转换成预测指标；
     * 3. 预测指标只更新已存在的链路，不负责发现新链路。
     *
     * 这样做的目的是保持 OLSR 的邻居发现语义不变，只在已有链路上
     * 叠加 P-OLSR 的稳定性估计。
     */
    if (!m_enablePrediction)
    {
        return;
    }

    Time now = Simulator::Now(); //
    m_state.PurgeExpiredNodeMotionTuples(now); // 清理过期的邻居运动信息

    LinkTuple* link = m_state.FindLinkTuple(receiverIface, senderIface);
    if (!link)
    {
        return;
    }

    Vector neighborPosition(hello.longitude, hello.latitude, hello.altitude);
    if (hello.hasGps) //
    {
        // ns-3 场景通常使用本地米制坐标；这里按论文字段名放入经度/纬度/高度槽位。
        m_state.UpdateNodeMotionTuple(msg.GetOriginatorAddress(),
                                      neighborPosition,
                                      hello.longitude,
                                      hello.latitude,
                                      hello.altitude,
                                      now,
                                      now + m_predictionValidityTime); //m_predictionValidityTime 预测有效时间
    }

    /*对phi进行更新，rho和relativeSpeed继承上一次的值*/
    double phi = (1.0 - m_predictionAlpha) * link->phi + m_predictionAlpha;
    double rho = link->rho;
    double relativeSpeed = link->relativeSpeed;

    for (const auto& linkMessage : hello.linkMessages)
    {
        for (const auto& quality : linkMessage.neighborLinkQualities)
        {
            if (quality.qualityValid && (quality.address == receiverIface ||
                                         quality.address == m_mainAddress ||
                                         IsMyOwnAddress(quality.address)))
            {
                /*
                 * 收到邻居的 HELLO 时，本节点可直接估计“邻居 -> 本节点”的接收率 phi；
                 * 邻居在 HELLO 中回告的 forwardReceivingRatio 则表示“本节点 -> 邻居”
                 * 方向的接收率。两者相乘就是 ETX 分母中的 phi*rho。
                 */
                rho = quality.forwardReceivingRatio;
                if (std::isfinite(quality.relativeSpeed))
                {
                    relativeSpeed = (1.0 - m_predictionGamma) * relativeSpeed +
                                    m_predictionGamma * quality.relativeSpeed;
                }
            }
        }
    }

    if (hello.hasGps)
    {
        /*
         * relativeSpeed 的物理含义：
         * - 正值：邻居沿两节点连线方向远离本节点，链路更可能变差；
         * - 负值：邻居靠近本节点，链路短期内更稳定；
         * - 0 附近：距离变化不明显。
         *
         * 论文公式使用相邻 HELLO 的距离变化率：
         *   v_l = gamma * ((d_l - d_{l-1}) / dt) + (1-gamma) * v_{l-1}
         */
        Vector localPosition = LookupOwnPosition();
        double dx = neighborPosition.x - localPosition.x;
        double dy = neighborPosition.y - localPosition.y;
        double dz = neighborPosition.z - localPosition.z;
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (link->previousDistanceValid)
        {
            double dt = (now - link->previousPredictionTime).GetSeconds();
            if (dt > 0.0)
            {
                double sampledRelativeSpeed = (distance - link->previousDistance) / dt;
                if (std::isfinite(sampledRelativeSpeed)) //
                {
                    relativeSpeed = m_predictionGamma * sampledRelativeSpeed +
                                    (1.0 - m_predictionGamma) * relativeSpeed;
                }
            }
        }

        link->previousDistance = distance;
        link->previousPredictionTime = now;
        link->previousDistanceValid = true;
    }

    phi = std::min(1.0, std::max(0.0, phi));
    rho = std::min(1.0, std::max(0.0, rho));
    double quality = std::max(m_minPredictionQuality, phi * rho);
    /*
     * 论文中的速度加权 ETX：
     *   ETX = exp(beta * relativeSpeed) / (phi * rho)
     * 正相对速度会放大代价，负相对速度会降低代价。
     */
    double weightedEtx = std::exp(m_predictionBeta * relativeSpeed) / quality;
    double predictedCost = std::min(m_maxPredictionCost, std::max(m_minPredictionQuality, weightedEtx));
    if (!std::isfinite(predictedCost))
    {
        predictedCost = m_maxPredictionCost;
    }

    m_state.UpdateLinkPrediction(receiverIface,
                                 senderIface,
                                 phi,
                                 rho,
                                 relativeSpeed,
                                 predictedCost,
                                 now);
    const double interval = m_helloInterval.GetSeconds();
    if (interval > 0.0)
    {
        link->nextExpectedHelloTime = now + Seconds(interval);
        link->helloDeadlineTime = now + Seconds(m_helloDeadlineFactor * interval);
        link->helloDeadlineValid = true;
    }
}

void
RoutingProtocol::ApplyHelloLossDecay()
{
    if (!m_enablePrediction)
    {
        return;
    }

    const double interval = m_helloInterval.GetSeconds();
    if (interval <= 0.0)
    {
        return;
    }

    Time now = Simulator::Now();
    LinkSet& links = m_state.GetLinks();
    bool changed = false;
    for (auto& link : links)
    {
        if (!link.predictionValid || link.time < now)
        {
            /*这条链路还没有有效的预测信息或这条链路已经过期，跳过这条链路，不参与后面的计算。*/
            continue;
        }

        if (!link.helloDeadlineValid)
        {
            link.nextExpectedHelloTime = link.predictionTime + Seconds(interval);
            link.helloDeadlineTime = link.predictionTime + Seconds(m_helloDeadlineFactor * interval);
            link.helloDeadlineValid = true;
        }

        if (now < link.helloDeadlineTime)
        {
            continue;
        }

        double elapsed = (now - link.helloDeadlineTime).GetSeconds();
        uint32_t missedIntervals = static_cast<uint32_t>(std::floor(elapsed / interval)) + 1;
        if (missedIntervals == 0)
        {
            continue;
        }

        link.nextExpectedHelloTime += Seconds(interval * missedIntervals);
        link.helloDeadlineTime += Seconds(interval * missedIntervals);

        /*
         * ETX receiving-ratio update with a missed HELLO:
         *   phi_l = alpha * 0 + (1 - alpha) * phi_{l-1}
         * If several HELLO periods elapsed, apply the same recurrence repeatedly.
         */
        link.phi *= std::pow(1.0 - m_predictionAlpha, missedIntervals);
        link.phi = std::min(1.0, std::max(0.0, link.phi));

        double quality = std::max(m_minPredictionQuality, link.phi * link.rho);
        double weightedEtx = std::exp(m_predictionBeta * link.relativeSpeed) / quality;
        link.predictedCost =
            std::min(m_maxPredictionCost, std::max(m_minPredictionQuality, weightedEtx));
        if (!std::isfinite(link.predictedCost))
        {
            link.predictedCost = m_maxPredictionCost;
        }
        link.predictionTime = now;
        changed = true;
    }

    if (changed)
    {
        if (m_usePredictionForMpr)
        {
            MprComputation();
        }
        if (m_usePredictionForRouting)
        {
            RoutingTableComputation();
        }
    }
}

double
RoutingProtocol::GetNeighborPredictionCost(const Ipv4Address& neighborMainAddr) const
{
    Time now = Simulator::Now();
    double bestCost = std::numeric_limits<double>::infinity();
    for (const auto& link : m_state.GetLinks())
    {
        if (GetMainAddress(link.neighborIfaceAddr) == neighborMainAddr && link.time >= now)
        {
            double cost = link.predictionValid ? link.predictedCost : 1.0;
            bestCost = std::min(bestCost, std::max(m_minPredictionQuality, cost));
        }
    }
    return std::isfinite(bestCost) ? bestCost : 1.0;
}

double
RoutingProtocol::GetNeighborForwardReceivingRatio(const Ipv4Address& neighborMainAddr) const
{
    Time now = Simulator::Now();
    double bestCost = std::numeric_limits<double>::infinity();
    double bestPhi = 1.0;
    for (const auto& link : m_state.GetLinks())
    {
        if (GetMainAddress(link.neighborIfaceAddr) == neighborMainAddr && link.time >= now)
        {
            double cost = link.predictionValid ? link.predictedCost : 1.0;
            if (cost < bestCost)
            {
                bestCost = cost;
                bestPhi = link.predictionValid ? link.phi : 1.0;
            }
        }
    }
    return bestPhi;
}

double
RoutingProtocol::GetNeighborReverseReceivingRatio(const Ipv4Address& neighborMainAddr) const
{
    Time now = Simulator::Now();
    double bestCost = std::numeric_limits<double>::infinity();
    double bestRho = 1.0;
    for (const auto& link : m_state.GetLinks())
    {
        if (GetMainAddress(link.neighborIfaceAddr) == neighborMainAddr && link.time >= now)
        {
            double cost = link.predictionValid ? link.predictedCost : 1.0;
            if (cost < bestCost)
            {
                bestCost = cost;
                bestRho = link.predictionValid ? link.rho : 1.0;
            }
        }
    }
    return bestRho;
}

double
RoutingProtocol::GetNeighborRelativeSpeed(const Ipv4Address& neighborMainAddr) const
{
    Time now = Simulator::Now();
    double bestCost = std::numeric_limits<double>::infinity();
    double bestSpeed = 0.0;
    for (const auto& link : m_state.GetLinks())
    {
        if (GetMainAddress(link.neighborIfaceAddr) == neighborMainAddr && link.time >= now)
        {
            double cost = link.predictionValid ? link.predictedCost : 1.0;
            if (cost < bestCost)
            {
                bestCost = cost;
                bestSpeed = link.predictionValid ? link.relativeSpeed : 0.0;
            }
        }
    }
    return bestSpeed;
}

void
RoutingProtocol::PredictiveRoutingTableComputation()
{
    /*
     * P-OLSR 路由表计算。
     *
     * 原始 RoutingTableComputation() 是按 hop-count 分层扩展：
     * 一跳距离为 1，二跳距离为 2，TC 拓扑再按 h+1 逐层加入。
     *
     * 这里改为构造带权图并按 predictedCost 选择路径：
     * - 本节点到一跳邻居：使用 LinkTuple.predictedCost；
     * - 从 HELLO 学到的二跳边：没有对端预测值时使用默认 cost=1；
     * - 从 TC 学到的拓扑边：优先使用 TopologyTuple.predictedCost；
     * - 没有预测值时退化为 cost=1，等价于原 OLSR 的无权边。
     *
     * 最终写入 m_table 的接口仍是 AddEntry()，distance 字段仍保存跳数，
     * 这样不会破坏 ns-3 Ipv4RoutingProtocol 对路由表项的使用方式。
     */
    /*
     * P-OLSR 预测路由表计算入口。
     *
     * 这个函数只在同时满足：
     *   m_enablePrediction == true
     *   m_usePredictionForRouting == true
     * 时由 RoutingTableComputation() 调用。
     *
     * 原始 OLSR 的 RoutingTableComputation() 主要按跳数扩展路由表：
     *   1. 一跳邻居 distance = 1；
     *   2. 二跳邻居 distance = 2；
     *   3. TC 拓扑信息继续按 h + 1 逐层扩展。
     *
     * 这里的预测路由计算不再单纯比较 hop-count，而是先把当前节点已知的
     * Link Set、TwoHopNeighbor Set、Topology Set 转换成一张带权有向图，
     * 再用类似 Dijkstra 的方式选择累计 predictedCost 最小的路径。
     *
     * 图中边的来源：
     *   1. 本节点 -> 一跳邻居：
     *      来自 LinkTuple。
     *      如果 link.predictionValid 为 true，使用 link.predictedCost；
     *      否则使用 cost = 1.0，退化为普通 OLSR 的无权边。
     *
     *   2. 一跳邻居 -> 二跳邻居：
     *      来自 HELLO 处理后维护的 TwoHopNeighborTuple。
     *      当前节点不能直接测量“邻居到二跳节点”这条边的相对速度、
     *      phi/rho 或 predictedCost，所以这里固定使用 cost = 1.0。
     *
     *   3. TC 中的拓扑边：
     *      来自 TopologyTuple。
     *      如果远端 P-OLSR 节点在 TC 中携带了 prediction 信息，
     *      使用 topologyTuple.predictedCost；
     *      如果是普通 OLSR TC 或 predictionValid 为 false，
     *      同样退化为 cost = 1.0。
     *
     * 注意：
     *   predictedCost 只影响“选哪条路径”。
     *   最终 AddEntry() 写入 m_table 时，distance 字段仍然保存跳数 hops，
     *   这样不会改变 ns-3 Ipv4RoutingProtocol 使用路由表项的方式。
     */
    NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                 << " : Node " << m_mainAddress
                 << ": PredictiveRoutingTableComputation begin...");

    /*
     * PredictiveEdge 表示带权图中的一条边。
     *
     * to:
     *   这条边指向的目标地址。
     *
     * nextHop:
     *   如果这条边是从本节点 m_mainAddress 发出的第一跳边，
     *   nextHop 保存真正要发给的下一跳接口地址。
     *   对二跳边和 TC 拓扑边，本函数后面会继承路径上的 firstHop，
     *   因此这里可以暂时为空地址。
     *
     * localIface:
     *   本节点发往 firstHop 时使用的本地接口地址。
     *   只有第一跳边需要设置，后续多跳边会沿用第一跳的 localIface。
     *
     * cost:
     *   这条边的预测代价，通常来自 predictedCost。
     *   没有预测信息时使用 1.0。
     */
    struct PredictiveEdge
    {
        Ipv4Address to;
        Ipv4Address nextHop;
        Ipv4Address localIface;
        double cost;
    };
    /*
     * PredictiveNodeState 保存从本节点到某个目的地址的当前最优路径状态。
     *
     * cost:
     *   从 m_mainAddress 到该目的地址的累计预测代价。
     *
     * hops:
     *   当前路径的跳数。它不参与主要代价比较，只在代价相同时作为 tie-breaker，
     *   并且最终写入 RoutingTableEntry.distance。
     *
     * firstHop:
     *   从本节点出发时应该交给哪个下一跳。
     *   即使目的地址是多跳之外的节点，路由表里也只需要保存第一跳。
     *
     * localIface:
     *   发往 firstHop 时使用的本地接口。
     *
     * reached:
     *   表示这个目的地址是否已经被发现过。
     *
     * settled:
     *   表示这个目的地址的最优代价是否已经确定。
     */
    struct PredictiveNodeState
    {
        double cost{std::numeric_limits<double>::infinity()};
        uint32_t hops{0};
        Ipv4Address firstHop;
        Ipv4Address localIface;
        bool reached{false};
        bool settled{false};
    };

    /*
    * 对边代价做保护。
    *
    * predictedCost 理论上来自 phi、rho、relativeSpeed 等预测量，
    * 但如果上游计算出现 NaN、Inf 或非正数，直接进入 Dijkstra 会导致
    * 路径比较异常。因此这里把非法代价退回 1.0。
    *
    * 注意这里没有强制 cost >= 1.0。
    * 因为当前实现允许“接近中的链路”得到小于 1 的 speed-weighted ETX，
    * 这样 P-OLSR 才能偏向预测更稳定或更有利的链路。
    */
    auto clampCost = [this](double cost) {
        // 防止 NaN/Inf 或非正代价进入路径计算；速度加权 ETX 允许接近链路 cost < 1。
        if (!std::isfinite(cost) || cost <= 0.0)
        {
            return 1.0;
        }
        return std::min(cost, m_maxPredictionCost);
    };

    std::map<Ipv4Address, std::vector<PredictiveEdge>> graph;
    auto addEdge = [&graph, &clampCost](const Ipv4Address& from,
                                        const Ipv4Address& to,
                                        const Ipv4Address& nextHop,
                                        const Ipv4Address& localIface,
                                        double cost) {
        graph[from].push_back(PredictiveEdge{to, nextHop, localIface, clampCost(cost)});
    };

    Time now = Simulator::Now();
    const LinkSet& linkSet = m_state.GetLinks();
    for (const auto& link : linkSet)
    {
        /*
         * 一跳边来自 Link Set，是本节点能够直接发送数据的邻居链路。
         *
         * 只有同时满足：
         *   1. link.symTime >= now，链路仍然是对称链路；
         *   2. link.time >= now，LinkTuple 本身没有过期；
         *   3. NeighborTuple 仍然是对称邻居；
         * 才把它加入预测图。
         *
         * 如果 link.predictionValid 为 true，说明 UpdatePredictionFromHello()
         * 或 ApplyHelloLossDecay() 已经给这条链路维护了有效 predictedCost。
         * 否则使用 cost = 1.0，保持和普通 OLSR 一样的可达性。
         */
        if (link.symTime < now || link.time < now)
        {
            continue;
        }

        Ipv4Address neighborMainAddr = GetMainAddress(link.neighborIfaceAddr);
        const NeighborTuple* neighbor = m_state.FindSymNeighborTuple(neighborMainAddr);
        if (!neighbor)
        {
            continue;
        }

        double cost = link.predictionValid ? link.predictedCost : 1.0;
        addEdge(m_mainAddress, link.neighborIfaceAddr, link.neighborIfaceAddr, link.localIfaceAddr, cost);
        if (link.neighborIfaceAddr != neighborMainAddr)
        {
            addEdge(m_mainAddress, neighborMainAddr, link.neighborIfaceAddr, link.localIfaceAddr, cost);
        }
    }

    const NeighborSet& neighborSet = m_state.GetNeighbors();
    const TwoHopNeighborSet& twoHopNeighbors = m_state.GetTwoHopNeighbors();
    for (const auto& twoHop : twoHopNeighbors)
    {
        /*
         * 二跳边来自 HELLO 中邻居通告的邻居信息。
         * 本地节点无法直接测量“邻居到二跳节点”的运动预测指标，
         * 因此这里保守使用 cost=1，保证二跳可达性仍被纳入图中。
         */
        if (twoHop.twoHopNeighborAddr == m_mainAddress)
        {
            continue;
        }
        if (m_state.FindSymNeighborTuple(twoHop.twoHopNeighborAddr))
        {
            continue;
        }

        bool usableNeighbor = false;
        for (const auto& neighbor : neighborSet)
        {
            if (neighbor.neighborMainAddr == twoHop.neighborMainAddr &&
                neighbor.status == NeighborTuple::STATUS_SYM &&
                neighbor.willingness != Willingness::NEVER)
            {
                usableNeighbor = true;
                break;
            }
        }
        if (usableNeighbor)
        {
            addEdge(twoHop.neighborMainAddr, twoHop.twoHopNeighborAddr, Ipv4Address(), Ipv4Address(), 1.0);
        }
    }

    const TopologySet& topology = m_state.GetTopologySet();
    for (const auto& topologyTuple : topology)
    {
        /*
         * 全网拓扑边来自 TC。
         * 如果远端 P-OLSR 节点在 TC 中携带了预测代价，就使用该代价；
         * 如果是普通 OLSR 格式 TC，则 predictionValid=false，退化为 cost=1。
         */
        if (topologyTuple.expirationTime < now)
        {
            continue;
        }
        double cost = topologyTuple.predictionValid ? topologyTuple.predictedCost : 1.0;
        addEdge(topologyTuple.lastAddr, topologyTuple.destAddr, Ipv4Address(), Ipv4Address(), cost);
    }

    Clear();

    std::map<Ipv4Address, PredictiveNodeState> best;
    best[m_mainAddress].cost = 0.0;
    best[m_mainAddress].hops = 0;
    best[m_mainAddress].reached = true;

    while (true)
    {
        /*
         * 简化 Dijkstra：
         * best 保存从本节点到每个目的节点的累计预测代价、跳数、
         * 第一跳和本地出接口。代价更低优先；代价相同则跳数更少优先。
         */
        bool found = false;
        Ipv4Address current;
        double bestCost = std::numeric_limits<double>::infinity();
        for (const auto& item : best)
        {
            if (item.second.reached && !item.second.settled && item.second.cost < bestCost)
            {
                found = true;
                current = item.first;
                bestCost = item.second.cost;
            }
        }
        if (!found)
        {
            break;
        }

        best[current].settled = true;
        auto edges = graph.find(current);
        if (edges == graph.end())
        {
            continue;
        }

        for (const auto& edge : edges->second)
        {
            PredictiveNodeState& currentState = best[current];
            PredictiveNodeState& nextState = best[edge.to];
            double newCost = currentState.cost + edge.cost;
            uint32_t newHops = currentState.hops + 1;
            bool better = !nextState.reached || newCost < nextState.cost;
            if (!better && nextState.reached && std::abs(newCost - nextState.cost) <= 1e-9 &&
                newHops < nextState.hops)
            {
                better = true;
            }

            if (better)
            {
                nextState.cost = newCost;
                nextState.hops = newHops;
                nextState.reached = true;
                if (current == m_mainAddress)
                {
                    nextState.firstHop = edge.nextHop;
                    nextState.localIface = edge.localIface;
                }
                else
                {
                    nextState.firstHop = currentState.firstHop;
                    nextState.localIface = currentState.localIface;
                }
            }
        }
    }

    for (const auto& item : best)
    {
        const Ipv4Address& dest = item.first;
        const PredictiveNodeState& state = item.second;
        if (dest == m_mainAddress || !state.reached || state.firstHop == Ipv4Address())
        {
            continue;
        }
        AddEntry(dest, state.firstHop, state.localIface, state.hops == 0 ? 1 : state.hops);
    }

    const IfaceAssocSet& ifaceAssocSet = m_state.GetIfaceAssocSet();
    for (const auto& tuple : ifaceAssocSet)
    {
        RoutingTableEntry mainEntry;
        RoutingTableEntry ifaceEntry;
        bool haveMainEntry = Lookup(tuple.mainAddr, mainEntry);
        bool haveIfaceEntry = Lookup(tuple.ifaceAddr, ifaceEntry);
        if (haveMainEntry && !haveIfaceEntry)
        {
            AddEntry(tuple.ifaceAddr, mainEntry.nextAddr, mainEntry.interface, mainEntry.distance);
        }
    }

    const AssociationSet& associationSet = m_state.GetAssociationSet();
    for (uint32_t i = 0; i < m_hnaRoutingTable->GetNRoutes(); i++)
    {
        m_hnaRoutingTable->RemoveRoute(0);
    }

    for (const auto& tuple : associationSet)
    {
        bool goToNextAssociationTuple = false;
        const Associations& localHnaAssociations = m_state.GetAssociations();
        for (const auto& localHnaAssoc : localHnaAssociations)
        {
            if (localHnaAssoc.networkAddr == tuple.networkAddr &&
                localHnaAssoc.netmask == tuple.netmask)
            {
                goToNextAssociationTuple = true;
            }
        }
        if (goToNextAssociationTuple)
        {
            continue;
        }

        RoutingTableEntry gatewayEntry;
        bool gatewayEntryExists = Lookup(tuple.gatewayAddr, gatewayEntry);
        bool addRoute = false;
        uint32_t routeIndex = 0;

        for (routeIndex = 0; routeIndex < m_hnaRoutingTable->GetNRoutes(); routeIndex++)
        {
            Ipv4RoutingTableEntry route = m_hnaRoutingTable->GetRoute(routeIndex);
            if (route.GetDestNetwork() == tuple.networkAddr &&
                route.GetDestNetworkMask() == tuple.netmask)
            {
                break;
            }
        }

        if (routeIndex == m_hnaRoutingTable->GetNRoutes())
        {
            addRoute = true;
        }
        else if (gatewayEntryExists &&
                 m_hnaRoutingTable->GetMetric(routeIndex) > gatewayEntry.distance)
        {
            m_hnaRoutingTable->RemoveRoute(routeIndex);
            addRoute = true;
        }

        if (addRoute && gatewayEntryExists)
        {
            m_hnaRoutingTable->AddNetworkRouteTo(tuple.networkAddr,
                                                 tuple.netmask,
                                                 gatewayEntry.nextAddr,
                                                 gatewayEntry.interface,
                                                 gatewayEntry.distance);
        }
    }

    NS_LOG_DEBUG("Node " << m_mainAddress << ": PredictiveRoutingTableComputation end.");
    m_routingTableChanged(GetSize());
}

void
RoutingProtocol::RoutingTableComputation()
{
    if (m_enablePrediction && m_usePredictionForRouting)
    {
        PredictiveRoutingTableComputation();
        return;
    }

    NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                 << " : Node " << m_mainAddress << ": RoutingTableComputation begin...");

    // 1. 首先清空原有路由表中的所有条目。
    Clear();

    // 2. 新路由项从对称一跳邻居开始加入。
    // 此时这些目的节点的跳数 h=1。
    // 路由是接口级转发，必须落到具体链路上。
    const NeighborSet& neighborSet = m_state.GetNeighbors();
    for (auto it = neighborSet.begin(); it != neighborSet.end(); it++)
    {
        const NeighborTuple& nb_tuple = *it;
        NS_LOG_DEBUG("Looking at neighbor tuple: " << nb_tuple);
        if (nb_tuple.status == NeighborTuple::STATUS_SYM)
        {
            bool nb_main_addr = false;
            const LinkTuple* lt = nullptr;
            const LinkSet& linkSet = m_state.GetLinks();
            for (auto it2 = linkSet.begin(); it2 != linkSet.end(); it2++)
            {
                const LinkTuple& link_tuple = *it2;
                NS_LOG_DEBUG("Looking at link tuple: "
                             << link_tuple
                             << (link_tuple.time >= Simulator::Now() ? "" : " (expired)"));
                if ((GetMainAddress(link_tuple.neighborIfaceAddr) == nb_tuple.neighborMainAddr) &&
                    link_tuple.time >= Simulator::Now())
                {
                    NS_LOG_LOGIC("Link tuple matches neighbor "
                                 << nb_tuple.neighborMainAddr
                                 << " => adding routing table entry to neighbor");
                    lt = &link_tuple;
                    AddEntry(link_tuple.neighborIfaceAddr,
                             link_tuple.neighborIfaceAddr,
                             link_tuple.localIfaceAddr,
                             1);
                    if (link_tuple.neighborIfaceAddr == nb_tuple.neighborMainAddr)
                    {
                        nb_main_addr = true;
                    }
                }
                else
                {
                    NS_LOG_LOGIC("Link tuple: linkMainAddress= "
                                 << GetMainAddress(link_tuple.neighborIfaceAddr)
                                 << "; neighborMainAddr =  " << nb_tuple.neighborMainAddr
                                 << "; expired=" << int(link_tuple.time < Simulator::Now())
                                 << " => IGNORE");
                }
            }

            // 如果上面的处理没有产生 R_dest_addr 等于该邻居主地址的路由项，
            // 则还必须额外添加一条新的路由项，
            // 字段如下：
            //      R_dest_addr  = 该邻居的主地址；
            //      R_next_addr  = 某个关联 link tuple 的 L_neighbor_iface_addr；
            //                     该 link tuple 的 L_time 必须不早于当前时间；
            //      R_dist       = 1；
            //      R_iface_addr = 该关联 link tuple 的 L_local_iface_addr。
            //                     也就是本节点用于到达该邻居的本地接口地址。
            if (!nb_main_addr && lt != nullptr)
            {
                NS_LOG_LOGIC("no R_dest_addr is equal to the main address of the neighbor "
                             "=> adding additional routing entry");
                AddEntry(nb_tuple.neighborMainAddr, lt->neighborIfaceAddr, lt->localIfaceAddr, 1);
            }
        }
    }

    //  3. 对 N2 中的每个节点，也就是既不是一跳邻居、
    //  也不是本节点自己的二跳邻居，且至少存在
    //  一个二跳邻居表项满足：
    //  N_neighbor_main_addr 对应的一跳邻居节点
    //  willingness 不等于 Willingness::NEVER。
    const TwoHopNeighborSet& twoHopNeighbors = m_state.GetTwoHopNeighbors();
    for (auto it = twoHopNeighbors.begin(); it != twoHopNeighbors.end(); it++)
    {
        const TwoHopNeighborTuple& nb2hop_tuple = *it;

        NS_LOG_LOGIC("Looking at two-hop neighbor tuple: " << nb2hop_tuple);

        // 该二跳邻居不能是一跳邻居，也不能是本节点自己。
        if (m_state.FindSymNeighborTuple(nb2hop_tuple.twoHopNeighborAddr))
        {
            NS_LOG_LOGIC("Two-hop neighbor tuple is also neighbor; skipped.");
            continue;
        }

        if (nb2hop_tuple.twoHopNeighborAddr == m_mainAddress)
        {
            NS_LOG_LOGIC("Two-hop neighbor is self; skipped.");
            continue;
        }

        // 并且二跳邻居集合中至少存在一个条目，
        // 其中 N_neighbor_main_addr 对应一个
        // willingness 不等于 Willingness::NEVER 的一跳邻居。
        bool nb2hopOk = false;
        for (auto neighbor = neighborSet.begin(); neighbor != neighborSet.end(); neighbor++)
        {
            if (neighbor->neighborMainAddr == nb2hop_tuple.neighborMainAddr &&
                neighbor->willingness != Willingness::NEVER)
            {
                nb2hopOk = true;
                break;
            }
        }
        if (!nb2hopOk)
        {
            NS_LOG_LOGIC("Two-hop neighbor tuple skipped: 2-hop neighbor "
                         << nb2hop_tuple.twoHopNeighborAddr << " is attached to neighbor "
                         << nb2hop_tuple.neighborMainAddr
                         << ", which was not found in the Neighbor Set.");
            continue;
        }

        // 选择一个二跳 tuple，并据此在路由表中创建一条路由项，字段如下：
        //                R_dest_addr  = 二跳邻居的主地址；
        //                R_next_addr  = 路由表中对应条目的 R_next_addr；
        //                               该对应条目满足：
        //                                   R_dest_addr == N_neighbor_main_addr，
        //                                                  即二跳 tuple 中的一跳邻居主地址；
        //                R_dist       = 2；
        //                R_iface_addr = 路由表中对应条目的 R_iface_addr；
        //                               该对应条目满足：
        //                                   R_dest_addr == N_neighbor_main_addr，
        //                                                  即二跳 tuple 中的一跳邻居主地址；
        RoutingTableEntry entry;
        bool foundEntry = Lookup(nb2hop_tuple.neighborMainAddr, entry);
        if (foundEntry)
        {
            NS_LOG_LOGIC("Adding routing entry for two-hop neighbor.");
            AddEntry(nb2hop_tuple.twoHopNeighborAddr, entry.nextAddr, entry.interface, 2);
        }
        else
        {
            NS_LOG_LOGIC("NOT adding routing entry for two-hop neighbor ("
                         << nb2hop_tuple.twoHopNeighborAddr << " not found in the routing table)");
        }
    }

    for (uint32_t h = 2;; h++)
    {
        bool added = false;

        // 3.1. 对拓扑表中的每个拓扑条目，如果它的
        // T_dest_addr 不对应路由表中任何路由项的 R_dest_addr，
        // 并且它的 T_last_addr
        // 对应某个 R_dist 为当前 h 的路由项的 R_dest_addr，
        // 等于当前跳数 h，则必须记录一条新的路由项。
        // 如果该路由项尚不存在，就加入路由表。
        const TopologySet& topology = m_state.GetTopologySet();
        for (auto it = topology.begin(); it != topology.end(); it++)
        {
            const TopologyTuple& topology_tuple = *it;
            NS_LOG_LOGIC("Looking at topology tuple: " << topology_tuple);

            RoutingTableEntry destAddrEntry;
            RoutingTableEntry lastAddrEntry;
            bool have_destAddrEntry = Lookup(topology_tuple.destAddr, destAddrEntry);
            bool have_lastAddrEntry = Lookup(topology_tuple.lastAddr, lastAddrEntry);
            if (!have_destAddrEntry && have_lastAddrEntry && lastAddrEntry.distance == h)
            {
                NS_LOG_LOGIC("Adding routing table entry based on the topology tuple.");
                // 则必须记录一条新的路由项，
                //                如果路由表中尚不存在该项，其字段如下：
                //                     R_dest_addr  = T_dest_addr；
                //                     R_next_addr  = 已有路由项的 R_next_addr；
                //                                    该已有路由项满足：
                //                                    R_dest_addr == T_last_addr。
                //                     R_dist       = h + 1；
                //                     R_iface_addr = 已有路由项的 R_iface_addr；
                //                                    该已有路由项满足：
                //                                       R_dest_addr == T_last_addr。
                AddEntry(topology_tuple.destAddr,
                         lastAddrEntry.nextAddr,
                         lastAddrEntry.interface,
                         h + 1);
                added = true;
            }
            else
            {
                NS_LOG_LOGIC("NOT adding routing table entry based on the topology tuple: "
                             "have_destAddrEntry="
                             << have_destAddrEntry << " have_lastAddrEntry=" << have_lastAddrEntry
                             << " lastAddrEntry.distance=" << (int)lastAddrEntry.distance
                             << " (h=" << h << ")");
            }
        }

        if (!added)
        {
            break;
        }
    }

    // 4. 对多接口关联表中的每个条目，
    // 如果存在满足以下条件的路由项：
    // R_dest_addr 等于多接口关联条目的 I_main_addr，
    // 并且不存在满足以下条件的路由项：
    // R_dest_addr 等于 I_iface_addr，
    const IfaceAssocSet& ifaceAssocSet = m_state.GetIfaceAssocSet();
    for (auto it = ifaceAssocSet.begin(); it != ifaceAssocSet.end(); it++)
    {
        const IfaceAssocTuple& tuple = *it;
        RoutingTableEntry entry1;
        RoutingTableEntry entry2;
        bool have_entry1 = Lookup(tuple.mainAddr, entry1);
        bool have_entry2 = Lookup(tuple.ifaceAddr, entry2);
        if (have_entry1 && !have_entry2)
        {
            // 则为该接口地址创建一条新的路由项，字段如下：
            //       R_dest_addr  = 多接口关联条目的 I_iface_addr；
            //                                     也就是该节点的某个接口地址；
            //       R_next_addr  = 已记录路由项的 R_next_addr；
            //       R_dist       = 已记录路由项的 R_dist；
            //       R_iface_addr = 已记录路由项的 R_iface_addr。
            AddEntry(tuple.ifaceAddr, entry1.nextAddr, entry1.interface, entry1.distance);
        }
    }

    // 5. 对 association 集合中的每个 tuple，
    //    如果路由表中不存在满足以下条件的条目：
    //        R_dest_addr == A_network_addr/A_netmask，
    //   并且该被通告网络不是由本节点自己通告的，
    //   则创建一条新的路由项。
    const AssociationSet& associationSet = m_state.GetAssociationSet();

    // 清空 HNA 路由表。
    for (uint32_t i = 0; i < m_hnaRoutingTable->GetNRoutes(); i++)
    {
        m_hnaRoutingTable->RemoveRoute(0);
    }

    for (auto it = associationSet.begin(); it != associationSet.end(); it++)
    {
        const AssociationTuple& tuple = *it;

        // 检查是否从其他网关收到了 HNA 关联信息。
        // 是否也由本节点自己通告。如果是这种情况，就不为该 association tuple 创建路由，
        // 而是直接处理下一个 association tuple。
        bool goToNextAssociationTuple = false;
        const Associations& localHnaAssociations = m_state.GetAssociations();
        NS_LOG_DEBUG("Nb local associations: " << localHnaAssociations.size());
        for (auto assocIterator = localHnaAssociations.begin();
             assocIterator != localHnaAssociations.end();
             assocIterator++)
        {
            const Association& localHnaAssoc = *assocIterator;
            if (localHnaAssoc.networkAddr == tuple.networkAddr &&
                localHnaAssoc.netmask == tuple.netmask)
            {
                NS_LOG_DEBUG("HNA association received from another GW is part of local HNA "
                             "associations: no route added for network "
                             << tuple.networkAddr << "/" << tuple.netmask);
                goToNextAssociationTuple = true;
            }
        }
        if (goToNextAssociationTuple)
        {
            continue;
        }

        RoutingTableEntry gatewayEntry;

        bool gatewayEntryExists = Lookup(tuple.gatewayAddr, gatewayEntry);
        bool addRoute = false;

        uint32_t routeIndex = 0;

        for (routeIndex = 0; routeIndex < m_hnaRoutingTable->GetNRoutes(); routeIndex++)
        {
            Ipv4RoutingTableEntry route = m_hnaRoutingTable->GetRoute(routeIndex);
            if (route.GetDestNetwork() == tuple.networkAddr &&
                route.GetDestNetworkMask() == tuple.netmask)
            {
                break;
            }
        }

        if (routeIndex == m_hnaRoutingTable->GetNRoutes())
        {
            addRoute = true;
        }
        else if (gatewayEntryExists &&
                 m_hnaRoutingTable->GetMetric(routeIndex) > gatewayEntry.distance)
        {
            m_hnaRoutingTable->RemoveRoute(routeIndex);
            addRoute = true;
        }

        if (addRoute && gatewayEntryExists)
        {
            m_hnaRoutingTable->AddNetworkRouteTo(tuple.networkAddr,
                                                 tuple.netmask,
                                                 gatewayEntry.nextAddr,
                                                 gatewayEntry.interface,
                                                 gatewayEntry.distance);
        }
    }

    NS_LOG_DEBUG("Node " << m_mainAddress << ": RoutingTableComputation end.");
    m_routingTableChanged(GetSize());
}

void
RoutingProtocol::ProcessHello(const polsr::MessageHeader& msg,
                              const Ipv4Address& receiverIface,
                              const Ipv4Address& senderIface)
{
    NS_LOG_FUNCTION(msg << receiverIface << senderIface);

    const polsr::MessageHeader::Hello& hello = msg.GetHello();

    //LinkSensing() 负责把“我收到谁的 HELLO、对方 HELLO 里有没有提到我、链路是否对称”这些信息，更新到 Link Set 和 Neighbor Set 中。
    LinkSensing(msg, hello, receiverIface, senderIface);
    /*
     * P-OLSR 扩展点：
     * LinkSensing() 已经完成标准 OLSR 链路状态更新后，
     * 再根据 HELLO 中的运动信息更新预测代价。
     */
    UpdatePredictionFromHello(msg, hello, receiverIface, senderIface);

//这段只是 ProcessHello() 里的调试输出，用来打印链路表和邻居表；
//不开日志时不会编译，开日志时也只影响输出，不改变协议运行结果。
#ifdef NS3_LOG_ENABLE
    {
        const LinkSet& links = m_state.GetLinks();
        NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                     << " ** BEGIN dump Link Set for POLSR Node " << m_mainAddress);
        for (auto link = links.begin(); link != links.end(); link++)
        {
            NS_LOG_DEBUG(*link);
        }
        NS_LOG_DEBUG("** END dump Link Set for POLSR Node " << m_mainAddress);

        const NeighborSet& neighbors = m_state.GetNeighbors();
        NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                     << " ** BEGIN dump Neighbor Set for POLSR Node " << m_mainAddress);
        for (auto neighbor = neighbors.begin(); neighbor != neighbors.end(); neighbor++)
        {
            NS_LOG_DEBUG(*neighbor);
        }
        NS_LOG_DEBUG("** END dump Neighbor Set for POLSR Node " << m_mainAddress);
    }
#endif // NS3_LOG_ENABLE

    //PopulateNeighborSet() 就是把 HELLO 包里发送方声明的“转发意愿 willingness”更新到本节点的一跳邻居表里。
    PopulateNeighborSet(msg, hello);
    //PopulateTwoHopNeighborSet() 把“邻居的邻居”记录下来，用来让本节点知道二跳范围，并为后面的 MPR 选择提供依据。
    PopulateTwoHopNeighborSet(msg, hello);

#ifdef NS3_LOG_ENABLE
    {
        const TwoHopNeighborSet& twoHopNeighbors = m_state.GetTwoHopNeighbors();
        NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                     << " ** BEGIN dump TwoHopNeighbor Set for POLSR Node " << m_mainAddress);
        for (auto tuple = twoHopNeighbors.begin(); tuple != twoHopNeighbors.end(); tuple++)
        {
            NS_LOG_DEBUG(*tuple);
        }
        NS_LOG_DEBUG("** END dump TwoHopNeighbor Set for POLSR Node " << m_mainAddress);
    }
#endif // NS3_LOG_ENABLE

    //MprComputation(); 的作用是：根据当前的一跳邻居表和二跳邻居表，重新计算“我应该选择哪些邻居作为 MPR”。
    MprComputation();
    //PopulateMprSelectorSet()的作用是：根据收到的 HELLO 消息，判断“发送方有没有选择我作为 MPR”，然后更新 MPR Selector Set。
    PopulateMprSelectorSet(msg, hello);
}

void
RoutingProtocol::ProcessTc(const polsr::MessageHeader& msg, const Ipv4Address& senderIface)
{
    const polsr::MessageHeader::Tc& tc = msg.GetTc();
    Time now = Simulator::Now();

    // 1. 如果该 message 的发送接口不属于本节点的对称
    // 一跳邻居中，则必须丢弃该 message。
    // 检查发送 TC 的接口是不是对称一跳邻居,如果不是就丢弃 TC 消息。
    // 上一跳接口地址 senderIface 是 TC 消息的发送方接口地址,通常来自外层 IP 包的源地址.
    const LinkTuple* link_tuple = m_state.FindSymLinkTuple(senderIface, now);
    if (link_tuple == nullptr)
    {
        return;
    }

    // 2. 如果拓扑集合中存在某个 tuple 满足：
    //    T_last_addr 等于 originator address，并且
    //    T_seq       >  ANSN,
    // 则不得继续处理该 TC message，
    // 也就是直接停止处理。
    //  检查 TC 是否过旧，如果 TC 消息里携带的 ANSN 比我记录的同一目的地、同一发送方的 ANSN 还旧，就丢弃 TC 消息。
    const TopologyTuple* topologyTuple =
        m_state.FindNewerTopologyTuple(msg.GetOriginatorAddress(), tc.ansn);
    if (topologyTuple != nullptr)
    {
        return;
    }

    // 3. 拓扑集合中所有满足以下条件的 tuple：
    //    T_last_addr 等于 originator address，并且
    //    T_seq       <  ANSN
    // 这些旧拓扑信息必须从拓扑表中移除。
    // 删除旧的拓扑记录
    m_state.EraseOlderTopologyTuples(msg.GetOriginatorAddress(), tc.ansn);

    // 4. 对 TC message 中收到的每一个被通告邻居主地址，
    // 执行以下处理：
    // 遍历 TC 中通告的邻居地址列表，更新拓扑表TopologyTuple
    for (auto i = tc.neighborAddresses.begin(); i != tc.neighborAddresses.end(); i++)
    {
        const Ipv4Address& addr = *i;
        /*
         * P-OLSR TC 扩展处理：
         * 对每个 advertised neighbor，先查 TC 是否带了对应的预测指标。
         * 后续仍然先按原始 OLSR 规则创建/刷新 TopologyTuple，
         * 只有拓扑关系被接受后，才把预测代价写入该 TopologyTuple。
         */
        bool predictionValid = false;
        double predictedCost = 1.0;
        double forwardReceivingRatio = 1.0;
        double reverseReceivingRatio = 1.0;
        double relativeSpeed = 0.0;
        for (const auto& prediction : tc.neighborPredictions)
        {
            if (prediction.address == addr && prediction.predictionValid)
            {
                predictionValid = true;
                predictedCost = prediction.predictedCost;
                forwardReceivingRatio = prediction.forwardReceivingRatio;
                reverseReceivingRatio = prediction.reverseReceivingRatio;
                relativeSpeed = prediction.relativeSpeed;
                break;
            }
        }

        // 4.1. 如果拓扑集合中存在某个 tuple 满足：
        //      T_dest_addr 等于被通告的邻居主地址，并且
        //      T_last_addr 等于 originator address，
        // 则必须把该 tuple 的保持时间设置为：
        //      T_time = 当前时间 + 有效时间。
        TopologyTuple* topologyTuple = m_state.FindTopologyTuple(addr, msg.GetOriginatorAddress());

        if (topologyTuple != nullptr)
        {
            topologyTuple->expirationTime = now + msg.GetVTime();
            if (predictionValid)
            {
                m_state.UpdateTopologyPrediction(addr,
                                                 msg.GetOriginatorAddress(),
                                                 predictedCost,
                                                 forwardReceivingRatio,
                                                 reverseReceivingRatio,
                                                 relativeSpeed,
                                                 now);
            }
        }
        else
        {   //如果不存在，就新增,并安排过期删除定时器
            // 4.2. 否则，必须在拓扑集合中记录一个新的 tuple，
            // 其字段如下：
            //      T_dest_addr = 被通告的邻居主地址，
            //      T_last_addr = originator address，
            //      T_seq = ANSN，
            //      T_time = 当前时间 + 有效时间。
            TopologyTuple topologyTuple;
            topologyTuple.destAddr = addr;
            topologyTuple.lastAddr = msg.GetOriginatorAddress();
            topologyTuple.sequenceNumber = tc.ansn;
            topologyTuple.expirationTime = now + msg.GetVTime();
            AddTopologyTuple(topologyTuple);
            if (predictionValid)
            {
                m_state.UpdateTopologyPrediction(addr,
                                                 msg.GetOriginatorAddress(),
                                                 predictedCost,
                                                 forwardReceivingRatio,
                                                 reverseReceivingRatio,
                                                 relativeSpeed,
                                                 now);
            }

            // 安排拓扑 tuple 的过期删除定时器。
            // 根据 RFC 3626 的规定，拓扑表中的记录是有过期时间的，过期时间到了就要删除。这里就是为新添加的拓扑记录安排一个定时器，到期后调用 TopologyTupleTimerExpire() 来删除这个记录。
            m_events.Track(Simulator::Schedule(DELAY(topologyTuple.expirationTime),
                                               &RoutingProtocol::TopologyTupleTimerExpire,
                                               this,
                                               topologyTuple.destAddr,
                                               topologyTuple.lastAddr));
        }
    }

#ifdef NS3_LOG_ENABLE
    {
        const TopologySet& topology = m_state.GetTopologySet();
        NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                     << " ** BEGIN dump TopologySet for POLSR Node " << m_mainAddress);
        for (auto tuple = topology.begin(); tuple != topology.end(); tuple++)
        {
            NS_LOG_DEBUG(*tuple);
        }
        NS_LOG_DEBUG("** END dump TopologySet Set for POLSR Node " << m_mainAddress);
    }
#endif // NS3_LOG_ENABLE
}

void
RoutingProtocol::ProcessMid(const polsr::MessageHeader& msg, const Ipv4Address& senderIface)
{
    const polsr::MessageHeader::Mid& mid = msg.GetMid();
    Time now = Simulator::Now();

    NS_LOG_DEBUG("Node " << m_mainAddress << " ProcessMid from " << senderIface);
    // 1. 如果该 message 的发送接口不属于本节点的对称
    // 一跳邻居中，则必须丢弃该 message。
    const LinkTuple* linkTuple = m_state.FindSymLinkTuple(senderIface, now);
    if (linkTuple == nullptr)
    {
        NS_LOG_LOGIC("Node " << m_mainAddress
                             << ": the sender interface of this message is not in the "
                                "symmetric 1-hop neighborhood of this node,"
                                " the message MUST be discarded.");
        return;
    }

    // 2. 对 MID message 中列出的每个接口地址，
    for (auto i = mid.interfaceAddresses.begin(); i != mid.interfaceAddresses.end(); i++)
    {
        bool updated = false;
        IfaceAssocSet& ifaceAssoc = m_state.GetIfaceAssocSetMutable();
        for (auto tuple = ifaceAssoc.begin(); tuple != ifaceAssoc.end(); tuple++)
        {
            if (tuple->ifaceAddr == *i && tuple->mainAddr == msg.GetOriginatorAddress())
            {
                NS_LOG_LOGIC("IfaceAssoc updated: " << *tuple);
                tuple->time = now + msg.GetVTime();
                updated = true;
            }
        }
        if (!updated)
        {
            IfaceAssocTuple tuple;
            tuple.ifaceAddr = *i;
            tuple.mainAddr = msg.GetOriginatorAddress();
            tuple.time = now + msg.GetVTime();
            AddIfaceAssocTuple(tuple);
            NS_LOG_LOGIC("New IfaceAssoc added: " << tuple);
            // Schedules iface association tuple deletion
            Simulator::Schedule(DELAY(tuple.time),
                                &RoutingProtocol::IfaceAssocTupleTimerExpire,
                                this,
                                tuple.ifaceAddr);
        }
    }

    // 3. (not part of the RFC) iterate over all NeighborTuple's and
    // TwoHopNeighborTuples, update the neighbor addresses taking into account
    // the new MID information.
    NeighborSet& neighbors = m_state.GetNeighbors();
    for (auto neighbor = neighbors.begin(); neighbor != neighbors.end(); neighbor++)
    {
        neighbor->neighborMainAddr = GetMainAddress(neighbor->neighborMainAddr);
    }

    TwoHopNeighborSet& twoHopNeighbors = m_state.GetTwoHopNeighbors();
    for (auto twoHopNeighbor = twoHopNeighbors.begin(); twoHopNeighbor != twoHopNeighbors.end();
         twoHopNeighbor++)
    {
        twoHopNeighbor->neighborMainAddr = GetMainAddress(twoHopNeighbor->neighborMainAddr);
        twoHopNeighbor->twoHopNeighborAddr = GetMainAddress(twoHopNeighbor->twoHopNeighborAddr);
    }
    NS_LOG_DEBUG("Node " << m_mainAddress << " ProcessMid from " << senderIface << " -> END.");
}

void
RoutingProtocol::ProcessHna(const polsr::MessageHeader& msg, const Ipv4Address& senderIface)
{
    const polsr::MessageHeader::Hna& hna = msg.GetHna();
    Time now = Simulator::Now();

    // 1. If the sender interface of this message is not in the symmetric
    // 1-hop neighborhood of this node, the message MUST be discarded.
    const LinkTuple* link_tuple = m_state.FindSymLinkTuple(senderIface, now);
    if (link_tuple == nullptr)
    {
        return;
    }

    // 2. Otherwise, for each (network address, netmask) pair in the
    // message:

    for (auto it = hna.associations.begin(); it != hna.associations.end(); it++)
    {
        AssociationTuple* tuple =
            m_state.FindAssociationTuple(msg.GetOriginatorAddress(), it->address, it->mask);

        // 2.1 如果 association 集合中已经存在满足以下条件的条目：
        //          A_gateway_addr == originator address，
        //          A_network_addr == network address，
        //          A_netmask == netmask，
        //      则必须把该 tuple 的保持时间设置为：
        //          A_time = 当前时间 + 有效时间。
        if (tuple != nullptr)
        {
            tuple->expirationTime = now + msg.GetVTime();
        }

        // 2.2 否则，必须记录一个新的 tuple，字段如下：
        //          A_gateway_addr = originator address，
        //          A_network_addr = network address，
        //          A_netmask = netmask，
        //          A_time = 当前时间 + 有效时间。
        else
        {
            AssociationTuple assocTuple = {msg.GetOriginatorAddress(),
                                           it->address,
                                           it->mask,
                                           now + msg.GetVTime()};
            AddAssociationTuple(assocTuple);

            // 安排 association tuple 的过期删除定时器。
            Simulator::Schedule(DELAY(assocTuple.expirationTime),
                                &RoutingProtocol::AssociationTupleTimerExpire,
                                this,
                                assocTuple.gatewayAddr,
                                assocTuple.networkAddr,
                                assocTuple.netmask);
        }
    }
}

void
RoutingProtocol::ForwardDefault(polsr::MessageHeader polsrMessage,
                                DuplicateTuple* duplicated,
                                const Ipv4Address& localIface,
                                const Ipv4Address& senderAddress)
{
    Time now = Simulator::Now();

    // If the sender interface address is not in the symmetric
    // 1-hop neighborhood the message must not be forwarded
    const LinkTuple* linkTuple = m_state.FindSymLinkTuple(senderAddress, now);
    if (linkTuple == nullptr)
    {
        return;
    }

    // 如果该 message 已经被考虑过转发，
    // 就不能再次重传。
    // 如果这个消息之前已经被转发过了，就不要再转发了
    if (duplicated != nullptr && duplicated->retransmitted)
    {
        NS_LOG_LOGIC(Simulator::Now()
                     << "Node " << m_mainAddress
                     << " does not forward a message received"
                        " from "
                     << polsrMessage.GetOriginatorAddress() << " because it is duplicated");
        return;
    }

    // If the sender interface address is an interface address
    // of a MPR selector of this node and ttl is greater than 1,
    // the message must be retransmitted
    bool retransmitted = false;
    if (polsrMessage.GetTimeToLive() > 1)
    {
        const MprSelectorTuple* mprselTuple =
            m_state.FindMprSelectorTuple(GetMainAddress(senderAddress));
        if (mprselTuple != nullptr)
        {
            polsrMessage.SetTimeToLive(polsrMessage.GetTimeToLive() - 1);
            polsrMessage.SetHopCount(polsrMessage.GetHopCount() + 1);
            // We have to introduce a random delay to avoid
            // synchronization with neighbors.
            QueueMessage(polsrMessage, JITTER);
            retransmitted = true;
        }
    }

    // Update duplicate tuple...更新 Duplicate Set，避免重复转发
    // 如果之前已经有这个消息的记录了，就更新过期时间和转发状态，并把转发接口加入到记录里；否则就新建一条记录。
    if (duplicated != nullptr)
    {
        duplicated->expirationTime = now + POLSR_DUP_HOLD_TIME;
        duplicated->retransmitted = retransmitted;
        duplicated->ifaceList.push_back(localIface);
    }
    // ...or create a new one
    else
    {
        DuplicateTuple newDup;
        newDup.address = polsrMessage.GetOriginatorAddress();
        newDup.sequenceNumber = polsrMessage.GetMessageSequenceNumber();
        newDup.expirationTime = now + POLSR_DUP_HOLD_TIME;
        newDup.retransmitted = retransmitted;
        newDup.ifaceList.push_back(localIface);
        AddDuplicateTuple(newDup);
        // Schedule dup tuple deletion
        Simulator::Schedule(POLSR_DUP_HOLD_TIME,
                            &RoutingProtocol::DupTupleTimerExpire,
                            this,
                            newDup.address,
                            newDup.sequenceNumber);
    }
}

void
RoutingProtocol::QueueMessage(const polsr::MessageHeader& message, Time delay)
{
    m_queuedMessages.push_back(message);
    if (not m_queuedMessagesTimer.IsRunning())
    {
        m_queuedMessagesTimer.SetDelay(delay);
        m_queuedMessagesTimer.Schedule();
    }
}

void
RoutingProtocol::SendPacket(Ptr<Packet> packet, const MessageList& containedMessages)
{
    NS_LOG_DEBUG("POLSR node " << m_mainAddress << " sending a POLSR packet");

    // Add a header
    polsr::PacketHeader header;
    header.SetPacketLength(header.GetSerializedSize() + packet->GetSize());
    header.SetPacketSequenceNumber(GetPacketSequenceNumber());
    packet->AddHeader(header);

    // Trace it
    m_txPacketTrace(header, containedMessages);

    // Send it
    for (auto i = m_sendSockets.begin(); i != m_sendSockets.end(); i++)
    {
        Ptr<Packet> pkt = packet->Copy();
        Ipv4Address bcast = i->second.GetLocal().GetSubnetDirectedBroadcast(i->second.GetMask());
        i->first->SendTo(pkt, 0, InetSocketAddress(bcast, POLSR_PORT_NUMBER));
    }
}

void
RoutingProtocol::SendQueuedMessages()
{
    Ptr<Packet> packet = Create<Packet>();
    int numMessages = 0;

    NS_LOG_DEBUG("Polsr node " << m_mainAddress << ": SendQueuedMessages");

    MessageList msglist;

    for (auto message = m_queuedMessages.begin(); message != m_queuedMessages.end(); message++)
    {
        Ptr<Packet> p = Create<Packet>();
        p->AddHeader(*message);
        packet->AddAtEnd(p);
        msglist.push_back(*message);
        if (++numMessages == POLSR_MAX_MSGS)
        {
            SendPacket(packet, msglist);
            msglist.clear();
            // Reset variables for next packet
            numMessages = 0;
            packet = Create<Packet>();
        }
    }

    if (packet->GetSize())
    {
        SendPacket(packet, msglist);
    }

    m_queuedMessages.clear();
}

void
RoutingProtocol::SendHello()
{
    NS_LOG_FUNCTION(this);

    polsr::MessageHeader msg;
    Time now = Simulator::Now();

    msg.SetVTime(POLSR_NEIGHB_HOLD_TIME);
    msg.SetOriginatorAddress(m_mainAddress);
    msg.SetTimeToLive(1);
    msg.SetHopCount(0);
    msg.SetMessageSequenceNumber(GetMessageSequenceNumber());
    polsr::MessageHeader::Hello& hello = msg.GetHello();//hello就是m_message.hello的别名

    hello.SetHTime(m_helloInterval);
    hello.willingness = m_willingness;
    if (m_enablePrediction)
    {
        /*
         * P-OLSR HELLO 发送扩展：
         * 只有启用预测且节点安装了 MobilityModel 时才写入 GPS 坐标字段。
         * 如果没有 MobilityModel，则继续发送普通 HELLO，避免因为缺少移动模型
         * 影响原有 OLSR 仿真。
         *
         * 注意：ns-3 MobilityModel 默认提供的是仿真局部坐标，不是真实 WGS84。
         * 这里把 x/y/z 映射到论文报文字段 longitude/latitude/altitude，
         * 使协议字段和公式可复现；若场景提供真实 GPS 转换，可在这里替换映射。
         */
        Ptr<Node> node = GetObject<Node>();
        Ptr<MobilityModel> mobility;
        if (node)
        {
            mobility = node->GetObject<MobilityModel>();
        }
        if (mobility)
        {
            Vector position = mobility->GetPosition();
            hello.hasGps = true;
            hello.longitude = position.x;
            hello.latitude = position.y;
            hello.altitude = position.z;
        }
    }

    std::vector<polsr::MessageHeader::Hello::LinkMessage>& linkMessages = hello.linkMessages;

    const LinkSet& links = m_state.GetLinks();
    for (auto link_tuple = links.begin(); link_tuple != links.end(); link_tuple++)
    {
        if (!(GetMainAddress(link_tuple->localIfaceAddr) == m_mainAddress &&
              link_tuple->time >= now))
        {
            continue;
        }

        LinkType linkType;
        NeighborType neighborType;

        // 确定链路类型。
        if (link_tuple->symTime >= now)
        {
            linkType = LinkType::SYM_LINK;
        }
        else if (link_tuple->asymTime >= now)
        {
            linkType = LinkType::ASYM_LINK;
        }
        else
        {
            linkType = LinkType::LOST_LINK;
        }
        // 确定邻居类型。
        if (m_state.FindMprAddress(GetMainAddress(link_tuple->neighborIfaceAddr)))
        {
            neighborType = NeighborType::MPR_NEIGH;
            NS_LOG_DEBUG("I consider neighbor " << GetMainAddress(link_tuple->neighborIfaceAddr)
                                                << " to be MPR_NEIGH.");
        }
        else
        {
            bool ok = false;
            for (auto nb_tuple = m_state.GetNeighbors().begin();
                 nb_tuple != m_state.GetNeighbors().end();
                 nb_tuple++)
            {
                if (nb_tuple->neighborMainAddr == GetMainAddress(link_tuple->neighborIfaceAddr))
                {
                    if (nb_tuple->status == NeighborTuple::STATUS_SYM)
                    {
                        NS_LOG_DEBUG("I consider neighbor "
                                     << GetMainAddress(link_tuple->neighborIfaceAddr)
                                     << " to be SYM_NEIGH.");
                        neighborType = NeighborType::SYM_NEIGH;
                    }
                    else if (nb_tuple->status == NeighborTuple::STATUS_NOT_SYM)
                    {
                        neighborType = NeighborType::NOT_NEIGH;
                        NS_LOG_DEBUG("I consider neighbor "
                                     << GetMainAddress(link_tuple->neighborIfaceAddr)
                                     << " to be NOT_NEIGH.");
                    }
                    else
                    {
                        NS_FATAL_ERROR("There is a neighbor tuple with an unknown status!\n");
                    }
                    ok = true;
                    break;
                }
            }
            if (!ok)
            {
                NS_LOG_WARN("I don't know the neighbor "
                            << GetMainAddress(link_tuple->neighborIfaceAddr) << "!!!");
                continue;
            }
        }

        polsr::MessageHeader::Hello::LinkMessage linkMessage;
        linkMessage.linkCode = (static_cast<uint8_t>(linkType) & 0x03) |
                               ((static_cast<uint8_t>(neighborType) << 2) & 0x0f);
        linkMessage.neighborInterfaceAddresses.push_back(link_tuple->neighborIfaceAddr);

        std::vector<Ipv4Address> interfaces =
            m_state.FindNeighborInterfaces(link_tuple->neighborIfaceAddr);

        linkMessage.neighborInterfaceAddresses.insert(linkMessage.neighborInterfaceAddresses.end(),
                                                      interfaces.begin(),
                                                      interfaces.end());

        if (m_enablePrediction && hello.hasGps)
        {
            polsr::MessageHeader::Hello::LinkMessage::NeighborLinkQuality quality;
            quality.address = link_tuple->neighborIfaceAddr;
            quality.forwardReceivingRatio = link_tuple->phi;
            quality.reverseReceivingRatio = link_tuple->rho;
            quality.relativeSpeed = link_tuple->relativeSpeed;
            quality.qualityValid = link_tuple->predictionValid;
            linkMessage.neighborLinkQualities.push_back(quality);
        }

        linkMessages.push_back(linkMessage);
    }
    NS_LOG_DEBUG("POLSR HELLO message size: " << int(msg.GetSerializedSize()) << " (with "
                                             << int(linkMessages.size()) << " link messages)");
    QueueMessage(msg, JITTER);
}

void
RoutingProtocol::SendTc()
{
    NS_LOG_FUNCTION(this);

    polsr::MessageHeader msg;

    msg.SetVTime(POLSR_TOP_HOLD_TIME);
    msg.SetOriginatorAddress(m_mainAddress);
    msg.SetTimeToLive(255);
    msg.SetHopCount(0);
    msg.SetMessageSequenceNumber(GetMessageSequenceNumber());

    polsr::MessageHeader::Tc& tc = msg.GetTc();
    tc.ansn = m_ansn;

    for (auto mprsel_tuple = m_state.GetMprSelectors().begin();
         mprsel_tuple != m_state.GetMprSelectors().end();
         mprsel_tuple++)
    {
        tc.neighborAddresses.push_back(mprsel_tuple->mainAddr);
        if (m_enablePrediction)
        {
            /*
             * P-OLSR TC 发送扩展：
             * 原 TC 只通告 MPR selector 地址。这里额外查找本节点到该
             * selector 的 ETX 代价、正反向接收率和相对速度，并放入 TC 的 neighborPredictions。
             * 接收方会在 ProcessTc() 中把这些值写入 TopologyTuple。
             */
            polsr::MessageHeader::Tc::NeighborPrediction prediction;
            prediction.address = mprsel_tuple->mainAddr;
            prediction.predictedCost = GetNeighborPredictionCost(mprsel_tuple->mainAddr);
            prediction.forwardReceivingRatio =
                GetNeighborForwardReceivingRatio(mprsel_tuple->mainAddr);
            prediction.reverseReceivingRatio =
                GetNeighborReverseReceivingRatio(mprsel_tuple->mainAddr);
            prediction.relativeSpeed = GetNeighborRelativeSpeed(mprsel_tuple->mainAddr);
            prediction.predictionValid = true;
            tc.neighborPredictions.push_back(prediction);
        }
    }
    QueueMessage(msg, JITTER);
}

void
RoutingProtocol::SendMid()
{
    polsr::MessageHeader msg;
    polsr::MessageHeader::Mid& mid = msg.GetMid();

    // 如果某个节点只有一个接口地址参与
    // MANET（也就是运行 OLSR），则不得生成任何 MID
    // message。

    // 如果某个节点有多个接口，但只有一个接口参与
    // MANET 并运行 OLSR（例如节点同时连接到
    // 有线网络和 MANET），则不得生成任何 MID
    // message。

    // 如果某个节点有多个接口，并且不止一个接口
    // 参与 MANET 并运行 OLSR，则必须按规范生成 MID
    // message。

    // [注意：这里假设所有接口都参与
    // MANET；后续可以考虑把它做成可配置项。]

    Ipv4Address loopback("127.0.0.1");
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
    {
        Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
        if (addr != m_mainAddress && addr != loopback &&
            m_interfaceExclusions.find(i) == m_interfaceExclusions.end())
        {
            mid.interfaceAddresses.push_back(addr);
        }
    }
    if (mid.interfaceAddresses.empty())
    {
        return;
    }

    msg.SetVTime(POLSR_MID_HOLD_TIME);
    msg.SetOriginatorAddress(m_mainAddress);
    msg.SetTimeToLive(255);
    msg.SetHopCount(0);
    msg.SetMessageSequenceNumber(GetMessageSequenceNumber());

    QueueMessage(msg, JITTER);
}

void
RoutingProtocol::SendHna()
{
    polsr::MessageHeader msg;

    msg.SetVTime(POLSR_HNA_HOLD_TIME);
    msg.SetOriginatorAddress(m_mainAddress);
    msg.SetTimeToLive(255);
    msg.SetHopCount(0);
    msg.SetMessageSequenceNumber(GetMessageSequenceNumber());
    polsr::MessageHeader::Hna& hna = msg.GetHna();

    std::vector<polsr::MessageHeader::Hna::Association>& associations = hna.associations;

    // 将所有本地 HNA association 加入 HNA message。
    const Associations& localHnaAssociations = m_state.GetAssociations();
    for (auto it = localHnaAssociations.begin(); it != localHnaAssociations.end(); it++)
    {
        polsr::MessageHeader::Hna::Association assoc = {it->networkAddr, it->netmask};
        associations.push_back(assoc);
    }
    // 如果没有需要发送的 HNA association，则不入队该 message，直接返回。
    if (associations.empty())
    {
        return;
    }

    // 否则将该 message 入队，稍后发送。
    QueueMessage(msg, JITTER);
}

void
RoutingProtocol::AddHostNetworkAssociation(Ipv4Address networkAddr, Ipv4Mask netmask)
{
    // 检查（networkAddr，netmask）tuple 是否已经存在于
    // 本地 HNA association 列表中。
    const Associations& localHnaAssociations = m_state.GetAssociations();
    for (auto assocIterator = localHnaAssociations.begin();
         assocIterator != localHnaAssociations.end();
         assocIterator++)
    {
        const Association& localHnaAssoc = *assocIterator;
        if (localHnaAssoc.networkAddr == networkAddr && localHnaAssoc.netmask == netmask)
        {
            NS_LOG_INFO("HNA association for network " << networkAddr << "/" << netmask
                                                       << " already exists.");
            return;
        }
    }
    // 如果该 tuple 尚不存在，就把它加入本地 HNA association 列表。
    NS_LOG_INFO("Adding HNA association for network " << networkAddr << "/" << netmask << ".");
    m_state.InsertAssociation(Association{networkAddr, netmask});
}

void
RoutingProtocol::RemoveHostNetworkAssociation(Ipv4Address networkAddr, Ipv4Mask netmask)
{
    NS_LOG_INFO("Removing HNA association for network " << networkAddr << "/" << netmask << ".");
    m_state.EraseAssociation(Association{networkAddr, netmask});
}

void
RoutingProtocol::SetRoutingTableAssociation(Ptr<Ipv4StaticRouting> routingTable)
{
    // 如果已经关联过一个路由表，先移除
    // 本地 HNA association 列表中对应的条目。
    if (m_routingTableAssociation)
    {
        NS_LOG_INFO("Removing HNA entries coming from the old routing table association.");
        for (uint32_t i = 0; i < m_routingTableAssociation->GetNRoutes(); i++)
        {
            Ipv4RoutingTableEntry route = m_routingTableAssociation->GetRoute(i);
            // If the outgoing interface for this route is a non-polsr interface
            if (UsesNonPolsrOutgoingInterface(route))
            {
                // 移除对应条目。
                RemoveHostNetworkAssociation(route.GetDestNetwork(), route.GetDestNetworkMask());
            }
        }
    }

    // 将 routingTableAssociation 设置为新的路由表。
    m_routingTableAssociation = routingTable;

    // Iterate over entries of the associated routing table and
    // add the routes using non-polsr outgoing interfaces to the list
    // of local HNA associations
    NS_LOG_DEBUG("Nb local associations before adding some entries from"
                 " the associated routing table: "
                 << m_state.GetAssociations().size());
    for (uint32_t i = 0; i < m_routingTableAssociation->GetNRoutes(); i++)
    {
        Ipv4RoutingTableEntry route = m_routingTableAssociation->GetRoute(i);
        Ipv4Address destNetworkAddress = route.GetDestNetwork();
        Ipv4Mask destNetmask = route.GetDestNetworkMask();

        // If the outgoing interface for this route is a non-polsr interface,
        if (UsesNonPolsrOutgoingInterface(route))
        {
            // 将该条目的网络地址和网络掩码加入本地 HNA 条目列表。
            AddHostNetworkAssociation(destNetworkAddress, destNetmask);
        }
    }
    NS_LOG_DEBUG("Nb local associations after having added some entries from "
                 "the associated routing table: "
                 << m_state.GetAssociations().size());
}

bool
RoutingProtocol::UsesNonPolsrOutgoingInterface(const Ipv4RoutingTableEntry& route)
{
    auto ci = m_interfaceExclusions.find(route.GetInterface());
    // The outgoing interface is a non-POLSR interface if a match is found
    // before reaching the end of the list of excluded interfaces
    return ci != m_interfaceExclusions.end();
}

void
RoutingProtocol::LinkSensing(const polsr::MessageHeader& msg,
                             const polsr::MessageHeader::Hello& hello,
                             const Ipv4Address& receiverIface,
                             const Ipv4Address& senderIface)
{
    Time now = Simulator::Now();
    bool updated = false;
    bool created = false;
    NS_LOG_DEBUG("@" << now.As(Time::S) << ": Polsr node " << m_mainAddress
                     << ": LinkSensing(receiverIface=" << receiverIface
                     << ", senderIface=" << senderIface << ") BEGIN");

    NS_ASSERT(msg.GetVTime().IsStrictlyPositive());
    LinkTuple* link_tuple = m_state.FindLinkTuple(senderIface);
    if (link_tuple == nullptr)
    {
        LinkTuple newLinkTuple;
        // 这里需要创建一个新的 tuple。
        newLinkTuple.neighborIfaceAddr = senderIface;
        newLinkTuple.localIfaceAddr = receiverIface;
        newLinkTuple.symTime = now - Seconds(1);
        newLinkTuple.time = now + msg.GetVTime();
        link_tuple = &m_state.InsertLinkTuple(newLinkTuple);
        created = true;
        NS_LOG_LOGIC("Existing link tuple did not exist => creating new one");
    }
    else
    {
        NS_LOG_LOGIC("Existing link tuple already exists => will update it");
        updated = true;
    }

    link_tuple->asymTime = now + msg.GetVTime();
    for (auto linkMessage = hello.linkMessages.begin(); linkMessage != hello.linkMessages.end();
         linkMessage++)
    {
        auto linkType = LinkType(linkMessage->linkCode & 0x03);
        auto neighborType = NeighborType((linkMessage->linkCode >> 2) & 0x03);

        NS_LOG_DEBUG("Looking at HELLO link messages with Link Type "
                     << linkType << " and Neighbor Type " << neighborType);

   // 不处理无效的被通告链路。
        if ((linkType == LinkType::SYM_LINK && neighborType == NeighborType::NOT_NEIGH) ||
            (neighborType != NeighborType::SYM_NEIGH && neighborType != NeighborType::MPR_NEIGH &&
             neighborType != NeighborType::NOT_NEIGH))
        {
            NS_LOG_LOGIC("HELLO link code is invalid => IGNORING");
            continue;
        }

        for (auto neighIfaceAddr = linkMessage->neighborInterfaceAddresses.begin();
             neighIfaceAddr != linkMessage->neighborInterfaceAddresses.end();
             neighIfaceAddr++)
        {
            NS_LOG_DEBUG("   -> Neighbor: " << *neighIfaceAddr);
            if (*neighIfaceAddr == receiverIface)
            {
                if (linkType == LinkType::LOST_LINK)
                {
                    NS_LOG_LOGIC("link is LOST => expiring it");
                    link_tuple->symTime = now - Seconds(1);
                    updated = true;
                }
                else if (linkType == LinkType::SYM_LINK || linkType == LinkType::ASYM_LINK)
                {
                    NS_LOG_DEBUG(*link_tuple << ": link is SYM or ASYM => should become SYM now"
                                                " (symTime being increased to "
                                             << now + msg.GetVTime());
                    link_tuple->symTime = now + msg.GetVTime();
                    link_tuple->time = link_tuple->symTime + POLSR_NEIGHB_HOLD_TIME;
                    updated = true;
                }
                else
                {
                    NS_FATAL_ERROR("bad link type");
                }
                break;
            }
            else
            {
                NS_LOG_DEBUG("     \\-> *neighIfaceAddr (" << *neighIfaceAddr
                                                           << " != receiverIface (" << receiverIface
                                                           << ") => IGNORING!");
            }
        }
        NS_LOG_DEBUG("Link tuple updated: " << int(updated));
    }
    link_tuple->time = std::max(link_tuple->time, link_tuple->asymTime);

    if (updated)
    {
        LinkTupleUpdated(*link_tuple, hello.willingness);
    }

    // 安排 link tuple 的过期删除定时器。
    if (created)
    {
        LinkTupleAdded(*link_tuple, hello.willingness);
        m_events.Track(Simulator::Schedule(DELAY(std::min(link_tuple->time, link_tuple->symTime)),
                                           &RoutingProtocol::LinkTupleTimerExpire,
                                           this,
                                           link_tuple->neighborIfaceAddr));
    }
    NS_LOG_DEBUG("@" << now.As(Time::S) << ": Polsr node " << m_mainAddress << ": LinkSensing END");
}

void
RoutingProtocol::PopulateNeighborSet(const polsr::MessageHeader& msg,
                                     const polsr::MessageHeader::Hello& hello)
{
    NeighborTuple* nb_tuple = m_state.FindNeighborTuple(msg.GetOriginatorAddress());
    if (nb_tuple != nullptr)
    {
        nb_tuple->willingness = hello.willingness;
    }
}

void
RoutingProtocol::PopulateTwoHopNeighborSet(const polsr::MessageHeader& msg,
                                           const polsr::MessageHeader::Hello& hello)
{
    Time now = Simulator::Now();

    NS_LOG_DEBUG("Polsr node " << m_mainAddress << ": PopulateTwoHopNeighborSet BEGIN");

    for (auto link_tuple = m_state.GetLinks().begin(); link_tuple != m_state.GetLinks().end();
         link_tuple++)
    {
        NS_LOG_LOGIC("Looking at link tuple: " << *link_tuple);
        if (GetMainAddress(link_tuple->neighborIfaceAddr) != msg.GetOriginatorAddress())
        {
            NS_LOG_LOGIC(
                "Link tuple ignored: "
                "GetMainAddress (link_tuple->neighborIfaceAddr) != msg.GetOriginatorAddress ()");
            NS_LOG_LOGIC("(GetMainAddress("
                         << link_tuple->neighborIfaceAddr
                         << "): " << GetMainAddress(link_tuple->neighborIfaceAddr)
                         << "; msg.GetOriginatorAddress (): " << msg.GetOriginatorAddress());
            continue;
        }

        if (link_tuple->symTime < now)
        {
            NS_LOG_LOGIC("Link tuple ignored: expired.");
            continue;
        }

        for (auto linkMessage = hello.linkMessages.begin(); linkMessage != hello.linkMessages.end();
             linkMessage++)
        {
            auto neighborType = NeighborType((linkMessage->linkCode >> 2) & 0x3);
            NS_LOG_DEBUG(
                "Looking at Link Message from HELLO message: neighborType=" << neighborType);

            for (auto nb2hop_addr_iter = linkMessage->neighborInterfaceAddresses.begin();
                 nb2hop_addr_iter != linkMessage->neighborInterfaceAddresses.end();
                 nb2hop_addr_iter++)
            {
                Ipv4Address nb2hop_addr = GetMainAddress(*nb2hop_addr_iter);
                NS_LOG_DEBUG("Looking at 2-hop neighbor address from HELLO message: "
                             << *nb2hop_addr_iter << " (main address is " << nb2hop_addr << ")");
                if (neighborType == NeighborType::SYM_NEIGH ||
                    neighborType == NeighborType::MPR_NEIGH)
                {
                    // 如果二跳邻居地址的主地址等于
                    // 当前接收节点的主地址，则静默丢弃该二跳
                    // 邻居地址。
                    if (nb2hop_addr == m_mainAddress)
                    {
                        NS_LOG_LOGIC("Ignoring 2-hop neighbor (it is the node itself)");
                        continue;
                    }

                    // 否则创建一个二跳 tuple。
                    TwoHopNeighborTuple* nb2hop_tuple =
                        m_state.FindTwoHopNeighborTuple(msg.GetOriginatorAddress(), nb2hop_addr);
                    NS_LOG_LOGIC("Adding the 2-hop neighbor"
                                 << (nb2hop_tuple ? " (refreshing existing entry)" : ""));
                    if (nb2hop_tuple == nullptr)
                    {
                        TwoHopNeighborTuple new_nb2hop_tuple;
                        new_nb2hop_tuple.neighborMainAddr = msg.GetOriginatorAddress();
                        new_nb2hop_tuple.twoHopNeighborAddr = nb2hop_addr;
                        new_nb2hop_tuple.expirationTime = now + msg.GetVTime();
                        AddTwoHopNeighborTuple(new_nb2hop_tuple);
                        // 安排二跳邻居 tuple 的过期删除定时器。
                        m_events.Track(Simulator::Schedule(DELAY(new_nb2hop_tuple.expirationTime),
                                                           &RoutingProtocol::Nb2hopTupleTimerExpire,
                                                           this,
                                                           new_nb2hop_tuple.neighborMainAddr,
                                                           new_nb2hop_tuple.twoHopNeighborAddr));
                    }
                    else
                    {
                        nb2hop_tuple->expirationTime = now + msg.GetVTime();
                    }
                }
                else if (neighborType == NeighborType::NOT_NEIGH)
                {
                    // 对 HELLO message 中列出的每个二跳节点，
                    // 如果其 Neighbor Type 等于 NOT_NEIGH，则删除所有满足条件的二跳
                    // tuple：N_neighbor_main_addr == Originator Address，
                    // 并且 N_2hop_addr == 该二跳邻居的主地址。
                    // 这些二跳邻居记录会被删除。
                    NS_LOG_LOGIC(
                        "2-hop neighbor is NOT_NEIGH => deleting matching 2-hop neighbor state");
                    m_state.EraseTwoHopNeighborTuples(msg.GetOriginatorAddress(), nb2hop_addr);
                }
                else
                {
                    NS_LOG_LOGIC("*** WARNING *** Ignoring link message (inside HELLO) with bad"
                                 " neighbor type value: "
                                 << neighborType);
                }
            }
        }
    }

    NS_LOG_DEBUG("Polsr node " << m_mainAddress << ": PopulateTwoHopNeighborSet END");
}

void
RoutingProtocol::PopulateMprSelectorSet(const polsr::MessageHeader& msg,
                                        const polsr::MessageHeader::Hello& hello)
{
    NS_LOG_FUNCTION(this);

    Time now = Simulator::Now();

    for (auto linkMessage = hello.linkMessages.begin(); linkMessage != hello.linkMessages.end();
         linkMessage++)
    {
        auto neighborType = NeighborType(linkMessage->linkCode >> 2);
        if (neighborType == NeighborType::MPR_NEIGH)
        {
            NS_LOG_DEBUG("Processing a link message with neighbor type MPR_NEIGH");

            for (auto nb_iface_addr = linkMessage->neighborInterfaceAddresses.begin();
                 nb_iface_addr != linkMessage->neighborInterfaceAddresses.end();
                 nb_iface_addr++)
            {
                if (GetMainAddress(*nb_iface_addr) == m_mainAddress)
                {
                    NS_LOG_DEBUG("Adding entry to mpr selector set for neighbor "
                                 << *nb_iface_addr);

                    // We must create a new entry into the mpr selector set
                    MprSelectorTuple* existing_mprsel_tuple =
                        m_state.FindMprSelectorTuple(msg.GetOriginatorAddress());
                    if (existing_mprsel_tuple == nullptr)
                    {
                        MprSelectorTuple mprsel_tuple;

                        mprsel_tuple.mainAddr = msg.GetOriginatorAddress();
                        mprsel_tuple.expirationTime = now + msg.GetVTime();
                        AddMprSelectorTuple(mprsel_tuple);

                        // 安排 MPR selector tuple 的过期删除定时器。
                        m_events.Track(Simulator::Schedule(DELAY(mprsel_tuple.expirationTime),
                                                           &RoutingProtocol::MprSelTupleTimerExpire,
                                                           this,
                                                           mprsel_tuple.mainAddr));
                    }
                    else
                    {
                        existing_mprsel_tuple->expirationTime = now + msg.GetVTime();
                    }
                }
            }
        }
    }
    NS_LOG_DEBUG("Computed MPR selector set for node " << m_mainAddress << ": "
                                                       << m_state.PrintMprSelectorSet());
}

#if 0
///
/// @brief 因为 MAC 层无法把 packet 投递到对应目的地，所以丢弃该 packet。
/// 这可能导致邻居失效，因此后续需要采取相应处理。
/// 相关处理会在这里触发。
///
/// @param p MAC 层无法投递的 packet。
///
void
POLSR::mac_failed(Ptr<Packet> p)
{
    double now = Simulator::Now ();
    struct hdr_ip* ih = HDR_IP (p);
    struct hdr_cmn* ch = HDR_CMN (p);

    debug("%f: Node %d MAC Layer detects a breakage on link to %d\n",
          now,
          POLSR::node_id (ra_addr ()),
          POLSR::node_id (ch->next_hop ()));

    if ((uint32_t)ih->daddr () == IP_BROADCAST)
    {
        drop (p, DROP_RTR_MAC_CALLBACK);
        return;
    }

    POLSR_link_tuple* link_tuple = state_.find_link_tuple(ch->next_hop());
    if (link_tuple)
    {
        link_tuple->lost_time() = now + POLSR_NEIGHB_HOLD_TIME;
        link_tuple->time() = now + POLSR_NEIGHB_HOLD_TIME;
        nb_loss(link_tuple);
    }
    drop(p, DROP_RTR_MAC_CALLBACK);
}
#endif

void
RoutingProtocol::NeighborLoss(const LinkTuple& tuple)
{
    NS_LOG_DEBUG(Simulator::Now().As(Time::S) << ": POLSR Node " << m_mainAddress << " LinkTuple "
                                              << tuple.neighborIfaceAddr << " -> neighbor loss.");
    LinkTupleUpdated(tuple, Willingness::DEFAULT);
    m_state.EraseTwoHopNeighborTuples(GetMainAddress(tuple.neighborIfaceAddr));
    m_state.EraseMprSelectorTuples(GetMainAddress(tuple.neighborIfaceAddr));

    MprComputation();
    RoutingTableComputation();
}

void
RoutingProtocol::AddDuplicateTuple(const DuplicateTuple& tuple)
{
    /*debug("%f: Node %d adds dup tuple: addr = %d seq_num = %d\n",
            Simulator::Now (),
            POLSR::node_id(ra_addr()),
            POLSR::node_id(tuple->addr()),
            tuple->seq_num());*/
    m_state.InsertDuplicateTuple(tuple);
}

void
RoutingProtocol::RemoveDuplicateTuple(const DuplicateTuple& tuple)
{
    /*debug("%f: Node %d removes dup tuple: addr = %d seq_num = %d\n",
      Simulator::Now (),
      POLSR::node_id(ra_addr()),
      POLSR::node_id(tuple->addr()),
      tuple->seq_num());*/
    m_state.EraseDuplicateTuple(tuple);
}

void
RoutingProtocol::LinkTupleAdded(const LinkTuple& tuple, Willingness willingness)
{
    // Creates associated neighbor tuple
    NeighborTuple nb_tuple;
    nb_tuple.neighborMainAddr = GetMainAddress(tuple.neighborIfaceAddr);
    nb_tuple.willingness = willingness;

    if (tuple.symTime >= Simulator::Now())
    {
        nb_tuple.status = NeighborTuple::STATUS_SYM;
    }
    else
    {
        nb_tuple.status = NeighborTuple::STATUS_NOT_SYM;
    }

    AddNeighborTuple(nb_tuple);
}

void
RoutingProtocol::RemoveLinkTuple(const LinkTuple& tuple)
{
    NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                 << ": POLSR Node " << m_mainAddress << " LinkTuple " << tuple << " REMOVED.");

    m_state.EraseNeighborTuple(GetMainAddress(tuple.neighborIfaceAddr));
    m_state.EraseLinkTuple(tuple);
}

void
RoutingProtocol::LinkTupleUpdated(const LinkTuple& tuple, Willingness willingness)
{
    // Each time a link tuple changes, the associated neighbor tuple must be recomputed

    NS_LOG_DEBUG(Simulator::Now().As(Time::S)
                 << ": POLSR Node " << m_mainAddress << " LinkTuple " << tuple << " UPDATED.");

    NeighborTuple* nb_tuple = m_state.FindNeighborTuple(GetMainAddress(tuple.neighborIfaceAddr));

    if (nb_tuple == nullptr)
    {
        LinkTupleAdded(tuple, willingness);
        nb_tuple = m_state.FindNeighborTuple(GetMainAddress(tuple.neighborIfaceAddr));
    }

    if (nb_tuple != nullptr)
    {
        int statusBefore = nb_tuple->status;

        bool hasSymmetricLink = false;

        const LinkSet& linkSet = m_state.GetLinks();
        for (auto it = linkSet.begin(); it != linkSet.end(); it++)
        {
            const LinkTuple& link_tuple = *it;
            if (GetMainAddress(link_tuple.neighborIfaceAddr) == nb_tuple->neighborMainAddr &&
                link_tuple.symTime >= Simulator::Now())
            {
                hasSymmetricLink = true;
                break;
            }
        }

        if (hasSymmetricLink)
        {
            nb_tuple->status = NeighborTuple::STATUS_SYM;
            NS_LOG_DEBUG(*nb_tuple << "->status = STATUS_SYM; changed:"
                                   << int(statusBefore != nb_tuple->status));
        }
        else
        {
            nb_tuple->status = NeighborTuple::STATUS_NOT_SYM;
            NS_LOG_DEBUG(*nb_tuple << "->status = STATUS_NOT_SYM; changed:"
                                   << int(statusBefore != nb_tuple->status));
        }
    }
    else
    {
        NS_LOG_WARN("ERROR! Wanted to update a NeighborTuple but none was found!");
    }
}

void
RoutingProtocol::AddNeighborTuple(const NeighborTuple& tuple)
{
    //   debug("%f: Node %d adds neighbor tuple: nb_addr = %d status = %s\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->neighborMainAddr),
    //         ((tuple->status() == POLSR_STATUS_SYM) ? "sym" : "not_sym"));

    m_state.InsertNeighborTuple(tuple);
    IncrementAnsn();
}

void
RoutingProtocol::RemoveNeighborTuple(const NeighborTuple& tuple)
{
    //   debug("%f: Node %d removes neighbor tuple: nb_addr = %d status = %s\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->neighborMainAddr),
    //         ((tuple->status() == POLSR_STATUS_SYM) ? "sym" : "not_sym"));

    m_state.EraseNeighborTuple(tuple);
    IncrementAnsn();
}

void
RoutingProtocol::AddTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple)
{
    //   debug("%f: Node %d adds 2-hop neighbor tuple: nb_addr = %d nb2hop_addr = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->neighborMainAddr),
    //         POLSR::node_id(tuple->twoHopNeighborAddr));

    m_state.InsertTwoHopNeighborTuple(tuple);
}

void
RoutingProtocol::RemoveTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple)
{
    //   debug("%f: Node %d removes 2-hop neighbor tuple: nb_addr = %d nb2hop_addr = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->neighborMainAddr),
    //         POLSR::node_id(tuple->twoHopNeighborAddr));

    m_state.EraseTwoHopNeighborTuple(tuple);
}

void
RoutingProtocol::IncrementAnsn()
{
    m_ansn = (m_ansn + 1) % (POLSR_MAX_SEQ_NUM + 1);
}

void
RoutingProtocol::AddMprSelectorTuple(const MprSelectorTuple& tuple)
{
    //   debug("%f: Node %d adds MPR selector tuple: nb_addr = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->main_addr()));

    m_state.InsertMprSelectorTuple(tuple);
    IncrementAnsn();
}

void
RoutingProtocol::RemoveMprSelectorTuple(const MprSelectorTuple& tuple)
{
    //   debug("%f: Node %d removes MPR selector tuple: nb_addr = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->main_addr()));

    m_state.EraseMprSelectorTuple(tuple);
    IncrementAnsn();
}

void
RoutingProtocol::AddTopologyTuple(const TopologyTuple& tuple)
{
    //   debug("%f: Node %d adds topology tuple: dest_addr = %d last_addr = %d seq = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->dest_addr()),
    //         POLSR::node_id(tuple->last_addr()),
    //         tuple->seq());

    m_state.InsertTopologyTuple(tuple);
}

void
RoutingProtocol::RemoveTopologyTuple(const TopologyTuple& tuple)
{
    //   debug("%f: Node %d removes topology tuple: dest_addr = %d last_addr = %d seq = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->dest_addr()),
    //         POLSR::node_id(tuple->last_addr()),
    //         tuple->seq());

    m_state.EraseTopologyTuple(tuple);
}

void
RoutingProtocol::AddIfaceAssocTuple(const IfaceAssocTuple& tuple)
{
    //   debug("%f: Node %d adds iface association tuple: main_addr = %d iface_addr = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->main_addr()),
    //         POLSR::node_id(tuple->iface_addr()));

    m_state.InsertIfaceAssocTuple(tuple);
}

void
RoutingProtocol::RemoveIfaceAssocTuple(const IfaceAssocTuple& tuple)
{
    //   debug("%f: Node %d removes iface association tuple: main_addr = %d iface_addr = %d\n",
    //         Simulator::Now (),
    //         POLSR::node_id(ra_addr()),
    //         POLSR::node_id(tuple->main_addr()),
    //         POLSR::node_id(tuple->iface_addr()));

    m_state.EraseIfaceAssocTuple(tuple);
}

void
RoutingProtocol::AddAssociationTuple(const AssociationTuple& tuple)
{
    m_state.InsertAssociationTuple(tuple);
}

void
RoutingProtocol::RemoveAssociationTuple(const AssociationTuple& tuple)
{
    m_state.EraseAssociationTuple(tuple);
}

uint16_t
RoutingProtocol::GetPacketSequenceNumber()
{
    m_packetSequenceNumber = (m_packetSequenceNumber + 1) % (POLSR_MAX_SEQ_NUM + 1);
    return m_packetSequenceNumber;
}

uint16_t
RoutingProtocol::GetMessageSequenceNumber()
{
    m_messageSequenceNumber = (m_messageSequenceNumber + 1) % (POLSR_MAX_SEQ_NUM + 1);
    return m_messageSequenceNumber;
}

void
RoutingProtocol::HelloTimerExpire()
{
    ApplyHelloLossDecay();
    SendHello();
    m_helloTimer.Schedule(m_helloInterval);
}

void
RoutingProtocol::TcTimerExpire()
{
    if (!m_state.GetMprSelectors().empty())
    {
        SendTc();
    }
    else
    {
        NS_LOG_DEBUG("Not sending any TC, no one selected me as MPR.");
    }
    m_tcTimer.Schedule(m_tcInterval);
}

void
RoutingProtocol::MidTimerExpire()
{
    SendMid();
    m_midTimer.Schedule(m_midInterval);
}

void
RoutingProtocol::HnaTimerExpire()
{
    if (!m_state.GetAssociations().empty())
    {
        SendHna();
    }
    else
    {
        NS_LOG_DEBUG("Not sending any HNA, no associations to advertise.");
    }
    m_hnaTimer.Schedule(m_hnaInterval);
}

void
RoutingProtocol::DupTupleTimerExpire(Ipv4Address address, uint16_t sequenceNumber)
{
    DuplicateTuple* tuple = m_state.FindDuplicateTuple(address, sequenceNumber);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->expirationTime < Simulator::Now())
    {
        RemoveDuplicateTuple(*tuple);
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(tuple->expirationTime),
                                           &RoutingProtocol::DupTupleTimerExpire,
                                           this,
                                           address,
                                           sequenceNumber));
    }
}

void
RoutingProtocol::LinkTupleTimerExpire(Ipv4Address neighborIfaceAddr)
{
    Time now = Simulator::Now();

    // the tuple parameter may be a stale copy; get a newer version from m_state
    LinkTuple* tuple = m_state.FindLinkTuple(neighborIfaceAddr);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->time < now)
    {
        RemoveLinkTuple(*tuple);
    }
    else if (tuple->symTime < now)
    {
        if (m_linkTupleTimerFirstTime)
        {
            m_linkTupleTimerFirstTime = false;
        }
        else
        {
            NeighborLoss(*tuple);
        }

        m_events.Track(Simulator::Schedule(DELAY(tuple->time),
                                           &RoutingProtocol::LinkTupleTimerExpire,
                                           this,
                                           neighborIfaceAddr));
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(std::min(tuple->time, tuple->symTime)),
                                           &RoutingProtocol::LinkTupleTimerExpire,
                                           this,
                                           neighborIfaceAddr));
    }
}

void
RoutingProtocol::Nb2hopTupleTimerExpire(Ipv4Address neighborMainAddr,
                                        Ipv4Address twoHopNeighborAddr)
{
    TwoHopNeighborTuple* tuple;
    tuple = m_state.FindTwoHopNeighborTuple(neighborMainAddr, twoHopNeighborAddr);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->expirationTime < Simulator::Now())
    {
        RemoveTwoHopNeighborTuple(*tuple);
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(tuple->expirationTime),
                                           &RoutingProtocol::Nb2hopTupleTimerExpire,
                                           this,
                                           neighborMainAddr,
                                           twoHopNeighborAddr));
    }
}

void
RoutingProtocol::MprSelTupleTimerExpire(Ipv4Address mainAddr)
{
    MprSelectorTuple* tuple = m_state.FindMprSelectorTuple(mainAddr);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->expirationTime < Simulator::Now())
    {
        RemoveMprSelectorTuple(*tuple);
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(tuple->expirationTime),
                                           &RoutingProtocol::MprSelTupleTimerExpire,
                                           this,
                                           mainAddr));
    }
}

void
RoutingProtocol::TopologyTupleTimerExpire(Ipv4Address destAddr, Ipv4Address lastAddr)
{
    TopologyTuple* tuple = m_state.FindTopologyTuple(destAddr, lastAddr);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->expirationTime < Simulator::Now())
    {
        RemoveTopologyTuple(*tuple);
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(tuple->expirationTime),
                                           &RoutingProtocol::TopologyTupleTimerExpire,
                                           this,
                                           tuple->destAddr,
                                           tuple->lastAddr));
    }
}

void
RoutingProtocol::IfaceAssocTupleTimerExpire(Ipv4Address ifaceAddr)
{
    IfaceAssocTuple* tuple = m_state.FindIfaceAssocTuple(ifaceAddr);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->time < Simulator::Now())
    {
        RemoveIfaceAssocTuple(*tuple);
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(tuple->time),
                                           &RoutingProtocol::IfaceAssocTupleTimerExpire,
                                           this,
                                           ifaceAddr));
    }
}

void
RoutingProtocol::AssociationTupleTimerExpire(Ipv4Address gatewayAddr,
                                             Ipv4Address networkAddr,
                                             Ipv4Mask netmask)
{
    AssociationTuple* tuple = m_state.FindAssociationTuple(gatewayAddr, networkAddr, netmask);
    if (tuple == nullptr)
    {
        return;
    }
    if (tuple->expirationTime < Simulator::Now())
    {
        RemoveAssociationTuple(*tuple);
    }
    else
    {
        m_events.Track(Simulator::Schedule(DELAY(tuple->expirationTime),
                                           &RoutingProtocol::AssociationTupleTimerExpire,
                                           this,
                                           gatewayAddr,
                                           networkAddr,
                                           netmask));
    }
}

void
RoutingProtocol::Clear()
{
    NS_LOG_FUNCTION_NOARGS();
    m_table.clear();
}

void
RoutingProtocol::RemoveEntry(const Ipv4Address& dest)
{
    m_table.erase(dest);
}

bool
RoutingProtocol::Lookup(const Ipv4Address& dest, RoutingTableEntry& outEntry) const
{
    // Get the iterator at "dest" position
    auto it = m_table.find(dest);
    // If there is no route to "dest", return NULL
    if (it == m_table.end())
    {
        return false;
    }
    outEntry = it->second;
    return true;
}

bool
RoutingProtocol::FindSendEntry(const RoutingTableEntry& entry, RoutingTableEntry& outEntry) const
{
    outEntry = entry;
    while (outEntry.destAddr != outEntry.nextAddr)
    {
        if (not Lookup(outEntry.nextAddr, outEntry))
        {
            return false;
        }
    }
    return true;
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput(Ptr<Packet> p,
                             const Ipv4Header& header,
                             Ptr<NetDevice> oif,
                             Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << " " << m_ipv4->GetObject<Node>()->GetId() << " "
                         << header.GetDestination() << " " << oif);
    Ptr<Ipv4Route> rtentry;
    RoutingTableEntry entry1;
    RoutingTableEntry entry2;
    bool found = false;

    if (Lookup(header.GetDestination(), entry1))
    {
        bool foundSendEntry = FindSendEntry(entry1, entry2);
        if (!foundSendEntry)
        {
            NS_FATAL_ERROR("FindSendEntry failure");
        }
        uint32_t interfaceIdx = entry2.interface;
        if (oif && m_ipv4->GetInterfaceForDevice(oif) != static_cast<int>(interfaceIdx))
        {
            // We do not attempt to perform a constrained routing search
            // if the caller specifies the oif; we just enforce that
            // that the found route matches the requested outbound interface
            NS_LOG_DEBUG("Polsr node " << m_mainAddress
                                      << ": RouteOutput for dest=" << header.GetDestination()
                                      << " Route interface " << interfaceIdx
                                      << " does not match requested output interface "
                                      << m_ipv4->GetInterfaceForDevice(oif));
            sockerr = Socket::ERROR_NOROUTETOHOST;
            return rtentry;
        }
        rtentry = Create<Ipv4Route>();
        rtentry->SetDestination(header.GetDestination());
        // the source address is the interface address that matches
        // the destination address (when multiple are present on the
        // outgoing interface, one is selected via scoping rules)
        NS_ASSERT(m_ipv4);
        uint32_t numOifAddresses = m_ipv4->GetNAddresses(interfaceIdx);
        NS_ASSERT(numOifAddresses > 0);
        Ipv4InterfaceAddress ifAddr;
        if (numOifAddresses == 1)
        {
            ifAddr = m_ipv4->GetAddress(interfaceIdx, 0);
        }
        else
        {
            /// @todo Implement IP aliasing and POLSR
            NS_FATAL_ERROR("XXX Not implemented yet:  IP aliasing and POLSR");
        }
        rtentry->SetSource(ifAddr.GetLocal());
        rtentry->SetGateway(entry2.nextAddr);
        rtentry->SetOutputDevice(m_ipv4->GetNetDevice(interfaceIdx));
        sockerr = Socket::ERROR_NOTERROR;
        NS_LOG_DEBUG("Polsr node " << m_mainAddress << ": RouteOutput for dest="
                                  << header.GetDestination() << " --> nextHop=" << entry2.nextAddr
                                  << " interface=" << entry2.interface);
        NS_LOG_DEBUG("Found route to " << rtentry->GetDestination() << " via nh "
                                       << rtentry->GetGateway() << " with source addr "
                                       << rtentry->GetSource() << " and output dev "
                                       << rtentry->GetOutputDevice());
        found = true;
    }
    else
    {
        rtentry = m_hnaRoutingTable->RouteOutput(p, header, oif, sockerr);

        if (rtentry)
        {
            found = true;
            NS_LOG_DEBUG("Found route to " << rtentry->GetDestination() << " via nh "
                                           << rtentry->GetGateway() << " with source addr "
                                           << rtentry->GetSource() << " and output dev "
                                           << rtentry->GetOutputDevice());
        }
    }

    if (!found)
    {
        NS_LOG_DEBUG("Polsr node " << m_mainAddress << ": RouteOutput for dest="
                                  << header.GetDestination() << " No route to host");
        sockerr = Socket::ERROR_NOROUTETOHOST;
    }
    return rtentry;
}

bool
RoutingProtocol::RouteInput(Ptr<const Packet> p,
                            const Ipv4Header& header,
                            Ptr<const NetDevice> idev,
                            const UnicastForwardCallback& ucb,
                            const MulticastForwardCallback& mcb,
                            const LocalDeliverCallback& lcb,
                            const ErrorCallback& ecb)
{
    NS_LOG_FUNCTION(this << " " << m_ipv4->GetObject<Node>()->GetId() << " "
                         << header.GetDestination());

    Ipv4Address dst = header.GetDestination();
    Ipv4Address origin = header.GetSource();

    // Consume self-originated packets
    if (IsMyOwnAddress(origin))
    {
        return true;
    }

    // Local delivery
    NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
    uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);
    if (m_ipv4->IsDestinationAddress(dst, iif))
    {
        if (!lcb.IsNull())
        {
            NS_LOG_LOGIC("Local delivery to " << dst);
            lcb(p, header, iif);
            return true;
        }
        else
        {
            // The local delivery callback is null.  This may be a multicast
            // or broadcast packet, so return false so that another
            // multicast routing protocol can handle it.  It should be possible
            // to extend this to explicitly check whether it is a unicast
            // packet, and invoke the error callback if so
            NS_LOG_LOGIC("Null local delivery callback");
            return false;
        }
    }

    NS_LOG_LOGIC("Forward packet");
    // Forwarding
    Ptr<Ipv4Route> rtentry;
    RoutingTableEntry entry1;
    RoutingTableEntry entry2;
    if (Lookup(header.GetDestination(), entry1))
    {
        bool foundSendEntry = FindSendEntry(entry1, entry2);
        if (!foundSendEntry)
        {
            NS_FATAL_ERROR("FindSendEntry failure");
        }
        rtentry = Create<Ipv4Route>();
        rtentry->SetDestination(header.GetDestination());
        uint32_t interfaceIdx = entry2.interface;
        // the source address is the interface address that matches
        // the destination address (when multiple are present on the
        // outgoing interface, one is selected via scoping rules)
        NS_ASSERT(m_ipv4);
        uint32_t numOifAddresses = m_ipv4->GetNAddresses(interfaceIdx);
        NS_ASSERT(numOifAddresses > 0);
        Ipv4InterfaceAddress ifAddr;
        if (numOifAddresses == 1)
        {
            ifAddr = m_ipv4->GetAddress(interfaceIdx, 0);
        }
        else
        {
            /// @todo Implement IP aliasing and POLSR
            NS_FATAL_ERROR("XXX Not implemented yet:  IP aliasing and POLSR");
        }
        rtentry->SetSource(ifAddr.GetLocal());
        rtentry->SetGateway(entry2.nextAddr);
        rtentry->SetOutputDevice(m_ipv4->GetNetDevice(interfaceIdx));

        NS_LOG_DEBUG("Polsr node " << m_mainAddress << ": RouteInput for dest="
                                  << header.GetDestination() << " --> nextHop=" << entry2.nextAddr
                                  << " interface=" << entry2.interface);

        ucb(rtentry, p, header);
        return true;
    }
    else
    {
        NS_LOG_LOGIC("No dynamic route, check network routes");
        if (m_hnaRoutingTable->RouteInput(p, header, idev, ucb, mcb, lcb, ecb))
        {
            return true;
        }
        else
        {
#ifdef NS3_LOG_ENABLE
            NS_LOG_DEBUG("Polsr node " << m_mainAddress
                                      << ": RouteInput for dest=" << header.GetDestination()
                                      << " --> NOT FOUND; ** Dumping routing table...");

            for (auto iter = m_table.begin(); iter != m_table.end(); iter++)
            {
                NS_LOG_DEBUG("dest=" << iter->first << " --> next=" << iter->second.nextAddr
                                     << " via interface " << iter->second.interface);
            }

            NS_LOG_DEBUG("** Routing table dump end.");
#endif // NS3_LOG_ENABLE

            return false;
        }
    }
}

void
RoutingProtocol::NotifyInterfaceUp(uint32_t i)
{
}

void
RoutingProtocol::NotifyInterfaceDown(uint32_t i)
{
}

void
RoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
}

void
RoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
}

void
RoutingProtocol::AddEntry(const Ipv4Address& dest,
                          const Ipv4Address& next,
                          uint32_t interface,
                          uint32_t distance)
{
    NS_LOG_FUNCTION(this << dest << next << interface << distance << m_mainAddress);

    NS_ASSERT(distance > 0);

    // Creates a new rt entry with specified values
    RoutingTableEntry& entry = m_table[dest];

    entry.destAddr = dest;
    entry.nextAddr = next;
    entry.interface = interface;
    entry.distance = distance;
}

void
RoutingProtocol::AddEntry(const Ipv4Address& dest,
                          const Ipv4Address& next,
                          const Ipv4Address& interfaceAddress,
                          uint32_t distance)
{
    NS_LOG_FUNCTION(this << dest << next << interfaceAddress << distance << m_mainAddress);

    NS_ASSERT(distance > 0);
    NS_ASSERT(m_ipv4);

    RoutingTableEntry entry;
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
    {
        for (uint32_t j = 0; j < m_ipv4->GetNAddresses(i); j++)
        {
            if (m_ipv4->GetAddress(i, j).GetLocal() == interfaceAddress)
            {
                AddEntry(dest, next, i, distance);
                return;
            }
        }
    }
    NS_ASSERT(false); // should not be reached
    AddEntry(dest, next, 0, distance);
}

std::vector<RoutingTableEntry>
RoutingProtocol::GetRoutingTableEntries() const
{
    std::vector<RoutingTableEntry> retval;
    for (auto iter = m_table.begin(); iter != m_table.end(); iter++)
    {
        retval.push_back(iter->second);
    }
    return retval;
}

MprSet
RoutingProtocol::GetMprSet() const
{
    return m_state.GetMprSet();
}

const MprSelectorSet&
RoutingProtocol::GetMprSelectors() const
{
    return m_state.GetMprSelectors();
}

const NeighborSet&
RoutingProtocol::GetNeighbors() const
{
    return m_state.GetNeighbors();
}

const TwoHopNeighborSet&
RoutingProtocol::GetTwoHopNeighbors() const
{
    return m_state.GetTwoHopNeighbors();
}

const TopologySet&
RoutingProtocol::GetTopologySet() const
{
    return m_state.GetTopologySet();
}

const PolsrState&
RoutingProtocol::GetPolsrState() const
{
    return m_state;
}

int64_t
RoutingProtocol::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_uniformRandomVariable->SetStream(stream);
    return 1;
}

bool
RoutingProtocol::IsMyOwnAddress(const Ipv4Address& a) const
{
    for (auto j = m_sendSockets.begin(); j != m_sendSockets.end(); ++j)
    {
        Ipv4InterfaceAddress iface = j->second;
        if (a == iface.GetLocal())
        {
            return true;
        }
    }
    return false;
}

void
RoutingProtocol::Dump()
{
#ifdef NS3_LOG_ENABLE
    Time now = Simulator::Now();
    NS_LOG_DEBUG("Dumping for node with main address " << m_mainAddress);
    NS_LOG_DEBUG(" Neighbor set");
    for (auto iter = m_state.GetNeighbors().begin(); iter != m_state.GetNeighbors().end(); iter++)
    {
        NS_LOG_DEBUG("  " << *iter);
    }
    NS_LOG_DEBUG(" Two-hop neighbor set");
    for (auto iter = m_state.GetTwoHopNeighbors().begin();
         iter != m_state.GetTwoHopNeighbors().end();
         iter++)
    {
        if (now < iter->expirationTime)
        {
            NS_LOG_DEBUG("  " << *iter);
        }
    }
    NS_LOG_DEBUG(" Routing table");
    for (auto iter = m_table.begin(); iter != m_table.end(); iter++)
    {
        NS_LOG_DEBUG("  dest=" << iter->first << " --> next=" << iter->second.nextAddr
                               << " via interface " << iter->second.interface);
    }
    NS_LOG_DEBUG("");
#endif // NS3_LOG_ENABLE
}

Ptr<const Ipv4StaticRouting>
RoutingProtocol::GetRoutingTableAssociation() const
{
    return m_hnaRoutingTable;
}

} // namespace polsr
} // namespace ns3
