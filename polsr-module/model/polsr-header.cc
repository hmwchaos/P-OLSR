/*
 * Copyright (c) 2007 INESC Porto
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Gustavo J. A. M. Carneiro  <gjc@inescporto.pt>
 */

#include "polsr-header.h"

#include "ns3/assert.h"
#include "ns3/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define IPV4_ADDRESS_SIZE 4
#define POLSR_MSG_HEADER_SIZE 12
#define POLSR_PKT_HEADER_SIZE 4
/*
 * P-OLSR 扩展标记。
 *
 * 本地 P-OLSR 副本可能会和其他本地 P-OLSR 节点交换扩展 HELLO/TC 消息。
 * 这些标记用于让反序列化逻辑判断消息体是否携带额外预测字段，
 * 还是旧的、没有扩展字段的 OLSR 格式消息体。
 */
#define POLSR_HELLO_GPS_MAGIC 0x504f
#define POLSR_HELLO_GPS_SIZE 16
#define POLSR_TC_PREDICTION_MAGIC 0x504f
#define POLSR_TC_NEIGHBOR_INFO_SIZE 12

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PolsrHeader");

namespace polsr
{

/// RFC 3626 中使用的缩放因子。
#define POLSR_C 0.0625

//把 double 转成 16 位有符号定点数
static uint16_t
EncodeSignedScaled16(double value, double scale)
{
    if (!std::isfinite(value))
    {
        value = 0.0;
    }

    const double maxValue = 32767.0 / scale;
    const double minValue = -32768.0 / scale;
    value = std::min(std::max(value, minValue), maxValue);
    int16_t encoded = static_cast<int16_t>(std::llround(value * scale));
    return static_cast<uint16_t>(encoded);
}

static double
DecodeSignedScaled16(uint16_t value, double scale)
{
    return static_cast<int16_t>(value) / scale;
}
//把 [0, 1] 范围内的接收率压缩成 1 字节
static uint8_t
EncodeRatio(double value)
{
    if (!std::isfinite(value))
    {
        value = 0.0;
    }
    value = std::min(std::max(value, 0.0), 1.0);
    return static_cast<uint8_t>(std::llround(value * 255.0));
}

static double
DecodeRatio(uint8_t value)
{
    return static_cast<double>(value) / 255.0;
}
//把 double 转成 4 字节 float 的二进制形式
static uint32_t
EncodeFloat32(double value)
{
    float narrowed = std::isfinite(value) ? static_cast<float>(value) : 0.0f;
    uint32_t bits;
    std::memcpy(&bits, &narrowed, sizeof(bits));
    return bits;
}

static double
DecodeFloat32(uint32_t value)
{
    float decoded;
    std::memcpy(&decoded, &value, sizeof(decoded));
    return decoded;
}
//把非负 double 转成 32 位无符号定点数。
static uint32_t
EncodeUnsignedScaled(double value, double scale)
{
    if (!std::isfinite(value) || value < 0.0)
    {
        value = 0.0;
    }

    const double maxValue = 4294967295.0 / scale;
    value = std::min(value, maxValue);
    return static_cast<uint32_t>(std::llround(value * scale));
}

static double
DecodeUnsignedScaled(uint32_t value, double scale)
{
    return static_cast<double>(value) / scale;
}

///
/// @brief 将十进制秒数转换为尾数/指数格式。
///
/// @param seconds 需要转换的十进制秒数。
/// @return 尾数/指数格式编码后的秒数。
///
uint8_t
SecondsToEmf(double seconds)
{
    int a;
    int b = 0;

    NS_ASSERT_MSG(seconds >= POLSR_C, "SecondsToEmf - Can not convert a value less than POLSR_C");

    // 查找满足 T/C >= 2^b 的最大整数 b。
    for (b = 1; (seconds / POLSR_C) >= (1 << b); ++b)
    {
    }
    NS_ASSERT((seconds / POLSR_C) < (1 << b));
    b--;
    NS_ASSERT((seconds / POLSR_C) >= (1 << b));

    // 计算表达式 16*(T/(C*(2^b))-1)，结果不一定是整数。
    double tmp = 16 * (seconds / (POLSR_C * (1 << b)) - 1);

    // 对结果四舍五入，得到 a 的取值。
    a = (int)std::ceil(tmp - 0.5);

    // 如果 a 等于 16，则 b 加 1，并把 a 置为 0。
    if (a == 16)
    {
        b += 1;
        a = 0;
    }

    // 此时 a 和 b 都应该是 0 到 15 之间的整数。
    NS_ASSERT(a >= 0 && a < 16);
    NS_ASSERT(b >= 0 && b < 16);

    // 字段最终用一个字节保存 a*16+b。
    return (uint8_t)((a << 4) | b);
}

///
/// @brief 将尾数/指数格式的秒数转换回十进制秒数。
///
/// @param polsrFormat 尾数/指数格式编码后的秒数。
/// @return 十进制秒数。
///
double
EmfToSeconds(uint8_t polsrFormat)
{
    int a = (polsrFormat >> 4);
    int b = (polsrFormat & 0xf);
    // value = C*(1+a/16)*2^b，单位为秒。
    return POLSR_C * (1 + a / 16.0) * (1 << b);
}

// ---------------- POLSR packet -------------------------------

NS_OBJECT_ENSURE_REGISTERED(PacketHeader);

PacketHeader::PacketHeader()
{
}

PacketHeader::~PacketHeader()
{
}

TypeId
PacketHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::polsr::PacketHeader")
                            .SetParent<Header>()
                            .SetGroupName("Polsr")
                            .AddConstructor<PacketHeader>();
    return tid;
}

