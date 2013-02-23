//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: José Braga                                                       *
// Author: Pedro Calado (Bottom Distance/Altitude filter)                   *
//***************************************************************************

// Local headers.
#include <DUNE/Navigation/BasicNavigation.hpp>

//! Z reference tolerance.
static const float c_z_tol = 0.1;

namespace DUNE
{
  namespace Navigation
  {
    using Tasks::DF_KEEP_TIME;

    static std::string
    getUncertaintyMessage(double hpos_var)
    {
      using Utils::String;
      return String::str(DTR("maximum horizontal position uncertainty is %0.2f m"),
                         std::sqrt(hpos_var));
    }

    BasicNavigation::BasicNavigation(const std::string& name, Tasks::Context& ctx):
      Tasks::Periodic(name, ctx),
      m_active(false),
      m_origin(NULL),
      m_avg_heave(NULL)
    {
      // Declare configuration parameters.
      param("Maximum distance to reference", m_max_dis2ref)
      .units(Units::Meter)
      .defaultValue("1000")
      .description("Maximum allowed distance to 'EstimatedState' reference");

      param("Max. Horizontal Position Variance", m_max_hpos_var)
      .units(Units::SquareMeter)
      .defaultValue("240.0")
      .description("Maximum allowed horizontal Position estimation covariance");

      param("Reject all LBL ranges", m_reject_all_lbl)
      .defaultValue("false")
      .description("Boolean variable that defines if vehicle rejects all LblRanges");

      param("LBL Expected Range Rejection Constants", m_lbl_reject_constants)
      .defaultValue("")
      .size(2)
      .description("Constants used in current LBL rejection scheme");

      param("GPS timeout", m_without_gps_timeout)
      .units(Units::Second)
      .defaultValue("3.0")
      .description("No GPS readings timeout");

      param("DVL timeout", m_without_dvl_timeout)
      .units(Units::Second)
      .defaultValue("1.0")
      .description("No DVL readings timeout");

      param("Distance Between DVL and CG", m_dist_dvl_cg)
      .units(Units::Meter)
      .defaultValue("0.3")
      .description("Distance between DVL and vehicle Center of Gravity");

      param("Distance Between LBL and GPS", m_dist_lbl_gps)
      .units(Units::Meter)
      .defaultValue("0.50")
      .description("Distance between LBL receiver and GPS in the vehicle");

      param("DVL absolute thresholds", m_dvl_abs_thresh)
      .defaultValue("")
      .size(2)
      .description("DVL absolute thresholds");

      param("DVL relative thresholds", m_dvl_rel_thresh)
      .defaultValue("")
      .size(2)
      .description("DVL relative thresholds");

      param("DVL relative threshold time window", m_dvl_time_rel_thresh)
      .units(Units::Second)
      .defaultValue("1.0")
      .minimumValue("0.0")
      .description("DVL relative threshold time window to be applied");

      param("LBL Threshold", m_lbl_threshold)
      .defaultValue("4.0")
      .description("LBL Threshold value for the LBL level check rejection scheme");

      param("GPS Maximum HDOP", m_max_hdop)
      .defaultValue("5.0")
      .minimumValue("3.0")
      .maximumValue("10.0")
      .description("Maximum Horizontal Dilution of Precision value accepted for GPS fixes");

      param("GPS Maximum HACC", m_max_hacc)
      .defaultValue("6.0")
      .minimumValue("3.0")
      .maximumValue("20.0")
      .description("Maximum Horizontal Accuracy Estimate value accepted for GPS fixes");

      param("Heave Moving Average Samples", m_avg_heave_samples)
      .defaultValue("40")
      .description("Number of moving average samples to smooth heave");

      param("Entity Label - Depth", m_label_depth)
      .description("Entity label of 'Depth' messages");

      param("Entity Label - Compass", m_label_ahrs)
      .description("Entity label of 'AHRS' messages");

      param("Entity Label - Alignment", m_label_calibration)
      .description("Entity label of 'EulerAngles' calibration messages");

      param("Entity Label - Altitude - Hardware", m_elabel_alt_hard)
      .description("Entity label of the 'Distance' message for Hardware profile");

      param("Entity Label - Altitude - Simulation", m_elabel_alt_sim)
      .description("Entity label of the 'Distance' message for Simulation profile");

      param("Altitude Attitude Compensation", m_alt_attitude_compensation)
      .defaultValue("false")
      .description("Enable or disable attitude compensation for altitude");

      param("Altitude EMA gain", m_alt_ema_gain)
      .defaultValue("1.0")
      .description("Exponential moving average filter gain used in altitude");

      // Do not use the declination offset when simulating.
      m_use_declination = (m_ctx.profiles.isSelected("Simulation") == true);
      m_declination_defined = false;
      std::memset(m_beacons, 0, sizeof(m_beacons));
      m_num_beacons = 0;
      m_integ_yrate = false;
      m_z_ref = 0;
      m_diving = false;
      m_rpm = 0;

      m_gps_val_bits = 0;
      m_gvel_val_bits = IMC::GroundVelocity::VAL_VEL_X
                        | IMC::GroundVelocity::VAL_VEL_Y
                        | IMC::GroundVelocity::VAL_VEL_Z;

      m_wvel_val_bits = IMC::WaterVelocity::VAL_VEL_X
                        | IMC::WaterVelocity::VAL_VEL_Y
                        | IMC::WaterVelocity::VAL_VEL_Z;

      // Register callbacks.
      bind<IMC::Acceleration>(this);
      bind<IMC::AngularVelocity>(this);
      bind<IMC::Distance>(this);
      bind<IMC::Depth>(this);
      bind<IMC::DepthOffset>(this);
      bind<IMC::DesiredZ>(this);
      bind<IMC::EulerAngles>(this);
      bind<IMC::GpsFix>(this);
      bind<IMC::GroundVelocity>(this);
      bind<IMC::LblConfig>(this);
      bind<IMC::LblRange>(this);
      bind<IMC::Rpm>(this);
      bind<IMC::WaterVelocity>(this);
    }

