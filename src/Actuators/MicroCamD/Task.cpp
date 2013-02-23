//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

// ISO C++ 98 headers.
#include <cstring>
#include <cstddef>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Actuators
{
  namespace MicroCamD
  {
    using DUNE_NAMESPACES;

    enum Mode
    {
      MODE_RATE = 0,
      MODE_PILOT = 0x03,
      MODE_STOW = 0x04
    };

    enum OSD
    {
      OSD_NONE = 0,
      OSD_GRAPH = 1 << 5,
      OSD_TEXT = 1 << 6,
      OSD_BOTH = OSD_TEXT | OSD_GRAPH
    };

    enum Zoom
    {
      ZOOM_NO_CHANGE = 0x00,
      ZOOM_OUT = 0x01,
      ZOOM_IN = 0x02
    };

    enum CameraMode
    {
      CAM_MODE_DEP_FOV = 0 << 3,
      CAM_MODE_NDEP_FOV = 1 << 3
    };

    enum Indices
    {
      IDX_HDR1 = 0,
      IDX_HDR2 = 1,
      IDX_HDR3 = 2,
      IDX_MODE = 3,
      IDX_ZOOM = 6,
      IDX_CAM_MODE = 8,
      IDX_FOV = 10,
      IDX_PITCH_ROLL_LSB = 11,
      IDX_BYTE13 = 13,
      IDX_PITCH_RATE_MSB = 16,
      IDX_ROLL_RATE_MSB = 17,
      IDX_BYTE18 = 18,
      IDX_CSUM = 19
    };

    enum ParserStates
    {
      PST_HDR1,
      PST_HDR2,
      PST_HDR3,
      PST_DATA,
      PST_CSUM
    };

    struct Arguments
    {
      // Serial port device.
      std::string uart_dev;
      // Serial port baud rate.
      unsigned uart_baud;
    };

    struct Task: public Tasks::Periodic
    {
      // Device protocol handler.
      SerialPort* m_uart;
      // Camera command.
      uint8_t m_cmd[20];
      // Task arguments.
      Arguments m_args;
      // Parser state.
      ParserStates m_pstate;
      uint8_t m_pdata[16];
      uint8_t m_pdata_idx;
      uint8_t m_pdata_crc;
      // Euler angles.
      IMC::EulerAngles m_euler;

      Task(const std::string& name, Tasks::Context& ctx):
        Tasks::Periodic(name, ctx),
        m_uart(NULL),
        m_pstate(PST_HDR1),
        m_pdata_idx(0),
        m_pdata_crc(0)
      {
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("")
        .description("Serial port device used to communicate with the sensor");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("19200")
        .description("Serial port baud rate");

        std::memset(m_cmd, 0, sizeof(m_cmd));

        // Header.
        m_cmd[IDX_HDR1] = 0xb0;
        m_cmd[IDX_HDR2] = 0x3b;
        m_cmd[IDX_HDR3] = 0x4f;

        bind<IMC::CameraZoom>(this);
        bind<IMC::RemoteActions>(this);
      }

      ~Task(void)
      {
        Task::onResourceRelease();
      }

      void
      onResourceAcquisition(void)
      {
        m_uart = new SerialPort(m_args.uart_dev, m_args.uart_baud, SerialPort::SP_PARITY_EVEN);
      }

      void
      onResourceRelease(void)
      {
        Memory::clear(m_uart);
      }

      void
      onResourceInitialization(void)
      {
        setMode(MODE_RATE);
        setOSD(OSD_NONE);
        setCameraMode(CAM_MODE_NDEP_FOV);
        sendCommand();
      }

      void
      consume(const IMC::RemoteActions* msg)
      {
        TupleList tuples(msg->actions);

        switch (tuples.get("Zoom", 0))
        {
          case 0:
            setZoom(ZOOM_NO_CHANGE);
            break;
          case -1:
            setZoom(ZOOM_OUT);
            break;
          case 1:
            setZoom(ZOOM_IN);
            break;
        }

        switch (tuples.get("Pan", 0))
        {
          case 0:
            setRollRate(-512);
            break;
          case -1:
            setRollRate(-445);
            break;
          case 1:
            setRollRate(445);
            break;
        }

        switch (tuples.get("Tilt", 0))
        {
          case 0:
            setPitchRate(-512);
            break;
          case -1:
            setPitchRate(-445);
            break;
          case 1:
            setPitchRate(445);
            break;
        }

        // m_changed = true;

        // if (tuples.get("Zoom", 0))
        //   setZoom(ZOOM_IN);
        // else
        //   setZoom(ZOOM_NO_CHANGE);

        // if (tuples.get("Zoom Out", 0))
        //   setZoom(ZOOM_OUT);
        // else
        //   setZoom(ZOOM_NO_CHANGE);

        // if (tuples.get("Pan Up", 0))
        //   setPitchRate(64);
        // else
        //   setPitchRate(0);

        // if (tuples.get("Pan Down", 0))
        //   setPitchRate(-64);
        // else
        //   setPitchRate(0);

        // if (tuples.get("Tilt Right", 0))
        //   setRollRate(64);
        // else
        //   setRollRate(0);

        // if (tuples.get("Tilt Left", 0))
        //   setRollRate(-64);
        // else
        //   setRollRate(0);

        setMirror(tuples.get("Mirror", 0) ? true : false);
      }

      void
      consume(const IMC::CameraZoom* msg)
      {
        switch (msg->action)
        {
          case IMC::CameraZoom::ACTION_ZOOM_IN:
            setZoom(ZOOM_IN);
            break;
          case IMC::CameraZoom::ACTION_ZOOM_OUT:
            setZoom(ZOOM_OUT);
            break;
          case IMC::CameraZoom::ACTION_ZOOM_STOP:
            setZoom(ZOOM_NO_CHANGE);
            break;
        }
      }

      void
      setMirror(bool value)
      {
        (void)value;
        // m_cmd[IDX_BYTE13] = (m_cmd[IDX_BYTE13] & 0xf3) | (value ? 0x04 : 0x00);
        // m_cmd[IDX_BYTE18] = (m_cmd[IDX_BYTE18] & 0xfb) | (value ? 0x04 : 0x00);

        // if (!value)
        //   return;

        // m_cmd[3] = 0xe0;
        // m_cmd[5] = 0x10;
        // m_cmd[8] = 0x50;
        // m_cmd[11] = 0xf0;
        // m_cmd[IDX_BYTE13] = value ? 0xf7 : 0xf3;
        // m_cmd[IDX_BYTE18] = value ? 0x2f : 0x2b;
      }

      void
      setCameraMode(CameraMode mode)
      {
        m_cmd[IDX_CAM_MODE] = mode;
      }

      void
      setZoom(Zoom zoom)
      {
        m_cmd[IDX_ZOOM] = zoom;
      }

      void
      setOSD(OSD osd)
      {
        m_cmd[IDX_MODE] = (m_cmd[IDX_MODE] & 0x1f) | osd;
      }

      void
      setMode(Mode mode)
      {
        m_cmd[IDX_MODE] = (m_cmd[IDX_MODE] & 0xe0) | mode;
      }

      void
      setPitchRate(int16_t pitch)
      {
        m_cmd[IDX_PITCH_ROLL_LSB] = (m_cmd[IDX_PITCH_ROLL_LSB] & 0xfc) | (pitch & 0x03);
        m_cmd[IDX_PITCH_RATE_MSB] = (pitch >> 2) & 0xff;
      }

      void
      setRollRate(int16_t roll)
      {
        uint16_t val = 0;
        std::memcpy(&val, &roll, 2);

        m_cmd[IDX_PITCH_ROLL_LSB] = (m_cmd[IDX_PITCH_ROLL_LSB] & 0xf3) | ((val & 0x03) << 2);
        m_cmd[IDX_ROLL_RATE_MSB] = (val >> 2) & 0xff;
      }

      void
      computeChecksum(void)
      {
        m_cmd[IDX_CSUM] = 0;
        for (unsigned i = 0; i < IDX_CSUM; ++i)
          m_cmd[IDX_CSUM] += m_cmd[i];
      }

      void
      sendCommand(void)
      {
        computeChecksum();

#if 0
        fprintf(stderr, "out: ");
        for (unsigned i = 0; i < sizeof(m_cmd); ++i)
          fprintf(stderr, "%02X ", m_cmd[i]);
        fprintf(stderr, "\n");
#endif

        m_uart->write(m_cmd, sizeof(m_cmd));
      }

      bool
      parse(uint8_t byte)
      {
        switch (m_pstate)
        {
          case PST_HDR1:
            if (byte == 0xb0)
            {
              m_pdata_idx = 0;
              m_pdata_crc = byte;
              m_pstate = PST_HDR2;
            }
            break;

          case PST_HDR2:
            if (byte == 0x3b)
            {
              m_pdata_crc += byte;
              m_pstate = PST_HDR3;
            }
            break;

          case PST_HDR3:
            if (byte == 0x4f)
            {
              m_pdata_crc += byte;
              m_pstate = PST_DATA;
            }
            break;

          case PST_DATA:
            m_pdata[m_pdata_idx++] = byte;
            m_pdata_crc += byte;
            if (m_pdata_idx == sizeof(m_pdata))
              m_pstate = PST_CSUM;
            break;

          case PST_CSUM:
            m_pstate = PST_HDR1;
            if (byte == m_pdata_crc)
              return true;
            break;
        }

        return false;
      }

      void
      task(void)
      {
        sendCommand();

        if (m_uart->hasNewData(0.1) == IOMultiplexing::PRES_OK)
        {
          uint8_t bfr[20];
          int rv = m_uart->read(bfr, sizeof(bfr));
          for (int i = 0; i < rv; ++i)
          {
            if (parse(bfr[i]))
            {
              //float fov = 48 - ((29 / 170.0) * m_pdata[5]);

              uint16_t tmp_u = (m_pdata[6] | ((m_pdata[7] & 0x0f) << 8));
              if (tmp_u & 0x800)
                tmp_u |= 0xf000;

              int16_t tmp_s = 0;
              std::memcpy(&tmp_s, &tmp_u, 2);

              m_euler.theta = Angles::radians((tmp_s * (360.0 / 4096.0)));

              tmp_u = (m_pdata[8] | ((m_pdata[9] & 0x0f) << 8));
              if (tmp_u & 0x800)
                tmp_u |= 0xf000;

              std::memcpy(&tmp_s, &tmp_u, 2);

              m_euler.phi = Angles::radians((tmp_s * (360.0 / 4096.0)));
              dispatch(m_euler);
            }
          }
        }
      }
    };
  }
}

DUNE_TASK