TypeId
PacketHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
PacketHeader::GetSerializedSize() const
{
    return POLSR_PKT_HEADER_SIZE;
}

void
PacketHeader::Print(std::ostream& os) const
{
    os << "len: " << m_packetLength << " seqNo: " << m_packetSequenceNumber;
}

void
PacketHeader::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;
    i.WriteHtonU16(m_packetLength);
    i.WriteHtonU16(m_packetSequenceNumber);
}

uint32_t
PacketHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    m_packetLength = i.ReadNtohU16();
    m_packetSequenceNumber = i.ReadNtohU16();
    return GetSerializedSize();
}

// ---------------- POLSR message -------------------------------

NS_OBJECT_ENSURE_REGISTERED(MessageHeader);

MessageHeader::MessageHeader()
    : m_messageType(MessageHeader::MessageType(0))
{
}

MessageHeader::~MessageHeader()
{
}

TypeId
MessageHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::polsr::MessageHeader")
                            .SetParent<Header>()
                            .SetGroupName("Polsr")
                            .AddConstructor<MessageHeader>();
    return tid;
}

TypeId
MessageHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
MessageHeader::GetSerializedSize() const
{
    uint32_t size = POLSR_MSG_HEADER_SIZE;
    switch (m_messageType)
    {
    case MID_MESSAGE:
        size += m_message.mid.GetSerializedSize();
        break;
    case HELLO_MESSAGE:
        NS_LOG_DEBUG("Hello Message Size: " << size << " + "
                                            << m_message.hello.GetSerializedSize());
        size += m_message.hello.GetSerializedSize();
        break;
    case TC_MESSAGE:
        size += m_message.tc.GetSerializedSize();
        break;
    case HNA_MESSAGE:
        size += m_message.hna.GetSerializedSize();
        break;
    default:
        NS_ASSERT(false);
    }
    return size;
}

void
MessageHeader::Print(std::ostream& os) const
{
    switch (m_messageType)
    {
    case HELLO_MESSAGE:
        os << "type: HELLO";
        break;
    case TC_MESSAGE:
        os << "type: TC";
        break;
    case MID_MESSAGE:
        os << "type: MID";
        break;
    case HNA_MESSAGE:
        os << "type: HNA";
        break;
    }

    os << " TTL: " << +m_timeToLive;
    os << " Orig: " << m_originatorAddress;
    os << " SeqNo: " << m_messageSequenceNumber;
    os << " Validity: " << +m_vTime;
    os << " Hop count: " << +m_hopCount;
    os << " Size: " << m_messageSize;

    switch (m_messageType)
    {
    case MID_MESSAGE:
        m_message.mid.Print(os);
        break;
    case HELLO_MESSAGE:
        m_message.hello.Print(os);
        break;
    case TC_MESSAGE:
        m_message.tc.Print(os);
        break;
    case HNA_MESSAGE:
        m_message.hna.Print(os);
        break;
    default:
        NS_ASSERT(false);
    }
}

