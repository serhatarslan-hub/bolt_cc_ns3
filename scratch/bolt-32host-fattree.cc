/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

// The topology simulated in this experiment involves 32 hosts in a fat tree.
// 1 ToR switch is connected to 8 hosts with 100G links. There are 4 ToR
// switches. 1 Leaf switch is connected to 2 ToRs with 400G links. There are 4
// Leaf switches. 1 Spine switch is connected to all the Leaf switches (4 of
// them) with 400G links. There are 2 Spine switches.

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

NS_LOG_COMPONENT_DEFINE("Bolt32HostsFattreeSimulation");

double lastdataArrivalTime;
uint64_t totalDataReceived = 0;

void TraceMsgBegin(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> msg,
                   Ipv4Address saddr, Ipv4Address daddr, uint16_t sport,
                   uint16_t dport, int txMsgId) {
  Time now = Simulator::Now();

  NS_LOG_INFO("+ " << now.GetNanoSeconds() << " " << msg->GetSize() << " "
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

  NS_LOG_INFO("- " << now.GetNanoSeconds() << " " << msgSize << " " << saddr
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
  if (now.GetSeconds() <= START_TIME + duration) {
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

static void BytesInQueueDiscTrace(Ptr<OutputStreamWrapper> stream, int hostIdx,
                                  size_t sideIdx, uint32_t oldval,
                                  uint32_t newval) {
  Time now = Simulator::Now();

  if (sideIdx == 0) {
    NS_LOG_INFO(now.GetNanoSeconds()
                << " Queue size of host " << hostIdx << " to TOR chenged from "
                << oldval << " to " << newval);
  } else if (sideIdx == 1) {
    NS_LOG_INFO(now.GetNanoSeconds()
                << " Queue size of TOR " << hostIdx << " to host chenged from "
                << oldval << " to " << newval);
  }

  *stream->GetStream() << "que " << now.GetNanoSeconds() << " " << hostIdx
                       << " " << sideIdx << " " << newval << std::endl;
}

static void PruTokensInQueueDiscTrace(Ptr<OutputStreamWrapper> stream,
                                      int hostIdx, size_t sideIdx,
                                      uint16_t oldval, uint16_t newval) {
  Time now = Simulator::Now();

  if (sideIdx == 0) {
    NS_LOG_INFO(now.GetNanoSeconds()
                << " PRU Token of host " << hostIdx << " to TOR chenged from "
                << oldval << " to " << newval);
  } else if (sideIdx == 1) {
    NS_LOG_INFO(now.GetNanoSeconds()
                << " PRU Token of TOR " << hostIdx << " to host chenged from "
                << oldval << " to " << newval);
  }

  *stream->GetStream() << "pru " << now.GetNanoSeconds() << " " << hostIdx
                       << " " << sideIdx << " " << newval << std::endl;
}

static void AbsTokensInQueueDiscTrace(Ptr<OutputStreamWrapper> stream,
                                      int hostIdx, size_t sideIdx, int oldval,
                                      int newval) {
  Time now = Simulator::Now();

  if (sideIdx == 0) {
    NS_LOG_INFO(now.GetNanoSeconds()
                << " ABS Token of host " << hostIdx << " to TOR chenged from "
                << oldval << " to " << newval);
  } else if (sideIdx == 1) {
    NS_LOG_INFO(now.GetNanoSeconds()
                << " ABS Token of TOR " << hostIdx << " to host chenged from "
                << oldval << " to " << newval);
  }

  *stream->GetStream() << "abs " << now.GetNanoSeconds() << " " << hostIdx
                       << " " << sideIdx << " " << newval << std::endl;
}

std::map<double, int> ReadMsgSizeDist(std::string msgSizeDistFileName,
                                      double &avgMsgSize) {
  std::ifstream msgSizeDistFile;
  msgSizeDistFile.open(msgSizeDistFileName);
  NS_LOG_DEBUG("Reading Msg Size Distribution From: " << msgSizeDistFileName);

  std::string line;
  std::istringstream lineBuffer;

  // First line of the given workload always has the average msg size
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

void CalculateTailQueueOccupancy(std::string qStreamName, double percentile,
                                 uint64_t bottleneckBitRate, double duration,
                                 bool simSingleRcvr) {
  std::ifstream qSizeTraceFile;
  qSizeTraceFile.open(qStreamName);
  NS_LOG_DEBUG("Reading Bottleneck Queue Size Trace From: " << qStreamName);

  // NOTE: See BytesInQueueDiscTrace() for the format of the queueing trace

  std::string line;
  std::istringstream lineBuffer;

  std::vector<int> queueSizes;
  std::string logType;
  uint64_t time;
  int hostIdx;
  size_t sideIdx;
  uint32_t qSizeBytes;
  while (getline(qSizeTraceFile, line)) {
    lineBuffer.clear();
    lineBuffer.str(line);
    lineBuffer >> logType;
    lineBuffer >> time;
    lineBuffer >> hostIdx;
    lineBuffer >> sideIdx;
    lineBuffer >> qSizeBytes;
    if (logType == "que" && time < (uint64_t)((START_TIME + duration) * 1e9) &&
        (!simSingleRcvr || (hostIdx == 0 && sideIdx == 1)))
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
  bool use100GLinks = true;  // A more realistic but much slower simulation
  bool simulateSingleReceiver =
      false;  // All hosts to send to a single receiver to accelrate simulation
  double duration = 0.05;
  std::string workload("Google_RPC_readBatch");
  double networkLoad = 0.5;
  uint32_t simIdx = 0;
  bool traceMessages = true;
  bool traceQueues = false;
  bool tracePruTokens = false;
  bool traceAbsTokens = false;
  bool traceFlowStats = false;
  bool debugMode = false;
  uint32_t mtu = 5000;                  // in bytes
  uint32_t bdpBytes = 62388;            // in bytes
  uint64_t inboundRtxTimeout = 25000;   // in microseconds
  uint64_t outboundRtxTimeout = 10000;  // in microseconds

  int nHosts = 32;
  int nTors = 4;
  int nLeafSw = 4;
  int nSpineSw = 2;
  int nLeafPerTor = 2;
  uint16_t portNoStart = 1000;

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
  uint32_t maxCwnd = bdpBytes;        // Default: 373760 Bytes
  bool usePerHopDelayForCc = false;   // Default: false

  bool enableMsgAgg = true;
  bool enableBts = false;
  bool enablePru = false;
  bool enableAbs = false;
  std::string ccThreshold("10KB");

  CommandLine cmd(__FILE__);
  cmd.AddValue("note", "Any note to identify the simulation in the future",
               simNote);
  cmd.AddValue("use10GLinks", "A faster but much less realistic simulation",
               use100GLinks);
  cmd.AddValue("simulateSingleReceiver",
               "Simulate only a single receiver to accelerate simulation",
               simulateSingleReceiver);
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
  cmd.AddValue("dontTraceMessages",
               "Whether to trace the message start and completion during the "
               "simulation.",
               traceMessages);
  cmd.AddValue("traceQueues",
               "Whether to trace the queue lengths during the simulation.",
               traceQueues);
  cmd.AddValue("tracePruTokens",
               "Whether to trace the PRU token values during the simulation.",
               tracePruTokens);
  cmd.AddValue("traceAbsTokens",
               "Whether to trace the ABS token values during the simulation.",
               traceAbsTokens);
  cmd.AddValue("traceFlowStats",
               "Whether to trace the flows stats (cwnd and rtt) during the "
               "simulation.",
               traceFlowStats);
  cmd.AddValue("debug", "Whether to enable detailed pkt traces for debugging",
               debugMode);
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
  cmd.Parse(argc, argv);

  if (ccMode == "DEFAULT") {
    enableBts = true;
    enablePru = true;
    enableAbs = true;
  }

  if (!use100GLinks) {
    mtu = 1500;
    bdpBytes = 19638;
    NS_LOG_UNCOND("10G Links are enabled! Using MTU: "
                  << mtu << " Bytes, BDP: " << bdpBytes << " Bytes.");
  }

  Time::SetResolution(Time::NS);
  LogComponentEnable("Bolt32HostsFattreeSimulation", LOG_LEVEL_WARN);
  LogComponentEnable("MsgGeneratorApp", LOG_LEVEL_WARN);
  LogComponentEnable("BoltSocket", LOG_LEVEL_WARN);
  LogComponentEnable("BoltL4Protocol", LOG_LEVEL_WARN);
  LogComponentEnable("PfifoBoltQueueDisc", LOG_LEVEL_WARN);

  if (debugMode) {
    Packet::EnablePrinting();
    LogComponentEnable("Bolt32HostsFattreeSimulation", LOG_LEVEL_DEBUG);
    NS_LOG_DEBUG("Running in DEBUG Mode!");
    SeedManager::SetRun(0);
  } else {
    SeedManager::SetRun(simIdx);
  }

  // Download the public workloads from:
  // https://github.com/PlatformLab/HomaSimulation
  std::string msgSizeDistFileName("inputs/workloads/");
  msgSizeDistFileName += workload + ".tr";
  std::string tracesFileName("outputs/bolt-32host-fattree/");

  tracesFileName += workload;
  tracesFileName += "_load-" + std::to_string((int)(networkLoad * 100)) + "p";
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
  if (use100GLinks) tracesFileName += "_100G";
  if (!simNote.empty()) {
    tracesFileName += "_" + simNote;
    NS_LOG_UNCOND("Note: " << simNote);
  }

  std::string qStreamName = tracesFileName + ".qlen";
  std::string msgTracesFileName = tracesFileName + ".tr";
  std::string statsTracesFileName = tracesFileName + ".log";

  /******** Create Nodes ********/
  NS_LOG_DEBUG("Creating Nodes...");
  NodeContainer hostNodes;
  hostNodes.Create(nHosts);

  NodeContainer torNodes;
  torNodes.Create(nTors);

  NodeContainer leafNodes;
  leafNodes.Create(nLeafSw);

  NodeContainer spineNodes;
  spineNodes.Create(nSpineSw);

  /******** Create Channels ********/
  NS_LOG_DEBUG("Configuring Channels...");
  PointToPointHelper hostLinks;
  if (use100GLinks) {
    hostLinks.SetDeviceAttribute("DataRate", StringValue("100Gbps"));
    hostLinks.SetChannelAttribute("Delay", StringValue("315ns"));
  } else {
    hostLinks.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    hostLinks.SetChannelAttribute("Delay", StringValue("1us"));
  }
  hostLinks.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

  PointToPointHelper coreLinks;
  if (use100GLinks) {
    coreLinks.SetDeviceAttribute("DataRate", StringValue("400Gbps"));
    coreLinks.SetChannelAttribute("Delay", StringValue("315ns"));
  } else {
    coreLinks.SetDeviceAttribute("DataRate", StringValue("40Gbps"));
    coreLinks.SetChannelAttribute("Delay", StringValue("1us"));
  }
  coreLinks.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

  /******** Create NetDevices ********/
  NS_LOG_DEBUG("Creating NetDevices...");
  NetDeviceContainer hostToTorDevices[nHosts];
  for (int i = 0; i < nHosts; i++) {
    hostToTorDevices[i] =
        hostLinks.Install(hostNodes.Get(i), torNodes.Get(i / (nHosts / nTors)));
    for (uint32_t n = 0; n < hostToTorDevices[i].GetN(); n++)
      hostToTorDevices[i].Get(n)->SetMtu(mtu);
  }

  NetDeviceContainer torToLeafDevices[nTors * nLeafPerTor];
  for (int i = 0; i < nTors; i++) {
    for (int j = 0; j < nLeafPerTor; j++) {
      torToLeafDevices[i * nLeafPerTor + j] = coreLinks.Install(
          torNodes.Get(i), leafNodes.Get((i / nLeafPerTor) * nLeafPerTor + j));
      for (uint32_t n = 0; n < torToLeafDevices[i * nLeafPerTor + j].GetN();
           n++)
        torToLeafDevices[i * nLeafPerTor + j].Get(n)->SetMtu(mtu);
    }
  }

  NetDeviceContainer leafToSpineDevices[nLeafSw * nSpineSw];
  for (int i = 0; i < nLeafSw; i++) {
    for (int j = 0; j < nSpineSw; j++) {
      leafToSpineDevices[i * nSpineSw + j] =
          coreLinks.Install(leafNodes.Get(i), spineNodes.Get(j));
      for (uint32_t n = 0; n < leafToSpineDevices[i * nSpineSw + j].GetN(); n++)
        leafToSpineDevices[i * nSpineSw + j].Get(n)->SetMtu(mtu);
    }
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
  stack.InstallAll();

  TrafficControlHelper boltQdisc;
  boltQdisc.SetRootQueueDisc(
      "ns3::PfifoBoltQueueDisc", "MaxSize", StringValue("1000p"), "EnableBts",
      BooleanValue(enableBts), "CcThreshold", StringValue(ccThreshold),
      "EnablePru", BooleanValue(enablePru), "MaxInstAvailLoad",
      IntegerValue(mtu), "EnableAbs", BooleanValue(enableAbs));

  Ptr<OutputStreamWrapper> qStream =
      asciiTraceHelper.CreateFileStream(qStreamName);
  QueueDiscContainer hostToTorQdisc[nHosts];
  for (int i = 0; i < nHosts; i++) {
    hostToTorQdisc[i] = boltQdisc.Install(hostToTorDevices[i]);

    for (size_t j = 0; j < hostToTorQdisc[i].GetN(); j++) {
      if (traceQueues) {
        hostToTorQdisc[i].Get(j)->TraceConnectWithoutContext(
            "BytesInQueue",
            MakeBoundCallback(&BytesInQueueDiscTrace, qStream, i, j));
      }
      if (tracePruTokens) {
        hostToTorQdisc[i].Get(j)->TraceConnectWithoutContext(
            "PruTokensInQueue",
            MakeBoundCallback(&PruTokensInQueueDiscTrace, qStream, i, j));
      }
      if (traceAbsTokens) {
        hostToTorQdisc[i].Get(j)->TraceConnectWithoutContext(
            "AbsTokensInQueue",
            MakeBoundCallback(&AbsTokensInQueueDiscTrace, qStream, i, j));
      }
    }
  }

  QueueDiscContainer torToLeafQdisc[nTors * nLeafPerTor];
  for (int i = 0; i < nTors * nLeafPerTor; i++) {
    torToLeafQdisc[i] = boltQdisc.Install(torToLeafDevices[i]);
  }

  QueueDiscContainer leafToSpineQdisc[nLeafSw * nSpineSw];
  for (int i = 0; i < nLeafSw * nSpineSw; i++) {
    leafToSpineQdisc[i] = boltQdisc.Install(leafToSpineDevices[i]);
  }

  /******** Set IP addresses of the nodes in the network ********/
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");

  std::vector<InetSocketAddress> hostAddresses;
  Ipv4InterfaceContainer hostToTorIfs[nHosts];
  for (int i = 0; i < nHosts; i++) {
    hostToTorIfs[i] = address.Assign(hostToTorDevices[i]);
    if (!simulateSingleReceiver || i == 0) {
      // Simulate only a single receiver when using 100G links to accelerate sim
      hostAddresses.push_back(
          InetSocketAddress(hostToTorIfs[i].GetAddress(0), portNoStart + i));
    }
    address.NewNetwork();
  }

  Ipv4InterfaceContainer torToLeafIfs[nTors * nLeafPerTor];
  for (int i = 0; i < nTors * nLeafPerTor; i++) {
    torToLeafIfs[i] = address.Assign(torToLeafDevices[i]);
    address.NewNetwork();
  }

  Ipv4InterfaceContainer LeafToSpineIfs[nLeafSw * nSpineSw];
  for (int i = 0; i < nLeafSw * nSpineSw; i++) {
    LeafToSpineIfs[i] = address.Assign(leafToSpineDevices[i]);
    address.NewNetwork();
  }

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
  PointToPointNetDevice *hostNetDevice =
      dynamic_cast<PointToPointNetDevice *>(&(*(hostToTorDevices[0].Get(0))));
  uint64_t hostBps = hostNetDevice->GetDataRate().GetBitRate();

  BoltHeader bolth;
  Ipv4Header ipv4h;
  uint32_t payloadSize =
      mtu - bolth.GetSerializedSize() - ipv4h.GetSerializedSize();
  Config::SetDefault("ns3::MsgGeneratorApp::PayloadSize",
                     UintegerValue(payloadSize));
  // Config::SetDefault("ns3::MsgGeneratorApp::MaxMsg", UintegerValue(1));
  // Config::SetDefault("ns3::MsgGeneratorApp::StaticMsgSize",
  // UintegerValue(125000));
  Config::SetDefault("ns3::MsgGeneratorApp::UnitsInBytes", BooleanValue(true));

  Ptr<MsgGeneratorApp> app;
  for (int i = 0; i < nHosts; i++) {
    app = CreateObject<MsgGeneratorApp>(hostToTorIfs[i].GetAddress(0),
                                        portNoStart + i);
    app->Install(hostNodes.Get(i), hostAddresses);
    if (!simulateSingleReceiver) {
      app->SetWorkload(networkLoad, msgSizeCDF, avgMsgSizeBytes, hostBps);
    } else if (i != 0) {
      // Simulate only a single receiver when using 100G links to accelerate sim
      app->SetWorkload(networkLoad / static_cast<double>(nHosts - 1),
                       msgSizeCDF, avgMsgSizeBytes, hostBps);
    }

    app->Start(Seconds(START_TIME));
    app->Stop(Seconds(START_TIME + duration));
  }

  /******** Set the message traces for the Bolt clients ********/
  if (traceMessages) {
    Ptr<OutputStreamWrapper> msgStream;
    msgStream = asciiTraceHelper.CreateFileStream(msgTracesFileName);
    Config::ConnectWithoutContext("/NodeList/*/$ns3::BoltL4Protocol/MsgBegin",
                                  MakeBoundCallback(&TraceMsgBegin, msgStream));
    // Config::ConnectWithoutContext(
    //     "/NodeList/*/$ns3::BoltL4Protocol/MsgFinish",
    //     MakeBoundCallback(&TraceMsgFinish, msgStream));
    Config::ConnectWithoutContext("/NodeList/*/$ns3::BoltL4Protocol/MsgAcked",
                                  MakeBoundCallback(&TraceMsgAcked, msgStream));
  }
  if (traceFlowStats) {
    Ptr<OutputStreamWrapper> statsStream;
    statsStream = asciiTraceHelper.CreateFileStream(statsTracesFileName);
    Config::ConnectWithoutContext(
        "/NodeList/*/$ns3::BoltL4Protocol/FlowStats",
        MakeBoundCallback(&TraceFlowStats, statsStream));
  }
  Config::ConnectWithoutContext(
      "/NodeList/*/$ns3::BoltL4Protocol/DataPktArrival",
      MakeBoundCallback(&TraceDataArrival, duration));

  /******** Run the Actual Simulation ********/
  NS_LOG_WARN("Running the Simulation...");
  Simulator::Stop(Seconds(START_TIME + duration));
  Simulator::Run();
  Simulator::Destroy();

  /******** Measure the total utilization of the network ********/
  double totalUtilization = (double)totalDataReceived * 8.0 / 1e9 /
                            (lastdataArrivalTime - START_TIME);
  NS_LOG_UNCOND("Total utilization: " << totalUtilization << "Gbps");

  /******** Measure the tail occupancy of the bottleneck link ********/
  if (traceQueues)
    CalculateTailQueueOccupancy(qStreamName, 0.99, hostBps, duration,
                                simulateSingleReceiver);

  /***** Measure the actual time the simulation has taken (for reference) *****/
  auto simStop = std::chrono::steady_clock::now();
  auto simTime =
      std::chrono::duration_cast<std::chrono::seconds>(simStop - simStart);
  double simTimeMin = (double)simTime.count() / 60.0;
  NS_LOG_UNCOND("Time taken by simulation: " << simTimeMin << " minutes");

  return 0;
}
