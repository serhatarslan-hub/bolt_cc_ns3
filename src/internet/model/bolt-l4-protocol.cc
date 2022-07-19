/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#include "bolt-l4-protocol.h"

#include <algorithm>

#include "bolt-socket-factory.h"
#include "bolt-socket.h"
#include "ipv4-end-point-demux.h"
#include "ipv4-end-point.h"
#include "ipv4-l3-protocol.h"
#include "ns3/assert.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/ipv4-route.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/object-vector.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ppp-header.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BoltL4Protocol");

NS_OBJECT_ENSURE_REGISTERED(BoltL4Protocol);

/* The protocol is not standardized yet. Using a temporary number */
const uint8_t BoltL4Protocol::PROT_NUMBER = BoltHeader::PROT_NUMBER;

TypeId BoltL4Protocol::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::BoltL4Protocol")
          .SetParent<IpL4Protocol>()
          .SetGroupName("Internet")
          .AddConstructor<BoltL4Protocol>()
          .AddAttribute("SocketList",
                        "The list of sockets associated to this protocol.",
                        ObjectVectorValue(),
                        MakeObjectVectorAccessor(&BoltL4Protocol::m_sockets),
                        MakeObjectVectorChecker<BoltSocket>())
          .AddAttribute(
              "BandwidthDelayProduct",
              "The number of bytes it takes to transmit at line rate, ie. BDP.",
              UintegerValue(312500),
              MakeUintegerAccessor(&BoltL4Protocol::m_bdp),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "InbndRtxTimeout",
              "Time value for the retransmission timeout of InboundMsgs",
              TimeValue(MilliSeconds(1)),
              MakeTimeAccessor(&BoltL4Protocol::m_inboundRtxTimeout),
              MakeTimeChecker(MicroSeconds(0)))
          .AddAttribute("OutbndRtxTimeout",
                        "Time value to determine the timeout of OutboundMsgs",
                        TimeValue(MilliSeconds(10)),
                        MakeTimeAccessor(&BoltL4Protocol::m_outboundRtxTimeout),
                        MakeTimeChecker(MicroSeconds(0)))
          .AddAttribute(
              "MaxRtxCnt",
              "Maximum allowed consecutive rtx timeout count per message",
              UintegerValue(5),
              MakeUintegerAccessor(&BoltL4Protocol::m_maxNumRtxPerMsg),
              MakeUintegerChecker<uint16_t>())
          .AddAttribute(
              "OptimizeMemory",
              "High performant mode (only packet sizes stored to save memory).",
              BooleanValue(true),
              MakeBooleanAccessor(&BoltL4Protocol::m_memIsOptimized),
              MakeBooleanChecker())
          .AddAttribute("CcMode", "Type of congestion control algorithm to run",
                        EnumValue(BoltL4Protocol::CC_DEFAULT),
                        MakeEnumAccessor(&BoltL4Protocol::m_ccMode),
                        MakeEnumChecker(BoltL4Protocol::CC_DEFAULT, "DEFAULT",
                                        BoltL4Protocol::CC_SWIFT, "SWIFT"))
          .AddAttribute("AggregateMsgsIfPossible",
                        "Whether to aggregate messages of the same recipient "
                        "into one flow.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&BoltL4Protocol::m_msgAggEnabled),
                        MakeBooleanChecker())
          .AddAttribute(
              "RttSmoothingAlpha", "Smoothing factor for the RTT measurements",
              DoubleValue(0.75),
              MakeDoubleAccessor(&BoltL4Protocol::m_rttSmoothingAlpha),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "TopoScalingPerHop",
              "Per hop scaling for target delay calculation (in nanoseconds)",
              UintegerValue(1000),
              MakeUintegerAccessor(&BoltL4Protocol::m_topoScalingPerHop),
              MakeUintegerChecker<uint16_t>())
          .AddAttribute("MaxFlowScaling",
                        "Maximum flow scaling factor (in nanoseconds)",
                        DoubleValue(100000.0),
                        MakeDoubleAccessor(&BoltL4Protocol::m_maxFlowScaling),
                        MakeDoubleChecker<double>())
          .AddAttribute(
              "MaxFlowScalingCwnd", "Max cwnd range for flow scaling (in pkts)",
              DoubleValue(256.0),
              MakeDoubleAccessor(&BoltL4Protocol::m_maxFlowScalingCwnd),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "MinFlowScalingCwnd", "Min cwnd range for flow scaling (in pkts)",
              DoubleValue(0.1),
              MakeDoubleAccessor(&BoltL4Protocol::m_minFlowScalingCwnd),
              MakeDoubleChecker<double>())
          .AddAttribute("BaseDelay", "Base delay (in nanoseconds)",
                        UintegerValue(25000),
                        MakeUintegerAccessor(&BoltL4Protocol::m_baseDelay),
                        MakeUintegerChecker<uint64_t>())
          .AddAttribute(
              "AiFactor", "Additive increase factor for congestion control",
              DoubleValue(1.0), MakeDoubleAccessor(&BoltL4Protocol::m_aiFactor),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "MdFactor", "Multiplicative decrease for congestion control",
              DoubleValue(0.8), MakeDoubleAccessor(&BoltL4Protocol::m_mdFactor),
              MakeDoubleChecker<double>())
          .AddAttribute("MaxMd", "Maximum multiplicative decrease allowed",
                        DoubleValue(0.5),
                        MakeDoubleAccessor(&BoltL4Protocol::m_maxMd),
                        MakeDoubleChecker<double>())
          .AddAttribute("MinCwnd", "Minimum cwnd allowed (in bytes)",
                        UintegerValue(15),
                        MakeUintegerAccessor(&BoltL4Protocol::m_minCwnd),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("MaxCwnd", "Maximum cwnd allowed (in bytes)",
                        UintegerValue(373760),  // ~256 packets
                        MakeUintegerAccessor(&BoltL4Protocol::m_maxCwnd),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "UsePerHopDelayForCc",
              "Whether to use per hop delay instead of RTT for CC.",
              BooleanValue(false),
              MakeBooleanAccessor(&BoltL4Protocol::m_usePerHopDelayForCc),
              MakeBooleanChecker())
          .AddAttribute(
              "TargetQ", "The target queue size in bytes when handling BTS",
              DoubleValue(0.0), MakeDoubleAccessor(&BoltL4Protocol::m_targetQ),
              MakeDoubleChecker<double>())
          .AddTraceSource(
              "MsgBegin",
              "Trace source indicating a message has been delivered to "
              "the BoltL4Protocol by the sender application layer.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_msgBeginTrace),
              "ns3::BoltL4Protocol::MsgBeginTracedCallback")
          .AddTraceSource(
              "MsgFinish",
              "Trace source indicating a message has been delivered to "
              "the receiver application by the BoltL4Protocol layer.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_msgFinishTrace),
              "ns3::BoltL4Protocol::MsgFinishTracedCallback")
          .AddTraceSource(
              "MsgAcked",
              "Trace source indicating a message has been ackowledged by "
              "the receiver and the ack has been delivered to the sender.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_msgAckedTrace),
              "ns3::BoltL4Protocol::MsgAckTracedCallback")
          .AddTraceSource(
              "DataPktArrival",
              "Trace source indicating a DATA packet has arrived "
              "to the BoltL4Protocol layer.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_dataRecvTrace),
              "ns3::BoltL4Protocol::DataArrivalTracedCallback")
          .AddTraceSource(
              "DataPktDeparture",
              "Trace source indicating a DATA packet has departed "
              "from the BoltL4Protocol layer.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_dataSendTrace),
              "ns3::BoltL4Protocol::DataDepartureTracedCallback")
          .AddTraceSource(
              "CtrlPktArrival",
              "Trace source indicating a control packet has arrived "
              "to the BoltL4Protocol layer.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_ctrlRecvTrace),
              "ns3::BoltL4Protocol::CtrlArrivalTracedCallback")
          .AddTraceSource(
              "CtrlPktDeparture",
              "Trace source indicating a control packet has departed "
              "from the BoltL4Protocol layer.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_ctrlSendTrace),
              "ns3::BoltL4Protocol::CtrlDepartureTracedCallback")
          .AddTraceSource(
              "FlowStats",
              "Trace source for monitoring the instantaneous statistics of the "
              "flow such as cwnd and rtt.",
              MakeTraceSourceAccessor(&BoltL4Protocol::m_flowStatsTrace),
              "ns3::BoltL4Protocol::FlowStatsTracedCallback");
  return tid;
}

BoltL4Protocol::BoltL4Protocol() : m_endPoints(new Ipv4EndPointDemux()) {
  NS_LOG_FUNCTION(this);

  m_sendScheduler = CreateObject<BoltSendScheduler>(this);
  m_recvScheduler = CreateObject<BoltRecvScheduler>(this);
}

BoltL4Protocol::~BoltL4Protocol() { NS_LOG_FUNCTION_NOARGS(); }