void
MessageHeader::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;
    i.WriteU8(m_messageType);
    i.WriteU8(m_vTime);
    i.WriteHtonU16(GetSerializedSize());
    i.WriteHtonU32(m_originatorAddress.Get());
    i.WriteU8(m_timeToLive);
    i.WriteU8(m_hopCount);
    i.WriteHtonU16(m_messageSequenceNumber);

    switch (m_messageType)
    {
    case MID_MESSAGE:
        m_message.mid.Serialize(i);
        break;
    case HELLO_MESSAGE:
        m_message.hello.Serialize(i);
        break;
    case TC_MESSAGE:
        m_message.tc.Serialize(i);
        break;
    case HNA_MESSAGE:
        m_message.hna.Serialize(i);
        break;
    default:
        NS_ASSERT(false);
    }
}

uint32_t
MessageHeader::Deserialize(Buffer::Iterator start)
{
    uint32_t size;
    Buffer::Iterator i = start;
    m_messageType = (MessageType)i.ReadU8();
    NS_ASSERT(m_messageType >= HELLO_MESSAGE && m_messageType <= HNA_MESSAGE);
    m_vTime = i.ReadU8();
    m_messageSize = i.ReadNtohU16();
    m_originatorAddress = Ipv4Address(i.ReadNtohU32());
    m_timeToLive = i.ReadU8();
    m_hopCount = i.ReadU8();
    m_messageSequenceNumber = i.ReadNtohU16();
    size = POLSR_MSG_HEADER_SIZE;
    switch (m_messageType)
    {
    case MID_MESSAGE:
        size += m_message.mid.Deserialize(i, m_messageSize - POLSR_MSG_HEADER_SIZE);
        break;
    case HELLO_MESSAGE:
        size += m_message.hello.Deserialize(i, m_messageSize - POLSR_MSG_HEADER_SIZE);
        break;
    case TC_MESSAGE:
        size += m_message.tc.Deserialize(i, m_messageSize - POLSR_MSG_HEADER_SIZE);
        break;
    case HNA_MESSAGE:
        size += m_message.hna.Deserialize(i, m_messageSize - POLSR_MSG_HEADER_SIZE);
        break;
    default:
        NS_ASSERT(false);
    }
    return size;
}

// ---------------- POLSR MID 消息 -------------------------------

uint32_t
MessageHeader::Mid::GetSerializedSize() const
{
    return this->interfaceAddresses.size() * IPV4_ADDRESS_SIZE;
}

void
MessageHeader::Mid::Print(std::ostream& os) const
{
    bool first = true;
    os << " [";
    for (const auto& iAddr : interfaceAddresses)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            os << ", ";
        }
        os << iAddr;
    }
    os << "]";
}

void
MessageHeader::Mid::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;

    for (auto iter = this->interfaceAddresses.begin(); iter != this->interfaceAddresses.end();
         iter++)
    {
        i.WriteHtonU32(iter->Get());
    }
}

uint32_t
MessageHeader::Mid::Deserialize(Buffer::Iterator start, uint32_t messageSize)
{
    Buffer::Iterator i = start;

    this->interfaceAddresses.clear();
    NS_ASSERT(messageSize % IPV4_ADDRESS_SIZE == 0);

    int numAddresses = messageSize / IPV4_ADDRESS_SIZE;
    this->interfaceAddresses.erase(this->interfaceAddresses.begin(),
                                   this->interfaceAddresses.end());
    for (int n = 0; n < numAddresses; ++n)
    {
        this->interfaceAddresses.emplace_back(i.ReadNtohU32());
    }
    return GetSerializedSize();
}

// ---------------- POLSR HELLO 消息 -------------------------------

uint32_t
MessageHeader::Hello::GetSerializedSize() const
{
    uint32_t size = 4 + (this->hasGps ? POLSR_HELLO_GPS_SIZE : 0);
    for (auto iter = this->linkMessages.begin(); iter != this->linkMessages.end(); iter++)
    {
        const LinkMessage& lm = *iter;
        size += 4;
        size += (this->hasGps ? POLSR_TC_NEIGHBOR_INFO_SIZE : IPV4_ADDRESS_SIZE) *
                lm.neighborInterfaceAddresses.size();
    }
    return size;
}

