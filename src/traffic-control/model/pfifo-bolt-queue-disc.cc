/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#include "pfifo-bolt-queue-disc.h"

#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/network-module.h"
#include "ns3/object-factory.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue.h"
#include "ns3/socket.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("PfifoBoltQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(PfifoBoltQueueDisc);

TypeId PfifoBoltQueueDisc::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::PfifoBoltQueueDisc")
          .SetParent<QueueDisc>()
          .SetGroupName("TrafficControl")
          .AddConstructor<PfifoBoltQueueDisc>()
          .AddAttribute(
              "MaxSize",
              "The maximum number of packets accepted by this queue disc.",
              QueueSizeValue(QueueSize("1000p")),
              MakeQueueSizeAccessor(&QueueDisc::SetMaxSize,
                                    &QueueDisc::GetMaxSize),
              MakeQueueSizeChecker())
          .AddAttribute(
              "CcThreshold", "Queue occupancy threshold for CC reaction.",
              QueueSizeValue(QueueSize("62KB")),
              MakeQueueSizeAccessor(&PfifoBoltQueueDisc::m_ccThreshold),
              MakeQueueSizeChecker())
          .AddAttribute("EnableAbs",
                        "Enable Available Bandwidth Signaling feature.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&PfifoBoltQueueDisc::m_absEnabled),
                        MakeBooleanChecker())
          .AddAttribute(
              "MaxInstAvailLoad",
              "Maximum amount of bytes allowed to be accumulated to measure "
              "available bandwidth.",
              IntegerValue(30000),
              MakeIntegerAccessor(&PfifoBoltQueueDisc::m_maxInstAvailLoad),
              MakeIntegerChecker<int>())
          .AddAttribute(
              "EnableBts",
              "Enable Back To Sender feature for congestion signalling.",
              BooleanValue(false),
              MakeBooleanAccessor(&PfifoBoltQueueDisc::m_btsEnabled),
              MakeBooleanChecker())
          .AddAttribute("EnableTrimming",
                        "Enable payload trimming instead of dropTail policy.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&PfifoBoltQueueDisc::m_trimEnabled),
                        MakeBooleanChecker())
          .AddAttribute(
              "EnablePru",
              "Enable Proactive Ramp-Up feature for higher link utilization.",
              BooleanValue(false),
              MakeBooleanAccessor(&PfifoBoltQueueDisc::m_pruEnabled),
              MakeBooleanChecker())
          .AddAttribute(
              "MaxNumPruTokens",
              "Maximum number of packet slots for the future",
              UintegerValue(256),
              MakeUintegerAccessor(&PfifoBoltQueueDisc::m_maxPruToken),
              MakeUintegerChecker<uint16_t>())
          .AddAttribute(
              "ReducedBtsFactor",
              "Period of BTS transmission during congestion", UintegerValue(1),
              MakeUintegerAccessor(&PfifoBoltQueueDisc::m_reducedBtsFactor),
              MakeUintegerChecker<uint32_t>())
          .AddTraceSource(
              "AbsTokensInQueue",
              "Amount of ABS tokens currently stored in the queue disc",
              MakeTraceSourceAccessor(&PfifoBoltQueueDisc::m_availLoad),
              "ns3::TracedValueCallback::Int")
          .AddTraceSource(
              "PruTokensInQueue",
              "Number of PRU tokens currently stored in the queue disc",
              MakeTraceSourceAccessor(&PfifoBoltQueueDisc::m_nPruToken),
              "ns3::TracedValueCallback::Uint16")
          .AddTraceSource(
              "BtsDeparture",
              "Trace source tracking the internal switch state every time it "
              "generates a new BTS packet.",
              MakeTraceSourceAccessor(&PfifoBoltQueueDisc::m_btsSendTrace),
              "ns3::PfifoBoltQueueDisc::BtsDepartureTracedCallback");
  return tid;
}

PfifoBoltQueueDisc::PfifoBoltQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS) {
  NS_LOG_FUNCTION(this);
}

PfifoBoltQueueDisc::~PfifoBoltQueueDisc() { NS_LOG_FUNCTION(this); }

