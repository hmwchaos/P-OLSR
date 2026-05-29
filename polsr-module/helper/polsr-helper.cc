/*
 * Copyright (c) 2008 INRIA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "polsr-helper.h"

#include "ns3/ipv4-list-routing.h"
#include "ns3/names.h"
#include "ns3/node-list.h"
#include "polsr-routing-protocol.h"
#include "ns3/ptr.h"

namespace ns3
{

PolsrHelper::PolsrHelper()
{
    m_agentFactory.SetTypeId("ns3::polsr::RoutingProtocol");//设置对象工厂的类型 ID 为 ns3::polsr::RoutingProtocol，以便创建 POLSR 路由协议实例
}

PolsrHelper::PolsrHelper(const PolsrHelper& o)
    : m_agentFactory(o.m_agentFactory)
{
    m_interfaceExclusions = o.m_interfaceExclusions;
}

PolsrHelper*
PolsrHelper::Copy() const
{
    return new PolsrHelper(*this);
}

void
PolsrHelper::ExcludeInterface(Ptr<Node> node, uint32_t interface)
{
    auto it = m_interfaceExclusions.find(node);

    if (it == m_interfaceExclusions.end())
    {
        std::set<uint32_t> interfaces;
        interfaces.insert(interface);

        m_interfaceExclusions.insert(std::make_pair(node, std::set<uint32_t>(interfaces)));
    }
    else
    {
        it->second.insert(interface);
    }
}

Ptr<Ipv4RoutingProtocol>
PolsrHelper::Create(Ptr<Node> node) const
{
    Ptr<polsr::RoutingProtocol> agent = m_agentFactory.Create<polsr::RoutingProtocol>();//如果之前不SetTypeId()工厂不知道要创建什么类型，会触发 ns-3 的断言错误。

    auto it = m_interfaceExclusions.find(node);

    if (it != m_interfaceExclusions.end())
    {
        agent->SetInterfaceExclusions(it->second);
    }

    node->AggregateObject(agent);//AggregateObject() 是 ns-3 的对象聚合机制的一部分，它允许将一个对象（这里是 POLSR 路由协议实例）聚合到另一个对象（这里是节点）中。这样，节点就可以通过 GetObject<polsr::RoutingProtocol>() 来访问这个 POLSR 实例。
    return agent;
}

void
PolsrHelper::Set(std::string name, const AttributeValue& value)
{
    m_agentFactory.Set(name, value);
}

int64_t
PolsrHelper::AssignStreams(NodeContainer c, int64_t stream)
{
    int64_t currentStream = stream;
    Ptr<Node> node;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        node = (*i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");
        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");
        Ptr<polsr::RoutingProtocol> polsr = DynamicCast<polsr::RoutingProtocol>(proto);
        if (polsr)
        {
            currentStream += polsr->AssignStreams(currentStream);
            continue;
        }
        // Polsr may also be in a list
        Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
        if (list)
        {
            int16_t priority;
            Ptr<Ipv4RoutingProtocol> listProto;
            Ptr<polsr::RoutingProtocol> listPolsr;
            for (uint32_t i = 0; i < list->GetNRoutingProtocols(); i++)
            {
                listProto = list->GetRoutingProtocol(i, priority);
                listPolsr = DynamicCast<polsr::RoutingProtocol>(listProto);
                if (listPolsr)
                {
                    currentStream += listPolsr->AssignStreams(currentStream);
                    break;
                }
            }
        }
    }
    return (currentStream - stream);
}

} // namespace ns3
