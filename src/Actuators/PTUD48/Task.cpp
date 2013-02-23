//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Joel Cardoso                                                     *
//***************************************************************************

// ISO C++ 98 headers.
#include <cstring>
#include <cstddef>
#include <string>

// DUNE headers.
#include <DUNE/DUNE.hpp>

// Definition
#define BUFFER_MAX 256

namespace Actuators
{
  namespace PTUD48
  {
    using DUNE_NAMESPACES;

    // Pan and Tilt maximum and minimum values.
    enum Limits
    {
      // Maximum tilt.
      TILT_MAX = 2333,
      // Minimum tilt.
      TILT_MIN = -6999,
      // Maximum pan.
      PAN_MAX  = 6999,
      // Minimum pan.
      PAN_MIN  = -6999
    };

    struct Arguments
    {
      // Serial port device.
      std::string uart_dev;
      // Serial port baud rate.
      unsigned uart_baud;
      // PTU pan continuous.
      bool ptu_pc;
      // PTU tracking.
      bool ptu_track;
      // PTU pan speed.
      int  pan_speed;
      // PTU tilt speed.
      int tilt_speed;
      // PTU pan acceleration.
      int pan_accel;
      // PTU tilt acceleration.
      int tilt_accel;
    };

    struct Task: public Tasks::Periodic
    {
      // Device protocol handler.
      SerialPort* m_uart;
      // Task Arguments.
      Arguments m_args;
      // Serial Port buffer.
      uint8_t m_bfr[BUFFER_MAX];

      Task(const std::string& name, Tasks::Context& ctx):
        Tasks::Periodic(name, ctx),
        m_uart(NULL)
      {
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("/dev/ttyUSB0")
        .description("Serial port device (used to communicate with the actuator)");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("9600")
        .description("Serial port baud rate");

        param("PTU Pan Continuous", m_args.ptu_pc)
        .defaultValue("false")
        .description("PTU pan continuous enable");

        param("PTU Tracking", m_args.ptu_track)
        .defaultValue("true")
        .description("PTU tracking mode enable");

        param("PTU Pan Speed", m_args.pan_speed)
        .defaultValue("1000")
        .description("PTU pan speed in positions/sec");

        param("PTU Tilt Speed", m_args.tilt_speed)
        .defaultValue("1000")
        .description("PTU tilt speed in positions/sec");

        param("PTU Pan Acceleration", m_args.pan_accel)
        .defaultValue("2000")
        .description("PTU pan acceleration in positions/sec/sec");

        param("PTU Tilt Acceleration", m_args.tilt_accel)
        .defaultValue("2000")
        .description("PTU tilt acceleration in positions/sec/sec");

        // Setup entity states.
        // @todo: set task entity states using new scheme.
        //
        // for reference:
        // m_estates[STA_COM_ERROR].state = IMC::EntityState::ESTA_ERROR;
        // m_estates[STA_COM_ERROR].description = DTR("no PTU data - serial port communication error");
        // m_estates[STA_DEV_ERROR].state = IMC::EntityState::ESTA_ERROR;
        // m_estates[STA_DEV_ERROR].description = DTR("PTU internal error");

        bind<IMC::RemoteActions>(this);
      }

      ~Task(void)
      {
        Task::onResourceRelease();
      }

      void
      onResourceAcquisition(void)
      {
        m_uart = new SerialPort(m_args.uart_dev, m_args.uart_baud);
      }

      void
      onResourceInitialization(void)
      {
        // Send execute immediatly command.
        sendCommand("i ");
        // Send reset.
        sendCommand("r ");
        debug("resetting PTU");
        // Wait for reset.
        sendCommand("a ");
        // Send position control command.
        sendCommand("ci ");
        // Wait.
        sendCommand("a ");
        // Send pan continuous mode (disable).
        sendCommand("pcd ");
        // Wait.
        sendCommand("a ");
        // Disable factory limits (Attention to not overload the tilt limits).
        sendCommand("ld ");
        // Wait.
        sendCommand("a ");
        // Set pan and tilt speeds.
        createCommand("ps", m_args.pan_speed);
        createCommand("ts", m_args.tilt_speed);
        // Set pan and tilt accelerations.
        createCommand("pa", m_args.pan_accel);
        createCommand("ta", m_args.tilt_accel);
      }