void BoltL4Protocol::SetNode(Ptr<Node> node) {
  m_node = node;

  Ptr<NetDevice> netDevice = m_node->GetDevice(0);
  m_mtu = netDevice->GetMtu();

  PointToPointNetDevice *p2pNetDevice =
      dynamic_cast<PointToPointNetDevice *>(&(*(netDevice)));
  m_linkRate = p2pNetDevice->GetDataRate();

  BoltHeader bolth;
  Ipv4Header ipv4h;
  m_maxPayloadSize =
      m_mtu - bolth.GetSerializedSize() - ipv4h.GetSerializedSize();

  m_flowScalingAlpha = m_maxFlowScaling / (1 / std::sqrt(m_minFlowScalingCwnd) -
                                           1 / std::sqrt(m_maxFlowScalingCwnd));
  m_flowScalingBeta = -m_flowScalingAlpha / std::sqrt(m_maxFlowScalingCwnd);

  // Receiver should piggyback the following flags back to the sender
  m_flagsToReflect =
      BoltHeader::Flags_t::FIN | BoltHeader::Flags_t::FIRST |
      BoltHeader::Flags_t::LAST | BoltHeader::Flags_t::INCWIN |
      BoltHeader::Flags_t::DECWIN | BoltHeader::Flags_t::AI |
      BoltHeader::Flags_t::LINK10G | BoltHeader::Flags_t::LINK25G |
      BoltHeader::Flags_t::LINK40G | BoltHeader::Flags_t::LINK100G |
      BoltHeader::Flags_t::LINK400G;
}

Ptr<Node> BoltL4Protocol::GetNode(void) const { return m_node; }

uint32_t BoltL4Protocol::GetMtu(void) const { return m_mtu; }

uint32_t BoltL4Protocol::GetBdp(void) const { return m_bdp; }

int BoltL4Protocol::GetProtocolNumber(void) const { return PROT_NUMBER; }

uint16_t BoltL4Protocol::GetFlagsToReflect(void) const {
  return m_flagsToReflect;
}

Time BoltL4Protocol::GetInboundRtxTimeout(void) const {
  return m_inboundRtxTimeout;
}

Time BoltL4Protocol::GetOutboundRtxTimeout(void) const {
  return m_outboundRtxTimeout;
}

uint16_t BoltL4Protocol::GetMaxNumRtxPerMsg(void) const {
  return m_maxNumRtxPerMsg;
}

uint32_t BoltL4Protocol::GetMaxPayloadSize(void) const {
  return m_maxPayloadSize;
}

uint32_t BoltL4Protocol::GetMsgSizePkts(uint32_t msgSizeByte) {
  return msgSizeByte / m_maxPayloadSize + (msgSizeByte % m_maxPayloadSize != 0);
}

uint64_t BoltL4Protocol::GetBitPerNsFromFlag(uint16_t flag) {
  NS_LOG_FUNCTION(this << BoltHeader::FlagsToString(flag));

  if (flag & BoltHeader::Flags_t::LINK10G)
    return 10;
  else if (flag & BoltHeader::Flags_t::LINK25G)
    return 25;
  else if (flag & BoltHeader::Flags_t::LINK40G)
    return 40;
  else if (flag & BoltHeader::Flags_t::LINK100G)
    return 100;
  else if (flag & BoltHeader::Flags_t::LINK400G)
    return 400;
  else
    NS_ASSERT_MSG(false, "Error: Bitrate not recognized for Bolt flag: "
                             << BoltHeader::FlagsToString(flag));
}

DataRate BoltL4Protocol::GetLinkRate(void) const { return m_linkRate; }

enum BoltL4Protocol::CcMode_e BoltL4Protocol::GetCcMode(void) const {
  return m_ccMode;
}

bool BoltL4Protocol::AggregateMsgsIfPossible(void) const {
  return m_msgAggEnabled;
}

double BoltL4Protocol::GetRttSmoothingAlpha(void) const {
  return m_rttSmoothingAlpha;
}

uint16_t BoltL4Protocol::GetTopoScalingPerHop(void) const {
  return m_topoScalingPerHop;
}

double BoltL4Protocol::GetFlowScalingAlpha(void) const {
  return m_flowScalingAlpha;
}

double BoltL4Protocol::GetFlowScalingBeta(void) const {
  return m_flowScalingBeta;
}

double BoltL4Protocol::GetMaxFlowScaling(void) const {
  return m_maxFlowScaling;
}

uint64_t BoltL4Protocol::GetBaseDelay(void) const { return m_baseDelay; }

double BoltL4Protocol::GetAiFactor(void) const { return m_aiFactor; }

double BoltL4Protocol::GetMdFactor(void) const { return m_mdFactor; }

double BoltL4Protocol::GetMaxMd(void) const { return m_maxMd; }

uint32_t BoltL4Protocol::GetMinCwnd(void) const { return m_minCwnd; }

uint32_t BoltL4Protocol::GetMaxCwnd(void) const { return m_maxCwnd; }

bool BoltL4Protocol::ShouldUsePerHopDelayForCc(void) const {
  return m_usePerHopDelayForCc;
}

double BoltL4Protocol::GetTargetQ(void) const { return m_targetQ; }

Ptr<BoltSendScheduler> BoltL4Protocol::GetSendScheduler(void) const {
  return m_sendScheduler;
}

Ptr<BoltRecvScheduler> BoltL4Protocol::GetRecvScheduler(void) const {
  return m_recvScheduler;
}

bool BoltL4Protocol::MemIsOptimized(void) const { return m_memIsOptimized; }

/*
 * This method is called by AggregateObject and completes the aggregation
 * by setting the node in the bolt stack and link it to the ipv4 object
 * present in the node along with the socket factory
 */
void BoltL4Protocol::NotifyNewAggregate() {
  NS_LOG_FUNCTION(this);
  Ptr<Node> node = this->GetObject<Node>();
  Ptr<Ipv4> ipv4 = this->GetObject<Ipv4>();

  NS_ASSERT_MSG(ipv4, "Bolt L4 Protocol supports only IPv4.");

  if (m_node == 0) {
    if ((node != 0) && (ipv4 != 0)) {
      this->SetNode(node);
      Ptr<BoltSocketFactory> boltFactory = CreateObject<BoltSocketFactory>();
      boltFactory->SetBolt(this);
      node->AggregateObject(boltFactory);

      NS_ASSERT(m_mtu);  // m_mtu is set inside SetNode() above.
      NS_ASSERT(m_outboundRtxTimeout != Time(0));
      NS_ASSERT(m_inboundRtxTimeout != Time(0));
    }
  }

  if (ipv4 != 0 && m_downTarget.IsNull()) {
    // BoltL4Protocol instance is one of the upper targets of the IP layer
    ipv4->Insert(this);
    // We set our down target to the IPv4 send function.
    this->SetDownTarget(MakeCallback(&Ipv4::Send, ipv4));
  }
  IpL4Protocol::NotifyNewAggregate();
}

void BoltL4Protocol::DoDispose(void) {
  NS_LOG_FUNCTION_NOARGS();
  for (std::vector<Ptr<BoltSocket> >::iterator i = m_sockets.begin();
       i != m_sockets.end(); i++) {
    *i = 0;
  }
  m_sockets.clear();

  if (m_endPoints != 0) {
    delete m_endPoints;
    m_endPoints = 0;
  }

  m_node = 0;
  m_downTarget.Nullify();
  IpL4Protocol::DoDispose();
}

/*
 * This method is called by BoltSocketFactory associated with m_node which
 * returns a socket that is tied to this BoltL4Protocol instance.
 */
Ptr<Socket> BoltL4Protocol::CreateSocket(void) {
  NS_LOG_FUNCTION_NOARGS();
  Ptr<BoltSocket> socket = CreateObject<BoltSocket>();
  socket->SetNode(m_node);
  socket->SetBolt(this);
  m_sockets.push_back(socket);
  return socket;
}

Ipv4EndPoint *BoltL4Protocol::Allocate(void) {
  NS_LOG_FUNCTION(this);
  return m_endPoints->Allocate();
}

Ipv4EndPoint *BoltL4Protocol::Allocate(Ipv4Address address) {
  NS_LOG_FUNCTION(this << address);
  return m_endPoints->Allocate(address);
}

Ipv4EndPoint *BoltL4Protocol::Allocate(Ptr<NetDevice> boundNetDevice,
                                       uint16_t port) {
  NS_LOG_FUNCTION(this << boundNetDevice << port);
  return m_endPoints->Allocate(boundNetDevice, port);
}

