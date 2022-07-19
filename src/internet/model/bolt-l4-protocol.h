/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#ifndef BOLT_L4_PROTOCOL_H
#define BOLT_L4_PROTOCOL_H

#include <stdint.h>

#include <functional>
#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ip-l4-protocol.h"
#include "ns3/bolt-header.h"
#include "ns3/data-rate.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Node;
class Socket;
class Ipv4EndPointDemux;
class Ipv4EndPoint;
class BoltSocket;
class NetDevice;
class BoltSendScheduler;
class BoltOutboundMsg;
class BoltRecvScheduler;
class BoltInboundMsg;

/**
 * \ingroup internet
 * \defgroup bolt BOLT
 *
 * This  is  the  implementation of the Bolt Congestion Control algorithm
 * along with its reliable transport support.
 */

/**
 * \ingroup bolt
 * \brief Implementation of the Bolt CC
 */
class BoltL4Protocol : public IpL4Protocol {
 public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);
  static const uint8_t PROT_NUMBER;  //!< Protocol number of BOLT in IP packets

  enum CcMode_e {
    CC_DEFAULT,  //!< Congestion control of Bolt itself
    CC_SWIFT,    //!< Swift's delay based congestion control algorithm
  };

  BoltL4Protocol();
  virtual ~BoltL4Protocol();

  /**
   * Set node associated with this stack.
   * \param node The corresponding node.
   */
  void SetNode(Ptr<Node> node);
  /**
   * \brief Get the node associated with this stack.
   * \return The corresponding node.
   */
  Ptr<Node> GetNode(void) const;

  /**
   * \brief Get the MTU of the associated net device
   * \return The corresponding MTU size in bytes.
   */
  uint32_t GetMtu(void) const;

  /**
   * \brief Get the approximated BDP value of the network in bytes
   * \return The number of packets required for full utilization, ie. BDP.
   */
  uint32_t GetBdp(void) const;

  /**
   * \brief Get the protocol number associated with Bolt CC.
   * \return The protocol identifier of Bolt used in IP headers.
   */
  virtual int GetProtocolNumber(void) const;

  /**
   * \brief Get flags that should be reflected by the receiver in ACK/NACK pkts
   * \return The BoltHeader flags that should be reflected
   */
  uint16_t GetFlagsToReflect(void) const;

  /**
   * \brief Get rtx timeout duration for inbound messages
   * \return Time value to determine the retransmission timeout of InboundMsgs
   */
  Time GetInboundRtxTimeout(void) const;

  /**
   * \brief Get rtx timeout duration for outbound messages
   * \return Time value to determine the retransmission timeout of OutboundMsgs
   */
  Time GetOutboundRtxTimeout(void) const;

  /**
   * \brief Get the maximum number of rtx timeouts allowed per message
   * \return Maximum allowed rtx timeout count per message
   */
  uint16_t GetMaxNumRtxPerMsg(void) const;

  /**
   * \brief Get the maximum amount of data in packet payload
   * \return Number of bytes that can be stored in packet excluding headers
   */
  uint32_t GetMaxPayloadSize(void) const;

  /**
   * \brief Get the total number of packets for a given message size.
   * \param msgSizeByte The size of the message in bytes
   * \return The number of packets
   */
  uint32_t GetMsgSizePkts(uint32_t msgSizeByte);

  /**
   * \brief Get marked bitrate flag and return bit per nanoseconds.
   * \param flag The flags of a packet that also includes the bottleneck bitrate
   * \return The bitrate embedded on the given flag in bits per nanoseconds
   */
  uint64_t GetBitPerNsFromFlag(uint16_t flag);

  /**
   * \brief Get the line rate at which the NetDevice is running at
   * \return The line rate at which the NetDevice is running at
   */
  DataRate GetLinkRate(void) const;

  /**
   * \brief Get the congestion control algorithm that is currently running
   * \return The type of the congestion control algorithm
   */
  enum CcMode_e GetCcMode(void) const;

  /**
   * \brief Return whether to aggregate messages of the same recipient into one
   * \returns the m_msgAggEnabled flag
   */
  bool AggregateMsgsIfPossible(void) const;

  /**
   * \brief Get the frequency of cwnd decrement per RTT
   * \return The number of times cwnd decrement is allowed per RTT
   */
  uint64_t GetDecWinPerRtt(void) const;

  /**
   * \brief Get smoothing factor for RTT measurements
   * \return The EMWA factor of RTT measurements
   */
  double GetRttSmoothingAlpha(void) const;

  /**
   * \brief Get the topology scaling factor for target delay calculation
   * \return The topology scaling factor per hop
   */
  uint16_t GetTopoScalingPerHop(void) const;

  /**
   * \brief Get the Alpha value for target delay computation
   * \return The Alpha value for target delay computation
   */
  double GetFlowScalingAlpha(void) const;

  /**
   * \brief Get the Beta value for target delay computation
   * \return The Beta value for target delay computation
   */
  double GetFlowScalingBeta(void) const;

  /**
   * \brief Get the maximum flow scaling factor allowed
   * \return The maximum flow scaling factor
   */
  double GetMaxFlowScaling(void) const;

  /**
   * \brief Get base delay to start the target delay computation
   * \return The base delay for the target delay computation
   */
  uint64_t GetBaseDelay(void) const;

  /**
   * \brief Get additive increase factor for congestion control
   * \return The additive increase factor
   */
  double GetAiFactor(void) const;

  /**
   * \brief Get multiplicative decrease factor for congestion control
   * \return The multiplicative decrease factor
   */
  double GetMdFactor(void) const;

  /**
   * \brief Get the maximum multiplicative decrease allowed
   * \return The maximum multiplicative decrease
   */
  double GetMaxMd(void) const;

  /**
   * \brief Get the minimum cwnd allowed in bytes
   * \return The minimum cwnd allowed for a flow in bytes
   */
  uint32_t GetMinCwnd(void) const;

  /**
   * \brief Get the maximum cwnd allowed in bytes
   * \return The maximum cwnd allowed for a flow in bytes
   */
  uint32_t GetMaxCwnd(void) const;

  /**
   * \brief Return whether to use per hop delay instead of RTT for CC
   * \returns the m_usePerHopDelayForCc flag
   */
  bool ShouldUsePerHopDelayForCc(void) const;

  /**
   * \brief Get the target queue size in bytes
   * \return The target queue size in bytes
   */
  double GetTargetQ(void) const;

  /**
   * \return The associated BoltSendScheduler on this protocol instance
   */
  Ptr<BoltSendScheduler> GetSendScheduler(void) const;

  /**
   * \return The associated BoltRecvScheduler on this protocol instance
   */
  Ptr<BoltRecvScheduler> GetRecvScheduler(void) const;

  /**
   * \brief Return whether the memory optimizations are enabled
   * \returns the m_memIsOptimized
   */
  bool MemIsOptimized(void) const;

  /**
   * \brief Create a BoltSocket and associate it with this Bolt Protocol
   * instance. \return A smart Socket pointer to a BoltSocket, allocated by the
   * BOLT Protocol.
   */
  Ptr<Socket> CreateSocket(void);

  /**
   * \brief Allocate an IPv4 Endpoint
   * \return the Endpoint
   */
  Ipv4EndPoint *Allocate(void);
  /**
   * \brief Allocate an IPv4 Endpoint
   * \param address address to use
   * \return the Endpoint
   */
  Ipv4EndPoint *Allocate(Ipv4Address address);
  /**
   * \brief Allocate an IPv4 Endpoint
   * \param boundNetDevice Bound NetDevice (if any)
   * \param port port to use
   * \return the Endpoint
   */
  Ipv4EndPoint *Allocate(Ptr<NetDevice> boundNetDevice, uint16_t port);
  /**
   * \brief Allocate an IPv4 Endpoint
   * \param boundNetDevice Bound NetDevice (if any)
   * \param address address to use
   * \param port port to use
   * \return the Endpoint
   */
  Ipv4EndPoint *Allocate(Ptr<NetDevice> boundNetDevice, Ipv4Address address,
                         uint16_t port);
  /**
   * \brief Allocate an IPv4 Endpoint
   * \param boundNetDevice Bound NetDevice (if any)
   * \param localAddress local address to use
   * \param localPort local port to use
   * \param peerAddress remote address to use
   * \param peerPort remote port to use
   * \return the Endpoint
   */
  Ipv4EndPoint *Allocate(Ptr<NetDevice> boundNetDevice,
                         Ipv4Address localAddress, uint16_t localPort,
                         Ipv4Address peerAddress, uint16_t peerPort);

  /**
   * \brief Remove an IPv4 Endpoint.
   * \param endPoint the end point to remove
   */
  void DeAllocate(Ipv4EndPoint *endPoint);

  // called by BoltSocket.
  /**
   * \brief Send a message via Bolt CC (IPv4)
   * \param message The message to send
   * \param saddr The source Ipv4Address
   * \param daddr The destination Ipv4Address
   * \param sport The source port number
   * \param dport The destination port number
   */
  void Send(Ptr<Packet> message, Ipv4Address saddr, Ipv4Address daddr,
            uint16_t sport, uint16_t dport);
  /**
   * \brief Send a message via Bolt CC (IPv4)
   * \param message The message to send
   * \param saddr The source Ipv4Address
   * \param daddr The destination Ipv4Address
   * \param sport The source port number
   * \param dport The destination port number
   * \param route The route requested by the sender
   */
  void Send(Ptr<Packet> message, Ipv4Address saddr, Ipv4Address daddr,
            uint16_t sport, uint16_t dport, Ptr<Ipv4Route> route);

  // called by BoltSendScheduler or BoltRecvScheduler.
  /**
   * \brief Send the selected packet down to the IP Layer
   * \param packet The packet to send
   * \param saddr The source Ipv4Address
   * \param daddr The destination Ipv4Address
   * \param route The route requested by the sender
   */
  void SendDown(Ptr<Packet> packet, Ipv4Address saddr, Ipv4Address daddr,
                Ptr<Ipv4Route> route = 0);

  // inherited from Ipv4L4Protocol
  /**
   * \brief Receive a packet from the lower IP layer
   * \param p The arriving packet from the network
   * \param header The IPv4 header of the arriving packet
   * \param interface The interface from which the packet arrives
   */
  virtual enum IpL4Protocol::RxStatus Receive(Ptr<Packet> p,
                                              Ipv4Header const &header,
                                              Ptr<Ipv4Interface> interface);
  virtual enum IpL4Protocol::RxStatus Receive(Ptr<Packet> p,
                                              Ipv6Header const &header,
                                              Ptr<Ipv6Interface> interface);

  /**
   * \brief Forward the reassembled message to the upper layers
   * \param completeMsg The message that is completely reassembled
   * \param header The IPv4 header associated with the message
   * \param sport The source port of the message
   * \param dport The destination port of the message
   * \param txMsgId The message ID determined by the sender
   * \param incomingInterface The interface from which the message arrived
   */
  void ForwardUp(Ptr<Packet> completeMsg, const Ipv4Header &header,
                 uint16_t sport, uint16_t dport, uint16_t txMsgId,
                 Ptr<Ipv4Interface> incomingInterface);

  // inherited from Ipv4L4Protocol (Not used for Bolt CC Purposes)
  virtual void ReceiveIcmp(Ipv4Address icmpSource, uint8_t icmpTtl,
                           uint8_t icmpType, uint8_t icmpCode,
                           uint32_t icmpInfo, Ipv4Address payloadSource,
                           Ipv4Address payloadDestination,
                           const uint8_t payload[8]);

  // From IpL4Protocol
  virtual void SetDownTarget(IpL4Protocol::DownTargetCallback cb);
  virtual void SetDownTarget6(IpL4Protocol::DownTargetCallback6 cb);

  // From IpL4Protocol
  virtual IpL4Protocol::DownTargetCallback GetDownTarget(void) const;
  virtual IpL4Protocol::DownTargetCallback6 GetDownTarget6(void) const;

  /**
   * \brief Call the msgAckedTrace Callback with the given arguments
   * \param msgSize The message size that is completely acknowledged
   * \param srcIp The source IPv4 address of the message
   * \param dstIp The destination IPv4 address of the message
   * \param srcPort The source port of the message
   * \param dstPort The destination port of the message
   * \param txMsgId The message ID determined by the sender
   */
  void MsgAckedTrace(uint32_t msgSize, Ipv4Address srcIp, Ipv4Address dstIp,
                     uint16_t srcPort, uint16_t dstPort, int txMsgId);

  /**
   * \brief Call the flowStatsTrace Callback with the given arguments
   * \param srcIp The source IPv4 address of the message
   * \param dstIp The destination IPv4 address of the message
   * \param srcPort The source port of the message
   * \param dstPort The destination port of the message
   * \param txMsgId The message ID determined by the sender
   * \param cwnd The current cwnd value in bytes
   * \param rtt The most recent rtt measurement in nanoseconds
   */
  void FlowStatsTrace(Ipv4Address srcIp, Ipv4Address dstIp, uint16_t srcPort,
                      uint16_t dstPort, int txMsgId, uint32_t cwnd,
                      uint64_t rtt);

 protected:
  virtual void DoDispose(void);
  /*
   * This function will notify other components connected to the node that a
   * new stack member is now connected. This will be used to notify Layer 3
   * protocol of layer 4 protocol stack to connect them together.
   */
  virtual void NotifyNewAggregate();

 private:
  Ptr<Node> m_node;                //!< the node this stack is associated with
  Ipv4EndPointDemux *m_endPoints;  //!< A list of IPv4 end points.

  std::vector<Ptr<BoltSocket>> m_sockets;  //!< list of sockets
  IpL4Protocol::DownTargetCallback
      m_downTarget;  //!< Callback to send packets over IPv4
  IpL4Protocol::DownTargetCallback6
      m_downTarget6;  //!< Callback to send packets over IPv6 (Not supported)

  bool m_memIsOptimized;  //!< High performant mode (only
                          //!< packet sizes are stored to save from memory)

  uint32_t m_mtu;             //!< The MTU of the bounded NetDevice
  uint32_t m_maxPayloadSize;  //!< Number of bytes that can be stored in packet
                              //!< excluding headers
  uint32_t m_bdp;             //!< The number of bytes it takes to transmit at
                              //!< line rate, ie. BDP.
  uint16_t m_flagsToReflect;  //!< BoltHeader flags to reflect from the receiver

  DataRate m_linkRate;  //!< Data Rate of the corresponding net device
                        //!< for this prototocol

  enum CcMode_e m_ccMode;  //!< Type of congestion control algorithm to run
  bool m_msgAggEnabled;    //!< Flag to aggregate msgs of the same recipient

  double m_rttSmoothingAlpha;    //!< Smoothing factor for the RTT measurements
  uint16_t m_topoScalingPerHop;  //!< Per hop scaling for target delay
  double m_maxFlowScaling;       //!< Maximum flow scaling factor
  double m_maxFlowScalingCwnd;   //!<
  double m_minFlowScalingCwnd;   //!<
  double m_flowScalingAlpha;     //!< Alpha value for target delay
  double m_flowScalingBeta;      //!< Beta value for target dealy
  uint64_t m_baseDelay;          //!< Base delay for the target delay
  double m_aiFactor;             //!< Additive increase for congestion control
  double m_mdFactor;   //!< Multiplicative decrease for congestion control
  double m_maxMd;      //!< Maximum multiplicative decrease allowed
  uint32_t m_minCwnd;  //!< Minimum cwnd allowed in bytes
  uint32_t m_maxCwnd;  //!< Maximum cwnd allowed in bytes
  bool m_usePerHopDelayForCc;  //!< Flag to use per hop delay instead of RTT
  double m_targetQ;    //!< Target queue size when handling BTS

  Ptr<BoltSendScheduler> m_sendScheduler;  //!< Scheduler of BoltOutboundMsgs
  Ptr<BoltRecvScheduler> m_recvScheduler;  //!< Scheduler of BoltInboundMsgs

  Time m_inboundRtxTimeout;   //!< The timeout duration of InboundMsgs
  Time m_outboundRtxTimeout;  //!< The timeout duration of OutboundMsgs
  uint16_t
      m_maxNumRtxPerMsg;  //!< Maximum allowed rtx timeout count per message

  TracedCallback<Ptr<const Packet>, Ipv4Address, Ipv4Address, uint16_t,
                 uint16_t, int>
      m_msgBeginTrace;
  TracedCallback<Ptr<const Packet>, Ipv4Address, Ipv4Address, uint16_t,
                 uint16_t, int>
      m_msgFinishTrace;
  TracedCallback<uint32_t, Ipv4Address, Ipv4Address, uint16_t, uint16_t, int>
      m_msgAckedTrace;

  TracedCallback<Ptr<const Packet>, Ipv4Address, Ipv4Address, uint16_t,
                 uint16_t, int, uint32_t, uint16_t>
      m_dataRecvTrace;  //!< Trace of {pkt, srcIp, dstIp, srcPort, dstPort,
                        //!< txMsgId, seqNo, flag} for arriving DATA packets
  TracedCallback<Ptr<const Packet>, Ipv4Address, Ipv4Address, uint16_t,
                 uint16_t, int, uint32_t, uint16_t>
      m_dataSendTrace;  //!< Trace of {pkt, srcIp, dstIp, srcPort, dstPort,
                        //!< txMsgId, seqNo, flag} for departing DATA packets
  TracedCallback<Ptr<const Packet>, Ipv4Address, Ipv4Address, uint16_t,
                 uint16_t, int, uint32_t, uint16_t>
      m_ctrlRecvTrace;  //!< Trace of {pkt, srcIp, dstIp, srcPort, dstPort,
                        //!< txMsgId, seqNo, flag} for arriving control packets
  TracedCallback<Ptr<const Packet>, Ipv4Address, Ipv4Address, uint16_t,
                 uint16_t, int, uint32_t, uint16_t>
      m_ctrlSendTrace;  //!< Trace of {pkt, srcIp, dstIp, srcPort, dstPort,
                        //!< txMsgId, seqNo, flag} for departing control packets

  TracedCallback<Ipv4Address, Ipv4Address, uint16_t, uint16_t, int, uint32_t,
                 uint64_t>
      m_flowStatsTrace;
};

