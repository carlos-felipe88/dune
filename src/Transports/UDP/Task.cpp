//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

// ISO C++ 98 headers.
#include <vector>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstddef>

// DUNE headers.
#include <DUNE/DUNE.hpp>

// Local headers.
#include "NodeAddress.hpp"
#include "NodeTable.hpp"
#include "Listener.hpp"
#include "LimitedComms.hpp"

namespace Transports
{
  namespace UDP
  {
    using DUNE_NAMESPACES;

    struct RateInfo
    {
      // Interval between sending the same message (s).
      double rate;
      // Last time a given message type was sent.
      double last;
    };

    struct Arguments
    {
      // Contact timeout.
      float contact_timeout;
      // Contact refresh periodicity.
      float contact_refresh_per;
      // Local UDP port.
      unsigned port;
      // Static destinations.
      std::vector<std::string> destinations;
      // Trace incoming messages.
      bool trace_in;
      // Trace outgoing messages.
      bool trace_out;
      // Rate limits.
      std::vector<std::string> rate_lims;
      // List of messages to publish.
      std::vector<std::string> messages;
      // Announce this transport to services or not
      bool announce_service;
      // Simulated Communications Range
      float comm_range;
      // Block packets sent/received underwater
      bool underwater_comms;
      // Messages that will always be transmitted, disregarding comm limitations
      std::vector<std::string> allowed_messages;
    };

    // Internal buffer size.
    static const int c_bfr_size = 65535;
    // Port bind retries.
    static const int c_port_retries = 5;

    struct Task: public DUNE::Tasks::Task
    {
      // Serialization buffer.
      uint8_t* m_bfr;
      // UDP Socket.
      UDPSocket m_sock;
      // Set of static nodes.
      std::set<NodeAddress> m_static_dsts;
      // Message rate information.
      std::map<uint32_t, double> m_rates_per_id;
      std::map<uint32_t, RateInfo> m_rates;
      // Set of destination nodes.
      NodeTable m_node_table;
      // Task arguments.
      Arguments m_args;
      // Simulate communication limitations
      bool m_comm_limitations;
      // Allow underwater communications when simulating limited comms
      bool m_underwater_comms;
      // Listener thread.
      Listener* m_listener;
      // Contact refresh counter.
      Time::Counter<float> m_contacts_refresh_counter;
      // LimitedComms object
      LimitedComms* m_lcomms;

      Task(const std::string& name, Tasks::Context& ctx):
        DUNE::Tasks::Task(name, ctx),
        m_bfr(NULL),
        m_listener(NULL),
        m_lcomms(NULL)
      {
        param("Local Port", m_args.port)
        .defaultValue("6002")
        .description("Local UDP port to listen on");

        param("Contact Timeout", m_args.contact_timeout)
        .units(Units::Second)
        .defaultValue("30");

        param("Contact Refresh Periodicity", m_args.contact_refresh_per)
        .units(Units::Second)
        .defaultValue("5.0");

        param("Print Outgoing Messages", m_args.trace_out)
        .defaultValue("false")
        .description("Print outgoing messages (Debug)");

        param("Print Incoming Messages", m_args.trace_in)
        .defaultValue("false")
        .description("Print incoming messages (Debug)");

        param("Static Destinations", m_args.destinations)
        .description("List of <IPv4>:<Port> destinations that will always receive outgoing messages");

        param("Rate Limiters", m_args.rate_lims)
        .description("List of <Message>:<Frequency>");

        param("Announce Service", m_args.announce_service)
        .defaultValue("true")
        .description("Announce this transport to services or not");

        param("Communication Range", m_args.comm_range)
        .defaultValue("0")
        .units(Units::Meter)
        .description("Communication range (0 for infinite)");

        param("Underwater Communications", m_args.underwater_comms)
        .defaultValue("false")
        .description("Allow underwater communication when limited comms are being simulated");

        param("Transports", m_args.messages)
        .defaultValue("")
        .description("List of messages to transport");

        param("Always Transmitted Messages", m_args.allowed_messages)
        .defaultValue("")
        .description("List of messages that will always be transmitted disregarding communication limitations");

        // Allocate space for internal buffer.
        m_bfr = new uint8_t[c_bfr_size];

        // Register listeners.
        bind<IMC::Announce>(this);
      }

      ~Task(void)
      {
        if (m_bfr != NULL)
          delete[] m_bfr;
      }

      void
      onUpdateParameters(void)
      {
        m_contacts_refresh_counter.setTop(m_args.contact_refresh_per);

        // Initialize set of static destinations.
        m_static_dsts.clear();
        for (unsigned int i = 0; i < m_args.destinations.size(); ++i)
          m_static_dsts.insert(NodeAddress(m_args.destinations[i]));

        // Process rate limiters.
        m_rates_per_id.clear();
        m_rates.clear();
        for (unsigned int i = 0; i < m_args.rate_lims.size(); ++i)
        {
          std::vector<std::string> parts;
          String::split(m_args.rate_lims[i], ":", parts);
          if (parts.size() != 2)
            continue;

          uint32_t id = IMC::Factory::getIdFromAbbrev(parts[0]);
          float rate = 0;
          std::sscanf(parts[1].c_str(), "%f", &rate);
          m_rates_per_id[id] = 1.0 / rate;
        }

        m_underwater_comms = m_args.underwater_comms;

        // Initialize communication limitations parameters.
        if (m_ctx.profiles.isSelected("Simulation") && m_args.comm_range > 0)
        {
          debug("simulating limited radio communications with maximum communication range of %f m",
                m_args.comm_range);
          debug("underwater communications are %s",
                m_underwater_comms ? "active" : "inactive");
          m_comm_limitations = true;
        }
        else
        {
          debug("limited communications simulation is not active");
          m_comm_limitations = false;
        }

        // Register normal messages.
        bind(this, m_args.messages);
      }

