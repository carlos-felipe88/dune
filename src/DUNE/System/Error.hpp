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

#ifndef DUNE_SYSTEM_ERROR_HPP_INCLUDED_
#define DUNE_SYSTEM_ERROR_HPP_INCLUDED_

// ISO C++ 98 headers.
#include <string>
#include <sstream>
#include <cerrno>
#include <cstring>
#include <stdexcept>

// DUNE headers.
#include <DUNE/Config.hpp>
#include <DUNE/Utils/String.hpp>

#if defined(DUNE_SYS_HAS_WINDOWS_H)
#  include <windows.h>
#endif

namespace DUNE
{
  namespace System
  {
    //! Error
    class Error: public std::exception
    {
    public:
      //! Constructor.
      //! @param[in] code error code.
      //! @param[in] msg message to the user.
      Error(int code, const std::string& msg)
      {
        std::ostringstream ss;
        ss << msg << ": " << getMessage(code);
        m_full_msg = ss.str();
      }

      //! Constructor.
      //! @param[in] code error code.
      //! @param[in] msg message to the user.
      //! @param[in] args user message arguments.
      Error(int code, const std::string& msg, const std::string& args)
      {
        std::ostringstream ss;
        ss << msg << ": " << args << ": " << getMessage(code);
        m_full_msg = ss.str();
      }

      //! Constructor.
      //! @param[in] code error code.
      //! @param[in] msg message to the user.
      //! @param[in] args user message argument.
      Error(int code, const std::string& msg, int args)
      {
        std::ostringstream ss;
        ss << msg << ": " << args << ": " << getMessage(code);
        m_full_msg = ss.str();
      }

      //! Constructor.
      //! @param[in] estr error message.
      //! @param[in] msg message to the user.
      Error(const std::string& estr, const std::string& msg)
      {
        std::ostringstream ss;
        ss << msg << ": " << estr;
        m_full_msg = ss.str();
      }

      //! Constructor.
      //! @param[in] estr error message.
      //! @param[in] msg message to the user.
      //! @param[in] args user message arguments.
      Error(const std::string& estr, const std::string& msg, const std::string& args)
      {
        std::ostringstream ss;
        ss << msg << ": " << args << ": " << estr;
        m_full_msg = ss.str();
      }

      //! Destructor.
      ~Error(void) throw()
      { }

      //! Get the full description of the exception.
      //! @return description c string.
      const char*
      what(void) const throw()
      {
        return m_full_msg.c_str();
      }

      //! Retrieve the message of the last error.
      //! @return last error message.
      static std::string
      getLastMessage(void)
      {
#if defined(DUNE_SYS_HAS_GET_LAST_ERROR)
        return getMessage(GetLastError());
#else
        return getMessage(errno);
#endif
      }

      //! Retrieve the message associated with a specific error code.
      //! @param[in] ec error code.
      //! @return error message.
      static std::string
      getMessage(int ec)
      {
        char bfr[512] = {0};
        char* p = bfr;

        // POSIX strerror_r
#if defined(DUNE_SYS_HAS_POSIX_STRERROR_R)
        strerror_r(ec, bfr, sizeof(bfr));

        // GNU strerror_r
#elif defined(DUNE_SYS_HAS_GNU_STRERROR_R)
        p = strerror_r(ec, bfr, sizeof(bfr));

        // POSIX strerror
#elif defined(DUNE_SYS_HAS_STRERROR)
        p = strerror(ec);

        // Microsoft Windows FormatMessage
#elif defined(DUNE_SYS_HAS_FORMAT_MESSAGE)
        WORD lid = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
        if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, "%0", ec, lid, bfr, sizeof(bfr), 0) == 0)
          return "unable to translate system error";

        // Unsupported system
#else
        return "retrieving of error messages is not supported in this system";
#endif

        return p;
      }

    private:
      //! Full message.
      std::string m_full_msg;
    };
  }
}

#endif
