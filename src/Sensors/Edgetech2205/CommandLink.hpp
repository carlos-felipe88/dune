//***************************************************************************
// Copyright 2007-2013 Universidade do Porto - Faculdade de Engenharia      *
// Laboratório de Sistemas e Tecnologia Subaquática (LSTS)                  *
//***************************************************************************
// This file is part of DUNE: Unified Navigation Environment.               *
//                                                                          *
// Commercial Licence Usage                                                 *
// Licencees holding valid commercial DUNE licences may use this file in    *
// accordance with the commercial licence agreement provided with the       *
// Software or, alternatively, in accordance with the terms contained in a  *
// written agreement between you and Universidade do Porto. For licensing   *
// terms, conditions, and further information contact lsts@fe.up.pt.        *
//                                                                          *
// European Union Public Licence - EUPL v.1.1 Usage                         *
// Alternatively, this file may be used under the terms of the EUPL,        *
// Version 1.1 only (the "Licence"), appearing in the file LICENCE.md       *
// included in the packaging of this file. You may not use this work        *
// except in compliance with the Licence. Unless required by applicable     *
// law or agreed to in writing, software distributed under the Licence is   *
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF     *
// ANY KIND, either express or implied. See the Licence for the specific    *
// language governing permissions and limitations at                        *
// https://www.lsts.pt/dune/licence.                                        *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

#ifndef SENSORS_EDGETECH_2205_COMMAND_LINK_HPP_INCLUDED_
#define SENSORS_EDGETECH_2205_COMMAND_LINK_HPP_INCLUDED_

// DUNE headers.
#include <DUNE/DUNE.hpp>

// Local headers.
#include "Parser.hpp"

namespace Sensors
{
  namespace Edgetech2205
  {
    using DUNE_NAMESPACES;

    class CommandLink
    {
    public:
      CommandLink(const Address& addr, unsigned port):
        m_avg_time_diff(0)
      {
        m_sock.setNoDelay(true);
        m_sock.setReceiveTimeout(5);
        m_sock.setSendTimeout(5);
        m_sock.connect(addr, port);
        m_sock.addToPoll(m_iom);
        m_bfr.resize(c_max_size);
      }

      int64_t
      getTimeDifference(void)
      {
        m_pkt.setMessageType(MSG_ID_SYSTEM_TIME);
        m_pkt.setSubsystemNumber(0);
        m_pkt.setChannel(0);
        m_pkt.setCommandType(COMMAND_TYPE_GET);
        m_pkt.setMessageSize(0);

        int64_t send_time = Clock::getSinceEpochMsec();
        sendPacket(m_pkt);

        const Packet* reply = read(MSG_ID_SYSTEM_TIME, COMMAND_TYPE_REPLY, 0, 0, 1.0);
        if (reply == NULL)
          throw std::runtime_error("failed to get time");

        int64_t recv_time = Clock::getSinceEpochMsec();
        int64_t rtt = static_cast<int64_t>((recv_time - send_time) / 2.0);

        const uint8_t* data = reply->getMessageData();
        uint32_t sec = 0;
        ByteCopy::fromLE(sec, data);
        uint32_t msec = 0;
        ByteCopy::fromLE(msec, data + 4);

        int64_t remote_time = sec;
        remote_time *= 1000;
        remote_time += msec;

        return (remote_time - (send_time + rtt));
      }

      void
      shutdown(void)
      {
        m_pkt.setMessageType(25);
        m_pkt.setSubsystemNumber(0);
        m_pkt.setChannel(0);
        m_pkt.setCommandType(COMMAND_TYPE_SET);
        m_pkt.setValue(0);
        sendPacket(m_pkt);
      }

      void
      setPingAutoselectMode(SubsystemId subsys, uint32_t mode)
      {
        m_pkt.setMessageType(MSG_ID_PING_AUTOSEL_MODE);
        m_pkt.setSubsystemNumber(subsys);
        m_pkt.setChannel(0);
        m_pkt.setCommandType(COMMAND_TYPE_SET);
        m_pkt.setValue(mode);
        sendPacket(m_pkt);
      }

