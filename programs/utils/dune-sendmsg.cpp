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
// Author: Eduardo Marques                                                  *
//***************************************************************************

// ISO C++ 9 headers.
#include <cstdlib>
#include <cstdio>

// DUNE headers.
#include <DUNE/DUNE.hpp>

using DUNE_NAMESPACES;
using namespace std;

int
main(int argc, char** argv)
{
  if (argc < 4)
  {
    fprintf(stderr, "Usage: %s <destination host> <destination port> <abbrev> [arguments]\n", argv[0]);
    return 1;
  }

  Address dest(argv[1]);

  // Parse port.
  unsigned port = 0;
  if (!castLexical(argv[2], port))
  {
    fprintf(stderr, "ERROR: invalid port '%s'\n", argv[2]);
    return 1;
  }

  if (port > 65535)
  {
    fprintf(stderr, "ERROR: invalid port '%s'\n", argv[2]);
    return 1;
  }

  IMC::Message* msg = NULL;

  if (strcmp(argv[3], "Heartbeat") == 0)
  {
    IMC::Heartbeat* tmsg = new IMC::Heartbeat;
    msg = tmsg;
  }
  if (strcmp(argv[3], "RestartSystem") == 0)
  {
    IMC::RestartSystem* tmsg = new IMC::RestartSystem;
    msg = tmsg;
  }
  else if (strcmp(argv[3], "Sms") == 0)
  {
    IMC::Sms* tmsg = new IMC::Sms;
    tmsg->number = argv[4];
    tmsg->timeout = atoi(argv[5]);
    tmsg->contents = argv[6];
    msg = tmsg;
  }
  else if (strcmp(argv[3], "EntityState") == 0)
  {
    IMC::EntityState* tmsg = new IMC::EntityState;
    msg = tmsg;
    tmsg->setSourceEntity(atoi(argv[4]));
    tmsg->state = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "MonitorEntityState") == 0)
  {
    IMC::MonitorEntityState* tmsg = new IMC::MonitorEntityState;
    msg = tmsg;
    tmsg->command = atoi(argv[4]);
    if (argc >= 6)
      tmsg->entities = argv[5];
  }
  else if (strcmp(argv[3], "Abort") == 0)
  {
    msg = new IMC::Abort;
  }
  else if (strcmp(argv[3], "LoggingControl") == 0)
  {
    IMC::LoggingControl* tmsg = new IMC::LoggingControl;
    msg = tmsg;
    tmsg->op = atoi(argv[4]);
    tmsg->name = argv[5];
  }
  else if (strcmp(argv[3], "CacheControl") == 0)
  {
    IMC::CacheControl* tmsg = new IMC::CacheControl;
    msg = tmsg;
    tmsg->op = atoi(argv[4]);
  }
  else if (strcmp(argv[3], "LblRange") == 0)
  {
    IMC::LblRange* tmsg = new IMC::LblRange;
    msg = tmsg;
    tmsg->id = atoi(argv[4]);
    tmsg->range = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "LblConfig") == 0)
  {
    IMC::LblConfig* tmsg = new IMC::LblConfig;
    msg = tmsg;
    tmsg->op = IMC::LblConfig::OP_SET_CFG;

    IMC::LblBeacon bc;
    bc.beacon = "b2";
    bc.lat = 0.71883274;
    bc.lon = -0.15194732;
    bc.depth = 3;
    bc.query_channel = 4;
    bc.reply_channel = 5;
    bc.transponder_delay = 0;
    tmsg->beacons.push_back(bc);
    bc.beacon = "b3";
    bc.lat = 0.71881068;
    bc.lon = -0.15192335;
    bc.reply_channel = 6;
    tmsg->beacons.push_back(bc);
  }
  else if (strcmp(argv[3], "DesiredZ") == 0)
  {
    IMC::DesiredZ* tmsg = new IMC::DesiredZ;
    tmsg->value = atof(argv[4]);
    tmsg->z_units = static_cast<IMC::ZUnits>(atoi(argv[5]));
    msg = tmsg;
  }
  else if (strcmp(argv[3], "DesiredPitch") == 0)
  {
    IMC::DesiredPitch* tmsg = new IMC::DesiredPitch;
    tmsg->value = DUNE::Math::Angles::radians(atof(argv[4]));
    msg = tmsg;
  }
  else if (strcmp(argv[3], "Calibration") == 0)
  {
    IMC::Calibration* tmsg = new IMC::Calibration;
    tmsg->duration = (uint16_t)(atof(argv[4]));
    msg = tmsg;
  }
  else if (strcmp(argv[3], "DesiredHeading") == 0)
  {
    IMC::DesiredHeading* tmsg = new IMC::DesiredHeading;
    tmsg->value = DUNE::Math::Angles::radians(atof(argv[4]));
    msg = tmsg;
  }
  else if (strcmp(argv[3], "DesiredHeadingRate") == 0)
  {
    IMC::DesiredHeadingRate* tmsg = new IMC::DesiredHeadingRate;
    tmsg->value = DUNE::Math::Angles::radians(atof(argv[4]));
    msg = tmsg;
  }
  else if (strcmp(argv[3], "DesiredSpeed") == 0)
  {
    IMC::DesiredSpeed* tmsg = new IMC::DesiredSpeed;
    tmsg->value = atof(argv[4]);
    if (argc == 5)
      tmsg->speed_units = IMC::SUNITS_PERCENTAGE;
    else
      tmsg->speed_units = atoi(argv[5]);
    msg = tmsg;
  }
  else if (strcmp(argv[3], "DesiredControl") == 0)
  {
    IMC::DesiredControl* tmsg = new IMC::DesiredControl;
    tmsg->k = atof(argv[4]);
    tmsg->m = atof(argv[5]);
    tmsg->n = atof(argv[6]);
    msg = tmsg;
  }
  else if (strcmp(argv[3], "SetThrusterActuation") == 0)
  {
    IMC::SetThrusterActuation* tmsg = new IMC::SetThrusterActuation;
    msg = tmsg;
    tmsg->id = atoi(argv[4]);
    tmsg->value = atof(argv[5]);
  }
  else if (strcmp(argv[3], "SetServoPosition") == 0)
  {
    IMC::SetServoPosition* tmsg = new IMC::SetServoPosition;
    msg = tmsg;
    tmsg->id = atoi(argv[4]);
    tmsg->value = atof(argv[5]);
  }
  else if (strcmp(argv[3], "GpsFix") == 0)
  {
    IMC::GpsFix* tmsg = new IMC::GpsFix;
    msg = tmsg;
    tmsg->lat = Angles::radians(atof(argv[4]));
    tmsg->lon = Angles::radians(atof(argv[5]));
    tmsg->height = atof(argv[6]);
  }
  else if (strcmp(argv[3], "SonarConfig") == 0)
  {
    IMC::SonarConfig* tmsg = new IMC::SonarConfig;
    msg = tmsg;
    tmsg->setDestination(atoi(argv[4]));
    tmsg->frequency = atoi(argv[5]);
    tmsg->max_range = atoi(argv[6]);
    tmsg->min_range = atoi(argv[7]);
  }
  else if (strcmp(argv[3], "VehicleCommand") == 0)
  {
    IMC::VehicleCommand* tmsg = new IMC::VehicleCommand;
    msg = tmsg;
    tmsg->type = IMC::VehicleCommand::VC_REQUEST;
    tmsg->command = atoi(argv[4]);

    if (tmsg->command == IMC::VehicleCommand::VC_EXEC_MANEUVER)
      tmsg->maneuver.set(dynamic_cast<IMC::Maneuver*>(IMC::Factory::produce(argv[5])));
  }
  else if (strcmp(argv[3], "ButtonEvent") == 0)
  {
    IMC::ButtonEvent* tmsg = new IMC::ButtonEvent;
    msg = tmsg;
    tmsg->button = atoi(argv[4]);
    tmsg->value = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "LedControl") == 0)
  {
    IMC::LedControl* tmsg = new IMC::LedControl;
    msg = tmsg;
    tmsg->id = atoi(argv[4]);
    tmsg->op = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "EstimatedState") == 0)
  {
    IMC::EstimatedState* tmsg = new IMC::EstimatedState;
    msg = tmsg;
    tmsg->x = atof(argv[4]);
    tmsg->y = atof(argv[5]);
    tmsg->z = atof(argv[6]);
    tmsg->phi = 0.0;
    tmsg->theta = 0.0;
    tmsg->psi = 0.0;
    tmsg->u = 0.0;
    tmsg->v = 0.0;
    tmsg->w = 0.0;
    tmsg->p = 0.0;
    tmsg->q = 0.0;
    tmsg->r = 0.0;
    tmsg->lat = 0.0;
    tmsg->lon = 0.0;
    tmsg->depth = 0.0;
  }
  else if (strcmp(argv[3], "PowerChannelControl") == 0)
  {
    IMC::PowerChannelControl* tmsg = new IMC::PowerChannelControl;
    msg = tmsg;
    tmsg->id = atoi(argv[4]);
    tmsg->op = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "AcousticSystemsQuery") == 0)
  {
    IMC::AcousticSystemsQuery* tmsg = new IMC::AcousticSystemsQuery;
    msg = tmsg;
  }
  else if (strcmp(argv[3], "AcousticRange") == 0)
  {
    IMC::AcousticRange* tmsg = new IMC::AcousticRange;
    msg = tmsg;
    tmsg->address = atoi(argv[4]);
  }
  else if (strcmp(argv[3], "AcousticMessage") == 0)
  {
    IMC::AcousticMessage* tmsg = new IMC::AcousticMessage;
    msg = tmsg;
    IMC::Message* imsg = IMC::Factory::produce(atoi(argv[4]));
    tmsg->message.set(*imsg);
    delete imsg;
  }
  else if (strcmp(argv[3], "AcousticPing") == 0)
  {
    msg = new IMC::AcousticPing;
  }
  else if (strcmp(argv[3], "QueryEntityInfo") == 0)
  {
    IMC::QueryEntityInfo* tmsg = new IMC::QueryEntityInfo;
    msg = tmsg;
    tmsg->id = atoi(argv[4]);
  }
  else if (strcmp(argv[3], "QueryEntityParameters") == 0)
  {
    IMC::QueryEntityParameters* tmsg = new IMC::QueryEntityParameters;
    msg = tmsg;
    tmsg->name = argv[4];
  }
  else if (strcmp(argv[3], "SaveEntityParameters") == 0)
  {
    IMC::SaveEntityParameters* tmsg = new IMC::SaveEntityParameters;
    msg = tmsg;
    tmsg->name = argv[4];
  }
  else if (strcmp(argv[3], "EntityList") == 0)
  {
    IMC::EntityList* tmsg = new IMC::EntityList;
    msg = tmsg;
    tmsg->op = IMC::EntityList::OP_QUERY;
  }
  else if (strcmp(argv[3], "ControlLoops") == 0)
  {
    IMC::ControlLoops* tmsg = new IMC::ControlLoops;
    msg = tmsg;
    tmsg->enable = atoi(argv[4]) ? 1 : 0;
    tmsg->mask = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "TeleoperationDone") == 0)
  {
    msg = new IMC::TeleoperationDone;
  }
  else if (strcmp(argv[3], "RemoteActionsRequest") == 0)
  {
    IMC::RemoteActionsRequest* tmsg = new IMC::RemoteActionsRequest;
    msg = tmsg;
    tmsg->op = IMC::RemoteActionsRequest::OP_QUERY;
  }
  else if (strcmp(argv[3], "RemoteActions") == 0)
  {
    IMC::RemoteActions* tmsg = new IMC::RemoteActions;
    msg = tmsg;
    tmsg->actions = argv[4];
  }
  else if (strcmp(argv[3], "LogBookControl") == 0)
  {
    IMC::LogBookControl* tmsg = new IMC::LogBookControl;
    tmsg->command = atoi(argv[4]);
    if (argc >= 6)
      tmsg->htime = Time::Clock::getSinceEpoch() - atof(argv[5]);
    else
      tmsg->htime = -1;
    msg = tmsg;
  }
  else if (strcmp(argv[3], "EmergencyControl") == 0)
  {
    IMC::EmergencyControl* tmsg = new IMC::EmergencyControl;
    tmsg->command = atoi(argv[4]);
    msg = tmsg;
  }
  else if (strcmp(argv[3], "LeakSimulation") == 0)
  {
    IMC::LeakSimulation* tmsg = new IMC::LeakSimulation;
    tmsg->op = atoi(argv[4]);
    if (argc >= 6)
      tmsg->entities = argv[5];
    msg = tmsg;
  }
  else if (strcmp(argv[3], "OperationalLimits") == 0)
  {
    IMC::OperationalLimits* tmsg = new IMC::OperationalLimits;
    tmsg->mask = IMC::OPL_AREA;
    tmsg->lat = DUNE::Math::Angles::radians(atof(argv[4]));
    tmsg->lon = DUNE::Math::Angles::radians(atof(argv[5]));
    tmsg->orientation = DUNE::Math::Angles::radians(atof(argv[6]));
    tmsg->width = atof(argv[7]);
    tmsg->length = atof(argv[8]);
    msg = tmsg;
  }
  else if (strcmp(argv[3], "UASimulation") == 0)
  {
    IMC::UASimulation* tmsg = new IMC::UASimulation;
    tmsg->setSource(atoi(argv[4]));
    tmsg->setDestination(atoi(argv[5]));
    tmsg->speed = atoi(argv[6]);
    tmsg->type = IMC::UASimulation::UAS_DATA;
    tmsg->data.assign(atoi(argv[7]), '0');
    msg = tmsg;
  }
  else if (strcmp(argv[3], "ReplayControl") == 0)
  {
    IMC::ReplayControl* tmsg = new IMC::ReplayControl;
    tmsg->op = atoi(argv[4]);
    if (tmsg->op == IMC::ReplayControl::ROP_START)
      tmsg->file = argv[5];
    msg = tmsg;
  }
  else if (strcmp(argv[3], "ClockControl") == 0)
  {
    IMC::ClockControl* tmsg = new IMC::ClockControl;
    tmsg->op = atoi(argv[4]);
    if (argc >= 6)
      tmsg->clock = atof(argv[5]);
    if (argc >= 7)
      tmsg->tz = atoi(argv[6]);
    msg = tmsg;
  }
  else if (strcmp(argv[3], "PlanControl") == 0)
  {
    IMC::PlanControl* tmsg = new IMC::PlanControl;
    tmsg->type = IMC::PlanControl::PC_REQUEST;
    tmsg->op = atoi(argv[4]);
    tmsg->plan_id = argv[5];
    if (argc >= 7)
      tmsg->flags = atoi(argv[6]);

    if (argc >= 8)
      tmsg->arg.set(IMC::Factory::produce(argv[7]));

    msg = tmsg;
  }
  else if (strcmp(argv[3], "LogBookEntry") == 0)
  {
    IMC::LogBookEntry* tmsg = new IMC::LogBookEntry;
    msg = tmsg;
    tmsg->context = argv[4];
    tmsg->text = argv[5];
    tmsg->htime = Time::Clock::getSinceEpoch();
    if (argc > 6)
      tmsg->type = atoi(argv[6]);
    else
      tmsg->type = IMC::LogBookEntry::LBET_WARNING;
  }
  else if (strcmp(argv[3], "TrexCommand") == 0)
  {
    IMC::TrexCommand* tmsg = new IMC::TrexCommand;
    msg = tmsg;
    if (strcmp(argv[4], "DISABLE") == 0 || strcmp(argv[4], "1") == 0 )
        tmsg->command = 1;
    else if (strcmp(argv[4], "ENABLE") == 0 || strcmp(argv[4], "2") == 0 )
        tmsg->command = 2;
  }
  else if (strcmp(argv[3], "PlanGeneration") == 0)
  {
    IMC::PlanGeneration* tmsg = new IMC::PlanGeneration;
    msg = tmsg;
    tmsg->cmd = atoi(argv[4]);
    tmsg->op = atoi(argv[5]);
    tmsg->plan_id = argv[6];

    if (argc >= 8)
      tmsg->params = argv[7];
  }
  else if (strcmp(argv[3], "SoundSpeed") == 0)
  {
    IMC::SoundSpeed* tmsg = new IMC::SoundSpeed;
    msg = tmsg;
    tmsg->value = atoi(argv[4]);
  }
  else if (strcmp(argv[3], "Parameter") == 0)
  {
    IMC::Parameter* tmsg = new IMC::Parameter;
    msg = tmsg;
    tmsg->section = argv[4];
    tmsg->param = argv[5];
    tmsg->value = argv[6];
  }
  else if (strcmp(argv[3], "DevCalibrationControl") == 0)
  {
    IMC::DevCalibrationControl * tmsg = new IMC::DevCalibrationControl;
    msg = tmsg;
    tmsg->setDestinationEntity(atoi(argv[4]));
    tmsg->op = atoi(argv[5]);
  }
  else if (strcmp(argv[3], "RegisterManeuver") == 0)
  {
    IMC::RegisterManeuver* tmsg = new IMC::RegisterManeuver;
    msg = tmsg;
    tmsg->mid = atoi(argv[4]);
  }
  else if (strcmp(argv[3], "Brake") == 0)
  {
    IMC::Brake* tmsg = new IMC::Brake;
    msg = tmsg;
    tmsg->op = atoi(argv[4]);
  }

  if (msg == NULL)
  {
    fprintf(stderr, "ERROR: unknown message '%s'\n", argv[3]);
    return 1;
  }

  msg->setTimeStamp();

  uint8_t bfr[1024] = {0};
  uint16_t rv = IMC::Packet::serialize(msg, bfr, sizeof(bfr));

  UDPSocket sock;
  try
  {
    sock.write((const char*)bfr, rv, dest, port);

    fprintf(stderr, "Raw:");
    for (int i = 0; i < rv; ++i)
      fprintf(stderr, " %02X", bfr[i]);
    fprintf(stderr, "\n");

    msg->toText(cerr);
  }
  catch (std::runtime_error& e)
  {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }

  if (msg != NULL)
  {
    delete msg;
    msg = NULL;
  }

  return 0;
}
