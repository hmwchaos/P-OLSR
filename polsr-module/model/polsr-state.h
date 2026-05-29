/*
 * Copyright (c) 2004 Francisco J. Ros
 * Copyright (c) 2007 INESC Porto
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Francisco J. Ros  <fjrm@dif.um.es>
 *          Gustavo J. A. M. Carneiro <gjc@inescporto.pt>
 */
/*
 * POLSR 内部主要表 / 集合总览
 *
 * 1. Link Set
 *    - 由 LinkTuple 组成
 *    - 记录：本节点接口和邻居接口之间的链路状态
 *    - 与本节点关系：直接属于本节点的一跳链路信息
 *
 * 2. Neighbor Set
 *    - 由 NeighborTuple 组成
 *    - 记录：本节点的一跳邻居
 *    - 与本节点关系：本节点的一跳邻居表
 *
 * 3. Two-hop Neighbor Set
 *    - 由 TwoHopNeighborTuple 组成
 *    - 记录：本节点通过哪个一跳邻居能到达哪个二跳邻居
 *    - 与本节点关系：本节点视角下的二跳邻居表
 *
 * 4. MPR Set
 *    - 由一组主地址组成
 *    - 记录：本节点选择哪些邻居作为 MPR
 *    - 与本节点关系：我选择了谁当 MPR
 *
 * 5. MPR Selector Set
 *    - 由 MprSelectorTuple 组成
 *    - 记录：哪些节点选择了本节点作为 MPR
 *    - 与本节点关系：谁选择了我当 MPR
 *
 * 6. Topology Set
 *    - 由 TopologyTuple 组成
 *    - 记录：通过 TC 消息学到的拓扑关系
 *    - 与本节点关系：本节点学到的全网拓扑信息，不一定只和自己直接相关
 *
 * 7. Duplicate Set
 *    - 由 DuplicateTuple 组成
 *    - 记录：本节点已经收到或转发过哪些控制消息
 *    - 与本节点关系：本节点自己的防重复记录表
 *
 * 8. Iface Association Set
 *    - 由 IfaceAssocTuple 组成
 *    - 记录：接口地址和主地址之间的映射关系
 *    - 与本节点关系：本节点维护的地址映射表，既可能包含自己的接口，
 *      也可能包含从 MID 消息中学到的其他节点接口
 *
 * 9. Associations
 *    - 由 Association 组成
 *    - 记录：本节点准备通过 HNA 通告出去的外部网络
 *    - 与本节点关系：本节点本地配置的 HNA 信息
 *
 * 10. Association Set
 *     - 由 AssociationTuple 组成
 *     - 记录：哪个网关可以到达哪个外部网络
 *     - 与本节点关系：本节点从 HNA 消息中学到的外部网络信息
 */

#ifndef POLSR_STATE_H
#define POLSR_STATE_H

#include "polsr-repositories.h"

namespace ns3
{
namespace polsr
{

/// @ingroup polsr
/// 该类封装了维护一个 POLSR 节点内部状态所需的全部数据结构。
/// 学习提示：repositories.h 定义“有哪些表和记录”，PolsrState 负责把这些表真正组织起来，
/// 并提供查找、插入、删除等访问接口。
class PolsrState
{
    //  friend class Polsr;

  protected:
    // 学习提示：这组成员就是 POLSR 运行时真正维护的“内部数据库”。
    LinkSet m_linkSet;                     //!< 链路集合（\RFC{3626} 第 4.2.1 节）。
    NeighborSet m_neighborSet;             //!< 邻居集合（\RFC{3626} 第 4.3.1 节）。
    TwoHopNeighborSet m_twoHopNeighborSet; //!< 二跳邻居集合（\RFC{3626} 第 4.3.2 节）。
    TopologySet m_topologySet;             //!< 拓扑集合（\RFC{3626} 第 4.4 节）。
    NodeMotionSet m_nodeMotionSet;         //!< P-OLSR 节点位置和速度缓存。
    MprSet m_mprSet;                       //!< MPR 集合（\RFC{3626} 第 4.3.3 节）。
    MprSelectorSet m_mprSelectorSet;       //!< MPR selector 集合（\RFC{3626} 第 4.3.4 节）。
    DuplicateSet m_duplicateSet;           //!< 重复消息集合（\RFC{3626} 第 3.4 节）。
    IfaceAssocSet m_ifaceAssocSet;         //!< 接口关联集合（\RFC{3626} 第 4.1 节）。
    AssociationSet m_associationSet; //!< HNA 关联集合（\RFC{3626} 第 12.2 节），保存其他节点通过 HNA
                                     //!< 消息通告而来的关联信息。
    Associations m_associations;     //!< 本节点自己的 Host Network Association，
                                     //!< 后续会通过 HNA 消息对外通告。