/******************************************************************************/

/**
 * \ingroup bolt
 * \brief Stores the state for an outbound Bolt message
 */
class BoltOutboundMsg : public Object {
 public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);

  BoltOutboundMsg(Ptr<Packet> message, uint16_t txMsgId, Ipv4Address saddr,
                  Ipv4Address daddr, uint16_t sport, uint16_t dport,
                  Ptr<BoltL4Protocol> bolt);
  ~BoltOutboundMsg(void);

  /**
   * \brief Append the new message data onto the current message
   * \param message The new message that is to be appended
   * \param route The corresponding route requested by the sender
   */
  void AppendNewMsg(Ptr<Packet> message, Ptr<Ipv4Route> route);

  /**
   * \brief Set the route requested for this message. (0 if not source-routed)
   * \param route The corresponding route.
   */
  void SetRoute(Ptr<Ipv4Route> route);

  /**
   * \brief Get the route requested for this message. (0 if not source-routed)
   * \return The corresponding route.
   */
  Ptr<Ipv4Route> GetRoute(void);

  /**
   * \brief Get the total number of bytes for this flow.
   * \return The flow size in bytes
   */
  uint32_t GetFlowSizeBytes(void);

  /**
   * \brief Get the sender's IP address for this message.
   * \return The IPv4 address of the sender
   */
  Ipv4Address GetSrcAddress(void);

  /**
   * \brief Get the receiver's IP address for this message.
   * \return The IPv4 address of the receiver
   */
  Ipv4Address GetDstAddress(void);

  /**
   * \brief Get the sender's port number for this message.
   * \return The port number of the sender
   */
  uint16_t GetSrcPort(void);

  /**
   * \brief Get the receiver's port number for this message.
   * \return The port number of the receiver
   */
  uint16_t GetDstPort(void);

  /**
   * \brief Get the congestion window size of the message in bytes
   * \return The cwnd for the message
   */
  uint32_t GetCwnd(void);
  /**
   * \brief Set the congestion window size of the message in bytes
   * \param cwnd The cwnd for the message
   */
  void SetCwnd(uint32_t cwnd);

  /**
   * \brief Get the next sequence number to send for the message
   * \return The snd_next for the message
   */
  uint32_t GetSndNext(void);

  /**
   * \brief Get the highest acknoledgement number received for the message
   * \return The ack_no for the message
   */
  uint32_t GetAckNo(void);

  /**
   * \brief Determines whether the available data is completely acknowledged
   * \return Whether the available data is completely acknowledged
   */
  bool IsIdle(void);

  /**
   * \brief Determines whether the last packet of the current msg is in flight
   * \return Whether the last packet of the current msg is in flight
   */
  bool FinPktIsInFlight(void);

  /**
   * \brief Get the latest RTT measured for this message
   * \return The RTT of the last acknowledged packet
   */
  uint64_t GetRtt(void);

  /**
   * \brief Get the retransmission event for this message, scheduled or expired.
   * \return The most recent retransmission event scheduled for the message
   */
  EventId GetRtxEvent(void);

  /**
   * \brief Get the trsanmission event for this message, scheduled or expired.
   * \return The most recent transmission event scheduled for the message
   */
  EventId GetSndEvent(void);

  /**
   * \brief Update the message state per the received Ack
   * \param boltHeader The header information for the received Ack
   */
  void HandleAck(BoltHeader const &boltHeader);

  /**
   * \brief Update the message state per the received Nack
   * \param boltHeader The header information for the received Nack
   */
  void HandleNack(BoltHeader const &boltHeader);

  /**
   * \brief Update the message state per the received control packet
   * \param boltHeader The header information for the received control packet
   * \param numBytesAcked The number bytes acknowledged for this cwnd update
   */
  void UpdateCwnd(BoltHeader const &boltHeader, uint32_t numBytesAcked);

  /**
   * \brief Update the message state according to Bolt-CC algorithm
   * \param boltHeader The header information for the received control packet
   * \param numBytesAcked The number bytes acknowledged for this cwnd update
   */
  void UpdateCwndAsBolt(BoltHeader const &boltHeader, uint32_t numBytesAcked);

  /**
   * \brief Get the target delay for this flow based on the topology and flow
   * \param cwndPkts Current cwnd value in packets for this flow
   * \param nHops Number of reflected hops for this flow
   * \param shouldUsePerHopDelay Flag to use only one hop for target calculation
   * \return The target RTT for the flow
   */
  uint64_t GetTargetDelay(double cwndPkts, uint16_t nHops,
                          bool shouldUsePerHopDelay);

  /**
   * \brief Update the message state according to Swift-CC algorithm
   * \param boltHeader The header information for the received control packet
   * \param numBytesAcked The number bytes acknowledged for this cwnd update
   */
  void UpdateCwndAsSwift(BoltHeader const &boltHeader, uint32_t numBytesAcked);

  /**
   * \brief Update the message state per the received BTS frame
   * \param boltHeader The header information for the received BTS frame
   */
  void HandleBts(BoltHeader const &boltHeader);

  /**
   * \brief Determines whether there exists some data packets to retransmit
   * \param rtxOffset The highest transmitted sequence number
   */
  void HandleRtx(uint32_t rtxOffset);

  /**
   * \brief Sends the packets that are within the available window
   */
  void SendDown(void);

  /**
   * \brief Creates and sends down a single packet. Updates the state
   * \param payloadSize The size of the packet that is requestes to be sent
   */
  void SendDownOnePacket(uint32_t payloadSize);

  /**
   * \brief Determines whether to keep the state of this msg for msg aggregation
   * \param shouldKeepForFutureMsg Whether or not to keep the state once idle
   */
  void SetToKeepForFutureMsg(bool shouldKeepForFutureMsg);

  /**
   * \brief Resets some state, so that future messages can easily get added
   */
  void KeepFlowForFutureMsg(void);

 private:
  uint16_t m_txMsgId;      //!< The unique msg ID allocated by the sender
  Ipv4Address m_saddr;     //!< Source IP address of this message
  Ipv4Address m_daddr;     //!< Destination IP address of this message
  uint16_t m_sport;        //!< Source port of this message
  uint16_t m_dport;        //!< Destination port of this message
  Ptr<Ipv4Route> m_route;  //!< Msg route determined by the sender socket
  bool m_keepWhenIdle;  //!< Whether to keep the message state af ther becoming
                        //!< idle for flow aggregation purposes
  bool m_btsEnabled;    //!< True if a BTS packet has been received before
  Ptr<BoltL4Protocol> m_bolt;  //!< the protocol instance itself that
                               //!< creates/sends/receives messages
  Ptr<Packet> m_message;       //!< Store the message until deleivered
                               //!< (Disabled for memery optimization)

  uint32_t m_flowSizeBytes;   //!< Number of bytes this flow occupies
  uint32_t m_curMsgStartSeq;  //!< The sequence number where the current message
                              //!< within this flow started
  std::list<uint32_t>
      m_msgFinSeqList;  //!< List of sequence numbers where each element denotes
                        //!< at which sequence number the next msg will begin

  uint32_t m_ackNo;    //!< Highest sequence number ACKed by the receiver
  uint32_t m_sndNext;  //!< Sequence number to send next
  uint32_t m_sndNextBeforeRtx;  //!< sndNext value before retransmission
  uint32_t m_cwnd;              //!< Congestion window in bytes (default: BDP)
  uint64_t m_rtt;               //!< The latest RTT measurement in nanoseconds
  uint64_t m_wndTimeMarker;     //!< Last time cwnd was decreased
  uint32_t m_nextWndAiSeq;      //!< Next sequence number for AI to cwnd
  bool m_setAiNext;             //!< Signal to set AI bit on the next Tx packet

  Time m_resumeTime;   //!< Time until which the message is paused
  Time m_pauseTime;    //!< Last time the message was paused
  EventId m_sndEvent;  //!< The EventID for the packet transmissions
  EventId m_rtxEvent;  //!< The EventID for the retransmission timeout
  uint32_t m_rtxCnt;   //!< Number of timer RTX expirations without progress
  uint32_t m_highestTransmittedSeqNo;
};

