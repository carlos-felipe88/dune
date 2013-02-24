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
// Author: Pedro Calado                                                     *
//***************************************************************************

// ISO C++ 98 headers.
#include <cmath>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Maneuver
{
  namespace FollowTrajectory
  {
    using DUNE_NAMESPACES;

    struct Arguments
    {
      bool mps_control;
      double timegap;
      double max_speed;
      double min_speed;
      double max_actuation;
      double min_actuation;
      double act_step;
    };

    struct Task: public Maneuvers::FollowTrajectory
    {
      //! Flag on whether or not the time in the waypoints shall be disregarded or not
      bool m_ignore_time;
      //! FollowTrajectory maneuver's speed
      IMC::DesiredSpeed m_maneuver_speed;
      //! Last actuation sent to the motor (used to compute actuation step
      double m_last_actuation;
      //! Time value to be used as reference
      double m_zero_time;
      //! Path completion flag
      bool m_done;
      //! Number of current waypoint number
      int m_curr;
      //! Task Arguments
      Arguments m_args;

      Task(const std::string& name, DUNE::Tasks::Context& ctx):
        Maneuvers::FollowTrajectory(name, ctx)
      {
        param("MPS Control", m_args.mps_control)
        .defaultValue("true")
        .description("Vehicle has mps speed control");

        param("Time Gap", m_args.timegap)
        .defaultValue("2.0")
        .description("Tolerance in time when reaching a new waypoint");

        param("Max Speed MPS", m_args.max_speed)
        .defaultValue("2.0")
        .description("Vehicle's maximum longitudinal speed");

        param("Min Speed MPS", m_args.min_speed)
        .defaultValue("1.0")
        .description("Vehicle's minimum longitudinal speed");

        param("Max Actuation", m_args.max_actuation)
        .defaultValue("100")
        .description("");

        param("Min Actuation", m_args.min_actuation)
        .defaultValue("50")
        .description("Vehicle's minimum actuation for this maneuver");

        param("Max Step Actuation", m_args.act_step)
        .defaultValue("5")
        .description("Maximum step in actuation applicable to compensate for delay/advance in trajectory");
      }

      void
      onUpdateParameters(void)
      {
        Maneuvers::FollowTrajectory::onUpdateParameters();
      }

      void
      onInit(const IMC::FollowTrajectory* maneuver)
      {
        int n = trajectory_points();

        m_maneuver_speed.value = maneuver->speed;
        m_maneuver_speed.speed_units = maneuver->speed_units;

        m_ignore_time = false;

        // if at least one of the waypoints has a time lower than zero
        // then trajectory's time constraints will be disregarded
        for (int i = 0; i < n; i++)
        {
          if (point(i).t < 0)
          {
            m_ignore_time = true;
            break;
          }
        }

        if (m_ignore_time)
          inf(DTR("disregarding trajectory's time constraints"));

        // first waypoint in trajectory should have the time 0 (zero)
        if (!isFeasible() && !m_ignore_time)
        {
          signalError(DTR("provided trajectory is not feasible by the current vehicle!"));
          return;
        }

        // Initialize member variables at zero
        m_zero_time = Clock::get();
        m_done = false;
        m_curr = 0;
        m_last_actuation = 0;
      }

      void
      onPathCompletion(void)
      {
        m_done = true;
      }

      void
      step(const IMC::EstimatedState& estate)
      {
        (void)estate;

        if (m_curr != 0 && !m_done)
          return;

        m_done = false;

        if (m_curr == 0)
          m_zero_time = Clock::get();

        if (m_curr + 1 == trajectory_points())
        {
          signalCompletion();
          return;
        }

        // throw new ground speed
        // the chosen ground speed is simply the distance between the points
        // divided by the time difference between them

        // if time constraints are ignored just throw the maneuver speed
        if (m_ignore_time)
        {
          desiredSpeed(m_maneuver_speed.value, m_maneuver_speed.speed_units);
        }
        else
        {
          // if this is the first waypoint
          if (!m_curr)
          {
            double value = dist(m_curr, m_curr + 1) / (point(m_curr + 1).t - point(m_curr).t);

            if (m_args.mps_control)
            {
              desiredSpeed(value, IMC::SUNITS_METERS_PS);
            }
            else
            {
              double actuation = Math::linearInterpolation(LinIntParam<double>(m_args.min_actuation,
                                                                               m_args.max_actuation,
                                                                               m_args.min_speed, m_args.max_speed,
                                                                               value));
              desiredSpeed(actuation, IMC::SUNITS_PERCENTAGE);
              m_last_actuation = actuation;
            }
          }
          else
          {
            double delay = Clock::get() - m_zero_time - point(m_curr).t;

            double value;

            // test if the delay/advance is between the desired bounds
            if (std::fabs(delay) <= m_args.timegap)
              value = dist(m_curr, m_curr + 1) / (point(m_curr + 1).t - point(m_curr).t);
            else // if not then compute proper speed to handle delay/advance
            {
              if (point(m_curr + 1).t - point(m_curr).t - delay <= 0)
                value = m_args.max_speed;
              else
                value = dist(m_curr, m_curr + 1) / (point(m_curr + 1).t - point(m_curr).t - delay);
            }

            value = trimValue(value, m_args.min_speed, m_args.max_speed);

            if (m_args.mps_control)
              desiredSpeed(value, IMC::SUNITS_METERS_PS);
            else
            {
              // use interpolation
              Math::LinIntParam<double> lip(m_args.min_actuation, m_args.max_actuation,
                                            m_args.min_speed, m_args.max_speed,
                                            value);
              double actuation = Math::linearInterpolation(lip);
              actuation = trimValue(actuation,
                                    m_last_actuation - m_args.act_step,
                                    m_last_actuation + m_args.act_step);

              desiredSpeed(actuation, IMC::SUNITS_PERCENTAGE);
              m_last_actuation = actuation;
            }
          }
        }

        desiredPath(point(m_curr), point(m_curr + 1));

        ++m_curr;
      }

      //! Function for testing the trajectory's feasibility
      bool
      isFeasible(void)
      {
        int n = trajectory_points();

        if (m_args.mps_control) // test feasibility and possible anomalies in trajectory
        {
          // testing if the first waypoint is timed at zero
          if (point(0).t != 0.0)
            return false;

          // testing for two dimensional trajectories
          for (int i = 1; i < n; i++)
          {
            double required_speed = dist(i, i - 1) / (point(i).t - point(i - 1).t);

            if (required_speed > m_args.max_speed || point(i).t - point(i - 1).t < 0)
              return false;
          }
        }

        return true;
      }

      //! Function for computing the horizontal distance between two points in the trajectory
      inline double
      dist(int p1, int p2)
      {
        return Math::norm((point(p1).x - point(p2).x), (point(p1).y - point(p2).y));
      }
    };
  }
}

DUNE_TASK
