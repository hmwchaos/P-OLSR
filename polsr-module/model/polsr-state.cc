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
/// @file polsr-state.cc
/// @brief Implementation of all functions needed for manipulating the internal
///        state of an POLSR node.
///

#include "polsr-state.h"

namespace ns3
{
namespace polsr
{

/********** P-OLSR Node Motion Set Manipulation **********/

NodeMotionTuple*
PolsrState::FindNodeMotionTuple(const Ipv4Address& mainAddr)
{
    for (auto it = m_nodeMotionSet.begin(); it != m_nodeMotionSet.end(); it++)
    {
        if (it->mainAddr == mainAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

const NodeMotionTuple*
PolsrState::FindNodeMotionTuple(const Ipv4Address& mainAddr) const
{
    for (auto it = m_nodeMotionSet.begin(); it != m_nodeMotionSet.end(); it++)
    {
        if (it->mainAddr == mainAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::InsertNodeMotionTuple(const NodeMotionTuple& tuple)
{
    for (auto it = m_nodeMotionSet.begin(); it != m_nodeMotionSet.end(); it++)
    {
        if (it->mainAddr == tuple.mainAddr)
        {
            *it = tuple;
            return;
        }
    }
    m_nodeMotionSet.push_back(tuple);
}

void
PolsrState::UpdateNodeMotionTuple(const Ipv4Address& mainAddr,
                                  const Vector& position,
                                  double longitude,
                                  double latitude,
                                  double altitude,
                                  Time updateTime,
                                  Time expirationTime)
{
    /*
     * P-OLSR 节点运动状态更新。
     *
     * 收到邻居 HELLO 后，如果 HELLO 中带有 GPS 坐标，就把它缓存到
     * NodeMotionSet。这里按 mainAddr 覆盖旧记录，因为对同一个邻居来说，
     * 最新 HELLO 中的坐标状态最接近当前拓扑。
     *
     * expirationTime 用来避免使用过期运动信息。若后续一段时间没有再收到
     * 该邻居的带运动信息 HELLO，则 PurgeExpiredNodeMotionTuples() 会清理它。
     */
    NodeMotionTuple tuple;
    tuple.mainAddr = mainAddr;
    tuple.position = position;
    tuple.longitude = longitude;
    tuple.latitude = latitude;
    tuple.altitude = altitude;
    tuple.updateTime = updateTime;
    tuple.expirationTime = expirationTime;
    tuple.valid = true;
    InsertNodeMotionTuple(tuple);
}

void
PolsrState::EraseNodeMotionTuple(const Ipv4Address& mainAddr)
{
    for (auto it = m_nodeMotionSet.begin(); it != m_nodeMotionSet.end();)
    {
        if (it->mainAddr == mainAddr)
        {
            it = m_nodeMotionSet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void
PolsrState::PurgeExpiredNodeMotionTuples(Time now)
{
    for (auto it = m_nodeMotionSet.begin(); it != m_nodeMotionSet.end();)
    {
        if (it->expirationTime <= now)
        {
            it = m_nodeMotionSet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

/********** MPR Selector Set Manipulation **********/

MprSelectorTuple*
PolsrState::FindMprSelectorTuple(const Ipv4Address& mainAddr)
{
    for (auto it = m_mprSelectorSet.begin(); it != m_mprSelectorSet.end(); it++)
    {
        if (it->mainAddr == mainAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseMprSelectorTuple(const MprSelectorTuple& tuple)
{
    for (auto it = m_mprSelectorSet.begin(); it != m_mprSelectorSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_mprSelectorSet.erase(it);
            break;
        }
    }
}

void
PolsrState::EraseMprSelectorTuples(const Ipv4Address& mainAddr)
{
    for (auto it = m_mprSelectorSet.begin(); it != m_mprSelectorSet.end();)
    {
        if (it->mainAddr == mainAddr)
        {
            it = m_mprSelectorSet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void
PolsrState::InsertMprSelectorTuple(const MprSelectorTuple& tuple)
{
    m_mprSelectorSet.push_back(tuple);
}

std::string
PolsrState::PrintMprSelectorSet() const
{
    std::ostringstream os;
    os << "[";
    for (auto iter = m_mprSelectorSet.begin(); iter != m_mprSelectorSet.end(); iter++)
    {
        auto next = iter;
        next++;
        os << iter->mainAddr;
        if (next != m_mprSelectorSet.end())
        {
            os << ", ";
        }
    }
    os << "]";
    return os.str();
}

/********** Neighbor Set Manipulation **********/

NeighborTuple*
PolsrState::FindNeighborTuple(const Ipv4Address& mainAddr)
{
    for (auto it = m_neighborSet.begin(); it != m_neighborSet.end(); it++)
    {
        if (it->neighborMainAddr == mainAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

const NeighborTuple*
PolsrState::FindSymNeighborTuple(const Ipv4Address& mainAddr) const
{
    for (auto it = m_neighborSet.begin(); it != m_neighborSet.end(); it++)
    {
        if (it->neighborMainAddr == mainAddr && it->status == NeighborTuple::STATUS_SYM)
        {
            return &(*it);
        }
    }
    return nullptr;
}

NeighborTuple*
PolsrState::FindNeighborTuple(const Ipv4Address& mainAddr, Willingness willingness)
{
    for (auto it = m_neighborSet.begin(); it != m_neighborSet.end(); it++)
    {
        if (it->neighborMainAddr == mainAddr && it->willingness == willingness)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseNeighborTuple(const NeighborTuple& tuple)
{
    for (auto it = m_neighborSet.begin(); it != m_neighborSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_neighborSet.erase(it);
            break;
        }
    }
}

void
PolsrState::EraseNeighborTuple(const Ipv4Address& mainAddr)
{
    for (auto it = m_neighborSet.begin(); it != m_neighborSet.end(); it++)
    {
        if (it->neighborMainAddr == mainAddr)
        {
            it = m_neighborSet.erase(it);
            break;
        }
    }
}

void
PolsrState::InsertNeighborTuple(const NeighborTuple& tuple)
{
    for (auto it = m_neighborSet.begin(); it != m_neighborSet.end(); it++)
    {
        if (it->neighborMainAddr == tuple.neighborMainAddr)
        {
            // Update it
            *it = tuple;
            return;
        }
    }
    m_neighborSet.push_back(tuple);
}

/********** Neighbor 2 Hop Set Manipulation **********/

TwoHopNeighborTuple*
PolsrState::FindTwoHopNeighborTuple(const Ipv4Address& neighborMainAddr,
                                   const Ipv4Address& twoHopNeighborAddr)
{
    for (auto it = m_twoHopNeighborSet.begin(); it != m_twoHopNeighborSet.end(); it++)
    {
        if (it->neighborMainAddr == neighborMainAddr &&
            it->twoHopNeighborAddr == twoHopNeighborAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple)
{
    for (auto it = m_twoHopNeighborSet.begin(); it != m_twoHopNeighborSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_twoHopNeighborSet.erase(it);
            break;
        }
    }
}

void
PolsrState::EraseTwoHopNeighborTuples(const Ipv4Address& neighborMainAddr,
                                     const Ipv4Address& twoHopNeighborAddr)
{
    for (auto it = m_twoHopNeighborSet.begin(); it != m_twoHopNeighborSet.end();)
    {
        if (it->neighborMainAddr == neighborMainAddr &&
            it->twoHopNeighborAddr == twoHopNeighborAddr)
        {
            it = m_twoHopNeighborSet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void
PolsrState::EraseTwoHopNeighborTuples(const Ipv4Address& neighborMainAddr)
{
    for (auto it = m_twoHopNeighborSet.begin(); it != m_twoHopNeighborSet.end();)
    {
        if (it->neighborMainAddr == neighborMainAddr)
        {
            it = m_twoHopNeighborSet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void
PolsrState::InsertTwoHopNeighborTuple(const TwoHopNeighborTuple& tuple)
{
    m_twoHopNeighborSet.push_back(tuple);
}

/********** MPR Set Manipulation **********/

bool
PolsrState::FindMprAddress(const Ipv4Address& addr)
{
    auto it = m_mprSet.find(addr);
    return (it != m_mprSet.end());
}

void
PolsrState::SetMprSet(MprSet mprSet)
{
    m_mprSet = mprSet;
}

MprSet
PolsrState::GetMprSet() const
{
    return m_mprSet;
}

/********** Duplicate Set Manipulation **********/

DuplicateTuple*
PolsrState::FindDuplicateTuple(const Ipv4Address& addr, uint16_t sequenceNumber)
{
    for (auto it = m_duplicateSet.begin(); it != m_duplicateSet.end(); it++)
    {
        if (it->address == addr && it->sequenceNumber == sequenceNumber)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseDuplicateTuple(const DuplicateTuple& tuple)
{
    for (auto it = m_duplicateSet.begin(); it != m_duplicateSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_duplicateSet.erase(it);
            break;
        }
    }
}

void
PolsrState::InsertDuplicateTuple(const DuplicateTuple& tuple)
{
    m_duplicateSet.push_back(tuple);
}

/********** Link Set Manipulation **********/

LinkTuple*
PolsrState::FindLinkTuple(const Ipv4Address& ifaceAddr)
{
    for (auto it = m_linkSet.begin(); it != m_linkSet.end(); it++)
    {
        if (it->neighborIfaceAddr == ifaceAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

LinkTuple*
PolsrState::FindLinkTuple(const Ipv4Address& localIfaceAddr,
                          const Ipv4Address& neighborIfaceAddr)
{
    for (auto it = m_linkSet.begin(); it != m_linkSet.end(); it++)
    {
        if (it->localIfaceAddr == localIfaceAddr && it->neighborIfaceAddr == neighborIfaceAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

LinkTuple*
PolsrState::FindSymLinkTuple(const Ipv4Address& ifaceAddr, Time now)
{
    for (auto it = m_linkSet.begin(); it != m_linkSet.end(); it++)
    {
        if (it->neighborIfaceAddr == ifaceAddr)
        {
            if (it->symTime > now)
            {
                return &(*it);
            }
            else
            {
                break;
            }
        }
    }
    return nullptr;
}

void
PolsrState::EraseLinkTuple(const LinkTuple& tuple)
{
    for (auto it = m_linkSet.begin(); it != m_linkSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_linkSet.erase(it);
            break;
        }
    }
}

LinkTuple&
PolsrState::InsertLinkTuple(const LinkTuple& tuple)
{
    m_linkSet.push_back(tuple);
    return m_linkSet.back();
}

void
PolsrState::UpdateLinkPrediction(const Ipv4Address& localIfaceAddr,
                                 const Ipv4Address& neighborIfaceAddr,
                                 double phi,
                                 double rho,
                                 double relativeSpeed,
                                 double predictedCost,
                                 Time predictionTime)
{
    /*
     * P-OLSR 链路预测状态更新。
     *
     * 原始 OLSR 对 LinkTuple 的更新只关心 symTime/asymTime/time。
     * P-OLSR 在不改变这些原有字段含义的前提下，把预测得到的链路质量、
     * 相对速度和 ETX 代价写回同一个 LinkTuple。
     *
     * 找不到链路时直接返回：预测信息必须依附于已经由 LinkSensing()
     * 建立的一跳链路，不能凭空创建链路，否则会破坏 OLSR 的邻居发现语义。
     */
    LinkTuple* tuple = FindLinkTuple(localIfaceAddr, neighborIfaceAddr);
    if (!tuple)
    {
        return;
    }

    tuple->phi = phi;
    tuple->rho = rho;
    tuple->relativeSpeed = relativeSpeed;
    tuple->predictedCost = predictedCost;
    tuple->predictionTime = predictionTime;
    tuple->predictionValid = true;
}

double
PolsrState::GetLinkPredictedCost(const Ipv4Address& localIfaceAddr,
                                 const Ipv4Address& neighborIfaceAddr) const
{
    for (auto it = m_linkSet.begin(); it != m_linkSet.end(); it++)
    {
        if (it->localIfaceAddr == localIfaceAddr && it->neighborIfaceAddr == neighborIfaceAddr)
        {
            return it->predictionValid ? it->predictedCost : 1.0;
        }
    }
    return 1.0;
}

/********** Topology Set Manipulation **********/

TopologyTuple*
PolsrState::FindTopologyTuple(const Ipv4Address& destAddr, const Ipv4Address& lastAddr)
{
    for (auto it = m_topologySet.begin(); it != m_topologySet.end(); it++)
    {
        if (it->destAddr == destAddr && it->lastAddr == lastAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

TopologyTuple*
PolsrState::FindNewerTopologyTuple(const Ipv4Address& lastAddr, uint16_t ansn)
{
    for (auto it = m_topologySet.begin(); it != m_topologySet.end(); it++)
    {
        if (it->lastAddr == lastAddr && it->sequenceNumber > ansn)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseTopologyTuple(const TopologyTuple& tuple)
{
    for (auto it = m_topologySet.begin(); it != m_topologySet.end(); it++)
    {
        if (*it == tuple)
        {
            m_topologySet.erase(it);
            break;
        }
    }
}

void
PolsrState::EraseOlderTopologyTuples(const Ipv4Address& lastAddr, uint16_t ansn)
{
    for (auto it = m_topologySet.begin(); it != m_topologySet.end();)
    {
        if (it->lastAddr == lastAddr && it->sequenceNumber < ansn)
        {
            it = m_topologySet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void
PolsrState::InsertTopologyTuple(const TopologyTuple& tuple)
{
    m_topologySet.push_back(tuple);
}

void
PolsrState::UpdateTopologyPrediction(const Ipv4Address& destAddr,
                                     const Ipv4Address& lastAddr,
                                     double predictedCost,
                                     double forwardReceivingRatio,
                                     double reverseReceivingRatio,
                                     double relativeSpeed,
                                     Time predictionTime)
{
    /*
     * P-OLSR 拓扑预测状态更新。
     *
     * ProcessTc() 先按照原始 OLSR 规则创建或刷新 TopologyTuple，
     * 然后再调用这里把 TC 携带的预测代价写入该拓扑边。
     * 如果找不到对应拓扑边，则说明标准 TC 处理没有接受这条拓扑关系，
     * 此时不能单独为了预测指标创建 TopologyTuple。
     */
    TopologyTuple* tuple = FindTopologyTuple(destAddr, lastAddr);
    if (!tuple)
    {
        return;
    }

    tuple->predictedCost = predictedCost;
    tuple->forwardReceivingRatio = forwardReceivingRatio;
    tuple->reverseReceivingRatio = reverseReceivingRatio;
    tuple->relativeSpeed = relativeSpeed;
    tuple->predictionTime = predictionTime;
    tuple->predictionValid = true;
}

double
PolsrState::GetTopologyPredictedCost(const Ipv4Address& destAddr,
                                     const Ipv4Address& lastAddr) const
{
    for (auto it = m_topologySet.begin(); it != m_topologySet.end(); it++)
    {
        if (it->destAddr == destAddr && it->lastAddr == lastAddr)
        {
            return it->predictionValid ? it->predictedCost : 1.0;
        }
    }
    return 1.0;
}

/********** Interface Association Set Manipulation **********/

IfaceAssocTuple*
PolsrState::FindIfaceAssocTuple(const Ipv4Address& ifaceAddr)
{
    for (auto it = m_ifaceAssocSet.begin(); it != m_ifaceAssocSet.end(); it++)
    {
        if (it->ifaceAddr == ifaceAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

const IfaceAssocTuple*
PolsrState::FindIfaceAssocTuple(const Ipv4Address& ifaceAddr) const
{
    for (auto it = m_ifaceAssocSet.begin(); it != m_ifaceAssocSet.end(); it++)
    {
        if (it->ifaceAddr == ifaceAddr)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseIfaceAssocTuple(const IfaceAssocTuple& tuple)
{
    for (auto it = m_ifaceAssocSet.begin(); it != m_ifaceAssocSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_ifaceAssocSet.erase(it);
            break;
        }
    }
}

void
PolsrState::InsertIfaceAssocTuple(const IfaceAssocTuple& tuple)
{
    m_ifaceAssocSet.push_back(tuple);
}

std::vector<Ipv4Address>
PolsrState::FindNeighborInterfaces(const Ipv4Address& neighborMainAddr) const
{
    std::vector<Ipv4Address> retval;
    for (auto it = m_ifaceAssocSet.begin(); it != m_ifaceAssocSet.end(); it++)
    {
        if (it->mainAddr == neighborMainAddr)
        {
            retval.push_back(it->ifaceAddr);
        }
    }
    return retval;
}

/********** Host-Network Association Set Manipulation **********/

AssociationTuple*
PolsrState::FindAssociationTuple(const Ipv4Address& gatewayAddr,
                                const Ipv4Address& networkAddr,
                                const Ipv4Mask& netmask)
{
    for (auto it = m_associationSet.begin(); it != m_associationSet.end(); it++)
    {
        if (it->gatewayAddr == gatewayAddr and it->networkAddr == networkAddr and
            it->netmask == netmask)
        {
            return &(*it);
        }
    }
    return nullptr;
}

void
PolsrState::EraseAssociationTuple(const AssociationTuple& tuple)
{
    for (auto it = m_associationSet.begin(); it != m_associationSet.end(); it++)
    {
        if (*it == tuple)
        {
            m_associationSet.erase(it);
            break;
        }
    }
}

void
PolsrState::InsertAssociationTuple(const AssociationTuple& tuple)
{
    m_associationSet.push_back(tuple);
}

void
PolsrState::EraseAssociation(const Association& tuple)
{
    for (auto it = m_associations.begin(); it != m_associations.end(); it++)
    {
        if (*it == tuple)
        {
            m_associations.erase(it);
            break;
        }
    }
}

void
PolsrState::InsertAssociation(const Association& tuple)
{
    m_associations.push_back(tuple);
}

} // namespace polsr
} // namespace ns3
