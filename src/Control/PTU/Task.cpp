//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Ricardo Bencatel                                                 *
// Author: Joel Cardoso                                                     *
//***************************************************************************

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Control
{
  namespace PTU
  {
    using DUNE_NAMESPACES;

    struct Task: public DUNE::Tasks::Task
    {
      // PTU position and angles.
      IMC::EstimatedState m_estate,
                          m_estate_ref;
      // Remote action to control PTU pan and tilt.
      IMC::RemoteActions m_ra;
      bool m_es_flag;
      bool m_trg_flag;
      bool m_sensor_flag;
      Matrix m_ptu_pos;
      Matrix m_ptu_rot;
      Matrix m_sensor_ang;
      // Target state.
      Matrix m_trg_pos;
      Matrix m_trg_vel;
      // Parameters.
      std::string m_trg_name;
      unsigned m_trg_id;
      bool m_ptu_ctrl_mode, m_ptu_fixed;
      float m_panrt_gain, m_tiltrt_gain, m_ptu_lat, m_ptu_lon, m_ptu_height;

      Task(const std::string& name, Tasks::Context& ctx):
        DUNE::Tasks::Task(name, ctx),
        m_es_flag(false),
        m_trg_flag(false),
        m_sensor_flag(false),
        m_ptu_pos(3,1),
        m_ptu_rot(3,3),
        m_sensor_ang(3,1),
        m_trg_pos(3,1),
        m_trg_vel(3,1)
      {
        // == Parameters ==
        // Identificador do veículo escrito directamente ini
        // é necessário usar o:
        // bus().resolver().resolve("alfa-007")
        // consultar src/DUNE/IMC/AddressResolver.hpp
        // ou
        // src/DUNE/Daemon.cpp
        // retrieve known imc addresses
        // ou
        // src/DUNE/Parsers/Config.hpp
        // imc-addresses.ini
        // ----
//        param(m_my_trg)
//        .name("Vehicle Address")
//        .description("Vehicle to be tracked")
//        .defaultValue("0x0c27");

        param("Target Vehicle", m_trg_name)
        .description("Vehicle to be tracked")
        .defaultValue("alfa-07");

        param("Control Mode", m_ptu_ctrl_mode)
        .description("PTU control mode (angular/angular rate)")
        .defaultValue("0");

        param("Pan Gain", m_panrt_gain)
        .description("Pan gain for PTU angular rate control mode")
        .defaultValue("0.5");

        param("Tilt Gain", m_tiltrt_gain)
        .description("Tilt gain for PTU angular rate control mode")
        .defaultValue("0.5");

        param("Fixed PTU", m_ptu_fixed)
        .description("Flag for fixed ground PTU position")
        .defaultValue("true");

        param("Latitude", m_ptu_lat)
        .description("Fixed ground PTU position latitude")
        .defaultValue("39.087752");

        param("Longitude", m_ptu_lon)
        .description("Fixed ground PTU position longitude")
        .defaultValue("-8.9620989");

        param("Height", m_ptu_height)
        .description("Fixed ground PTU position height")
        .defaultValue("85");

        // Register consumers.
        bind<IMC::EstimatedState>(this);
        bind<IMC::EulerAngles>(this);
        bind<IMC::Target>(this);
      }

      ~Task(void)
      {
        Task::onResourceRelease();
      }

      void
      onUpdateParameters(void)
      {
        m_trg_id = resolveSystemName(m_trg_name);
        inf("Target name is %s, with ID %d",m_trg_name.c_str(),m_trg_id);
      }

      void
      consume(const IMC::EstimatedState* msg)
      {
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);

        inf("Estimated State arrived from %d", msg->getSource());
        Matrix rel_pos_ned = Matrix(3,1);
        Matrix rel_vel_ned = Matrix(3,1);
        Matrix tmp_ptu_rot;
        Matrix rel_pos_body = Matrix(3,1);
        Matrix rel_vel_body = Matrix(3,1);
        float cmd_pan,
              des_pan_rate,
              cmd_pan_rate,
              cmd_tilt,
              des_tilt_rate,
              cmd_tilt_rate,
              tmp_hor_dist_sq,
              tmp_hor_dist;
        std::stringstream ss;

        // Self estimated state for target x, y and z position computation
//        if (getEntityId() == msg->getSourceEntity())
//      {
//        m_estate = *msg;
//        m_es_flag = true;
//      }

        // Target estimated state source selection
        if (m_trg_id == msg->getSource())
        {
          m_estate_ref = *msg;
          m_trg_flag = true;

          m_trg_pos(0,0) = msg->x;
          m_trg_pos(1,0) = msg->y;
          m_trg_pos(2,0) = msg->z;

          m_trg_vel(0,0) = msg->vx;
          m_trg_vel(1,0) = msg->vy;
          m_trg_vel(2,0) = msg->vz;
        }

        // Orientation computation.
        if (m_trg_flag)
        {
          // Check if it is expecting a fixed predefined position of an estimated state
          if (m_ptu_fixed)
          {
            float deg2rad = Math::c_pi / 180;

            m_ptu_lat *=  deg2rad;
            m_ptu_lon *=  deg2rad;

            WGS84::displacement(m_estate_ref.lat, m_estate_ref.lon, -m_estate_ref.depth, m_ptu_lat, m_ptu_lon, m_ptu_height, &m_ptu_pos(0,0), &m_ptu_pos(1,0));

            // Relative position.
            rel_pos_ned(0,0) = (m_trg_pos(0,0) - m_ptu_pos(0,0));
            rel_pos_ned(1,0) = (m_trg_pos(1,0) - m_ptu_pos(1,0));
            rel_pos_ned(2,0) = (m_trg_pos(2,0) - m_ptu_height - m_estate.depth);

            // Relative velocity.
            rel_vel_ned(0,0) = m_trg_vel(0,0);
            rel_vel_ned(1,0) = m_trg_vel(1,0);
            rel_vel_ned(2,0) = m_trg_vel(2,0);
          }
          else
          {
            if (getEntityId() == msg->getSourceEntity())
            {
              // Relative position.
              rel_pos_ned(0,0) = (m_trg_pos(0,0) - msg->x);
              rel_pos_ned(1,0) = (m_trg_pos(1,0) - msg->y);
              rel_pos_ned(2,0) = (m_trg_pos(2,0) - msg->z);

              // Relative velocity.
              rel_vel_ned(0,0) = (m_trg_vel(0,0) - msg->vx);
              rel_vel_ned(1,0) = (m_trg_vel(1,0) - msg->vy);
              rel_vel_ned(2,0) = (m_trg_vel(2,0) - msg->vz);
            }
          }

          // Relative position reference transformation.
          Matrix ptu_euler = Matrix(3,1);
          ptu_euler(0,0) = msg->phi;
          ptu_euler(1,0) = msg->theta;
          ptu_euler(2,0) = msg->psi;
          tmp_ptu_rot = Matrix(ptu_euler.toDCM());
          m_ptu_rot = transpose(tmp_ptu_rot);
          rel_pos_body = m_ptu_rot * rel_pos_ned;

          // Pan and tilt computation.
          tmp_hor_dist_sq = (rel_pos_body(0,0) * rel_pos_body(0,0)) + (rel_pos_body(1,0)*rel_pos_body(1,0));
          cmd_pan = std::atan2(rel_pos_body(1,0), rel_pos_body(2,0));
          cmd_tilt = std::atan(-rel_pos_body(2,0)/std::sqrt(tmp_hor_dist_sq)); // BENCATEL - Verificar se indice (2,0) está certo - era (3,0) que não existe
          // Generating PTU commands.
          m_ra.setSourceEntity(getEntityId());
          if (m_ptu_ctrl_mode == 0)
          {
            ss << "Pan=" << cmd_pan << ";Tilt=" << cmd_tilt << ";";
            m_ra.actions = ss.str();
            dispatch(m_ra);

            debug("PTU in angular control mode");
            debug("Created tuplelist %s ", m_ra.actions.c_str());
          }
          else
          {
            // Relative velocity and horizontal distance.
            rel_vel_body = (m_ptu_rot * rel_vel_ned);
            tmp_hor_dist = std::sqrt(tmp_hor_dist_sq);
            // Target apparent pan and tilt rates computation.
            des_pan_rate = (rel_pos_body(0,0)*rel_vel_body(1,0) - rel_pos_body(1,0)*rel_vel_body(0,0))/tmp_hor_dist_sq;
            des_tilt_rate = (rel_pos_body(3,0)*(rel_pos_body(0,0)*rel_vel_body(0,0)+ rel_pos_body(1,0)*rel_vel_body(1,0))/tmp_hor_dist -
                             tmp_hor_dist*rel_vel_body(2,0))/std::sqrt(tmp_hor_dist_sq + rel_pos_body(2,0)*rel_pos_body(2,0));

            // Pan and tilt rate commands computation.
            cmd_pan_rate = des_pan_rate + m_panrt_gain*(cmd_pan - m_sensor_ang(0,0));
            cmd_tilt_rate = des_tilt_rate + m_tiltrt_gain*(cmd_tilt - m_sensor_ang(1,0));

            ss << "PanRate=" << cmd_pan_rate << ";TiltRate=" << cmd_tilt_rate << ";";
            m_ra.actions = ss.str();
            dispatch(m_ra);

            debug("PTU in angular rate control mode");
            debug("Created tuplelist %s ", m_ra.actions.c_str());
           }
        }
      }

      void
      consume(const IMC::Target* msg)
      {
        if (m_es_flag)
        {
          m_estate_ref = m_estate;
          m_trg_flag = true;

          WGS84::displacement(m_estate.lat, m_estate.lon, -m_estate.depth, msg->lat, msg->lon, -msg->z, &m_trg_pos(0,0), &m_trg_pos(1,0));
          m_trg_pos(2,0) = msg->z + m_estate.depth;
        }
      }

      void
      consume(const IMC::EulerAngles* msg)
      {
        inf("EulerAngles arrived");
        // Pan & Tilt angles: Sensor orientation relative to the PTU fixation
        // Used in case of angular velocity control
        m_sensor_flag = true;
        m_sensor_ang(0,0) = msg->phi;
        m_sensor_ang(1,0) = msg->theta;
        (void)msg;
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

DUNE_TASK