/******************************************************************************/

/**
 * \ingroup bolt
 *
 * \brief Manages the transmission of all BoltOutboundMsg from BoltL4Protocol
 *
 * This class keeps the state necessary for transmisssion of the messages.
 * For every new message that arrives from the applications, this class is
 * responsible for sending the data packets as grants are received.
 *
 */
class BoltSendScheduler : public Object {
 public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);

  BoltSendScheduler(Ptr<BoltL4Protocol> boltL4Protocol);
  ~BoltSendScheduler(void);

  /**
   * \brief Accept a new msg from the upper layers and append to pending msgs
   * \param message The message to send
   * \param saddr The source Ipv4Address
   * \param daddr The destination Ipv4Address
   * \param sport The source port number
   * \param dport The destination port number
   * \param route The route requested by the sender
   * \return The txMsgId allocated for this message
   */
  uint16_t ScheduleNewMsg(Ptr<Packet> message, Ipv4Address saddr,
                          Ipv4Address daddr, uint16_t sport, uint16_t dport,
                          Ptr<Ipv4Route> route);

  /**
   * \brief Allocate a new txMsgId for the given message and add to pending msgs
   * \param message The message to send
   * \param saddr The source Ipv4Address
   * \param daddr The destination Ipv4Address
   * \param sport The source port number
   * \param dport The destination port number
   * \param route The route requested by the sender
   * \return The txMsgId allocated for this message
   */
  uint16_t AllocateNewMsg(Ptr<Packet> message, Ipv4Address saddr,
                          Ipv4Address daddr, uint16_t sport, uint16_t dport,
                          Ptr<Ipv4Route> route);

  /**
   * \brief Try to find the msg of the provided tuple among the pending
   * flows. \param saddr The source IPv4 address of the flow. \param daddr
   * The destination IPv4 address of the flow. \param sport The source port
   * of the flow. \param dport The destination port of the flow. \param
   * txMsgId The msg ID if it exists. (set within the function) \return
   * Whether the corresponding flow was found among the pending msgs.
   */
  bool FlowExistsForTuple(Ipv4Address saddr, Ipv4Address daddr, uint16_t sport,
                          uint16_t dport, uint16_t &txMsgId);

  /**
   * \brief Handle the the received control packet for an outbound message
   * \param ipv4Header The Ipv4 header of the received GRANT.
   * \param boltHeader The Bolt header of the received GRANT.
   */
  void CtrlPktRecvdForOutboundMsg(Ipv4Header const &ipv4Header,
                                  BoltHeader const &boltHeader);

  /**
   * \brief Delete the state for a msg
   * \param txMsgId The TX msg ID of the message to be cleared
   */
  void ClearStateForMsg(uint16_t txMsgId);

 private:
  Ptr<BoltL4Protocol> m_bolt;  //!< the protocol instance itself that
                               //!< sends/receives messages

  uint16_t m_txMsgCounter;  //!< Number of messages sent (used for choosing ID)
  std::unordered_map<uint16_t, Ptr<BoltOutboundMsg>>
      m_outboundMsgs;  //!< active BoltOutboundMsg list with the key as txMsgId
};