    BasicNavigation::~BasicNavigation(void)
    { }

    void
    BasicNavigation::onUpdateParameters(void)
    {
      // Initialize timers.
      m_time_without_gps.setTop(m_without_gps_timeout);
      m_time_without_dvl.setTop(m_without_dvl_timeout);
      m_time_without_bdist.setTop(m_without_dvl_timeout);

      // Distance DVL to vehicle Center of Gravity is 0 in Simulation.
      if (m_ctx.profiles.isSelected("Simulation"))
      {
        m_dist_dvl_cg = 0.0;
        m_dist_lbl_gps = 0.0;
      }
    }

    void
    BasicNavigation::onResourceInitialization(void)
    {
      m_avg_heave = new Math::MovingAverage<double>(m_avg_heave_samples);

      reset();
    }

    void
    BasicNavigation::onEntityResolution(void)
    {
      m_depth_eid = resolveEntity(m_label_depth);
      m_ahrs_eid = resolveEntity(m_label_ahrs);
      m_agvel_eid = m_ahrs_eid;

      try
      {
        m_calibration_eid = resolveEntity(m_label_calibration);
      }
      catch (...)
      {
        m_calibration_eid = 0;
      }

      try
      {
        if (m_ctx.profiles.isSelected("Simulation"))
          m_alt_eid = resolveEntity(m_elabel_alt_sim);
        else
          m_alt_eid = resolveEntity(m_elabel_alt_hard);
      }
      catch (...)
      {
        m_alt_eid = 0;
      }
    }

    void
    BasicNavigation::onResourceRelease(void)
    {
      Memory::clear(m_origin);
      Memory::clear(m_avg_heave);

      for (unsigned i = 0; i < c_max_beacons; ++i)
        Memory::clear(m_beacons[i]);
    }

    void
    BasicNavigation::consume(const IMC::Acceleration* msg)
    {
      // Acceleration and AngularVelocity share same sensor entity label id.
      if (msg->getSourceEntity() != m_agvel_eid)
        return;

      m_accel_x_bfr += msg->x;
      m_accel_y_bfr += msg->y;
      m_accel_z_bfr += msg->z;
      ++m_accel_readings;
    }

    void
    BasicNavigation::consume(const IMC::AngularVelocity* msg)
    {
      if (msg->getSourceEntity() != m_agvel_eid)
        return;

      m_p_bfr += msg->x;
      m_q_bfr += msg->y;
      m_r_bfr += msg->z;
      ++m_angular_readings;
    }

    void
    BasicNavigation::consume(const IMC::Depth* msg)
    {
      if (msg->getSourceEntity() != m_depth_eid)
        return;

      m_depth_bfr += msg->value + m_depth_offset;
      ++m_depth_readings;
    }

