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

#ifndef DUNE_TASK_PARAMETER_HPP_INCLUDED_
#define DUNE_TASK_PARAMETER_HPP_INCLUDED_

// ISO C++ 98 headers.
#include <string>
#include <stdexcept>
#include <sstream>
#include <map>
#include <vector>

// DUNE headers.
#include <DUNE/Config.hpp>
#include <DUNE/Units.hpp>
#include <DUNE/Tasks/AbstractParameterParser.hpp>

namespace DUNE
{
  namespace Tasks
  {
    // Export DLL Symbol.
    class DUNE_DLL_SYM Parameter;

    class Parameter
    {
    public:
      enum Visibility
      {
        VISIBILITY_USER,
        VISIBILITY_DEVELOPER
      };

      enum Scope
      {
        SCOPE_GLOBAL,
        SCOPE_PLAN,
        SCOPE_MANEUVER
      };

      struct ValuesIf
      {
        std::string name;
        std::string equals;
        std::string values;
      };

      //! Constructor.
      Parameter(const std::string& param_name, const std::string& type_name);

      //! Destructor.
      ~Parameter(void);

      void
      reader(AbstractParameterParser* r);

      void
      read(const std::string& val);

      bool
      commit(void)
      {
        return m_reader->commit();
      }

      std::string
      name(void) const
      {
        return m_name;
      }

      std::string
      value(void) const
      {
        return m_value;
      }

      std::string
      defaultValue(void) const
      {
        return m_default;
      }

      Parameter&
      defaultValue(const std::string& val)
      {
        m_default = val;
        return *this;
      }

      Parameter&
      valuesIf(const std::string& if_param, const std::string& if_value, const std::string& a_values)
      {
        ValuesIf* statement = new ValuesIf;
        statement->name = if_param;
        statement->equals = if_value;
        statement->values = a_values;
        m_values_if.push_back(statement);
        return *this;
      }

      Units::Types
      units(void) const
      {
        return m_units;
      }

      Parameter&
      units(Units::Types val)
      {
        m_units = val;
        return *this;
      }

      std::string
      description(void) const
      {
        return m_desc;
      }

      Parameter&
      description(const std::string& val)
      {
        m_desc = val;
        return *this;
      }

      Parameter&
      minimumValue(const std::string& min_value)
      {
        m_min_value = min_value;
        m_reader->minimumValue(min_value);
        return *this;
      }

      Parameter&
      maximumValue(const std::string& max_value)
      {
        m_max_value = max_value;
        m_reader->maximumValue(max_value);
        return *this;
      }

      Parameter&
      size(int size_value)
      {
        minimumSize(size_value);
        maximumSize(size_value);
        return *this;
      }

      Parameter&
      minimumSize(unsigned min_size)
      {
        m_min_size = min_size;
        m_reader->minimumSize(min_size);
        return *this;
      }

      Parameter&
      maximumSize(unsigned max_size)
      {
        m_max_size = max_size;
        m_reader->maximumSize(max_size);
        return *this;
      }

      Parameter&
      values(const std::string& list)
      {
        m_values = list;
        m_reader->values(list);
        return *this;
      }

      Parameter&
      visibility(Visibility a_visibility)
      {
        m_visibility = a_visibility;
        return *this;
      }

      Parameter&
      scope(Scope a_scope)
      {
        m_scope = a_scope;
        return *this;
      }

      void
      writeXML(std::ostream& os) const;

      bool
      changed(void)
      {
        bool old = m_changed;
        m_changed = false;
        return old;
      }

      void
      setChanged(void)
      {
        m_changed = true;
      }

    protected:
      std::string m_name;
      std::string m_type_name;
      std::string m_desc;
      std::string m_default;
      Units::Types m_units;
      std::string m_old_value;
      std::string m_value;
      std::string m_values;

      //! Minimum size.
      unsigned m_min_size;
      //! Maximum size.
      unsigned m_max_size;

      //! Minimum value.
      std::string m_min_value;
      //! Maximum value.
      std::string m_max_value;

      std::vector<ValuesIf*> m_values_if;
      AbstractParameterParser* m_reader;
      //! True if the value of this parameter changed.
      bool m_changed;
      //! Parameter visibility.
      Visibility m_visibility;
      //! Parameter scope.
      Scope m_scope;
    };
  }
}

#endif