  public:
    PolsrState()
    {
    }

    // P-OLSR 节点运动状态相关接口

    /**
     * 获取节点运动状态集合。
     * @returns 节点运动状态集合。
     */
    const NodeMotionSet& GetNodeMotionSet() const
    {
        return m_nodeMotionSet;
    }

    /**
     * 查找节点运动状态 tuple。
     * @param mainAddr 节点主地址。
     * @returns 如果找到则返回 tuple，否则返回空指针。
     */
    NodeMotionTuple* FindNodeMotionTuple(const Ipv4Address& mainAddr);

    /**
     * 查找节点运动状态 tuple。
     * @param mainAddr 节点主地址。
     * @returns 如果找到则返回 tuple，否则返回空指针。
     */
    const NodeMotionTuple* FindNodeMotionTuple(const Ipv4Address& mainAddr) const;

    /**
     * 插入或更新节点运动状态 tuple。
     * @param tuple 要插入或更新的 tuple。
     */
    void InsertNodeMotionTuple(const NodeMotionTuple& tuple);

    /**
     * 插入或更新节点 GPS 坐标。
     * @param mainAddr 节点主地址。
     * @param position 节点坐标，按 (longitude, latitude, altitude) 映射。
     * @param longitude 经度。
     * @param latitude 纬度。
     * @param altitude 高度。
     * @param updateTime 更新时间。
     * @param expirationTime 过期时间。
     */
    void UpdateNodeMotionTuple(const Ipv4Address& mainAddr,
                               const Vector& position,
                               double longitude,
                               double latitude,
                               double altitude,
                               Time updateTime,
                               Time expirationTime);

    /**
     * 删除节点运动状态 tuple。
     * @param mainAddr 节点主地址。
     */
    void EraseNodeMotionTuple(const Ipv4Address& mainAddr);

    /**
     * 清理过期的节点运动状态 tuple。
     * @param now 当前仿真时间。
     */
    void PurgeExpiredNodeMotionTuples(Time now);

    // MPR selector 相关接口

    /**
     * 获取 MPR selector 集合。
     * @returns MPR selector 集合。
     */
    const MprSelectorSet& GetMprSelectors() const
    {
        return m_mprSelectorSet;
    }

    /**
     * 查找一个 MPR selector tuple。
     * @param mainAddr MPR selector 的主地址。
     * @returns 如果找到则返回对应 tuple，否则返回空指针。
     */
    MprSelectorTuple* FindMprSelectorTuple(const Ipv4Address& mainAddr);

    /**
     * 删除一个 MPR selector tuple。
     * @param tuple 要删除的 MPR selector tuple。
     */
    void EraseMprSelectorTuple(const MprSelectorTuple& tuple);

    /**
     * 删除属于同一地址的全部 MPR selector tuple。
     * @param mainAddr MPR selector 的主地址。
     */
    void EraseMprSelectorTuples(const Ipv4Address& mainAddr);

    /**
     * 插入一个 MPR selector tuple。
     * @param tuple 要插入的 MPR selector tuple。
     */
    void InsertMprSelectorTuple(const MprSelectorTuple& tuple);

    /**
     * 打印 MPR selector 集合。
     * @return 包含输出数据的字符串。
     */
    std::string PrintMprSelectorSet() const;

    // 邻居相关接口

    /**
     * 获取邻居集合。
     * @returns 邻居集合。
     */
    const NeighborSet& GetNeighbors() const
    {
        return m_neighborSet;
    }