    void
    BasicNavigation::consume(const IMC::DepthOffset* msg)
    {
      if (msg->getSourceEntity() != m_depth_eid)
        return;

      m_depth_offset = msg->value;
    }

    void
    BasicNavigation::consume(const IMC::DesiredZ* msg)
    {
      m_z_ref = msg->value;

      switch (msg->z_units)
      {
        case (IMC::Z_NONE):
          break;
        case (IMC::Z_DEPTH):
          if (m_z_ref > c_z_tol)
            m_diving = true;
        case (IMC::Z_ALTITUDE):
          if ((getAltitude() > 0) && (m_z_ref < (getAltitude() - c_z_tol)))
            m_diving = true;
        case (IMC::Z_HEIGHT):
          break;
      }

      if (std::abs(m_z_ref) < c_z_tol)
      {
        m_diving = false;
        m_reject_gps = false;
      }
    }

    void
    BasicNavigation::consume(const IMC::Distance* msg)
    {
      if (msg->getSourceEntity() != m_alt_eid)
        return;

      if (msg->validity == IMC::Distance::DV_INVALID)
        return;

      // Reset bottom Distance timer.
      m_time_without_bdist.reset();

      float value = msg->value;

      if (m_alt_attitude_compensation)
        value = value * std::cos(getRoll()) * std::cos(getPitch());

      // Initialize altitude.
      if (m_altitude == -1)
        m_altitude = value;
      else
        // Exponential moving average.
        m_altitude = m_altitude + m_alt_ema_gain * (value - m_altitude);
    }

    void
    BasicNavigation::consume(const IMC::EulerAngles* msg)
    {
      if (msg->getSourceEntity() == m_calibration_eid)
      {
        correctAlignment(msg->psi);
        m_phi_offset = msg->phi - getRoll();
        m_theta_offset = msg->theta - getPitch();
        debug("Euler Angles offset - phi, theta: %f | %f", m_phi_offset, m_theta_offset);
        m_alignment = true;
        return;
      }

      if (msg->getSourceEntity() != m_ahrs_eid)
        return;

      m_roll_bfr += getRoll() + Math::Angles::minimumSignedAngle(getRoll(), msg->phi + m_phi_offset);
      m_pitch_bfr += getPitch() + Math::Angles::minimumSignedAngle(getPitch(), msg->theta + m_theta_offset);
      m_heading_bfr += getYaw() + Math::Angles::minimumSignedAngle(getYaw(), msg->psi);

      ++m_euler_readings;

      if (m_declination_defined && m_use_declination)
        m_heading_bfr += m_declination;
    }