const uint32_t PfifoBoltQueueDisc::prio2band[16] = {1, 2, 2, 2, 1, 2, 0, 0,
                                                    1, 1, 1, 1, 1, 1, 1, 1};

void PfifoBoltQueueDisc::CalculateCurrentlyAvailableBw(uint32_t pktSize) {
  NS_LOG_FUNCTION(this << pktSize);

  Ipv4Header ipv4h;  // Account for the IP header of the arriving packet as well
  // Arriving packet reduces the available bandwidth
  int newAvailLoad =
      m_availLoad - static_cast<int>(pktSize + ipv4h.GetSerializedSize() - 1);
  // TODO(serhatarslan): The -1 above is required for m_availLoad to closely
  //                     follow the current queue occupancy. Investigate why.

  // availLoad should be periodically increasing to account for draining queue
  uint64_t now = Simulator::Now().GetNanoSeconds();
  double secondsSinceLastUpdate =
      static_cast<double>(now - m_availLoadUpdateTime) / 1e9;
  newAvailLoad += static_cast<int>(
      static_cast<double>(m_boundNetDevice->GetDataRate().GetBitRate()) *
      secondsSinceLastUpdate / 8.0);
  m_availLoadUpdateTime = now;
  // Cap the available bandwidth measure to prevent large bursts
  if (newAvailLoad > m_maxInstAvailLoad) newAvailLoad = m_maxInstAvailLoad;
  m_availLoad = newAvailLoad;
}

void PfifoBoltQueueDisc::TrimPacketPayload(Ptr<Packet> p,
                                           BoltHeaderPtr *bolth) {
  NS_LOG_FUNCTION(this << p << bolth);

  uint32_t payloadSize = p->GetSize();
  p->RemoveAtEnd(payloadSize);
  IncreaseDroppedBytesBeforeEnqueueStats((uint64_t)payloadSize);

  bolth->SetFlags((bolth->GetFlags() & ~BoltHeader::Flags_t::DATA) |
                  BoltHeader::Flags_t::CHOP);
}

void PfifoBoltQueueDisc::NotifySender(Ipv4Header ipv4h, BoltHeaderPtr *bolth) {
  NS_LOG_FUNCTION(this << ipv4h << bolth);

  Ptr<Packet> p = Create<Packet>();

  BoltHeader newBolth;
  newBolth.SetSrcPort(bolth->GetDstPort());
  newBolth.SetDstPort(bolth->GetSrcPort());
  newBolth.SetSeqAckNo(bolth->GetSeqAckNo());
  uint16_t flag = (bolth->GetFlags() & ~BoltHeader::Flags_t::DATA) |
                  BoltHeader::Flags_t::BTS | BoltHeader::Flags_t::DECWIN;
  newBolth.SetFlags(flag);
  newBolth.SetTxMsgId(bolth->GetTxMsgId());
  newBolth.SetDrainTime(bolth->GetDrainTime());
  newBolth.SetReflectedDelay(bolth->GetReflectedDelay());
  // TODO(serhatarslan): Lookup the default TTL value instead of using 64
  newBolth.SetReflectedHopCnt(64 - ipv4h.GetTtl());
  p->AddHeader(newBolth);

  Ipv4Header newIpv4h;
  newIpv4h.SetSource(ipv4h.GetDestination());
  newIpv4h.SetDestination(ipv4h.GetSource());
  newIpv4h.SetProtocol(BoltL4Protocol::PROT_NUMBER);
  newIpv4h.SetDontFragment();
  newIpv4h.SetPayloadSize(p->GetSize());
  p->AddHeader(newIpv4h);

  Ptr<Ipv4L3Protocol> ipv4L3Protocol =
      m_boundNetDevice->GetNode()->GetObject<Ipv4L3Protocol>();

  Address dummy;
  ipv4L3Protocol->Receive(m_boundNetDevice, p, Ipv4L3Protocol::PROT_NUMBER,
                          dummy, dummy,
                          NetDevice::PacketType::PACKET_OTHERHOST);

  m_nBtsInFlight++;
  m_btsSendTrace(m_nBtsInFlight, GetNBytes());
}

