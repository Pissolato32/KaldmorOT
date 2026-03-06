#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/file_sinks.h>
#include <iostream>
#include <boost/filesystem.hpp>

static std::shared_ptr<spdlog::logger> g_logger;

void Logger::init()
{
	try {
		boost::filesystem::create_directory("logs");

		// Enble async mode for all loggers created after this point
#ifndef _WIN32
		spdlog::set_async_mode(8192);
#endif
		
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
		sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>("logs/server.log", 23, 59));
        
		g_logger = std::make_shared<spdlog::logger>("server", sinks.begin(), sinks.end());
		g_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
		g_logger->flush_on(spdlog::level::warn);
		
	} catch (const spdlog::spdlog_ex& ex) {
		std::cout << "Log initialization failed: " << ex.what() << std::endl;
	}
}

void Logger::shutdown()
{
	spdlog::drop_all();
}

LogMessage::~LogMessage()
{
	std::string str = m_buffer.str();
	if (str.empty()) return;

	if (g_logger) {
		switch (m_level) {
			case LogLevel::Info: g_logger->info("{}", str); break;
			case LogLevel::Warn: g_logger->warn("{}", str); break;
			case LogLevel::Error: g_logger->error("{}", str); break;
			case LogLevel::Fatal: g_logger->critical("{}", str); break;
			default: break;
		}
	} else {
		// Fallback if Logger is not initialized
		std::cout << str << std::endl;
	}
}
