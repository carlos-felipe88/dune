//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

// ISO C++ 98 headers.
#include <stdexcept>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Sensors
{
  //! Device driver for the Microstrain 3DM-GX3 AHRS.
  //!
  //! This task is responsible to extract acceleration, angular rates, magnetic
  //! field and euler angles information from the device.
  //!
  //! This task allows for Hard-Iron calibration when resources are initialized.
  //!
  //! @author Ricardo Martins
  namespace Microstrain3DMGX3
  {
    using DUNE_NAMESPACES;

    //! Commands to device.
    enum Commands
    {
      //! Acceleration, Angular Rates, Magnetometer Vector and the Orientation Matrix.
      CMD_DATA = 0xCC,
      //! Write Word to EEPROM
      CMD_WRITE_EEPROM = 0xE4,
      //! Read Word from EEPROM
      CMD_READ_EEPROM = 0xE5,
      //! Firmware version.
      CMD_FWARE_VERSION = 0xE9,
      //! Device reset.
      CMD_DEVICE_RESET = 0xFE
    };

    //! Response frame sizes.
    enum Sizes
    {
      //! Size of Acceleration, Angular Rates,
      //! Magnetometer Vector and Orientation Matrix frame.
      CMD_DATA_SIZE = 79,
      //! Size of Write Word to EEPROM frame.
      CMD_WRITE_EEPROM_SIZE = 5,
      //! Size of Read Word from EEPROM.
      CMD_READ_EEPROM_SIZE = 5,
      //! Size of Firmware version frame.
      CMD_FWARE_VERSION_SIZE = 7,
      //! Size of Device reset frame.
      CMD_DEVICE_RESET_SIZE = 0
    };

    //! %Task arguments.
    struct Arguments
    {
      //! UART device.
      std::string uart_dev;
      //! UART baud rate.
      unsigned uart_baud;
      //! Input timeout.
      double data_tout;
      //! Calibration threshold.
      double calib_threshold;
      //! Hard iron calibration.
      std::vector<float> calib_params;
      //! Incoming Calibration Parameters entity label.
      std::string calib_elabel;
    };

    //! Time to wait for soft-reset.
    static const float c_reset_tout = 5.0;

    //! %Microstrain3DMGX3 software driver.
    struct Task: public DUNE::Tasks::Periodic
    {
      //! Internal read buffer.
      static const unsigned c_bfr_size = 128;
      //! Number of addresses for magnetic calibration.
      static const unsigned c_num_addr = 6;
      //! Magnetic calibration initial address.
      static const uint16_t c_mag_addr = 0x0400;
      //! Serial port.
      SerialPort* m_uart;
      //! Euler angles message.
      IMC::EulerAngles m_euler;
      //! Acceleration message.
      IMC::Acceleration m_accel;
      //! Angular velocity message.
      IMC::AngularVelocity m_agvel;
      //! Magnetometer Vector message.
      IMC::MagneticField m_magfield;
      //! ParameterControl message.
      IMC::ParameterControl m_pc;
      //! Calibration parameter.
      std::string m_param;
      //! Timer to wait for soft-reset without issuing error.
      Time::Counter<float> m_timer;
      //! Internal read buffer.
      uint8_t m_bfr[c_bfr_size];
      //! Magnetic Calibration addresses.
      uint16_t m_addr[c_num_addr];
      //! Compass Calibration maneuver entity id.
      unsigned m_calib_eid;
      //! Read timestamp.
      double m_tstamp;
      //! Task arguments.
      Arguments m_args;

      Task(const std::string& name, Tasks::Context& ctx):
        DUNE::Tasks::Periodic(name, ctx),
        m_uart(NULL),
        m_param("Hard-Iron Calibration"),
        m_tstamp(0)
      {
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("")
        .description("Serial port device used to communicate with the sensor");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("115200")
        .description("Serial port baud rate");

        param("Data Timeout", m_args.data_tout)
        .defaultValue("2.0")
        .units(Units::Second)
        .description("Number of seconds without data before reporting an error");

        param("Calibration Threshold", m_args.calib_threshold)
        .defaultValue("0.1")
        .units(Units::Gauss)
        .minimumValue("0.0")
        .description("Number of seconds without data before reporting an error");

        param(m_param, m_args.calib_params)
        .units(Units::Gauss)
        .size(3)
        .description("Hard-Iron calibration parameters");

        param("Calibration Maneuver - Entity Label", m_args.calib_elabel)
        .defaultValue("")
        .description("Entity label of maneuver responsible for compass calibration");

        m_pc.op = IMC::ParameterControl::OP_SAVE_PARAMS;
        m_timer.setTop(c_reset_tout);

        bind<IMC::MagneticField>(this);
      }

      ~Task(void)
      {
        Task::onResourceRelease();
      }

      //! Release resources.
      void
      onResourceRelease(void)
      {
        Memory::clear(m_uart);
      }

      //! Resolve entities.
      void
      onEntityResolution(void)
      {
        m_calib_eid = resolveEntity(m_args.calib_elabel);
      }

      //! Acquire resources.
      void
      onResourceAcquisition(void)
      {
        setEntityState(IMC::EntityState::ESTA_BOOT, Status::CODE_INIT);
        m_uart = new SerialPort(m_args.uart_dev, m_args.uart_baud);
        m_uart->flush();
      }

      //! Initialize resources.
      void
      onResourceInitialization(void)
      {
        // initialize magnetic calibration addresses.
        for (unsigned i = 0; i <= c_num_addr - 1; i++)
          m_addr[i] = c_mag_addr + (uint16_t)(i * 2);

        while (!stopping())
        {
          // Read firmware version in order to assess if we can communicate
          // with the device.
          m_uart->setMinimumRead(CMD_FWARE_VERSION_SIZE);
          if (poll(CMD_FWARE_VERSION, CMD_FWARE_VERSION_SIZE, 0, 0))
            break;

          setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
        }

        // Run calibration using configuration parameters.
        runCalibration();

        // Prepare to read data frame.
        m_uart->setMinimumRead(CMD_DATA_SIZE);
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
      }

      void
      consume(const IMC::MagneticField* msg)
      {
        if (m_calib_eid != msg->getSourceEntity())
          return;

        // Reject if it is small adjustment.
        if ((std::abs(msg->x) < m_args.calib_threshold) &&
            (std::abs(msg->y) < m_args.calib_threshold))
          return;

        m_args.calib_params[0] += msg->x;
        m_args.calib_params[1] += msg->y;

        runCalibration();
        saveParameters();
      }

      //! Send commands to the device.
      //! @param[in] cmd command char.
      //! @param[in] cmd_size expected frame response size.
      //! @param[in] addr address.
      //! @param[in] value value to be assigned.
      //! @return true if communication successful, false otherwise.
      inline bool
      poll(Commands cmd, Sizes cmd_size, uint16_t addr, uint16_t value)
      {
        // Request data.
        switch (cmd)
        {
          case CMD_DATA:
            m_uart->write((uint8_t*)&cmd, 1);
            break;
          case CMD_WRITE_EEPROM:
            calibrate(addr, value);
            break;
          case CMD_READ_EEPROM:
            requestCalibration(addr);
            break;
          case CMD_FWARE_VERSION:
            m_uart->write((uint8_t*)&cmd, 1);
            break;
          case CMD_DEVICE_RESET:
            resetDevice();
          default:
            break;
        }

        return listen(cmd, cmd_size);
      }

      //! Listen for responses.
      //! @param[in] cmd command char.
      //! @param[in] cmd_size expected frame response size.
      //! @return true if communication successful, false otherwise.
      inline bool
      listen(Commands cmd, Sizes cmd_size)
      {
        // Device reset has no response.
        if (!cmd_size)
          return true;

        if (m_uart->hasNewData(m_args.data_tout) != IOMultiplexing::PRES_OK)
        {
          if (m_timer.overflow())
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
          return false;
        }

        // Read response.
        int rv = m_uart->read(m_bfr, c_bfr_size);
        m_tstamp = Clock::getSinceEpoch();

        if (rv <= 0)
        {
          if (m_timer.overflow())
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
          return false;
        }

        if (rv != cmd_size)
        {
          if (m_timer.overflow())
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
          return false;
        }

        // Check if we have a response to our query.
        if (m_bfr[0] != cmd)
        {
          if (m_timer.overflow())
            setEntityState(IMC::EntityState::ESTA_ERROR, Status::CODE_COM_ERROR);
          return false;
        }

        // Validate checksum.
        if (!validateChecksum(m_bfr, cmd_size))
          return false;

        return true;
      }

      //! Validate response checksum.
      //! @param[in] bfr buffer to be validated.
      //! @param[in] bfr_len length of the buffer.
      //! @return true if checksum validated, false otherwise.
      inline bool
      validateChecksum(const uint8_t* bfr, unsigned bfr_len)
      {
        unsigned frame_size = bfr_len - 2;

        // Compute checksum.
        uint16_t ccks = 0;
        for (unsigned i = 0; i < frame_size; ++i)
          ccks += bfr[i];

        uint16_t rcks = 0;
        ByteCopy::fromBE(rcks, bfr + frame_size);

        // Validate checksum.
        if (ccks == rcks)
          return true;

        return false;
      }

      //! Routine to run calibration proceedings.
      void
      runCalibration(void)
      {
        // See if vehicle has same hard iron calibration parameters.
        if (!isCalibrated())
        {
          // Set hard iron calibration parameters and reset device.
          if (!setHardIron())
          {
            err(DTR("failed to calibrate device"));
          }
          else
          {
            m_timer.reset();
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_CALIBRATING);
            debug(DTR("resetting device"));
            poll(CMD_DEVICE_RESET, CMD_DEVICE_RESET_SIZE, 0, 0);
          }
        }
      }

      //! Check if sensor has the same hard iron calibration parameters.
      //! @return true if the parameters are the same, false otherwise.
      bool
      isCalibrated(void)
      {
        m_uart->setMinimumRead(CMD_READ_EEPROM_SIZE);

        uint16_t hard_iron[c_num_addr] = {0};

        for (unsigned i = 0; i <= c_num_addr - 1; i++)
        {
          if (!poll(CMD_READ_EEPROM, CMD_READ_EEPROM_SIZE, m_addr[i], 0))
          {
            war(DTR("failed to read magnetic calibration parameters from device"));
            return false;
          }

          ByteCopy::fromBE(hard_iron[i], m_bfr + 1);
        }

        // Sensor magnetic calibration.
        uint32_t senCal[3] = {0};
        // Magnetic calibration in configuration.
        uint32_t cfgCal[3] = {0};

        for (unsigned i = 0; i <= 2; i++)
        {
          senCal[i] = hard_iron[i * 2 + 1] << 16 | hard_iron[i * 2];
          std::memcpy(&cfgCal[i], &m_args.calib_params[i], sizeof(uint32_t));

          if (senCal[i] != cfgCal[i])
          {
            war(DTR("different calibration parameters"));
            return false;
          }
        }

        return true;
      }

      //! Soft-reset device.
      void
      resetDevice(void)
      {
        uint8_t bfr[3];

        // Fill buffer.
        bfr[0] = CMD_DEVICE_RESET;
        bfr[1] = 0x9E;
        bfr[2] = 0x3A;

        // Reset device.
        m_uart->write((uint8_t*)&bfr, 3);
      }

      //! Request calibration parameters from device.
      //! @param[in] addr address.
      void
      requestCalibration(uint16_t addr)
      {
        uint8_t bfr[4];

        // Fill buffer.
        bfr[0] = CMD_READ_EEPROM;
        bfr[1] = 0x00;
        bfr[2] = (uint8_t)((addr & 0xff00) >> 8);
        bfr[3] = (uint8_t)(addr & 0x00ff);

        // Request calibration from device.
        m_uart->write((uint8_t*)&bfr, 4);
      }

      //! Set new hard iron calibration parameters.
      //! @return true if successful, false otherwise.
      bool
      setHardIron(void)
      {
        debug("Hard-Iron Calibration: %f | %f", m_args.calib_params[0], m_args.calib_params[1]);
        m_uart->setMinimumRead(CMD_WRITE_EEPROM_SIZE);

        for (unsigned i = 0; i <= c_num_addr / 3; i++)
        {
          uint32_t val;
          std::memcpy(&val, &m_args.calib_params[i], sizeof(uint32_t));

          if (!poll(CMD_WRITE_EEPROM, CMD_WRITE_EEPROM_SIZE, m_addr[i * 2], (uint16_t)(val & 0x0000ffff)))
            return false;

          if (!poll(CMD_WRITE_EEPROM, CMD_WRITE_EEPROM_SIZE, m_addr[i * 2 + 1], (uint16_t)((val & 0xffff0000) >> 16)))
            return false;
        }

        return true;
      }

      //! Send calibration frame to the device
      //! @param[in] addr EEPROM address.
      //! @param[in] value new calibration value.
      void
      calibrate(uint16_t addr, uint16_t value)
      {
        uint8_t bfr[8];

        // Fill buffer.
        bfr[0] = CMD_WRITE_EEPROM;
        bfr[1] = 0xC1;
        bfr[2] = 0x29;
        bfr[3] = 0x00;
        bfr[4] = (uint8_t)((addr & 0xff00) >> 8);
        bfr[5] = (uint8_t)(addr & 0x00ff);
        bfr[6] = (uint8_t)((value & 0xff00) >> 8);
        bfr[7] = (uint8_t)(value & 0x00ff);

        // Calibrate device.
        m_uart->write((uint8_t*)&bfr, 8);
      }

      //! Save parameters to configuration.
      void
      saveParameters(void)
      {
        m_pc.params.clear();

        IMC::Parameter p;
        p.section = getName();
        p.param = m_param;
        p.value = String::str("%0.6f, %0.6f, %0.6f", m_args.calib_params[0],
                              m_args.calib_params[1], m_args.calib_params[2]);

        m_pc.params.push_back(p);

        dispatch(m_pc);
      }

      //! Main task.
      void
      task(void)
      {
        // Check for incoming messages.
        consumeMessages();

        if (!poll(CMD_DATA, CMD_DATA_SIZE, 0, 0))
          return;

        // Set timestamps so we have realistic times.
        m_euler.setTimeStamp(m_tstamp);
        m_accel.setTimeStamp(m_tstamp);
        m_agvel.setTimeStamp(m_tstamp);
        m_magfield.setTimeStamp(m_tstamp);

        // Extract acceleration.
        fp32_t accel[3] = {0};
        ByteCopy::fromBE(accel[0], m_bfr + 1);
        ByteCopy::fromBE(accel[1], m_bfr + 5);
        ByteCopy::fromBE(accel[2], m_bfr + 9);
        m_accel.x = Math::c_gravity * accel[0];
        m_accel.y = Math::c_gravity * accel[1];
        m_accel.z = Math::c_gravity * accel[2];

        // Extract angular rates.
        fp32_t arate[3] = {0};
        ByteCopy::fromBE(arate[0], m_bfr + 13);
        ByteCopy::fromBE(arate[1], m_bfr + 17);
        ByteCopy::fromBE(arate[2], m_bfr + 21);
        m_agvel.x = arate[0];
        m_agvel.y = arate[1];
        m_agvel.z = arate[2];

        // Extract magnetic field.
        fp32_t mfield[3] = {0};
        ByteCopy::fromBE(mfield[0], m_bfr + 25);
        ByteCopy::fromBE(mfield[1], m_bfr + 29);
        ByteCopy::fromBE(mfield[2], m_bfr + 33);
        m_magfield.x = mfield[0];
        m_magfield.y = mfield[1];
        m_magfield.z = mfield[2];

        // Extract orientation matrix and compute Euler angles.
        fp32_t omtrx[5] = {0};
        ByteCopy::fromBE(omtrx[0], m_bfr + 37); // M11
        ByteCopy::fromBE(omtrx[1], m_bfr + 41); // M12
        ByteCopy::fromBE(omtrx[2], m_bfr + 45); // M13
        ByteCopy::fromBE(omtrx[3], m_bfr + 57); // M23
        ByteCopy::fromBE(omtrx[4], m_bfr + 69); // M33
        m_euler.phi = static_cast<double>(std::atan2(omtrx[3], omtrx[4]));
        m_euler.theta = static_cast<double>(std::asin(-omtrx[2]));
        m_euler.psi = static_cast<double>(std::atan2(omtrx[1], omtrx[0]));
        m_euler.psi_magnetic = m_euler.psi;

        // Extract time.
        uint32_t timer = 0;
        ByteCopy::fromBE(timer, m_bfr + CMD_DATA_SIZE - 6);
        m_euler.time = timer / 62500.0;
        m_accel.time = m_euler.time;
        m_agvel.time = m_euler.time;
        m_magfield.time = m_euler.time;

        // Dispatch messages.
        dispatch(m_euler, DF_KEEP_TIME);
        dispatch(m_accel, DF_KEEP_TIME);
        dispatch(m_agvel, DF_KEEP_TIME);
        dispatch(m_magfield, DF_KEEP_TIME);

        // Clear entity state.
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
      }
    };
  }
}

DUNE_TASK
