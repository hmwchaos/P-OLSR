/*
 * Copyright (c) 2004 Francisco J. Ros
 * Copyright (c) 2007 INESC Porto
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Francisco J. Ros  <fjrm@dif.um.es>
 *          Gustavo J. A. M. Carneiro <gjc@inescporto.pt>
 */

#ifndef POLSR_AGENT_IMPL_H
#define POLSR_AGENT_IMPL_H

#include "polsr-header.h"
#include "polsr-repositories.h"
#include "polsr-state.h"

#include "ns3/event-garbage-collector.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/test.h"
#include "ns3/timer.h"
#include "ns3/traced-callback.h"

#include <map>
#include <vector>

/// MPR 计算机制的测试用例
class PolsrMprTestCase;

namespace ns3
{
namespace polsr
{

///
/// @defgroup polsr POLSR 路由
/// 本节记录 ns-3 POLSR 模块的 API。若需要通用功能说明，
/// 请参考 ns-3 manual。

/// @ingroup polsr
/// POLSR 的一条路由表项。
struct RoutingTableEntry
{
    Ipv4Address destAddr; //!< 目的节点地址。
    Ipv4Address nextAddr; //!< 下一跳地址。
    uint32_t interface;   //!< 本地出接口索引。
    uint32_t distance;    //!< 到目的节点的跳数距离。

    RoutingTableEntry()
        : // 默认值
          destAddr(),
          nextAddr(),
          interface(0),
          distance(0)
    {
    }
};

class RoutingProtocol;

///
/// @ingroup polsr
///
/// @brief IPv4 的 POLSR 路由协议实现。
///
/// 学习提示：这个类是整个 POLSR 模块的主控类。它一边实现 ns-3
/// Ipv4RoutingProtocol 接口，一边维护 POLSR 自己的邻居表、MPR、拓扑表和路由表。
///
class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    /**
     * 声明为友元类，便于单元测试访问内部状态,可以访问 RoutingProtocol 里面的 private 和 protected 成员。
     */
    friend class ::PolsrMprTestCase;

    static const uint16_t POLSR_PORT_NUMBER; //!< POLSR 使用的 UDP 端口号（698）。

    /**
     * @brief 获取 TypeId。
     * @return 当前对象的 TypeId。
     */
    static TypeId GetTypeId();//它主要用于 ns-3 的对象系统，让 ns-3 知道这个类叫什么、继承自谁、有哪些属性、有哪些 TraceSource。

    RoutingProtocol();
    ~RoutingProtocol() override;

    /**
     * @brief 将 POLSR 主地址设置为指定接口上的第一个地址。
     *
     * @param interface IPv4 接口索引。
     */
    void SetMainInterface(uint32_t interface);

    /**
     * 将邻居表、二跳邻居表和路由表输出到日志中（NS_LOG_DEBUG 级别）。
     * 如果日志未启用，此函数不会产生输出。
     */
    void Dump();

    /**
     * 获取路由表项。
     * @return POLSR 发现并计算出的路由表项列表。
     */
    std::vector<RoutingTableEntry> GetRoutingTableEntries() const;

    /**
     * 获取 MPR 集合。
     * @return MPR 集合。
     */
    MprSet GetMprSet() const;

    /**
     * 获取 MPR selector 集合。
     * @returns MPR selector 集合。
     */
    const MprSelectorSet& GetMprSelectors() const;

    /**
     * 获取一跳邻居集合。
     * @return POLSR 发现的一跳邻居集合。
     */
    const NeighborSet& GetNeighbors() const;//前面的 const：保护返回出去的结果，外部不能通过这个返回值修改邻居表。后面的 const：保护当前对象，保证这个函数内部不能修改 RoutingProtocol 对象的成员变量。

    /**
     * 获取二跳邻居集合。
     * @return POLSR 发现的二跳邻居集合。
     */
    const TwoHopNeighborSet& GetTwoHopNeighbors() const;

    /**
     * 获取拓扑集合。
     * @returns POLSR 发现的拓扑集合。
     */
    const TopologySet& GetTopologySet() const;

    /**
     * 获取底层 POLSR 状态对象。
     * @returns POLSR 状态对象。
     */
    const PolsrState& GetPolsrState() const;