      void
      onResourceAcquisition(void)
      {
        // Find a free port.
        unsigned port_limit = m_args.port + c_port_retries;
        while (m_args.port != port_limit)
        {
          try
          {
            m_sock.bind(m_args.port, Address::Any, false);
            break;
          }
          catch (std::runtime_error& e)
          {
            war("failed to bind to port %u: %s", m_args.port, e.what());
            ++m_args.port;
          }
        }

        if (m_args.port == port_limit)
        {
          err("could not bind to a port");
          throw std::runtime_error("could not bind to a port");
        }

        inf(DTR("listening on port %u"), m_args.port);

        if (m_args.announce_service)
        {
          // Initialize and dispatch AnnounceService.
          std::vector<Interface> itfs = Interface::get();
          for (unsigned i = 0; i < itfs.size(); ++i)
          {
            std::stringstream os;
            os << "imc+udp://" << itfs[i].address().str() << ":" << m_args.port
               << "/";

            IMC::AnnounceService announce;
            announce.service = os.str();

            if (itfs[i].address().isLoopback())
              announce.service_type = IMC::AnnounceService::SRV_TYPE_LOCAL;
            else
              announce.service_type = IMC::AnnounceService::SRV_TYPE_EXTERNAL;

            dispatch(announce);
          }
        }

        // Initialize limited comms object
        m_lcomms = new LimitedComms(m_args.comm_range, getSystemId());
        m_lcomms->setActive(m_comm_limitations);
        m_node_table.setLimitedComms(m_lcomms);

        // Start listener thread.
        m_listener = new Listener(*this, m_sock, m_lcomms,
                                  m_args.contact_timeout, m_args.trace_in);
        m_listener->start();

        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
      }

      void
      onResourceRelease(void)
      {
        if (m_listener != NULL)
        {
          m_listener->stopAndJoin();
          delete m_listener;
          m_listener = NULL;
        }

        Memory::clear(m_lcomms);
      }

      void
      consume(const IMC::Message* msg)
      {
        if (m_lcomms->isActive())
        {
          if (msg->getId() == DUNE_IMC_ESTIMATEDSTATE)
            m_lcomms->setMyEstimatedState(static_cast<const IMC::EstimatedState*>(msg));
        }

        if (m_node_table.getActiveCount() == 0 && m_static_dsts.size() == 0)
          return;

        uint32_t key = (msg->getId() << 16) + (msg->getSourceEntity() << 8) + (msg->getSubId() & 0xff);

        if (m_rates.find(key) == m_rates.end())
        {
          m_rates[key].rate = m_rates_per_id[msg->getId()];
          m_rates[key].last = 0.0;
        }
        else
        {
          double now = Clock::get();
          if (m_rates[key].last + m_rates[key].rate >= now)
            return;
          m_rates[key].last = now;
        }

        if (m_args.trace_out)
          msg->toText(std::cerr);

        uint16_t rv = IMC::Packet::serialize(msg, m_bfr, c_bfr_size);

        // Send to static nodes.
        std::set<NodeAddress>::iterator itr = m_static_dsts.begin();
        for (; itr != m_static_dsts.end(); ++itr)
        {
          try
          {
            m_sock.write((char*)m_bfr, rv, itr->getAddress(), itr->getPort());
          }
          catch (...)
          { }
        }

        // Send to dynamic nodes.
        m_node_table.send(m_sock, m_bfr, rv, msg->getId());
      }

      void
      consume(const IMC::Announce* msg)
      {
        m_node_table.addNode(msg->getSource(), msg->sys_name, msg->services);
        m_lcomms->setAnnounce(msg);
      }

      void
      refreshContacts(void)
      {
        m_listener->lockContacts();

        std::vector<Contact> contacts;
        m_listener->getContacts(contacts);
        std::vector<Contact>::iterator itr = contacts.begin();

        for (; itr != contacts.end(); ++itr)
        {
          std::string name = resolveSystemId(itr->getId());

          if (itr->isActive())
          {
            if (m_node_table.activate(itr->getId(), itr->getAddress()))
              inf(DTR("activating transmission to node '%s'"), name.c_str());
          }
          else
          {
            if (m_node_table.deactivate(itr->getId(), itr->getAddress()))
              inf(DTR("deactivating transmission to node '%s'"), name.c_str());
          }
        }

        m_listener->unlockContacts();
      }

      void
      onMain(void)
      {
        while (!stopping())
        {
          waitForMessages(1.0);

          // Check if it's time to update the contact list.
          if (m_contacts_refresh_counter.overflow())
          {
            refreshContacts();
            m_contacts_refresh_counter.reset();
          }
        }
      }
    };
  }
}

DUNE_TASK
