/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#include "ns3/object.h"
#include "bolt-socket-factory.h"
#include "bolt-l4-protocol.h"
#include "ns3/socket.h"
#include "ns3/assert.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (BoltSocketFactory);

TypeId BoltSocketFactory::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::BoltSocketFactory")
    .SetParent<SocketFactory> ()
    .SetGroupName ("Internet")
  ;
  return tid;
}

BoltSocketFactory::BoltSocketFactory ()
  : m_bolt (0)
{
}
BoltSocketFactory::~BoltSocketFactory ()
{
  NS_ASSERT (m_bolt == 0);
}

void
BoltSocketFactory::SetBolt (Ptr<BoltL4Protocol> bolt)
{
  m_bolt = bolt;
}

Ptr<Socket>
BoltSocketFactory::CreateSocket (void)
{
  return m_bolt->CreateSocket ();
}

void
BoltSocketFactory::DoDispose (void)
{
  m_bolt = 0;
  Object::DoDispose ();
}

} // namespace ns3