    /**
     * 为本模型使用的<随机变量>分配固定随机数流编号。
     * 返回已分配的流数量，可能为 0。
     *
     * @param stream 起始随机数流索引。
     * @return 本模型分配的随机数流索引数量。
     */
    int64_t AssignStreams(int64_t stream);

    /**
     * Packet 发送和接收事件的 TracedCallback 签名。
     *
     * @param [in] header
     * @param [in] messages
     */
    typedef void (*PacketTxRxTracedCallback)(const PacketHeader& header,
                                             const MessageList& messages);

    /**
     * 路由表计算事件的 TracedCallback 签名。
     *
     * @param [in] size 最终路由表大小。
     */
    typedef void (*TableChangeTracedCallback)(uint32_t size);

  private:
    std::set<uint32_t> m_interfaceExclusions; //!< 被排除、不运行 POLSR 的接口集合。
    Ptr<Ipv4StaticRouting>
        m_routingTableAssociation; //!< 与 Ipv4StaticRouting 实例建立的关联。

  public:
    /**
     * 获取被排除的接口集合。
     * @returns 被排除接口的容器。
     */
    std::set<uint32_t> GetInterfaceExclusions() const
    {
        return m_interfaceExclusions;
    }

    /**
     * 设置要排除的接口集合。
     * @param exceptions 被排除接口的容器。
     */
    void SetInterfaceExclusions(std::set<uint32_t> exceptions);

    /**
     *  @brief 将指定的 (networkAddr, netmask) 元组加入本地 HNA 关联列表，
     *  后续由节点通过 HNA 消息发送出去。
     *  如果该元组已存在，则不做任何处理。
     *
     * @param networkAddr 网络地址。
     * @param netmask 网络掩码。
     */
    void AddHostNetworkAssociation(Ipv4Address networkAddr, Ipv4Mask netmask);

    /**
     * @brief 从本地 HNA 关联列表中删除指定的 (networkAddr, netmask) 元组，
     * 该列表用于通过 HNA 消息对外通告。
     * 如果该元组不存在，则不做任何处理（见 "PolsrState::EraseAssociation()"）。
     *
     * @param networkAddr 网络地址。
     * @param netmask 网络掩码。
     */
    void RemoveHostNetworkAssociation(Ipv4Address networkAddr, Ipv4Mask netmask);

    /**
     * @brief 将指定的 Ipv4StaticRouting 路由表关联到 POLSR 路由协议。
     *        该关联路由表中使用非 POLSR 出接口的表项会被加入本地 HNA 关联列表，
     *        从而包含在本节点发送的 HNA 消息中。
     *        如果此方法被多次调用，会先删除旧关联中的表项，再加入新关联中的表项。
     * @param routingTable 要关联的 Ipv4StaticRouting 路由表。
     */
    void SetRoutingTableAssociation(Ptr<Ipv4StaticRouting> routingTable);

    /**
     * @brief 返回内部 HNA 路由表。
     * @returns 内部 HNA 路由表。
     */
    Ptr<const Ipv4StaticRouting> GetRoutingTableAssociation() const;

  protected:
    void DoInitialize() override;// 这个函数在 ns-3 对象系统中被调用，用于初始化对象的状态。对于 POLSR 来说，这里会设置定时器，初始化协议状态等。override这个函数是在重写父类中的虚函数。
    void DoDispose() override;

  private:
    // 学习提示：下面这些成员是协议运行时的核心状态。
    // m_table 是最终给数据包转发使用的路由表，m_state 保存 POLSR 协议内部各类集合。
    std::map<Ipv4Address, RoutingTableEntry> m_table; //!< 路由表的数据结构。

    Ptr<Ipv4StaticRouting> m_hnaRoutingTable; //!< HNA 路由使用的路由表。

    EventGarbageCollector m_events; //!< 当前运行中的事件集合。

    uint16_t m_packetSequenceNumber;  //!< Packet 序列号计数器。
    uint16_t m_messageSequenceNumber; //!< Message 序列号计数器。
    uint16_t m_ansn;                  //!< Advertised Neighbor Set 序列号。

    Time m_helloInterval;      //!< HELLO 消息发送间隔。
    Time m_tcInterval;         //!< TC 消息发送间隔。
    Time m_midInterval;        //!< MID 消息发送间隔。
    Time m_hnaInterval;        //!< HNA 消息发送间隔。
    Willingness m_willingness; //!< 节点替其他节点转发数据包的意愿值。