Ipv4EndPoint *BoltL4Protocol::Allocate(Ptr<NetDevice> boundNetDevice,
                                       Ipv4Address address, uint16_t port) {
  NS_LOG_FUNCTION(this << boundNetDevice << address << port);
  return m_endPoints->Allocate(boundNetDevice, address, port);
}
Ipv4EndPoint *BoltL4Protocol::Allocate(Ptr<NetDevice> boundNetDevice,
                                       Ipv4Address localAddress,
                                       uint16_t localPort,
                                       Ipv4Address peerAddress,
                                       uint16_t peerPort) {
  NS_LOG_FUNCTION(this << boundNetDevice << localAddress << localPort
                       << peerAddress << peerPort);
  return m_endPoints->Allocate(boundNetDevice, localAddress, localPort,
                               peerAddress, peerPort);
}

void BoltL4Protocol::DeAllocate(Ipv4EndPoint *endPoint) {
  NS_LOG_FUNCTION(this << endPoint);
  m_endPoints->DeAllocate(endPoint);
}

void BoltL4Protocol::Send(Ptr<Packet> message, Ipv4Address saddr,
                          Ipv4Address daddr, uint16_t sport, uint16_t dport) {
  NS_LOG_FUNCTION(this << message << saddr << daddr << sport << dport);

  Send(message, saddr, daddr, sport, dport, 0);
}

void BoltL4Protocol::Send(Ptr<Packet> message, Ipv4Address saddr,
                          Ipv4Address daddr, uint16_t sport, uint16_t dport,
                          Ptr<Ipv4Route> route) {
  NS_LOG_FUNCTION(this << message << saddr << daddr << sport << dport << route);

  uint16_t txMsgId = m_sendScheduler->ScheduleNewMsg(message, saddr, daddr,
                                                     sport, dport, route);
  m_msgBeginTrace(message, saddr, daddr, sport, dport, txMsgId);
}

/*
 * This method is called either by the associated BoltSendScheduler after
 * the next data packet to transmit is selected or by the associated
 * BoltRecvScheduler once a control packet is generated. The selected
 * packet is then pushed down to the lower IP layer.
 */
void BoltL4Protocol::SendDown(Ptr<Packet> packet, Ipv4Address saddr,
                              Ipv4Address daddr, Ptr<Ipv4Route> route) {
  NS_LOG_FUNCTION(this << packet << saddr << daddr << route);

  BoltHeader bolth;
  packet->PeekHeader(bolth);
  uint16_t txFlag = bolth.GetFlags();
  if (txFlag & BoltHeader::Flags_t::DATA)
    m_dataSendTrace(packet->Copy(), saddr, daddr, bolth.GetSrcPort(),
                    bolth.GetDstPort(), bolth.GetTxMsgId(), bolth.GetSeqAckNo(),
                    txFlag);
  else
    m_ctrlSendTrace(packet->Copy(), saddr, daddr, bolth.GetSrcPort(),
                    bolth.GetDstPort(), bolth.GetTxMsgId(), bolth.GetSeqAckNo(),
                    txFlag);

  m_downTarget(packet, saddr, daddr, PROT_NUMBER, route);
}

/*
 * This method is called by the lower IP layer to notify arrival of a
 * new packet from the network. The method then classifies the packet
 * and forward it to the appropriate scheduler (send or receive) to have
 * Bolt Transport logic applied on it.
 */
enum IpL4Protocol::RxStatus BoltL4Protocol::Receive(
    Ptr<Packet> packet, Ipv4Header const &header,
    Ptr<Ipv4Interface> interface) {
  NS_LOG_FUNCTION(this << packet << header << interface);

  NS_LOG_DEBUG("BoltL4Protocol (" << this
                                  << ") received: " << packet->ToString());

  NS_ASSERT(header.GetProtocol() == PROT_NUMBER);

  Ptr<Packet> cp = packet->Copy();

  BoltHeader boltHeader;
  cp->RemoveHeader(boltHeader);

  NS_LOG_LOGIC("Looking up dst " << header.GetDestination() << " port "
                                 << boltHeader.GetDstPort());
  Ipv4EndPointDemux::EndPoints endPoints = m_endPoints->Lookup(
      header.GetDestination(), boltHeader.GetDstPort(), header.GetSource(),
      boltHeader.GetSrcPort(), interface);
  if (endPoints.empty()) {
    NS_LOG_LOGIC("RX_ENDPOINT_UNREACH");
    return IpL4Protocol::RX_ENDPOINT_UNREACH;
  }

  //  The Bolt protocol logic starts here!
  uint16_t rxFlag = boltHeader.GetFlags();
  if (rxFlag & (BoltHeader::Flags_t::DATA | BoltHeader::Flags_t::CHOP)) {
    m_recvScheduler->ReceivePacket(cp, header, boltHeader, interface);
  } else if (rxFlag & (BoltHeader::Flags_t::ACK | BoltHeader::Flags_t::NACK |
                       BoltHeader::Flags_t::BTS)) {
    m_sendScheduler->CtrlPktRecvdForOutboundMsg(header, boltHeader);
  } else {
    NS_LOG_ERROR("ERROR: BoltL4Protocol received an unknown type of a packet: "
                 << boltHeader.FlagsToString(rxFlag));
    return IpL4Protocol::RX_ENDPOINT_UNREACH;
  }

  if (rxFlag & BoltHeader::Flags_t::DATA)
    m_dataRecvTrace(cp, header.GetSource(), header.GetDestination(),
                    boltHeader.GetSrcPort(), boltHeader.GetDstPort(),
                    boltHeader.GetTxMsgId(), boltHeader.GetSeqAckNo(), rxFlag);
  else
    m_ctrlRecvTrace(cp, header.GetSource(), header.GetDestination(),
                    boltHeader.GetSrcPort(), boltHeader.GetDstPort(),
                    boltHeader.GetTxMsgId(), boltHeader.GetSeqAckNo(), rxFlag);

  return IpL4Protocol::RX_OK;
}

enum IpL4Protocol::RxStatus BoltL4Protocol::Receive(
    Ptr<Packet> packet, Ipv6Header const &header,
    Ptr<Ipv6Interface> interface) {
  NS_FATAL_ERROR_CONT(
      "BoltL4Protocol currently doesn't support IPv6. "
      "Use IPv4 instead.");
  return IpL4Protocol::RX_ENDPOINT_UNREACH;
}

/*
 * This method is called by the BoltRecvScheduler everytime a message is ready
 * to be forwarded up to the applications.
 */
void BoltL4Protocol::ForwardUp(Ptr<Packet> completeMsg,
                               const Ipv4Header &header, uint16_t sport,
                               uint16_t dport, uint16_t txMsgId,
                               Ptr<Ipv4Interface> incomingInterface) {
  NS_LOG_FUNCTION(this << completeMsg << header << sport << incomingInterface);

  NS_LOG_LOGIC("Looking up dst " << header.GetDestination() << " port "
                                 << dport);
  Ipv4EndPointDemux::EndPoints endPoints =
      m_endPoints->Lookup(header.GetDestination(), dport, header.GetSource(),
                          sport, incomingInterface);

  NS_ASSERT_MSG(!endPoints.empty(),
                "BoltL4Protocol was able to find an endpoint when msg was "
                "received, but now it couldn't");

  for (Ipv4EndPointDemux::EndPointsI endPoint = endPoints.begin();
       endPoint != endPoints.end(); endPoint++)
    (*endPoint)->ForwardUp(completeMsg, header, sport, incomingInterface);

  m_msgFinishTrace(completeMsg, header.GetSource(), header.GetDestination(),
                   sport, dport, (int)txMsgId);
}

// inherited from Ipv4L4Protocol (Not used for Bolt Transport Purposes)
void BoltL4Protocol::ReceiveIcmp(Ipv4Address icmpSource, uint8_t icmpTtl,
                                 uint8_t icmpType, uint8_t icmpCode,
                                 uint32_t icmpInfo, Ipv4Address payloadSource,
                                 Ipv4Address payloadDestination,
                                 const uint8_t payload[8]) {
  NS_LOG_FUNCTION(this << icmpSource << icmpTtl << icmpType << icmpCode
                       << icmpInfo << payloadSource << payloadDestination);
  uint16_t src, dst;
  src = payload[0] << 8;
  src |= payload[1];
  dst = payload[2] << 8;
  dst |= payload[3];

  Ipv4EndPoint *endPoint =
      m_endPoints->SimpleLookup(payloadSource, src, payloadDestination, dst);
  if (endPoint != 0) {
    endPoint->ForwardIcmp(icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
  } else {
    NS_LOG_DEBUG("no endpoint found source="
                 << payloadSource << ", destination=" << payloadDestination
                 << ", src=" << src << ", dst=" << dst);
  }
}

void BoltL4Protocol::SetDownTarget(IpL4Protocol::DownTargetCallback callback) {
  NS_LOG_FUNCTION(this);
  m_downTarget = callback;
}

void BoltL4Protocol::SetDownTarget6(
    IpL4Protocol::DownTargetCallback6 callback) {
  NS_LOG_FUNCTION(this);
  NS_FATAL_ERROR(
      "BoltL4Protocol currently doesn't support IPv6. "
      "Use IPv4 instead.");
  m_downTarget6 = callback;
}

IpL4Protocol::DownTargetCallback BoltL4Protocol::GetDownTarget(void) const {
  return m_downTarget;
}

IpL4Protocol::DownTargetCallback6 BoltL4Protocol::GetDownTarget6(void) const {
  NS_FATAL_ERROR(
      "BoltL4Protocol currently doesn't support IPv6. Use IPv4 instead.");
  return m_downTarget6;
}

void BoltL4Protocol::MsgAckedTrace(uint32_t msgSize, Ipv4Address srcIp,
                                   Ipv4Address dstIp, uint16_t srcPort,
                                   uint16_t dstPort, int txMsgId) {
  m_msgAckedTrace(msgSize, srcIp, dstIp, srcPort, dstPort, txMsgId);
}

void BoltL4Protocol::FlowStatsTrace(Ipv4Address srcIp, Ipv4Address dstIp,
                                    uint16_t srcPort, uint16_t dstPort,
                                    int txMsgId, uint32_t cwnd, uint64_t rtt) {
  m_flowStatsTrace(srcIp, dstIp, srcPort, dstPort, txMsgId, cwnd, rtt);
}

/******************************************************************************/

TypeId BoltOutboundMsg::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::BoltOutboundMsg")
                          .SetParent<Object>()
                          .SetGroupName("Internet");
  return tid;
}

