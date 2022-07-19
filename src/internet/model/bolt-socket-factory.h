/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Google LLC
 *
 * All rights reserved.
 *
 * Author: Serhat Arslan <serhatarslan@google.com>
 */

#ifndef BOLT_SOCKET_FACTORY_H
#define BOLT_SOCKET_FACTORY_H

#include "ns3/socket-factory.h"
#include "ns3/ptr.h"

namespace ns3 {

class Socket;
class BoltL4Protocol;

/**
 * \ingroup socket
 * \ingroup bolt
 *
 * \brief API to create BOLT socket instances
 *
 * This abstract class defines the API for BOLT socket factory.
 * All BOLT implementations must provide an implementation of CreateSocket
 * below.
 *
 */
class BoltSocketFactory : public SocketFactory
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  BoltSocketFactory ();
  virtual ~BoltSocketFactory ();

  /**
   * \brief Set the associated BOLT L4 protocol.
   * \param bolt the BOLT L4 protocol
   */
  void SetBolt (Ptr<BoltL4Protocol> bolt);

  /**
   * \brief Implements a method to create a Bolt-based socket and return
   * a base class smart pointer to the socket.
   *
   * \return smart pointer to Socket
   */
  virtual Ptr<Socket> CreateSocket (void);

protected:
  virtual void DoDispose (void);
private:
  Ptr<BoltL4Protocol> m_bolt; //!< the associated BOLT L4 protocol
};

} // namespace ns3

#endif /* BOLT_SOCKET_FACTORY_H */