    /*
     * P-OLSR 参数组。
     *
     * 默认保持关闭，保证这份协议副本在不设置 Attribute 时仍尽量表现为普通 OLSR。
     * 实验脚本中选择 protocol=P-OLSR 时，才打开预测、预测路由和 MPR tie-break。
     */
    bool m_enablePrediction;          //!< 是否启用 P-OLSR 预测机制。
    bool m_usePredictionForRouting;   //!< 是否用预测代价计算路由表。
    bool m_usePredictionForMpr;       //!< 是否用预测稳定度辅助 MPR 选择。
    double m_predictionAlpha;         //!< HELLO 链路质量平滑系数。
    double m_predictionBeta;          //!< 相对速度对稳定度的影响系数。
    double m_predictionGamma;         //!< 链路稳定度平滑系数。
    double m_minPredictionQuality;    //!< 预测质量下限，避免除零。
    double m_maxPredictionCost;       //!< 预测代价上限。
    Time m_predictionValidityTime;    //!< 节点运动信息有效时间。

    PolsrState m_state; //!< POLSR 内部状态，包含协议所需的各类集合和表。
    Ptr<Ipv4> m_ipv4;  //!< 当前路由协议关联的 IPv4 对象。

    /**
     * @brief 清空路由表，并释放各路由表项占用的内存。
     */
    void Clear();

    /**
     * 返回路由表大小。
     * @return 路由表大小。
     */
    uint32_t GetSize() const
    {
        return m_table.size();
    }

    /**
     * @brief 删除指定目的地址对应的路由表项。
     * @param dest 目的节点地址。
     */
    void RemoveEntry(const Ipv4Address& dest);
    /**
     * @brief 向路由表中加入新表项。
     *
     * 如果指定目的地址已有表项，则先删除并释放旧表项。
     *
     * @param dest 目的节点地址。
     * @param next 下一跳节点地址。
     * @param interface 本地接口索引。
     * @param distance 到目的节点的跳数距离。
     */
    void AddEntry(const Ipv4Address& dest,
                  const Ipv4Address& next,
                  uint32_t interface,
                  uint32_t distance);
    /**
     * @brief 向路由表中加入新表项。
     *
     * 如果指定目的地址已有表项，则抛出错误。
     *
     * @param dest 目的节点地址。
     * @param next 下一跳节点地址。
     * @param interfaceAddress 本地接口地址。
     * @param distance 到目的节点的跳数距离。
     */
    void AddEntry(const Ipv4Address& dest,
                  const Ipv4Address& next,
                  const Ipv4Address& interfaceAddress,
                  uint32_t distance);

    /**
     * @brief 查找指定目的地址对应的路由表项。
     * @param [in] dest 目的地址。
     * @param [out] outEntry 如果找到，则保存路由表项结果。
     * @return 找到返回 true，否则返回 false。
     */
    bool Lookup(const Ipv4Address& dest, RoutingTableEntry& outEntry) const;

    /**
     * @brief 在给定目的地址的情况下，查找真正用于向下一跳转发数据包的合适表项。
     *
     * 假设路由表形如：[A,B] [B,C] [C,C]，每个二元组格式为
     * [目的地址, 下一跳地址]。如果调用本函数时传入 [A,B]，则返回 [C,C]，
     * 因为要把发往 A 的数据包转发出去时，真正应使用的下一跳邻居是 C。
     * 换句话说，C 是本节点的邻居，而 B 不是。本函数用于找到可直接转发的邻居。
     *
     * \param[in] entry 指向目标目的节点的路由表项。
     *
     * \param[out] outEntry 合适的路由表项，表示转发数据包时必须使用的下一跳；
     * 如果不存在这样的表项，则为空。
     *
     * @return 找到表项返回 true，否则返回 false。
     */
    bool FindSendEntry(const RoutingTableEntry& entry, RoutingTableEntry& outEntry) const;

