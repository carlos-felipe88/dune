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

#ifndef DUNE_MANEUVERS_MANEUVER_HPP_INCLUDED_
#define DUNE_MANEUVERS_MANEUVER_HPP_INCLUDED_

// ISO C++ 98 headers.
#include <string>
#include <map>

// DUNE headers.
#include <DUNE/Config.hpp>
#include <DUNE/Tasks.hpp>
#include <DUNE/IMC.hpp>
#include <DUNE/Time.hpp>

namespace DUNE
{
  namespace Maneuvers
  {
    // Export DLL Symbol.
    class DUNE_DLL_SYM Maneuver;

    //! Base abstract class for maneuver tasks.
    class Maneuver: public Tasks::Task
    {
    public:
      //! Constructor.
      Maneuver(const std::string& name, Tasks::Context& ctx);

      //! Destructor.
      virtual
      ~Maneuver();

      //! On resource initialization
      void
      onResourceInitialization(void)
      {
        Task::deactivate();
      }

      //! On maneuver activation
      virtual void
      onManeuverActivation(void)
      { }

      //! On maneuver deactivation
      virtual void
      onManeuverDeactivation(void)
      { }

      //! On task activation
      void
      onActivation(void)
      {
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
        onManeuverActivation();
      }

      //! On task deactivation
      void
      onDeactivation(void)
      {
        onManeuverDeactivation();
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_IDLE);
        debug("disabling");

        unlock();
      }

      //! Method fired on maneuver startup.
      //! It performs some initialization, then delegates handling on
      //! the task's consume method for the message.
      //! @param maneuver maneuver object
      template <typename T, typename M>
      void
      startManeuver(const M* maneuver)
      {
        if (!isActive())
        {
          while (!tryLock())
          {
            Time::Delay::wait(0.5);
          }
        }

        debug("enabling");
        signalProgress(65535, "in progress");

        static_cast<T*>(this)->consume(maneuver);

        if (m_mcs.state == IMC::ManeuverControlState::MCS_EXECUTING)
          activate();
      }

      template <typename T, typename M>
      void
      bindToManeuver(void)
      {
        void (Maneuver::* startfunc)(const M*) = &Maneuver::startManeuver<T, M>;
        Task::bind<M>(this, startfunc);
        m_rm.mid = M::getIdStatic();
      }

      template <typename M, typename T>
      void
      consumeIfActive(const M* msg)
      {
        if (isActive())
          static_cast<T*>(this)->consume(msg);
      }

      template <typename M, typename T>
      void
      bind(T* task_obj, bool always = false)
      {
        if (always)
        {
          Task::bind<M>(task_obj);
        }
        else
        {
          void (Maneuver::* func)(const M*) = &Maneuver::consumeIfActive<M, T>;
          Task::bind<M>(this, func);
        }
      }

      //! Consumer for StopManeuver message.
      //! @param sm message to consume.
      void
      consume(const IMC::StopManeuver* sm);

      //! Set or reconfigure control loops used by maneuver task.
      //! @param mask mask identifying controllers that should be made active.
      void
      setControl(uint32_t mask);

      //! State report handler.
      //! It should be overriden by maneuvers where it
      //! convenient to do so in time-triggered manner
      //! rather than in response to a particular message.
      virtual void
      onStateReport(void)
      { }

      //! Update active control loops
      //! @param cl control loops message
      void
      updateLoops(const IMC::ControlLoops* cl);

      //! Signal an error.
      //! This method should be used by subclasses to signal an error condition.
      //! @param msg error message
      void
      signalError(const std::string& msg);

      //! Signal no altitude error.
      //! This method should be used by subclasses to signal an error condition.
      void
      signalNoAltitude(void);

      //! Signal an error.
      //! This method should be used by subclasses to signal maneuver completion.
      //! @param msg completion message
      void
      signalCompletion(const std::string& msg = "done");

      //! Signal maneuver progress.
      //! @param time_left estimated time for completion.
      //! @param msg human-readable information.
      void
      signalProgress(uint16_t time_left, const std::string& msg);

      //! Signal maneuver progress.
      //! @param msg human-readable information.
      inline void
      signalProgress(const std::string& msg)
      {
        signalProgress(65535, msg);
      }

      //! Signal maneuver progress.
      //! @param time_left estimated time for completion.
      inline void
      signalProgress(uint16_t time_left)
      {
        signalProgress(time_left, "");
      }

      //! Signal maneuver progress.
      inline void
      signalProgress(void)
      {
        signalProgress("");
      }

      void
      onMain(void);

    private:
      //! Check if maneuver system is locked
      //! meaning a maneuver is in execution already
      //! @return if sucessful at locking return true
      bool
      tryLock(void);

      //! Unlock maneuver so that other maneuver may start
      void
      unlock(void);

      IMC::ManeuverControlState m_mcs;
      IMC::RegisterManeuver m_rm;
    };
  }
}

#endif
