/*
 * Smoke test for protocol-level P-OLSR message extensions.
 *
 * This checks that the local P-OLSR HELLO GPS/ETX fields and TC prediction
 * fields survive ns-3 Packet header serialization and deserialization.
 */

#include "polsr-module/model/polsr-header.h"

#include "ns3/core-module.h"
#include "ns3/network-module.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace ns3;

namespace
{

bool
Near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 0.02;
}

void
Check(bool condition, const std::string& message)
{
    if (!condition)
    {
        NS_FATAL_ERROR(message);
    }
}

void
CheckHello()
{
    polsr::MessageHeader original;
    original.SetVTime(Seconds(6));
    original.SetOriginatorAddress(Ipv4Address("10.0.0.1"));
    original.SetTimeToLive(1);
    original.SetHopCount(0);
    original.SetMessageSequenceNumber(7);
    polsr::MessageHeader::Hello& hello = original.GetHello();
    hello.SetHTime(Seconds(0.5));
    hello.willingness = polsr::Willingness::DEFAULT;
    hello.hasGps = true;
    hello.longitude = 12.34;
    hello.latitude = -56.78;
    hello.altitude = 90.1;

    polsr::MessageHeader::Hello::LinkMessage linkMessage;
    linkMessage.linkCode = 6;
    linkMessage.neighborInterfaceAddresses.push_back(Ipv4Address("10.0.0.2"));
    linkMessage.neighborLinkQualities.push_back(
        polsr::MessageHeader::Hello::LinkMessage::NeighborLinkQuality{
            Ipv4Address("10.0.0.2"),
            0.75,
            0.8,
            3.2,
            true});
    hello.linkMessages.push_back(linkMessage);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(original);

    polsr::MessageHeader decoded;
    packet->RemoveHeader(decoded);
    const polsr::MessageHeader::Hello& out = decoded.GetHello();
    Check(out.hasGps, "HELLO GPS flag was not preserved");
    Check(Near(out.longitude, hello.longitude), "HELLO longitude mismatch");
    Check(Near(out.latitude, hello.latitude), "HELLO latitude mismatch");
    Check(Near(out.altitude, hello.altitude), "HELLO altitude mismatch");
    Check(out.linkMessages.size() == 1, "HELLO link message count mismatch");
    Check(out.linkMessages[0].neighborLinkQualities.size() == 1,
          "HELLO link-quality count mismatch");
    Check(out.linkMessages[0].neighborLinkQualities[0].qualityValid,
          "HELLO link-quality flag mismatch");
    Check(Near(out.linkMessages[0].neighborLinkQualities[0].forwardReceivingRatio, 0.75),
          "HELLO forward receiving ratio mismatch");
    Check(Near(out.linkMessages[0].neighborLinkQualities[0].reverseReceivingRatio, 0.8),
          "HELLO reverse receiving ratio mismatch");
    Check(Near(out.linkMessages[0].neighborLinkQualities[0].relativeSpeed, 3.2),
          "HELLO relative speed mismatch");
}

void
CheckTc()
{
    polsr::MessageHeader original;
    original.SetVTime(Seconds(15));
    original.SetOriginatorAddress(Ipv4Address("10.0.0.2"));
    original.SetTimeToLive(255);
    original.SetHopCount(0);
    original.SetMessageSequenceNumber(9);
    polsr::MessageHeader::Tc& tc = original.GetTc();
    tc.ansn = 3;
    tc.neighborAddresses.push_back(Ipv4Address("10.0.0.3"));
    tc.neighborPredictions.push_back(
        polsr::MessageHeader::Tc::NeighborPrediction{Ipv4Address("10.0.0.3"),
                                                     2.5,
                                                     0.7,
                                                     0.8,
                                                     3.2,
                                                     true});

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(original);

    polsr::MessageHeader decoded;
    packet->RemoveHeader(decoded);
    const polsr::MessageHeader::Tc& out = decoded.GetTc();
    Check(out.neighborAddresses.size() == 1, "TC neighbor count mismatch");
    Check(out.neighborPredictions.size() == 1, "TC prediction count mismatch");
    Check(out.neighborPredictions[0].predictionValid, "TC prediction flag was not preserved");
    Check(Near(out.neighborPredictions[0].predictedCost, 2.5), "TC predicted cost mismatch");
    Check(Near(out.neighborPredictions[0].forwardReceivingRatio, 0.7),
          "TC forward receiving ratio mismatch");
    Check(Near(out.neighborPredictions[0].reverseReceivingRatio, 0.8),
          "TC reverse receiving ratio mismatch");
    Check(Near(out.neighborPredictions[0].relativeSpeed, 3.2), "TC relative speed mismatch");
}

} // namespace

int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    CheckHello();
    CheckTc();
    std::cout << "P-OLSR header serialization smoke test passed" << std::endl;
    return 0;
}