    void
    BasicNavigation::consume(const IMC::GpsFix* msg)
    {
      // GpsFix validation.
      m_gps_rej.utc_time = msg->utc_time;
      m_gps_rej.setTimeStamp(msg->getTimeStamp());

      // Speed over ground.
      if (msg->validity & IMC::GpsFix::GFV_VALID_SOG)
        m_gps_sog = msg->sog;

      // After GPS timeout, stop rejecting GPS by default.
      if (m_time_without_gps.overflow())
        m_reject_gps = false;

      // Rejecting GPS.
      if (m_reject_gps)
      {
        m_gps_rej.reason = IMC::GpsFixRejection::RR_LOST_VAL_BIT;
        dispatch(m_gps_rej, DF_KEEP_TIME);
        return;
      }

      // Integrating yaw rate to get heading.
      // this means we need very accurate navigation so GPS fixes
      // will be rejected if any validaty bit is lost between
      // consecutive GPS fixes.
      if (m_integ_yrate && m_diving)
      {
        // reinitialize if we exceed GPS timeout.
        if (m_time_without_gps.overflow())
          m_gps_val_bits = msg->validity;
        else
          m_gps_val_bits |= msg->validity;

        // if different, at least one previous valid bit is now invalid.
        if (m_gps_val_bits != msg->validity)
        {
          // Start rejecting GPS fixes.
          m_reject_gps = true;
          m_gps_rej.reason = IMC::GpsFixRejection::RR_LOST_VAL_BIT;
          dispatch(m_gps_rej, DF_KEEP_TIME);
          return;
        }
      }

      // Check fix validity.
      if ((msg->validity & IMC::GpsFix::GFV_VALID_POS) == 0)
      {
        m_gps_rej.reason = IMC::GpsFixRejection::RR_INVALID;
        dispatch(m_gps_rej, DF_KEEP_TIME);
        return;
      }

      // Check if we have valid Horizontal Accuracy index.
      if (msg->validity & IMC::GpsFix::GFV_VALID_HACC)
      {
        // Update GPS measurement noise parameters.
        updateKalmanGpsParameters(msg->hacc);

        // Check if it is above Maximum Horizontal Accuracy.
        if (msg->hacc > m_max_hacc)
        {
          m_gps_rej.reason = IMC::GpsFixRejection::RR_ABOVE_MAX_HACC;
          dispatch(m_gps_rej, DF_KEEP_TIME);
          return;
        }
      }
      else
      {
        // Horizontal Dilution of Precision.
        if (msg->hdop > m_max_hdop)
        {
          m_gps_rej.reason = IMC::GpsFixRejection::RR_ABOVE_MAX_HDOP;
          dispatch(m_gps_rej, DF_KEEP_TIME);
          return;
        }
      }

      // Check current declination value.
      checkDeclination(msg->lat, msg->lon, msg->height);

      m_last_lat = msg->lat;
      m_last_lon = msg->lon;
      m_last_hae = msg->height;

      // Start navigation if filter not active.
      if (!m_active)
      {
        // Navigation self-initialisation.
        startNavigation(msg);
        return;
      }

      // Not sure about altitude.
      double x = 0;
      double y = 0;
      Coordinates::WGS84::displacement(m_origin->lat, m_origin->lon, m_origin->height,
                                       msg->lat, msg->lon, msg->height,
                                       &x, &y, &m_last_z);

      // Check distance to current LLH origin.
      if (Math::norm(x, y) > m_max_dis2ref)
      {
        // Redefine origin.
        Memory::replace(m_origin, new IMC::GpsFix(*msg));

        // Save message to cache.
        IMC::CacheControl cop;
        cop.op = IMC::CacheControl::COP_STORE;
        cop.message.set(*msg);
        dispatch(cop);

        // Save reference in EstimatedState message.
        m_estate.lat = msg->lat;
        m_estate.lon = msg->lon;
        m_estate.height = msg->height;

        // Set position estimate at the origin.
        m_kal.setState(STATE_X, 0);
        m_kal.setState(STATE_Y, 0);

        // Recalculate LBL positions.
        correctLBL();

        debug("defined new navigation reference");
        return;
      }

      // Call GPS EKF functions to assign output values.
      runKalmanGPS(x, y);
    }

    void
    BasicNavigation::consume(const IMC::GroundVelocity* msg)
    {
      m_gvel = *msg;
      // Correct for the distance between center of gravity and dvl.
      m_gvel.y = msg->y - m_dist_dvl_cg * BasicNavigation::getAngularVelocity(AXIS_Z);

      if (msg->validity != m_gvel_val_bits)
        return;

      m_dvl_rej.setTimeStamp(msg->getTimeStamp());
      m_dvl_rej.type = IMC::DvlRejection::TYPE_GV;

      double tstep = m_dvl_gv_tstep.getDelta();

      // Check if we have a valid time delta.
      if ((tstep > 0) && (tstep < m_dvl_time_rel_thresh))
      {
        // Innovation threshold checking in the x-axis.
        if (std::abs(m_gvel.x - m_gvel_previous.x) > m_dvl_rel_thresh[0])
        {
          m_dvl_rej.reason = IMC::DvlRejection::RR_INNOV_THRESHOLD_X;
          m_dvl_rej.value = std::abs(m_gvel.x - m_gvel_previous.x);
          m_dvl_rej.timestep = tstep;
          dispatch(m_dvl_rej, DF_KEEP_TIME);
          return;
        }

        // Innovation threshold checking in the y-axis.
        if (std::abs(m_gvel.y - m_gvel_previous.y) > m_dvl_rel_thresh[1])
        {
          m_dvl_rej.reason = IMC::DvlRejection::RR_INNOV_THRESHOLD_Y;
          m_dvl_rej.value = std::abs(m_gvel.y - m_gvel_previous.y);
          m_dvl_rej.timestep = tstep;
          dispatch(m_dvl_rej, DF_KEEP_TIME);
          return;
        }
      }

      // Absolute filter.
      if (std::abs(m_gvel.x) > m_dvl_abs_thresh[0])
      {
        m_dvl_rej.reason = IMC::DvlRejection::RR_ABS_THRESHOLD_X;
        m_dvl_rej.value = std::abs(m_gvel.x);
        m_dvl_rej.timestep = 0;
        dispatch(m_dvl_rej, DF_KEEP_TIME);
        return;
      }

      if (std::abs(m_gvel.y) > m_dvl_abs_thresh[1])
      {
        m_dvl_rej.reason = IMC::DvlRejection::RR_ABS_THRESHOLD_Y;
        m_dvl_rej.value = std::abs(m_gvel.y);
        m_dvl_rej.timestep = 0;
        dispatch(m_dvl_rej, DF_KEEP_TIME);
        return;
      }

      m_time_without_dvl.reset();
      m_valid_gv = true;

      // Store accepted msg.
      m_gvel_previous = *msg;
      m_gvel_previous.y = m_gvel.y;
    }

