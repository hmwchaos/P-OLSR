/*
 * Copyright (c) 2007 INESC Porto
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Gustavo J. A. M. Carneiro  <gjc@inescporto.pt>
 */

#ifndef POLSR_HEADER_H
#define POLSR_HEADER_H

#include "polsr-repositories.h"

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"

#include <stdint.h>
#include <vector>

namespace ns3
{
namespace polsr
{

double EmfToSeconds(uint8_t emf);
uint8_t SecondsToEmf(double seconds);

/**
 * @ingroup polsr
 *
 * 任意 POLSR packet 的基本布局如下（省略 IP 和 UDP 头）：
  \verbatim
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |         Packet Length         |    Packet Sequence Number     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Message Type |     Vtime     |         Message Size          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Originator Address                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Time To Live |   Hop Count   |    Message Sequence Number    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               |
   :                            MESSAGE                            :
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Message Type |     Vtime     |         Message Size          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Originator Address                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Time To Live |   Hop Count   |    Message Sequence Number    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               |
   :                            MESSAGE                            :
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   :                                                               :
            (etc.)
  \endverbatim
  *
  * 学习提示：PacketHeader 是整个 POLSR packet 的“外层包头”，
  * 这里只保存一组消息共有的部分，也就是最前面的 4 个字节。
  * 一个 POLSR packet 后面可以继续携带多条 MessageHeader。
  */
class PacketHeader : public Header
{
  public:
    PacketHeader();
    ~PacketHeader() override;

    /**
     * 设置 packet 总长度。
     * @param length packet 长度。
     */
    void SetPacketLength(uint16_t length)
    {
        m_packetLength = length;
    }

    /**
     * 获取 packet 总长度。
     * @return packet 长度。
     */
    uint16_t GetPacketLength() const
    {
        return m_packetLength;
    }

    /**
     * 设置 packet 序列号。
     * @param seqnum packet 序列号。
     */
    void SetPacketSequenceNumber(uint16_t seqnum)
    {
        m_packetSequenceNumber = seqnum;
    }

    /**
     * 获取 packet 序列号。
     * @returns packet 序列号。
     */
    uint16_t GetPacketSequenceNumber() const
    {
        return m_packetSequenceNumber;
    }

  private:
    uint16_t m_packetLength;         //!< packet 长度。
    uint16_t m_packetSequenceNumber; //!< packet 序列号。

  public:
    /**
     * @brief 获取 TypeId。
     * @return 当前对象的 TypeId。
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
};

/**
 * @ingroup polsr
 *
 * 该头部可以保存 HELLO、TC、MID 和 HNA 消息。
 * 头部大小是可变的，取决于实际的消息类型。
 *
 * 学习提示：MessageHeader 是“每一条 POLSR 控制消息的通用外壳”。
 * 不同类型消息共用消息类型、有效时间、源地址、TTL、跳数和消息序列号，
 * 但真正的消息体会因 HELLO / TC / MID / HNA 而不同。
 *
  \verbatim
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Message Type |     Vtime     |         Message Size          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Originator Address                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  Time To Live |   Hop Count   |    Message Sequence Number    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               |
   :                            MESSAGE                            :
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  \endverbatim
 */
class MessageHeader : public Header
{
  public:
    /**
     * 消息类型。
     */
    enum MessageType
    {
        HELLO_MESSAGE = 1,
        TC_MESSAGE = 2,
        MID_MESSAGE = 3,
        HNA_MESSAGE = 4,
    };

    MessageHeader();
    ~MessageHeader() override;

    /**
     * 设置消息类型。
     * @param messageType 消息类型。
     */
    void SetMessageType(MessageType messageType)
    {
        m_messageType = messageType;
    }

    /**
     * 获取消息类型。
     * @return 消息类型。
     */
    MessageType GetMessageType() const
    {
        return m_messageType;
    }

    /**
     * 设置消息有效时间。
     * @param time 有效时间。
     */
    void SetVTime(Time time)
    {
        m_vTime = SecondsToEmf(time.GetSeconds());
    }