void PfifoBoltQueueDisc::BtsWithArtificialDelay(Ipv4Header ipv4h,
                                                BoltHeaderPtr *bolth) {
  NS_LOG_FUNCTION(this << ipv4h << bolth);

  Ptr<Packet> p = Create<Packet>();

  BoltHeader newBolth;
  newBolth.SetSrcPort(bolth->GetDstPort());
  newBolth.SetDstPort(bolth->GetSrcPort());
  newBolth.SetSeqAckNo(bolth->GetSeqAckNo() + 1454);
  uint16_t flag = (bolth->GetFlags() & ~BoltHeader::Flags_t::DATA) |
                  BoltHeader::Flags_t::BTS | BoltHeader::Flags_t::DECWIN;
  newBolth.SetFlags(flag);
  newBolth.SetTxMsgId(bolth->GetTxMsgId());
  newBolth.SetDrainTime(bolth->GetDrainTime());
  newBolth.SetReflectedDelay(bolth->GetReflectedDelay());
  // TODO(serhatarslan): Lookup the default TTL value instead of using 64
  newBolth.SetReflectedHopCnt(64 - ipv4h.GetTtl());
  p->AddHeader(newBolth);

  Ipv4Header newIpv4h;
  newIpv4h.SetSource(ipv4h.GetDestination());
  newIpv4h.SetDestination(ipv4h.GetSource());
  newIpv4h.SetProtocol(BoltL4Protocol::PROT_NUMBER);
  newIpv4h.SetDontFragment();
  newIpv4h.SetPayloadSize(p->GetSize());

  Time delay = NanoSeconds(0);
  // Time delay = NanoSeconds(bolth->GetDrainTime());
  // Time delay = NanoSeconds(1273);                          // Bts Clock
  // Time delay = NanoSeconds(bolth->GetDrainTime() + 1273); // Egress Bts Clock
  // Time delay = NanoSeconds(6593);             // RTT clock Time delay =
  // NanoSeconds(bolth->GetDrainTime() + 6593);  // Normal Ack Clock
  Simulator::Schedule(delay, &PfifoBoltQueueDisc::SendArtificialBts, this,
                      bolth->GetSrcPort(), p, newIpv4h);
}

void PfifoBoltQueueDisc::SendArtificialBts(uint16_t srcPort, Ptr<Packet> p,
                                           Ipv4Header newIpv4h) {
  Ptr<Node> senderNode = NodeList::GetNode(srcPort - 1000);
  Ptr<BoltL4Protocol> boltL4Protocol = senderNode->GetObject<BoltL4Protocol>();
  boltL4Protocol->Receive(p, newIpv4h, 0);
}

uint16_t PfifoBoltQueueDisc::SetBitRateFlag(uint16_t curFlag) {
  NS_LOG_FUNCTION(this << curFlag);

  // Reset all the bit rate flags
  uint16_t newFlag = curFlag & 0x07ff;

  uint64_t bitRate = m_boundNetDevice->GetDataRate().GetBitRate();
  switch (bitRate) {
    case 10000000000lu:
      newFlag |= BoltHeader::Flags_t::LINK10G;
      break;
    case 25000000000lu:
      newFlag |= BoltHeader::Flags_t::LINK25G;
      break;
    case 40000000000lu:
      newFlag |= BoltHeader::Flags_t::LINK40G;
      break;
    case 100000000000lu:
      newFlag |= BoltHeader::Flags_t::LINK100G;
      break;
    case 400000000000lu:
      newFlag |= BoltHeader::Flags_t::LINK400G;
      break;
    default:
      NS_ASSERT_MSG(
          false, "Error: Bitrate " << bitRate << " not recognized for Bolt.");
      break;
  }
  return newFlag;
}

