/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

// The topology generated in this experiment is a dumbbell topology where the
// bottleneck link in the middle has 25Gbps bandwidth, and the RTT of flows
// are 12 usec.
//
// The benchmark simply runs 4 flows on the given topology. Then statistics
// about the flows (ie. cwnd, rtt etc.) are collected to see how different
// algorithms behave.
//
// Example to run the simulation:
// $./waf --run "scratch/bolt-testbed-benchmarks --duration=0.05 --ccMode=SWIFT
// --workload=Google_RPC_read"

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
#define ONE_WAY_DELAY 7180e-9

uint32_t mtu;
double lastdataArrivalTime;
uint64_t totalDataReceived = 0;
double lastBtsDepartureTime;
uint64_t totalBtsSize = 0;

NS_LOG_COMPONENT_DEFINE("BoltTestbedBenchmarks");

void TraceMsgBegin(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> msg,
                   Ipv4Address saddr, Ipv4Address daddr, uint16_t sport,
                   uint16_t dport, int txMsgId) {
  Time now = Simulator::Now();

  NS_LOG_DEBUG("+ " << now.GetNanoSeconds() << " " << msg->GetSize() << " "
                    << saddr << ":" << sport << " " << daddr << ":" << dport
                    << " " << txMsgId);

  *stream->GetStream() << "+ " << now.GetNanoSeconds() << " " << msg->GetSize()
                       << " " << saddr << ":" << sport << " " << daddr << ":"
                       << dport << " " << txMsgId << std::endl;
}

void TraceMsgAcked(Ptr<OutputStreamWrapper> stream, uint32_t msgSize,
                   Ipv4Address saddr, Ipv4Address daddr, uint16_t sport,
                   uint16_t dport, int txMsgId) {
  Time now = Simulator::Now();

  NS_LOG_DEBUG("- " << now.GetNanoSeconds() << " " << msgSize << " " << saddr
                    << ":" << sport << " " << daddr << ":" << dport << " "
                    << txMsgId);

  *stream->GetStream() << "- " << now.GetNanoSeconds() << " " << msgSize << " "
                       << saddr << ":" << sport << " " << daddr << ":" << dport
                       << " " << txMsgId << std::endl;
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

static void BtsDepartureTrace(double duration, uint32_t nBtsInFlight,
                              uint32_t curQLen) {
  Time now = Simulator::Now();
  if (now.GetSeconds() <= START_TIME + duration) {
    lastBtsDepartureTime = now.GetSeconds();

    Ipv4Header ipv4h;  // Consider the total pkt size for throughput
    BoltHeader bolth;
    totalBtsSize += bolth.GetSerializedSize() + ipv4h.GetSerializedSize();
  }
}

static void BytesInQueueDiscTrace(Ptr<OutputStreamWrapper> stream,
                                  uint32_t oldval, uint32_t newval) {
  NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
              << " Queue Disc size from " << oldval << " to " << newval);

  *stream->GetStream() << Simulator::Now().GetNanoSeconds() << " " << newval
                       << std::endl;
}