BoltOutboundMsg::BoltOutboundMsg(Ptr<Packet> message, uint16_t txMsgId,
                                 Ipv4Address saddr, Ipv4Address daddr,
                                 uint16_t sport, uint16_t dport,
                                 Ptr<BoltL4Protocol> bolt)
    : m_route(0),
      m_btsEnabled(false),
      m_curMsgStartSeq(0),
      m_ackNo(0),
      m_sndNext(0),
      m_sndNextBeforeRtx(0),
      m_rtt(0),
      m_setAiNext(true),
      m_rtxCnt(0),
      m_highestTransmittedSeqNo(0) {
  NS_LOG_FUNCTION(this);

  m_txMsgId = txMsgId;
  m_saddr = saddr;
  m_daddr = daddr;
  m_sport = sport;
  m_dport = dport;
  m_bolt = bolt;

  if (!m_bolt->MemIsOptimized()) m_message = message;

  m_flowSizeBytes = message->GetSize();
  m_msgFinSeqList.push_back(m_flowSizeBytes);

  m_cwnd = m_bolt->GetBdp();  // Initial Cwnd
  m_nextWndAiSeq = m_cwnd;

  m_resumeTime = Simulator::Now();
  m_pauseTime = m_resumeTime;

  m_wndTimeMarker = m_resumeTime.GetNanoSeconds();

  m_keepWhenIdle = m_bolt->AggregateMsgsIfPossible();

  SendDown();  // Send all the packets of the first window

  m_rtxEvent = Simulator::Schedule(m_bolt->GetOutboundRtxTimeout(),
                                   &BoltOutboundMsg::HandleRtx, this, m_cwnd);
}

BoltOutboundMsg::~BoltOutboundMsg() { NS_LOG_FUNCTION_NOARGS(); }

void BoltOutboundMsg::AppendNewMsg(Ptr<Packet> message, Ptr<Ipv4Route> route) {
  NS_LOG_FUNCTION(this << message << route);

  if (!m_bolt->MemIsOptimized()) m_message->AddAtEnd(message);
  m_flowSizeBytes += message->GetSize();
  m_msgFinSeqList.push_back(m_flowSizeBytes);

  SendDown();  // Send down any packet that current cwnd allows
  if (m_rtxEvent.IsExpired())
    m_rtxEvent = Simulator::Schedule(m_bolt->GetOutboundRtxTimeout(),
                                     &BoltOutboundMsg::HandleRtx, this, m_cwnd);
}

void BoltOutboundMsg::SetRoute(Ptr<Ipv4Route> route) { m_route = route; }

Ptr<Ipv4Route> BoltOutboundMsg::GetRoute() { return m_route; }

uint32_t BoltOutboundMsg::GetFlowSizeBytes() { return m_flowSizeBytes; }

Ipv4Address BoltOutboundMsg::GetSrcAddress() { return m_saddr; }

Ipv4Address BoltOutboundMsg::GetDstAddress() { return m_daddr; }

uint16_t BoltOutboundMsg::GetSrcPort() { return m_sport; }

uint16_t BoltOutboundMsg::GetDstPort() { return m_dport; }

uint32_t BoltOutboundMsg::GetCwnd() { return m_cwnd; }
void BoltOutboundMsg::SetCwnd(uint32_t cwnd) { m_cwnd = cwnd; }

uint32_t BoltOutboundMsg::GetSndNext() { return m_sndNext; }

uint32_t BoltOutboundMsg::GetAckNo() { return m_ackNo; }

bool BoltOutboundMsg::IsIdle() { return m_ackNo >= m_flowSizeBytes; }

bool BoltOutboundMsg::FinPktIsInFlight() {
  return m_highestTransmittedSeqNo >= m_flowSizeBytes;
}

uint64_t BoltOutboundMsg::GetRtt() { return m_rtt; }

EventId BoltOutboundMsg::GetRtxEvent() { return m_rtxEvent; }

EventId BoltOutboundMsg::GetSndEvent() { return m_sndEvent; }

void BoltOutboundMsg::HandleAck(BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this << boltHeader);

  NS_ASSERT(boltHeader.GetFlags() & BoltHeader::Flags_t::ACK);

  uint32_t oldAckNo = m_ackNo;
  m_ackNo = std::max(m_ackNo, boltHeader.GetSeqAckNo());
  uint32_t numBytesAcked = m_ackNo - oldAckNo;

  uint64_t now = Simulator::Now().GetNanoSeconds();
  m_rtt = now - boltHeader.GetReflectedDelay();
  NS_ASSERT(m_rtt > 0);

  // If BTS is not enabled, DECWIN on the ACK packet behaves as BTS notification
  if (!m_btsEnabled && (boltHeader.GetFlags() & BoltHeader::Flags_t::DECWIN))
    HandleBts(boltHeader);

  uint32_t curMsgFinSeq = m_msgFinSeqList.front();
  while (m_msgFinSeqList.size() > 0 && m_ackNo >= curMsgFinSeq) {
    m_bolt->MsgAckedTrace(curMsgFinSeq - m_curMsgStartSeq, m_saddr, m_daddr,
                          m_sport, m_dport, static_cast<int>(m_txMsgId));
    m_curMsgStartSeq = curMsgFinSeq;
    m_msgFinSeqList.pop_front();
    curMsgFinSeq = m_msgFinSeqList.front();
  }

  if (IsIdle()) {        // Available data fully delivered
    if (m_keepWhenIdle)  // Keep the flow for future msgs
      KeepFlowForFutureMsg();
    else
      m_bolt->GetSendScheduler()->ClearStateForMsg(m_txMsgId);
  } else {
    UpdateCwnd(boltHeader, numBytesAcked);
    SendDown();  // Transmit packets if allowed
  }

  if (m_sndNext >= m_sndNextBeforeRtx)
    m_sndNextBeforeRtx = 0;  // Not in fast recovery state (anymore)
}

void BoltOutboundMsg::HandleNack(BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this << boltHeader);

  NS_ASSERT(boltHeader.GetFlags() & BoltHeader::Flags_t::NACK);

  uint32_t nackNo = boltHeader.GetSeqAckNo();
  uint32_t oldAckNo = m_ackNo;
  m_ackNo = nackNo;

  uint64_t now = Simulator::Now().GetNanoSeconds();
  m_rtt = now - boltHeader.GetReflectedDelay();
  NS_ASSERT(m_rtt > 0);

  if (m_sndNextBeforeRtx == 0 || nackNo < oldAckNo) {
    // Mark the beginning of fast recovery state
    m_sndNextBeforeRtx = std::max(m_sndNextBeforeRtx, m_sndNext);
    m_sndNext = nackNo;

    if (now - m_wndTimeMarker >= m_rtt) {
      m_cwnd = static_cast<uint32_t>(static_cast<double>(m_cwnd) *
                                     (1.0 - m_bolt->GetMaxMd()));
      m_wndTimeMarker = now;
    }
  } else {
    UpdateCwnd(boltHeader, 0);
  }

  SendDown();  // Rtx based on the current m_sndNext
}

