#pragma once

#include <string>
#include <sstream>
#include <ostream>

enum class LogLevel {
	Info,
	Warn,
	Error,
	Fatal
};

class LogMessage {
public:
	explicit LogMessage(LogLevel level) : m_level(level) {}
	
	LogMessage(LogMessage&& other) noexcept 
		: m_buffer(std::move(other.m_buffer)), m_level(other.m_level) {}

	~LogMessage();

	template <typename T>
	LogMessage& operator<<(const T& msg) {
		m_buffer << msg;
		return *this;
	}
	
	LogMessage& operator<<(std::ostream& (*)(std::ostream&)) {
		return *this;
	}

private:
	std::ostringstream m_buffer;
	LogLevel m_level;
};

class Logger {
public:
	static void init();
	static void shutdown();

	static LogMessage info() { return LogMessage(LogLevel::Info); }
	static LogMessage warn() { return LogMessage(LogLevel::Warn); }
	static LogMessage error() { return LogMessage(LogLevel::Error); }
	static LogMessage fatal() { return LogMessage(LogLevel::Fatal); }
};
