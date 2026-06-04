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
/*  repositories.h = 数据结构定义
    state.h        = 状态管理接口,对这些表的增删查改
    state.cc       = 状态管理接口的具体实现
    header.h       = POLSR 消息头部定义,以及这些消息怎么在网络中被打包和解包。
*/
#ifndef POLSR_REPOSITORIES_H
#define POLSR_REPOSITORIES_H

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/vector.h"

#include <iostream>
#include <set>
#include <vector>

namespace ns3
{
namespace polsr
{

/**
 * @ingroup polsr
 *
 * 节点替其他节点转发数据包的意愿值。
 * 标准定义了如下取值集合。
 * 标准允许 0 - 7 的取值，但代码中没有强制校验这个范围。
 *
 * 见 \RFC{3626} 第 18.8 节。
 */
enum Willingness : uint8_t
{
    NEVER = 0,
    LOW = 1,
    DEFAULT = 3, // 中等
    HIGH = 6,
    ALWAYS = 7,
};

/**
 * POLSR willingness 的流输出运算符。
 *
 * @param os 输出流。
 * @param willingness willingness 值。
 * @return 输出流引用。
 */
inline std::ostream&
operator<<(std::ostream& os, Willingness willingness)
{
    switch (willingness)
    {
    case Willingness::NEVER:
        return (os << "NEVER");
    case Willingness::LOW:
        return (os << "LOW");
    case Willingness::DEFAULT:
        return (os << "DEFAULT");
    case Willingness::HIGH:
        return (os << "HIGH");
    case Willingness::ALWAYS:
        return (os << "ALWAYS");
    default:
        return (os << static_cast<uint32_t>(willingness)); // 转成 uint32_t 以便正确打印。
    }
    return os;
}

/// @ingroup polsr
/// 接口关联 tuple。
/// 学习提示：当一个节点有多个接口地址时，POLSR 需要把这些接口地址映射回同一个主地址。
/// 它用来记录:某个接口地址 ifaceAddr 属于哪个节点主地址 mainAddr,通常从 POLSR 的 MID 消息里学到。
/// 不一定是本节点自己的接口地址和主地址，也可能是其他节点的接口地址和主地址对应关系。
struct IfaceAssocTuple
{
    /// 节点的接口地址。
    Ipv4Address ifaceAddr;
    /// 节点主地址。
    Ipv4Address mainAddr;
    /// tuple 到期并需要被删除的时间。
    Time time;
};

//规定两个接口关联元组怎么判断相等
inline bool
operator==(const IfaceAssocTuple& a, const IfaceAssocTuple& b)
{
    return (a.ifaceAddr == b.ifaceAddr && a.mainAddr == b.mainAddr);
}
//规定接口关联元组怎么打印输出
inline std::ostream&
operator<<(std::ostream& os, const IfaceAssocTuple& tuple)
{
    os << "IfaceAssocTuple(ifaceAddr=" << tuple.ifaceAddr << ", mainAddr=" << tuple.mainAddr
       << ", time=" << tuple.time << ")";
    return os;
}

/// @ingroup polsr
/// 链路 tuple。
/// 学习提示：它描述“本地某接口”和“邻居某接口”之间的链路状态，
/// 是 HELLO 链路感知的底层依据。
/// 两个接口之间的连接关系
struct LinkTuple
{
    /// 本地节点接口地址。
    Ipv4Address localIfaceAddr;
    /// 邻居节点接口地址。
    Ipv4Address neighborIfaceAddr;
    /// 在该时间之前，链路被认为是双向可达。
    Time symTime;
    /// 在该时间之前，链路被认为是单向可达。
    Time asymTime;
    /// tuple 到期并需要被删除的时间。
    Time time;

    /*
     * P-OLSR 扩展字段说明：
     *
     * 原始 OLSR 的 LinkTuple 只回答“这个邻居接口现在是否可达 / 是否对称”。
     * P-OLSR 还需要回答“这条链路接下来是否稳定，适不适合拿来转发数据”。
     * 因此这里把链路预测所需的中间量直接放进 LinkTuple。
     *
     * 这些字段不参与 LinkTuple 的相等性判断，避免破坏原 OLSR 按
     * localIfaceAddr + neighborIfaceAddr 定位链路的逻辑。
     */