void BoltOutboundMsg::UpdateCwnd(BoltHeader const &boltHeader,
                                 uint32_t numBytesAcked) {
  NS_LOG_FUNCTION(this << boltHeader << numBytesAcked);

  switch (m_bolt->GetCcMode()) {
    case BoltL4Protocol::CC_DEFAULT: {
      UpdateCwndAsBolt(boltHeader, numBytesAcked);
      break;
    }
    case BoltL4Protocol::CC_SWIFT: {
      UpdateCwndAsSwift(boltHeader, numBytesAcked);
      break;
    }
  }

  uint16_t flag = boltHeader.GetFlags();
  if (flag & BoltHeader::Flags_t::DECWIN)
    NS_LOG_LOGIC("DECWIN flag is set for a " << boltHeader.FlagsToString(flag)
                                             << " packet");
  else if (flag & BoltHeader::Flags_t::INCWIN)
    m_cwnd += m_bolt->GetMaxPayloadSize();

  m_cwnd =
      std::max(m_bolt->GetMinCwnd(), std::min(m_cwnd, m_bolt->GetMaxCwnd()));
}

void BoltOutboundMsg::UpdateCwndAsBolt(BoltHeader const &boltHeader,
                                       uint32_t numBytesAcked) {
  NS_LOG_FUNCTION(this << boltHeader << numBytesAcked);

  uint32_t maxPayloadSize = m_bolt->GetMaxPayloadSize();

  // double inc = m_bolt->GetAiFactor() * static_cast<double>(numBytesAcked);
  // if (m_cwnd >= maxPayloadSize)
  //   inc /= static_cast<double>(m_cwnd) / static_cast<double>(maxPayloadSize);
  // m_cwnd += static_cast<uint32_t>(inc);

  // if (m_nextWndAiSeq <= m_ackNo) {
  //   m_cwnd += maxPayloadSize;
  //   m_nextWndAiSeq += m_cwnd;
  // }

  if (boltHeader.GetFlags() & BoltHeader::Flags_t::AI) {
    NS_ASSERT(!m_setAiNext);
    m_setAiNext = true;
    m_cwnd += m_bolt->GetAiFactor() * static_cast<double>(maxPayloadSize);
  }

  NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
               << " BoltOutboundMsg::UpdateCwndAsBolt rtt: " << m_rtt
               << " cwnd: " << m_cwnd);
}

uint64_t BoltOutboundMsg::GetTargetDelay(double cwndPkts, uint16_t nHops,
                                         bool shouldUsePerHopDelay) {
  double flowScaling = m_bolt->GetFlowScalingAlpha() / std::sqrt(cwndPkts) -
                       m_bolt->GetFlowScalingBeta();
  flowScaling =
      std::max(0.0, std::min(flowScaling, m_bolt->GetMaxFlowScaling()));

  uint64_t targetDelay = m_bolt->GetBaseDelay() + flowScaling;

  if (shouldUsePerHopDelay) return targetDelay;

  targetDelay += m_bolt->GetTopoScalingPerHop() * nHops;
  return targetDelay;
}

void BoltOutboundMsg::UpdateCwndAsSwift(BoltHeader const &boltHeader,
                                        uint32_t numBytesAcked) {
  NS_LOG_FUNCTION(this << boltHeader << numBytesAcked);

  uint64_t now = Simulator::Now().GetNanoSeconds();

  double maxPayloadSize = static_cast<double>(m_bolt->GetMaxPayloadSize());

  double cwndPkts = static_cast<double>(m_cwnd) / maxPayloadSize;
  uint16_t nHops = boltHeader.GetReflectedHopCnt();

  bool shouldUsePerHopDelayForCc = m_bolt->ShouldUsePerHopDelayForCc();
  bool shouldAdditivielyIncrease;

  uint64_t targetDelay =
      GetTargetDelay(cwndPkts, nHops, shouldUsePerHopDelayForCc);

  uint32_t drainTime;
  if (shouldUsePerHopDelayForCc) {
    drainTime = boltHeader.GetDrainTime();
    shouldAdditivielyIncrease = drainTime < targetDelay;
  } else {
    shouldAdditivielyIncrease = m_rtt < targetDelay;
  }

  // Implement the AIMD mechanism
  double inc;
  double decScale;
  double dec;
  if (shouldAdditivielyIncrease) {
    inc = m_bolt->GetAiFactor() * static_cast<double>(numBytesAcked);
    if (cwndPkts >= 1.0)
      inc /= static_cast<double>(m_cwnd) / static_cast<double>(maxPayloadSize);
    m_cwnd += inc;

  } else if (now - m_wndTimeMarker >= m_rtt) {
    if (shouldUsePerHopDelayForCc)
      decScale = static_cast<double>(drainTime - targetDelay) /
                 static_cast<double>(drainTime);
    else
      decScale =
          static_cast<double>(m_rtt - targetDelay) / static_cast<double>(m_rtt);
    dec = std::min(decScale * m_bolt->GetMdFactor(), m_bolt->GetMaxMd());
    m_cwnd = static_cast<uint32_t>(static_cast<double>(m_cwnd) * (1.0 - dec));

    m_wndTimeMarker = now;
  }

  NS_LOG_DEBUG(now << " BoltOutboundMsg::UpdateCwndAsSwift rtt: " << m_rtt
                   << " targetRtt: " << targetDelay << " inc: " << inc
                   << " decScale: " << decScale << " dec: " << dec
                   << " cwnd: " << m_cwnd);
}

void BoltOutboundMsg::HandleBts(BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this << boltHeader);

  uint16_t flags = boltHeader.GetFlags();
  if (flags & BoltHeader::Flags_t::BTS) {
    m_btsEnabled = true;
  } else if (m_btsEnabled) {
    NS_ASSERT_MSG(false,
                  "HandleBts function called without a BTS packet although BTS "
                  "is enabled!");
  }

  Time now = Simulator::Now();

  switch (m_bolt->GetCcMode()) {
    case BoltL4Protocol::CC_DEFAULT: {
      uint32_t maxPayloadSize = m_bolt->GetMaxPayloadSize();
      uint64_t nowNs = now.GetNanoSeconds();  // now in nanoseconds
      double rttBts =
          static_cast<double>(nowNs - boltHeader.GetReflectedDelay());

      if (m_cwnd <= maxPayloadSize) {
        m_cwnd = static_cast<uint32_t>(static_cast<double>(m_cwnd) *
                                       (1.0 - m_bolt->GetMaxMd()));
      } else {
        uint64_t linkBpns = m_bolt->GetBitPerNsFromFlag(flags);
        double curRate;  // in bit per ns
        if (m_rtt) {
          curRate =
              static_cast<double>(m_cwnd) * 8.0 / (static_cast<double>(m_rtt));
        } else {  // We have not received RTT measurements yet
          curRate =
              static_cast<double>(m_bolt->GetLinkRate().GetBitRate()) * 1e-9;
        }
        double congestionShare = curRate / static_cast<double>(linkBpns);
        double qSizePkt =
            (static_cast<double>(boltHeader.GetDrainTime() * linkBpns) -
             (m_bolt->GetTargetQ() * 8.0)) /
            (static_cast<double>(m_bolt->GetMtu()) * 8.0);
        uint64_t cwndDecInterval =
            static_cast<uint64_t>(rttBts / (qSizePkt * congestionShare));

        if (flags & BoltHeader::Flags_t::DECWIN &&
            (nowNs - m_wndTimeMarker >= cwndDecInterval)) {
          // Should decrement cwnd to drain the bottleneck queue
          m_cwnd -= maxPayloadSize;
          m_wndTimeMarker = nowNs;
        }
      }

      m_cwnd = std::max(m_bolt->GetMinCwnd(), m_cwnd);

      NS_LOG_DEBUG(nowNs << " wndTimeMarker: " << m_wndTimeMarker
                         << " rttBts: " << rttBts << " cwnd: " << m_cwnd
                         << " drainTime: " << boltHeader.GetDrainTime());
      break;
    }
    case BoltL4Protocol::CC_SWIFT: {
      // uint64_t rttBts = std::max(
      //     m_rtt, now.GetNanoSeconds() - boltHeader.GetReflectedDelay());
      // NS_ASSERT(rttBts > 0);
      // if (now.GetNanoSeconds() - m_wndTimeMarker >= rttBts) {
      // m_cwnd = static_cast<uint32_t>(static_cast<double>(m_cwnd) *
      //                                (1.0 - m_bolt->GetMaxMd()));
      //   m_wndTimeMarker = now.GetNanoSeconds();
      // }

      UpdateCwndAsSwift(boltHeader, m_bolt->GetMtu());

      // if (m_resumeTime <= now) m_pauseTime = now;  // Not on pause, pause now
      // Time resumeTime = m_pauseTime + NanoSeconds(boltHeader.GetDrainTime());
      // if (m_resumeTime > resumeTime) return;
      // m_resumeTime = resumeTime;

      // Simulator::Cancel(m_sndEvent);
      // m_sndEvent = Simulator::Schedule(m_resumeTime - now,
      //                                  &BoltOutboundMsg::SendDown, this);
      break;
    }
  }

  m_bolt->FlowStatsTrace(m_saddr, m_daddr, m_sport, m_dport, m_txMsgId, m_cwnd,
                         m_rtt);
}