void
MessageHeader::Hello::Print(std::ostream& os) const
{
    os << " Interval: " << +hTime << " (" << EmfToSeconds(hTime) << "s)";
    os << " Willingness: " << willingness;
    if (hasGps)
    {
        os << " GPS(longitude=" << longitude << ", latitude=" << latitude
           << ", altitude=" << altitude << ")";
    }

    for (const auto& ilinkMessage : linkMessages)
    {
        const LinkMessage& lm = ilinkMessage;
        os << " Link code: " << +(lm.linkCode);
        os << " [";
        bool first = true;
        for (const auto& neigh_iter : lm.neighborInterfaceAddresses)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                os << ", ";
            }
            os << neigh_iter;
            for (const auto& quality : lm.neighborLinkQualities)
            {
                if (quality.address == neigh_iter && quality.qualityValid)
                {
                    os << "(phi=" << quality.forwardReceivingRatio
                       << ", rho=" << quality.reverseReceivingRatio
                       << ", speed=" << quality.relativeSpeed << ")";
                    break;
                }
            }
        }
        os << "]";
    }
}

void
MessageHeader::Hello::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;

    i.WriteU16(0); // 保留字段
    i.WriteU8(this->hTime);
    i.WriteU8(static_cast<uint8_t>(this->willingness));
    /*
     * P-OLSR HELLO 扩展布局：
     *   magic(2) + flags(1) + 保留字段(1)
     *   + longitude(float32) + latitude(float32)
     *   + altitude(缩放后的 int16) + 保留字段(2)
     *
     * 仅当 hasGps=true 时写入。预测关闭时，HELLO 仍保持原始 OLSR 大小。
     */
    if (this->hasGps)
    {
        i.WriteHtonU16(POLSR_HELLO_GPS_MAGIC);
        i.WriteU8(1);
        i.WriteU8(0); // 保留字段
        i.WriteHtonU32(EncodeFloat32(this->longitude));
        i.WriteHtonU32(EncodeFloat32(this->latitude));
        i.WriteHtonU16(EncodeSignedScaled16(this->altitude, 10.0));
        i.WriteHtonU16(0);
    }

    for (auto iter = this->linkMessages.begin(); iter != this->linkMessages.end(); iter++)
    {
        const LinkMessage& lm = *iter;

        i.WriteU8(lm.linkCode);
        i.WriteU8(0); // 保留字段

        // Link Message Size 以字节为单位，从当前 Link Code 字段开始，
        // 一直计到下一个 Link Code 字段之前。
        // 如果后面没有更多链路类型，则计到当前消息结尾。
        uint32_t neighborInfoSize = this->hasGps ? POLSR_TC_NEIGHBOR_INFO_SIZE : IPV4_ADDRESS_SIZE;
        i.WriteHtonU16(4 + lm.neighborInterfaceAddresses.size() * neighborInfoSize);

        for (auto neigh_iter = lm.neighborInterfaceAddresses.begin();
             neigh_iter != lm.neighborInterfaceAddresses.end();
             neigh_iter++)
        {
            i.WriteHtonU32(neigh_iter->Get());
            if (!this->hasGps)
            {
                continue;
            }

            double phi = 1.0;
            double rho = 1.0;
            double relativeSpeed = 0.0;
            for (const auto& quality : lm.neighborLinkQualities)
            {
                if (quality.address == *neigh_iter && quality.qualityValid)
                {
                    phi = quality.forwardReceivingRatio;
                    rho = quality.reverseReceivingRatio;
                    relativeSpeed = quality.relativeSpeed;
                    break;
                }
            }
            i.WriteU8(EncodeRatio(phi));
            i.WriteU8(EncodeRatio(rho));
            i.WriteHtonU16(EncodeSignedScaled16(relativeSpeed, 10.0));
        }
    }
}

