//***************************************************************************
// Copyright (C) 2007-2013 Laboratório de Sistemas e Tecnologia Subaquática *
// Departamento de Engenharia Electrotécnica e de Computadores              *
// Rua Dr. Roberto Frias, 4200-465 Porto, Portugal                          *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

// ISO C++ 98 headers.
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstddef>

// DUNE headers.
#include <DUNE/DUNE.hpp>

namespace Transports
{
  namespace Logging
  {
    using DUNE_NAMESPACES;

    // Bytes per Mebibyte.
    static const unsigned c_bytes_per_mib = 1048576U;

    struct Arguments
    {
      // Flush interval.
      float flush_interval;
      // List of messages to log.
      std::vector<std::string> messages;
      // Volume folders.
      std::vector<std::string> lsf_volumes;
      // Volume size.
      unsigned lsf_volume_size;
      // Compression method.
      std::string lsf_compression;
    };

    struct Task: public Tasks::Task
    {
      // Timestamp of last flush.
      double m_last_flush;
      // Label of current log.
      std::string m_label;
      // Current log directory.
      Path m_dir;
      // Reference time of log.
      double m_ref_time;
      // Current LSF volume directory.
      std::string m_volume_dir;
      // Compression format.
      Compression::Methods m_compression;
      // Output file stream for LSF/LSF_GZ formats.
      std::ostream* m_lsf;
      // Path to LSF file.
      Path m_lsf_file;
      // Serialization buffer.
      ByteBuffer m_buffer;
      // Logging control message.
      IMC::LoggingControl m_log_ctl;
      // True if logging is enabled.
      bool m_active;
      // Task arguments.
      Arguments m_args;

      Task(const std::string& name, Tasks::Context& ctx):
        Tasks::Task(name, ctx),
        m_last_flush(0),
        m_lsf(NULL),
        m_active(true)
      {
        // Define configuration parameters.
        param("Flush Interval", m_args.flush_interval)
        .defaultValue("5.0")
        .units(Units::Second)
        .description("Number of second to wait before forcing data to be written to disk");

        param("LSF Compression Method", m_args.lsf_compression)
        .defaultValue("none")
        .description("Compression method");

        param("LSF Volume Size", m_args.lsf_volume_size)
        .units(Units::Mebibyte)
        .defaultValue("0");

        param("LSF Volume Directories", m_args.lsf_volumes)
        .defaultValue("");

        param("Transports", m_args.messages)
        .defaultValue("");

        m_log_ctl.setSource(getSystemId());

        bind<IMC::CacheControl>(this);
        bind<IMC::LoggingControl>(this);
        bind<IMC::PowerOperation>(this);
      }

      ~Task(void)
      {
        onResourceRelease();
      }

      void
      onResourceInitialization(void)
      {
        // Initialize entity state.
        setEntityState(IMC::EntityState::ESTA_NORMAL, Status::CODE_ACTIVE);
      }

      void
      onResourceRelease(void)
      {
        Memory::clear(m_lsf);
      }

      void
      onUpdateParameters(void)
      {
        m_compression = Compression::Factory::method(m_args.lsf_compression);
        if (m_args.lsf_volumes.empty())
          m_args.lsf_volumes.push_back("");

        bind(this, m_args.messages);
      }

      void
      consume(const IMC::LoggingControl* msg)
      {
        if (m_active)
          logMessage(msg);

        switch (msg->op)
        {
          case IMC::LoggingControl::COP_REQUEST_START:
            tryStartLog(msg->name);
            break;
          case IMC::LoggingControl::COP_REQUEST_STOP:
            stopLog(false);
            break;
          case IMC::LoggingControl::COP_REQUEST_CURRENT_NAME:
            {
              IMC::LoggingControl lc;
              lc.op = IMC::LoggingControl::COP_CURRENT_NAME;
              lc.name = m_log_ctl.name;
              dispatchReply(*msg, lc);
            }
            break;
        }
      }

      void
      consume(const IMC::CacheControl* msg)
      {
        if (msg->op != IMC::CacheControl::COP_COPY_COMPLETE)
          return;

        logFile(msg->snapshot);

        try
        {
          Path(msg->snapshot).remove();
        }
        catch (std::exception& e)
        {
          war(DTR("failed to remove cache snapshot: %s"), e.what());
        }
      }

      void
      consume(const IMC::PowerOperation* msg)
      {
        if (msg->op == IMC::PowerOperation::POP_PWR_DOWN_IP)
        {
          stopLog(false);
          dune_term.close();
        }
        else if (msg->op == IMC::PowerOperation::POP_PWR_DOWN_ABORTED)
        {
          // FIXME: Resume.
        }
      }

      void
      consume(const IMC::Message* msg)
      {
        if (m_active)
          logMessage(msg);
      }

      bool
      changeVolumeDirectory(void)
      {
        if (m_args.lsf_volumes.empty())
          return false;

        m_volume_dir = m_args.lsf_volumes.front();
        m_args.lsf_volumes.erase(m_args.lsf_volumes.begin());
        return true;
      }

