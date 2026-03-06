/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2017  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "server.h"

#include "game.h"

#include "configmanager.h"
#include "scriptmanager.h"
#include "rsa.h"
#include "protocolspectator.h"

#include "protocolold.h"
#include "protocollogin.h"
#include "protocolstatus.h"
#include "databasemanager.h"
#include "scheduler.h"
#include "databasetasks.h"

DatabaseTasks g_databaseTasks;
Dispatcher g_dispatcher;
Scheduler g_scheduler;

IPList serverIPs;

Game g_game;
ConfigManager g_config;
Monsters g_monsters;
Vocations g_vocations;
RSA g_RSA;

std::mutex g_loaderLock;
std::condition_variable g_loaderSignal;
std::unique_lock<std::mutex> g_loaderUniqueLock(g_loaderLock);

void startupErrorMessage(const std::string& errorStr)
{
	Logger::error() << "> ERROR: " << errorStr << std::endl;
	g_loaderSignal.notify_all();
}

void mainLoader(int argc, char* argv[], ServiceManager* servicer);

void badAllocationHandler()
{
	// Use functions that only use stack allocation
	puts("Allocation failed, server out of memory.\nDecrease the size of your map or compile in 64 bits mode.\n");
	getchar();
	exit(-1);
}

int main(int argc, char* argv[])
{
	Logger::init();

	// Setup bad allocation handler
	std::set_new_handler(badAllocationHandler);

	ServiceManager serviceManager;

	g_dispatcher.start();
	g_scheduler.start();

	g_dispatcher.addTask(createTask(std::bind(mainLoader, argc, argv, &serviceManager)));

	g_loaderSignal.wait(g_loaderUniqueLock);

	if (serviceManager.is_running()) {
		Logger::info() << ">> " << g_config.getString(ConfigManager::SERVER_NAME) << " Server Online!" << std::endl << std::endl;
		serviceManager.run();
	} else {
		Logger::warn() << ">> No services running. The server is NOT online." << std::endl;
		g_scheduler.shutdown();
		g_databaseTasks.shutdown();
		g_dispatcher.shutdown();
	}

	g_scheduler.join();
	g_databaseTasks.join();
	g_dispatcher.join();
	
	Logger::shutdown();
	return 0;
}