    void
    BasicNavigation::consume(const IMC::LblConfig* msg)
    {
      if (msg->op != IMC::LblConfig::OP_SET_CFG)
          return;

      // Save message to cache.
      IMC::CacheControl cop;
      cop.op = IMC::CacheControl::COP_STORE;
      cop.message.set(*msg);
      dispatch(cop);

      m_lbl_log_beacons = false;

      if (m_origin == NULL)
      {
        debug("There is no reference yet. LBL configuration is stored. Waiting for GPS fix");
        m_lbl_log_beacons = true;
        m_lbl_cfg = *msg;
        return;
      }

      m_num_beacons = 0;

      IMC::MessageList<IMC::LblBeacon>::const_iterator itr = msg->beacons.begin();
      for (unsigned i = 0; itr != msg->beacons.end(); ++itr, ++i)
        addBeacon(i, *itr);
      onConsumeLblConfig();
    }

    void
    BasicNavigation::consume(const IMC::LblRange* msg)
    {
      if (!m_active)
        return;

      // LBL range validation.
      m_lbl_ac.id = msg->id;
      m_lbl_ac.range = msg->range;
      m_lbl_ac.setTimeStamp(msg->getTimeStamp());

      // Get beacon position.
      uint8_t beacon = msg->id;
      float range = msg->range;

      if ((m_beacons[beacon] == 0) || (beacon > m_num_beacons - 1) || (BasicNavigation::rejectLbl()))
      {
        m_lbl_ac.acceptance = IMC::LblRangeAcceptance::RR_NO_INFO;
        dispatch(m_lbl_ac, DF_KEEP_TIME);
        return;
      }

      // Reject LBL ranges when GPS is available.
      if (!m_time_without_gps.overflow())
      {
        m_lbl_ac.acceptance = IMC::LblRangeAcceptance::RR_AT_SURFACE;
        dispatch(m_lbl_ac, DF_KEEP_TIME);
        return;
      }

      // Compute expected range.
      double dx = m_kal.getState(STATE_X) + m_dist_lbl_gps * std::cos(getYaw()) - m_beacons[beacon]->x;
      double dy = m_kal.getState(STATE_Y) + m_dist_lbl_gps * std::sin(getYaw()) - m_beacons[beacon]->y;
      double dz = getDepth() - m_beacons[beacon]->depth;
      double exp_range = std::sqrt(dx * dx + dy * dy + dz * dz);

      if (!exp_range)
      {
        // Singular point (don't use).
        m_lbl_ac.acceptance = IMC::LblRangeAcceptance::RR_SINGULAR;
        dispatch(m_lbl_ac, DF_KEEP_TIME);
      }
      else
        runKalmanLBL((int)beacon, range, dx, dy, exp_range);
    }

    void
    BasicNavigation::consume(const IMC::Rpm* msg)
    {
      m_rpm = msg->value;
    }