void BoltOutboundMsg::HandleRtx(uint32_t rtxOffset) {
  NS_LOG_FUNCTION(this << rtxOffset);

  NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
               << " Timer for BoltOutboundMsg (" << this
               << ") expired. (rtxOffset: " << rtxOffset
               << " ackNo: " << m_ackNo << ")");

  if (m_ackNo > rtxOffset)
    m_rtxCnt = 0;
  else  // All the packets below rtxOffset are not delivered
  {
    m_sndNext = m_ackNo;
    m_rtxCnt++;

    uint64_t now = Simulator::Now().GetNanoSeconds();
    if (now - m_wndTimeMarker >= m_rtt) {
      m_cwnd = static_cast<uint32_t>(static_cast<double>(m_cwnd) *
                                     (1.0 - m_bolt->GetMaxMd()));
      m_wndTimeMarker = now;
    }
    SendDown();  // Retransmit all the bytes after m_ackNo (Go-Back-N)
  }

  if (m_rtxCnt < m_bolt->GetMaxNumRtxPerMsg()) {
    m_rtxEvent = Simulator::Schedule(m_bolt->GetOutboundRtxTimeout(),
                                     &BoltOutboundMsg::HandleRtx, this,
                                     m_highestTransmittedSeqNo);
  } else {
    NS_LOG_WARN(Simulator::Now().GetNanoSeconds()
                << " BoltOutboundMsg (" << this << ") has timed-out.");
    if (m_keepWhenIdle)  // Keep the flow for future msgs
      KeepFlowForFutureMsg();
    else
      m_bolt->GetSendScheduler()->ClearStateForMsg(m_txMsgId);  // Msg expired
  }
}

void BoltOutboundMsg::SendDown() {
  NS_LOG_FUNCTION(this);

  NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
               << " BoltOutboundMsg::SendDown ackNo: " << m_ackNo
               << " cwnd: " << m_cwnd << " sndNext: " << m_sndNext);

  if (IsIdle()) {        // Available data fully delivered
    if (m_keepWhenIdle)  // Keep the flow for future msgs
      KeepFlowForFutureMsg();
    else
      m_bolt->GetSendScheduler()->ClearStateForMsg(m_txMsgId);
    return;
  }

  m_bolt->FlowStatsTrace(m_saddr, m_daddr, m_sport, m_dport, m_txMsgId, m_cwnd,
                         m_rtt);

  if (!m_sndEvent.IsExpired())
    return;  // sndEvent will call this function soon anyway

  if (Simulator::Now() < m_resumeTime) {  // message is paused at the moment
    m_sndEvent = Simulator::Schedule(m_resumeTime - Simulator::Now(),
                                     &BoltOutboundMsg::SendDown, this);
    return;
  }

  uint32_t maxPayloadSize = m_bolt->GetMaxPayloadSize();
  uint32_t payloadSize = std::min(m_flowSizeBytes - m_sndNext, maxPayloadSize);

  if (payloadSize == 0 || m_ackNo + m_cwnd <= m_sndNext) {
    return;  // There is no packet to send
  } else if (m_cwnd < payloadSize ||
             m_ackNo + m_cwnd - m_sndNext < payloadSize) {
    // Pace the trasnmissions to account for cwnd < 1 pkt

    // Send 1 packet out if the message was paced previously
    if (Simulator::Now() == m_resumeTime) SendDownOnePacket(payloadSize);

    uint64_t pacingDelay = static_cast<uint64_t>(
        static_cast<double>(m_rtt) * static_cast<double>(maxPayloadSize) /
        static_cast<double>(m_cwnd));
    NS_ASSERT_MSG(pacingDelay >= 0,
                  "Negative Pacing Delay: " << pacingDelay << " Rtt: " << m_rtt
                                            << " Cwnd: " << m_cwnd);
    m_pauseTime = Simulator::Now();
    m_resumeTime = m_pauseTime + NanoSeconds(pacingDelay);
    m_sndEvent = Simulator::Schedule(NanoSeconds(pacingDelay),
                                     &BoltOutboundMsg::SendDown, this);

  } else {  // We are good to send a packet
    SendDownOnePacket(payloadSize);

    if (m_ackNo + m_cwnd > m_sndNext && m_sndNext < m_flowSizeBytes) {
      // More packets can be sent
      Time timeToSerialize = m_bolt->GetLinkRate().CalculateBytesTxTime(
          payloadSize + m_bolt->GetMtu() - maxPayloadSize);
      // Time timeToSerialize =
      //     m_bolt->GetLinkRate().CalculateBytesTxTime(payloadSize);
      m_sndEvent = Simulator::Schedule(timeToSerialize,
                                       &BoltOutboundMsg::SendDown, this);
    }
  }
}

void BoltOutboundMsg::SendDownOnePacket(uint32_t payloadSize) {
  NS_LOG_FUNCTION(this << payloadSize);

  Ptr<Packet> p;
  if (m_bolt->MemIsOptimized())
    p = Create<Packet>(payloadSize);
  else
    p = m_message->CreateFragment(m_sndNext, payloadSize);

  uint16_t flag = BoltHeader::Flags_t::DATA;
  if (m_rtt == 0) {
    // No Ack/Nack has been received yet
    flag |= BoltHeader::Flags_t::FIRST;
  } else if (m_sndNext + m_cwnd < m_flowSizeBytes) {
    // This is not the first and last window, so we can probe for bw
    flag |= BoltHeader::Flags_t::INCWIN;
  }

  if (m_sndNext + payloadSize >= m_flowSizeBytes)
    flag |= BoltHeader::Flags_t::FIN | BoltHeader::Flags_t::LAST;
  else if (m_sndNext + m_cwnd >= m_flowSizeBytes)
    flag |= BoltHeader::Flags_t::LAST;

  if (m_setAiNext) {
    flag |= BoltHeader::Flags_t::AI;
    m_setAiNext = false;
  }

  BoltHeader bolth;
  bolth.SetSrcPort(m_sport);
  bolth.SetDstPort(m_dport);
  bolth.SetTxMsgId(m_txMsgId);
  bolth.SetSeqAckNo(m_sndNext);
  bolth.SetFlags(flag);

  bolth.SetReflectedDelay(Simulator::Now().GetNanoSeconds());
  p->AddHeader(bolth);

  m_bolt->SendDown(p, m_saddr, m_daddr, m_route);

  m_sndNext += payloadSize;
  if (m_sndNext > m_highestTransmittedSeqNo)
    m_highestTransmittedSeqNo = m_sndNext;
}

void BoltOutboundMsg::SetToKeepForFutureMsg(bool shouldKeepForFutureMsg) {
  m_keepWhenIdle = shouldKeepForFutureMsg;
}

void BoltOutboundMsg::KeepFlowForFutureMsg() {
  NS_LOG_FUNCTION(this);

  NS_ASSERT_MSG(
      m_msgFinSeqList.size() == 0 || m_rtxCnt >= m_bolt->GetMaxNumRtxPerMsg(),
      "A flow has become idle although there are "
          << m_msgFinSeqList.size()
          << " uncompleted messages within the flow. nextMsgFinSeq: "
          << m_msgFinSeqList.front() << " curMsgStartSeq: " << m_curMsgStartSeq
          << " totalflowSize: " << m_flowSizeBytes << " curAckNo: " << m_ackNo);
  m_flowSizeBytes = 0;
  m_curMsgStartSeq = 0;

  if (!m_bolt->MemIsOptimized()) m_message = Create<Packet>();

  m_ackNo = 0;
  m_sndNext = 0;
  m_sndNextBeforeRtx = 0;
  m_rtt = 0;  // Future msg should measure its own rtt
  m_rtxCnt = 0;
  m_highestTransmittedSeqNo = 0;
  m_nextWndAiSeq = m_cwnd;
  m_setAiNext = true;

  Simulator::Cancel(m_sndEvent);
  Simulator::Cancel(m_rtxEvent);

  // Log this event by denoting zero cwnd and rtt
  m_bolt->FlowStatsTrace(m_saddr, m_daddr, m_sport, m_dport, m_txMsgId, 0, 0);

  // Note that state variables such as m_cwnd are not reset,
  // so that a new message appended onto this flow can use the historical data
  // for congestion control purposes.
}

/******************************************************************************/

TypeId BoltSendScheduler::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::BoltSendScheduler")
                          .SetParent<Object>()
                          .SetGroupName("Internet");
  return tid;
}

BoltSendScheduler::BoltSendScheduler(Ptr<BoltL4Protocol> boltL4Protocol)
    : m_txMsgCounter(0) {
  NS_LOG_FUNCTION(this);

  m_bolt = boltL4Protocol;
}

BoltSendScheduler::~BoltSendScheduler() {
  NS_LOG_FUNCTION_NOARGS();

  int numIncmpltMsg = m_outboundMsgs.size();
  if (numIncmpltMsg > 0) {
    NS_LOG_ERROR("ERROR: BoltSendScheduler ("
                 << this << ") couldn't completely deliver " << numIncmpltMsg
                 << " outbound messages!");
  }
}

