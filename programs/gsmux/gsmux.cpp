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

// ISO C++ 98 headers.
#include <iostream>
#include <fstream>
#include <vector>

// DUNE headers.
#include <DUNE/DUNE.hpp>
using DUNE_NAMESPACES;

// Local headers.
#include "Parser.hpp"

int
main(int argc, char** argv)
{
  if (argc != 3)
  {
    std::cerr << "Usage: " << argv[0] << " <device> <folder>" << std::endl;
    return 1;
  }

  double now = Clock::getSinceEpoch();
  std::string prefix(argv[2]);
  prefix += "/";
  prefix += Time::Format::getDateSafe(now);
  prefix += "_";
  prefix += Time::Format::getTimeSafe(now);

  std::string bin_name = prefix + ".bin";
  std::string tsv_name = prefix + ".tsv";

  FILE* bin = fopen(bin_name.c_str(), "wb");
  if (bin == NULL)
  {
    std::cerr << "ERROR: failed to open file '" << bin_name << "'" << std::endl;
    return 1;
  }

  FILE* tsv = fopen(tsv_name.c_str(), "w");
  if (tsv == NULL)
  {
    std::cerr << "ERROR: failed to open file '" << tsv_name << "'" << std::endl;
    return 1;
  }

  std::cerr << "Logging to '" << prefix << ".{bin,tsv}'" << std::endl;

  SerialPort port(argv[1], 500000);
  uint8_t bfr[1024];
  ::Parser parser(tsv);

  while (1)
  {
    if (port.hasNewData(1.0) != DUNE::System::IOMultiplexing::PRES_OK)
      continue;

    int rv = port.read(bfr, sizeof(bfr));
    fwrite(bfr, 1, rv, bin);
    for (int i = 0; i < rv; ++i)
    {
      parser.parse(bfr[i]);
    }
  }

  return 0;
}
