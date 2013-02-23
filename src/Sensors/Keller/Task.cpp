//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Renato Caldas                                                    *
//***************************************************************************

// ISO C++ 98 headers.
#include <cstddef>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Sensors
{
  //! Device driver for %Keller Pressure %Sensors.
  namespace Keller
  {
    using DUNE_NAMESPACES;

    enum Commands
    {
      CMD_CONFIRMATION_FOR_INITIALIZATION = 48,
      CMD_READ_SERIAL_NUMBER = 69,
      CMD_READ_CHANNEL = 73,
      CMD_ZERO_CHANNEL = 95
    };

    enum CommandDataSizes
    {
      CMD_CONFIRMATION_FOR_INITIALIZATION_SIZE = 6,
      CMD_READ_SERIAL_NUMBER_SIZE = 4,
      CMD_READ_CHANNEL_SIZE = 5,
      CMD_ZERO_CHANNEL_SIZE = 1
    };

    enum ParserStates
    {
      STA_ADDR,
      STA_CMD,
      STA_DATA,
      STA_CRC_MSB,
      STA_CRC_LSB
    };

    enum ParserResults
    {
      RES_IN_PROGRESS = 0,
      RES_DONE,
      RES_CRC,
      RES_EXCEPTION
    };

    struct Arguments
    {
      // UART device.
      std::string uart_dev;
      // UART baud rate.
      unsigned uart_baud;
      // True if UART has local echo enabled.
      bool uart_echo;
      // Depth conversion factor.
      float depth_conv;
      // Device address.
      int address;
    };

    // Number of seconds to wait before setting an entity error.
    static const float c_expire_wdog = 2.0f;
    // Conversion between bar and pascal
    static const float c_pascal_per_bar = 100000.0f;

    struct Task: public Tasks::Periodic
    {
      static const int c_parser_data_size = 6;
      // Serial port handle.
      SerialPort* m_uart;
      // True if serial port echoes sent commands.
      bool m_echo;
      // Read Pressure message;
      uint8_t m_msg_read_pressure[5];
      // Read Temperature message;
      uint8_t m_msg_read_temperature[5];
      // Pressure.
      IMC::Pressure m_pressure;
      // Depth.
      IMC::Depth m_depth;
      // Measured temperature.
      IMC::Temperature m_temperature;
      // Entity ID.
      int m_entity_id;
      // Current parser state.
      ParserStates m_parser_state;
      // Current parser command.
      uint8_t m_parser_cmd;
      // Parser data buffer.
      uint8_t m_parser_data[c_parser_data_size];
      // Parser data buffer length.
      uint8_t m_parser_data_len;
      // Parser data CRC.
      uint16_t m_parser_data_crc;
      // Parser packet CRC.
      uint16_t m_parser_packet_crc;
      // Active channel value.
      float m_channel_readout;
      // Entity error reporting expire time checker
      Time::Counter<float> m_error_wdog;
      // Task arguments.
      Arguments m_args;

      Task(const std::string& name, Tasks::Context& ctx):
        Tasks::Periodic(name, ctx),
        m_uart(NULL)
      {
        // Define configuration parameters.
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("")
        .description("Serial port device used to communicate with the sensor");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("9600")
        .description("Serial port baud rate");

        param("Serial Port - Local Echo", m_args.uart_echo)
        .defaultValue("false")
        .description("Set to true if serial port has local echo enabled");

        param("Device Address", m_args.address)
        .minimumValue("0")
        .maximumValue("250");

        param("Water Density", m_args.depth_conv)
        .units(Units::KilogramPerCubicMeter)
        .defaultValue("1025.0");
      }

      ~Task(void)
      {
        Task::onResourceRelease();
      }

      void
      onUpdateParameters(void)
      {
        // Depth conversion (bar to meters of fluid).
        m_args.depth_conv = 100e3 / (9.8 * m_args.depth_conv);

        // Initialize serial messages.
        m_msg_read_pressure[0] = m_args.address;
        m_msg_read_pressure[1] = CMD_READ_CHANNEL;
        m_msg_read_pressure[2] = 1;
        uint16_t crc = Algorithms::CRC16::compute(m_msg_read_pressure, 3, 0xFFFF);
        ByteCopy::toBE(crc, &m_msg_read_pressure[3]);

        m_msg_read_temperature[0] = m_args.address;
        m_msg_read_temperature[1] = CMD_READ_CHANNEL;
        m_msg_read_temperature[2] = 4;
        crc = Algorithms::CRC16::compute(m_msg_read_temperature, 3, 0xFFFF);
        ByteCopy::toBE(crc, &m_msg_read_temperature[3]);

        m_error_wdog.setTop(c_expire_wdog);
      }

      void
      onResourceAcquisition(void)
      {
        onResourceRelease();

        m_uart = new SerialPort(m_args.uart_dev, m_args.uart_baud);
      }

      void
      onResourceRelease(void)
      {
        Memory::clear(m_uart);
      }

      void
      onResourceInitialization(void)
      {
        initialize();
        zero();
      }

      bool
      write(uint8_t* bfr, int len)
      {
        uint8_t rxbfr[10];
        int i = len;
        bool aborted = true;

        m_uart->write(bfr, len);
        // If no echo is expected, do nothing here.
        if (!m_args.uart_echo)
          return true;

        while (m_uart->hasNewData(0.1) == IOMultiplexing::PRES_OK)
        {
          i -= m_uart->read(rxbfr + (len - i), i);
          if (i == 0)
          {
            aborted = false;
            break;
          }
        }

        if (aborted)
        {
          setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
          // No echo received, so something is seriously broken
          throw std::runtime_error(DTR("echo handling enabled, but got no RS-485 echo"));
        }

        // Check for collisions here.
        for (i = 0; i < len; i++)
        {
          if (rxbfr[i] != bfr[i])
          {
            // Echo doesn't match, bus error (may be fatal)
            if (getEntityState() != IMC::EntityState::ESTA_ERROR)
              err(DTR("received RS-485 echo doesn't match"));
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
            return false;
          }
        }

        // All ok.
        return true;
      }

      bool
      read(void)
      {
        uint8_t bfr[10];

        // Reset the parser whenever a read is asked for.
        m_parser_state = STA_ADDR;

        while (m_uart->hasNewData(0.1) == IOMultiplexing::PRES_OK)
        {
          int len = m_uart->read(bfr, sizeof(bfr));
          ParserResults result = parse(bfr, len);

          if (result == RES_DONE)
          {
            m_error_wdog.reset();
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
            return true;
          }
          else if (result == RES_CRC)
          {
            err(DTR("invalid CRC"));
            return false;
          }
          else if (result == RES_EXCEPTION)
          {
            return false;
          }
        }

        // hasNewData timed out, so got nothing useful:
        return false;
      }

      ParserResults
      parse(uint8_t* bfr, uint8_t len)
      {
        ParserResults result = RES_IN_PROGRESS;

        while (len > 0)
        {
          switch (m_parser_state)
          {
            case STA_ADDR:
              if (*bfr == m_args.address)
              {
                m_parser_data_crc = Algorithms::CRC16::compute(bfr, 1, 0xFFFF);
                m_parser_state = STA_CMD;
              }
              break;
            case STA_CMD:
              m_parser_cmd = *bfr;
              m_parser_data_crc = Algorithms::CRC16::compute(bfr, 1, m_parser_data_crc);
              m_parser_state = STA_DATA;
              m_parser_data_len = 0;
              break;
            case STA_DATA:
              m_parser_data[m_parser_data_len++] = *bfr;
              if ((m_parser_data_len >= c_parser_data_size) ||
                  // This means we got an exception, so only one data byte to read:
                  (m_parser_cmd & (1 << 7)) ||
                  ((m_parser_cmd == CMD_CONFIRMATION_FOR_INITIALIZATION) && (m_parser_data_len >= CMD_CONFIRMATION_FOR_INITIALIZATION_SIZE)) ||
                  ((m_parser_cmd == CMD_READ_SERIAL_NUMBER) && (m_parser_data_len >= CMD_READ_SERIAL_NUMBER_SIZE)) ||
                  ((m_parser_cmd == CMD_READ_CHANNEL) && (m_parser_data_len >= CMD_READ_CHANNEL_SIZE)) ||
                  ((m_parser_cmd == CMD_ZERO_CHANNEL) && (m_parser_data_len >= CMD_ZERO_CHANNEL_SIZE)))
                m_parser_state = STA_CRC_MSB;
              break;
            case STA_CRC_MSB:
              m_parser_data_crc = Algorithms::CRC16::compute(m_parser_data, m_parser_data_len, m_parser_data_crc);

              m_parser_packet_crc = (*bfr << 8);
              m_parser_state = STA_CRC_LSB;
              break;
            case STA_CRC_LSB:
              m_parser_packet_crc |= *bfr;
              // Handle crc errors properly:
              if (m_parser_packet_crc != m_parser_data_crc)
                result = RES_CRC;
              else if (!interpret())
                result = RES_EXCEPTION;
              else
                result = RES_DONE;
            default:
              m_parser_state = STA_ADDR;
              break;
          }

          bfr++;
          len--;
        }

        return result;
      }

      bool
      interpret(void)
      {
        uint32_t tmp = 0;

        if (m_parser_cmd & (1 << 7))
        {
          if (m_parser_data[0] == 32)
          {
            err(DTR("device not initialized, initializing"));
            setEntityState(IMC::EntityState::ESTA_BOOT, Status::CODE_INIT);
            initialize();
          }
          else
          {
            err(DTR("got exception %d for command %d"),
                (int)m_parser_data[0], (int)m_parser_cmd);
          }

          // Got an exception don't bother interpreting anything else.
          return false;
        }

        switch (m_parser_cmd)
        {
          case CMD_CONFIRMATION_FOR_INITIALIZATION:
            inf(DTR("initialized device: class=%d.%d firmware=%d"),
                (int)m_parser_data[0], (int)m_parser_data[2], (int)m_parser_data[3]);
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
            break;
          case CMD_READ_SERIAL_NUMBER:
            ByteCopy::fromBE(tmp, m_parser_data);
            inf(DTR("device serial number=%u"), tmp);
            break;
          case CMD_READ_CHANNEL:
            ByteCopy::fromBE(m_channel_readout, m_parser_data);
            break;
          case CMD_ZERO_CHANNEL:
            inf(DTR("successfuly zeroed device"));
        }

        // Everything correctly interpreted, so return true
        return true;
      }

      void
      initialize(void)
      {
        uint16_t crc = 0;
        uint8_t bfr[10] =
        {
          (uint8_t)m_args.address,
          (uint8_t)CMD_CONFIRMATION_FOR_INITIALIZATION
        };

        crc = Algorithms::CRC16::compute(bfr, 2, 0xFFFF);
        ByteCopy::toBE(crc, &bfr[2]);
        write(bfr, 4);
        if (!read())
          throw std::runtime_error(DTR("unable to initialize the device"));

        bfr[0] = m_args.address;
        bfr[1] = CMD_READ_SERIAL_NUMBER;
        crc = Algorithms::CRC16::compute(bfr, 2, 0xFFFF);
        ByteCopy::toBE(crc, &bfr[2]);
        write(bfr, 4);
        if (!read())
          throw std::runtime_error(DTR("unable to retrieve the serial number"));
      }

      void
      zero(void)
      {
        uint16_t crc = 0;
        uint8_t bfr[10] =
        {
          (uint8_t)m_args.address,
          (uint8_t)CMD_ZERO_CHANNEL,
          0
        };

        crc = Algorithms::CRC16::compute(bfr, 3, 0xFFFF);
        ByteCopy::toBE(crc, &bfr[3]);
        write(bfr, 5);
        if (!read())
          throw std::runtime_error(DTR("unable to zero the device"));
      }

      void
      task(void)
      {
        // Query pressure.
        if (write(m_msg_read_pressure, sizeof(m_msg_read_pressure)))
        {
          if (read())
          {
            m_pressure.value = m_channel_readout * c_pascal_per_bar;
            dispatch(m_pressure);
            m_depth.value = m_channel_readout * m_args.depth_conv;
            dispatch(m_depth);
          }
        }

        // Query temperature.
        if (write(m_msg_read_temperature, sizeof(m_msg_read_temperature)))
        {
          if (read())
          {
            m_temperature.value = m_channel_readout;
            dispatch(m_temperature);
          }
        }

        // If we had no good answer from device in x seconds, show entity error.
        if (m_error_wdog.overflow())
        {
          setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);

          // The device seems to be dead.. attempt to restart
          try
          {
            onResourceAcquisition();
            onResourceInitialization();
          }
          catch (...)
          {
          }
        }
      }
    };
  }
}

DUNE_TASK