/*
 * This method is called upon receiving a new message from the upper layers.
 * It inserts the message into the list of pending outbound messages and updates
 * the scheduler's state accordingly.
 */
uint16_t BoltSendScheduler::ScheduleNewMsg(Ptr<Packet> message,
                                           Ipv4Address saddr, Ipv4Address daddr,
                                           uint16_t sport, uint16_t dport,
                                           Ptr<Ipv4Route> route) {
  NS_LOG_FUNCTION(this << message << saddr << daddr << sport << dport << route);

  uint16_t txMsgId;
  uint16_t newTxMsgId;
  if (m_bolt->AggregateMsgsIfPossible() &&
      FlowExistsForTuple(saddr, daddr, sport, dport, txMsgId)) {
    if (m_outboundMsgs[txMsgId]->FinPktIsInFlight()) {
      // We can not add new data onto this flow because it has already sent the
      // FIN packet, yet it is not idle. Therefore we will create a new flow and
      // allow the existing one to get destructed once it becomes idle (soon).
      m_outboundMsgs[txMsgId]->SetToKeepForFutureMsg(false);

      newTxMsgId = AllocateNewMsg(message, saddr, daddr, sport, dport, route);
      m_outboundMsgs[newTxMsgId]->SetCwnd(m_outboundMsgs[txMsgId]->GetCwnd());

      txMsgId = newTxMsgId;  // Return the correct most recent txMsgId allocated
    } else if (!m_outboundMsgs[txMsgId]->IsIdle() &&
               message->GetSize() <= m_bolt->GetBdp()) {
      // We don't let short messages to stay behind existing messages to
      // minimize Head-Of-Line Blocking. Therefore we create new flows for them.
      newTxMsgId = AllocateNewMsg(message, saddr, daddr, sport, dport, route);
      m_outboundMsgs[newTxMsgId]->SetCwnd(m_outboundMsgs[txMsgId]->GetCwnd());
      m_outboundMsgs[newTxMsgId]->SetToKeepForFutureMsg(false);

      txMsgId = newTxMsgId;  // Return the correct most recent txMsgId allocated
    } else {
      m_outboundMsgs[txMsgId]->AppendNewMsg(message, route);
    }
  } else {
    txMsgId = AllocateNewMsg(message, saddr, daddr, sport, dport, route);
  }
  return txMsgId;
}

uint16_t BoltSendScheduler::AllocateNewMsg(Ptr<Packet> message,
                                           Ipv4Address saddr, Ipv4Address daddr,
                                           uint16_t sport, uint16_t dport,
                                           Ptr<Ipv4Route> route) {
  NS_LOG_FUNCTION(this << message << saddr << daddr << sport << dport << route);

  uint16_t txMsgId = m_txMsgCounter;
  m_txMsgCounter++;
  NS_LOG_LOGIC("BoltSendScheduler allocating txMsgId: " << txMsgId);

  Ptr<BoltOutboundMsg> outMsg = CreateObject<BoltOutboundMsg>(
      message, txMsgId, saddr, daddr, sport, dport, m_bolt);
  outMsg->SetRoute(route);

  m_outboundMsgs[txMsgId] = outMsg;

  return txMsgId;
}

bool BoltSendScheduler::FlowExistsForTuple(Ipv4Address saddr, Ipv4Address daddr,
                                           uint16_t sport, uint16_t dport,
                                           uint16_t &txMsgId) {
  NS_LOG_FUNCTION(this << saddr << daddr << sport << dport);

  for (auto &it : m_outboundMsgs) {
    if (it.second->GetSrcAddress() == saddr &&
        it.second->GetDstAddress() == daddr &&
        it.second->GetSrcPort() == sport && it.second->GetDstPort() == dport) {
      NS_LOG_LOGIC("The BoltOutboundMsg ("
                   << it.second << ") is found among the pending flows.");
      txMsgId = it.first;
      return true;
    }
  }

  NS_LOG_LOGIC("There is no pending flow for the given tuple.");
  return false;
}

/*
 * This method is called when a control packet is received that interests
 * an outbound message.
 */
void BoltSendScheduler::CtrlPktRecvdForOutboundMsg(
    Ipv4Header const &ipv4Header, BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this << ipv4Header << boltHeader);

  uint16_t targetTxMsgId = boltHeader.GetTxMsgId();
  uint16_t ctrlFlag = boltHeader.GetFlags();
  if (m_outboundMsgs.find(targetTxMsgId) == m_outboundMsgs.end()) {
    NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
                 << " BoltSendScheduler (" << this << ") received a "
                 << boltHeader.FlagsToString(ctrlFlag)
                 << " packet for an unknown txMsgId (" << targetTxMsgId
                 << ").");
    return;
  }

  Ptr<BoltOutboundMsg> targetMsg = m_outboundMsgs[targetTxMsgId];
  // Verify that the TxMsgId indeed matches the 4 tuple
  NS_ASSERT((targetMsg->GetSrcAddress() == ipv4Header.GetDestination()) &&
            (targetMsg->GetDstAddress() == ipv4Header.GetSource()) &&
            (targetMsg->GetSrcPort() == boltHeader.GetDstPort()) &&
            (targetMsg->GetDstPort() == boltHeader.GetSrcPort()));

  if (ctrlFlag & BoltHeader::Flags_t::ACK) {
    targetMsg->HandleAck(boltHeader);
  } else if (ctrlFlag & BoltHeader::Flags_t::NACK) {
    targetMsg->HandleNack(boltHeader);
  } else if (ctrlFlag & BoltHeader::Flags_t::BTS) {
    targetMsg->HandleBts(boltHeader);
  } else {
    NS_LOG_ERROR("ERROR: BoltSendScheduler ("
                 << this << ") has received an unexpected control packet ("
                 << boltHeader.FlagsToString(ctrlFlag) << ")");
    return;
  }
}

void BoltSendScheduler::ClearStateForMsg(uint16_t txMsgId) {
  NS_LOG_FUNCTION(this << txMsgId);

  NS_LOG_DEBUG("Clearing state for BoltOutboundMsg ("
               << txMsgId
               << ") from the pending messages list of BoltSendScheduler ("
               << this << ").");

  m_bolt->FlowStatsTrace(m_outboundMsgs[txMsgId]->GetSrcAddress(),
                         m_outboundMsgs[txMsgId]->GetDstAddress(),
                         m_outboundMsgs[txMsgId]->GetSrcPort(),
                         m_outboundMsgs[txMsgId]->GetDstPort(), txMsgId, 0, 0);

  Simulator::Cancel(m_outboundMsgs[txMsgId]->GetRtxEvent());
  Simulator::Cancel(m_outboundMsgs[txMsgId]->GetSndEvent());
  m_outboundMsgs.erase(m_outboundMsgs.find(txMsgId));
}

/******************************************************************************/

TypeId BoltInboundMsg::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::BoltInboundMsg")
                          .SetParent<Object>()
                          .SetGroupName("Internet");
  return tid;
}

BoltInboundMsg::BoltInboundMsg(uint16_t rxMsgId, Ipv4Header const &ipv4Header,
                               BoltHeader const &boltHeader,
                               Ptr<Ipv4Interface> iface,
                               Ptr<BoltL4Protocol> bolt)
    : m_nTimeOutWithoutProgress(0) {
  NS_LOG_FUNCTION(this);

  m_ipv4Header = ipv4Header;
  m_iface = iface;
  m_bolt = bolt;

  m_sport = boltHeader.GetSrcPort();
  m_dport = boltHeader.GetDstPort();
  m_txMsgId = boltHeader.GetTxMsgId();
  m_rxMsgId = rxMsgId;

  if (!m_bolt->MemIsOptimized()) m_message = Create<Packet>();
  m_nextExpected = 0;

  m_timeOutEvent =
      Simulator::Schedule(m_bolt->GetInboundRtxTimeout(),
                          &BoltInboundMsg::HandleTimeout, this, m_nextExpected);
}

BoltInboundMsg::~BoltInboundMsg() { NS_LOG_FUNCTION_NOARGS(); }

Ipv4Address BoltInboundMsg::GetSrcAddress() { return m_ipv4Header.GetSource(); }

Ipv4Address BoltInboundMsg::GetDstAddress() {
  return m_ipv4Header.GetDestination();
}

uint16_t BoltInboundMsg::GetSrcPort() { return m_sport; }

uint16_t BoltInboundMsg::GetDstPort() { return m_dport; }

uint16_t BoltInboundMsg::GetTxMsgId() { return m_txMsgId; }

Ipv4Header BoltInboundMsg::GetIpv4Header() { return m_ipv4Header; }

Ptr<Ipv4Interface> BoltInboundMsg::GetIpv4Interface() { return m_iface; }

