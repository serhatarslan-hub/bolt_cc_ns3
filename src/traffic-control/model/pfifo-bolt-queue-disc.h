/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#ifndef PFIFO_BOLT_H
#define PFIFO_BOLT_H

#include "ns3/bolt-header-ptr.h"
#include "ns3/bolt-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue-disc.h"

namespace ns3 {

/**
 * \ingroup traffic-control
 *
 * The pfifo_bolt is basically the prototype model of the switch program for
 * the BoltL4Protocol. It mainly mimics pfifo_fast of linux, but adds some
 * packet processing logic such as congestion signalling and return to sender
 * capabilities.
 *
 */
class PfifoBoltQueueDisc : public QueueDisc {
 public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId(void);
  /**
   * \brief PfifoBoltQueueDisc constructor
   *
   * Creates a queue with a depth of 1000 packets per band by default
   */
  PfifoBoltQueueDisc();

  virtual ~PfifoBoltQueueDisc();

  /**
   * \brief Computes the currently available bandwidth to measure congestion
   * \param pktSize The size of the arriving packet
   */
  void CalculateCurrentlyAvailableBw(uint32_t pktSize);

  /**
   * \brief Trims the given packet and updates the header accordingly
   * \param p The original data packet to be trimmed
   * \param boltHeader The Bolt header of the packet for update
   */
  void TrimPacketPayload(Ptr<Packet> p, BoltHeaderPtr *boltHeader);

  /**
   * \brief Creates a Back To Sender packet and sends it
   * \param ipv4h IPv4 header of the original packet
   * \param bolth Bolt header of the original packet
   */
  void NotifySender(Ipv4Header ipv4h, BoltHeaderPtr *bolth);

  /**
   * \brief Creates an artificial BTS and schedules to send with a certain delay
   * \param ipv4h IPv4 header of the original packet
   * \param bolth Bolt header of the original packet
   */
  void BtsWithArtificialDelay(Ipv4Header ipv4h, BoltHeaderPtr *bolth);
  /**
   * \brief Sends the artificial BTS directly to the sender
   * \param srcPort Used to determine the sender
   * \param p The packet to be sent to the sender
   * \param ipv4h IPv4 header of the original packet
   */
  void SendArtificialBts(uint16_t srcPort, Ptr<Packet> p, Ipv4Header newIpv4h);

  /**
   * \brief Updates the Fastest Flow (ff) state for this qdisc
   * \param srcAddr The source IPv4 address of the new ff
   * \param dstAddr The destination IPv4 address of the new ff
   * \param srcPort The source port of the new ff
   * \param dstPort The destination port of the new ff
   * \param flowRate The current transmission rate of the new ff
   */
  void UpdateFfState(Ipv4Address srcAddr, Ipv4Address dstAddr, uint16_t srcPort,
                     uint16_t dstPort, uint32_t flowRate);

  /**
   * \brief Resets any bitrate flag and marks only the bitrate of boundNetDevice
   * \param curFlag The current set of flag values on the packet
   * \return The new set of flag values to be marked on the packet
   */
  uint16_t SetBitRateFlag(uint16_t curFlag);

  // Reasons for dropping packets
  static constexpr const char *LIMIT_EXCEEDED_DROP =
      "Queue disc limit exceeded";  //!< Packet dropped due to queue
                                    //!< overflow

 private:
  /**
   * Priority to band map. Values are taken from the prio2band array used by
   * the Linux pfifo_fast queue disc.
   */
  static const uint32_t prio2band[16];

  virtual bool DoEnqueue(Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue(void);
  virtual Ptr<const QueueDiscItem> DoPeek(void);
  virtual bool CheckConfig(void);
  virtual void InitializeParams(void);

  Ptr<PointToPointNetDevice>
      m_boundNetDevice;  // The net device which this queue disc is connected to

  QueueSize m_ccThreshold;  //!< Queue occupancy threshold for CC reaction
  int m_maxInstAvailLoad;   //!< Maximum amount of bytes allowed to be
                            //!< accumulated to measure available bandwidth
  TracedValue<int>
      m_availLoad;  //!< Currently available bandwidth in bytes. Congested if
                    //!< negative. Periodically increases & decreased on pkt RX
  uint64_t m_availLoadUpdateTime;  //!< Last time availLoad was updated (in ns)
  bool m_absEnabled;   //!< Flag to denote Available Bandwidth Signaling enabled
  bool m_btsEnabled;   //!< Flag to denote wheter Back To Sender is enabled
  bool m_trimEnabled;  //!< Flag to denote wheter payload trimming is enabled
  bool m_pruEnabled;   //!< Flag to enable Proactive Ramp-Up
  TracedValue<uint16_t> m_nPruToken;  //!< Number of available packet slots in
                                      //!< the future
  uint16_t m_maxPruToken;  //!< Maximum number of packet slots for the future

  uint32_t m_nBtsInFlight;  //!< Number of BTS packets that are still in flight
  uint32_t m_reducedBtsFactor;  //!< Period of BTS transmission during
                                //!< congestion, ie. once every x data packets

  TracedCallback<uint32_t, uint32_t> m_btsSendTrace;  //!< Trace of nBtsInFlight
                                                      //!< and queue occupancy
                                                      //!< at every BTS event
};

}  // namespace ns3

#endif /* PFIFO_BOLT_H */