bool PfifoBoltQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item) {
  NS_LOG_FUNCTION(this << item);

  Ptr<Packet> p = item->GetPacket();
  // Use direct pointers to accelerate packet processing in the simulation
  uint8_t *buf = p->GetBuffer();
  BoltHeaderPtr *bolth = reinterpret_cast<BoltHeaderPtr *>(buf);
  NS_LOG_DEBUG("PfifoBoltQueueDisc (" << this
                                      << ") received: " << p->ToString());

  uint8_t priority = 0;
  SocketPriorityTag priorityTag;
  if (p->PeekPacketTag(priorityTag)) priority = priorityTag.GetPriority();
  uint32_t band = prio2band[priority & 0x0f];

  CalculateCurrentlyAvailableBw(p->GetSize());

  uint16_t boltFlag = bolth->GetFlags();
  uint16_t highPriorityFlags =
      BoltHeader::Flags_t::ACK | BoltHeader::Flags_t::NACK |
      BoltHeader::Flags_t::CHOP | BoltHeader::Flags_t::BTS;
  if (boltFlag & highPriorityFlags) {
    band = 0;
  } else {
    NS_ASSERT(boltFlag & BoltHeader::Flags_t::DATA);

    Ipv4QueueDiscItem *ipv4Item = dynamic_cast<Ipv4QueueDiscItem *>(&(*(item)));
    Ipv4Header ipv4h = ipv4Item->GetHeader();
    NS_ASSERT(ipv4h.GetProtocol() == BoltL4Protocol::PROT_NUMBER);

    // Perform congestion control
    uint32_t curNBytes = GetNBytes();
    if (curNBytes >= m_ccThreshold.GetValue()) {
      uint32_t curDrainTime = m_boundNetDevice->GetDataRate()
                                  .CalculateBytesTxTime(curNBytes)
                                  .GetNanoSeconds();

      // Make sure noone else increases cwnd of this flow
      uint16_t flags = boltFlag & ~BoltHeader::Flags_t::INCWIN;
      if (m_btsEnabled) flags |= BoltHeader::Flags_t::DECWIN;

      if (!(boltFlag & BoltHeader::Flags_t::DECWIN)) {
        // if (curDrainTime > bolth->GetDrainTime()) {
        bolth->SetDrainTime(curDrainTime);
        flags = SetBitRateFlag(flags);
        bolth->SetFlags(flags);

        if (m_btsEnabled && (m_nBtsInFlight % m_reducedBtsFactor == 0)) {
          NotifySender(ipv4h, bolth);
          // BtsWithArtificialDelay(ipv4h, bolth);
        } else {
          flags |= BoltHeader::Flags_t::DECWIN;
          bolth->SetFlags(flags);
        }
      } else {
        if (curDrainTime > bolth->GetDrainTime()) {
          bolth->SetDrainTime(curDrainTime);
          flags = SetBitRateFlag(flags);
        }
        bolth->SetFlags(flags);
      }

      if (GetCurrentSize() >= GetMaxSize()) {
        // The buffer is actually full. Trim payload and forward the header to
        // enable quick detection of packet loss.
        if (m_trimEnabled) {
          NS_LOG_LOGIC("Queue disc limit exceeded -- trimming packet");
          TrimPacketPayload(p, bolth);
          band = 0;
        } else {
          NS_LOG_LOGIC("Queue disc limit exceeded -- dropping packet");
          IncreaseDroppedBytesBeforeEnqueueStats(bolth->GetSerializedSize());
          DropBeforeEnqueue(item, LIMIT_EXCEEDED_DROP);
          return false;
        }
      }
    } else if (m_pruEnabled && (boltFlag & BoltHeader::Flags_t::LAST)) {
      // This pkt will not naturally cause a new pkt to be injected once acked
      if (!(boltFlag & BoltHeader::Flags_t::FIRST)) {
        if (m_nPruToken < m_maxPruToken) {
          m_nPruToken++;
        } else {
          m_nPruToken = m_maxPruToken;
        }
      }
    } else if (m_pruEnabled && m_nPruToken > 0 &&
               boltFlag & BoltHeader::Flags_t::INCWIN) {
      // There will be available bandwidth in the future
      m_nPruToken--;
    } else if (m_absEnabled && m_availLoad >= m_boundNetDevice->GetMtu() &&
               boltFlag & BoltHeader::Flags_t::INCWIN) {
      // There is available bandwidth
      m_availLoad -= m_boundNetDevice->GetMtu();
    } else {
      bolth->SetFlags(boltFlag & ~BoltHeader::Flags_t::INCWIN);
    }
  }

  bool retval = GetInternalQueue(band)->Enqueue(item);

  // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
  // internal queue because QueueDisc::AddInternalQueue sets the trace callback

  if (!retval)
    NS_LOG_WARN("Packet enqueue failed. Check the size of internal queues");

  NS_LOG_LOGIC("Number of packets in band "
               << band << ": " << GetInternalQueue(band)->GetNPackets());

  return retval;
}

