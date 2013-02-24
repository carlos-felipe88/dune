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
// Author: José Braga                                                       *
//***************************************************************************

//***************************************************************************
// References:
//  "New approach to coarse alignment,"
//  Dr. Leonid Schimelevich and Dr. Rahel Naor,
//  Position Location and Navigation Symposium,
//  IEEE 1996, 22-26 Apr 1996
//
//  "Comparison of initial alignment methods for SINS,"
//  Hongyu Zhao, Hong Shang, Zhelong Wang, Ming Jiang,
//  Intelligent Control and Automation (WCICA),
//  2011, 21-25 June 2011
//***************************************************************************

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Navigation
{
  namespace General
  {
    //! This task performs static heading calibration
    //! using inertial rotational measurements from
    //! an Inertial Measurement Unit such as the
    //! Honeywell HG 1700-AG58 unit.
    //!
    //! @author José Braga
    namespace Alignment
    {
      using DUNE_NAMESPACES;

      //! %Task arguments.
      struct Arguments
      {
        //! IMU entity label.
        std::string elabel_imu;
        //! Minimum calibration time.
        float time;
        //! Delay time to accept data.
        float delay;
        //! Number of samples to average accelerations.
        int avg_samples;
        //! Minimum standard deviation value to detect motion.
        float std;
      };

      struct Task: public DUNE::Tasks::Task
      {
        //! Device is calibrating.
        bool m_calibrating;
        //! Device calibrated.
        bool m_calibrated;
        //! GpsFix received.
        bool m_gps;
        //! Euler Angles calibrated.
        IMC::EulerAngles m_euler;
        //! Moving average for acceleration vector.
        MovingAverage<double>* m_avg_acc;
        //! IMU entity id.
        unsigned m_imu_eid;
        //! Vehicle WGS-84 latitude.
        double m_lat;
        //! Accumulator for x-axis angular velocity.
        double m_av_x;
        //! Accumulator for y-axis angular velocity.
        double m_av_y;
        //! Accumulator for z-axis angular velocity.
        double m_av_z;
        //! Number of Angular Velocity readings.
        unsigned m_av_readings;
        //! Accumulator for x-axis acceleration.
        double m_acc_x;
        //! Accumulator for y-axis acceleration.
        double m_acc_y;
        //! Accumulator for z-axis acceleration.
        double m_acc_z;
        //! Number of Acceleration readings.
        unsigned m_acc_readings;
        //! Minimum calibration time.
        Time::Counter<float> m_time;
        //! Initial delay time.
        Time::Counter<float> m_delay;
        //! Task arguments.
        Arguments m_args;

        Task(const std::string& name, Tasks::Context& ctx):
          DUNE::Tasks::Task(name, ctx),
          m_calibrating(false),
          m_gps(false),
          m_avg_acc(NULL)
        {
          // Definition of configuration parameters.
          param("Entity Label - IMU", m_args.elabel_imu)
          .defaultValue("IMU")
          .description("Entity label of the IMU");

          param("Calibration Time", m_args.time)
          .units(Units::Second)
          .minimumValue("20")
          .defaultValue("20")
          .description("Minimum amount of time that the vehicle has to perform static orientation calibration");

          param("Delay Time", m_args.delay)
          .units(Units::Second)
          .minimumValue("5")
          .defaultValue("5")
          .description("Delay time to avoid using initial noisier IMU booting data.");

          param("Moving Average Samples", m_args.avg_samples)
          .defaultValue("10")
          .description("Number of moving average samples to smooth acceleration vector");

          param("Minimum Std Dev for Motion Detection", m_args.std)
          .defaultValue("0.2")
          .description("Minimum standard deviation value for motion detection");

          // Initialize entity state.
          setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_IDLE);
          m_calibrated = false;

          bind<IMC::Acceleration>(this);
          bind<IMC::AngularVelocity>(this);
          bind<IMC::EntityControl>(this);
          bind<IMC::GpsFix>(this);
          bind<IMC::VehicleMedium>(this);
        }

        void
        onUpdateParameters(void)
        {
          m_time.setTop(m_args.time + m_args.delay);
          m_delay.setTop(m_args.delay);
        }

        void
        onEntityResolution(void)
        {
          try
          {
            m_imu_eid = resolveEntity(m_args.elabel_imu);
          }
          catch (std::runtime_error& e)
          {
            war(DTR("failed to resolve entity '%s': %s"), m_args.elabel_imu.c_str(), e.what());
            m_imu_eid = UINT_MAX;
          }
        }

        void
        onResourceInitialization(void)
        {
          m_avg_acc = new MovingAverage<double>(m_args.avg_samples);
          reset();
        }

        void
        onResourceRelease(void)
        {
          Memory::clear(m_avg_acc);
        }

        void
        consume(const IMC::Acceleration* msg)
        {
          if (msg->getSourceEntity() != m_imu_eid)
            return;

          if (!m_calibrating)
            return;

          if (!m_delay.overflow())
            return;

          double accel = std::sqrt(msg->x * msg->x + msg->y * msg->y + msg->z * msg->z);

          m_avg_acc->update(accel);

          if (m_avg_acc->stdev() > m_args.std)
          {
            setEntityState(IMC::EntityState::ESTA_FAULT, DTR("motion detected"));
            return;
          }
          else
          {
            if (getEntityState() == IMC::EntityState::ESTA_FAULT)
              setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_CALIBRATING);
          }

          m_acc_x += msg->x;
          m_acc_y += msg->y;
          m_acc_z += msg->z;
          m_acc_readings++;
        }

        void
        consume(const IMC::AngularVelocity* msg)
        {
          if (msg->getSourceEntity() != m_imu_eid)
            return;

          if (!m_calibrating)
            return;

          if (getEntityState() == IMC::EntityState::ESTA_FAULT)
            return;

          if (getEntityState() == IMC::EntityState::ESTA_BOOT && m_gps)
          {
            reset();
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_CALIBRATING);
          }

          if (!m_delay.overflow())
            return;

          m_av_x += msg->x;
          m_av_y += msg->y;
          m_av_z += msg->z;
          m_av_readings++;

          if (m_time.overflow())
          {
            calibrate();
            dispatch(m_euler);
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_CALIBRATED);
            m_calibrated = true;
            m_calibrating = false;
            reset();
          }
        }

        void
        consume(const IMC::EntityControl* msg)
        {
          if (msg->getDestinationEntity() != m_imu_eid)
            return;

          if (msg->op == IMC::EntityControl::ECO_ACTIVATE)
          {
            if (!m_calibrating && !m_calibrated)
            {
              setEntityState(IMC::EntityState::ESTA_BOOT, Status::CODE_INIT);
              m_calibrating = true;
            }
          }
          else
          {
            setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_IDLE);
            m_calibrating = false;
            m_calibrated = false;
          }
        }

        void
        consume(const IMC::GpsFix* msg)
        {
          if (msg->validity & IMC::GpsFix::GFV_VALID_POS)
          {
            m_gps = true;
            m_lat = msg->lat;
          }
        }

        void
        consume(const IMC::VehicleMedium* msg)
        {
          if (msg->medium == IMC::VehicleMedium::VM_WATER ||
              msg->medium == IMC::VehicleMedium::VM_UNDERWATER)
            m_calibrating = false;
        }

        //! Reset internal parameters.
        void
        reset(void)
        {
          m_time.reset();
          m_delay.reset();
          m_av_readings = 0;
          m_acc_readings = 0;
          m_av_x = 0.0;
          m_av_y = 0.0;
          m_av_z = 0.0;
          m_acc_x = 0.0;
          m_acc_y = 0.0;
          m_acc_z = 0.0;
        }

        //! Calibrate orientation by means of computing
        //! current compass heading bias.
        void
        calibrate(void)
        {
          if (!m_av_readings || !m_acc_readings)
            return;

          Matrix a;
          Matrix w;
          a.resizeAndFill(3, 1, 0.0);
          w.resizeAndFill(3, 1, 0.0);

          a(0) = m_acc_x / m_acc_readings;
          a(1) = m_acc_y / m_acc_readings;
          a(2) = m_acc_z / m_acc_readings;

          w(0) = m_av_x / m_av_readings;
          w(1) = m_av_y / m_av_readings;
          w(2) = m_av_z / m_av_readings;

          // Coarse alignment.
          Matrix dcm;
          Matrix dcm_vector[3];
          dcm.resizeAndFill(3, 3, 0.0);

          for (unsigned i = 0; i < 3; i++)
            dcm_vector[i].resizeAndFill(3, 1, 0.0);

          // Fill rotation matrix vectors.
          dcm_vector[0] = Matrix::cross(Matrix::cross(a, w), a) / Matrix::cross(Matrix::cross(a, w), a).norm_2();
          dcm_vector[1] = Matrix::cross(-a, w) / Matrix::cross(-a, w).norm_2();
          dcm_vector[2] = - a / a.norm_2();

          for (unsigned i = 0; i < 3; i++)
            dcm.set(0, 2, i, i, dcm_vector[i]);

          // Convert DCM to Euler Angles.
          Matrix euler = transpose(dcm).toEulerAngles();

          debug("Result: %f | %f | %f",
                Angles::degrees(euler(0)),
                Angles::degrees(euler(1)),
                Angles::degrees(euler(2)));

          m_euler.phi = euler(0);
          m_euler.theta = euler(1);
          m_euler.psi = euler(2);
        }

        //! Evaluate orientation performance.
        double
        evaluate(void)
        {
          // @todo
          return 0;
        }

        void
        onMain(void)
        {
          while (!stopping())
          {
            waitForMessages(1.0);
          }
        }
      };
    }
  }
}

DUNE_TASK
