/*
 * Copyright (c) 2008 INRIA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#ifndef POLSR_HELPER_H
#define POLSR_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/object-factory.h"

#include <map>//包含 map 头文件，用于存储节点和接口的映射关系
#include <set>//包含 set 头文件，用于存储接口集合

namespace ns3
{

/**
 * @ingroup polsr
 *
 * @brief Helper class that adds POLSR routing to nodes.
 *
 * This class is expected to be used in conjunction with
 * ns3::InternetStackHelper::SetRoutingHelper
 */
class PolsrHelper : public Ipv4RoutingHelper//继承自 Ipv4RoutingHelper，提供了安装 POLSR 路由协议的功能
{
  public:
    /**
     * Create an PolsrHelper that makes life easier for people who want to install
     * POLSR routing to nodes.
     */
    PolsrHelper();//默认构造函数，创建一个 PolsrHelper 实例

    /**
     * @brief Construct an PolsrHelper from another previously initialized instance
     * (Copy Constructor).
     *
     * @param o object to copy
     */
    PolsrHelper(const PolsrHelper& o);//复制构造函数，从另一个 PolsrHelper 实例创建一个新的 PolsrHelper 实例

    // Delete assignment operator to avoid misuse
    PolsrHelper& operator=(const PolsrHelper&) = delete;//禁止赋值操作符，避免误用

    /**
     * @returns pointer to clone of this PolsrHelper
     *
     * This method is mainly for internal use by the other helpers;
     * clients are expected to free the dynamic memory allocated by this method
     */
    PolsrHelper* Copy() const override;//复制当前 PolsrHelper 实例，返回一个新的 PolsrHelper 指针

    /**
     * @param node the node for which an exception is to be defined
     * @param interface an interface of node on which POLSR is not to be installed
     *
     * This method allows the user to specify an interface on which POLSR is not to be installed on
     */
    void ExcludeInterface(Ptr<Node> node, uint32_t interface);//指定一个节点的接口，使 POLSR 不安装在该接口上,不会重复安装同一接口

    /**
     * @param node the node on which the routing protocol will run
     * @returns a newly-created routing protocol
     *
     * This method will be called by ns3::InternetStackHelper::Install
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;//创建一个新的 POLSR 路由协议实例，并返回一个指向该实例的智能指针

    /**
     * @param name the name of the attribute to set
     * @param value the value of the attribute to set.
     *
     * This method controls the attributes of ns3::polsr::RoutingProtocol
     */
    void Set(std::string name, const AttributeValue& value);//设置 POLSR 路由协议的属性，通过属性名称和值进行设置

    /**
     * Assign a fixed random variable stream number to the random variables
     * used by this model.  Return the number of streams (possibly zero) that
     * have been assigned.  The Install() method of the InternetStackHelper
     * should have previously been called by the user.
     *
     * @param stream first stream index to use
     * @param c NodeContainer of the set of nodes for which the PolsrRoutingProtocol
     *          should be modified to use a fixed stream
     * @return the number of stream indices assigned by this helper
     */
    int64_t AssignStreams(NodeContainer c, int64_t stream);//为 POLSR 路由协议使用的随机变量分配一个固定的随机数流编号，返回分配的流数量

  private:
    ObjectFactory m_agentFactory; //!< Object factory//对象工厂，用于创建 POLSR 路由协议实例

    std::map<Ptr<Node>, std::set<uint32_t>>//存储节点和接口的映射关系，表示哪些接口被排除在 POLSR 安装之外
        m_interfaceExclusions; //!< container of interfaces excluded from POLSR operations
};

} // namespace ns3

#endif /* POLSR_HELPER_H */
