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
#include <string>
#include <vector>
#include <set>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Supervisors
{
  namespace Vehicle
  {
    using DUNE_NAMESPACES;

    //! State description strings
    static const char* c_state_desc[] = {DTR_RT("SERVICE"), DTR_RT("CALIBRATION"),
                                         DTR_RT("ERROR"), DTR_RT("MANEUVERING"),
                                         DTR_RT("EXTERNAL CONTROL")};
    //! Vehicle command description strings
    static const char* c_cmd_desc[] =
    {"maneuver start", "maneuver stop", "calibration start", "calibration stop"};
    //! Printing errors period
    static const float c_error_period = 2.0;
    //! Maneuver request timeout
    static const float c_man_timeout = 1.0;

    struct Arguments
    {
      //! Relevant entities when performing a safe plan.
      std::vector<std::string> safe_ents;
    };

    struct Task: public DUNE::Tasks::Periodic
    {
      //! Timer to wait for calibration and maneuver requests.
      float m_switch_time;
      //! Currently performing a safe plan.
      bool m_in_safe_plan;
      //! Counter for printing errors
      Time::Counter<float> m_err_timer;
      //! Calibration message.
      IMC::Calibration m_calibration;
      //! Vehicle command message.
      IMC::VehicleCommand m_vc_reply;
      //! Vehicle state message.
      IMC::VehicleState m_vs;
      //! Stop maneuver message.
      IMC::StopManeuver m_stop;
      //! Idle maneuver message.
      IMC::IdleManeuver m_idle;
      //! Control loops last reference time
      float m_scope_ref;
      //! Task arguments.
      Arguments m_args;

      Task(const std::string& name, Tasks::Context& ctx):
        Tasks::Periodic(name, ctx),
        m_switch_time(-1.0),
        m_in_safe_plan(false),
        m_scope_ref(0.0)
      {
        param("Safe Entities", m_args.safe_ents)
        .defaultValue("")
        .description("Relevant entities when performing a safe plan");

        bind<IMC::Abort>(this);
        bind<IMC::ControlLoops>(this);
        bind<IMC::EntityMonitoringState>(this);
        bind<IMC::ManeuverControlState>(this);
        bind<IMC::VehicleCommand>(this);
        bind<IMC::PlanControl>(this);
      }

      void
      onResourceInitialization(void)
      {
        setInitialState();
        m_err_timer.setTop(c_error_period);
        m_idle.duration = 0;
      }

      void
      setInitialState(void)
      {
        // Initialize entity state.
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);

        m_vs.op_mode = IMC::VehicleState::VS_SERVICE;
        m_vs.maneuver_type = 0xFFFF;
        m_vs.maneuver_stime = -1;
        m_vs.maneuver_eta = 0xFFFF;
        m_vs.error_ents.clear();
        m_vs.error_count = 0;
        m_vs.flags = 0;
        m_vs.last_error.clear();
        m_vs.last_error_time = -1;
        m_vs.control_loops = 0;
      }

      void
      consume(const IMC::Abort* msg)
      {
        (void)msg;

        m_vs.last_error = DTR("got abort request");
        m_vs.last_error_time = Clock::getSinceEpoch();
        err("%s", m_vs.last_error.c_str());

        if (!errorMode())
        {
          reset();

          if (!externalMode() || !nonOverridableLoops())
            changeMode(IMC::VehicleState::VS_SERVICE);
        }
      }

      void
      consume(const IMC::ControlLoops* msg)
      {
        // If this scope is obsolete, ignore message
        if (msg->scope_ref < m_scope_ref)
          return;

        m_scope_ref = msg->scope_ref;

        uint32_t was = m_vs.control_loops;

        if (msg->enable == IMC::ControlLoops::CL_ENABLE)
        {
          m_vs.control_loops |= msg->mask;

          if (!was && m_vs.control_loops)
            onEnabledControlLoops();
        }
        else
        {
          m_vs.control_loops &= ~msg->mask;

          if (was && !m_vs.control_loops)
            onDisabledControlLoops();
        }
      }

      void
      onEnabledControlLoops(void)
      {
        debug("some control loops are enabled now");

        switch (m_vs.op_mode)
        {
          case IMC::VehicleState::VS_SERVICE:
            changeMode(IMC::VehicleState::VS_EXTERNAL);
            break;
          case IMC::VehicleState::VS_ERROR:
            if (nonOverridableLoops())
              changeMode(IMC::VehicleState::VS_EXTERNAL);
            else
              reset();  // try to disable the control loops
            break;
          default:
            break; // ignore
        }
      }

      void
      onDisabledControlLoops(void)
      {
        debug("no control loops are enabled now");

        if (externalMode())
          changeMode(IMC::VehicleState::VS_SERVICE);

        // ignore otherwise
      }

      void
      changeMode(IMC::VehicleState::OperationModeEnum s, IMC::Message* maneuver = 0)
      {
        if (m_vs.op_mode != s)
        {
          if (s == IMC::VehicleState::VS_SERVICE && entityError())
            s = IMC::VehicleState::VS_ERROR;

          m_vs.op_mode = s;

          war(DTR("now in '%s' mode"), DTR(c_state_desc[s]));

          if (!maneuverMode())
          {
            m_vs.maneuver_type = 0xFFFF;
            m_vs.maneuver_stime = -1;
            m_vs.maneuver_eta = 0xFFFF;
            m_vs.flags &= ~IMC::VehicleState::VFLG_MANEUVER_DONE;
          }
        }

        if (maneuverMode())
        {
          dispatch(maneuver);
          m_vs.maneuver_stime = maneuver->getTimeStamp();
          m_vs.maneuver_type = maneuver->getId();
          m_vs.maneuver_eta = 0xFFFF;
          m_vs.last_error.clear();
          m_vs.last_error_time = -1;
          m_vs.flags &= ~IMC::VehicleState::VFLG_MANEUVER_DONE;
        }

        m_switch_time = -1.0;
        dispatch(m_vs);
      }

      void
      consume(const IMC::EntityMonitoringState* msg)
      {
        uint8_t prev_count = m_vs.error_count;

        m_vs.error_count = msg->ccount + msg->ecount;

        if (m_vs.error_count && msg->last_error_time > m_vs.last_error_time)
        {
          m_vs.last_error = msg->last_error;
          m_vs.last_error_time = msg->last_error_time;
        }

        m_vs.error_ents = "";

        if (msg->ccount)
          m_vs.error_ents = msg->cnames;

        if (msg->ecount)
          m_vs.error_ents += (msg->ccount ? "," : "") + msg->enames;

        if (prev_count && !m_vs.error_count)
        {
          war(DTR("entity errors cleared"));
        }
        else if ((prev_count != m_vs.error_count) && m_err_timer.overflow())
        {
          war(DTR("vehicle errors: %s"), m_vs.error_ents.c_str());
          m_err_timer.reset();
        }

        if (errorMode())
        {
          if (!m_vs.error_count)
            changeMode(IMC::VehicleState::VS_SERVICE);
          return;
        }

        // External/maneuver mode
        if (externalMode() || maneuverMode())
        {
          if (entityError() && !nonOverridableLoops() && !teleoperationOn())
          {
            reset();
            changeMode(IMC::VehicleState::VS_ERROR);
          }
          return;
        }

        // Otherwise (SERVICE, MANEUVER modes)
        if (entityError() && !calibrationMode())
        {
          reset();
          changeMode(IMC::VehicleState::VS_ERROR);
        }
      }

      void
      consume(const IMC::ManeuverControlState* msg)
      {
        if (msg->getSource() != getSystemId())
          return;

        if (!maneuverMode())
          return;

        switch (msg->state)
        {
          case IMC::ManeuverControlState::MCS_EXECUTING:
            if (msg->eta != m_vs.maneuver_eta)
            {
              m_vs.maneuver_eta = msg->eta;
              dispatch(m_vs);
            }
            break;
          case IMC::ManeuverControlState::MCS_DONE:
            debug("%s maneuver done",
                  IMC::Factory::getAbbrevFromId(m_vs.maneuver_type).c_str());
            m_vs.maneuver_eta = 0;
            m_vs.flags |= IMC::VehicleState::VFLG_MANEUVER_DONE;
            dispatch(m_vs);
            // start timer
            m_switch_time = Clock::get();
            break;
          case IMC::ManeuverControlState::MCS_ERROR:
            m_vs.last_error = IMC::Factory::getAbbrevFromId(m_vs.maneuver_type)
            + DTR(" maneuver error: ") + msg->info;
            m_vs.last_error_time = msg->getTimeStamp();
            debug("%s", m_vs.last_error.c_str());
            changeMode(IMC::VehicleState::VS_SERVICE);
            reset();
            break;
        }
      }

      void
      consume(const IMC::PlanControl* msg)
      {
        if ((msg->type == IMC::PlanControl::PC_REQUEST) &&
            (msg->op == IMC::PlanControl::PC_START))
        {
          // check if plan is supposed to ignore some errors
          if (msg->flags & IMC::PlanControl::FLG_IGNORE_ERRORS) // temporary
            m_in_safe_plan = true;
          else
            m_in_safe_plan = false;
        }
      }

      void
      consume(const IMC::VehicleCommand* cmd)
      {
        if (cmd->type != IMC::VehicleCommand::VC_REQUEST)
          return;

        trace("%s request (%u/%u/%u)", c_cmd_desc[cmd->command],
              cmd->getSource(), cmd->getSourceEntity(), cmd->request_id);

        switch (cmd->command)
        {
          case IMC::VehicleCommand::VC_EXEC_MANEUVER:
            startManeuver(cmd);
            break;
          case IMC::VehicleCommand::VC_STOP_MANEUVER:
            stopManeuver(cmd);
            break;
          case IMC::VehicleCommand::VC_START_CALIBRATION:
            startCalibration(cmd);
            break;
          case IMC::VehicleCommand::VC_STOP_CALIBRATION:
            stopCalibration(cmd);
            break;
        }
      }

      void
      answer(const IMC::VehicleCommand* cmd, IMC::VehicleCommand::TypeEnum type,
             const std::string& desc)
      {
        m_vc_reply.setDestination(cmd->getSource());
        m_vc_reply.setDestinationEntity(cmd->getSourceEntity());
        m_vc_reply.type = type;
        m_vc_reply.command = cmd->command;
        m_vc_reply.request_id = cmd->request_id;
        m_vc_reply.info = desc;
        dispatch(m_vc_reply);

        if (type == IMC::VehicleCommand::VC_FAILURE)
          err("%s", desc.c_str());
        else
          trace("%s", desc.c_str());

        trace("(%u/%u/%u)", cmd->getSource(), cmd->getSourceEntity(), cmd->request_id);
      }

      inline void
      requestOK(const IMC::VehicleCommand* cmd, const std::string& desc)
      {
        answer(cmd, IMC::VehicleCommand::VC_SUCCESS, desc);
      }

      inline void
      requestFailed(const IMC::VehicleCommand* cmd, const std::string& desc)
      {
        answer(cmd, IMC::VehicleCommand::VC_FAILURE, desc);
      }

      void
      startCalibration(const IMC::VehicleCommand* msg)
      {
        if (externalMode())
        {
          requestFailed(msg, DTR("cannot calibrate: vehicle is in external mode"));
          return;
        }

        if (maneuverMode())
          reset();

        changeMode(IMC::VehicleState::VS_CALIBRATION);

        m_calibration.duration = msg->calib_time;
        dispatch(m_calibration);

        m_switch_time = Clock::get();

        requestOK(msg, String::str(DTR("calibrating vehicle for %u seconds"),
                                   m_calibration.duration));
      }

      void
      stopCalibration(const IMC::VehicleCommand* msg)
      {
        if (!calibrationMode())
        {
          requestOK(msg, DTR("cannot stop calibration: vehicle is not calibrating"));
          return;
        }

        requestOK(msg, String::str(DTR("stopped calibration"), m_calibration.duration));

        debug("calibration over");
        changeMode(IMC::VehicleState::VS_SERVICE);
      }

      void
      startManeuver(const IMC::VehicleCommand* cmd)
      {
        const IMC::Message* m = 0;

        if (!cmd->maneuver.isNull())
          m = cmd->maneuver.get();

        if (!m)
        {
          requestFailed(cmd, DTR("no maneuver specified"));
          return;
        }

        std::string mtype = m->getName();

        if (externalMode())
        {
          requestFailed(cmd, mtype + DTR(" maneuver cannot be started in current mode"));
          return;
        }

        dispatch(m_stop);
        IMC::Message* clone = m->clone();
        changeMode(IMC::VehicleState::VS_MANEUVER, clone);
        delete clone;

        requestOK(cmd, mtype + DTR(" maneuver started"));
      }

      void
      stopManeuver(const IMC::VehicleCommand* cmd)
      {
        if (!errorMode())
        {
          reset();

          if (!externalMode() || !nonOverridableLoops())
            changeMode(IMC::VehicleState::VS_SERVICE);
        }

        requestOK(cmd, DTR("OK"));
      }

      void
      reset(void)
      {
        if (maneuverMode())
          dispatch(m_stop);

        m_in_safe_plan = false;

        m_err_timer.reset();

        m_vs.control_loops = 0;

        dispatch(m_idle);
      }

      void
      task(void)
      {
        dispatch(m_vs);

        if (m_switch_time < 0.0)
          return;

        double delta = Clock::get() - m_switch_time;

        if (calibrationMode() && (delta > m_calibration.duration))
        {
          debug("calibration over");
          changeMode(IMC::VehicleState::VS_SERVICE);
        }
        else if (maneuverMode() && (delta > c_man_timeout))
        {
          inf(DTR("maneuver request timeout"));
          reset();
          changeMode(IMC::VehicleState::VS_SERVICE);
        }
        else
        {
          return;
        }

        m_switch_time = -1.0;
      }

      //! Check if the entities in error are relevant for performing an emergency plan
      //! @return true if entity error is relevant for current state, false otherwise
      bool
      entityError(void)
      {
        if (m_vs.error_count)
        {
          if (m_args.safe_ents.size() && m_in_safe_plan)
          {
            std::vector<std::string> ents;
            String::split(m_vs.error_ents, ",", ents);

            std::vector<std::string>::const_iterator it_ents = ents.begin();

            for (; it_ents != ents.end(); ++it_ents)
            {
              std::vector<std::string>::const_iterator it_safe = m_args.safe_ents.begin();
              for (; it_safe != m_args.safe_ents.end(); ++it_safe)
              {
                if (!(*it_ents).compare((*it_safe)))
                  return true;
              }
            }

            return false;
          }
          else
          {
            return true;
          }
        }
        else
        {
          return false;
        }
      }

      inline bool
      serviceMode(void) const
      {
        return modeIs(IMC::VehicleState::VS_SERVICE);
      }

      inline bool
      maneuverMode(void) const
      {
        return modeIs(IMC::VehicleState::VS_MANEUVER);
      }

      inline bool
      errorMode(void) const
      {
        return modeIs(IMC::VehicleState::VS_ERROR);
      }

      inline bool
      externalMode(void) const
      {
        return modeIs(IMC::VehicleState::VS_EXTERNAL);
      }

      inline bool
      calibrationMode(void) const
      {
        return modeIs(IMC::VehicleState::VS_CALIBRATION);
      }

      inline bool
      modeIs(IMC::VehicleState::OperationModeEnum mode) const
      {
        return m_vs.op_mode == mode;
      }

      inline bool
      teleoperationOn(void) const
      {
        return maneuverIs(DUNE_IMC_TELEOPERATION);
      }

      inline bool
      maneuverIs(uint16_t id) const
      {
        return m_vs.maneuver_type == id;
      }

      inline bool
      nonOverridableLoops(void) const
      {
        return (m_vs.control_loops & (IMC::CL_TELEOPERATION | IMC::CL_NO_OVERRIDE)) != 0;
      }
    };
  }
}

DUNE_TASK