/******************************************************************************/

/**
 * \ingroup bolt
 * \brief Stores the state for an inbound Bolt message
 */
class BoltInboundMsg : public Object {
 public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);

  BoltInboundMsg(uint16_t rxMsgId, Ipv4Header const &ipv4Header,
                 BoltHeader const &boltHeader, Ptr<Ipv4Interface> iface,
                 Ptr<BoltL4Protocol> bolt);
  ~BoltInboundMsg(void);

  /**
   * \brief Get the sender's IP address for this message.
   * \return The IPv4 address of the sender
   */
  Ipv4Address GetSrcAddress(void);
  /**
   * \brief Get the receiver's IP address for this message.
   * \return The IPv4 address of the receiver
   */
  Ipv4Address GetDstAddress(void);
  /**
   * \brief Get the sender's port number for this message.
   * \return The port number of the sender
   */
  uint16_t GetSrcPort(void);
  /**
   * \brief Get the receiver's port number for this message.
   * \return The port number of the receiver
   */
  uint16_t GetDstPort(void);
  /**
   * \brief Get the TX msg ID for this message.
   * \return The TX msg ID determined the sender
   */
  uint16_t GetTxMsgId(void);

  /**
   * \brief Get the Ipv4Header of the first arrived packet of the message
   * \return The IPv4 header associated with the message
   */
  Ipv4Header GetIpv4Header(void);
  /**
   * \brief Get the interface from which the message arrives.
   * \return The interface from which the message arrives
   */
  Ptr<Ipv4Interface> GetIpv4Interface(void);

  /**
   * \brief Gets the most recent timeout event for this message
   * \return The most recent retransmission event
   */
  EventId GetTimeoutEvent(void);

  /**
   * \brief Determines whether the message continues to receive new packets
   * \param oldExpectedSegment The current nextExpected segment
   */
  void HandleTimeout(uint32_t oldExpectedSegment);

  /**
   * \brief Insert the received data packet in the buffer and update state
   * \param p The received data packet
   * \param ipv4Header The IPv4 header of the received packet
   * \param boltHeader The Bolt header of the received packet
   */
  void ReceiveDataPacket(Ptr<Packet> p, Ipv4Header const &ipv4Header,
                         BoltHeader const &boltHeader);

  /**
   * \brief Sends an Ack with the most recent message state
   * \param ttl The ttl value of the received packet to reflect hop count
   * \param boltHeader The header information for the received packet
   */
  void SendAck(uint8_t ttl, BoltHeader const &boltHeader);

  /**
   * \brief Forwards the completed message to the upper layers
   * \param msgSizeBytes Size of the message that should be forwarded up
   */
  void ForwardUp(uint32_t msgSizeBytes);

  /**
   * \brief Handle the chopped packet
   * \param ttl The ttl value of the received packet to reflect hop count
   * \param boltHeader The header information for the received trimmed packet
   */
  void HandleChop(uint8_t ttl, BoltHeader const &boltHeader);

  /**
   * \brief Sends a Nack with the most recent message state
   * \param ttl The ttl value of the received packet to reflect hop count
   * \param boltHeader The header information for the received packet
   * \param nackNo The sequence number that the rtx is requested for
   */
  void SendNack(uint8_t ttl, BoltHeader const &boltHeader, uint32_t nackNo);

 private:
  Ipv4Header m_ipv4Header;     //!< The IPv4 Header of the first packet arrived
                               //!< for this message
  Ptr<Ipv4Interface> m_iface;  //!< The interface from which the message
                               //!< first came in from
  Ptr<BoltL4Protocol> m_bolt;  //!< the protocol instance itself that
                               //!< creates/sends/receives messages

  uint16_t m_sport;    //!< Source port of this message
  uint16_t m_dport;    //!< Destination port of this message
  uint16_t m_txMsgId;  //!< TX msg ID of the message determined by the sender
  uint16_t m_rxMsgId;  //!< RX msg ID of the message determined by the receiver

  Ptr<Packet> m_message;  //!< Packet buffer for the message

  uint32_t m_nextExpected;  //!< Next expected sequence number

  EventId m_timeOutEvent;  //!< The EventID for the retransmission timeout
  uint16_t m_nTimeOutWithoutProgress;  //!< The number of rtx timeouts without
                                       //!< receiving any new packet
};

