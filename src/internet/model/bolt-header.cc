/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#include <stdint.h>
#include <iostream>

#include "bolt-header.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("BoltHeader");

NS_OBJECT_ENSURE_REGISTERED (BoltHeader);

/* The magic values below are used only for debugging.
 * They can be used to easily detect memory corruption
 * problems so you can see the patterns in memory.
 */
BoltHeader::BoltHeader()
    : m_srcPort(0xfffd),
      m_dstPort(0xfffd),
      m_seqAckNo(0xffffffff),
      m_flags(0),
      m_txMsgId(0),
      m_drainTime(0),
      m_reflectedDelay(0),
      m_reflectedHopCnt(0) {}

BoltHeader::~BoltHeader() {}

TypeId BoltHeader::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::BoltHeader")
                          .SetParent<Header>()
                          .SetGroupName("Internet")
                          .AddConstructor<BoltHeader>();
  return tid;
}
TypeId BoltHeader::GetInstanceTypeId(void) const { return GetTypeId(); }

void BoltHeader::Print(std::ostream& os) const {
  os << "length: " << GetSerializedSize() << " " << m_srcPort << " > "
     << m_dstPort << " txMsgId: " << m_txMsgId << " seqAckNo: " << m_seqAckNo
     << " hopCnt: " << (uint16_t)m_reflectedHopCnt
     << " reflectedDelay: " << m_reflectedDelay << " drainTime: " << m_drainTime
     << " " << FlagsToString(m_flags);
}

uint32_t BoltHeader::GetSerializedSize(void) const {
  return sizeof(m_srcPort) + sizeof(m_dstPort) + sizeof(m_txMsgId) +
         sizeof(m_seqAckNo) + sizeof(m_flags) + sizeof(m_reflectedHopCnt) +
         sizeof(m_reflectedDelay) + sizeof(m_drainTime);
}

std::string BoltHeader::FlagsToString(uint16_t flags,
                                      const std::string& delimiter) {
  static const char* flagNames[16] = {
      "DATA",    "ACK",     "NACK",     "CHOP",    "BTS", "FIN",
      "LAST",    "FIRST",   "INCWIN",   "DECWIN",  "AI",  "LINK10G",
      "LINK25G", "LINK40G", "LINK100G", "LINK400G"};
  std::string flagsDescription = "";
  for (uint16_t i = 0; i < 16; ++i) {
    if (flags & (1 << i)) {
      if (flagsDescription.length() > 0) flagsDescription += delimiter;
      flagsDescription.append(flagNames[i]);
    }
  }
  return flagsDescription;
}

void BoltHeader::Serialize(Buffer::Iterator start) const {
  Buffer::Iterator i = start;

  i.WriteU16(m_srcPort);
  i.WriteU16(m_dstPort);
  i.WriteU32(m_seqAckNo);
  i.WriteU16(m_flags);
  i.WriteU16(m_txMsgId);
  i.WriteU32(m_drainTime);
  i.WriteU32(m_reflectedDelay);
  i.WriteU8(m_reflectedHopCnt);
}
uint32_t BoltHeader::Deserialize(Buffer::Iterator start) {
  Buffer::Iterator i = start;
  m_srcPort = i.ReadU16();
  m_dstPort = i.ReadU16();
  m_seqAckNo = i.ReadU32();
  m_flags = i.ReadU16();
  m_txMsgId = i.ReadU16();
  m_drainTime = i.ReadU32();
  m_reflectedDelay = i.ReadU32();
  m_reflectedHopCnt = i.ReadU8();

  return GetSerializedSize();
}

void BoltHeader::SetSrcPort(uint16_t port) { m_srcPort = port; }
uint16_t BoltHeader::GetSrcPort(void) const { return m_srcPort; }

void BoltHeader::SetDstPort(uint16_t port) { m_dstPort = port; }
uint16_t BoltHeader::GetDstPort(void) const { return m_dstPort; }

void BoltHeader::SetTxMsgId(uint16_t txMsgId) { m_txMsgId = txMsgId; }
uint16_t BoltHeader::GetTxMsgId(void) const { return m_txMsgId; }

void BoltHeader::SetSeqAckNo(uint32_t seqAckNo) { m_seqAckNo = seqAckNo; }
uint32_t BoltHeader::GetSeqAckNo(void) const { return m_seqAckNo; }

void BoltHeader::SetFlags(uint16_t flags) { m_flags = flags; }
uint16_t BoltHeader::GetFlags(void) const { return m_flags; }

void BoltHeader::SetReflectedHopCnt(uint8_t reflectedHopCnt) {
  m_reflectedHopCnt = reflectedHopCnt;
}
uint8_t BoltHeader::GetReflectedHopCnt(void) const { return m_reflectedHopCnt; }

void BoltHeader::SetReflectedDelay(uint32_t reflectedDelay) {
  m_reflectedDelay = reflectedDelay;
}
uint32_t BoltHeader::GetReflectedDelay(void) const { return m_reflectedDelay; }

void BoltHeader::SetDrainTime(uint32_t drainTime) { m_drainTime = drainTime; }
uint32_t BoltHeader::GetDrainTime(void) const { return m_drainTime; }

}  // namespace ns3