    /// P-OLSR: HELLO 接收质量的平滑值，越接近 1 表示近期 HELLO 越稳定。
    double phi{0.0};
    /// P-OLSR: 反向接收率的平滑值，表示邻居接收本节点 HELLO 的质量。
    double rho{0.0};
    /// P-OLSR: 本节点和邻居沿连线方向的相对速度，正值表示距离正在变大。
    double relativeSpeed{0.0};
    /// P-OLSR: 最终链路预测代价。数值越小越适合作为路由边。
    double predictedCost{1.0};
    /// P-OLSR: 上一次更新上述预测字段的仿真时间。
    Time predictionTime;
    /// P-OLSR: Next nominal HELLO arrival used by Expected Arrival Time decay.
    Time nextExpectedHelloTime;
    /// P-OLSR: HELLO loss deadline after jitter tolerance.
    Time helloDeadlineTime;
    /// P-OLSR: Whether nextExpectedHelloTime and helloDeadlineTime are initialized.
    bool helloDeadlineValid{false};
    /// P-OLSR: 是否已经收到过足够的信息，可把 predictedCost 当作有效值。
    bool predictionValid{false};
    /// P-OLSR: 上一次本节点和邻居之间的距离，用于估算距离变化率。
    double previousDistance{0.0};
    /// P-OLSR: 上一次距离采样时间。
    Time previousPredictionTime;
    /// P-OLSR: 是否已经有上一轮距离采样。
    bool previousDistanceValid{false};
};

inline bool
operator==(const LinkTuple& a, const LinkTuple& b)
{
    return (a.localIfaceAddr == b.localIfaceAddr && a.neighborIfaceAddr == b.neighborIfaceAddr);
}

inline std::ostream&
operator<<(std::ostream& os, const LinkTuple& tuple)
{
    os << "LinkTuple(localIfaceAddr=" << tuple.localIfaceAddr
       << ", neighborIfaceAddr=" << tuple.neighborIfaceAddr << ", symTime=" << tuple.symTime
       << ", asymTime=" << tuple.asymTime << ", expTime=" << tuple.time
       << ", predictedCost=" << tuple.predictedCost << ", phi=" << tuple.phi
       << ", rho=" << tuple.rho << ", relativeSpeed=" << tuple.relativeSpeed << ")";
    return os;
}

/// @ingroup polsr
/// 邻居 tuple。
/// 学习提示：LinkTuple 偏向“接口级链路”，NeighborTuple 偏向“节点级邻居”。
/// 一跳邻居表
struct NeighborTuple
{
    /// 邻居节点主地址。
    Ipv4Address neighborMainAddr;

    /// 链路状态（对称或非对称）。
    enum Status
    {
        STATUS_NOT_SYM = 0, // 非对称
        STATUS_SYM = 1,     // 对称
    };

    /// 链路状态。
    Status status;

    /// 0 到 7 之间的取值，表示这个邻居节点替其他节点转发流量的意愿。
    Willingness willingness;
};

inline bool
operator==(const NeighborTuple& a, const NeighborTuple& b)
{
    return (a.neighborMainAddr == b.neighborMainAddr && a.status == b.status &&
            a.willingness == b.willingness);
}

inline std::ostream&
operator<<(std::ostream& os, const NeighborTuple& tuple)
{
    os << "NeighborTuple(neighborMainAddr=" << tuple.neighborMainAddr
       << ", status=" << (tuple.status == NeighborTuple::STATUS_SYM ? "SYM" : "NOT_SYM")
       << ", willingness=" << tuple.willingness << ")";
    return os;
}

/// @ingroup polsr
/// 二跳邻居 tuple。
/// 学习提示：MPR 的选择离不开二跳邻居信息；本节点要用尽量少的一跳邻居覆盖全部二跳邻居。
/// 通过上述一跳邻居可达的二跳邻居
struct TwoHopNeighborTuple
{
    /// 一跳邻居主地址。
    Ipv4Address neighborMainAddr;
    /// 通过上述一跳邻居可达的二跳邻居主地址，一跳邻居 和 二跳邻居之间是对称链路。
    Ipv4Address twoHopNeighborAddr;
    /// tuple 到期并需要被删除的时间。
    Time expirationTime; // 之前名为 time_
};

inline std::ostream&
operator<<(std::ostream& os, const TwoHopNeighborTuple& tuple)
{
    os << "TwoHopNeighborTuple(neighborMainAddr=" << tuple.neighborMainAddr
       << ", twoHopNeighborAddr=" << tuple.twoHopNeighborAddr
       << ", expirationTime=" << tuple.expirationTime << ")";
    return os;
}

inline bool
operator==(const TwoHopNeighborTuple& a, const TwoHopNeighborTuple& b)
{
    return (a.neighborMainAddr == b.neighborMainAddr &&
            a.twoHopNeighborAddr == b.twoHopNeighborAddr);
}

/// @ingroup polsr
/// MPR selector tuple。
/// 学习提示：MprSet 表示“我选了谁做 MPR”，MprSelectorSet 表示“谁选了我做 MPR”。
/// MPR 选择者记录
struct MprSelectorTuple
{
    /// 选择了本节点作为 MPR 的节点主地址。
    Ipv4Address mainAddr;
    /// tuple 到期并需要被删除的时间。
    Time expirationTime; // 之前名为 time_
};

inline bool
operator==(const MprSelectorTuple& a, const MprSelectorTuple& b)
{
    return (a.mainAddr == b.mainAddr);
}

// “接口地址列表”类型。
// typedef std::vector<nsaddr_t> addr_list_t;

/// @ingroup polsr
/// 重复消息 tuple。
/// 学习提示：用于识别控制消息是否已处理/转发过，避免泛洪时反复处理同一消息。
/// 记录已经处理过的消息的源地址和序列号，以便识别重复消息
struct DuplicateTuple
{
    /// 消息源发起者地址。
    Ipv4Address address;
    /// 消息序列号。
    uint16_t sequenceNumber;
    /// 标记该消息是否已经被重传。
    bool retransmitted;
    /// 可以存放多个 Ipv4Address 的动态数组,如果同一条消息分别从接口1和接口2收到过,可能ifaceList = [10.1.1.2, 10.1.2.2].    
    std::vector<Ipv4Address> ifaceList;
    /// tuple 到期并需要被删除的时间.防止表太大.
    Time expirationTime;
};

inline bool
operator==(const DuplicateTuple& a, const DuplicateTuple& b)
{
    return (a.address == b.address && a.sequenceNumber == b.sequenceNumber);
}

/// @ingroup polsr
/// 拓扑 tuple。
/// 学习提示：TC 消息会把网络中的拓扑关系逐步填入这里，后续据此计算路由表。
/// 如果要到 A(destAddr)，拓扑上可以通过 C(lastAddr) 这个节点接近 A
struct TopologyTuple
{
    /// 目的节点主地址。
    Ipv4Address destAddr;
    /// 目的节点的邻居节点主地址。
    Ipv4Address lastAddr;
    /// TC 消息的序列号,避免旧 TC 消息把新的拓扑表改乱。
    uint16_t sequenceNumber;
    /// tuple 到期并需要被删除的时间。如果超过 expirationTime 还没有收到新的 TC 消息更新它，就说明这条拓扑关系可能已经失效，需要删除.
    Time expirationTime;

