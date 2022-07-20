/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

// The topology generated in this experiment is described in section 2.1 of the
// BFC [1] paper. Basically, we will use a dumbbell topology with where the
// bottleneck link in the middle has 100Gbps bandwidth, and the RTT of flows
// are 8 usec.
//
// [1] P. Goyal, P. Shah, K. Zhao, G. Nikolaidis, M. Alizadeh, and T. E.
//     Anderson, “Backpressure Flow Control,” arXiv [cs.NI], 2019.
//     http://arxiv.org/abs/1909.09923
//
// The benchmark simply runs nFlows many flows on the given topology where each
// flow completes one by one. Then statistics about the flows (ie. cwnd, rtt
// etc.) are collected to see how different algorithms react to the change.
//
// Example to run the simulation:
// $./waf --run "scratch/bolt-pru-microbencmarks --ccModr=SWIFT"

#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

#define START_TIME 1.0
#define ONE_WAY_DELAY 4110e-9
#define MTU 1500

double lastdataArrivalTime;
uint64_t totalDataReceived = 0;

NS_LOG_COMPONENT_DEFINE("BoltPruBenchmarks");

void TraceFlowStats(Ptr<OutputStreamWrapper> stream, Ipv4Address saddr,
                    Ipv4Address daddr, uint16_t sport, uint16_t dport,
                    int txMsgId, uint32_t cwnd, uint64_t rtt) {
  Time now = Simulator::Now();

  NS_LOG_DEBUG("stats " << now.GetNanoSeconds() << " " << saddr << ":" << sport
                        << " " << daddr << ":" << dport << " " << txMsgId << " "
                        << cwnd << " " << rtt);

  *stream->GetStream() << now.GetNanoSeconds() << " " << saddr << ":" << sport
                       << " " << daddr << ":" << dport << " " << txMsgId << " "
                       << cwnd << " " << rtt << std::endl;
}

static void BytesInQueueDiscTrace(Ptr<OutputStreamWrapper> stream,
                                  uint32_t oldval, uint32_t newval) {
  NS_LOG_INFO(Simulator::Now().GetNanoSeconds()
              << " Queue Disc size from " << oldval << " to " << newval);

  *stream->GetStream() << Simulator::Now().GetNanoSeconds() << " " << newval
                       << std::endl;
}

void ReceiveLongLivedFlow(Ptr<Socket> socket) {
  Time now = Simulator::Now();
  Ptr<Packet> message;
  uint32_t messageSize;
  Address from;
  Ipv4Header ipv4h;
  BoltHeader bolth;
  while ((message = socket->RecvFrom(from))) {
    messageSize = message->GetSize();
    NS_LOG_DEBUG(now.GetNanoSeconds()
                 << " Received " << messageSize << " Bytes from "
                 << InetSocketAddress::ConvertFrom(from).GetIpv4() << ":"
                 << InetSocketAddress::ConvertFrom(from).GetPort());

    uint32_t hdrSize = bolth.GetSerializedSize() + ipv4h.GetSerializedSize();
    uint32_t payloadSize = MTU - hdrSize;
    uint32_t msgSizePkts =
        messageSize / payloadSize + (messageSize % payloadSize != 0);
    messageSize += msgSizePkts * hdrSize;

    double thp = (double)messageSize * 8.0 /
                 (now.GetSeconds() - ONE_WAY_DELAY - START_TIME) / 1e9;
    NS_LOG_INFO("The average thp from " << from << ": " << thp << "Gbps");
  }
}

void TraceDataArrival(double duration, Ptr<const Packet> msg, Ipv4Address saddr,
                      Ipv4Address daddr, uint16_t sport, uint16_t dport,
                      int txMsgId, uint32_t seqNo, uint16_t flag) {
  Time now = Simulator::Now();
  if (duration == 0.0 || now.GetSeconds() <= START_TIME + duration) {
    lastdataArrivalTime = now.GetSeconds();

    Ipv4Header ipv4h;  // Consider the total pkt size for link utilization
    BoltHeader bolth;
    totalDataReceived +=
        msg->GetSize() + ipv4h.GetSerializedSize() + bolth.GetSerializedSize();
  }
}

