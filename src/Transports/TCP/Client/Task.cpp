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
// Author: Eduardo Marques                                                  *
//***************************************************************************

// ISO C++ 98 headers.
#include <cstddef>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Transports
{
  namespace TCP
  {
    namespace Client
    {
      using DUNE_NAMESPACES;

      struct Arguments
      {
        Address address; // Server address.
        int port; // Server port.
      };

      struct Task: public Tasks::SimpleTransport
      {
        // Task arguments.
        Arguments m_args;
        // Socket handle.
        TCPSocket* m_sock;
        // I/O multiplexer.
        IOMultiplexing* m_iom;
        // Parser handle.
        IMC::Parser m_parser;

        Task(const std::string& name, Tasks::Context& ctx):
          Tasks::SimpleTransport(name, ctx),
          m_sock(NULL),
          m_iom(NULL)
        {
          param("Server - Address", m_args.address)
          .defaultValue("127.0.0.1")
          .description("Remote server address");

          param("Server - Port", m_args.port)
          .defaultValue("7001")
          .description("Remote server port");
        }

        ~Task(void)
        {
          onResourceRelease();
        }

        void
        onResourceAcquisition(void)
        {
          try
          {
            m_sock = new TCPSocket;
            m_sock->connect(m_args.address, m_args.port);
            m_sock->setKeepAlive(true);

            m_iom = new IOMultiplexing;
            m_sock->addToPoll(*m_iom);
            inf("connected to %s:%u", m_args.address.c_str(), m_args.port);
          }
          catch (std::runtime_error& e)
          {
            throw RestartNeeded(e.what(), 5);
          }
        }

        void
        onResourceRelease(void)
        {
          if (m_iom)
          {
            delete m_iom;
            m_iom = NULL;
          }

          if (m_sock)
          {
            delete m_sock;
            m_sock = NULL;
          }

          m_parser.reset();
        }

        void
        onDataTransmission(const uint8_t* p, unsigned int len)
        {
          try
          {
            m_sock->write((const char*)p, len);
          }
          catch (std::exception& e)
          {
            throw RestartNeeded(e.what(), 5);
          }
        }

        void
        onDataReception(uint8_t* p, unsigned int n, double timeout)
        {
          if (!m_iom->poll(timeout))
            return;

          int n_r;
          try
          {
            n_r = m_sock->read((char*)p, n);
          }
          catch (std::runtime_error& e)
          {
            err("receive error: %s", e.what());
            return;
          }

          if (n_r > 0)
            handleData(m_parser, p, n_r);
        }
      };
    }
  }
}

DUNE_TASK