std::map<double, int> ReadMsgSizeDist(std::string msgSizeDistFileName,
                                      double &avgMsgSize) {
  std::ifstream msgSizeDistFile;
  msgSizeDistFile.open(msgSizeDistFileName);
  NS_LOG_FUNCTION(
      "Reading Msg Size Distribution From: " << msgSizeDistFileName);

  std::string line;
  std::istringstream lineBuffer;

  getline(msgSizeDistFile, line);
  lineBuffer.str(line);
  lineBuffer >> avgMsgSize;

  std::map<double, int> msgSizeCDF;
  double prob;
  int msgSize;
  while (getline(msgSizeDistFile, line)) {
    lineBuffer.clear();
    lineBuffer.str(line);
    lineBuffer >> msgSize;
    lineBuffer >> prob;
    msgSizeCDF[prob] = msgSize;
  }
  msgSizeDistFile.close();

  return msgSizeCDF;
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
  double duration = 0.05;
  // Download the workload from:
  // https://github.com/PlatformLab/HomaSimulation
  std::string workload("Facebook_Hadoop");
  double networkLoad = 0.8;
  uint32_t simIdx = 0;
  bool traceMessages = false;
  bool traceFlowStats = true;
  bool traceQueues = true;
  bool traceBtsDeparture = true;
  bool debugMode = false;
  uint32_t bdpBytes = 45000;            // in bytes
  uint64_t inboundRtxTimeout = 25000;   // in microseconds
  uint64_t outboundRtxTimeout = 10000;  // in microseconds

  int nFlows = 4;
  int senderPortNoStart = 1000;
  int receiverPortNoStart = 2000;
  mtu = 5000;  // in bytes

  std::string ccMode("SWIFT");
  /* Bolt (Swift) Related Parameters */
  double rttSmoothingAlpha = 0.75;    // Default: 0.75
  uint16_t topoScalingPerHop = 1000;  // Default: 1000 ns
  double maxFlowScaling = 100000.0;   // Default: 100000.0
  double maxFlowScalingCwnd = 256.0;  // Default: 256.0 pkts
  double minFlowScalingCwnd = 0.1;    // Default: 0.1 pkts
  uint64_t baseDelay = 50000;         // Default: 50000 ns (50 usec)
  double aiFactor = 1.0;              // Default: 1.0
  double mdFactor = 0.8;              // Default: 0.8
  double maxMd = 0.5;                 // Default: 0.5
  uint32_t maxCwnd = 373760;          // Default: 373760 Bytes
  bool usePerHopDelayForCc = false;   // Default: false

  bool enableMsgAgg = true;
  bool enableBts = false;
  bool enablePru = false;
  bool enableAbs = false;
  std::string ccThreshold("10KB");  // 2 packets
  uint32_t reducedBtsFactor = 2;

  CommandLine cmd(__FILE__);
  cmd.AddValue("note", "Any note to identify the simulation in the future",
               simNote);
  cmd.AddValue("duration", "The maximum duration of the simulation in seconds.",
               duration);
  cmd.AddValue("workload", "The workload to simulate the network with.",
               workload);
  cmd.AddValue("load",
               "The network load to simulate the network at, ie 0.5 for 50%.",
               networkLoad);
  cmd.AddValue("simIdx",
               "The index of the simulation used to identify parallel runs.",
               simIdx);
  cmd.AddValue("nFlows", "Number of flows in the topology.", nFlows);
  cmd.AddValue("traceMessages",
               "Whether to trace the message start and completion during the "
               "simulation.",
               traceMessages);
  cmd.AddValue("dontTraceFlowStats",
               "Whether to trace the flows stats (cwnd and rtt) during the "
               "simulation.",
               traceFlowStats);
  cmd.AddValue("dontTraceQueues",
               "Whether to trace the queue lengths during the simulation.",
               traceQueues);
  cmd.AddValue("dontTraceBtsDeparture",
               "Whether to trace the BTS send events during the simulation.",
               traceBtsDeparture);
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
  cmd.AddValue("disableMsgAgg",
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
  cmd.AddValue("reducedBtsFactor", "Period of BTS transmission.",
               reducedBtsFactor);
  cmd.AddValue("mtu", "The MTU, in bytes, to be used throughout the simulation",
               mtu);
  cmd.Parse(argc, argv);

  if (ccMode == "DEFAULT") {
    NS_LOG_DEBUG("Bolt uses BTS, PRU, and ABS mechanisms by default!");
    enableBts = true;
    enablePru = true;
    enableAbs = true;
  }

  Time::SetResolution(Time::NS);
  LogComponentEnable("BoltTestbedBenchmarks", LOG_LEVEL_WARN);
  LogComponentEnable("MsgGeneratorApp", LOG_LEVEL_WARN);
  LogComponentEnable("BoltSocket", LOG_LEVEL_WARN);
  LogComponentEnable("BoltL4Protocol", LOG_LEVEL_WARN);
  LogComponentEnable("PfifoBoltQueueDisc", LOG_LEVEL_WARN);

  if (debugMode) {
    LogComponentEnable("BoltTestbedBenchmarks", LOG_LEVEL_DEBUG);
    NS_LOG_DEBUG("Running in DEBUG Mode!");
    SeedManager::SetRun(0);
  } else {
    SeedManager::SetRun(simIdx);
  }

  std::string msgSizeDistFileName("inputs/workloads/");
  msgSizeDistFileName += workload + ".tr";
  std::string tracesFileName("outputs/bolt-testbed-benchmarks/");
  tracesFileName += workload;
  tracesFileName += "_load-" + std::to_string((int)(networkLoad * 100)) + "p";
  tracesFileName += "_nFlows-" + std::to_string(nFlows);
  if (debugMode)
    tracesFileName += "_debug";
  else
    tracesFileName += "_" + std::to_string(simIdx);
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
  std::string msgTracesFileName = tracesFileName + ".tr";
  std::string statsTracesFileName = tracesFileName + ".log";

  /******** Create Nodes ********/
  NS_LOG_DEBUG("Creating Nodes...");
  NodeContainer senderNodes;
  senderNodes.Create(nFlows);

  NodeContainer receiverNodes;
  receiverNodes.Create(1);

  NodeContainer switchNodes;
  switchNodes.Create(2);

  /******** Create Channels ********/
  NS_LOG_DEBUG("Configuring Channels...");
  PointToPointHelper hostLinks;
  hostLinks.SetDeviceAttribute("DataRate", StringValue("100Gbps"));
  hostLinks.SetChannelAttribute("Delay", StringValue("1800ns"));
  hostLinks.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));

  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute("DataRate", StringValue("25Gbps"));
  bottleneckLink.SetChannelAttribute("Delay", StringValue("2200ns"));
  bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

  /******** Create NetDevices ********/
  NS_LOG_DEBUG("Creating NetDevices...");
  NetDeviceContainer switchToSwitchDevices;
  switchToSwitchDevices = bottleneckLink.Install(switchNodes);
  for (uint32_t n = 0; n < switchToSwitchDevices.GetN(); n++)
    switchToSwitchDevices.Get(n)->SetMtu(mtu);

  NetDeviceContainer senderToSwitchDevices[nFlows];
  for (int i = 0; i < nFlows; i++) {
    senderToSwitchDevices[i] =
        hostLinks.Install(senderNodes.Get(i), switchNodes.Get(0));
    for (uint32_t n = 0; n < senderToSwitchDevices[i].GetN(); n++)
      senderToSwitchDevices[i].Get(n)->SetMtu(mtu);
  }

  NetDeviceContainer receiverToSwitchDevices;
  receiverToSwitchDevices =
      hostLinks.Install(receiverNodes.Get(0), switchNodes.Get(1));
  for (uint32_t n = 0; n < receiverToSwitchDevices.GetN(); n++)
    receiverToSwitchDevices.Get(n)->SetMtu(mtu);

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
  // double targetQBytes =
  // static_cast<double>(QueueSize(ccThreshold).GetValue());
  // Config::SetDefault("ns3::BoltL4Protocol::TargetQ",
  // DoubleValue(targetQBytes));

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
      IntegerValue(mtu), "EnableAbs", BooleanValue(enableAbs),
      "ReducedBtsFactor", UintegerValue(reducedBtsFactor));

  QueueDiscContainer bottleneckQdisc = boltQdisc.Install(switchToSwitchDevices);

  if (traceQueues) {
    Ptr<OutputStreamWrapper> qStream =
        asciiTraceHelper.CreateFileStream(qStreamName);
    bottleneckQdisc.Get(0)->TraceConnectWithoutContext(
        "BytesInQueue", MakeBoundCallback(&BytesInQueueDiscTrace, qStream));
  }
  if (traceBtsDeparture) {
    bottleneckQdisc.Get(0)->TraceConnectWithoutContext(
        "BtsDeparture", MakeBoundCallback(&BtsDepartureTrace, duration));
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
  Ipv4InterfaceContainer receiverToSwitchIfs;
  address.NewNetwork();
  receiverToSwitchIfs = address.Assign(receiverToSwitchDevices);

  receiverAddresses.push_back(InetSocketAddress(
      receiverToSwitchIfs.GetAddress(0), receiverPortNoStart));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  /******** Read the Workload Distribution From File ********/
  NS_LOG_DEBUG("Reading Msg Size Distribution...");
  double avgMsgSizeBytes;
  std::map<double, int> msgSizeCDF =
      ReadMsgSizeDist(msgSizeDistFileName, avgMsgSizeBytes);

  NS_LOG_LOGIC("The CDF of message sizes is given below: ");
  for (auto it = msgSizeCDF.begin(); it != msgSizeCDF.end(); it++) {
    NS_LOG_LOGIC(it->second << " : " << it->first);
  }
  NS_LOG_LOGIC("Average Message Size is: " << avgMsgSizeBytes);

  /******** Create Message Generator Apps on End-hosts ********/
  NS_LOG_DEBUG("Installing the Applications...");
  PointToPointNetDevice *bottleneckNetDevice =
      dynamic_cast<PointToPointNetDevice *>(&(*(switchToSwitchDevices.Get(0))));
  uint64_t bottleneckBps = bottleneckNetDevice->GetDataRate().GetBitRate();

  BoltHeader bolth;
  Ipv4Header ipv4h;
  uint32_t payloadSize =
      mtu - bolth.GetSerializedSize() - ipv4h.GetSerializedSize();
  Config::SetDefault("ns3::MsgGeneratorApp::PayloadSize",
                     UintegerValue(payloadSize));
  Config::SetDefault("ns3::MsgGeneratorApp::UnitsInBytes", BooleanValue(true));

  Ptr<MsgGeneratorApp> app;
  for (int i = 0; i < nFlows; i++) {
    app = CreateObject<MsgGeneratorApp>(senderToSwitchIfs[i].GetAddress(0),
                                        senderPortNoStart + i);
    app->Install(senderNodes.Get(i), receiverAddresses);
    app->SetWorkload(networkLoad / static_cast<double>(nFlows),
                      msgSizeCDF, avgMsgSizeBytes, bottleneckBps);
    app->Start(Seconds(START_TIME));
    app->Stop(Seconds(START_TIME + duration));
  }
  app = CreateObject<MsgGeneratorApp>(receiverToSwitchIfs.GetAddress(0),
                                      receiverPortNoStart);
  app->Install(receiverNodes.Get(0), receiverAddresses);
  app->Start(Seconds(START_TIME));
  app->Stop(Seconds(START_TIME + duration));

  /******** Set the message traces for the Bolt clients ********/
  if (traceMessages) {
    Ptr<OutputStreamWrapper> msgStream;
    msgStream = asciiTraceHelper.CreateFileStream(msgTracesFileName);
    Config::ConnectWithoutContext("/NodeList/*/$ns3::BoltL4Protocol/MsgBegin",
                                  MakeBoundCallback(&TraceMsgBegin, msgStream));
    Config::ConnectWithoutContext("/NodeList/*/$ns3::BoltL4Protocol/MsgAcked",
                                  MakeBoundCallback(&TraceMsgAcked, msgStream));
  }
  Config::ConnectWithoutContext(
      "/NodeList/*/$ns3::BoltL4Protocol/DataPktArrival",
      MakeBoundCallback(&TraceDataArrival, duration));
  if (traceFlowStats) {
    Ptr<OutputStreamWrapper> statsStream;
    statsStream = asciiTraceHelper.CreateFileStream(statsTracesFileName);
    Config::ConnectWithoutContext(
        "/NodeList/*/$ns3::BoltL4Protocol/FlowStats",
        MakeBoundCallback(&TraceFlowStats, statsStream));
  }

  /******** Run the Actual Simulation ********/
  NS_LOG_WARN("Running the Simulation...");
  Simulator::Stop(Seconds(START_TIME + duration));
  Simulator::Run();
  Simulator::Destroy();

  /******** Measure the total utilization of the bottleneck link ********/
  double totalUtilization = (double)totalDataReceived * 8.0 / 1e9 /
                            (lastdataArrivalTime - ONE_WAY_DELAY - START_TIME);
  NS_LOG_UNCOND("Total utilization: " << totalUtilization << "Gbps");

  /******** Measure the tail occupancy of the bottleneck link ********/
  if (traceQueues) {
    CalculateTailBottleneckQueueOccupancy(qStreamName, 0.50, bottleneckBps,
                                          duration);
    CalculateTailBottleneckQueueOccupancy(qStreamName, 0.99, bottleneckBps,
                                          duration);
  }

  /******** Measure the bandwidth occupied by the BTS packets ********/
  if (traceBtsDeparture) {
    double btsBw =
        (double)totalBtsSize * 8.0 / 1e9 / (lastBtsDepartureTime - START_TIME);
    NS_LOG_UNCOND("The BTS bandwidth: " << btsBw << "Gbps");
  }

  /***** Measure the actual time the simulation has taken (for reference) *****/
  auto simStop = std::chrono::steady_clock::now();
  auto simTime =
      std::chrono::duration_cast<std::chrono::seconds>(simStop - simStart);
  double simTimeMin = (double)simTime.count() / 60.0;
  NS_LOG_UNCOND("Time taken by simulation: " << simTimeMin << " minutes");

  return 0;
}