  public:
    // 来自 Ipv4RoutingProtocol 的接口。
    // 学习提示：RouteOutput/RouteInput 是 ns-3 数据包真正向 POLSR 查询路由的入口。
    /*POLSR 协议对象和接口的关系
      ├── 读取节点上的 IPv4 接口信息
      ├── 为接口创建/绑定 socket
      ├── 通过接口发送 HELLO、TC 等控制消息
      ├── 根据收到消息的接口判断链路关系
      └── 计算路由时记录应该从哪个接口转发
      一、接入绑定
      SetIpv4()

      二、数据包转发
      RouteOutput()
      RouteInput()

      三、接口/地址变化通知
      NotifyInterfaceUp()
      NotifyInterfaceDown()
      NotifyAddAddress()
      NotifyRemoveAddress()

      四、调试输出
      PrintRoutingTable()
    */

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;// 这个函数在 ns-3 数据包转发过程中被调用，用于查询数据包的下一跳和出接口。对于 POLSR 来说，这里会根据目的地址查找路由表，找到合适的下一跳和出接口，并返回一个 Ipv4Route 对象给 ns-3 的转发机制。
    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;// 这个函数在 ns-3 数据包接收过程中被调用，用于处理收到的数据包。对于 POLSR 来说，这里会检查数据包是否是发往本节点的，如果是则本地处理；如果不是则根据路由表转发出去；如果没有路由则调用错误回调。
    void SetIpv4(Ptr<Ipv4> ipv4) override;//  这个函数在 ns-3 对象系统中被调用，用于将当前路由协议与一个 Ipv4 对象关联起来。对于 POLSR 来说，这里会保存传入的 Ipv4 对象指针，并通过它来访问节点的接口信息等。  把当前 POLSR 对象和当前节点的 IPv4 协议对象关联起来。

    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;// 这个函数在 ns-3 中被调用，用于打印路由表信息。对于 POLSR 来说，这里会遍历路由表并将每条路由的信息输出到指定的流中，通常用于调试和分析。

  private:
    void NotifyInterfaceUp(uint32_t interface) override;// 这个函数在 ns-3 的接口状态变化时被调用，用于通知 POLSR 某个接口变为可用。对于 POLSR 来说，这里会检查新接口是否需要运行 POLSR，如果需要则加入协议状态并可能发送 HELLO 消息。
    void NotifyInterfaceDown(uint32_t interface) override;// 这个函数在 ns-3 的接口状态变化时被调用，用于通知 POLSR 某个接口变为不可用。对于 POLSR 来说，这里会检查失效接口是否在协议状态中，如果在则删除相关状态并可能发送 HELLO 消息。
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;// 这个函数在 ns-3 的接口地址变化时被调用，用于通知 POLSR 某个接口上添加了一个地址。对于 POLSR 来说，这里会检查新地址是否需要加入协议状态，如果需要则加入相关状态并可能发送 MID 消息。
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;// 这个函数在 ns-3 的接口地址变化时被调用，用于通知 POLSR 某个接口上删除了一个地址。对于 POLSR 来说，这里会检查失效地址是否在协议状态中，如果在则删除相关状态并可能发送 MID 消息。

    /**
     * 发送一个 POLSR 消息包。
     * @param packet 要发送的 Packet。
     * @param containedMessages Packet 中包含的 POLSR 消息列表。
     */
    void SendPacket(Ptr<Packet> packet, const MessageList& containedMessages);

    /**
     * 递增 Packet 序列号，并返回新值。
     * @return Packet 序列号。
     */
    inline uint16_t GetPacketSequenceNumber();

    /**
     * 递增 Message 序列号，并返回新值。
     * @return Message 序列号。
     */
    inline uint16_t GetMessageSequenceNumber();

    /**
     * 接收 POLSR 消息。
     * @param socket 接收消息的 socket。
     */
    void RecvPolsr(Ptr<Socket> socket);

    /**
     * @brief 按照 \RFC{3626} 的思路计算节点的 MPR 集合。
     */
    void MprComputation();

    /**
     * @brief 按照 \RFC{3626} 的思路为节点生成路由表。
     */
    void RoutingTableComputation();

    /**
     * @brief 使用 P-OLSR 预测代价生成路由表。
     */
    void PredictiveRoutingTableComputation();

    /**
     * @brief 查询本节点当前位置。
     */
    Vector LookupOwnPosition() const;

    /**
     * @brief 查询本节点当前速度。
     */
    Vector LookupOwnVelocity() const;

    /**
     * @brief 计算两节点沿连线方向的相对速度。
     */
    double ComputeRadialRelativeSpeed(const Vector& localPosition,
                                      const Vector& localVelocity,
                                      const Vector& neighborPosition,
                                      const Vector& neighborVelocity) const;

