//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Faculdade de Engenharia da Universidade do Porto                         *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Eduardo Marques                                                  *
//***************************************************************************

#include <vector>
#include <DUNE/Parsers/PlanConfigParser.hpp>
#include <DUNE/Math/Matrix.hpp>

namespace DUNE
{
  namespace Parsers
  {
    using namespace DUNE::IMC;

#ifdef DUNE_IMC_IDLEMANEUVER
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::IdleManeuver& man)
    {
      parseDuration(cfg, id, man);
    }

#endif
#ifdef DUNE_IMC_POPUP
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::PopUp& man)
    {
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseTimeout(cfg, id, man);
      parseDuration(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);
    }

#endif
#ifdef DUNE_IMC_GOTO
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::Goto& man)
    {
      // Get configurable parameters
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseTimeout(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);
    }
#endif
#ifdef DUNE_IMC_STATIONKEEPING
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::StationKeeping& man)
    {
      // Get configurable parameters
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);
      parseDuration(cfg, id, man);
      cfg.get(id, "Radius (meters)", "15.0", man.radius);
    }
#endif
#ifdef DUNE_IMC_LOITER
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::Loiter& man)
    {
      // Get configurable parameters
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseTimeout(cfg, id, man);
      parseDuration(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);

      int8_t type;
      cfg.get(id, "Loiter Type", "0", type);

      switch (type)
      {
        case 1:
          man.type = IMC::Loiter::LT_RACETRACK; break;
        case 2:
          man.type = IMC::Loiter::LT_HOVER; break;
        case 3:
          man.type = IMC::Loiter::LT_EIGHT; break;
        case 0:
        default:
          man.type = IMC::Loiter::LT_CIRCULAR; break;
      }

      std::string ldir;
      cfg.get(id, "Loiter Direction", "Clock", ldir);

      if (ldir == "Clockwise")
        man.direction = IMC::Loiter::LD_CLOCKW;
      else
        man.direction = IMC::Loiter::LD_CCLOCKW;

      cfg.get(id, "Radius (meters)", "50", man.radius);
      parseAngle(cfg, id, "Bearing (degrees)", man.bearing, 0.0);
      cfg.get(id, "Length (meters)", "100", man.length);
    }

#endif
#ifdef DUNE_IMC_FOLLOWPATH
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::FollowPath& man)
    {
      // Get configurable parameters
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseTimeout(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);

      int n_points;

      cfg.get(id, "Number of Points", "0", n_points);

      Math::Matrix W(n_points, 3);

      W.readFromConfig(cfg, id, "Points");

      IMC::MessageList<IMC::PathPoint>* list = &man.points;

      for (int i = 0; i < W.rows(); ++i)
      {
        IMC::PathPoint* p = new IMC::PathPoint;
        p->x = W(i, 0);
        p->y = W(i, 1);
        p->z = W(i, 2);

        list->push_back(*p);
      }
    }

#endif
#ifdef DUNE_IMC_ROWS
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::Rows& man)
    {
      // Get configurable parameters
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);
      parseAngle(cfg, id, "Bearing (degrees)", man.bearing, 0.0);
      parseAngle(cfg, id, "Cross Angle (degrees)", man.cross_angle, 0.0);
      cfg.get(id, "Width (meters)", "150", man.width);
      cfg.get(id, "Length (meters)", "100", man.length);
      cfg.get(id, "Curve Offset (meters)", "15", man.coff);
      cfg.get(id, "Alternation (%)", "100", man.alternation);
      cfg.get(id, "Horizontal Step (meters)", "30", man.hstep);
      cfg.get(id, "Flags", "3", man.flags);
    }

#endif
#ifdef DUNE_IMC_TELEOPERATION
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::Teleoperation& man)
    {
      cfg.get(id, "Custom Settings", "", man.custom);
    }

#endif
#ifdef DUNE_IMC_LBLBEACONSETUP
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string section, IMC::LblBeaconSetup& bs)
    {
      bs.beacon = section;
      parseCoordinate(cfg, section, bs);
      parseZ(cfg, section);
      cfg.get(section, "Transponder Delay (msecs)", "", bs.transponder_delay);
      cfg.get(section, "Interrogation Channel", "", bs.query_channel);
      cfg.get(section, "Reply Channel", "", bs.reply_channel);
    }

#endif
#ifdef DUNE_IMC_YOYO
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string section, IMC::YoYo& man)
    {
      parseCoordinate(cfg, section, man);
      parseZ(cfg, section, man);
      parseZUnits(cfg, section, man);
      cfg.get(section, "Amplitude (meters)", "0.0", man.amplitude);
      parseAngle(cfg, section, "Pitch (degrees)", man.pitch, (fp32_t)15.0);
      parseSpeed(cfg, section, man);
    }
#endif
#ifdef DUNE_IMC_ELEVATOR
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string section, IMC::Elevator& man)
    {
      parseSpeed(cfg, section, man);
      parseCoordinate(cfg, section, man);
      cfg.get(section, "Flags", "0x00", man.flags);
      parseZUnits(cfg, section, man.start_z_units, "Start Z Units");
      parseZUnits(cfg, section, man.end_z_units, "End Z Units");
      cfg.get(section, "Start Z (meters)", "0.0", man.start_z);
      cfg.get(section, "End Z (meters)", "0.0", man.end_z);
      cfg.get(section, "Radius (meters)", "15.0", man.radius);
    }