    /**
     * 获取消息有效时间。
     * @return 有效时间。
     */
    Time GetVTime() const
    {
        return Seconds(EmfToSeconds(m_vTime));
    }

    /**
     * 设置源发起者地址。
     * @param originatorAddress 源发起者地址。
     */
    void SetOriginatorAddress(Ipv4Address originatorAddress)
    {
        m_originatorAddress = originatorAddress;
    }

    /**
     * 获取源发起者地址。
     * @return 源发起者地址。
     */
    Ipv4Address GetOriginatorAddress() const
    {
        return m_originatorAddress;
    }

    /**
     * 设置生存时间 TTL。
     * @param timeToLive 生存时间 TTL。
     */
    void SetTimeToLive(uint8_t timeToLive)
    {
        m_timeToLive = timeToLive;
    }

    /**
     * 获取生存时间 TTL。
     * @return 生存时间 TTL。
     */
    uint8_t GetTimeToLive() const
    {
        return m_timeToLive;
    }

    /**
     * 设置跳数计数。
     * @param hopCount 跳数计数。
     */
    void SetHopCount(uint8_t hopCount)
    {
        m_hopCount = hopCount;
    }

    /**
     * 获取跳数计数。
     * @return 跳数计数。
     */
    uint8_t GetHopCount() const
    {
        return m_hopCount;
    }

    /**
     * 设置消息序列号。
     * @param messageSequenceNumber 消息序列号。
     */
    void SetMessageSequenceNumber(uint16_t messageSequenceNumber)
    {
        m_messageSequenceNumber = messageSequenceNumber;
    }

    /**
     * 获取消息序列号。
     * @return 消息序列号。
     */
    uint16_t GetMessageSequenceNumber() const
    {
        return m_messageSequenceNumber;
    }

  private:
    MessageType m_messageType;        //!< 消息类型。
    uint8_t m_vTime;                  //!< 有效时间。
    Ipv4Address m_originatorAddress;  //!< 源发起者地址。
    uint8_t m_timeToLive;             //!< 生存时间 TTL。
    uint8_t m_hopCount;               //!< 跳数计数。
    uint16_t m_messageSequenceNumber; //!< 消息序列号。
    uint16_t m_messageSize;           //!< 消息大小。

  public:
    /**
     * @brief 获取 TypeId。
     * @return 当前对象的 TypeId。
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    /**
     * @ingroup polsr
     * MID 消息格式。
     *
     * 学习提示：MID 用来说明“同一个节点还拥有哪些其他接口地址”，
     * 接收方据此维护 Iface Association Set，把多个接口地址映射回同一个主地址。
     *
    \verbatim
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                    POLSR Interface Address                     |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                    POLSR Interface Address                     |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                              ...               Deserialize()               |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    \endverbatim
    */
    struct Mid
    {
        std::vector<Ipv4Address> interfaceAddresses; //!< 接口地址容器。
        /**
         * 打印 MID 消息内容。
         * @param os 输出流。
         */
        void Print(std::ostream& os) const;
        /**
         * 返回头部预期大小。
         * @returns 头部预期大小。
         */
        uint32_t GetSerializedSize() const;
        /**
         * 供 Packet::AddHeader 调用，
         * 将头部写入 packet 的字节缓冲区。
         *
         * @param start 指向头部应写入位置的迭代器。
         */
        void Serialize(Buffer::Iterator start) const;
        /**
         * 供 Packet::RemoveHeader 调用，
         * 从 packet 的字节缓冲区重建头部。
         *
         * @param start 指向头部读取起点的迭代器。
         * @param messageSize 消息大小。
         * @returns 读取的字节数。
         */
        uint32_t Deserialize(Buffer::Iterator start, uint32_t messageSize);
    };