    /**
     * @brief 将 HELLO 中携带的 GPS/ETX 字段转换为链路预测指标。
     */
    void UpdatePredictionFromHello(const polsr::MessageHeader& msg,
                                   const polsr::MessageHeader::Hello& hello,
                                   const Ipv4Address& receiverIface,
                                   const Ipv4Address& senderIface);

    /**
     * @brief 获取到指定一跳邻居的预测代价。
     */
    double GetNeighborPredictionCost(const Ipv4Address& neighborMainAddr) const;

    /**
     * @brief 获取到指定一跳邻居的正向接收率。
     */
    double GetNeighborForwardReceivingRatio(const Ipv4Address& neighborMainAddr) const;

    /**
     * @brief 获取到指定一跳邻居的反向接收率。
     */
    double GetNeighborReverseReceivingRatio(const Ipv4Address& neighborMainAddr) const;

    /**
     * @brief 获取到指定一跳邻居的相对速度。
     */
    double GetNeighborRelativeSpeed(const Ipv4Address& neighborMainAddr) const;

  public:
    /**
     * @brief 获取指定接口地址对应的主地址。
     * @param iface_addr 接口地址。
     * @return 对应的主地址。
     */
    Ipv4Address GetMainAddress(Ipv4Address iface_addr) const;

  private:
    /**
     * @brief 测试指定路由是否使用非 POLSR 出接口。
     * @param route 要测试的路由。
     * @returns 如果指定路由的出接口是非 POLSR 接口，则返回 true；否则返回 false。
     */
    bool UsesNonPolsrOutgoingInterface(const Ipv4RoutingTableEntry& route);

    // 定时器处理函数。
    // 学习提示：这组定时器决定 POLSR 控制消息的周期性发送，以及状态表项何时过期。
    Timer m_helloTimer; //!< HELLO 消息定时器。
    /**
     * @brief 发送 HELLO 消息，并重新调度 HELLO 定时器。
     */
    void HelloTimerExpire();

    Timer m_tcTimer; //!< TC 消息定时器。
    /**
     * @brief 发送 TC 消息（如果存在 MPR selector），并重新调度 TC 定时器。
     */
    void TcTimerExpire();

    Timer m_midTimer; //!< MID 消息定时器。
    /**
     * @brief 发送 MID 消息（如果节点有多个接口），并重置 MID 定时器。
     */
    void MidTimerExpire();

    Timer m_hnaTimer; //!< HNA 消息定时器。
    /**
     * @brief 发送 HNA 消息（如果节点有关联主机/网络），并重新调度 HNA 定时器。
     */
    void HnaTimerExpire();

    /**
     * @brief 如果 tuple 已过期则删除；否则重新调度定时器，使其在 tuple.expirationTime 过期。
     *
     * 实际删除 tuple 的任务由 POLSR agent 完成。
     *
     * @param address tuple 的地址。
     * @param sequenceNumber tuple 的序列号。
     */
    void DupTupleTimerExpire(Ipv4Address address, uint16_t sequenceNumber);

    bool m_linkTupleTimerFirstTime; //!< 标记 LinkTupleTimer 是否第一次触发。
    /**
     * @brief 如果 tuple_ 已过期则删除。否则，如果对称链路时间已过期，
     * 则认为发生邻居丢失并调用 agent_->nb_loss()。
     * 在这种情况下，定时器会被重新调度到 tuple_->time() 过期；
     * 否则，定时器会被重新调度到 tuple_->time() 和 tuple_->sym_time() 中较早的时间过期。
     *
     * 实际删除 tuple 的任务由 POLSR agent 完成。
     *
     * @param neighborIfaceAddr tuple 中的邻居接口地址。
     */
    void LinkTupleTimerExpire(Ipv4Address neighborIfaceAddr);

    /**
     * @brief 如果二跳邻居 tuple_ 已过期则删除；否则重新调度定时器，使其在 tuple_->time() 过期。
     *
     * 实际删除 tuple 的任务由 POLSR agent 完成。
     *
     * @param neighborMainAddr 邻居主地址。
     * @param twoHopNeighborAddr 二跳邻居地址。
     */
    void Nb2hopTupleTimerExpire(Ipv4Address neighborMainAddr, Ipv4Address twoHopNeighborAddr);

    /**
     * @brief 如果 MPR selector tuple_ 已过期则删除；否则重新调度定时器，使其在 tuple_->time() 过期。
     *
     * 实际删除 tuple 的任务由 POLSR agent 完成。
     *
     * @param mainAddr tuple 的 IPv4 地址。
     */
    void MprSelTupleTimerExpire(Ipv4Address mainAddr);