#endif
#ifdef DUNE_IMC_DUBIN
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::Dubin& man)
    {
      // Get configurable parameters
      parseSpeed(cfg, id, man);
      parseDuration(cfg, id, man);
      parseTimeout(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);
    }
#endif
#ifdef DUNE_IMC_COMPASSCALIBRATION
    void
    PlanConfigParser::parse(Parsers::Config& cfg, std::string id, IMC::CompassCalibration& man)
    {
      // Get configurable parameters
      parseCoordinate(cfg, id, man);
      parseSpeed(cfg, id, man);
      parseTimeout(cfg, id, man);
      parseDuration(cfg, id, man);
      parseZ(cfg, id, man);
      parseZUnits(cfg, id, man);

      std::string ldir;
      cfg.get(id, "Loiter Direction", "Clock", ldir);

      if (ldir == "Clockwise")
        man.direction = IMC::Loiter::LD_CLOCKW;
      else
        man.direction = IMC::Loiter::LD_CCLOCKW;

      cfg.get(id, "Radius (meters)", "50", man.radius);
      cfg.get(id, "Amplitude (meters)", "1", man.amplitude);
      parseAngle(cfg, id, "Pitch (degrees)", man.pitch, (fp32_t)0.0);
    }
#endif

    void
    PlanConfigParser::parse(Parsers::Config& cfg, IMC::PlanSpecification& plan)
    {
      cfg.get("Plan Configuration", "Plan ID", "", plan.plan_id);

      std::vector<std::string> ids;

      cfg.get("Plan Configuration", "Maneuvers", "", ids);

      for (uint16_t i = 0; i < ids.size(); i++)
      {
        std::string id = ids[i];

        IMC::PlanManeuver pman;
        pman.maneuver_id = id;

        std::string type;
        cfg.get(id, "Type", "!!unknown!!", type);

#ifdef DUNE_IMC_POPUP
        if (type == "PopUp")
        {
          IMC::PopUp popup;
          parse(cfg, id, popup);
          pman.data.set(popup);
        }
        else
#endif
#ifdef DUNE_IMC_GOTO
          if (type == "Goto")
          {
            IMC::Goto goto_man;
            parse(cfg, id, goto_man);
            pman.data.set(goto_man);
          }
          else
#endif
#ifdef DUNE_IMC_STATIONKEEPING
          if (type == "StationKeeping")
          {
            IMC::StationKeeping sk_man;
            parse(cfg, id, sk_man);
            pman.data.set(sk_man);
          }
          else
#endif
#ifdef DUNE_IMC_IDLEMANEUVER
            if (type == "Idle")
            {
              IMC::IdleManeuver idle;
              parse(cfg, id, idle);
              pman.data.set(idle);
            }
            else
#endif
#ifdef DUNE_IMC_LOITER
              if (type == "Loiter")
              {
                IMC::Loiter loiter;
                parse(cfg, id, loiter);
                pman.data.set(loiter);
              }
              else
#endif
#ifdef DUNE_IMC_FOLLOWPATH
                if (type == "FollowPath")
                {
                  IMC::FollowPath fp;
                  parse(cfg, id, fp);
                  pman.data.set(fp);

                }
                else
#endif
#ifdef DUNE_IMC_ROWS
                  if (type == "Rows")
                  {
                    IMC::Rows r;
                    parse(cfg, id, r);
                    pman.data.set(r);
                    r.toText(std::cerr);
                  }
                  else
#endif
#ifdef DUNE_IMC_ELEMENTALMANEUVER
                    if (type == "ElementalManeuver")
                    {
                      IMC::ElementalManeuver eman;
                      parse(cfg, id, eman);
                      pman.data.set(eman);
                    }
                    else
#endif
#ifdef DUNE_IMC_YOYO
                    if (type == "YoYo")
                    {
                      IMC::YoYo yoyo;
                      parse(cfg, id, yoyo);
                      pman.data.set(yoyo);
                    }
                    else
#endif
#ifdef DUNE_IMC_ELEVATOR
                  if (type == "Elevator")
                  {
                    IMC::Elevator elev;
                    parse(cfg, id, elev);
                    pman.data.set(elev);
                    elev.toText(std::cerr);
                  }
                  else
#endif
#ifdef DUNE_IMC_DUBIN
                      if (type == "Dubin")
                      {
                        IMC::Dubin dub;
                        parse(cfg, id, dub);
                        pman.data.set(dub);
                      }
                      else
#endif
#ifdef DUNE_IMC_COMPASSCALIBRATION
                        if (type == "CompassCalibration")
                        {
                          IMC::CompassCalibration ccalib;
                          parse(cfg, id, ccalib);
                          pman.data.set(ccalib);
                        }
                        else
#endif
                      {
                        DUNE_ERR
                        ("Plan Load", "Unknown or unsupported maneuver type: '" << type << '\'');
                        return;
                      }

        plan.maneuvers.push_back(pman);

        if (plan.maneuvers.size() == 1)
        {
          plan.start_man_id = id;
        }
        else
        {
          // See maneuver sequence in graph terms
          IMC::PlanTransition ptransition;
          ptransition.source_man = ids[plan.maneuvers.size() - 2];
          ptransition.dest_man = id;
          plan.transitions.push_back(ptransition);
        }
      }
    }
  }
}