    /**
     * @ingroup polsr
     * HELLO 消息格式。
     *
     * 学习提示：HELLO 是“本地邻居发现”消息，不会被全网泛洪。
     * 它携带本节点对链路和邻居的观察结果，接收方据此更新
     * Link Set、Neighbor Set、Two-hop Neighbor Set 和 MPR Selector Set。
     *
    \verbatim
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1

     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |          Reserved             |     Htime     |  Willingness  |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |   Link Code   |   Reserved    |       Link Message Size       |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                  Neighbor Interface Address                   |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                  Neighbor Interface Address                   |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     :                             .  .  .                           :
     :                                                               :
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |   Link Code   |   Reserved    |       Link Message Size       |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                  Neighbor Interface Address                   |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                  Neighbor Interface Address                   |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     :                                                               :
       (etc.)
    \endverbatim
    */
    struct Hello
    {
        /**
         * 一组链路消息项。
         */
        struct LinkMessage
        {
            struct NeighborLinkQuality
            {
                Ipv4Address address;               //!< 邻居接口地址。
                double forwardReceivingRatio{1.0}; //!< P-OLSR/ETX: 正向接收率 phi。
                double reverseReceivingRatio{1.0}; //!< P-OLSR/ETX: 反向接收率 rho。
                double relativeSpeed{0.0};         //!< P-OLSR: 平滑后的相对速度。
                bool qualityValid{false};          //!< 是否携带有效链路质量字段。
            };

            uint8_t linkCode; //!< 链路代码。
            std::vector<Ipv4Address>
                neighborInterfaceAddresses; //!< 邻居接口地址容器。
            std::vector<NeighborLinkQuality>
                neighborLinkQualities; //!< P-OLSR: 与邻居接口地址对应的链路质量。
        };

        uint8_t hTime; //!< HELLO 发送间隔（编码后）。

        /**
         * 设置 HELLO 发送间隔。
         * @param time HELLO 发送间隔。
         */
        void SetHTime(Time time)
        {
            this->hTime = SecondsToEmf(time.GetSeconds());
        }

        /**
         * 获取 HELLO 发送间隔。
         * @return HELLO 发送间隔。
         */
        Time GetHTime() const
        {
            return Seconds(EmfToSeconds(this->hTime));
        }

        Willingness willingness; //!< 节点替其他节点承载并转发流量的意愿值。
        /*
         * P-OLSR HELLO 扩展：
         *
         * 原始 HELLO 只描述邻居关系，不携带节点坐标。
         * 论文中的 P-OLSRd 在 HELLO 中携带 longitude / latitude / altitude，
         * 并在每个邻居项中携带链路质量与相对速度，用于 speed-weighted ETX。
         *
         * hasGps=false 时仍按原始 HELLO 结构发送，不增加额外字节；
         * hasGps=true 时才在 HELLO 固定字段后追加 GPS 和链路质量字段。
         */
        bool hasGps{false};      //!< P-OLSR: HELLO 是否携带 GPS 坐标和链路质量。
        double longitude{0.0};   //!< P-OLSR: 节点经度。
        double latitude{0.0};    //!< P-OLSR: 节点纬度。
        double altitude{0.0};    //!< P-OLSR: 节点高度。
        std::vector<LinkMessage> linkMessages; //!< 链路消息容器。

        /**
         * 打印 HELLO 消息内容。
         * @param os 输出流。
         */
        void Print(std::ostream& os) const;
        /**
         * 返回头部预期大小。
         * @returns 头部预期大小。
         */
        uint32_t GetSerializedSize() const;
        /**
         * 供 Packet::AddHeader 调用，
         * 将头部写入 packet 的字节缓冲区。
         *
         * @param start 指向头部应写入位置的迭代器。
         */
        void Serialize(Buffer::Iterator start) const;
        /**
         * 供 Packet::RemoveHeader 调用，
         * 从 packet 的字节缓冲区重建头部。
         *
         * @param start 指向头部读取起点的迭代器。
         * @param messageSize 消息大小。
         * @returns 读取的字节数。
         */
        uint32_t Deserialize(Buffer::Iterator start, uint32_t messageSize);
    };