    /**
     * 获取邻居集合。
     * @returns 邻居集合。
     */
    NeighborSet& GetNeighbors()
    {
        return m_neighborSet;
    }

    /**
     * 查找一个邻居 tuple。
     * @param mainAddr 邻居 tuple 的主地址。
     * @returns 如果找到则返回对应 tuple，否则返回空指针。
     */
    NeighborTuple* FindNeighborTuple(const Ipv4Address& mainAddr);

    /**
     * 查找一个对称邻居 tuple。
     * @param mainAddr 邻居 tuple 的主地址。
     * @returns 如果找到则返回对应 tuple，否则返回空指针。
     */
    const NeighborTuple* FindSymNeighborTuple(const Ipv4Address& mainAddr) const;

    /**
     * 查找一个邻居 tuple。
     * @param mainAddr 邻居 tuple 的主地址。
     * @param willingness 邻居的 willingness 值。
     * @returns 如果找到则返回对应 tuple，否则返回空指针。
     */
    NeighborTuple* FindNeighborTuple(const Ipv4Address& mainAddr, Willingness willingness);

    /**
     * 删除一个邻居 tuple。
     * @param neighborTuple 要删除的邻居 tuple。
     */
    void EraseNeighborTuple(const NeighborTuple& neighborTuple);
    /**
     * 按主地址删除邻居 tuple。
     * @param mainAddr 邻居 tuple 的主地址。
     */
    void EraseNeighborTuple(const Ipv4Address& mainAddr);

    /**
     * 插入一个邻居 tuple。
     * @param tuple 要插入的邻居 tuple。
     */
    void InsertNeighborTuple(const NeighborTuple& tuple);

    // 二跳邻居相关接口

    /**
     * 获取二跳邻居集合。
     * @returns 二跳邻居集合。
     */
    const TwoHopNeighborSet& GetTwoHopNeighbors() const
    {
        return m_twoHopNeighborSet;
    }

    /**
     * 获取二跳邻居集合。
     * @returns 二跳邻居集合。
     */
    TwoHopNeighborSet& GetTwoHopNeighbors()
    {
        return m_twoHopNeighborSet;
    }

    /**
     * 查找一个二跳邻居 tuple。
     * @param neighbor 一跳邻居主地址。
     * @param twoHopNeighbor 二跳邻居主地址。
     * @returns 如果找到则返回对应 tuple，否则返回空指针。
     */
    TwoHopNeighborTuple* FindTwoHopNeighborTuple(const Ipv4Address& neighbor,
                                                 const Ipv4Address& twoHopNeighbor);

    /**
     * 删除一个二跳邻居 tuple。
     * @param tuple 要删除的二跳邻居 tuple。
     */
    void EraseTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple);
    /**
     * 删除具有相同一跳邻居的二跳邻居 tuple。
     * @param neighbor 一跳邻居地址。
     */
    void EraseTwoHopNeighborTuples(const Ipv4Address& neighbor);
    /**
     * 删除满足指定条件的二跳邻居 tuple。
     * @param neighbor 一跳邻居地址。
     * @param twoHopNeighbor 二跳邻居主地址。
     */
    void EraseTwoHopNeighborTuples(const Ipv4Address& neighbor, const Ipv4Address& twoHopNeighbor);
    /**
     * 插入一个二跳邻居 tuple。
     * @param tuple 要插入的二跳邻居 tuple。
     */
    void InsertTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple);

    // MPR 相关接口

    /**
     * 检查指定地址是否属于 MPR 集合。
     * @param address 要检查的地址。
     * @return 如果指定地址存在于 MPR 集合中，则返回 true。
     */
    bool FindMprAddress(const Ipv4Address& address);

    /**
     * 将 MPR 集合设置为指定集合。
     * @param mprSet 新的 MPR 集合。
     */
    void SetMprSet(MprSet mprSet);

    /**
     * 获取 MPR 集合。
     * @return MPR 集合。
     */
    MprSet GetMprSet() const;

    // 重复消息相关接口