      void
      logAuxFiles(double time_ref)
      {
        // Request a snapshot of the cache.
        IMC::CacheControl cc;
        cc.op = IMC::CacheControl::COP_COPY;
        cc.snapshot = (m_ctx.dir_log / String::str("cache-%0.6f.lsf", time_ref)).str();
        dispatch(cc);

        // Copy IMC XML to log directory.
        Path imc_dst = m_dir / "IMC.xml";
        Path imc_src = m_ctx.dir_cfg / "xml" / "IMC.xml";

        try
        {
          imc_src.copy(imc_dst);
        }
        catch (System::Error& e)
        {
          err(DTR("unable to copy IMC XML to '%s': %s"),
              imc_dst.c_str(), e.what());
        }

        // Copy current configuration file to log directory.
        Path cfg_path = m_dir / "Config.ini";
        Path out_path = m_dir / "Output.txt";
        m_ctx.config.writeToFile(cfg_path.c_str());
        dune_term.open(out_path.c_str());

        // Log entities.
        std::vector<EntityDataBase::Entity*> devs;
        m_ctx.entities.contents(devs);
        for (unsigned int i = 0; i < devs.size(); ++i)
        {
          IMC::EntityInfo info;
          info.setTimeStamp(m_ref_time);
          info.setSource(getSystemId());
          info.id = devs[i]->id;
          info.label = devs[i]->label;
          info.component = devs[i]->task_name;
          logMessage(&info);
        }

        std::vector<IMC::TransportBindings *> bindings = m_ctx.mbus.getBindings();
        for (unsigned int i = 0; i < bindings.size(); i++)
        {
          bindings[i]->setSource(getSystemId());
          bindings[i]->setTimeStamp();
          logMessage((IMC::Message *)bindings[i]);
        }
      }

      void
      logFile(const std::string& file)
      {
        std::ifstream ifs(file.c_str(), std::ios::binary);

        if (!ifs.is_open())
          return;

        char bfr[16 * 1024];
        while (!ifs.eof())
        {
          ifs.read(bfr, sizeof(bfr));
          m_lsf->write(bfr, ifs.gcount());
        }
      }

      void
      stopLog(bool keep_logging = true)
      {
        // If we are already stopped don't try to stop again.
        if (!m_active)
          return;

        m_active = keep_logging;

        // Inform everyone that we stopped logging (if we were logging).
        if (m_log_ctl.name.size() == 0)
          return;

        m_log_ctl.op = IMC::LoggingControl::COP_STOPPED;
        m_log_ctl.setTimeStamp();
        logMessage(&m_log_ctl);
        dispatch(m_log_ctl, DF_KEEP_TIME);

        inf(DTR("log stopped '%s'"), m_log_ctl.name.c_str());
        m_log_ctl.name.clear();

        onResourceRelease();
      }

      void
      startLog(std::string label)
      {
        m_active = true;

        m_ref_time = Clock::getSinceEpoch();

        // Replace white spaces with underscores.
        String::replaceWhiteSpace(label, '_');
        std::string dir_label = label;

        if (dir_label != "")
          dir_label = "_" + dir_label;

        m_dir = m_ctx.dir_log
        / m_volume_dir
        / Time::Format::getDateSafe(m_ref_time)
        / Time::Format::getTimeSafe(m_ref_time) + dir_label;

        // Create log directory.
        m_dir.create();

        // Stop current log.
        stopLog();

        m_lsf_file = m_dir / "Data.lsf" + Compression::Factory::extension(m_compression);

        if (m_compression == METHOD_UNKNOWN)
          m_lsf = new std::ofstream(m_lsf_file.c_str(), std::ios::binary);
        else
          m_lsf = new Compression::FileOutput(m_lsf_file.c_str(), m_compression);

        // Log LoggingControl to facilitate posterior conversion to LLF.
        m_log_ctl.op = IMC::LoggingControl::COP_STARTED;
        m_log_ctl.name = m_ctx.dir_log.suffix(m_dir);
        m_log_ctl.setTimeStamp(m_ref_time);
        logMessage(&m_log_ctl);
        dispatch(m_log_ctl, DF_KEEP_TIME);

        inf(DTR("log started '%s'"), m_log_ctl.name.c_str());

        logAuxFiles(m_ref_time);

        m_label = label;
      }

      void
      tryFlush(void)
      {
        double now = Clock::get();

        if (now > (m_last_flush + m_args.flush_interval))
        {
          tryRotate();
          m_last_flush = now;
        }
      }

      void
      tryRotate(void)
      {
        if (m_lsf == NULL)
          return;

        int64_t mib = Path(m_lsf_file).size();
        mib /= c_bytes_per_mib;

        m_lsf->flush();

        if ((m_args.lsf_volume_size > 0) && (mib >= m_args.lsf_volume_size))
          tryStartLog(m_label);

        int64_t available_mib = Path::storageAvailable(m_dir);
        available_mib /= c_bytes_per_mib;

        if (available_mib < (m_args.lsf_volume_size * 2))
          changeVolumeDirectory();
      }

      void
      tryStartLog(const std::string& label = "idle")
      {
        try
        {
          startLog(label);
        }
        catch (std::exception& e)
        {
          setEntityState(IMC::EntityState::ESTA_FAILURE, String::str(DTR("failed to start log, check available storage: %s"), e.what()));
          m_active = false;
          err("%s", e.what());
          war(DTR("waiting for human intervention"));
        }
      }

      void
      logMessage(const IMC::Message* msg)
      {
        IMC::Packet::serialize(msg, m_buffer);
        m_lsf->write(m_buffer.getBufferSigned(), m_buffer.getSize());
      }

      void
      onMain(void)
      {
        changeVolumeDirectory();

        tryStartLog();

        while (!stopping())
        {
          waitForMessages(1.0);
          if (m_active)
            tryFlush();
        }
      }
    };
  }
}

DUNE_TASK
