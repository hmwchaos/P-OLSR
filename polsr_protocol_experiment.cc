/*
 * Protocol-level ns-3 experiment for the P-OLSR paper reproduction.
 *
 * This executable compares native ns-3 OLSR with the local P-OLSR copy in
 * scratch/P-OLSR/polsr-module. It does not modify ns-3 src/olsr.
 */

#include "polsr-module/helper/polsr-helper.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/wifi-module.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;

namespace
{

constexpr uint16_t kPort = 9000;
constexpr double kPi = 3.14159265358979323846;

Vector
LinkPosition(uint32_t nodeIndex, double t)
{
    if (nodeIndex == 0)
    {
        return Vector(0.0, 0.0, 10.0);
    }

    const double phase = std::fmod(t, 90.0);
    const double x = (phase <= 45.0) ? -450.0 * phase / 45.0
                                     : -450.0 * (1.0 - (phase - 45.0) / 45.0);
    const double turnOffset = 30.0 * std::sin(2.0 * kPi * phase / 90.0);
    return Vector(x, turnOffset, 75.0);
}

Vector
FieldPosition(uint32_t nodeIndex, double t)
{
    if (nodeIndex == 0)
    {
        return Vector(0.0, 0.0, 10.0);
    }
    if (nodeIndex == 1)
    {
        const double phase = std::fmod(t, 120.0);
        const double x = (phase <= 60.0) ? -600.0 * phase / 60.0
                                         : -600.0 * (1.0 - (phase - 60.0) / 60.0);
        return Vector(x, 0.0, 75.0);
    }

    const double theta = (12.0 / 30.0) * t;
    return Vector(-250.0 + 30.0 * std::cos(theta), 30.0 * std::sin(theta), 75.0);
}

Vector
LargePosition(uint32_t nodeIndex, double t)
{
    if (nodeIndex == 1)
    {
        const double phase = std::fmod(t, 380.0) / 380.0;
        if (phase < 0.25)
        {
            const double u = phase / 0.25;
            return Vector(-900.0 + 900.0 * u, -220.0, 75.0);
        }
        if (phase < 0.5)
        {
            const double u = (phase - 0.25) / 0.25;
            return Vector(0.0, -220.0 + 440.0 * u, 75.0);
        }
        if (phase < 0.75)
        {
            const double u = (phase - 0.5) / 0.25;
            return Vector(-900.0 * u, 220.0, 75.0);
        }
        const double u = (phase - 0.75) / 0.25;
        return Vector(-900.0, 220.0 - 440.0 * u, 75.0);
    }

    static const std::vector<Vector> centers = {
        Vector(0.0, 0.0, 75.0),       Vector(0.0, 0.0, 75.0),
        Vector(-150.0, -180.0, 75.0), Vector(-300.0, -180.0, 75.0),
        Vector(-450.0, -180.0, 75.0), Vector(-600.0, -180.0, 75.0),
        Vector(-150.0, -60.0, 75.0),  Vector(-300.0, -60.0, 75.0),
        Vector(-450.0, -60.0, 75.0),  Vector(-600.0, -60.0, 75.0),
        Vector(-150.0, 60.0, 75.0),   Vector(-300.0, 60.0, 75.0),
        Vector(-450.0, 60.0, 75.0),   Vector(-600.0, 60.0, 75.0),
        Vector(-150.0, 180.0, 75.0),  Vector(-300.0, 180.0, 75.0),
        Vector(-450.0, 180.0, 75.0),  Vector(-600.0, 180.0, 75.0),
        Vector(-750.0, 0.0, 75.0),
    };

    const uint32_t centerIndex = std::min<uint32_t>(nodeIndex, centers.size() - 1);
    const double theta = (12.0 / 30.0) * t + (nodeIndex * 17.0 * kPi / 180.0);
    const Vector c = centers[centerIndex];
    return Vector(c.x + 30.0 * std::cos(theta), c.y + 30.0 * std::sin(theta), c.z);
}

Vector
PositionForScenario(const std::string& scenario, uint32_t nodeIndex, double t)
{
    if (scenario == "link")
    {
        return LinkPosition(nodeIndex, t);
    }
    if (scenario == "field")
    {
        return FieldPosition(nodeIndex, t);
    }
    if (scenario == "large")
    {
        return LargePosition(nodeIndex, t);
    }

    NS_FATAL_ERROR("scenario must be link, field, or large");
    return Vector();
}

uint32_t
NodeCountForScenario(const std::string& scenario)
{
    if (scenario == "link")
    {
        return 2;
    }
    if (scenario == "field")
    {
        return 3;
    }
    if (scenario == "large")
    {
        return 19;
    }

    NS_FATAL_ERROR("scenario must be link, field, or large");
    return 0;
}

double
StopTimeForScenario(const std::string& scenario)
{
    if (scenario == "link")
    {
        return 110.0;
    }
    if (scenario == "field")
    {
        return 140.0;
    }
    if (scenario == "large")
    {
        return 400.0;
    }

    NS_FATAL_ERROR("scenario must be link, field, or large");
    return 0.0;
}

void
ApplyMobility(NodeContainer nodes, std::string scenario, double t, double step, double stopTime)
{
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<MobilityModel> mobility = nodes.Get(i)->GetObject<MobilityModel>();
        mobility->SetPosition(PositionForScenario(scenario, i, t));
    }

    if (t + step <= stopTime)
    {
        Simulator::Schedule(Seconds(step), &ApplyMobility, nodes, scenario, t + step, step, stopTime);
    }
}