/******************************************************************************/

/**
 * \ingroup bolt
 *
 * \brief Manages the arrival of all BoltInboundMsg from BoltL4Protocol
 *
 * This class keeps the state necessary for arrival of the messages.
 * For every new message that arrives from the network, this class is
 * responsible for scheduling the messages and sending the control packets.
 *
 */
class BoltRecvScheduler : public Object {
 public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);

  BoltRecvScheduler(Ptr<BoltL4Protocol> boltL4Protocol);
  ~BoltRecvScheduler(void);

  /**
   * \brief Notify this BoltRecvScheduler upon arrival of a packet
   * \param packet The received packet (without any headers)
   * \param ipv4Header IPv4 header of the received packet
   * \param boltHeader The Bolt header of the received packet
   * \param interface The interface from which the packet came in
   */
  void ReceivePacket(Ptr<Packet> packet, Ipv4Header const &ipv4Header,
                     BoltHeader const &boltHeader,
                     Ptr<Ipv4Interface> interface);

  /**
   * \brief Try to find the msg of the provided headers among the pending msg.
   * \param ipv4Header IPv4 header of the received packet.
   * \param boltHeader The Bolt header of the received packet.
   * \param rxMsgId The msg ID if it is active. (set within the function)
   * \return Whether the corresponding msg was found among the pending msgs.
   */
  bool GetInboundMsg(Ipv4Header const &ipv4Header, BoltHeader const &boltHeader,
                     uint16_t &rxMsgId);

  /**
   * \brief Cancel timer of the given message and remove from the list
   * \param rxMsgId The locally unique ID for the msg to be removed
   */
  void ClearStateForMsg(uint16_t rxMsgId);

 private:
  Ptr<BoltL4Protocol> m_bolt;  //!< the protocol instance itself that
                               //!< sends/receives messages

  uint16_t m_rxMsgCounter;  //!< Number of msgs received (used for choosing ID)
  std::unordered_map<uint16_t, Ptr<BoltInboundMsg>>
      m_inboundMsgs;  //!< active BoltInboundMsg list with the key as rxMsgId
};

}  // namespace ns3

#endif /* BOLT_L4_PROTOCOL_H */