    /**
     * @ingroup polsr
     * TC 消息格式。
     * 
     *
     * 学习提示：TC 用来传播拓扑信息。
     * 节点把自己要通告的邻居主地址放进 TC，接收方据此更新 Topology Set。
     *
     \verbatim
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |              ANSN             |           Reserved            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |               Advertised Neighbor Main Address                |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |               Advertised Neighbor Main Address                |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              ...                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     \endverbatim
     */
    struct Tc
    {
        struct NeighborPrediction
        {
            /*
             * P-OLSR TC 扩展：
             *
             * 原始 TC 只通告 advertised neighbor main address。
             * 这里为每个 advertised neighbor 可选增加一组预测指标，
             * 让接收端更新 TopologyTuple 时能保存带权拓扑边。
             */
            Ipv4Address address;                 //!< 被通告邻居主地址。
            double predictedCost{1.0};          //!< P-OLSR: speed-weighted ETX 代价。
            double forwardReceivingRatio{1.0};  //!< P-OLSR/ETX: 正向接收率 phi。
            double reverseReceivingRatio{1.0};  //!< P-OLSR/ETX: 反向接收率 rho。
            double relativeSpeed{0.0};          //!< P-OLSR: 平滑后的相对速度。
            bool predictionValid{false};        //!< P-OLSR: 是否有可用预测值。
        };

        std::vector<Ipv4Address> neighborAddresses; //!< 邻居地址容器。
        std::vector<NeighborPrediction>
            neighborPredictions; //!< P-OLSR: 与邻居地址对应的预测指标。
        uint16_t ansn;                              //!< Advertised Neighbor Sequence Number。

        /**
         * 打印 TC 消息内容。
         * @param os 输出流。
         */
        void Print(std::ostream& os) const;
        /**
         * 返回头部预期大小。
         * @returns 头部预期大小。
         */
        uint32_t GetSerializedSize() const;
        /**
         * 供 Packet::AddHeader 调用，
         * 将头部写入 packet 的字节缓冲区。
         *
         * @param start 指向头部应写入位置的迭代器。
         */
        void Serialize(Buffer::Iterator start) const;
        /**
         * 供 Packet::RemoveHeader 调用，
         * 从 packet 的字节缓冲区重建头部。
         *
         * @param start 指向头部读取起点的迭代器。
         * @param messageSize 消息大小。
         * @returns 读取的字节数。
         */
        uint32_t Deserialize(Buffer::Iterator start, uint32_t messageSize);
    };

    /**
     * @ingroup polsr
     * HNA（Host Network Association）消息格式。
     *
     * 学习提示：HNA 用来通告“本节点还能代表哪些外部网络”。
     * 接收方据此更新 Association Set，从而知道哪个网关可以到达哪个外部网段。
     *
     \verbatim
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         Network Address                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                             Netmask                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         Network Address                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                             Netmask                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              ...                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     \endverbatim
     */
    struct Hna
    {
        /**
         * 关联项结构。
         */
        struct Association
        {
            Ipv4Address address; //!< IPv4 地址。
            Ipv4Mask mask;       //!< IPv4 网络掩码。
        };

        std::vector<Association> associations; //!< 关联项容器。

        /**
         * 打印 HNA 消息内容。
         * @param os 输出流。
         */
        void Print(std::ostream& os) const;
        /**
         * 返回头部预期大小。
         * @returns 头部预期大小。
         */
        uint32_t GetSerializedSize() const;
        /**
         * 供 Packet::AddHeader 调用，
         * 将头部写入 packet 的字节缓冲区。
         *
         * @param start 指向头部应写入位置的迭代器。
         */
        void Serialize(Buffer::Iterator start) const;
        /**
         * 供 Packet::RemoveHeader 调用，
         * 从 packet 的字节缓冲区重建头部。
         *
         * @param start 指向头部读取起点的迭代器。
         * @param messageSize 消息大小。
         * @returns 读取的字节数。
         */
        uint32_t Deserialize(Buffer::Iterator start, uint32_t messageSize);
    };