    /**
     * 查找重复消息 tuple。
     * @param address 重复消息 tuple 的源地址。
     * @param sequenceNumber 重复消息 tuple 的序列号。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    DuplicateTuple* FindDuplicateTuple(const Ipv4Address& address, uint16_t sequenceNumber);

    /**
     * 删除一个重复消息 tuple。
     * @param tuple 要删除的 tuple。
     */
    void EraseDuplicateTuple(const DuplicateTuple& tuple);
    /**
     * 插入一个重复消息 tuple。
     * @param tuple 要插入的 tuple。
     */
    void InsertDuplicateTuple(const DuplicateTuple& tuple);

    // 链路相关接口

    /**
     * 获取链路集合。
     * @return 链路集合。
     */
    const LinkSet& GetLinks() const
    {
        return m_linkSet;
    }

    /**
     * 查找链路 tuple。
     * @param ifaceAddr 链路对应的接口地址。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    LinkTuple* FindLinkTuple(const Ipv4Address& ifaceAddr);
    /**
     * 查找指定本地接口和邻居接口对应的链路 tuple。
     * @param localIfaceAddr 本地接口地址。
     * @param neighborIfaceAddr 邻居接口地址。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    LinkTuple* FindLinkTuple(const Ipv4Address& localIfaceAddr,
                             const Ipv4Address& neighborIfaceAddr);
    /**
     * 查找一个对称链路 tuple。
     * @param ifaceAddr 链路对应的接口地址。
     * @param time 在该时刻判断链路是否应视为对称。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    LinkTuple* FindSymLinkTuple(const Ipv4Address& ifaceAddr, Time time);
    /**
     * 删除一个链路 tuple。
     * @param tuple 要删除的 tuple。
     */
    void EraseLinkTuple(const LinkTuple& tuple);
    /**
     * 插入一个链路 tuple。
     * @param tuple 要插入的 tuple。
     * @returns 已插入 tuple 的引用。
     */
    LinkTuple& InsertLinkTuple(const LinkTuple& tuple);

    /**
     * 更新指定链路的 P-OLSR 预测指标。
     * @param localIfaceAddr 本地接口地址。
     * @param neighborIfaceAddr 邻居接口地址。
     * @param phi 正向接收率。
     * @param rho 反向接收率。
     * @param relativeSpeed 相对速度。
     * @param predictedCost 预测代价。
     * @param predictionTime 更新时间。
     */
    void UpdateLinkPrediction(const Ipv4Address& localIfaceAddr,
                              const Ipv4Address& neighborIfaceAddr,
                              double phi,
                              double rho,
                              double relativeSpeed,
                              double predictedCost,
                              Time predictionTime);

    /**
     * 获取指定链路的预测代价。没有预测值时返回 1.0。
     */
    double GetLinkPredictedCost(const Ipv4Address& localIfaceAddr,
                                const Ipv4Address& neighborIfaceAddr) const;

    // 拓扑相关接口

    /**
     * 获取拓扑集合。
     * @returns 拓扑集合。
     */
    const TopologySet& GetTopologySet() const
    {
        return m_topologySet;
    }

    /**
     * 查找一个拓扑 tuple。
     * @param destAddr 目的地址。
     * @param lastAddr 目的节点上一跳节点的地址。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    TopologyTuple* FindTopologyTuple(const Ipv4Address& destAddr, const Ipv4Address& lastAddr);
    /**
     * 查找一个更新的拓扑 tuple。
     * @param lastAddr 目的节点上一跳节点的地址。
     * @param ansn Advertised Neighbor Sequence Number。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    TopologyTuple* FindNewerTopologyTuple(const Ipv4Address& lastAddr, uint16_t ansn);
    /**
     * 删除一个拓扑 tuple。
     * @param tuple 要删除的 tuple。
     */
    void EraseTopologyTuple(const TopologyTuple& tuple);
    /**
     * 删除旧版本的拓扑 tuple。
     * @param lastAddr 目的节点上一跳节点的地址。
     * @param ansn Advertised Neighbor Sequence Number。
     */
    void EraseOlderTopologyTuples(const Ipv4Address& lastAddr, uint16_t ansn);
    /**
     * 插入一个拓扑 tuple。
     * @param tuple 要插入的 tuple。
     */
    void InsertTopologyTuple(const TopologyTuple& tuple);