    /*
     * P-OLSR 拓扑扩展：
     *
     * 原始 TC 只传播“某个节点通告了哪个邻居”，路由计算时默认每条边代价为 1。
     * P-OLSR 需要把链路预测代价也随 TC 存进 TopologyTuple，
     * 这样非一跳的拓扑边在路由计算时也可以使用预测代价，而不是只看跳数。
     */

    /// P-OLSR: TC 中通告的这条拓扑边的预测路由代价。
    double predictedCost{1.0};
    /// P-OLSR: TC 中通告的这条拓扑边的正向接收率。
    double forwardReceivingRatio{1.0};
    /// P-OLSR: TC 中通告的这条拓扑边的反向接收率。
    double reverseReceivingRatio{1.0};
    /// P-OLSR: TC 中通告的这条拓扑边的相对速度。
    double relativeSpeed{0.0};
    /// P-OLSR: TC 是否携带了可用的预测字段；没有携带时路由代价退化为 1。
    bool predictionValid{false};
    /// P-OLSR: 收到这组预测字段的仿真时间。
    Time predictionTime;
};

inline bool
operator==(const TopologyTuple& a, const TopologyTuple& b)
{
    return (a.destAddr == b.destAddr && a.lastAddr == b.lastAddr &&
            a.sequenceNumber == b.sequenceNumber);
}

inline std::ostream&
operator<<(std::ostream& os, const TopologyTuple& tuple)
{
    os << "TopologyTuple(destAddr=" << tuple.destAddr << ", lastAddr=" << tuple.lastAddr
       << ", sequenceNumber=" << (int)tuple.sequenceNumber
       << ", expirationTime=" << tuple.expirationTime
       << ", predictedCost=" << tuple.predictedCost << ", phi=" << tuple.forwardReceivingRatio
       << ", rho=" << tuple.reverseReceivingRatio
       << ", relativeSpeed=" << tuple.relativeSpeed << ")";
    return os;
}

/// @ingroup polsr
/// P-OLSR node GPS tuple.
/// It caches the local-coordinate GPS fields learned from HELLO messages.
///
/// 学习提示：
/// 原始 OLSR 不关心节点坐标，只依赖 HELLO 判断链路是否存在。
/// P-OLSR 要预测链路未来是否会断开，因此需要缓存邻居通过 HELLO
/// 发来的经度、纬度和高度。这里不直接参与路由表输出，而是供
/// UpdatePredictionFromHello() 计算 relativeSpeed 和 predictedCost。
struct NodeMotionTuple
{
    /// Node main address.
    Ipv4Address mainAddr;
    /// Last advertised position mapped as (longitude, latitude, altitude).
    Vector position;
    /// Last advertised longitude.
    double longitude{0.0};
    /// Last advertised latitude.
    double latitude{0.0};
    /// Last advertised altitude.
    double altitude{0.0};
    /// Time when the tuple was updated.
    Time updateTime;
    /// Expiration time for the cached motion information.
    Time expirationTime;
    /// Whether the motion values are valid.
    bool valid{false};
};

inline bool
operator==(const NodeMotionTuple& a, const NodeMotionTuple& b)
{
    return (a.mainAddr == b.mainAddr);
}

inline std::ostream&
operator<<(std::ostream& os, const NodeMotionTuple& tuple)
{
    os << "NodeMotionTuple(mainAddr=" << tuple.mainAddr << ", position=" << tuple.position
       << ", longitude=" << tuple.longitude << ", latitude=" << tuple.latitude
       << ", altitude=" << tuple.altitude << ", updateTime=" << tuple.updateTime
       << ", expirationTime=" << tuple.expirationTime << ", valid=" << tuple.valid << ")";
    return os;
}

/// @ingroup polsr
/// 本地网络关联。
/// 学习提示：只记录“一个外部网络地址段”,不记录“通过哪个网关可达这个网络地址段”
/// 本节点自己准备放进 HNA 消息里通告出去的信息。
struct Association
{
    Ipv4Address networkAddr; //!< IPv4 网络地址。
    Ipv4Mask netmask;        //!< IPv4 网络掩码。
};

inline bool
operator==(const Association& a, const Association& b)
{
    return (a.networkAddr == b.networkAddr && a.netmask == b.netmask);
}

inline std::ostream&
operator<<(std::ostream& os, const Association& tuple)
{
    os << "Association(networkAddr=" << tuple.networkAddr << ", netmask=" << tuple.netmask << ")";
    return os;
}

/// @ingroup polsr
/// HNA 关联 tuple。
/// 学习提示：用于描述“某个网关可以到达某个外部网络”，是 HNA 的状态基础。
/// 这是本节点从 HNA 消息里学到的信息,记录某个可达的外部网络地址段,以及通过哪个网关可达这个网络地址段
struct AssociationTuple
{
    /// 网关主地址。
    Ipv4Address gatewayAddr;
    /// 通过 gatewayAddr 可达的网络地址。
    Ipv4Address networkAddr;
    /// 通过 gatewayAddr 可达网络的掩码。
    Ipv4Mask netmask;
    /// tuple 到期并需要被删除的时间。
    Time expirationTime;
};

inline bool
operator==(const AssociationTuple& a, const AssociationTuple& b)
{
    return (a.gatewayAddr == b.gatewayAddr && a.networkAddr == b.networkAddr &&
            a.netmask == b.netmask);
}

inline std::ostream&
operator<<(std::ostream& os, const AssociationTuple& tuple)
{
    os << "AssociationTuple(gatewayAddr=" << tuple.gatewayAddr
       << ", networkAddr=" << tuple.networkAddr << ", netmask=" << tuple.netmask
       << ", expirationTime=" << tuple.expirationTime << ")";
    return os;
}

// 学习提示：tuple 是“一条记录”，Set 是“同类记录的集合”。
// repositories.h 只定义这些记录长什么样，不负责具体增删查改。
typedef std::set<Ipv4Address> MprSet;                       //!< MPR 集合类型。
typedef std::vector<MprSelectorTuple> MprSelectorSet;       //!< MPR selector 集合类型。
typedef std::vector<LinkTuple> LinkSet;                     //!< 链路集合类型。
typedef std::vector<NeighborTuple> NeighborSet;             //!< 邻居集合类型。
typedef std::vector<TwoHopNeighborTuple> TwoHopNeighborSet; //!< 二跳邻居集合类型。
typedef std::vector<TopologyTuple> TopologySet;             //!< 拓扑集合类型。
typedef std::vector<NodeMotionTuple> NodeMotionSet;         //!< P-OLSR 节点运动状态集合类型。
typedef std::vector<DuplicateTuple> DuplicateSet;           //!< 重复消息集合类型。
typedef std::vector<IfaceAssocTuple> IfaceAssocSet;         //!< 接口关联集合类型。
typedef std::vector<AssociationTuple> AssociationSet;       //!< HNA 关联集合类型。
typedef std::vector<Association> Associations;              //!< 本地主机网络关联集合类型。

} // namespace polsr
} // namespace ns3

#endif /* POLSR_REPOSITORIES_H */