uint32_t
MessageHeader::Hello::Deserialize(Buffer::Iterator start, uint32_t messageSize)
{
    Buffer::Iterator i = start;

    NS_ASSERT(messageSize >= 4);

    this->linkMessages.clear();

    uint16_t helloSizeLeft = messageSize;

    i.ReadNtohU16(); // 保留字段
    this->hTime = i.ReadU8();
    this->willingness = Willingness(i.ReadU8());

    helloSizeLeft -= 4;
    this->hasGps = false;
    this->longitude = 0.0;
    this->latitude = 0.0;
    this->altitude = 0.0;

    if (helloSizeLeft >= POLSR_HELLO_GPS_SIZE)
    {
        Buffer::Iterator gpsIterator = i;
        uint16_t gpsMagic = gpsIterator.ReadNtohU16();
        if (gpsMagic == POLSR_HELLO_GPS_MAGIC)
        {
            /*
             * 如果存在扩展标记，先读取并解析 P-OLSR GPS 扩展块，
             * 再解析普通 HELLO 的链路消息列表。
             * 如果没有扩展标记，则继续按标准 OLSR HELLO 消息体解析剩余字节。
             */
            i.ReadNtohU16();
            uint8_t flags = i.ReadU8();
            i.ReadU8(); // 保留字段
            this->hasGps = ((flags & 0x1) != 0);
            this->longitude = DecodeFloat32(i.ReadNtohU32());
            this->latitude = DecodeFloat32(i.ReadNtohU32());
            this->altitude = DecodeSignedScaled16(i.ReadNtohU16(), 10.0);
            i.ReadNtohU16(); // 保留字段
            helloSizeLeft -= POLSR_HELLO_GPS_SIZE;
        }
    }

    while (helloSizeLeft)
    {
        LinkMessage lm;
        NS_ASSERT(helloSizeLeft >= 4);
        lm.linkCode = i.ReadU8();
        i.ReadU8(); // 保留字段
        uint16_t lmSize = i.ReadNtohU16();
        uint32_t neighborInfoSize = this->hasGps ? POLSR_TC_NEIGHBOR_INFO_SIZE : IPV4_ADDRESS_SIZE;
        NS_ASSERT((lmSize - 4) % neighborInfoSize == 0);
        for (int n = (lmSize - 4) / neighborInfoSize; n; --n)
        {
            Ipv4Address address(i.ReadNtohU32());
            lm.neighborInterfaceAddresses.emplace_back(address);
            if (this->hasGps)
            {
                double phi = DecodeRatio(i.ReadU8());
                double rho = DecodeRatio(i.ReadU8());
                double relativeSpeed = DecodeSignedScaled16(i.ReadNtohU16(), 10.0);
                lm.neighborLinkQualities.push_back(
                    LinkMessage::NeighborLinkQuality{address, phi, rho, relativeSpeed, true});
            }
        }
        helloSizeLeft -= lmSize;
        this->linkMessages.push_back(lm);
    }

    return messageSize;
}

// ---------------- POLSR TC 消息 -------------------------------

uint32_t
MessageHeader::Tc::GetSerializedSize() const
{
    for (const auto& prediction : this->neighborPredictions)
    {
        if (prediction.predictionValid)
        {
            return 4 + this->neighborAddresses.size() * POLSR_TC_NEIGHBOR_INFO_SIZE;
        }
    }
    return 4 + this->neighborAddresses.size() * IPV4_ADDRESS_SIZE;
}

void
MessageHeader::Tc::Print(std::ostream& os) const
{
    os << " Adv. SeqNo: " << ansn;
    os << " [";
    bool first = true;
    for (const auto& iAddr : neighborAddresses)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            os << ", ";
        }
        os << iAddr;
        for (const auto& prediction : neighborPredictions)
        {
            if (prediction.address == iAddr && prediction.predictionValid)
            {
                os << "(cost=" << prediction.predictedCost
                   << ", phi=" << prediction.forwardReceivingRatio
                   << ", rho=" << prediction.reverseReceivingRatio
                   << ", speed=" << prediction.relativeSpeed << ")";
                break;
            }
        }
    }
    os << "]";
}

void
MessageHeader::Tc::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;

    i.WriteHtonU16(this->ansn);
    /*
     * P-OLSR TC 扩展布局：
     *   保留字段/magic(2)
     *   重复项：neighbor address(4) + speed-weighted ETX(4)
     *          + phi(1) + rho(1) + relative speed(2)
     *
     * 如果没有可用预测字段，保留字段仍为 0，
     * 每个邻居会完全按原始 OLSR TC 格式序列化：只写一个 IPv4 地址。
     */
    bool hasPrediction = false;
    for (const auto& prediction : this->neighborPredictions)
    {
        if (prediction.predictionValid)
        {
            hasPrediction = true;
            break;
        }
    }
    i.WriteHtonU16(hasPrediction ? POLSR_TC_PREDICTION_MAGIC : 0);

    for (auto iter = this->neighborAddresses.begin(); iter != this->neighborAddresses.end(); iter++)
    {
        i.WriteHtonU32(iter->Get());
        if (!hasPrediction)
        {
            continue;
        }
        double predictedCost = 1.0;
        double phi = 1.0;
        double rho = 1.0;
        double relativeSpeed = 0.0;
        for (const auto& prediction : this->neighborPredictions)
        {
            if (prediction.address == *iter && prediction.predictionValid)
            {
                predictedCost = prediction.predictedCost;
                phi = prediction.forwardReceivingRatio;
                rho = prediction.reverseReceivingRatio;
                relativeSpeed = prediction.relativeSpeed;
                break;
            }
        }
        i.WriteHtonU32(EncodeUnsignedScaled(predictedCost, 1000.0));
        i.WriteU8(EncodeRatio(phi));
        i.WriteU8(EncodeRatio(rho));
        i.WriteHtonU16(EncodeSignedScaled16(relativeSpeed, 10.0));
    }
}