void SendLongLivedFlow(Ptr<Socket> socket, InetSocketAddress receiverAddr,
                       uint32_t flowSizeBytes) {
  Ptr<Packet> msg = Create<Packet>(flowSizeBytes);
  int sentBytes = socket->SendTo(msg, 0, receiverAddr);
  if (sentBytes > 0) {
    NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
                 << " Sent " << sentBytes << " Bytes to "
                 << receiverAddr.GetIpv4() << ":" << receiverAddr.GetPort());
  }
}

void SetLinkDownToStopFlow(NetDeviceContainer netDevices) {
  PointToPointNetDevice *p2pNetDevice;

  for (uint32_t n = 0; n < netDevices.GetN(); n++) {
    p2pNetDevice =
      dynamic_cast<PointToPointNetDevice *>(&(*(netDevices.Get(n))));
    p2pNetDevice->SetLinkDown();
  }
}

void CalculateTailBottleneckQueueOccupancy(std::string qStreamName,
                                           double percentile,
                                           uint64_t bottleneckBitRate,
                                           double duration) {
  std::ifstream qSizeTraceFile;
  qSizeTraceFile.open(qStreamName);
  NS_LOG_FUNCTION("Reading Bottleneck Queue Size Trace From: " << qStreamName);

  std::string line;
  std::istringstream lineBuffer;

  std::vector<int> queueSizes;
  uint64_t time;
  uint32_t qSizeBytes;
  while (getline(qSizeTraceFile, line)) {
    lineBuffer.clear();
    lineBuffer.str(line);
    lineBuffer >> time;
    lineBuffer >> qSizeBytes;
    if (duration == 0.0 || time < (uint64_t)((START_TIME + duration) * 1e9))
      queueSizes.push_back(qSizeBytes);
  }
  qSizeTraceFile.close();

  std::sort(queueSizes.begin(), queueSizes.end());
  uint32_t idx = (uint32_t)((double)queueSizes.size() * percentile);
  int tailQueueSizeBytes = queueSizes[idx];
  double tailQueueSizeUsec =
      (double)tailQueueSizeBytes * 8.0 * 1e6 / (double)bottleneckBitRate;

  NS_LOG_UNCOND(percentile * 100 << "%ile queue size: " << tailQueueSizeUsec
                                 << "usec (" << tailQueueSizeBytes
                                 << " Bytes)");
}