      void
      setDataActive(SubsystemId subsys, unsigned channel, uint32_t mode)
      {
        m_pkt.setMessageType(MSG_ID_DATA_ACTIVE);
        m_pkt.setSubsystemNumber(subsys);
        m_pkt.setChannel(channel);
        m_pkt.setCommandType(COMMAND_TYPE_SET);
        m_pkt.setValue(mode);
        sendPacket(m_pkt);
      }

      void
      setPingRange(SubsystemId subsys, uint32_t range)
      {
        // Convert to millimeter.
        range *= 1000;

        m_pkt.setMessageType(MSG_ID_PING_RANGE);
        m_pkt.setCommandType(COMMAND_TYPE_SET);
        m_pkt.setSubsystemNumber(subsys);
        m_pkt.setChannel(0);
        m_pkt.setValue(range);
        sendPacket(m_pkt);

        uint32_t reply_range = getPingRange(subsys);
        if (reply_range != range)
        {
          throw std::runtime_error("range mismatch");
        }
      }

      unsigned
      getPingRange(SubsystemId subsys)
      {
        m_pkt.setMessageType(MSG_ID_PING_RANGE);
        m_pkt.setCommandType(COMMAND_TYPE_GET);
        m_pkt.setSubsystemNumber(subsys);
        m_pkt.setChannel(0);
        m_pkt.setMessageSize(0);
        sendPacket(m_pkt);

        const Packet* reply = read(MSG_ID_PING_RANGE, COMMAND_TYPE_REPLY, subsys, 0, 1.0);

        if (reply == NULL)
          throw std::runtime_error("failed to set range");

        return reply->getValue();
      }

      void
      setPingTrigger(SubsystemId subsys, uint32_t value)
      {
        m_pkt.setMessageType(MSG_ID_PING_TRIGGER);
        m_pkt.setCommandType(COMMAND_TYPE_SET);
        m_pkt.setSubsystemNumber(subsys);
        m_pkt.setChannel(0);
        m_pkt.setValue(value);
        sendPacket(m_pkt);
      }

      void
      setPing(SubsystemId subsys, uint32_t value)
      {
        m_pkt.setMessageType(MSG_ID_PING);
        m_pkt.setSubsystemNumber(subsys);
        m_pkt.setChannel(0);
        m_pkt.setCommandType(COMMAND_TYPE_SET);
        m_pkt.setValue(value);
        sendPacket(m_pkt);
      }

      int64_t
      estimateTimeDifference(void)
      {
        int64_t delta = 0;

        for (unsigned i = 0; i < c_time_diff_sample_count; ++i)
          delta += getTimeDifference();

        return delta / c_time_diff_sample_count;
      }

    private:
      //! Maximum packet size.
      static const unsigned c_max_size = 4096;
      //! Time difference estimation: samples per average.
      static const unsigned c_time_diff_sample_count = 10;
      //! TCP socket.
      TCPSocket m_sock;
      //! I/O multiplexer.
      IOMultiplexing m_iom;
      //! Parser.
      Parser m_parser;
      //! Packet.
      Packet m_pkt;
      //! Read buffer.
      std::vector<uint8_t> m_bfr;
      //! Average time difference between CPU and sidescan.
      int64_t m_avg_time_diff;

      void
      sendPacket(const Packet& pkt)
      {
        m_sock.write((const char*)pkt.getData(), pkt.getSize());
      }

      const Packet*
      read(uint16_t msg_id, uint8_t cmd_type, uint8_t subsys, uint8_t channel, double timeout = 1.0)
      {
        Counter<double> timer(timeout);

        while (!timer.overflow())
        {
          if (m_iom.poll(timer.getRemaining()))
          {
            if (!m_sock.wasTriggered(m_iom))
              continue;

            int rv = m_sock.read((char*)&m_bfr[0], m_bfr.size());
            for (int i = 0; i < rv; ++i)
            {
              if (!m_parser.parse(m_bfr[i]))
                continue;

              const Packet* pkt = m_parser.getPacket();

              if ((pkt->getMessageType() == msg_id)
                  && (pkt->getCommandType() == cmd_type)
                  && (pkt->getSubsystemNumber() == subsys)
                  && (pkt->getChannel() == channel))
              {
                return pkt;
              }
            }
          }
        }

        return NULL;
      }
    };
  }
}

#endif
