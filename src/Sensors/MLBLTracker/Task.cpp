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
//
// The format of a quick tracking message is:
// +----+----+----+----+----+----+----+----+----+----+----+----+----+
// | 0  |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  8 | 9  | 10 | 11 | 12 |
// +----+----+----+----+----+----+----+----+----+----+----+----+----+
// |                   Range                         | Beacon  |  1 |
// +----+----+----+----+----+----+----+----+----+----+----+----+----+
//

// ISO C++ 98 headers.
#include <vector>
#include <memory>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Sensors
{
  namespace MLBLTracker
  {
    using DUNE_NAMESPACES;

    enum Operation
    {
      // No operation is in progress.
      OP_NONE,
      // Narrow band pinging in progress.
      OP_PING_NB,
      // Micro-Modem pinging in progress.
      OP_PING_MM,
      // Abort in progress.
      OP_ABORT
    };

    // Narrow band transponder.
    struct Transponder
    {
      // Query frequency.
      unsigned query_freq;
      // Reply frequency.
      unsigned reply_freq;
      // Abort frequency.
      unsigned abort_freq;

      Transponder(unsigned q, unsigned r, unsigned a):
        query_freq(q),
        reply_freq(r),
        abort_freq(a)
      { }
    };

    // Task arguments.
    struct Arguments
    {
      // UART device.
      std::string uart_dev;
      // UART baud rate.
      unsigned uart_baud;
      // Sound speed on water.
      double sspeed;
      // Narrow band ping timeout.
      double tout_nbping;
      // Micro-Modem ping timeout.
      double tout_mmping;
      // Abort timeout.
      double tout_abort;
      // Input timeout.
      double tout_input;
      // GPIO to detect if transducer is connected.
      int gpio_txd;
      // Length of transmit pings.
      unsigned tx_length;
      // Length of receive pings.
      unsigned rx_length;
    };

    // Type definition for mapping addresses.
    typedef std::map<unsigned, unsigned> AddressMap;
    typedef std::map<std::string, Transponder> NarrowBandMap;
    typedef std::map<std::string, unsigned> MicroModemMap;

    struct Task: public DUNE::Tasks::Task
    {
      // Abort code.
      static const unsigned c_code_abort = 0x000a;
      // Abort acked code.
      static const unsigned c_code_abort_ack = 0x000b;
      // Address used to send change plan messages.
      static const unsigned c_plan_addr = 15;
      // Quick tracking mask.
      static const unsigned c_mask_qtrack = 0x1000;
      // Quick tracking beacon mask.
      static const unsigned c_mask_qtrack_beacon = 0x0c00;
      // Quick tracking range mask.
      static const unsigned c_mask_qtrack_range = 0x03ff;
      // Maximum buffer size.
      static const int c_bfr_size = 256;
      // Serial port handle.
      SerialPort* m_uart;
      // Map of narrow band transponders.
      NarrowBandMap m_nbmap;
      // Map of micro-modem addresses.
      MicroModemMap m_ummap;
      // Map of micro-modem to IMC addresses.
      AddressMap m_mimap;
      // Map of IMC to Micro-Modem addresses.
      AddressMap m_immap;
      // Task arguments.
      Arguments m_args;
      // Timestamp of last operation.
      double m_op_deadline;
      // Local modem-address.
      unsigned m_address;
      // Last acoustic operation.
      IMC::AcousticOperation m_acop;
      // Outgoing acoustic operation.
      IMC::AcousticOperation m_acop_out;
      // Save modem commands.
      IMC::DevDataText m_dev_data;
      // Current operation.
      Operation m_op;
      // Transducer detection GPIO.
      GPIO* m_txd_gpio;
      // Time of last sentence from modem.
      Counter<double> m_last_stn;

      Task(const std::string& name, Tasks::Context& ctx):
        DUNE::Tasks::Task(name, ctx),
        m_uart(NULL),
        m_op_deadline(-1.0),
        m_op(OP_NONE),
        m_txd_gpio(0)
      {
        // Define configuration parameters.
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("")
        .description("Serial port device used to communicate with the sensor");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("19200")
        .description("Serial port baud rate");

        param("Length of Transmit Pings", m_args.tx_length)
        .units(Units::Millisecond)
        .defaultValue("5");

        param("Length of Receive Pings", m_args.rx_length)
        .units(Units::Millisecond)
        .defaultValue("5");

        param("Sound Speed on Water", m_args.sspeed)
        .units(Units::MeterPerSecond)
        .defaultValue("1500");

        param("Timeout - Micro-Modem Ping", m_args.tout_mmping)
        .units(Units::Second)
        .defaultValue("5.0");

        param("Timeout - Narrow Band Ping", m_args.tout_nbping)
        .units(Units::Second)
        .defaultValue("5.0");

        param("Timeout - Abort", m_args.tout_abort)
        .units(Units::Second)
        .defaultValue("5.0");

        param("Timeout - Input", m_args.tout_input)
        .units(Units::Second)
        .defaultValue("20.0");

        param("GPIO - Transducer Detection", m_args.gpio_txd)
        .defaultValue("-1");

        // Process micro-modem addresses.
        std::string agent = getSystemName();
        std::vector<std::string> addrs = ctx.config.options("Micromodem Addresses");
        for (unsigned i = 0; i < addrs.size(); ++i)
        {
          unsigned iid = resolveSystemName(addrs[i]);
          unsigned mid = 0;
          ctx.config.get("Micromodem Addresses", addrs[i], "0", mid);
          m_mimap[mid] = iid;
          m_immap[iid] = mid;
          m_ummap[addrs[i]] = mid;

          if (addrs[i] == agent)
            m_address = mid;
        }

        // Process narrow band transponders.
        std::vector<std::string> txponders = ctx.config.options("Narrow Band Transponders");
        for (unsigned i = 0; i < txponders.size(); ++i)
        {
          std::vector<unsigned> freqs;
          ctx.config.get("Narrow Band Transponders", txponders[i], "", freqs);
          if (freqs.size() == 2)
            freqs.push_back(0);
          m_nbmap.insert(std::make_pair(txponders[i], Transponder(freqs[0], freqs[1], freqs[2])));
        }

        // Register message handlers.
        bind<IMC::AcousticOperation>(this);
      }

      ~Task(void)
      {
        onResourceRelease();
      }

      void
      onUpdateParameters(void)
      {
        // Configure transducer GPIO (if any).
        if (m_args.gpio_txd > 0)
        {
          try
          {
            m_txd_gpio = new GPIO((unsigned)m_args.gpio_txd);
            m_txd_gpio->setDirection(GPIO::GPIO_DIR_INPUT);
          }
          catch (...)
          {
            err(DTR("unable to use GPIO %d for transducer detection"), m_args.gpio_txd);
          }
        }

        // Input timeout.
        m_last_stn.setTop(m_args.tout_input);
      }

      void
      onResourceAcquisition(void)
      {
        m_uart = new SerialPort(m_args.uart_dev.c_str(), m_args.uart_baud);
        m_uart->setCanonicalInput(true);
        m_uart->flush();

        {
          NMEAWriter stn("CCCFG");
          stn << "SRC" << m_address;
          std::string cmd = stn.sentence();
          sendCommand(cmd);
        }

        {
          NMEAWriter stn("CCCFG");
          stn << "XST" << 0;
          std::string cmd = stn.sentence();
          sendCommand(cmd);
        }

        {
          NMEAWriter stn("CCCFG");
          stn << "CTO" << 10;
          std::string cmd = stn.sentence();
          sendCommand(cmd);
        }

        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
      }

      void
      onResourceRelease(void)
      {
        Memory::clear(m_uart);
      }

      void
      onResourceInitialization(void)
      {
        IMC::AnnounceService announce;
        announce.service = std::string("imc+any://acoustic/operation/")
        + URL::encode(getEntityLabel());
        dispatch(announce);
      }

      void
      resetOp(void)
      {
        m_op = OP_NONE;
        m_op_deadline = -1.0;
      }

      bool
      hasTransducer(void)
      {
        if (m_txd_gpio == 0)
          return true;

        if (m_txd_gpio->getValue() == false)
          return true;

        err("%s", DTR("transducer not connected"));
        m_acop_out.op = IMC::AcousticOperation::AOP_NO_TXD;
        dispatch(m_acop_out);
        return false;
      }

      void
      sendCommand(const std::string& cmd)
      {
        inf("%s", sanitize(cmd).c_str());
        m_uart->write(cmd.c_str());
        m_dev_data.value.assign(sanitize(cmd));
        dispatch(m_dev_data);
      }

      void
      consume(const IMC::AcousticOperation* msg)
      {
        if (m_op != OP_NONE)
        {
          m_acop_out.op = IMC::AcousticOperation::AOP_BUSY;
          dispatch(m_acop_out);
          return;
        }

        m_acop = *msg;

        switch (msg->op)
        {
          case IMC::AcousticOperation::AOP_ABORT:
            abort(msg->system);
            break;
          case IMC::AcousticOperation::AOP_RANGE:
            ping(msg->system);
            break;
          case IMC::AcousticOperation::AOP_MSG:
            transmitMessage(msg->system, msg->msg);
            break;
        }
      }

      void
      transmitMessage(const std::string& sys, const InlineMessage<IMC::Message>& imsg)
      {
        if (!hasTransducer())
          return;

        MicroModemMap::iterator itr = m_ummap.find(sys);
        if (itr == m_ummap.end())
          return;

        const IMC::Message* msg = NULL;
        std::string command;

        try
        {
          msg = imsg.get();
        }
        catch (...)
        {
          return;
        }

        if (msg->getId() == DUNE_IMC_PLANCONTROL)
        {
          const IMC::PlanControl* pc = static_cast<const IMC::PlanControl*>(msg);
          if (pc->op == IMC::PlanControl::PC_START)
          {
            if (pc->plan_id.size() == 1)
              command = String::str("$CCMUC,%u,%u,%04x\r\n", c_plan_addr, itr->second, pc->plan_id[0] & 0xff);
          }
        }

        if (command.empty())
          return;

        sendCommand(command);
      }

      void
      abort(const std::string& sys)
      {
        if (!hasTransducer())
          return;

        NarrowBandMap::iterator nitr = m_nbmap.find(sys);
        if (nitr != m_nbmap.end())
        {
          if (nitr->second.abort_freq == 0)
          {
            m_acop_out.op = IMC::AcousticOperation::AOP_UNSUPPORTED;
            dispatch(m_acop_out);
            return;
          }

          abortNarrowBand(sys, nitr->second.abort_freq);
        }

        MicroModemMap::iterator itr = m_ummap.find(sys);
        if (itr == m_ummap.end())
        {
          m_acop_out.op = IMC::AcousticOperation::AOP_UNSUPPORTED;
          dispatch(m_acop_out);
          return;
        }

        std::string cmd = String::str("$CCMUC,%u,%u,%04x\r\n", m_address, itr->second, c_code_abort);
        sendCommand(cmd);
        m_op = OP_ABORT;
        m_op_deadline = Clock::get() + m_args.tout_abort;
      }

      void
      abortNarrowBand(const std::string& sys, unsigned freq)
      {
        m_acop_out.op = IMC::AcousticOperation::AOP_ABORT_IP;
        m_acop_out.system = sys;
        dispatch(m_acop_out);

        char bfr[128];
        for (unsigned i = 0; i < 10; ++i)
        {
          std::string cmd = String::str("$CCPNT,%u,%u,%u,100,23000,0,0,0,1\r\n", freq,
                                        m_args.tx_length, m_args.rx_length);
          sendCommand(cmd);
          m_uart->read(bfr, sizeof(bfr));
          Delay::wait(0.2);
          m_uart->flushInput();
        }

        m_acop_out.op = IMC::AcousticOperation::AOP_ABORT_TIMEOUT;
        dispatch(m_acop_out);
      }

      void
      ping(const std::string& sys)
      {
        if (!hasTransducer())
          return;

        {
          MicroModemMap::iterator itr = m_ummap.find(sys);

          if (itr != m_ummap.end())
          {
            pingMicroModem(sys);
            return;
          }
        }

        {
          NarrowBandMap::iterator itr = m_nbmap.find(sys);

          if (itr != m_nbmap.end())
          {
            pingNarrowBand(sys);
            return;
          }
        }

        m_acop_out.op = IMC::AcousticOperation::AOP_UNSUPPORTED;
        m_acop_out.system = sys;
        dispatch(m_acop_out);
      }

      void
      pingMicroModem(const std::string& sys)
      {
        MicroModemMap::iterator itr = m_ummap.find(sys);

        if (itr == m_ummap.end())
          return;

        std::string cmd = String::str("$CCMPC,%u,%u\r\n", m_address, itr->second);
        sendCommand(cmd);
        m_op = OP_PING_MM;
        m_op_deadline = Clock::get() + m_args.tout_mmping;
      }

      void
      pingNarrowBand(const std::string& sys)
      {
        NarrowBandMap::iterator itr = m_nbmap.find(sys);

        if (itr == m_nbmap.end())
          return;

        unsigned query = itr->second.query_freq;
        unsigned reply = itr->second.reply_freq;

        std::string cmd = String::str("$CCPNT,%u,%u,%u,1000,%u,0,0,0,1\r\n", query,
                                      m_args.tx_length, m_args.rx_length, reply);
        sendCommand(cmd);
        m_op = OP_PING_NB;
        m_op_deadline = Clock::get() + m_args.tout_nbping;
      }

      void
      handleCAMPR(std::auto_ptr<NMEAReader>& stn)
      {
        unsigned src = 0;
        *stn >> src;
        unsigned dst = 0;
        *stn >> dst;

        if (dst != m_address)
          return;

        double ttime = 0;
        try
        {
          *stn >> ttime;
        }
        catch (...)
        { }

        if (ttime < 0)
          ttime = 0;

        m_acop_out.op = IMC::AcousticOperation::AOP_RANGE_RECVED;
        m_acop_out.system = m_acop.system;
        m_acop_out.range = ttime * m_args.sspeed;
        dispatch(m_acop_out);
        resetOp();
      }

      void
      handleSNTTA(std::auto_ptr<NMEAReader>& stn)
      {
        double ttime = 0;

        try
        {
          *stn >> ttime;
        }
        catch (...)
        {
          // No travel-time.
          return;
        }

        if (ttime < 0)
          ttime = 0;

        m_acop_out.op = IMC::AcousticOperation::AOP_RANGE_RECVED;
        m_acop_out.system = m_acop.system;
        m_acop_out.range = ttime * m_args.sspeed;
        dispatch(m_acop_out);
        resetOp();
      }

      void
      handleCAMUA(std::auto_ptr<NMEAReader>& stn)
      {
        unsigned src = 0;
        *stn >> src;
        unsigned dst = 0;
        *stn >> dst;

        // Get value.
        std::string val;
        *stn >> val;

        unsigned value = 0;
        std::sscanf(val.c_str(), "%04X", &value);

        if (value == c_code_abort_ack)
        {
          m_acop_out.op = IMC::AcousticOperation::AOP_ABORT_ACKED;
          m_acop_out.system = m_acop.system;
          dispatch(m_acop_out);
          resetOp();
        }
        else if (value & c_mask_qtrack)
        {
          unsigned beacon = (value & c_mask_qtrack_beacon) >> 10;
          unsigned range = (value & c_mask_qtrack_range);
          IMC::LblRangeAcceptance msg;
          msg.setSource(m_mimap[src]);
          msg.id = beacon;
          msg.range = range;
          msg.acceptance = IMC::LblRangeAcceptance::RR_ACCEPTED;
          dispatch(msg);
          inf("%s %u: %u", DTR("range to"), beacon, range);
        }
      }

      void
      handleCAMPCSNPNT(std::auto_ptr<NMEAReader>& stn)
      {
        (void)stn;
        m_acop_out.op = IMC::AcousticOperation::AOP_RANGE_IP;
        m_acop_out.system = m_acop.system;
        dispatch(m_acop_out);
      }

      void
      handleCAMUC(std::auto_ptr<NMEAReader>& stn)
      {
        unsigned src = 0;
        unsigned dst = 0;
        std::string val;
        *stn >> src >> dst >> val;

        unsigned value = 0;
        std::sscanf(val.c_str(), "%04X", &value);

        if (value == c_code_abort)
        {
          m_acop_out.op = IMC::AcousticOperation::AOP_ABORT_IP;
          m_acop_out.system = m_acop.system;
          dispatch(m_acop_out);
        }
      }

      void
      handleCARXD(std::auto_ptr<NMEAReader>& stn)
      {
        unsigned src;
        unsigned dst;
        unsigned ack;
        unsigned fnr;
        std::string hex;

        try
        {
          *stn >> src >> dst >> ack >> fnr >> hex;
        }
        catch (...)
        {
          return;
        }

        if (dst != 0)
          return;

        std::string msg = String::fromHex(hex);
        const char* msg_raw = msg.data();

        float lat;
        float lon;
        float depth;
        float yaw;
        uint16_t ranges[2];

        std::memcpy(&lat, msg_raw + 0, 4);
        std::memcpy(&lon, msg_raw + 4, 4);
        std::memcpy(&depth, msg_raw + 8, 4);
        std::memcpy(&yaw, msg_raw + 12, 4);
        std::memcpy(&ranges[0], msg_raw + 16, 2);
        std::memcpy(&ranges[1], msg_raw + 18, 2);

        for (int i = 0; i < 2; ++i)
        {
          if (ranges[i] == 0)
            continue;

          IMC::LblRangeAcceptance lbl;
          lbl.setSource(m_mimap[src]);
          lbl.id = i;
          lbl.range = ranges[i];
          lbl.acceptance = IMC::LblRangeAcceptance::RR_ACCEPTED;
          dispatch(lbl);
        }

        IMC::EstimatedState es;
        es.setSource(m_mimap[src]);
        es.lat = lat;
        es.lon = lon;
        es.depth = depth;
        es.psi = yaw;
        dispatch(es);
      }

      void
      readSentence(void)
      {
        char bfr[c_bfr_size];
        m_uart->readString(bfr, sizeof(bfr));

        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);

        m_dev_data.value.assign(sanitize(bfr));
        dispatch(m_dev_data);

        try
        {
          std::auto_ptr<NMEAReader> stn = std::auto_ptr<NMEAReader>(new NMEAReader(bfr));
          if (std::strcmp(stn->code(), "CAMPR") == 0)
            handleCAMPR(stn);
          else if (std::strcmp(stn->code(), "CAMUA") == 0)
            handleCAMUA(stn);
          else if (std::strcmp(stn->code(), "CAMPC") == 0)
            handleCAMPCSNPNT(stn);
          else if (std::strcmp(stn->code(), "SNPNT") == 0)
            handleCAMPCSNPNT(stn);
          else if (std::strcmp(stn->code(), "CAMUC") == 0)
            handleCAMUC(stn);
          else if (std::strcmp(stn->code(), "SNTTA") == 0)
            handleSNTTA(stn);
          else if (std::strcmp(stn->code(), "CARXD") == 0)
            handleCARXD(stn);
        }
        catch (std::exception& e)
        {
          err("%s", e.what());
        }
      }

      void
      checkTimeouts(void)
      {
        if (m_op == OP_NONE)
          return;

        double now = Clock::get();

        if (now > m_op_deadline)
        {
          m_acop_out.system = m_acop.system;

          if ((m_op == OP_PING_NB) || (m_op == OP_PING_MM))
            m_acop_out.op = IMC::AcousticOperation::AOP_RANGE_TIMEOUT;
          else if (m_op == OP_ABORT)
            m_acop_out.op = IMC::AcousticOperation::AOP_ABORT_TIMEOUT;

          dispatch(m_acop_out);
          resetOp();
        }
      }

      void
      onMain(void)
      {
        while (!stopping())
        {
          consumeMessages();

          if (m_uart->hasNewData(0.1) == IOMultiplexing::PRES_OK)
          {
            readSentence();
            m_last_stn.reset();
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
          }

          if (m_last_stn.overflow())
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);

          checkTimeouts();
        }
      }
    };
  }
}

DUNE_TASK
