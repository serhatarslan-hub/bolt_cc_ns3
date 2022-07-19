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

#include "bolt-header-ptr.h"

namespace ns3 {

/* The magic values below are used only for debugging.
 * They can be used to easily detect memory corruption
 * problems so you can see the patterns in memory.
 */
BoltHeaderPtr::BoltHeaderPtr()
    : m_srcPort(0xfffd),
      m_dstPort(0xfffd),
      m_seqAckNo(0xffffffff),
      m_flags(0),
      m_txMsgId(0),
      m_drainTime(0),
      m_reflectedDelay(0),
      m_reflectedHopCnt(0) {}

BoltHeaderPtr::~BoltHeaderPtr() {}

uint32_t BoltHeaderPtr::GetSerializedSize(void) const {
  return sizeof(m_srcPort) + sizeof(m_dstPort) + sizeof(m_txMsgId) +
         sizeof(m_seqAckNo) + sizeof(m_flags) + sizeof(m_reflectedHopCnt) +
         sizeof(m_reflectedDelay) + sizeof(m_drainTime);
}

void BoltHeaderPtr::SetSrcPort(uint16_t port) { m_srcPort = port; }
uint16_t BoltHeaderPtr::GetSrcPort(void) const { return m_srcPort; }

void BoltHeaderPtr::SetDstPort(uint16_t port) { m_dstPort = port; }
uint16_t BoltHeaderPtr::GetDstPort(void) const { return m_dstPort; }

void BoltHeaderPtr::SetTxMsgId(uint16_t txMsgId) { m_txMsgId = txMsgId; }
uint16_t BoltHeaderPtr::GetTxMsgId(void) const { return m_txMsgId; }

void BoltHeaderPtr::SetSeqAckNo(uint32_t seqAckNo) { m_seqAckNo = seqAckNo; }
uint32_t BoltHeaderPtr::GetSeqAckNo(void) const { return m_seqAckNo; }

void BoltHeaderPtr::SetFlags(uint16_t flags) { m_flags = flags; }
uint16_t BoltHeaderPtr::GetFlags(void) const { return m_flags; }

void BoltHeaderPtr::SetReflectedHopCnt(uint8_t reflectedHopCnt) {
  m_reflectedHopCnt = reflectedHopCnt;
}
uint8_t BoltHeaderPtr::GetReflectedHopCnt(void) const {
  return m_reflectedHopCnt;
}

void BoltHeaderPtr::SetReflectedDelay(uint32_t reflectedDelay) {
  m_reflectedDelay = reflectedDelay;
}
uint32_t BoltHeaderPtr::GetReflectedDelay(void) const {
  return m_reflectedDelay;
}

void BoltHeaderPtr::SetDrainTime(uint32_t drainTime) {
  m_drainTime = drainTime;
}
uint32_t BoltHeaderPtr::GetDrainTime(void) const { return m_drainTime; }

}  // namespace ns3