EventId BoltInboundMsg::GetTimeoutEvent() { return m_timeOutEvent; }

void BoltInboundMsg::HandleTimeout(uint32_t oldExpectedSegment) {
  NS_LOG_FUNCTION(this << oldExpectedSegment);

  if (oldExpectedSegment < m_nextExpected)
    m_nTimeOutWithoutProgress = 0;
  else
    m_nTimeOutWithoutProgress++;

  if (m_nTimeOutWithoutProgress < m_bolt->GetMaxNumRtxPerMsg()) {
    m_timeOutEvent = Simulator::Schedule(m_bolt->GetInboundRtxTimeout(),
                                         &BoltInboundMsg::HandleTimeout, this,
                                         m_nextExpected);
  } else {
    NS_LOG_WARN(Simulator::Now().GetNanoSeconds()
                << " BoltInboundMsg (" << this << ") has timed-out.");
    m_bolt->GetRecvScheduler()->ClearStateForMsg(m_rxMsgId);  // Msg expired
  }
}

/*
 * Update the state for an inbound message upon receival of a data packet.
 */
void BoltInboundMsg::ReceiveDataPacket(Ptr<Packet> p,
                                       Ipv4Header const &ipv4Header,
                                       BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this << p << ipv4Header << boltHeader);

  uint32_t seqNo = boltHeader.GetSeqAckNo();

  if (seqNo == m_nextExpected) {
    m_nextExpected += p->GetSize();

    SendAck(ipv4Header.GetTtl(), boltHeader);

    if (!m_bolt->MemIsOptimized()) m_message->AddAtEnd(p);

    if (boltHeader.GetFlags() & BoltHeader::Flags_t::FIN)  // Fully received
      ForwardUp(m_nextExpected);
  } else {
    NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
                 << " BoltInboundMsg (" << this
                 << ") received a packet for seqNo " << seqNo
                 << " which is not the next expected segment. "
                 << "(Next Expected: " << m_nextExpected << ")");
    // TODO(serhatarslan): Insert a trace source to monitor spurious arrivals.

    SendNack(ipv4Header.GetTtl(), boltHeader, m_nextExpected);
  }
}

void BoltInboundMsg::SendAck(uint8_t ttl, BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this);

  Ptr<Packet> p = Create<Packet>();

  uint8_t hopCnt = 65 - ttl;
  // TODO(serhatarslan): Lookup the default TTL value instead of using 64

  uint16_t flag = boltHeader.GetFlags() & m_bolt->GetFlagsToReflect();
  flag |= BoltHeader::Flags_t::ACK;

  BoltHeader bolth;
  bolth.SetSrcPort(m_dport);
  bolth.SetDstPort(m_sport);
  bolth.SetTxMsgId(m_txMsgId);
  bolth.SetSeqAckNo(m_nextExpected);
  bolth.SetFlags(flag);
  bolth.SetReflectedHopCnt(hopCnt);
  bolth.SetReflectedDelay(boltHeader.GetReflectedDelay());
  bolth.SetDrainTime(boltHeader.GetDrainTime());
  p->AddHeader(bolth);

  m_bolt->SendDown(p, GetDstAddress(), GetSrcAddress());
}

void BoltInboundMsg::ForwardUp(uint32_t msgSizeBytes) {
  NS_LOG_FUNCTION(this);

  if (m_bolt->MemIsOptimized()) m_message = Create<Packet>(msgSizeBytes);

  m_bolt->ForwardUp(m_message, m_ipv4Header, m_sport, m_dport, m_txMsgId,
                    m_iface);

  m_bolt->GetRecvScheduler()->ClearStateForMsg(m_rxMsgId);  // Msg completed
}

void BoltInboundMsg::HandleChop(uint8_t ttl, BoltHeader const &boltHeader) {
  NS_LOG_FUNCTION(this << ttl << boltHeader);

  NS_ASSERT(boltHeader.GetFlags() & BoltHeader::Flags_t::CHOP);

  SendNack(ttl, boltHeader, boltHeader.GetSeqAckNo());
}

void BoltInboundMsg::SendNack(uint8_t ttl, BoltHeader const &boltHeader,
                              uint32_t nackNo) {
  NS_LOG_FUNCTION(this << ttl << boltHeader << nackNo);

  Ptr<Packet> p = Create<Packet>();

  uint8_t hopCnt = 65 - ttl;
  // TODO(serhatarslan): Lookup the default TTL value instead of using 64

  uint16_t reflectedFlag = boltHeader.GetFlags() & m_bolt->GetFlagsToReflect();
  reflectedFlag |= BoltHeader::Flags_t::NACK;

  BoltHeader bolth;
  bolth.SetSrcPort(m_dport);
  bolth.SetDstPort(m_sport);
  bolth.SetTxMsgId(m_txMsgId);
  bolth.SetSeqAckNo(nackNo);
  bolth.SetFlags(reflectedFlag);
  bolth.SetReflectedHopCnt(hopCnt);
  bolth.SetReflectedDelay(boltHeader.GetReflectedDelay());
  bolth.SetDrainTime(boltHeader.GetDrainTime());
  p->AddHeader(bolth);

  m_bolt->SendDown(p, GetDstAddress(), GetSrcAddress());
}

/******************************************************************************/

TypeId BoltRecvScheduler::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::BoltRecvScheduler")
                          .SetParent<Object>()
                          .SetGroupName("Internet");
  return tid;
}

BoltRecvScheduler::BoltRecvScheduler(Ptr<BoltL4Protocol> boltL4Protocol)
    : m_rxMsgCounter(0) {
  NS_LOG_FUNCTION(this);

  m_bolt = boltL4Protocol;
}

BoltRecvScheduler::~BoltRecvScheduler() {
  NS_LOG_FUNCTION_NOARGS();

  int numIncmpltMsg = m_inboundMsgs.size();
  if (numIncmpltMsg > 0) {
    NS_LOG_ERROR("ERROR: BoltRecvScheduler ("
                 << this << ") couldn't completely receive " << numIncmpltMsg
                 << " active inbound messages!");
  }
}

void BoltRecvScheduler::ReceivePacket(Ptr<Packet> packet,
                                      Ipv4Header const &ipv4Header,
                                      BoltHeader const &boltHeader,
                                      Ptr<Ipv4Interface> interface) {
  NS_LOG_FUNCTION(this << packet << ipv4Header << boltHeader);

  Ptr<Packet> cp = packet->Copy();
  Ptr<BoltInboundMsg> inboundMsg;

  uint16_t rxMsgId;
  if (this->GetInboundMsg(ipv4Header, boltHeader, rxMsgId)) {
    inboundMsg = m_inboundMsgs[rxMsgId];
  } else {
    rxMsgId = m_rxMsgCounter;
    m_rxMsgCounter++;
    inboundMsg = CreateObject<BoltInboundMsg>(rxMsgId, ipv4Header, boltHeader,
                                              interface, m_bolt);
    m_inboundMsgs[rxMsgId] = inboundMsg;
  }

  uint16_t rxFlag = boltHeader.GetFlags();
  if (rxFlag & BoltHeader::Flags_t::DATA)
    inboundMsg->ReceiveDataPacket(cp, ipv4Header, boltHeader);
  else if (rxFlag & BoltHeader::Flags_t::CHOP)
    inboundMsg->HandleChop(ipv4Header.GetTtl(), boltHeader);
}

bool BoltRecvScheduler::GetInboundMsg(Ipv4Header const &ipv4Header,
                                      BoltHeader const &boltHeader,
                                      uint16_t &rxMsgId) {
  NS_LOG_FUNCTION(this << ipv4Header << boltHeader);

  for (auto &it : m_inboundMsgs) {
    if (it.second->GetSrcAddress() == ipv4Header.GetSource() &&
        it.second->GetDstAddress() == ipv4Header.GetDestination() &&
        it.second->GetSrcPort() == boltHeader.GetSrcPort() &&
        it.second->GetDstPort() == boltHeader.GetDstPort() &&
        it.second->GetTxMsgId() == boltHeader.GetTxMsgId()) {
      NS_LOG_LOGIC("The BoltInboundMsg ("
                   << it.second << ") is found among the pending messages.");
      rxMsgId = it.first;
      return true;
    }
  }

  NS_LOG_LOGIC("Incoming packet doesn't belong to a pending inbound message.");
  return false;
}

void BoltRecvScheduler::ClearStateForMsg(uint16_t rxMsgId) {
  NS_LOG_FUNCTION(this << rxMsgId);

  Simulator::Cancel(m_inboundMsgs[rxMsgId]->GetTimeoutEvent());

  NS_LOG_DEBUG("Clearing state for BoltInboundMsg ("
               << rxMsgId
               << ") from the pending messages list of BoltRecvScheduler ("
               << this << ").");

  m_inboundMsgs.erase(m_inboundMsgs.find(rxMsgId));
}

}  // namespace ns3