    void
    BasicNavigation::consume(const IMC::WaterVelocity* msg)
    {
      m_wvel = *msg;
      // Correct for the distance between center of gravity and dvl.
      m_wvel.y = msg->y - m_dist_dvl_cg * BasicNavigation::getAngularVelocity(AXIS_Z);

      if (msg->validity != m_wvel_val_bits)
        return;

      m_dvl_rej.setTimeStamp(msg->getTimeStamp());
      m_dvl_rej.type = IMC::DvlRejection::TYPE_WV;

      double tstep = m_dvl_wv_tstep.getDelta();

      // Check if we have a valid time delta.
      if ((tstep > 0) && (tstep < m_dvl_time_rel_thresh))
      {
        // Innovation threshold checking in the x-axis.
        if (std::abs(m_wvel.x - m_wvel_previous.x) > m_dvl_rel_thresh[0])
        {
          m_dvl_rej.reason = IMC::DvlRejection::RR_INNOV_THRESHOLD_X;
          m_dvl_rej.value = std::abs(m_wvel.x - m_wvel_previous.x);
          m_dvl_rej.timestep = tstep;
          dispatch(m_dvl_rej, DF_KEEP_TIME);
          return;
        }

        // Innovation threshold checking in the y-axis.
        if (std::abs(m_wvel.y - m_wvel_previous.y) > m_dvl_rel_thresh[1])
        {
          m_dvl_rej.reason = IMC::DvlRejection::RR_INNOV_THRESHOLD_Y;
          m_dvl_rej.value = std::abs(m_wvel.y - m_wvel_previous.y);
          m_dvl_rej.timestep = tstep;
          dispatch(m_dvl_rej, DF_KEEP_TIME);
          return;
        }
      }

      // Absolute filter.
      if (std::abs(m_wvel.x) > m_dvl_abs_thresh[0])
      {
        m_dvl_rej.reason = IMC::DvlRejection::RR_ABS_THRESHOLD_X;
        m_dvl_rej.value = std::abs(m_wvel.x);
        m_dvl_rej.timestep = 0;
        dispatch(m_dvl_rej, DF_KEEP_TIME);
        return;
      }

      if (std::abs(m_wvel.y) > m_dvl_abs_thresh[1])
      {
        m_dvl_rej.reason = IMC::DvlRejection::RR_ABS_THRESHOLD_Y;
        m_dvl_rej.value = std::abs(m_wvel.y);
        m_dvl_rej.timestep = 0;
        dispatch(m_dvl_rej, DF_KEEP_TIME);
        return;
      }

      m_time_without_dvl.reset();
      m_valid_wv = true;

      // Store accepted msg.
      m_wvel_previous = *msg;
      m_wvel_previous.y = m_wvel.y;
    }

    void
    BasicNavigation::startNavigation(const IMC::GpsFix* msg)
    {
      Memory::replace(m_origin, new IMC::GpsFix(*msg));

      // Save message to cache.
      IMC::CacheControl cop;
      cop.op = IMC::CacheControl::COP_STORE;
      cop.message.set(*msg);
      dispatch(cop);

      m_active = setup();

      m_navstate = SM_STATE_BOOT;
      setEntityState(IMC::EntityState::ESTA_BOOT, Status::CODE_WAIT_CONVERGE);
    }

    void
    BasicNavigation::reset(void)
    {
      m_last_lat = 0;
      m_last_lon = 0;
      m_last_hae = 0;
      m_last_z = 0;

      m_gps_sog = 0;
      m_heading = 0.0;
      m_phi_offset = 0.0;
      m_theta_offset = 0.0;
      m_altitude = -1;
      m_alignment = false;

      m_reject_gps = false;
      m_lbl_log_beacons = false;

      m_navstate = SM_STATE_IDLE;

      setEntityState(IMC::EntityState::ESTA_BOOT, Status::CODE_WAIT_GPS_FIX);

      m_valid_gv = false;
      m_valid_wv = false;

      resetBuffers();
    }

    bool
    BasicNavigation::setup(void)
    {
      reset();

      if (m_origin == NULL)
        return false;

      m_estate.lat = m_origin->lat;
      m_estate.lon = m_origin->lon;
      m_estate.height = m_origin->height;

      // Set position of the vehicle at the origin and reset filter state.
      m_kal.resetState();

      // Possibly correct LBL locations.
      correctLBL();

      debug("setup completed");
      return true;
    }

    void
    BasicNavigation::correctLBL(void)
    {
      // Correct LBL positions.
      for (unsigned i = 0; i < c_max_beacons; i++)
      {
        if (m_beacons[i])
        {
          // This is relative to surface, thus using 0.0 as height value.
          Coordinates::WGS84::displacement(m_origin->lat, m_origin->lon, 0.0,
                                           m_beacons[i]->lat, m_beacons[i]->lon, m_beacons[i]->depth,
                                           &m_beacons[i]->x, &m_beacons[i]->y);

          debug("correcting beacon %d position (%0.2f, %0.2f, %0.2f)", i, m_beacons[i]->x, m_beacons[i]->y, m_beacons[i]->depth);
        }
      }
    }

    void
    BasicNavigation::onConsumeLblConfig(void)
    {
      // do nothing.
    }

    void
    BasicNavigation::updateKalmanGpsParameters(double hacc)
    {
      // do nothing.
      (void)hacc;
    }