int main(int argc, char *argv[]) {
  auto simStart = std::chrono::steady_clock::now();

  AsciiTraceHelper asciiTraceHelper;
  std::string simNote("");
  uint32_t simIdx = 0;
  double flowEndTime = 0.002;
  bool stopFlowsManually = false;
  bool traceQueues = true;
  bool debugMode = false;
  uint32_t bdpBytes = 100000;            // in bytes
  uint64_t inboundRtxTimeout = 250000;   // in microseconds
  uint64_t outboundRtxTimeout = 100000;  // in microseconds

  int nFlows = 2;
  int senderPortNoStart = 1000;
  int receiverPortNoStart = 2000;

  std::string ccMode("DEFAULT");
  /* Bolt (Swift) Related Parameters */
  double rttSmoothingAlpha = 0.75;    // Default: 0.75
  uint16_t topoScalingPerHop = 1000;  // Default: 1000 ns
  double maxFlowScaling = 100000.0;   // Default: 100000.0
  double maxFlowScalingCwnd = 256.0;  // Default: 256.0 pkts
  double minFlowScalingCwnd = 0.1;    // Default: 0.1 pkts
  uint64_t baseDelay = 10000;         // Default: 25000 us (25 usec)
  double aiFactor = 1.0;              // Default: 1.0
  double mdFactor = 0.8;              // Default: 0.8
  double maxMd = 0.5;                 // Default: 0.5
  uint32_t maxCwnd = 373760;          // Default: 373760 Bytes
  bool usePerHopDelayForCc = false;   // Default: false

  bool enableMsgAgg = false;
  bool enableBts = false;
  bool enablePru = false;
  bool enableAbs = false;
  std::string ccThreshold("3KB");  // 2 packets

  CommandLine cmd(__FILE__);
  cmd.AddValue("note", "Any note to identify the simulation in the future",
               simNote);
  cmd.AddValue("simIdx",
               "The index of the simulation used to identify parallel runs.",
               simIdx);
  cmd.AddValue("flowEndTime", "The interval at which a flow leaves.",
               flowEndTime);
  cmd.AddValue("stopFlowsManually",
               "Whether to manually stop flows even if they are not finished.",
               stopFlowsManually);
  cmd.AddValue("nFlows", "Number of flows in the topology.", nFlows);
  cmd.AddValue("dontTraceQueues",
               "Whether to trace the queue lengths during the simulation.",
               traceQueues);
  cmd.AddValue("debug", "Whether to enable detailed pkt traces for debugging",
               debugMode);
  cmd.AddValue("bdp", "RttBytes to use in the simulation.", bdpBytes);
  cmd.AddValue("inboundRtxTimeout",
               "Number of microseconds before an inbound msg expires.",
               inboundRtxTimeout);
  cmd.AddValue("outboundRtxTimeout",
               "Number of microseconds before an outbound msg expires.",
               outboundRtxTimeout);
  cmd.AddValue("ccMode", "Type of congestion control algorithm to run.",
               ccMode);
  cmd.AddValue("rttSmoothingAlpha",
               "Smoothing factor for the RTT measurements.", rttSmoothingAlpha);
  cmd.AddValue("topoScalingPerHop", "Per hop scaling for target delay.",
               topoScalingPerHop);
  cmd.AddValue("maxFlowScaling", "Flow scaling multiplier for target delay.",
               maxFlowScaling);
  cmd.AddValue("baseDelay", "Base delay for the target delay.", baseDelay);
  cmd.AddValue("aiFactor", "Additive increase for congestion control.",
               aiFactor);
  cmd.AddValue("mdFactor", "Multiplicative decrease for congestion control.",
               mdFactor);
  cmd.AddValue("maxMd", "Maximum multiplicative decrease allowed.", maxMd);
  cmd.AddValue("maxCwnd", "Maximum value of cwnd a flow can have.", maxCwnd);
  cmd.AddValue("usePerHopDelayForCc",
               "Flag to to use per hop delay instead of RTT for CC.",
               usePerHopDelayForCc);
  cmd.AddValue("enableMsgAgg",
               "Flag to enable message aggregation on end-hosts.",
               enableMsgAgg);
  cmd.AddValue("enableBts", "Flag to enable back to sender feature.",
               enableBts);
  cmd.AddValue("enablePru", "Flag to enable proactive ramp-up feature.",
               enablePru);
  cmd.AddValue("enableAbs",
               "Flag to enable Available Bandwidth Signaling. If false, queue "
               "occupancy is used to detect congestion.",
               enableAbs);
  cmd.AddValue("ccThreshold", "Threshold for declaring congestion, i.e 15KB.",
               ccThreshold);
  cmd.Parse(argc, argv);

  if (ccMode == "DEFAULT") {
    NS_LOG_DEBUG("Bolt uses BTS, PRU, and ABS mechanisms by default!");
    enableBts = true;
    enablePru = true;
    enableAbs = true;
  } else if (ccMode == "SWIFT") {
    ccThreshold = "1500KB";  // Swift doesn't have the notion of threshold
  }

  Time::SetResolution(Time::NS);
  // Packet::EnablePrinting();
  LogComponentEnable("BoltPruBenchmarks", LOG_LEVEL_WARN);
  LogComponentEnable("BoltSocket", LOG_LEVEL_WARN);
  LogComponentEnable("BoltL4Protocol", LOG_LEVEL_WARN);
  LogComponentEnable("PfifoBoltQueueDisc", LOG_LEVEL_WARN);

  if (debugMode) {
    LogComponentEnable("BoltPruBenchmarks", LOG_LEVEL_DEBUG);
    NS_LOG_DEBUG("Running in DEBUG Mode!");
    SeedManager::SetRun(0);
  } else {
    SeedManager::SetRun(simIdx);
  }

  std::string tracesFileName("outputs/bolt-pru-benchmarks/");
  if (debugMode)
    tracesFileName += "debug";
  else
    tracesFileName += std::to_string(simIdx);
  if (enableMsgAgg) tracesFileName += "_MSGAGG";
  tracesFileName += "_" + ccMode;
  if (ccMode != "DEFAULT") {
    if (enableBts) tracesFileName += "_BTS";
    if (enablePru) tracesFileName += "_PRU";
    if (usePerHopDelayForCc) tracesFileName += "_PERHOP";
    if (enableAbs) tracesFileName += "_ABS";
  }
  if (!simNote.empty()) {
    tracesFileName += "_" + simNote;
    NS_LOG_UNCOND("Note: " << simNote);
  }

  std::string qStreamName = tracesFileName + ".qlen";
  std::string statsTracesFileName = tracesFileName + ".log";

  /******** Create Nodes ********/
  NS_LOG_DEBUG("Creating Nodes...");
  NodeContainer senderNodes;
  senderNodes.Create(nFlows);

  NodeContainer receiverNodes;
  receiverNodes.Create(nFlows);

  NodeContainer switchNodes;
  switchNodes.Create(2);

  /******** Create Channels ********/
  NS_LOG_DEBUG("Configuring Channels...");
  PointToPointHelper hostLinks;
  hostLinks.SetDeviceAttribute("DataRate", StringValue("102Gbps"));
  hostLinks.SetChannelAttribute("Delay", StringValue("1272ns"));
  hostLinks.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));

  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute("DataRate", StringValue("100Gbps"));
  bottleneckLink.SetChannelAttribute("Delay", StringValue("1272ns"));
  bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

  /******** Create NetDevices ********/
  NS_LOG_DEBUG("Creating NetDevices...");
  NetDeviceContainer switchToSwitchDevices;
  switchToSwitchDevices = bottleneckLink.Install(switchNodes);
  for (uint32_t n = 0; n < switchToSwitchDevices.GetN(); n++)
    switchToSwitchDevices.Get(n)->SetMtu(MTU);

  NetDeviceContainer senderToSwitchDevices[nFlows];
  for (int i = 0; i < nFlows; i++) {
    senderToSwitchDevices[i] =
        hostLinks.Install(senderNodes.Get(i), switchNodes.Get(0));
    for (uint32_t n = 0; n < senderToSwitchDevices[i].GetN(); n++)
      senderToSwitchDevices[i].Get(n)->SetMtu(MTU);
  }

  NetDeviceContainer receiverToSwitchDevices[nFlows];
  for (int i = 0; i < nFlows; i++) {
    receiverToSwitchDevices[i] =
        hostLinks.Install(receiverNodes.Get(i), switchNodes.Get(1));
    for (uint32_t n = 0; n < receiverToSwitchDevices[i].GetN(); n++)
      receiverToSwitchDevices[i].Get(n)->SetMtu(MTU);
  }

  /******** Install Internet Stack ********/
  NS_LOG_DEBUG("Installing Internet Stack...");

  /******** Set default BDP value in packets ********/
  Config::SetDefault("ns3::BoltL4Protocol::AggregateMsgsIfPossible",
                     BooleanValue(enableMsgAgg));
  Config::SetDefault("ns3::BoltL4Protocol::BandwidthDelayProduct",
                     UintegerValue(bdpBytes));
  Config::SetDefault("ns3::BoltL4Protocol::InbndRtxTimeout",
                     TimeValue(MicroSeconds(inboundRtxTimeout)));
  Config::SetDefault("ns3::BoltL4Protocol::OutbndRtxTimeout",
                     TimeValue(MicroSeconds(outboundRtxTimeout)));
  Config::SetDefault("ns3::BoltL4Protocol::CcMode", StringValue(ccMode));
  Config::SetDefault("ns3::BoltL4Protocol::RttSmoothingAlpha",
                     DoubleValue(rttSmoothingAlpha));
  Config::SetDefault("ns3::BoltL4Protocol::TopoScalingPerHop",
                     UintegerValue(topoScalingPerHop));
  Config::SetDefault("ns3::BoltL4Protocol::MaxFlowScaling",
                     DoubleValue(maxFlowScaling));
  Config::SetDefault("ns3::BoltL4Protocol::MaxFlowScalingCwnd",
                     DoubleValue(maxFlowScalingCwnd));
  Config::SetDefault("ns3::BoltL4Protocol::MinFlowScalingCwnd",
                     DoubleValue(minFlowScalingCwnd));
  Config::SetDefault("ns3::BoltL4Protocol::BaseDelay",
                     UintegerValue(baseDelay));
  Config::SetDefault("ns3::BoltL4Protocol::AiFactor", DoubleValue(aiFactor));
  Config::SetDefault("ns3::BoltL4Protocol::MdFactor", DoubleValue(mdFactor));
  Config::SetDefault("ns3::BoltL4Protocol::MaxMd", DoubleValue(maxMd));
  Config::SetDefault("ns3::BoltL4Protocol::MaxCwnd", UintegerValue(maxCwnd));
  Config::SetDefault("ns3::BoltL4Protocol::UsePerHopDelayForCc",
                     BooleanValue(usePerHopDelayForCc));

  Config::SetDefault("ns3::Ipv4GlobalRouting::EcmpMode",
                     EnumValue(Ipv4GlobalRouting::ECMP_PER_FLOW));

  InternetStackHelper stack;
  stack.Install(senderNodes);
  stack.Install(receiverNodes);
  stack.Install(switchNodes);

  TrafficControlHelper boltQdisc;
  boltQdisc.SetRootQueueDisc(
      "ns3::PfifoBoltQueueDisc", "MaxSize", StringValue("1000p"), "EnableBts",
      BooleanValue(enableBts), "CcThreshold", StringValue(ccThreshold),
      "EnablePru", BooleanValue(enablePru), "MaxInstAvailLoad",
      IntegerValue(MTU), "EnableAbs", BooleanValue(enableAbs));

  QueueDiscContainer bottleneckQdisc = boltQdisc.Install(switchToSwitchDevices);

  if (traceQueues) {
    Ptr<OutputStreamWrapper> qStream =
        asciiTraceHelper.CreateFileStream(qStreamName);
    bottleneckQdisc.Get(0)->TraceConnectWithoutContext(
        "BytesInQueue", MakeBoundCallback(&BytesInQueueDiscTrace, qStream));
  }

  /******** Set IP addresses of the nodes in the network ********/
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");
  address.Assign(switchToSwitchDevices);

  Ipv4InterfaceContainer senderToSwitchIfs[nFlows];
  for (int i = 0; i < nFlows; i++) {
    address.NewNetwork();
    senderToSwitchIfs[i] = address.Assign(senderToSwitchDevices[i]);
  }

  std::vector<InetSocketAddress> receiverAddresses;
  Ipv4InterfaceContainer receiverToSwitchIfs[nFlows];
  for (int i = 0; i < nFlows; i++) {
    address.NewNetwork();
    receiverToSwitchIfs[i] = address.Assign(receiverToSwitchDevices[i]);

    receiverAddresses.push_back(InetSocketAddress(
          receiverToSwitchIfs[i].GetAddress(0), receiverPortNoStart + i));
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  /******** Create Flows on End-hosts ********/
  NS_LOG_DEBUG("Installing the Applications...");
  PointToPointNetDevice *bottleneckNetDevice =
      dynamic_cast<PointToPointNetDevice *>(&(*(switchToSwitchDevices.Get(0))));
  uint64_t bottleneckBps = bottleneckNetDevice->GetDataRate().GetBitRate();

  BoltHeader bolth;
  Ipv4Header ipv4h;
  uint32_t payloadSize =
      MTU - bolth.GetSerializedSize() - ipv4h.GetSerializedSize();
  double dataToPktRatio = static_cast<double>(payloadSize) /
                                static_cast<double>(MTU);

  /******** Schedule the long lived flows for microbenchmarks ********/
  uint32_t flowSizeBytes = static_cast<uint32_t>(
      flowEndTime * static_cast<double>(bottleneckBps) * dataToPktRatio / 8.0);

  InetSocketAddress receiverAddr =
      InetSocketAddress(receiverToSwitchIfs[0].GetAddress(0), 0); // dummy
  Ptr<Socket> receiverSocket[nFlows];
  Ptr<Socket> senderSocket[nFlows];
  for (int i = 0; i < nFlows; i++) {
    receiverAddr = InetSocketAddress(
      receiverToSwitchIfs[i].GetAddress(0), receiverPortNoStart + i);

    receiverSocket[i] = Socket::CreateSocket(
        receiverNodes.Get(i), BoltSocketFactory::GetTypeId());
    receiverSocket[i]->Bind(receiverAddr);
    receiverSocket[i]->SetRecvCallback(MakeCallback(&ReceiveLongLivedFlow));

    senderSocket[i] = Socket::CreateSocket(senderNodes.Get(i),
                                           BoltSocketFactory::GetTypeId());
    senderSocket[i]->Bind(InetSocketAddress(senderToSwitchIfs[i].GetAddress(0),
                                            senderPortNoStart + i));
    Simulator::Schedule(Seconds(START_TIME), &SendLongLivedFlow,
                        senderSocket[i], receiverAddr, flowSizeBytes * (i + 1));
  }

  if (stopFlowsManually) {
    for (int i = 0; i < nFlows; i++) {
      Simulator::Schedule(Seconds(START_TIME + (i + 1) * flowEndTime),
                          &SetLinkDownToStopFlow, senderToSwitchDevices[i]);
    }
  }

  /******** Set the message traces for the Bolt clients ********/
  Ptr<OutputStreamWrapper> statsStream;
  statsStream = asciiTraceHelper.CreateFileStream(statsTracesFileName);
  Config::ConnectWithoutContext(
      "/NodeList/*/$ns3::BoltL4Protocol/FlowStats",
      MakeBoundCallback(&TraceFlowStats, statsStream));

  double duration = 0.0;
  if (stopFlowsManually) duration = nFlows * flowEndTime;
  Config::ConnectWithoutContext(
      "/NodeList/*/$ns3::BoltL4Protocol/DataPktArrival",
      MakeBoundCallback(&TraceDataArrival, duration));

  /******** Run the Actual Simulation ********/
  NS_LOG_WARN("Running the Simulation...");
  if (stopFlowsManually)
    Simulator::Stop(Seconds(START_TIME + nFlows * flowEndTime));
  Simulator::Run();
  Simulator::Destroy();

  /******** Measure the total utilization of the bottleneck link ********/
  double totalUtilization = (double)totalDataReceived * 8.0 / 1e9 /
                            (lastdataArrivalTime - ONE_WAY_DELAY - START_TIME);
  NS_LOG_UNCOND("Total utilization: " << totalUtilization << "Gbps");

  /******** Measure the tail occupancy of the bottleneck link ********/
  if (traceQueues)
    CalculateTailBottleneckQueueOccupancy(qStreamName, 0.99, bottleneckBps,
                                          duration);

  /***** Measure the actual time the simulation has taken (for reference) *****/
  auto simStop = std::chrono::steady_clock::now();
  auto simTime =
      std::chrono::duration_cast<std::chrono::seconds>(simStop - simStart);
  double simTimeMin = (double)simTime.count() / 60.0;
  NS_LOG_UNCOND("Time taken by simulation: " << simTimeMin << " minutes");

  return 0;
}