Ptr<QueueDiscItem> PfifoBoltQueueDisc::DoDequeue(void) {
  NS_LOG_FUNCTION(this);

  Ptr<QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNInternalQueues(); i++) {
    if ((item = GetInternalQueue(i)->Dequeue()) != 0) {
      NS_LOG_LOGIC("Popped from band " << i << ": " << item);
      NS_LOG_LOGIC("Number of packets in band "
                   << i << ": " << GetInternalQueue(i)->GetNPackets());
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

Ptr<const QueueDiscItem> PfifoBoltQueueDisc::DoPeek(void) {
  NS_LOG_FUNCTION(this);

  Ptr<const QueueDiscItem> item;

  for (uint32_t i = 0; i < GetNInternalQueues(); i++) {
    if ((item = GetInternalQueue(i)->Peek()) != 0) {
      NS_LOG_LOGIC("Peeked from band " << i << ": " << item);
      NS_LOG_LOGIC("Number of packets in band "
                   << i << ": " << GetInternalQueue(i)->GetNPackets());
      return item;
    }
  }

  NS_LOG_LOGIC("Queue empty");
  return item;
}

bool PfifoBoltQueueDisc::CheckConfig(void) {
  NS_LOG_FUNCTION(this);
  if (GetNQueueDiscClasses() > 0) {
    NS_LOG_ERROR("PfifoBoltQueueDisc cannot have classes");
    return false;
  }

  if (GetNPacketFilters() != 0) {
    NS_LOG_ERROR("PfifoBoltQueueDisc needs no packet filter");
    return false;
  }

  if (GetNInternalQueues() == 0) {
    // create 3 DropTail queues with GetLimit() packets each
    ObjectFactory factory;
    factory.SetTypeId("ns3::DropTailQueue<QueueDiscItem>");
    factory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
    AddInternalQueue(factory.Create<InternalQueue>());
    AddInternalQueue(factory.Create<InternalQueue>());
    AddInternalQueue(factory.Create<InternalQueue>());
  }

  if (GetNInternalQueues() != 3) {
    NS_LOG_ERROR("PfifoBoltQueueDisc needs 3 internal queues");
    return false;
  }

  if (GetInternalQueue(0)->GetMaxSize().GetUnit() != QueueSizeUnit::PACKETS ||
      GetInternalQueue(1)->GetMaxSize().GetUnit() != QueueSizeUnit::PACKETS ||
      GetInternalQueue(2)->GetMaxSize().GetUnit() != QueueSizeUnit::PACKETS) {
    NS_LOG_ERROR("PfifoBoltQueueDisc needs 3 internal queues "
                 << "operating in packet mode");
    return false;
  }

  for (uint8_t i = 0; i < 2; i++) {
    if (GetInternalQueue(i)->GetMaxSize() < GetMaxSize()) {
      NS_LOG_ERROR("The capacity of some internal queue(s) is "
                   << "less than the queue disc capacity");
      return false;
    }
  }

  if (m_ccThreshold.GetUnit() != QueueSizeUnit::BYTES) {
    NS_LOG_ERROR(
        "The congestion control threshold should be provided in Bytes!");
    return false;
  }

  if (m_ccThreshold.GetValue() > GetMaxSize().GetValue() * 1500) {
    // TODO(serhatarslan): Find a generic method to compute the default MTU
    NS_LOG_ERROR("The congestion control threshold is not smaller than "
                 << "the total size of the queue disc!");
    return false;
  }

  return true;
}

void PfifoBoltQueueDisc::InitializeParams(void) {
  NS_LOG_FUNCTION(this);

  m_availLoad = 0;
  m_availLoadUpdateTime = Simulator::Now().GetNanoSeconds();

  Ptr<NetDevice> device =
      GetNetDeviceQueueInterface()->GetTxQueue(0)->GetNetDevice();
  m_boundNetDevice = dynamic_cast<PointToPointNetDevice *>(&(*(device)));

  m_nPruToken = 0;
  m_nBtsInFlight = 0;
}

}  // namespace ns3