    void
    BasicNavigation::runKalmanGPS(double x, double y)
    {
      // do nothing.
      (void)x;
      (void)y;
    }

    void
    BasicNavigation::runKalmanLBL(int beacon, float range, double dx, double dy, double exp_range)
    {
      // do nothing.
      (void)beacon;
      (void)range;
      (void)dx;
      (void)dy;
      (void)exp_range;
    }

    void
    BasicNavigation::runKalmanDVL(void)
    {
      // do nothing.
    }

    void
    BasicNavigation::correctAlignment(double psi)
    {
      // do nothing.
      (void)psi;
    }

    void
    BasicNavigation::onDispatchNavigation(void)
    {
      m_estate.x = m_kal.getState(STATE_X);
      m_estate.y = m_kal.getState(STATE_Y);
      m_estate.z = m_last_z + BasicNavigation::getDepth();
      m_estate.phi = BasicNavigation::getRoll();
      m_estate.theta = BasicNavigation::getPitch();

      // Update Euler Angles derivatives when
      // Angular Velocity readings are not available.
      if (!gotAngularReadings())
      {
        m_drv_roll.update(m_estate.phi);
        m_drv_pitch.update(m_estate.theta);
        m_estate.p = BasicNavigation::produceAngularVelocity(AXIS_X);
        m_estate.q = BasicNavigation::produceAngularVelocity(AXIS_Y);
      }
      else
      {
        m_estate.p = BasicNavigation::getAngularVelocity(AXIS_X);
        m_estate.q = BasicNavigation::getAngularVelocity(AXIS_Y);
      }

      m_estate.alt = BasicNavigation::getAltitude();

      m_estate.depth = BasicNavigation::getDepth();
      m_estate.w = m_avg_heave->update(m_drv_heave.update(m_estate.depth));

      // Velocity in the navigation frame.
      Coordinates::BodyFixedFrame::toInertialFrame(m_estate.phi, m_estate.theta, m_estate.psi,
                                                   m_estate.u, m_estate.v, m_estate.w,
                                                   &m_estate.vx, &m_estate.vy, &m_estate.vz);

      m_uncertainty.x = m_kal.getCovariance(STATE_X, STATE_X);
      m_uncertainty.y = m_kal.getCovariance(STATE_Y, STATE_Y);
      m_navdata.cyaw = m_heading;
    }

    void
    BasicNavigation::addBeacon(unsigned id, const IMC::LblBeacon* msg)
    {
      if (id >= c_max_beacons)
      {
        err(DTR("beacon id %d is greater than %d"), id, c_max_beacons);
        return;
      }

      Memory::clear(m_beacons[id]);

      if (msg == NULL)
        return;

      if (id + 1 > m_num_beacons)
        m_num_beacons = id + 1;

      LblBeaconXYZ* bp = new LblBeaconXYZ;
      bp->lat = msg->lat;
      bp->lon = msg->lon;
      bp->depth = msg->depth;
      // This is relative to surface thus using 0.0 as height reference.
      Coordinates::WGS84::displacement(m_origin->lat, m_origin->lon, 0.0,
                                       msg->lat, msg->lon, msg->depth,
                                       &(bp->x), &(bp->y));

      m_beacons[id] = bp;

      debug("setting beacon %s (%0.2f, %0.2f, %0.2f)", msg->beacon.c_str(),
            m_beacons[id]->x, m_beacons[id]->y, m_beacons[id]->depth);
    }

    bool
    BasicNavigation::isActive(void)
    {
      if (m_active)
        return true;

      if (gotEulerReadings())
      {
        IMC::EstimatedState estate;
        estate.lat = m_last_lat;
        estate.lon = m_last_lon;
        estate.height = m_last_hae;
        estate.phi = getRoll();
        estate.theta = getPitch();
        estate.psi = getYaw();
        estate.depth = getDepth();
        m_heading = estate.psi;
        updateEuler(c_wma_filter);
        updateDepth(c_wma_filter);

        if (gotAngularReadings())
        {
          m_estate.p = BasicNavigation::getAngularVelocity(AXIS_X);
          m_estate.q = BasicNavigation::getAngularVelocity(AXIS_Y);
          m_estate.r = BasicNavigation::getAngularVelocity(AXIS_Z);
          updateAngularVelocities(c_wma_filter);
        }

        dispatch(estate);
      }

      return false;
    }