    /**
     * @brief 如果 topology tuple_ 已过期则删除；否则重新调度定时器，使其在 tuple_->time() 过期。
     *
     * 实际删除 tuple 的任务由 POLSR agent 完成。
     *
     * @param destAddr 目的地址。
     * @param lastAddr 上一跳/最后通告者地址。
     */
    void TopologyTupleTimerExpire(Ipv4Address destAddr, Ipv4Address lastAddr);

    /**
     * @brief 如果接口关联 tuple_ 已过期则删除；否则重新调度定时器，使其在 tuple_->time() 过期。
     *
     * @param ifaceAddr 接口地址。
     */
    void IfaceAssocTupleTimerExpire(Ipv4Address ifaceAddr);

    /**
     * @brief 如果 association tuple_ 已过期则删除；否则重新调度定时器，使其在 tuple_->time() 过期。
     *
     * @param gatewayAddr 网关地址。
     * @param networkAddr 网络地址。
     * @param netmask 网络掩码。
     */
    void AssociationTupleTimerExpire(Ipv4Address gatewayAddr,
                                     Ipv4Address networkAddr,
                                     Ipv4Mask netmask);

    /**
     * 递增 ANSN 计数器。
     */
    void IncrementAnsn();

    /// 等待发送的消息缓存列表。
    polsr::MessageList m_queuedMessages;
    Timer m_queuedMessagesTimer; //!< 用于限制/聚合外发消息的定时器。

    /**
     * @brief POLSR 默认转发算法。
     *
     * 详细规则见 \RFC{3626}。
     *
     * @param polsrMessage 必须被转发的 %POLSR 消息。
     * @param duplicated 如果该消息从未被考虑过转发，则为 NULL；否则为对应的 duplicate tuple。
     * @param localIface 接收该消息的本地接口地址。
     * @param senderAddress 发送者 IPv4 地址。
     */
    void ForwardDefault(polsr::MessageHeader polsrMessage,
                        DuplicateTuple* duplicated,
                        const Ipv4Address& localIface,
                        const Ipv4Address& senderAddress);

    /**
     * @brief 将一条 %POLSR 消息加入队列，该消息会在 (0, delay] 的延迟范围内发送。
     *
     * 该缓存机制用于把多条 %POLSR 消息搭载在同一个 %POLSR packet 中发送。
     *
     * @param message 必须发送的 %POLSR 消息。
     * @param delay %POLSR 消息允许被缓存的最大延迟。
     */
    void QueueMessage(const polsr::MessageHeader& message, Time delay);

    /**
     * @brief 根据需要创建足够数量的 %POLSR packet，以发送所有已缓存的 %POLSR 消息。
     *
     * 一个 %POLSR packet 可包含的最大消息数由 POLSR_MAX_MSGS 常量决定。
     */
    void SendQueuedMessages();

    /**
     * @brief 创建一条新的 %POLSR HELLO 消息，并缓存起来等待稍后发送。
     * SendHello() 负责“准备内容”,给 msg 这个结构体赋值，Serialize() 负责“按协议格式排好顺序写进 packet”，Deserialize() 负责“收到后按同样顺序读出来”。
     */
    void SendHello();

    /**
     * @brief 创建一条新的 %POLSR TC 消息，并缓存起来等待稍后发送。
     */
    void SendTc();

    /**
     * @brief 创建一条新的 %POLSR MID 消息，并缓存起来等待稍后发送。
     */
    void SendMid();

    /**
     * @brief 创建一条新的 %POLSR HNA 消息，并缓存起来等待稍后发送。
     */
    void SendHna();

    /**
     * @brief 发生邻居丢失时执行所需的全部处理。
     *
     * 会更新 Neighbor Set、2-hop Neighbor Set、MPR Set 和 MPR Selector Set。
     *
     * @param tuple 记录已丢失邻居链路信息的 link tuple。
     */
    void NeighborLoss(const LinkTuple& tuple);

    /**
     * @brief 向 Duplicate Set 中加入 duplicate tuple。
     *
     * @param tuple 要加入的 duplicate tuple。
     */
    void AddDuplicateTuple(const DuplicateTuple& tuple);