void mainLoader(int argc, char* argv[], ServiceManager* services)
{
	//dispatcher thread
	g_game.setGameState(GAME_STATE_STARTUP);

	srand(static_cast<unsigned int>(OTSYS_TIME()));
#ifdef _WIN32
	SetConsoleTitle(STATUS_SERVER_NAME);
#endif
	Logger::info() << "The " << STATUS_SERVER_NAME << " Global - Version: (" << STATUS_SERVER_VERSION << "." << MINOR_VERSION << " . " << REVISION_VERSION << ") - Codename: ( " << SOFTWARE_CODENAME << " )" << std::endl;
	Logger::info() << "Compiled with: " << BOOST_COMPILER << std::endl;
	Logger::info() << "Compiled on " << __DATE__ << ' ' << __TIME__ << " for platform ";

#if defined(__amd64__) || defined(_M_X64)
	Logger::info() << "x64" << std::endl;
#elif defined(__i386__) || defined(_M_IX86) || defined(_X86_)
	Logger::info() << "x86" << std::endl;
#elif defined(__arm__)
	Logger::info() << "ARM" << std::endl;
#else
	Logger::info() << "unknown" << std::endl;
#endif
	Logger::info() << std::endl;

	Logger::info() << "" << STATUS_SERVER_DEVELOPERS << "." << std::endl;
	Logger::info() << "" << GIT_REPO <<"." << std::endl;
	Logger::info() << std::endl;

	for (int i = 1; i < argc; ++i) {
		std::string param = argv[i];
		if (param == "-c" && (i + 1) < argc) {
			g_config.setConfigFileLua(argv[++i]);
		}
	}

	// read global config
	Logger::info() << ">> Loading config: " << g_config.getConfigFileLua() << std::endl;
	if (!g_config.load()) {
		startupErrorMessage("Unable to load Config File!");
		return;
	}

#ifdef _WIN32
	const std::string& defaultPriority = g_config.getString(ConfigManager::DEFAULT_PRIORITY);
	if (strcasecmp(defaultPriority.c_str(), "high") == 0) {
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	} else if (strcasecmp(defaultPriority.c_str(), "above-normal") == 0) {
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	}
#endif

	//set RSA key
	const char* p("14299623962416399520070177382898895550795403345466153217470516082934737582776038882967213386204600674145392845853859217990626450972452084065728686565928113");
	const char* q("7630979195970404721891201847792002125535401292779123937207447574596692788513647179235335529307251350570728407373705564708871762033017096809910315212884101");
	g_RSA.setKey(p, q);

	Logger::info() << ">> Establishing database connection..." << std::flush;

	if (!Database::getInstance().connect()) {
		startupErrorMessage("Failed to connect to database.");
		return;
	}

	Logger::info() << " MySQL " << Database::getClientVersion() << std::endl;

	// run database manager
	Logger::info() << ">> Running database manager" << std::endl;

	if (!DatabaseManager::isDatabaseSetup()) {
		startupErrorMessage("The database you have specified in config lua file is empty, please import the schema.sql to your database.");
		return;
	}
	g_databaseTasks.start();

	DatabaseManager::updateDatabase();

	if (g_config.getBoolean(ConfigManager::OPTIMIZE_DATABASE) && !DatabaseManager::optimizeTables()) {
		Logger::warn() << "> No tables were optimized." << std::endl;
	}

	//load vocations
	Logger::info() << ">> Loading vocations" << std::endl;
	if (!g_vocations.loadFromXml()) {
		startupErrorMessage("Unable to load vocations!");
		return;
	}

	// load item data
	Logger::info() << ">> Loading items" << std::endl;
	if (Item::items.loadFromOtb("data/items/items.otb") != ERROR_NONE) {
		startupErrorMessage("Unable to load items (OTB)!");
		return;
	}

	if (!Item::items.loadFromXml()) {
		startupErrorMessage("Unable to load items (XML)!");
		return;
	}

	Logger::info() << ">> Loading script systems" << std::endl;
	if (!ScriptingManager::getInstance().loadScriptSystems()) {
		startupErrorMessage("Failed to load script systems");
		return;
	}

	Logger::info() << ">> Loading monsters" << std::endl;
	if (!g_monsters.loadFromXml()) {
		startupErrorMessage("Unable to load monsters!");
		return;
	}

	Logger::info() << ">> Loading outfits" << std::endl;
	if (!Outfits::getInstance().loadFromXml()) {
		startupErrorMessage("Unable to load outfits!");
		return;
	}

	Logger::info() << ">> Checking world type... " << std::flush;
	std::string worldType = asLowerCaseString(g_config.getString(ConfigManager::WORLD_TYPE));
	if (worldType == "pvp") {
		g_game.setWorldType(WORLD_TYPE_PVP);
	} else if (worldType == "no-pvp") {
		g_game.setWorldType(WORLD_TYPE_NO_PVP);
	} else if (worldType == "pvp-enforced") {
		g_game.setWorldType(WORLD_TYPE_PVP_ENFORCED);
	} else {
		Logger::info() << std::endl;

		std::ostringstream ss;
		ss << "> ERROR: Unknown world type: " << g_config.getString(ConfigManager::WORLD_TYPE) << ", valid world types are: pvp, no-pvp and pvp-enforced.";
		startupErrorMessage(ss.str());
		return;
	}
	Logger::info() << asUpperCaseString(worldType) << std::endl;

	Logger::info() << ">> Loading map" << std::endl;
	if (!g_game.loadMainMap(g_config.getString(ConfigManager::MAP_NAME))) {
		startupErrorMessage("Failed to load map");
		return;
	}

	Logger::info() << ">> Initializing gamestate" << std::endl;
	g_game.setGameState(GAME_STATE_INIT);

	// Game client protocols
	services->add<ProtocolGame>(g_config.getNumber(ConfigManager::GAME_PORT));
	
	if (g_config.getBoolean(ConfigManager::ENABLE_LIVE_CASTING)) {
		ProtocolGame::clearLiveCastInfo();
		//services->add<ProtocolSpectator>(g_config.getNumber(ConfigManager::LIVE_CAST_PORT));
	}
	
	services->add<ProtocolLogin>(g_config.getNumber(ConfigManager::LOGIN_PORT));

	// OT protocols
	services->add<ProtocolStatus>(g_config.getNumber(ConfigManager::STATUS_PORT));

	// Legacy login protocol
	services->add<ProtocolOld>(g_config.getNumber(ConfigManager::LOGIN_PORT));

	RentPeriod_t rentPeriod;
	std::string strRentPeriod = asLowerCaseString(g_config.getString(ConfigManager::HOUSE_RENT_PERIOD));

	if (strRentPeriod == "yearly") {
		rentPeriod = RENTPERIOD_YEARLY;
	} else if (strRentPeriod == "weekly") {
		rentPeriod = RENTPERIOD_WEEKLY;
	} else if (strRentPeriod == "monthly") {
		rentPeriod = RENTPERIOD_MONTHLY;
	} else if (strRentPeriod == "daily") {
		rentPeriod = RENTPERIOD_DAILY;
	} else {
		rentPeriod = RENTPERIOD_NEVER;
	}

	g_game.map.houses.payHouses(rentPeriod);

	Logger::info() << ">> Loaded all modules, server starting up..." << std::endl;

	// Resolve the configured public IP first (from config.lua)
	// This MUST be first so clients always get the correct IP to connect to the game server.
	std::string ip;
	ip = g_config.getString(ConfigManager::IP);

	uint32_t resolvedIp = inet_addr(ip.c_str());
	if (resolvedIp == INADDR_NONE) {
		struct addrinfo hints = {};
		hints.ai_family = AF_INET;
		struct addrinfo* res = nullptr;
		if (getaddrinfo(ip.c_str(), nullptr, &hints, &res) == 0 && res) {
			resolvedIp = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
			freeaddrinfo(res);
		} else {
			if (res) { freeaddrinfo(res); }
			startupErrorMessage("Cannot resolve IP/hostname: '" + ip + "'. Check the 'ip' setting in config.lua.");
		}
	}

	std::pair<uint32_t, uint32_t> IpNetMask;
	// Add configured IP first with mask 0 (matches any client subnet - wildcard fallback)
	IpNetMask.first = resolvedIp;
	IpNetMask.second = 0;
	serverIPs.push_back(IpNetMask);

	// Add localhost
	IpNetMask.first = inet_addr("127.0.0.1");
	IpNetMask.second = 0xFFFFFFFF;
	serverIPs.push_back(IpNetMask);

	// Add hostname-resolved IPs (e.g., internal Docker IPs) for LAN clients
	char szHostName[128];
	if (gethostname(szHostName, 128) == 0) {
		struct addrinfo hints = {};
		hints.ai_family = AF_INET;
		struct addrinfo* res = nullptr;
		if (getaddrinfo(szHostName, nullptr, &hints, &res) == 0) {
			for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
				IpNetMask.first = reinterpret_cast<struct sockaddr_in*>(p->ai_addr)->sin_addr.s_addr;
				IpNetMask.second = 0x0000FFFF;
				serverIPs.push_back(IpNetMask);
			}
			freeaddrinfo(res);
		}
	}

#ifndef _WIN32
	if (getuid() == 0 || geteuid() == 0) {
		Logger::warn() << "> Warning: " << STATUS_SERVER_NAME << " has been executed as root user, please consider running it as a normal user." << std::endl;
	}
#endif

	g_game.start(services);
	g_game.setGameState(GAME_STATE_NORMAL);
	g_loaderSignal.notify_all();
}