    void
    BasicNavigation::reportToBus(void)
    {
      double tstamp = Time::Clock::getSinceEpoch();
      m_estate.setTimeStamp(tstamp);
      m_uncertainty.setTimeStamp(tstamp);
      m_navdata.setTimeStamp(tstamp);
      m_ewvel.setTimeStamp(tstamp);

      dispatch(m_estate, DF_KEEP_TIME);
      dispatch(m_uncertainty, DF_KEEP_TIME);
      dispatch(m_navdata, DF_KEEP_TIME);
      dispatch(m_ewvel, DF_KEEP_TIME);
    }

    void
    BasicNavigation::updateBuffers(float filter)
    {
      // Reinitialize buffers.
      updateAcceleration(filter);
      updateAngularVelocities(filter);
      updateDepth(filter);
      updateEuler(filter);
    }

    void
    BasicNavigation::resetAcceleration(void)
    {
      m_accel_x_bfr = 0;
      m_accel_y_bfr = 0;
      m_accel_z_bfr = 0;
      m_accel_readings = 0;
    }

    void
    BasicNavigation::resetAngularVelocity(void)
    {
      m_p_bfr = 0.0;
      m_q_bfr = 0.0;
      m_r_bfr = 0.0;
      m_angular_readings = 0.0;
    }

    void
    BasicNavigation::resetDepth(void)
    {
      m_depth_bfr = 0;
      m_depth_readings = 0;
      m_depth_offset = 0.0;
    }

    void
    BasicNavigation::resetEulerAngles(void)
    {
      m_heading_bfr = 0.0;
      m_roll_bfr = 0.0;
      m_pitch_bfr = 0.0;
      m_euler_readings = 0.0;
    }

    void
    BasicNavigation::resetBuffers(void)
    {
      resetAcceleration();
      resetAngularVelocity();
      resetDepth();
      resetEulerAngles();
    }

    void
    BasicNavigation::checkUncertainty(void)
    {
      // Compute maximum horizontal position variance value
      float hpos_var = std::max(m_kal.getCovariance(STATE_X, STATE_X), m_kal.getCovariance(STATE_Y, STATE_Y));

      // Check if it exceeds the specified threshold value.
      if (hpos_var > m_max_hpos_var)
      {
        switch (m_navstate)
        {
          case SM_STATE_BOOT:
            // do nothing
            break;
          case SM_STATE_NORMAL:
            setEntityState(IMC::EntityState::ESTA_ERROR, getUncertaintyMessage(hpos_var));
            m_navstate = SM_STATE_UNSAFE; // Change state
            break;
          case SM_STATE_UNSAFE:
            // do nothing;
            break;
          default:
            debug("caught unexpected navigation state transition");
            break;
        }
      }
      else
      {
        switch (m_navstate)
        {
          case SM_STATE_BOOT:
            setEntityState(IMC::EntityState::ESTA_NORMAL, getUncertaintyMessage(hpos_var));
            break;
          case SM_STATE_NORMAL:
            // do nothing;
            break;
          case SM_STATE_UNSAFE:
            setEntityState(IMC::EntityState::ESTA_NORMAL, getUncertaintyMessage(hpos_var));
            break;
          default:
            debug("caught unexpected navigation state transition");
            break;
        }
        m_navstate = SM_STATE_NORMAL;
      }
    }

    void
    BasicNavigation::checkDeclination(double lat, double lon, double height)
    {
      if (!m_declination_defined && m_use_declination)
      {
        // Compute declination value
        // -- note: this is done only once, thus the short-lived wmm object
        Coordinates::WMM wmm(m_ctx.dir_cfg);
        m_declination = wmm.declination(lat, lon, height);
        m_declination_defined = true;
      }
    }

    void
    BasicNavigation::extractEarthRotation(double& p, double& q, double& r)
    {
      // Insert euler angles into row matrix.
      Math::Matrix ea(3,1);
      ea(0) = getRoll();
      ea(1) = getPitch();
      ea(2) = getYaw();

      // Earth rotation vector.
      Math::Matrix we(3,1);
      we(0) = Math::c_earth_rotation * std::cos(m_last_lat);
      we(1) = 0.0;
      we(2) = - Math::c_earth_rotation * std::sin(m_last_lat);

      // Sensed angular velocities due to Earth rotation effect.
      Math::Matrix av(3,1);
      av = inverse(ea.toDCM()) * we;

      // Extract from angular velocities measurements.
      p -= av(0);
      q -= av(1);
      r -= av(2);
    }
  }
}