    /**
     * @brief 从 Duplicate Set 中删除 duplicate tuple。
     *
     * @param tuple 要删除的 duplicate tuple。
     */
    void RemoveDuplicateTuple(const DuplicateTuple& tuple);

    /**
     * 添加 link tuple。
     * @param tuple 要加入的 tuple。
     * @param willingness 该 tuple 对应的 willingness 值。
     */
    void LinkTupleAdded(const LinkTuple& tuple, Willingness willingness);

    /**
     * @brief 从 Link Set 中删除 link tuple。
     *
     * @param tuple 要删除的 link tuple。
     */
    void RemoveLinkTuple(const LinkTuple& tuple);

    /**
     * @brief 当 link tuple 被更新时调用本函数。
     * 其目标是在需要时同步更新对应的 neighbor tuple。
     *
     * @param tuple 已更新的 link tuple。
     * @param willingness 该 tuple 对应的 willingness 值。
     */
    void LinkTupleUpdated(const LinkTuple& tuple, Willingness willingness);

    /**
     * @brief 向 Neighbor Set 中加入 neighbor tuple。
     *
     * @param tuple 要加入的 neighbor tuple。
     */
    void AddNeighborTuple(const NeighborTuple& tuple);

    /**
     * @brief 从 Neighbor Set 中删除 neighbor tuple。
     *
     * @param tuple 要删除的 neighbor tuple。
     */
    void RemoveNeighborTuple(const NeighborTuple& tuple);

    /**
     * @brief 向 2-hop Neighbor Set 中加入二跳邻居 tuple。
     *
     * @param tuple 要加入的二跳邻居 tuple。
     */
    void AddTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple);