  private:
    /**
     * 保存具体消息内容的结构。
     *
     * 学习提示：同一个 MessageHeader 在某一时刻m_message只承载一种实际消息体。
     * `m_messageType` 决定当前应读取 `mid`、`hello`、`tc` 还是 `hna`。
     */
    struct
    {
        Mid mid;     //!< MID 消息（可选）。
        Hello hello; //!< HELLO 消息（可选）。
        Tc tc;       //!< TC 消息（可选）。
        Hna hna;     //!< HNA 消息（可选）。
    } m_message;     //!< 当前实际承载的消息内容。

  public:
    /**
     * 将消息类型设为 MID，并返回对应消息内容。
     * @returns MID 消息。
     */
    Mid& GetMid()
    {
        if (m_messageType == 0)
        {
            m_messageType = MID_MESSAGE;
        }
        else
        {
            NS_ASSERT(m_messageType == MID_MESSAGE);
        }
        return m_message.mid;
    }

    /**
     * 将消息类型设为 HELLO，并返回对应消息内容。
     * @returns HELLO 消息。
     * & 表示引用,它返回的不是 Hello 的拷贝，而是返回对象内部那个真正的 Hello.
     * Deserialize() 会把 HELLO 包里的字节解析到 m_message.hello 这个结构体里；GetHello() 只是把这个结构体拿出来给 ProcessHello() 使用。
     */
    Hello& GetHello()
    {
        if (m_messageType == 0)
        {
            m_messageType = HELLO_MESSAGE;
        }
        else
        {
            NS_ASSERT(m_messageType == HELLO_MESSAGE);
        }
        return m_message.hello;
    }

    /**
     * 将消息类型设为 TC，并返回对应消息内容。
     * @returns TC 消息。
     */
    Tc& GetTc()
    {
        if (m_messageType == 0)
        {
            m_messageType = TC_MESSAGE;
        }
        else
        {
            NS_ASSERT(m_messageType == TC_MESSAGE);
        }
        return m_message.tc;
    }

    /**
     * 将消息类型设为 HNA，并返回对应消息内容。
     * @returns HNA 消息。
     */
    Hna& GetHna()
    {
        if (m_messageType == 0)
        {
            m_messageType = HNA_MESSAGE;
        }
        else
        {
            NS_ASSERT(m_messageType == HNA_MESSAGE);
        }
        return m_message.hna;
    }

    /**
     * 获取 MID 消息。
     * @returns MID 消息。
     */
    const Mid& GetMid() const
    {
        NS_ASSERT(m_messageType == MID_MESSAGE);
        return m_message.mid;
    }

    /**
     * 获取 HELLO 消息。
     * @returns HELLO 消息。
     */
    const Hello& GetHello() const
    {
        NS_ASSERT(m_messageType == HELLO_MESSAGE);
        return m_message.hello;
    }

    /**
     * 获取 TC 消息。
     * @returns TC 消息。
     */
    const Tc& GetTc() const
    {
        NS_ASSERT(m_messageType == TC_MESSAGE);
        return m_message.tc;
    }

    /**
     * 获取 HNA 消息。
     * @returns HNA 消息。
     */
    const Hna& GetHna() const
    {
        NS_ASSERT(m_messageType == HNA_MESSAGE);
        return m_message.hna;
    }
};

inline std::ostream&
operator<<(std::ostream& os, const PacketHeader& packet)
{
    packet.Print(os);
    return os;
}

inline std::ostream&
operator<<(std::ostream& os, const MessageHeader& message)
{
    message.Print(os);
    return os;
}

typedef std::vector<MessageHeader> MessageList;

inline std::ostream&
operator<<(std::ostream& os, const MessageList& messages)
{
    os << "[";
    for (auto messageIter = messages.begin(); messageIter != messages.end(); messageIter++)
    {
        messageIter->Print(os);
        if (messageIter + 1 != messages.end())
        {
            os << ", ";
        }
    }
    os << "]";
    return os;
}

} // namespace polsr
} // namespace ns3

#endif /* POLSR_HEADER_H */