void
InstallRouting(NodeContainer nodes, const std::string& protocol, double helloInterval)
{
    Ipv4ListRoutingHelper list;
    InternetStackHelper internet;

    if (protocol == "OLSR")
    {
        OlsrHelper olsr;
        olsr.Set("HelloInterval", TimeValue(Seconds(helloInterval)));
        list.Add(olsr, 100);
    }
    else if (protocol == "P-OLSR")
    {
        PolsrHelper polsr;
        polsr.Set("HelloInterval", TimeValue(Seconds(helloInterval)));
        /*
         * 这里安装的是 scratch/P-OLSR/polsr-module 中的协议级 P-OLSR 副本，
         * 不是 ns-3 原生 src/olsr。
         *
         * EnablePrediction 打开 HELLO/TC 的预测字段和链路预测状态；
         * UsePredictionForRouting 让路由表计算使用 predictedCost；
         * UsePredictionForMpr 只在 MPR 候选打平时使用预测代价作为辅助判断。
         */
        polsr.Set("EnablePrediction", BooleanValue(true));
        polsr.Set("UsePredictionForRouting", BooleanValue(true));
        polsr.Set("UsePredictionForMpr", BooleanValue(true));
        polsr.Set("PredictionAlpha", DoubleValue(0.05));
        polsr.Set("PredictionBeta", DoubleValue(0.2));
        polsr.Set("PredictionGamma", DoubleValue(0.04));
        polsr.Set("PredictionValidityTime", TimeValue(Seconds(3.0 * helloInterval)));
        list.Add(polsr, 100);
    }
    else
    {
        NS_FATAL_ERROR("protocol must be OLSR or P-OLSR");
    }

    internet.SetRoutingHelper(list);
    internet.Install(nodes);
}

void
RunExperiment(const std::string& scenario,
              const std::string& protocol,
              const std::string& outputDir,
              double helloInterval)
{
    const uint32_t nodeCount = NodeCountForScenario(scenario);
    const double stopTime = StopTimeForScenario(scenario);
    const double appStart = 20.0;

    NodeContainer nodes;
    nodes.Create(nodeCount);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("DsssRate11Mbps"),
                                 "ControlMode",
                                 StringValue("DsssRate11Mbps"));

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(16.0));
    phy.Set("TxPowerEnd", DoubleValue(16.0));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    ApplyMobility(nodes, scenario, 0.0, 0.5, stopTime);

    InstallRouting(nodes, protocol, helloInterval);

    Ipv4AddressHelper address;
    address.SetBase("10.10.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    PacketSinkHelper sink("ns3::UdpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), kPort));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(stopTime));

    OnOffHelper source("ns3::UdpSocketFactory",
                       InetSocketAddress(interfaces.GetAddress(0), kPort));
    source.SetAttribute("PacketSize", UintegerValue(1470));
    source.SetAttribute("DataRate", DataRateValue(DataRate("1Mbps")));
    source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
    ApplicationContainer sourceApp = source.Install(nodes.Get(1));
    sourceApp.Start(Seconds(appStart));
    sourceApp.Stop(Seconds(stopTime));

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(stopTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    uint64_t txPackets = 0;
    uint64_t rxPackets = 0;
    uint64_t lostPackets = 0;
    uint64_t rxBytes = 0;
    Time delaySum = Seconds(0);

    for (const auto& item : monitor->GetFlowStats())
    {
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(item.first);
        if (tuple.destinationPort != kPort)
        {
            continue;
        }
        txPackets += item.second.txPackets;
        rxPackets += item.second.rxPackets;
        lostPackets += item.second.lostPackets;
        rxBytes += item.second.rxBytes;
        delaySum += item.second.delaySum;
    }

    std::filesystem::create_directories(outputDir);
    const std::filesystem::path csvPath =
        std::filesystem::path(outputDir) / "polsr_protocol_summary.csv";
    const bool exists = std::filesystem::exists(csvPath);
    std::ofstream csv(csvPath, std::ios::app);
    if (!exists)
    {
        csv << "scenario,protocol,hello_interval_s,tx_packets,rx_packets,lost_packets,pdr,dlr,"
            << "goodput_mbps,mean_delay_ms,notes\n";
    }

    const double pdr = txPackets == 0 ? 0.0 : static_cast<double>(rxPackets) / txPackets;
    const double dlr = 1.0 - pdr;
    const double duration = stopTime - appStart;
    const double goodput = duration <= 0.0 ? 0.0 : (static_cast<double>(rxBytes) * 8.0) / duration / 1e6;
    const double meanDelayMs =
        rxPackets == 0 ? 0.0 : delaySum.GetMilliSeconds() / static_cast<double>(rxPackets);

    csv << scenario << ',' << protocol << ',' << helloInterval << ',' << txPackets << ','
        << rxPackets << ',' << lostPackets << ',' << std::setprecision(8) << pdr << ',' << dlr
        << ',' << goodput << ',' << meanDelayMs
        << ",local_polsr_copy_no_src_olsr_modification\n";

    Simulator::Destroy();
    std::cout << "Wrote " << csvPath << std::endl;
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenario = "field";
    std::string protocol = "P-OLSR";
    std::string outputDir = "scratch/P-OLSR/protocol-results";
    double helloInterval = 0.5;

    CommandLine cmd(__FILE__);
    cmd.AddValue("scenario", "link, field, or large", scenario);
    cmd.AddValue("protocol", "OLSR or P-OLSR", protocol);
    cmd.AddValue("outputDir", "CSV output directory", outputDir);
    cmd.AddValue("helloInterval", "Hello interval in seconds", helloInterval);
    cmd.Parse(argc, argv);

    RunExperiment(scenario, protocol, outputDir, helloInterval);
    return 0;
}