uint32_t
MessageHeader::Tc::Deserialize(Buffer::Iterator start, uint32_t messageSize)
{
    Buffer::Iterator i = start;

    this->neighborAddresses.clear();
    this->neighborPredictions.clear();
    NS_ASSERT(messageSize >= 4);

    this->ansn = i.ReadNtohU16();
    uint16_t reserved = i.ReadNtohU16(); // 保留字段或 P-OLSR magic。

    if (reserved == POLSR_TC_PREDICTION_MAGIC &&
        (messageSize - 4) % POLSR_TC_NEIGHBOR_INFO_SIZE == 0)
    {
        /*
         * P-OLSR 扩展 TC：每个 advertised neighbor 后面跟随它的
         * speed-weighted ETX、接收率和相对速度。
         * 这些值后续会由 ProcessTc() 复制到 TopologyTuple。
         */
        int numAddresses = (messageSize - 4) / POLSR_TC_NEIGHBOR_INFO_SIZE;
        for (int n = 0; n < numAddresses; ++n)
        {
            Ipv4Address address(i.ReadNtohU32());
            double predictedCost = DecodeUnsignedScaled(i.ReadNtohU32(), 1000.0);
            double phi = DecodeRatio(i.ReadU8());
            double rho = DecodeRatio(i.ReadU8());
            double relativeSpeed = DecodeSignedScaled16(i.ReadNtohU16(), 10.0);
            this->neighborAddresses.emplace_back(address);
            this->neighborPredictions.push_back(
                NeighborPrediction{address, predictedCost, phi, rho, relativeSpeed, true});
        }
    }
    else
    {
        /*
         * 标准 OLSR TC 回退路径。
         * 这样反序列化逻辑可以兼容不携带 P-OLSR 预测字段的 TC 消息体。
         */
        NS_ASSERT((messageSize - 4) % IPV4_ADDRESS_SIZE == 0);
        int numAddresses = (messageSize - 4) / IPV4_ADDRESS_SIZE;
        for (int n = 0; n < numAddresses; ++n)
        {
            Ipv4Address address(i.ReadNtohU32());
            this->neighborAddresses.emplace_back(address);
            this->neighborPredictions.push_back(NeighborPrediction{address, 1.0, 1.0, 1.0, 0.0, false});
        }
    }

    return messageSize;
}

// ---------------- POLSR HNA 消息 -------------------------------

uint32_t
MessageHeader::Hna::GetSerializedSize() const
{
    return 2 * this->associations.size() * IPV4_ADDRESS_SIZE;
}

void
MessageHeader::Hna::Print(std::ostream& os) const
{
    os << " [";
    bool first = true;
    for (const auto& iAssoc : associations)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            os << ", ";
        }
        os << iAssoc.address << "/" << iAssoc.mask.GetPrefixLength();
    }
    os << "]";
}

void
MessageHeader::Hna::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;

    for (size_t n = 0; n < this->associations.size(); ++n)
    {
        i.WriteHtonU32(this->associations[n].address.Get());
        i.WriteHtonU32(this->associations[n].mask.Get());
    }
}

uint32_t
MessageHeader::Hna::Deserialize(Buffer::Iterator start, uint32_t messageSize)
{
    Buffer::Iterator i = start;

    NS_ASSERT(messageSize % (IPV4_ADDRESS_SIZE * 2) == 0);
    int numAddresses = messageSize / IPV4_ADDRESS_SIZE / 2;
    this->associations.clear();
    for (int n = 0; n < numAddresses; ++n)
    {
        Ipv4Address address(i.ReadNtohU32());
        Ipv4Mask mask(i.ReadNtohU32());
        this->associations.push_back(Association{address, mask});
    }
    return messageSize;
}

} // 命名空间 polsr
} // 命名空间 ns3