    /**
     * @brief 从 2-hop Neighbor Set 中删除二跳邻居 tuple。
     *
     * @param tuple 要删除的二跳邻居 tuple。
     */
    void RemoveTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple);

    /**
     * @brief 向 MPR Selector Set 中加入 MPR selector tuple。
     * 同时更新 Advertised Neighbor Sequence Number（ANSN）。
     *
     * @param tuple 要加入的 MPR selector tuple。
     */
    void AddMprSelectorTuple(const MprSelectorTuple& tuple);

    /**
     * @brief 从 MPR Selector Set 中删除 MPR selector tuple。
     * 同时更新 Advertised Neighbor Sequence Number（ANSN）。
     *
     * @param tuple 要删除的 MPR selector tuple。
     */
    void RemoveMprSelectorTuple(const MprSelectorTuple& tuple);

    /**
     * @brief 向 Topology Set 中加入 topology tuple。
     *
     * @param tuple 要加入的 topology tuple。
     */
    void AddTopologyTuple(const TopologyTuple& tuple);

    /**
     * @brief 从 Topology Set 中删除 topology tuple。
     *
     * @param tuple 要删除的 topology tuple。
     */
    void RemoveTopologyTuple(const TopologyTuple& tuple);

    /**
     * @brief 向 Interface Association Set 中加入接口关联 tuple。
     *
     * @param tuple 要加入的接口关联 tuple。
     */
    void AddIfaceAssocTuple(const IfaceAssocTuple& tuple);

    /**
     * @brief 从 Interface Association Set 中删除接口关联 tuple。
     *
     * @param tuple 要删除的接口关联 tuple。
     */
    void RemoveIfaceAssocTuple(const IfaceAssocTuple& tuple);

    /**
     * @brief 向 Association Set 中加入主机网络关联 tuple。
     *
     * @param tuple 要加入的主机网络关联 tuple。
     */
    void AddAssociationTuple(const AssociationTuple& tuple);

    /**
     * @brief 从 Association Set 中删除主机网络关联 tuple。
     *
     * @param tuple 要删除的主机网络关联 tuple。
     */
    void RemoveAssociationTuple(const AssociationTuple& tuple);

    /**
     * @brief 按照 \RFC{3626} 规范处理 HELLO 消息。
     *
     * 这里会执行链路感知，并填充/更新 Neighbor Set、2-hop Neighbor Set 和 MPR Selector Set。
     *
     * @param msg 包含 HELLO 消息的 %POLSR 消息。
     * @param receiverIface 接收该消息的接口地址。
     * @param senderIface 发送该消息的接口地址。
     */
    void ProcessHello(const polsr::MessageHeader& msg,
                      const Ipv4Address& receiverIface,
                      const Ipv4Address& senderIface);

    /**
     * @brief 按照 \RFC{3626} 规范处理 TC 消息。
     *
     * 根据收到的 TC 消息信息，在需要时更新 Topology Set。
     *
     * @param msg 包含 TC 消息的 %POLSR 消息。
     * @param senderIface 发送该消息的接口地址。
     *
     */
    void ProcessTc(const polsr::MessageHeader& msg, const Ipv4Address& senderIface);

    /**
     * @brief 按照 \RFC{3626} 规范处理 MID 消息。
     *
     * 根据收到的 MID 消息信息，在需要时更新 Interface Association Set。
     *
     * @param msg 包含 MID 消息的 %POLSR 消息。
     * @param senderIface 发送该消息的接口地址。
     */
    void ProcessMid(const polsr::MessageHeader& msg, const Ipv4Address& senderIface);

    /**
     *
     * @brief 按照 \RFC{3626} 规范处理 HNA 消息。
     *
     * 根据收到的 HNA 消息信息，在需要时更新 Host Network Association Set。
     *
     * @param msg 包含 HNA 消息的 %POLSR 消息。
     * @param senderIface 发送该消息的接口地址。
     *
     */
    void ProcessHna(const polsr::MessageHeader& msg, const Ipv4Address& senderIface);

    /**
     * @brief 根据新收到的 HELLO 消息更新 Link Set（遵循 \RFC{3626}）。
     * 如果需要，也会更新 Neighbor Set。
     * @param msg 收到的消息。
     * @param hello 收到的 HELLO 子消息。
     * @param receiverIface 接收该消息的接口。
     * @param senderIface 发送方接口。
     */
    void LinkSensing(const polsr::MessageHeader& msg,
                     const polsr::MessageHeader::Hello& hello,
                     const Ipv4Address& receiverIface,
                     const Ipv4Address& senderIface);

    /**
     * @brief 根据新收到的 HELLO 消息中包含的信息更新 Neighbor Set
     * （遵循 \RFC{3626}）。
     * @param msg 收到的消息。
     * @param hello 收到的 HELLO 子消息。
     */
    void PopulateNeighborSet(const polsr::MessageHeader& msg,
                             const polsr::MessageHeader::Hello& hello);

    /**
     * @brief 根据新收到的 HELLO 消息中包含的信息更新 2-hop Neighbor Set
     * （遵循 \RFC{3626}）。
     * @param msg 收到的消息。
     * @param hello 收到的 HELLO 子消息。
     */
    void PopulateTwoHopNeighborSet(const polsr::MessageHeader& msg,
                                   const polsr::MessageHeader::Hello& hello);

    /**
     * @brief 根据新收到的 HELLO 消息中包含的信息更新 MPR Selector Set
     * （遵循 \RFC{3626}）。
     * @param msg 收到的消息。
     * @param hello 收到的 HELLO 子消息。
     */
    void PopulateMprSelectorSet(const polsr::MessageHeader& msg,
                                const polsr::MessageHeader::Hello& hello);

    int Degree(const NeighborTuple& tuple);

    /**
     * 检查某个地址是否属于本节点的某个接口。
     * @param a 要检查的地址。
     * @return 如果该地址属于本节点，则返回 true。
     */
    bool IsMyOwnAddress(const Ipv4Address& a) const;

    Ipv4Address m_mainAddress; //!< 节点主地址。

    // 每个接口对应一个 socket，每个 socket 绑定到该接口地址。
    // 原因：POLSR 链路感知需要知道 HELLO 消息是从哪个接口收到的。
    std::map<Ptr<Socket>, Ipv4InterfaceAddress>
        m_sendSockets;        //!< socket 及其绑定接口的容器。
    Ptr<Socket> m_recvSocket; //!< 接收 socket。

    /// 接收 Packet 的 trace。
    TracedCallback<const PacketHeader&, const MessageList&> m_rxPacketTrace;

    /// 发送 Packet 的 trace。
    TracedCallback<const PacketHeader&, const MessageList&> m_txPacketTrace;

    /// 路由表变化回调。
    TracedCallback<uint32_t> m_routingTableChanged;

    /// 提供均匀分布随机变量。
    Ptr<UniformRandomVariable> m_uniformRandomVariable;
};

} // namespace polsr
} // namespace ns3

#endif /* POLSR_AGENT_IMPL_H */
