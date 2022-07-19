/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2020 Stanford University
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Serhat Arslan <sarslan@stanford.edu>
 */

#include "msg-generator-app.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/callback.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/string.h"
#include "ns3/double.h"

#include "ns3/udp-socket-factory.h"
#include "ns3/bolt-socket-factory.h"
#include "ns3/point-to-point-net-device.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MsgGeneratorApp");

NS_OBJECT_ENSURE_REGISTERED (MsgGeneratorApp);

TypeId MsgGeneratorApp::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::MsgGeneratorApp")
          .SetParent<Application>()
          .SetGroupName("Applications")
          .AddAttribute("Protocol",
                        "The type of protocol to use. This should be "
                        "a subclass of ns3::SocketFactory",
                        TypeIdValue(BoltSocketFactory::GetTypeId()),
                        MakeTypeIdAccessor(&MsgGeneratorApp::m_tid),
                        // This should check for SocketFactory as a parent
                        MakeTypeIdChecker())
          .AddAttribute(
              "MaxMsg",
              "The total number of messages to send. The value zero means "
              "that there is no limit.",
              UintegerValue(0),
              MakeUintegerAccessor(&MsgGeneratorApp::m_maxMsgs),
              MakeUintegerChecker<uint16_t>())
          .AddAttribute(
              "PayloadSize",
              "MTU for the network interface excluding the header sizes",
              UintegerValue(1400),
              MakeUintegerAccessor(&MsgGeneratorApp::m_maxPayloadSize),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute("UnitsInBytes",
                        "Whether units in workload are in bytes vs packets",
                        BooleanValue(false),
                        MakeBooleanAccessor(&MsgGeneratorApp::m_unitsInBytes),
                        MakeBooleanChecker())
          .AddAttribute("StaticMsgSize",
                        "Sets message sizes to a constant value if provided "
                        "for testing purposes",
                        UintegerValue(0),
                        MakeUintegerAccessor(&MsgGeneratorApp::m_staticMsgSize),
                        MakeUintegerChecker<uint32_t>());
  return tid;
}

MsgGeneratorApp::MsgGeneratorApp(Ipv4Address localIp, uint16_t localPort)
    : m_socket(0),
      m_interMsgTime(0),
      m_msgSize(0),
      m_remoteClient(0),
      m_totMsgCnt(0) {
  NS_LOG_FUNCTION(this << localIp << localPort);

  m_localIp = localIp;
  m_localPort = localPort;
}

MsgGeneratorApp::~MsgGeneratorApp() { NS_LOG_FUNCTION(this); }

void MsgGeneratorApp::Install(Ptr<Node> node,
                              std::vector<InetSocketAddress> remoteClients) {
  NS_LOG_FUNCTION(this << node);

  node->AddApplication(this);

  m_socket = Socket::CreateSocket(node, m_tid);
  m_socket->Bind(InetSocketAddress(m_localIp, m_localPort));
  m_socket->SetRecvCallback(
      MakeCallback(&MsgGeneratorApp::ReceiveMessage, this));

  for (std::size_t i = 0; i < remoteClients.size(); i++) {
    if (remoteClients[i].GetIpv4() != m_localIp ||
        remoteClients[i].GetPort() != m_localPort) {
      m_remoteClients.push_back(remoteClients[i]);
    } else {
      // Remove the local address from the client addresses list
      NS_LOG_LOGIC("MsgGeneratorApp ("
                   << this << ") removes address " << remoteClients[i].GetIpv4()
                   << ":" << remoteClients[i].GetPort()
                   << " from remote clients because it is the local address.");
    }
  }

  m_remoteClient = CreateObject<UniformRandomVariable>();
  m_remoteClient->SetAttribute("Min", DoubleValue(0));
  m_remoteClient->SetAttribute("Max", DoubleValue(m_remoteClients.size()));
}

void MsgGeneratorApp::SetWorkload(double load, std::map<double, int> msgSizeCDF,
                                  double avgMsgSize, uint64_t bottleneckBps) {
  NS_LOG_FUNCTION(this << avgMsgSize);

  load = std::max(0.0, std::min(load, 1.0));

  Ptr<NetDevice> netDevice = GetNode()->GetDevice(0);
  uint32_t mtu = netDevice->GetMtu();

  if (bottleneckBps == 0) {
    PointToPointNetDevice* p2pNetDevice =
        dynamic_cast<PointToPointNetDevice*>(&(*(netDevice)));
    bottleneckBps = p2pNetDevice->GetDataRate().GetBitRate();
  }

  double avgInterMsgTime;
  if (m_unitsInBytes) {
    double avgMsgSizePkts = avgMsgSize / (double)m_maxPayloadSize;
    avgInterMsgTime =
        (avgMsgSizePkts * (double)mtu * 8.0) / ((double)bottleneckBps * load);

  } else {
    avgInterMsgTime =
        (avgMsgSize * (double)mtu * 8.0) / (((double)bottleneckBps) * load);
  }

  m_interMsgTime = CreateObject<ExponentialRandomVariable>();
  m_interMsgTime->SetAttribute("Mean", DoubleValue(avgInterMsgTime));

  m_msgSizeCDF = msgSizeCDF;

  m_msgSize = CreateObject<UniformRandomVariable>();
  m_msgSize->SetAttribute("Min", DoubleValue(0));
  m_msgSize->SetAttribute("Max", DoubleValue(1));
}

