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
#include "ns3/propagation-module.h"
#include "ns3/wifi-module.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/trace-helper.h"
#include "ns3/output-stream-wrapper.h"


#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ns3;

namespace ns3
{

class PaperDlrPropagationLossModel : public PropagationLossModel
{
  public:
    static TypeId GetTypeId();

  private:
    double DoCalcRxPower(double txPowerDbm, Ptr<MobilityModel> a, Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;

    double m_frequencyHz{5.18e9};
    double m_systemLoss{1.0};
    double m_dlrMaxExtraLossDb{12.0};
};

NS_OBJECT_ENSURE_REGISTERED(PaperDlrPropagationLossModel);

TypeId
PaperDlrPropagationLossModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::PaperDlrPropagationLossModel")
            .SetParent<PropagationLossModel>()
            .SetGroupName("Propagation")
            .AddConstructor<PaperDlrPropagationLossModel>()
            .AddAttribute("Frequency",
                          "Carrier frequency in Hz.",
                          DoubleValue(5.18e9),
                          MakeDoubleAccessor(&PaperDlrPropagationLossModel::m_frequencyHz),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("SystemLoss",
                          "Dimensionless system loss used by the Friis base loss.",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&PaperDlrPropagationLossModel::m_systemLoss),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("DlrMaxExtraLoss",
                          "Maximum extra loss in dB contributed by the paper DLR curve.",
                          DoubleValue(12.0),
                          MakeDoubleAccessor(&PaperDlrPropagationLossModel::m_dlrMaxExtraLossDb),
                          MakeDoubleChecker<double>(0.0));
    return tid;
}

double
PaperDlrPropagationLossModel::DoCalcRxPower(double txPowerDbm,
                                            Ptr<MobilityModel> a,
                                            Ptr<MobilityModel> b) const
{
    const double distance = a->GetDistanceFrom(b);
    if (distance <= 0.0)
    {
        return txPowerDbm;
    }

    constexpr double speedOfLight = 299792458.0;
    const double wavelength = speedOfLight / m_frequencyHz;
    constexpr double pi = 3.14159265358979323846;
    const double friisLossDb =
        20.0 * std::log10(4.0 * pi * distance / wavelength) + 10.0 * std::log10(m_systemLoss);
    const double paperDlr = 1.0 / (1.0 + std::exp(-(-8.9 + 0.025 * distance)));
    const double extraLossDb = m_dlrMaxExtraLossDb * std::clamp(paperDlr, 0.0, 1.0);
    return txPowerDbm - friisLossDb - extraLossDb;
}

int64_t
PaperDlrPropagationLossModel::DoAssignStreams(int64_t stream)
{
    return 0;
}

} // namespace ns3

namespace
{

constexpr uint16_t kPort = 9000;
constexpr double kPi = 3.14159265358979323846;

struct ExperimentOptions
{
    double helloInterval{1.0};
    bool usePredictionForRouting{true};
    bool usePredictionForMpr{false};
    double predictionAlpha{0.2};
    double predictionBeta{0.2};
    double predictionGamma{0.08};
    double helloDeadlineFactor{1.4};
    double predictionValidityMultiplier{3.0};
    double outageWindow{1.0};
    double outageDlrThreshold{0.2};
    double maxDeliveryDelay{5.0};
    uint32_t runIndex{1};
    uint32_t rngRun{1};
    std::string wirelessProfile{"legacy"};
    double paperDlrMaxExtraLossDb{12.0};
};

struct PacketRecord
{
    uint32_t bin{0};
    double txTime{0.0};
};

struct DeliveryTracker
{
    double appStart{0.0};
    double appStop{0.0};
    double window{1.0};
    double maxDelay{5.0};
    double outageDlrThreshold{0.2};
    std::vector<uint64_t> txByBin;
    std::vector<uint64_t> rxByBin;
    std::vector<uint64_t> rxBytesByBin;
    std::map<uint64_t, PacketRecord> txRecords;
    uint64_t latePackets{0};
};

struct OutageSummary
{
    uint64_t txPackets{0};
    uint64_t rxPackets{0};
    uint64_t lostPackets{0};
    double pdr{0.0};
    double dlr{0.0};
    double outageTime{0.0};
    double outageRatio{0.0};
};

uint32_t
BinForTime(const DeliveryTracker& tracker, double time)
{
    if (time <= tracker.appStart || tracker.window <= 0.0)
    {
        return 0;
    }
    return static_cast<uint32_t>((time - tracker.appStart) / tracker.window);
}

void
RecordTx(DeliveryTracker* tracker, Ptr<const Packet> packet)
{
    const double now = Simulator::Now().GetSeconds();
    const uint32_t bin = BinForTime(*tracker, now);
    if (bin >= tracker->txByBin.size())
    {
        return;
    }

    tracker->txByBin[bin]++;
    tracker->txRecords[packet->GetUid()] = PacketRecord{bin, now};
}

void
RecordRx(DeliveryTracker* tracker, Ptr<const Packet> packet, const Address&)
{
    const auto item = tracker->txRecords.find(packet->GetUid());
    if (item == tracker->txRecords.end())
    {
        return;
    }

    const double delay = Simulator::Now().GetSeconds() - item->second.txTime;
    if (delay <= tracker->maxDelay && item->second.bin < tracker->rxByBin.size())
    {
        tracker->rxByBin[item->second.bin]++;
        tracker->rxBytesByBin[item->second.bin] += packet->GetSize();
    }
    else
    {
        tracker->latePackets++;
    }
    tracker->txRecords.erase(item);
}

OutageSummary
ComputeOutageSummary(const DeliveryTracker& tracker)
{
    OutageSummary summary;
    const double duration = tracker.appStop - tracker.appStart;
    for (uint32_t i = 0; i < tracker.txByBin.size(); ++i)
    {
        const uint64_t tx = tracker.txByBin[i];
        const uint64_t rx = tracker.rxByBin[i];
        summary.txPackets += tx;
        summary.rxPackets += rx;

        if (tx == 0)
        {
            continue;
        }

        const double dlr = 1.0 - static_cast<double>(rx) / static_cast<double>(tx);
        if (dlr > tracker.outageDlrThreshold)
        {
            const double binStart = i * tracker.window;
            const double binDuration = std::min(tracker.window, duration - binStart);
            if (binDuration > 0.0)
            {
                summary.outageTime += binDuration;
            }
        }
    }

    summary.lostPackets = summary.txPackets >= summary.rxPackets
                              ? summary.txPackets - summary.rxPackets
                              : 0;
    summary.pdr = summary.txPackets == 0
                      ? 0.0
                      : static_cast<double>(summary.rxPackets) / summary.txPackets;
    summary.dlr = 1.0 - summary.pdr;
    summary.outageRatio = duration <= 0.0 ? 0.0 : summary.outageTime / duration;
    return summary;
}

void
ValidateCsvHeader(const std::filesystem::path& csvPath, const std::string& expectedHeader)
{
    if (!std::filesystem::exists(csvPath))
    {
        return;
    }

    std::ifstream existingCsv(csvPath);
    std::string existingHeader;
    std::getline(existingCsv, existingHeader);
    if (existingHeader != expectedHeader)
    {
        NS_FATAL_ERROR("Existing CSV header does not match the current schema: "
                       << csvPath << ". Choose a new outputDir to avoid mixing old and new results.");
    }
}

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

/*
 * 调试辅助函数：输出当前所有节点的 IPv4 路由表。
 *
 * 作用：
 * 1. 遍历仿真中的所有节点；
 * 2. 从每个节点中取出 Ipv4 对象；
 * 3. 再从 Ipv4 对象中取出当前安装的路由协议；
 * 4. 调用路由协议自己的 PrintRoutingTable() 接口，把路由表写入文件。
 *
 * 注意：
 * - 该函数只用于实验观察和调试；
 * - 不会修改路由表；
 * - 不会改变 P-OLSR / OLSR-ETX 的路由计算逻辑；
 * - 不会影响数据包转发和实验指标；
 * - 输出内容取决于调用该函数时的仿真时间。
 */
static void
PrintAllRoutingTables(NodeContainer nodes,
                      Ptr<OutputStreamWrapper> stream,
                      Time::Unit unit = Time::S)
{
    /*
     * 在输出文件中标记当前快照时间。
     * Simulator::Now() 返回当前仿真时间，不是系统真实时间。
     */
    *stream->GetStream() << "\n=== Routing tables at "
                         << Simulator::Now().GetSeconds()
                         << " s ===\n";
     /*
     * 逐个遍历 NodeContainer 中的节点。
     * i 是节点编号，和 nodes.Get(i) 对应。
     */
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

        /*
         * 给每个节点的路由表加一个分隔标题，方便后续查看。
         */
        *stream->GetStream() << "\n--- Node " << i << " ---\n";

          /*
         * 如果节点还没有安装 IPv4 协议栈，则无法打印路由表。
         * 正常情况下，InstallRouting() 调用 internet.Install(nodes) 后应当存在 Ipv4 对象。
         */
        if (!ipv4)
        {
            *stream->GetStream() << "No Ipv4 object\n";
            continue;
        }

        /*
         * 获取该节点当前使用的 IPv4 路由协议。
         * 这里实际拿到的通常是 Ipv4ListRouting，里面再包含 P-OLSR 或 OLSR-ETX。
         */
        Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
        if (!routing)
        {
            *stream->GetStream() << "No routing protocol\n";
            continue;
        }

        /*
         * 调用 ns-3 路由协议统一的路由表打印接口。
         * 对 P-OLSR 来说，最终会进入 polsr-routing-protocol.cc 中的 PrintRoutingTable()。
         */
        routing->PrintRoutingTable(stream, unit);
    }
}

void
InstallRouting(NodeContainer nodes, const std::string& protocol, const ExperimentOptions& options)
{
    Ipv4ListRoutingHelper list;
    InternetStackHelper internet;

    if (protocol == "OLSR")
    {
        OlsrHelper olsr;
        olsr.Set("HelloInterval", TimeValue(Seconds(options.helloInterval)));
        list.Add(olsr, 100);
    }
    else if (protocol == "OLSR-ETX" || protocol == "P-OLSR")
    {
        PolsrHelper polsr;
        polsr.Set("HelloInterval", TimeValue(Seconds(options.helloInterval)));
        /*
         * 这里安装的是 scratch/P-OLSR/polsr-module 中的协议级 P-OLSR 副本，
         * 不是 ns-3 原生 src/olsr。
         *
         * EnablePrediction 打开 HELLO/TC 的预测字段和链路预测状态；
         * UsePredictionForRouting 让路由表计算使用 predictedCost；
         * UsePredictionForMpr 只在 MPR 候选打平时使用预测代价作为辅助判断。
         */
        polsr.Set("EnablePrediction", BooleanValue(true));
        polsr.Set("UsePredictionForRouting", BooleanValue(options.usePredictionForRouting));
        polsr.Set("UsePredictionForMpr", BooleanValue(options.usePredictionForMpr));
        polsr.Set("PredictionAlpha", DoubleValue(options.predictionAlpha));
        polsr.Set("PredictionBeta", DoubleValue(protocol == "OLSR-ETX" ? 0.0 : options.predictionBeta));
        polsr.Set("PredictionGamma", DoubleValue(options.predictionGamma));
        polsr.Set("HelloDeadlineFactor", DoubleValue(options.helloDeadlineFactor));
        polsr.Set("PredictionValidityTime",
                  TimeValue(Seconds(options.predictionValidityMultiplier * options.helloInterval)));
        list.Add(polsr, 100);
    }
    else
    {
        NS_FATAL_ERROR("protocol must be OLSR, OLSR-ETX, or P-OLSR");
    }

    internet.SetRoutingHelper(list);
    internet.Install(nodes);
}

void
RunExperiment(const std::string& scenario,
              const std::string& protocol,
              const std::string& outputDir,
              const ExperimentOptions& options)
{
    const uint32_t nodeCount = NodeCountForScenario(scenario);
    const double stopTime = StopTimeForScenario(scenario);
    const double appStart = 20.0;
    SeedManager::SetRun(options.rngRun);

    NodeContainer nodes;
    nodes.Create(nodeCount);

    WifiHelper wifi;
    if (options.wirelessProfile == "legacy")
    {
        wifi.SetStandard(WIFI_STANDARD_80211b);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",
                                     StringValue("DsssRate11Mbps"),
                                     "ControlMode",
                                     StringValue("DsssRate11Mbps"));
    }
    else if (options.wirelessProfile == "paper" || options.wirelessProfile == "paper-dlr")
    {
        wifi.SetStandard(WIFI_STANDARD_80211n);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",
                                     StringValue("HtMcs1"),
                                     "ControlMode",
                                     StringValue("HtMcs0"));
    }
    else
    {
        NS_FATAL_ERROR("wirelessProfile must be legacy, paper, or paper-dlr");
    }

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    if (options.wirelessProfile == "legacy")
    {
        channel.AddPropagationLoss("ns3::FriisPropagationLossModel");
    }
    else if (options.wirelessProfile == "paper")
    {
        channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent",
                                   DoubleValue(2.3),
                                   "ReferenceDistance",
                                   DoubleValue(1.0),
                                   "ReferenceLoss",
                                   DoubleValue(46.7));
        channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");
    }
    else
    {
        channel.AddPropagationLoss("ns3::PaperDlrPropagationLossModel",
                                   "DlrMaxExtraLoss",
                                   DoubleValue(options.paperDlrMaxExtraLossDb));
        channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");
    }

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    if (options.wirelessProfile == "paper" || options.wirelessProfile == "paper-dlr")
    {
        phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));
    }
    phy.Set("TxPowerStart", DoubleValue(16.0));
    phy.Set("TxPowerEnd", DoubleValue(16.0));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    ApplyMobility(nodes, scenario, 0.0, 0.5, stopTime);

    InstallRouting(nodes, protocol, options);

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

    DeliveryTracker deliveryTracker;
    deliveryTracker.appStart = appStart;
    deliveryTracker.appStop = stopTime;
    deliveryTracker.window = options.outageWindow;
    deliveryTracker.maxDelay = options.maxDeliveryDelay;
    deliveryTracker.outageDlrThreshold = options.outageDlrThreshold;
    const uint32_t outageBins =
        options.outageWindow <= 0.0
            ? 0
            : static_cast<uint32_t>(std::ceil((stopTime - appStart) / options.outageWindow));
    deliveryTracker.txByBin.assign(outageBins, 0);
    deliveryTracker.rxByBin.assign(outageBins, 0);
    deliveryTracker.rxBytesByBin.assign(outageBins, 0);
    sourceApp.Get(0)->TraceConnectWithoutContext("Tx",
                                                 MakeBoundCallback(&RecordTx, &deliveryTracker));
    sinkApp.Get(0)->TraceConnectWithoutContext("Rx",
                                               MakeBoundCallback(&RecordRx, &deliveryTracker));

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
    
    /*
    * 路由表快照输出。
    *
    * 作用：
    * 1. 在指定仿真时刻输出所有节点的路由表；
    * 2. 用于观察 P-OLSR 或 OLSR-ETX 在不同时间点选择的下一跳；
    * 3. 输出文件保存在当前实验的 outputDir 中。
    *
    * 注意：
    * - 这里只是安排打印事件，不改变协议逻辑；
    * - 打印事件会在 Simulator::Run() 期间按指定时间触发；
    * - 如果仿真结束时间小于某个快照时间，该快照不会输出；
    * - 每个 protocol / runIndex 使用单独文件，避免不同实验结果混在一起。
    */
    std::filesystem::create_directories(outputDir);

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> routingStream =
        ascii.CreateFileStream(outputDir + "/routing-tables-" + protocol +
                            "-run" + std::to_string(options.runIndex) + ".txt");
    /*
    * 这些时间点可以按需要调整。
    * 例如 large 场景总时长较长，可以看 30s、100s、200s、300s；
    * 如果只是短时间测试，可以改成 5s、10s、20s。
    */
    Simulator::Schedule(Seconds(30.0), &PrintAllRoutingTables, nodes, routingStream, Time::S);
    Simulator::Schedule(Seconds(100.0), &PrintAllRoutingTables, nodes, routingStream, Time::S);
    Simulator::Schedule(Seconds(200.0), &PrintAllRoutingTables, nodes, routingStream, Time::S);
    Simulator::Schedule(Seconds(300.0), &PrintAllRoutingTables, nodes, routingStream, Time::S);

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
    const std::string csvHeader =
        "run_index,rng_run,scenario,protocol,hello_interval_s,tx_packets,rx_packets,lost_packets,pdr,dlr,"
        "goodput_mbps,mean_delay_ms,paper_tx_packets,paper_rx_packets,"
        "paper_lost_packets,paper_pdr,paper_dlr,outage_time_s,outage_ratio,"
        "outage_window_s,outage_dlr_threshold,max_delivery_delay_s,"
        "delayed_over_max_packets,use_prediction_for_routing,use_prediction_for_mpr,"
        "prediction_alpha,prediction_beta,prediction_gamma,hello_deadline_factor,wireless_profile,"
        "paper_dlr_max_extra_loss_db,notes";
    const bool exists = std::filesystem::exists(csvPath);
    ValidateCsvHeader(csvPath, csvHeader);

    std::ofstream csv(csvPath, std::ios::app);
    if (!exists)
    {
        csv << csvHeader << "\n";
    }

    const double pdr = txPackets == 0 ? 0.0 : static_cast<double>(rxPackets) / txPackets;
    const double dlr = 1.0 - pdr;
    const double duration = stopTime - appStart;
    const double goodput = duration <= 0.0 ? 0.0 : (static_cast<double>(rxBytes) * 8.0) / duration / 1e6;
    const double meanDelayMs =
        rxPackets == 0 ? 0.0 : delaySum.GetMilliSeconds() / static_cast<double>(rxPackets);
    const OutageSummary outage = ComputeOutageSummary(deliveryTracker);
    const double effectivePredictionBeta = protocol == "OLSR-ETX" ? 0.0 : options.predictionBeta;

    csv << options.runIndex << ',' << options.rngRun << ',' << scenario << ',' << protocol << ','
        << options.helloInterval << ',' << txPackets << ','
        << rxPackets << ',' << lostPackets << ',' << std::setprecision(8) << pdr << ',' << dlr
        << ',' << goodput << ',' << meanDelayMs << ',' << outage.txPackets << ','
        << outage.rxPackets << ',' << outage.lostPackets << ',' << outage.pdr << ','
        << outage.dlr << ',' << outage.outageTime << ',' << outage.outageRatio << ','
        << options.outageWindow << ',' << options.outageDlrThreshold << ','
        << options.maxDeliveryDelay << ',' << deliveryTracker.latePackets << ','
        << options.usePredictionForRouting << ',' << options.usePredictionForMpr << ','
        << options.predictionAlpha << ',' << effectivePredictionBeta << ','
        << options.predictionGamma << ',' << options.helloDeadlineFactor << ','
        << options.wirelessProfile << ','
        << options.paperDlrMaxExtraLossDb
        << ",local_polsr_copy_no_src_olsr_modification\n";

    const std::filesystem::path timeSeriesPath =
        std::filesystem::path(outputDir) / "polsr_goodput_timeseries.csv";
    const std::string timeSeriesHeader =
        "run_index,rng_run,scenario,protocol,hello_interval_s,prediction_alpha,prediction_beta,"
        "prediction_gamma,hello_deadline_factor,use_prediction_for_routing,use_prediction_for_mpr,"
        "wireless_profile,paper_dlr_max_extra_loss_db,"
        "window_s,time_s,tx_packets,rx_packets,rx_bytes,goodput_mbps";
    const bool timeSeriesExists = std::filesystem::exists(timeSeriesPath);
    ValidateCsvHeader(timeSeriesPath, timeSeriesHeader);
    std::ofstream timeSeriesCsv(timeSeriesPath, std::ios::app);
    if (!timeSeriesExists)
    {
        timeSeriesCsv << timeSeriesHeader << "\n";
    }
    for (uint32_t i = 0; i < deliveryTracker.rxBytesByBin.size(); ++i)
    {
        const double elapsed = i * options.outageWindow;
        const double windowDuration = std::min(options.outageWindow, duration - elapsed);
        if (windowDuration <= 0.0)
        {
            continue;
        }
        const double time = appStart + elapsed + 0.5 * windowDuration;
        const double windowGoodput =
            static_cast<double>(deliveryTracker.rxBytesByBin[i]) * 8.0 / windowDuration / 1e6;
        timeSeriesCsv << options.runIndex << ',' << options.rngRun << ',' << scenario << ','
                      << protocol << ',' << options.helloInterval << ','
                      << options.predictionAlpha << ',' << effectivePredictionBeta << ','
                      << options.predictionGamma << ',' << options.helloDeadlineFactor << ','
                      << options.usePredictionForRouting << ','
                      << options.usePredictionForMpr << ',' << options.wirelessProfile << ','
                      << options.paperDlrMaxExtraLossDb << ',' << options.outageWindow << ','
                      << time << ',' << deliveryTracker.txByBin[i] << ','
                      << deliveryTracker.rxByBin[i] << ',' << deliveryTracker.rxBytesByBin[i]
                      << ',' << windowGoodput << "\n";
    }

    Simulator::Destroy();
    std::cout << "Wrote " << csvPath << std::endl;
    std::cout << "Wrote " << timeSeriesPath << std::endl;
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string scenario = "large";
    std::string protocol = "P-OLSR";
    std::string outputDir = "scratch/P-OLSR/protocol-results";
    ExperimentOptions options;

    CommandLine cmd(__FILE__);
    cmd.AddValue("scenario", "link, field, or large", scenario);
    cmd.AddValue("protocol", "OLSR, OLSR-ETX, or P-OLSR", protocol);
    cmd.AddValue("outputDir", "CSV output directory", outputDir);
    cmd.AddValue("helloInterval", "Hello interval in seconds", options.helloInterval);
    cmd.AddValue("usePredictionForRouting",
                 "Use predicted ETX cost in P-OLSR route computation",
                 options.usePredictionForRouting);
    cmd.AddValue("usePredictionForMpr",
                 "Use predicted ETX cost as an MPR tie-breaker",
                 options.usePredictionForMpr);
    cmd.AddValue("predictionAlpha", "P-OLSR HELLO reception smoothing alpha", options.predictionAlpha);
    cmd.AddValue("predictionBeta", "P-OLSR speed-weighted ETX beta", options.predictionBeta);
    cmd.AddValue("predictionGamma", "P-OLSR relative-speed smoothing gamma", options.predictionGamma);
    cmd.AddValue("helloDeadlineFactor",
                 "HELLO deadline multiplier for Expected Arrival Time decay",
                 options.helloDeadlineFactor);
    cmd.AddValue("predictionValidityMultiplier",
                 "Prediction validity time as a multiple of Hello interval",
                 options.predictionValidityMultiplier);
    cmd.AddValue("outageWindow", "Outage accounting window in seconds", options.outageWindow);
    cmd.AddValue("outageDlrThreshold", "A window is outage when DLR is above this value", options.outageDlrThreshold);
    cmd.AddValue("maxDeliveryDelay",
                 "Packets delayed more than this many seconds are counted as not delivered for paper-style outage",
                 options.maxDeliveryDelay);
    cmd.AddValue("runIndex", "User-visible run index written to CSV output", options.runIndex);
    cmd.AddValue("rngRun", "ns-3 RNG run number for repeated experiments", options.rngRun);
    cmd.AddValue("wirelessProfile",
                 "Wireless profile: legacy, paper, or paper-dlr",
                 options.wirelessProfile);
    cmd.AddValue("paperDlrMaxExtraLossDb",
                 "Maximum extra propagation loss in dB added by the paper-dlr profile",
                 options.paperDlrMaxExtraLossDb);
    cmd.Parse(argc, argv);

    RunExperiment(scenario, protocol, outputDir, options);
    return 0;
}