      void
      onResourceRelease(void)
      {
        Memory::clear(m_uart);
      }

      void
      sendCommand(const std::string& cmd)
      {
        m_uart->write(cmd.c_str(), cmd.size());
        trace("OUT | %s | %u", sanitize(cmd).c_str(), (unsigned)cmd.size());
        // Check for command success.
        /*if(m_uart->hasNewData(1.0) == IOMultiplexing::PRES_OK)
          {
          int retval = m_uart->read(m_bfr, sizeof(bfr));
          debug("%i", retval);
          }
          else
          {
          debug("no response!");
          }*/
      }

      void
      createCommand(const std::string& cmd_type, int& val)
      {
        std::stringstream cmd;
        cmd << cmd_type << val << " ";
        sendCommand(cmd.str());
      }

      void
      consume(const IMC::RemoteActions* ra)
      {
        TupleList tuples(ra->actions);

        // Get command fields.
        float pan_rad = tuples.get("Pan", 0.0f);
        float tilt_rad =  tuples.get("Tilt", 0.0f);
        //float pan_rate_rad =  tuples.get("PanRate", 0.0f);
        //float tilt_rate_rad =  tuples.get("TiltRate", 0.0f);

        int pan_pos = panRadToPos(pan_rad);
        int tilt_pos = tiltRadToPos(tilt_rad);
        //int pan_rate_pos = radToPos(pan_rate_rad);
        //int tilt_rate_pos = radToPos(tilt_rate_rad);

        // Bound pan and tilt values.
        boundPan(pan_pos);
        boundTilt(tilt_pos);

        // Debug messages.
        debug("%f", pan_rad);
        debug("%d", pan_pos);
        debug("%f", tilt_rad);
        debug("%d", tilt_pos);
        debug("pan bounded %d", pan_pos);
        debug("tilt bounded %d", tilt_pos);
        //debug("%f", pan_rate_rad);
        //debug("%f", pan_rate_pos);
        //debug("%f", tilt_rate_rad);
        //debug("%f", tilt_rate_pos);

        // Send pan rate command.
        //createCommand("ps", pan_rate_pos);

        // Send pan command.
        createCommand("pp", pan_pos);

        // Send tilt rate command.
        //createCommand("ts", tilt_rate_pos);

        // Send tilt command.
        createCommand("tp", tilt_pos);

        // Send halt command.
        if (tuples.get("Halt", 0)) sendCommand("h ");
      }

      int
      panRadToPos(float& val)
      {
        return (int)((val * 6999 / Math::c_pi) + 0.5);
      }

      int
      tiltRadToPos(float& val)
      {
        return (int)((val * 2 * 6999/ Math::c_pi) + 0.5);
      }

      void
      boundPan(int& val)
      {
        val = val < PAN_MIN ? PAN_MIN : val; val = val > PAN_MAX ? PAN_MAX : val;
      }

      void
      boundTilt(int& val)
      {
        val = val < TILT_MIN ? TILT_MIN : val; val = val > TILT_MAX ? TILT_MAX : val;
      }

      void
      task(void)
      {
        // Check serial port.
        /*if (m_uart->hasNewData(0.1) == IOMultiplexing::PRES_OK)
          {
          uint8_t bfr[BUFFER_MAX];
          int retval = m_uart->read(bfr, sizeof(bfr));
          for (int i = 0; i < retval; ++i)
          {
          if(parse(bfr[i]))
          }
          }*/
      }
    };
  }
}

DUNE_TASK