void MsgGeneratorApp::Start(Time start) {
  NS_LOG_FUNCTION(this);

  SetStartTime(start);
  DoInitialize();
}

void MsgGeneratorApp::Stop(Time stop) {
  NS_LOG_FUNCTION(this);

  SetStopTime(stop);
}

void MsgGeneratorApp::DoDispose(void) {
  NS_LOG_FUNCTION(this);

  CancelNextEvent();
  // chain up
  Application::DoDispose();
}

void MsgGeneratorApp::StartApplication() {
  NS_LOG_FUNCTION(Simulator::Now().GetNanoSeconds() << this);

  if (m_remoteClient && m_interMsgTime && m_msgSize)
    ScheduleNextMessage();
  else
    NS_LOG_LOGIC("MsgGeneratorApp (" << this
                                     << ") is not assigned as a sender");
}

void MsgGeneratorApp::StopApplication() {
  NS_LOG_FUNCTION(Simulator::Now().GetNanoSeconds() << this);

  CancelNextEvent();
}

void MsgGeneratorApp::CancelNextEvent() {
  NS_LOG_FUNCTION(this);

  if (!m_nextSendEvent.IsExpired()) Simulator::Cancel(m_nextSendEvent);
}

void MsgGeneratorApp::ScheduleNextMessage() {
  NS_LOG_FUNCTION(Simulator::Now().GetNanoSeconds() << this);

  if (m_nextSendEvent.IsExpired()) {
    m_nextSendEvent = Simulator::Schedule(Seconds(m_interMsgTime->GetValue()),
                                          &MsgGeneratorApp::SendMessage, this);
  } else {
    NS_LOG_LOGIC(
        "MsgGeneratorApp ("
        << this
        << ") tries to schedule the next msg before the previous one is sent!");
  }
}

uint32_t MsgGeneratorApp::GetNextMsgSizeFromDist() {
  NS_LOG_FUNCTION(this);

  int msgSize = -1;
  double rndValue = m_msgSize->GetValue();
  for (auto it = m_msgSizeCDF.begin(); it != m_msgSizeCDF.end(); it++) {
    if (rndValue <= it->first) {
      msgSize = it->second;
      break;
    }
  }

  NS_ASSERT(msgSize >= 0);

  if (m_unitsInBytes)
    return (uint32_t)msgSize;
  else {
    if (m_maxPayloadSize > 0)
      return m_maxPayloadSize * (uint32_t)msgSize;
    else
      return GetNode()->GetDevice(0)->GetMtu() * (uint32_t)msgSize;
    // NOTE: If maxPayloadSize is set to zero, the generated messages will be
    //       slightly larger than the intended number of packets due to the
    //       addition of the protocol headers.
  }
}

void MsgGeneratorApp::SendMessage() {
  NS_LOG_FUNCTION(Simulator::Now().GetNanoSeconds() << this);

  /* Decide which remote client to send to */
  double rndValue = m_remoteClient->GetValue();
  int remoteClientIdx = (int)std::floor(rndValue);
  InetSocketAddress receiverAddr = m_remoteClients[remoteClientIdx];

  /* Decide on the message size to send */
  uint32_t msgSizeBytes;
  if (m_staticMsgSize == 0) {
    msgSizeBytes = GetNextMsgSizeFromDist();
  } else {
    msgSizeBytes = m_staticMsgSize;
  }

  /* Create the message to send */
  Ptr<Packet> msg = Create<Packet>(msgSizeBytes);
  NS_LOG_LOGIC("MsgGeneratorApp {" << this << ") generates a message of size: "
                                   << msgSizeBytes << " Bytes.");

  int sentBytes = m_socket->SendTo(msg, 0, receiverAddr);

  if (sentBytes > 0) {
    NS_LOG_INFO(Simulator::Now().GetNanoSeconds()
                << " " << m_localIp << ":" << m_localPort << " sent "
                << sentBytes << " Bytes to " << receiverAddr.GetIpv4() << ":"
                << receiverAddr.GetPort());
    m_totMsgCnt++;
  }

  if (m_maxMsgs == 0 || m_totMsgCnt < m_maxMsgs) ScheduleNextMessage();
}

void MsgGeneratorApp::ReceiveMessage(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this);

  Ptr<Packet> message;
  Address from;
  while ((message = socket->RecvFrom(from))) {
    NS_LOG_INFO(Simulator::Now().GetNanoSeconds()
                << " " << m_localIp << ":" << m_localPort << " received "
                << message->GetSize() << " Bytes from "
                << InetSocketAddress::ConvertFrom(from).GetIpv4() << ":"
                << InetSocketAddress::ConvertFrom(from).GetPort());
  }
}

} // Namespace ns3