    /**
     * 更新指定拓扑边的 P-OLSR 预测指标。
     */
    void UpdateTopologyPrediction(const Ipv4Address& destAddr,
                                  const Ipv4Address& lastAddr,
                                  double predictedCost,
                                  double forwardReceivingRatio,
                                  double reverseReceivingRatio,
                                  double relativeSpeed,
                                  Time predictionTime);

    /**
     * 获取指定拓扑边的预测代价。没有预测值时返回 1.0。
     */
    double GetTopologyPredictedCost(const Ipv4Address& destAddr,
                                    const Ipv4Address& lastAddr) const;

    // 接口关联相关接口

    /**
     * 获取接口关联集合。
     * @returns 接口关联集合。
     */
    const IfaceAssocSet& GetIfaceAssocSet() const
    {
        return m_ifaceAssocSet;
    }

    /**
     * 获取接口关联集合的可修改引用。
     * @returns 接口关联集合。
     */
    IfaceAssocSet& GetIfaceAssocSetMutable()
    {
        return m_ifaceAssocSet;
    }

    /**
     * 查找一个接口关联 tuple。
     * @param ifaceAddr 接口地址。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    IfaceAssocTuple* FindIfaceAssocTuple(const Ipv4Address& ifaceAddr);
    /**
     * 查找一个接口关联 tuple。
     * @param ifaceAddr 接口地址。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    const IfaceAssocTuple* FindIfaceAssocTuple(const Ipv4Address& ifaceAddr) const;
    /**
     * 删除一个接口关联 tuple。
     * @param tuple 要删除的 tuple。
     */
    void EraseIfaceAssocTuple(const IfaceAssocTuple& tuple);
    /**
     * 插入一个接口关联 tuple。
     * @param tuple 要插入的 tuple。
     */
    void InsertIfaceAssocTuple(const IfaceAssocTuple& tuple);

    // Host-Network Association（HNA）相关接口
    // 学习提示：m_associationSet 是“从别人那里听来的外部网络”，
    // m_associations 是“本节点自己要向别人通告的外部网络”。
    /**
     * 获取当前节点已知的关联集合。
     * @returns 当前节点已知的关联集合。
     */
    const AssociationSet& GetAssociationSet() const // 当前节点已知的关联集合。
    {
        return m_associationSet;
    }

    /**
     * 获取当前节点自身拥有的关联集合。
     * @returns 当前节点自身拥有的关联集合。
     */
    const Associations& GetAssociations() const // 当前节点自身拥有的关联集合。
    {
        return m_associations;
    }

    /**
     * 查找一个关联 tuple。
     * @param gatewayAddr 网关地址。
     * @param networkAddr 网络地址。
     * @param netmask 网络掩码。
     * @returns 如果匹配则返回 tuple，否则返回空指针。
     */
    AssociationTuple* FindAssociationTuple(const Ipv4Address& gatewayAddr,
                                           const Ipv4Address& networkAddr,
                                           const Ipv4Mask& netmask);
    /**
     * 删除一个已知关联 tuple。
     * @param tuple 要删除的 tuple。
     */
    void EraseAssociationTuple(const AssociationTuple& tuple);
    /**
     * 插入一个已知关联 tuple。
     * @param tuple 要插入的 tuple。
     */
    void InsertAssociationTuple(const AssociationTuple& tuple);
    /**
     * 删除一个本地关联。
     * @param tuple 要删除的 tuple。
     */
    void EraseAssociation(const Association& tuple);
    /**
     * 插入一个本地关联 tuple。
     * @param tuple 要插入的 tuple。
     */
    void InsertAssociation(const Association& tuple);

    /**
     * 返回指定邻居的全部接口地址，但不包含其“主地址”。
     * @param neighborMainAddr 邻居主地址。
     * @returns 邻居接口地址容器（不含主地址）。
     */
    std::vector<Ipv4Address> FindNeighborInterfaces(const Ipv4Address& neighborMainAddr) const;
};

} // namespace polsr
} // namespace ns3

#endif /* POLSR_STATE_H */
