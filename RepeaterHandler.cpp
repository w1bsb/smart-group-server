/*
 *   Copyright (C) 2010-2015 by Jonathan Naylor G4KLX
 *   Copyright (c) 2017 by Thomas A. Early N7TAE
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <cassert>
#include <cstdlib>
#include <list>
#include <cstring>

#include "RepeaterHandler.h"
#include "DExtraHandler.h"
#include "DPlusHandler.h"
#include "DStarDefines.h"
#include "DCSHandler.h"
#include "CCSHandler.h"
#include "HeaderData.h"
#include "DDHandler.h"
#include "AMBEData.h"
#include "Utils.h"

const unsigned int  ETHERNET_ADDRESS_LENGTH = 6U;

const unsigned char ETHERNET_BROADCAST_ADDRESS[] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
// Multicast address '01:00:5E:00:00:01' - IP: '224.0.0.1' (to all)
const unsigned char TOALL_MULTICAST_ADDRESS[] = {0x01U, 0x00U, 0x5EU, 0x00U, 0x00U, 0x01U};
// Multicast address '01:00:5E:00:00:23' - IP: '224.0.0.35' (DX-Cluster)
const unsigned char DX_MULTICAST_ADDRESS[] = {0x01U, 0x00U, 0x5EU, 0x00U, 0x00U, 0x23U};

unsigned int              CRepeaterHandler::m_maxRepeaters = 0U;
CRepeaterHandler**        CRepeaterHandler::m_repeaters = NULL;

std::string               CRepeaterHandler::m_localAddress;
CG2ProtocolHandler*       CRepeaterHandler::m_g2Handler = NULL;
CIRCDDB*                  CRepeaterHandler::m_irc = NULL;
CCacheManager*            CRepeaterHandler::m_cache = NULL;
std::string               CRepeaterHandler::m_gateway;
TEXT_LANG                 CRepeaterHandler::m_language = TL_ENGLISH_UK;
bool                      CRepeaterHandler::m_dextraEnabled = true;
bool                      CRepeaterHandler::m_dplusEnabled = false;
bool                      CRepeaterHandler::m_dcsEnabled = true;
bool                      CRepeaterHandler::m_infoEnabled = true;
bool                      CRepeaterHandler::m_echoEnabled = true;
bool                      CRepeaterHandler::m_dtmfEnabled = true;

CHeaderLogger*            CRepeaterHandler::m_headerLogger = NULL;

CAPRSWriter*              CRepeaterHandler::m_aprsWriter  = NULL;

CCallsignList*            CRepeaterHandler::m_restrictList = NULL;

CRepeaterHandler::CRepeaterHandler(const std::string& callsign, const std::string& band, const std::string& address, unsigned int port, HW_TYPE hwType, const std::string& reflector, bool atStartup, RECONNECT reconnect, bool dratsEnabled, double frequency, double offset, double range, double latitude, double longitude, double agl, const std::string& description1, const std::string& description2, const std::string& url, IRepeaterProtocolHandler* handler, unsigned char band1, unsigned char band2, unsigned char band3) :
m_index(0x00U),
m_rptCallsign(),
m_gwyCallsign(),
m_band(' '),
m_address(),
m_port(port),
m_hwType(hwType),
m_repeaterHandler(handler),
m_frequency(frequency),
m_offset(offset),
m_range(range),
m_latitude(latitude),
m_longitude(longitude),
m_agl(agl),
m_description1(description1),
m_description2(description2),
m_url(url),
m_band1(band1),
m_band2(band2),
m_band3(band3),
m_repeaterId(0x00U),
m_busyId(0x00U),
m_watchdogTimer(1000U, REPEATER_TIMEOUT),
m_ddMode(false),
m_ddCallsign(),
m_queryTimer(1000U, 5U),		// 5 seconds
m_myCall1(),
m_myCall2(),
m_yourCall(),
m_rptCall1(),
m_rptCall2(),
m_flag1(0x00U),
m_flag2(0x00U),
m_flag3(0x00U),
m_restricted(false),
m_frames(0U),
m_silence(0U),
m_errors(0U),
m_textCollector(),
m_text(),
m_xBandRptr(NULL),
m_starNet(NULL),
m_g2Status(G2_NONE),
m_g2User(),
m_g2Repeater(),
m_g2Gateway(),
m_g2Header(NULL),
m_g2Address(),
m_linkStatus(LS_NONE),
m_linkRepeater(),
m_linkGateway(),
m_linkReconnect(reconnect),
m_linkAtStartup(atStartup),
m_linkStartup(reflector),
m_linkReconnectTimer(1000U),
m_linkRelink(false),
m_echo(NULL),
m_infoAudio(NULL),
m_infoNeeded(false),
m_msgAudio(NULL),
m_msgNeeded(false),
m_wxAudio(NULL),
m_wxNeeded(false),
m_version(NULL),
m_drats(NULL),
m_dtmf(),
m_pollTimer(1000U, 900U),			// 15 minutes
m_ccsHandler(NULL),
m_lastReflector(),
m_heardUser(),
m_heardRepeater(),
m_heardTimer(1000U, 0U, 100U)		// 100ms
{
	assert(callsign.size() > 0);
	assert(port > 0U);
	assert(handler != NULL);

	m_ddMode = band.size() > 1U;

	m_band = band.at(0U);

	m_rptCallsign = callsign;
	m_rptCallsign.resize(LONG_CALLSIGN_LENGTH - 1U, ' ');
	m_rptCallsign.append(band);
	m_rptCallsign.resize(LONG_CALLSIGN_LENGTH, ' ');

	m_gwyCallsign = callsign;
	m_gwyCallsign.resize(LONG_CALLSIGN_LENGTH - 1U, ' ');
	m_gwyCallsign.push_back('G');

	m_address.s_addr = inet_addr(address.c_str());

	m_pollTimer.start();

	switch (m_linkReconnect) {
		case RECONNECT_5MINS:
			m_linkReconnectTimer.start(5U * 60U);
			break;
		case RECONNECT_10MINS:
			m_linkReconnectTimer.start(10U * 60U);
			break;
		case RECONNECT_15MINS:
			m_linkReconnectTimer.start(15U * 60U);
			break;
		case RECONNECT_20MINS:
			m_linkReconnectTimer.start(20U * 60U);
			break;
		case RECONNECT_25MINS:
			m_linkReconnectTimer.start(25U * 60U);
			break;
		case RECONNECT_30MINS:
			m_linkReconnectTimer.start(30U * 60U);
			break;
		case RECONNECT_60MINS:
			m_linkReconnectTimer.start(60U * 60U);
			break;
		case RECONNECT_90MINS:
			m_linkReconnectTimer.start(90U * 60U);
			break;
		case RECONNECT_120MINS:
			m_linkReconnectTimer.start(120U * 60U);
			break;
		case RECONNECT_180MINS:
			m_linkReconnectTimer.start(180U * 60U);
			break;
		default:
			break;
	}

	std::string messageFile(std::getenv("HOME"));
	messageFile.append("/message");
	messageFile.append(".dvtool");

	std::string weatherFile(std::getenv("HOME"));
	weatherFile.append("/weather");
	weatherFile.append(".dvtool");

	m_echo      = new CEchoUnit(this, callsign);
	m_infoAudio = new CAudioUnit(this, callsign);
	m_msgAudio  = new CAnnouncementUnit(this, callsign, messageFile, "MSG");
	m_wxAudio   = new CAnnouncementUnit(this, callsign, weatherFile, "WX");
	m_version   = new CVersionUnit(this, callsign);

	if (dratsEnabled) {
		m_drats = new CDRATSServer(m_localAddress, port, callsign, this);
		bool ret = m_drats->open();
		if (!ret) {
			delete m_drats;
			m_drats = NULL;
		}
	}
}

CRepeaterHandler::~CRepeaterHandler()
{
	delete m_echo;
	delete m_infoAudio;
	delete m_msgAudio;
	delete m_wxAudio;
	delete m_version;

	if (m_drats != NULL)
		m_drats->close();
}

void CRepeaterHandler::initialise(unsigned int maxRepeaters)
{
	assert(maxRepeaters > 0U);

	m_maxRepeaters = maxRepeaters;

	m_repeaters = new CRepeaterHandler*[m_maxRepeaters];
	for (unsigned int i = 0U; i < m_maxRepeaters; i++)
		m_repeaters[i] = NULL;
}

void CRepeaterHandler::setIndex(unsigned int index)
{
	m_index = index;
}

void CRepeaterHandler::add(const std::string& callsign, const std::string& band, const std::string& address, unsigned int port, HW_TYPE hwType, const std::string& reflector, bool atStartup, RECONNECT reconnect, bool dratsEnabled, double frequency, double offset, double range, double latitude, double longitude, double agl, const std::string& description1, const std::string& description2, const std::string& url, IRepeaterProtocolHandler* handler, unsigned char band1, unsigned char band2, unsigned char band3)
{
	assert(callsign.size() > 0);
	assert(port > 0U);
	assert(handler != NULL);

	CRepeaterHandler* repeater = new CRepeaterHandler(callsign, band, address, port, hwType, reflector, atStartup, reconnect, dratsEnabled, frequency, offset, range, latitude, longitude, agl, description1, description2, url, handler, band1, band2, band3);

	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		if (m_repeaters[i] == NULL) {
			repeater->setIndex(i);
			m_repeaters[i] = repeater;
			return;
		}
	}

	CUtils::lprint("Cannot add repeater with callsign %s, no space", callsign.c_str());

	delete repeater;
}

void CRepeaterHandler::setG2Handler(CG2ProtocolHandler* handler)
{
	assert(handler != NULL);

	m_g2Handler = handler;
}

void CRepeaterHandler::setCache(CCacheManager* cache)
{
	assert(cache != NULL);

	m_cache = cache;
}

void CRepeaterHandler::setIRC(CIRCDDB* irc)
{
	assert(irc != NULL);

	m_irc = irc;
}

void CRepeaterHandler::setGateway(const std::string& gateway)
{
	m_gateway = gateway;
}

void CRepeaterHandler::setLanguage(TEXT_LANG language)
{
	m_language = language;
}

void CRepeaterHandler::setDExtraEnabled(bool enabled)
{
	m_dextraEnabled = enabled;
}

void CRepeaterHandler::setDPlusEnabled(bool enabled)
{
	m_dplusEnabled = enabled;
}

void CRepeaterHandler::setDCSEnabled(bool enabled)
{
	m_dcsEnabled = enabled;
}

void CRepeaterHandler::setInfoEnabled(bool enabled)
{
	m_infoEnabled = enabled;
}

void CRepeaterHandler::setEchoEnabled(bool enabled)
{
	m_echoEnabled = enabled;
}

void CRepeaterHandler::setDTMFEnabled(bool enabled)
{
	m_dtmfEnabled = enabled;
}

void CRepeaterHandler::setHeaderLogger(CHeaderLogger* logger)
{
	m_headerLogger = logger;
}

void CRepeaterHandler::setAPRSWriter(CAPRSWriter* writer)
{
	m_aprsWriter = writer;
}

void CRepeaterHandler::setLocalAddress(const std::string& address)
{
	m_localAddress = address;
}

void CRepeaterHandler::setRestrictList(CCallsignList* list)
{
	assert(list != NULL);

	m_restrictList = list;
}

bool CRepeaterHandler::getRepeater(unsigned int n, std::string& callsign, LINK_STATUS& linkStatus, std::string& linkCallsign)
{
	if (n >= m_maxRepeaters)
		return false;

	if (m_repeaters[n] == NULL)
		return false;

	callsign     = m_repeaters[n]->m_rptCallsign;
	linkStatus   = m_repeaters[n]->m_linkStatus;
	linkCallsign = m_repeaters[n]->m_linkRepeater;

	return true;
}

void CRepeaterHandler::resolveUser(const std::string &user, const std::string& repeater, const std::string& gateway, const std::string &address)
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		if (m_repeaters[i] != NULL)
			m_repeaters[i]->resolveUserInt(user, repeater, gateway, address);
	}
}

void CRepeaterHandler::resolveRepeater(const std::string& repeater, const std::string& gateway, const std::string &address, DSTAR_PROTOCOL protocol)
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		if (m_repeaters[i] != NULL)
			m_repeaters[i]->resolveRepeaterInt(repeater, gateway, address, protocol);
	}
}

void CRepeaterHandler::startup()
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		if (m_repeaters[i] != NULL)
			m_repeaters[i]->startupInt();
	}
}

void CRepeaterHandler::clock(unsigned int ms)
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		if (m_repeaters[i] != NULL)
			m_repeaters[i]->clockInt(ms);
	}
}

void CRepeaterHandler::finalise()
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		delete m_repeaters[i];
		m_repeaters[i] = NULL;
	}

	if (m_aprsWriter != NULL) {
		m_aprsWriter->close();
		delete m_aprsWriter;
	}

	delete[] m_repeaters;
}

CRepeaterHandler* CRepeaterHandler::findDVRepeater(const CHeaderData& header)
{
	std::string rpt1 = header.getRptCall1();
	in_addr address = header.getYourAddress();

	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL) {
			if (!repeater->m_ddMode && repeater->m_address.s_addr == address.s_addr && 0==repeater->m_rptCallsign.compare(rpt1))
				return repeater;
		}
	}

	return NULL;
}

CRepeaterHandler* CRepeaterHandler::findDVRepeater(const CAMBEData& data, bool busy)
{
	unsigned int id = data.getId();

	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL) {
			if (!busy && !repeater->m_ddMode && repeater->m_repeaterId == id)
				return repeater;
			if (busy && !repeater->m_ddMode && repeater->m_busyId == id)
				return repeater;
		}
	}

	return NULL;
}

CRepeaterHandler* CRepeaterHandler::findDVRepeater(const std::string& callsign)
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL) {
			if (!repeater->m_ddMode && 0==repeater->m_rptCallsign.compare(callsign))
				return repeater;
		}
	}

	return NULL;
}

CRepeaterHandler* CRepeaterHandler::findRepeater(const CPollData& data)
{
	in_addr   address = data.getYourAddress();
	unsigned int port = data.getYourPort();

	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL) {
			if (repeater->m_address.s_addr == address.s_addr && repeater->m_port == port)
				return repeater;
		}
	}

	return NULL;
}

CRepeaterHandler* CRepeaterHandler::findDDRepeater(const CDDData& data)
{
	std::string rpt1 = data.getRptCall1();

	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL) {
			if (repeater->m_ddMode && 0==repeater->m_rptCallsign.compare(rpt1))
				return repeater;
		}
	}

	return NULL;
}

CRepeaterHandler* CRepeaterHandler::findDDRepeater()
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL) {
			if (repeater->m_ddMode)
				return repeater;
		}
	}

	return NULL;
}

std::list<std::string> CRepeaterHandler::listDVRepeaters()
{
	std::list<std::string> repeaters;

	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL && !repeater->m_ddMode)
			repeaters.push_back(repeater->m_rptCallsign);
	}

	return repeaters;
}

void CRepeaterHandler::pollAllIcom(CPollData& data)
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		CRepeaterHandler* repeater = m_repeaters[i];
		if (repeater != NULL && repeater->m_hwType == HW_ICOM)
			repeater->processRepeater(data);
	}
}

CRemoteRepeaterData* CRepeaterHandler::getInfo() const
{
	return new CRemoteRepeaterData(m_rptCallsign, m_linkReconnect, m_linkStartup);
}

void CRepeaterHandler::processRepeater(CHeaderData& header)
{
	unsigned int id = header.getId();

	// Stop duplicate headers
	if (id == m_repeaterId)
		return;

	// Save the header fields
	m_myCall1  = header.getMyCall1();
	m_myCall2  = header.getMyCall2();
	m_yourCall = header.getYourCall();
	m_rptCall1 = header.getRptCall1();
	m_rptCall2 = header.getRptCall2();
	m_flag1    = header.getFlag1();
	m_flag2    = header.getFlag2();
	m_flag3    = header.getFlag3();

	if (m_hwType == HW_ICOM) {
		unsigned char band1 = header.getBand1();
		unsigned char band2 = header.getBand2();
		unsigned char band3 = header.getBand3();

		if (m_band1 != band1 || m_band2 != band2 || m_band3 != band3) {
			m_band1 = band1;
			m_band2 = band2;
			m_band3 = band3;
			CUtils::lprint("Repeater %s registered with bands %u %u %u", m_rptCall1.c_str(), m_band1, m_band2, m_band3);
		}
	}

	if (m_flag1 == 0x01) {
		CUtils::lprint("Received a busy message from repeater %s", m_rptCall1.c_str());
		return;
	}

	if (m_heardUser.size() && m_myCall1.compare(m_heardUser) && m_irc != NULL)
		m_irc->sendHeard(m_heardUser, "    ", "        ", m_heardRepeater, "        ", 0x00U, 0x00U, 0x00U);

	// Inform CCS
	m_ccsHandler->writeHeard(header);
	m_ccsHandler->writeHeader(header);

	// The Icom heard timer
	m_heardTimer.stop();

	if (m_drats != NULL)
		m_drats->writeHeader(header);

	// Reset the statistics
	m_frames  = 0U;
	m_silence = 0U;
	m_errors  = 0U;

	// An RF header resets the reconnect timer
	m_linkReconnectTimer.start();

	// Incoming links get everything
	sendToIncoming(header);

	// Reset the slow data text collector
	m_textCollector.reset();
	m_text.clear();

	// Reset the APRS Writer if it's enabled
	if (m_aprsWriter != NULL)
		m_aprsWriter->reset(m_rptCallsign);

	// Write to Header.log if it's enabled
	if (m_headerLogger != NULL)
		m_headerLogger->write("Repeater", header);

	// Reset the DTMF decoder
	m_dtmf.reset();

	// Reset the info, echo and version commands if they're running
	m_infoAudio->cancel();
	m_msgAudio->cancel();
	m_wxAudio->cancel();
	m_echo->cancel();
	m_version->cancel();

	// A new header resets fields and G2 routing status
	m_repeaterId = id;
	m_busyId     = 0x00U;
	m_watchdogTimer.start();

	m_xBandRptr = NULL;
	m_starNet   = NULL;

	// If we're querying for a user or repeater, kill the query timer
	if (m_g2Status == G2_USER || m_g2Status == G2_REPEATER)
		m_queryTimer.stop();

	delete m_g2Header;
	m_g2Header = NULL;
	m_g2Status = G2_NONE;
	m_g2User.clear();
	m_g2Repeater.clear();
	m_g2Gateway.clear();

	// Check if this user is restricted
	m_restricted = false;
	if (m_restrictList != NULL) {
		bool res = m_restrictList->isInList(m_myCall1);
		if (res)
			m_restricted = true;
	}

	// Reject silly RPT2 values
	if (0==m_rptCall2.compare(m_rptCallsign) || 0==m_rptCall2.compare("        "))
		return;

	// Do cross-band routing if RPT2 is not one of the gateway callsigns
	if (m_rptCall2.compare(m_gwyCallsign) && m_rptCall2.compare(m_gateway)) {
		CRepeaterHandler* repeater = findDVRepeater(m_rptCall2);
		if (repeater != NULL) {
			CUtils::lprint("Cross-band routing by %s from %s to %s", m_myCall1.c_str(), m_rptCallsign.c_str(), m_rptCall2.c_str());
			m_xBandRptr = repeater;
			m_xBandRptr->process(header, DIR_INCOMING, AS_XBAND);
			m_g2Status = G2_XBAND;
		} else {
			// Keep the transmission local
			CUtils::lprint("Invalid cross-band route by %s from %s to %s", m_myCall1.c_str(), m_rptCallsign.c_str(), m_rptCall2.c_str());
			m_g2Status = G2_LOCAL;
		}
		return;
	}

	m_starNet = CStarNetHandler::findStarNet(header);
	if (m_starNet != NULL && !m_restricted) {
		CUtils::lprint("StarNet routing by %s to %s", m_myCall1.c_str(), m_yourCall.c_str());
		m_starNet->process(header);
		m_g2Status = G2_STARNET;
		return;
	}

	// Reject simple cases
	if (0 == m_yourCall.compare(0, 4, "CQCQ")) {
		sendToOutgoing(header);
		return;
	}

	// Handle the Echo command
	if (m_echoEnabled && 0==m_yourCall.compare("       E")) {
		m_g2Status = G2_ECHO;
		m_echo->writeHeader(header);
		return;
	}

	// Handle the Info command
	if (m_infoEnabled && 0==m_yourCall.compare("       I")) {
		m_g2Status = G2_LOCAL;
		m_infoNeeded = true;
		return;
	}

	// Handle the MSG command
	if (m_infoEnabled && 0==m_yourCall.compare("       M")) {
		m_g2Status = G2_LOCAL;
		m_msgNeeded = true;
		return;
	}

	// Handle the WX command
	if (m_infoEnabled && 0==m_yourCall.compare("       W")) {
		m_g2Status = G2_LOCAL;
		m_wxNeeded = true;
		return;
	}

	// Handle the Version command
	if (m_infoEnabled && 0==m_yourCall.compare("       V")) {
		m_g2Status = G2_VERSION;
		sendToOutgoing(header);
		return;
	}

	if (m_restricted) {
		sendToOutgoing(header);
		return;
	}

	if (isCCSCommand(m_yourCall)) {
		ccsCommandHandler(m_yourCall, m_myCall1, "UR Call");
		sendToOutgoing(header);
	} else {
		g2CommandHandler(m_yourCall, m_myCall1, header);

		if (m_g2Status == G2_NONE) {
			reflectorCommandHandler(m_yourCall, m_myCall1, "UR Call");
			sendToOutgoing(header);
		}
	}
}

void CRepeaterHandler::processRepeater(CAMBEData& data)
{
	// AMBE data via RF resets the reconnect timer
	m_linkReconnectTimer.start();
	m_watchdogTimer.start();

	m_frames++;
	m_errors += data.getErrors();

	unsigned char buffer[DV_FRAME_MAX_LENGTH_BYTES];
	data.getData(buffer, DV_FRAME_MAX_LENGTH_BYTES);

	if (memcpy(buffer, NULL_AMBE_DATA_BYTES, VOICE_FRAME_LENGTH_BYTES) == 0)
		m_silence++;

	// Don't do DTMF decoding or blanking if off and not on crossband either
	if (m_dtmfEnabled && m_g2Status != G2_XBAND) {
		bool pressed = m_dtmf.decode(buffer, data.isEnd());
		if (pressed) {
			// Replace the DTMF with silence
			memcpy(buffer, NULL_AMBE_DATA_BYTES, VOICE_FRAME_LENGTH_BYTES);
			data.setData(buffer, DV_FRAME_LENGTH_BYTES);
		}

		bool dtmfDone = m_dtmf.hasCommand();
		if (dtmfDone) {
			std::string command = m_dtmf.translate();

			// Only process the DTMF command if the your call is CQCQCQ and not a restricted user
			if (!m_restricted && 0==m_yourCall.compare(0, 4U, "CQCQ")) {
				if (0 == command.size()) {
					// Do nothing
				} else if (isCCSCommand(command)) {
					ccsCommandHandler(command, m_myCall1, "DTMF");
				} else if (0==command.compare("       I")) {
					m_infoNeeded = true;
				} else {
					reflectorCommandHandler(command, m_myCall1, "DTMF");
				}
			}
		}
	}

	// Incoming links get everything
	sendToIncoming(data);

	// CCS gets everything
	m_ccsHandler->writeAMBE(data);

	if (m_drats != NULL)
		m_drats->writeData(data);

	if (m_aprsWriter != NULL)
		m_aprsWriter->writeData(m_rptCallsign, data);

	if (0==m_text.size() && !data.isEnd()) {
		m_textCollector.writeData(data);

		bool hasText = m_textCollector.hasData();
		if (hasText) {
			m_text = m_textCollector.getData();
			sendHeard(m_text);
		}
	}

	data.setText(m_text);

	// If no slow data text has been received, send a heard with no text when the end of the
	// transmission arrives
	if (data.isEnd() && 0==m_text.size())
		sendHeard();

	// Send the statistics after the end of the data, any stats from the repeater should have
	// been received by now
	if (data.isEnd()) {
		m_watchdogTimer.stop();
		sendStats();
	}

	switch (m_g2Status) {
		case G2_LOCAL:
			if (data.isEnd()) {
				m_repeaterId = 0x00U;
				m_g2Status   = G2_NONE;
			}
			break;

		case G2_OK:
			data.setDestination(m_g2Address, G2_DV_PORT);
			m_g2Handler->writeAMBE(data);

			if (data.isEnd()) {
				m_repeaterId = 0x00U;
				m_g2Status   = G2_NONE;
			}
			break;

		case G2_USER:
		case G2_REPEATER:
			// Data ended before the callsign could be resolved
			if (data.isEnd()) {
				m_queryTimer.stop();
				delete m_g2Header;
				m_repeaterId = 0x0U;
				m_g2Status   = G2_NONE;
				m_g2Header   = NULL;
			}
			break;

		case G2_NONE:
			if (data.isEnd())
				m_repeaterId = 0x00U;

			sendToOutgoing(data);
			break;

		case G2_XBAND:
			m_xBandRptr->process(data, DIR_INCOMING, AS_XBAND);

			if (data.isEnd()) {
				m_repeaterId = 0x00U;
				m_g2Status   = G2_NONE;
				m_xBandRptr  = NULL;
			}
			break;

		case G2_STARNET:
			m_starNet->process(data);

			if (data.isEnd()) {
				m_repeaterId = 0x00U;
				m_g2Status   = G2_NONE;
				m_starNet    = NULL;
			}
			break;

		case G2_ECHO:
			m_echo->writeData(data);

			if (data.isEnd()) {
				m_repeaterId = 0x00U;
				m_g2Status   = G2_NONE;
			}
			break;

		case G2_VERSION:
			sendToOutgoing(data);

			if (data.isEnd()) {
				m_version->sendVersion();

				m_repeaterId = 0x00U;
				m_g2Status   = G2_NONE;
			}
			break;
	}

	if (data.isEnd() && m_infoNeeded) {
		m_infoAudio->sendStatus();
		m_infoNeeded = false;
	}

	if (data.isEnd() && m_msgNeeded) {
		m_msgAudio->sendAnnouncement();
		m_msgNeeded = false;
	}

	if (data.isEnd() && m_wxNeeded) {
		m_wxAudio->sendAnnouncement();
		m_wxNeeded = false;
	}
}

// Incoming headers when relaying network traffic, as detected by the repeater, will be used as a command
// to the reflector command handler, probably to do an unlink.
void CRepeaterHandler::processBusy(CHeaderData& header)
{
	unsigned int id = header.getId();

	// Ignore duplicate headers
	if (id == m_busyId)
		return;

	std::string rptCall1 = header.getRptCall1();
	std::string rptCall2 = header.getRptCall2();

	if (m_hwType == HW_ICOM) {
		unsigned char band1 = header.getBand1();
		unsigned char band2 = header.getBand2();
		unsigned char band3 = header.getBand3();

		if (m_band1 != band1 || m_band2 != band2 || m_band3 != band3) {
			m_band1 = band1;
			m_band2 = band2;
			m_band3 = band3;
			CUtils::lprint("Repeater %s registered with bands %u %u %u", rptCall1.c_str(), m_band1, m_band2, m_band3);
		}
	}

	if (header.getFlag1() == 0x01) {
		CUtils::lprint("Received a busy message from repeater %s", rptCall1.c_str());
		return;
	}

	// Reject the header if the RPT2 value is not one of the gateway callsigns
	if (rptCall2.compare(m_gwyCallsign) && rptCall2.compare(m_gateway))
		return;

	m_myCall1  = header.getMyCall1();
	m_yourCall = header.getYourCall();
	m_rptCall1 = rptCall1;
	m_rptCall2 = rptCall2;

	m_dtmf.reset();

	m_busyId     = id;
	m_repeaterId = 0x00U;
	m_watchdogTimer.start();

	// If restricted then don't send to the command handler
	m_restricted = false;
	if (m_restrictList != NULL) {
		bool res = m_restrictList->isInList(m_myCall1);
		if (res) {
			m_restricted = true;
			return;
		}
	}

	// Reject simple cases
	if (0==m_yourCall.compare(0, 4, "CQCQ") || 0==m_yourCall.compare("       E") || 0==m_yourCall.compare("       I"))
		return;

	if (isCCSCommand(m_yourCall))
		ccsCommandHandler(m_yourCall, m_myCall1, "background UR Call");
	else
		reflectorCommandHandler(m_yourCall, m_myCall1, "background UR Call");
}

void CRepeaterHandler::processBusy(CAMBEData& data)
{
	m_watchdogTimer.start();

	unsigned char buffer[DV_FRAME_MAX_LENGTH_BYTES];
	data.getData(buffer, DV_FRAME_MAX_LENGTH_BYTES);

	// Don't do DTMF decoding if off
	if (m_dtmfEnabled) {
		m_dtmf.decode(buffer, data.isEnd());

		bool dtmfDone = m_dtmf.hasCommand();
		if (dtmfDone) {
			std::string command = m_dtmf.translate();

			// Only process the DTMF command if the your call is CQCQCQ and the user isn't restricted
			if (!m_restricted && 0==m_yourCall.compare(0, 4, "CQCQ")) {
				if (0 == command.size()) {
					// Do nothing
				} else if (isCCSCommand(command)) {
					ccsCommandHandler(command, m_myCall1, "background DTMF");
				} else if (0 == command.compare("       I")) {
					// Do nothing
				} else {
					reflectorCommandHandler(command, m_myCall1, "background DTMF");
				}
			}
		}
	}

	if (data.isEnd()) {
		if (m_infoNeeded) {
			m_infoAudio->sendStatus();
			m_infoNeeded = false;
		}

		if (m_msgNeeded) {
			m_msgAudio->sendAnnouncement();
			m_msgNeeded = false;
		}

		if (m_wxNeeded) {
			m_wxAudio->sendAnnouncement();
			m_wxNeeded = false;
		}

		if (m_g2Status == G2_VERSION)
			m_version->sendVersion();

		m_g2Status = G2_NONE;
		m_busyId = 0x00U;
		m_watchdogTimer.stop();
	}
}

void CRepeaterHandler::processRepeater(CHeardData& heard)
{
	if (m_irc == NULL)
		return;

	// A second heard has come in before the original has been sent or cancelled
	if (m_heardTimer.isRunning() && !m_heardTimer.hasExpired())
		m_irc->sendHeard(m_heardUser, "    ", "        ", m_heardRepeater, "        ", 0x00U, 0x00U, 0x00U);

	m_heardUser     = heard.getUser();
	m_heardRepeater = heard.getRepeater();

	m_heardTimer.start();
}

void CRepeaterHandler::processRepeater(CPollData& data)
{
	if (!m_pollTimer.hasExpired())
		return;

	if (m_irc == NULL)
		return;

	std::string callsign = m_rptCallsign;
	if (m_ddMode)
		callsign.push_back('D');

	std::string text = data.getData1();

	m_irc->kickWatchdog(callsign, text);

	m_pollTimer.start();
}

void CRepeaterHandler::processRepeater(CDDData& data)
{
	if (!m_ddMode)
		return;

	if (0 == m_ddCallsign.size()) {
		m_ddCallsign = data.getYourCall();
		CUtils::lprint("Added DD callsign %s", m_ddCallsign.c_str());
	}

	CDDHandler::process(data);
}

bool CRepeaterHandler::process(CDDData& data)
{
	unsigned char* address = data.getDestinationAddress();
	if (memcpy(address, ETHERNET_BROADCAST_ADDRESS, ETHERNET_ADDRESS_LENGTH) == 0)
		data.setRepeaters(m_gwyCallsign, "        ");
	else if (memcpy(address, TOALL_MULTICAST_ADDRESS, ETHERNET_ADDRESS_LENGTH) == 0)
		data.setRepeaters(m_gwyCallsign, m_rptCallsign);
	else if (memcpy(address, DX_MULTICAST_ADDRESS, ETHERNET_ADDRESS_LENGTH) == 0)
		data.setRepeaters(m_gwyCallsign, m_rptCallsign);
	else
		data.setRepeaters(m_gwyCallsign, m_rptCallsign);

	data.setDestination(m_address, m_port);
	data.setFlags(0xC0U, 0x00U, 0x00U);
	data.setMyCall1(m_ddCallsign);
	data.setMyCall2("    ");

	m_repeaterHandler->writeDD(data);

	return true;
}

bool CRepeaterHandler::process(CHeaderData& header, DIRECTION, AUDIO_SOURCE source)
{
	// If data is coming from the repeater then don't send
	if (m_repeaterId != 0x00U)
		return false;

	// Rewrite the ID if we're using Icom hardware
	if (m_hwType == HW_ICOM) {
		unsigned int id1 = header.getId();
		unsigned int id2 = id1 + m_index;
		header.setId(id2);
	}

	// Send all original headers to all repeater types, and only send duplicate headers to homebrew repeaters
	if (source != AS_DUP || (source == AS_DUP && m_hwType == HW_HOMEBREW)) {
		header.setBand1(m_band1);
		header.setBand2(m_band2);
		header.setBand3(m_band3);
		header.setDestination(m_address, m_port);
		header.setRepeaters(m_gwyCallsign, m_rptCallsign);

		m_repeaterHandler->writeHeader(header);
	}

	// Don't send duplicate headers to anyone else
	if (source == AS_DUP)
		return true;

	sendToIncoming(header);

	if (source == AS_DPLUS || source == AS_DEXTRA || source == AS_DCS)
		m_ccsHandler->writeHeader(header);

	if (source == AS_G2 || source == AS_INFO || source == AS_VERSION || source == AS_XBAND || source == AS_ECHO)
		return true;

	// Reset the slow data text collector, used for DCS text passing
	m_textCollector.reset();
	m_text.clear();

	sendToOutgoing(header);

	return true;
}

bool CRepeaterHandler::process(CAMBEData& data, DIRECTION, AUDIO_SOURCE source)
{
	// If data is coming from the repeater then don't send
	if (m_repeaterId != 0x00U)
		return false;

	// Rewrite the ID if we're using Icom hardware
	if (m_hwType == HW_ICOM) {
		unsigned int id = data.getId();
		id += m_index;
		data.setId(id);
	}

	data.setBand1(m_band1);
	data.setBand2(m_band2);
	data.setBand3(m_band3);
	data.setDestination(m_address, m_port);

	m_repeaterHandler->writeAMBE(data);

	sendToIncoming(data);

	if (source == AS_DPLUS || source == AS_DEXTRA || source == AS_DCS)
		m_ccsHandler->writeAMBE(data);

	if (source == AS_G2 || source == AS_INFO || source == AS_VERSION || source == AS_XBAND || source == AS_ECHO)
		return true;

	// Collect the text from the slow data for DCS
	if (0==m_text.size() && !data.isEnd()) {
		m_textCollector.writeData(data);

		bool hasText = m_textCollector.hasData();
		if (hasText)
			m_text = m_textCollector.getData();
	}

	data.setText(m_text);

	sendToOutgoing(data);

	return true;
}

void CRepeaterHandler::resolveUserInt(const std::string& user, const std::string& repeater, const std::string& gateway, const std::string &address)
{
	if (m_g2Status == G2_USER && 0==m_g2User.compare(user)) {
		m_queryTimer.stop();

		if (address.size()) {
			// No point routing to self
			if (0 == repeater.compare(m_rptCallsign)) {
				m_g2Status = G2_LOCAL;
				delete m_g2Header;
				m_g2Header = NULL;
				return;
			}

			// User found, update the settings and send the header to the correct place
			m_g2Address.s_addr = ::inet_addr(address.c_str());

			m_g2Repeater = repeater;
			m_g2Gateway  = gateway;

			m_g2Header->setDestination(m_g2Address, G2_DV_PORT);
			m_g2Header->setRepeaters(m_g2Gateway, m_g2Repeater);
			m_g2Handler->writeHeader(*m_g2Header);

			delete m_g2Header;
			m_g2Status = G2_OK;
			m_g2Header = NULL;
		} else {
			// User not found, remove G2 settings
			m_g2Status = G2_LOCAL;
			m_g2User.clear();
			m_g2Repeater.clear();
			m_g2Gateway.clear();

			delete m_g2Header;
			m_g2Header = NULL;
		}
	}
}

void CRepeaterHandler::resolveRepeaterInt(const std::string& repeater, const std::string& gateway, const std::string &address, DSTAR_PROTOCOL protocol)
{
	if (m_g2Status == G2_REPEATER && 0==m_g2Repeater.compare(repeater)) {
		m_queryTimer.stop();

		if (address.size()) {
			// Repeater found, update the settings and send the header to the correct place
			m_g2Address.s_addr = ::inet_addr(address.c_str());

			m_g2Repeater = repeater;
			m_g2Gateway  = gateway;

			m_g2Header->setDestination(m_g2Address, G2_DV_PORT);
			m_g2Header->setRepeaters(m_g2Gateway, m_g2Repeater);
			m_g2Handler->writeHeader(*m_g2Header);

			delete m_g2Header;
			m_g2Status = G2_OK;
			m_g2Header = NULL;
		} else {
			// Repeater not found, remove G2 settings
			m_g2Status = G2_LOCAL;
			m_g2User.clear();
			m_g2Repeater.clear();
			m_g2Gateway.clear();

			delete m_g2Header;
			m_g2Header = NULL;
		}
	}

	if (m_linkStatus == LS_PENDING_IRCDDB && 0==m_linkRepeater.compare(repeater)) {
		m_queryTimer.stop();

		if (address.size()) {
			// Repeater found
			in_addr addr;
			switch (protocol) {
				case DP_DPLUS:
					if (m_dplusEnabled) {
						m_linkGateway = gateway;
						addr.s_addr = ::inet_addr(address.c_str());
						CDPlusHandler::link(this, m_rptCallsign, m_linkRepeater, addr);
						m_linkStatus = LS_LINKING_DPLUS;
					} else {
						CUtils::lprint("Require D-Plus for linking to %s, but D-Plus is disabled", repeater.c_str());
						m_linkStatus = LS_NONE;
						m_linkRepeater.clear();
						m_linkGateway.clear();
						writeNotLinked();
						triggerInfo();
					}
					break;

				case DP_DCS:
					if (m_dcsEnabled) {
						m_linkGateway = gateway;
						addr.s_addr = ::inet_addr(address.c_str());
						CDCSHandler::link(this, m_rptCallsign, m_linkRepeater, addr);
						m_linkStatus = LS_LINKING_DCS;
					} else {
						CUtils::lprint("Require DCS for linking to %s, but DCS is disabled", repeater.c_str());
						m_linkStatus = LS_NONE;
						m_linkRepeater.clear();
						m_linkGateway.clear();
						writeNotLinked();
						triggerInfo();
					}
					break;

				case DP_LOOPBACK:
					m_linkGateway = gateway;
					addr.s_addr = ::inet_addr(address.c_str());
					CDCSHandler::link(this, m_rptCallsign, m_linkRepeater, addr);
					m_linkStatus = LS_LINKING_LOOPBACK;
					break;

				default:
					if (m_dextraEnabled) {
						m_linkGateway = gateway;
						addr.s_addr = ::inet_addr(address.c_str());
						CDExtraHandler::link(this, m_rptCallsign, m_linkRepeater, addr);
						m_linkStatus = LS_LINKING_DEXTRA;
					} else {
						CUtils::lprint("Require DExtra for linking to %s, but DExtra is disabled", repeater.c_str());
						m_linkStatus = LS_NONE;
						m_linkRepeater.clear();
						m_linkGateway.clear();
						writeNotLinked();
						triggerInfo();
					}
					break;

			}
		} else {
			// Repeater not found
			m_linkStatus = LS_NONE;
			m_linkRepeater.clear();
			m_linkGateway.clear();

			writeNotLinked();
			triggerInfo();
		}
	}
}

void CRepeaterHandler::clockInt(unsigned int ms)
{
	m_infoAudio->clock(ms);
	m_msgAudio->clock(ms);
	m_wxAudio->clock(ms);
	m_echo->clock(ms);
	m_version->clock(ms);

	m_linkReconnectTimer.clock(ms);
	m_watchdogTimer.clock(ms);
	m_queryTimer.clock(ms);
	m_heardTimer.clock(ms);
	m_pollTimer.clock(ms);

	// If the reconnect timer has expired
	if (m_linkReconnectTimer.isRunning() && m_linkReconnectTimer.hasExpired()) {
		if (m_linkStatus != LS_NONE && (0==m_linkStartup.size() || 0==m_linkStartup.compare("        "))) {
			// Unlink if linked to something
			CUtils::lprint("Reconnect timer has expired, unlinking %s from %s", m_rptCallsign.c_str(), m_linkRepeater.c_str());

			CDExtraHandler::unlink(this);
			CDPlusHandler::unlink(this);
			CDCSHandler::unlink(this);

			m_linkStatus = LS_NONE;
			m_linkRepeater.clear();

			// Tell the users
			writeNotLinked();
			triggerInfo();
		} else if ((m_linkStatus == LS_NONE && m_linkStartup.size() && m_linkStartup.compare("        ")) ||
				   (m_linkStatus != LS_NONE && m_linkRepeater.compare(m_linkStartup))) {
			// Relink if not linked or linked to the wrong reflector
			CUtils::lprint("Reconnect timer has expired, relinking %s to %s", m_rptCallsign.c_str(), m_linkStartup.c_str());

			// Check for just a change of letter
			if (m_linkStatus != LS_NONE) {
				std::string oldCall = m_linkRepeater.substr(0, LONG_CALLSIGN_LENGTH - 1U);
				std::string newCall = m_linkStartup.substr(0, LONG_CALLSIGN_LENGTH - 1U);

				// Just a change of port?
				if (0==oldCall.compare(newCall)) {
					switch (m_linkStatus) {
						case LS_LINKING_DEXTRA:
						case LS_LINKED_DEXTRA:
							m_linkRelink = true;
							m_linkRepeater = m_linkStartup;
							CDExtraHandler::unlink(this, m_linkRepeater);

							m_linkStatus = LS_LINKING_DEXTRA;
							writeLinkingTo(m_linkRepeater);
							triggerInfo();
							break;

						case LS_LINKING_DCS:
						case LS_LINKED_DCS:
							m_linkRelink = true;
							m_linkRepeater = m_linkStartup;
							CDCSHandler::unlink(this, m_linkRepeater);

							m_linkStatus = LS_LINKING_DCS;
							writeLinkingTo(m_linkRepeater);
							triggerInfo();
							break;

						case LS_LINKING_LOOPBACK:
						case LS_LINKED_LOOPBACK:
							m_linkRelink = true;
							m_linkRepeater = m_linkStartup;
							CDCSHandler::unlink(this, m_linkRepeater);

							m_linkStatus = LS_LINKING_LOOPBACK;
							writeLinkingTo(m_linkRepeater);
							triggerInfo();
							break;

						case LS_LINKING_DPLUS:
							m_linkRepeater = m_linkStartup;
							CDPlusHandler::relink(this, m_linkRepeater);
							writeLinkingTo(m_linkRepeater);
							triggerInfo();
							break;

						case LS_LINKED_DPLUS:
							m_linkRepeater = m_linkStartup;
							CDPlusHandler::relink(this, m_linkRepeater);
							writeLinkedTo(m_linkRepeater);
							triggerInfo();
							break;

						default:
							break;
					}

					return;
				}
			}

			CDExtraHandler::unlink(this);
			CDPlusHandler::unlink(this);
			CDCSHandler::unlink(this);

			linkInt(m_linkStartup);
		}

		m_linkReconnectTimer.start();
	}

	// If the ircDDB query timer has expired
	if (m_queryTimer.isRunning() && m_queryTimer.hasExpired()) {
		m_queryTimer.stop();

		if (m_g2Status == G2_USER || m_g2Status == G2_REPEATER) {
			// User or repeater not found in time, remove G2 settings
			CUtils::lprint("ircDDB did not reply within five seconds");

			m_g2Status = G2_LOCAL;
			m_g2User.clear();
			m_g2Repeater.clear();
			m_g2Gateway.clear();

			delete m_g2Header;
			m_g2Header = NULL;
		} else if (m_linkStatus == LS_PENDING_IRCDDB) {
			// Repeater not found in time
			CUtils::lprint("ircDDB did not reply within five seconds");

			m_linkStatus = LS_NONE;
			m_linkRepeater.clear();
			m_linkGateway.clear();

			writeNotLinked();
			triggerInfo();
		} else if (m_linkStatus == LS_LINKING_CCS) {
			// CCS didn't reply in time
			CUtils::lprint("CCS did not reply within five seconds");

			m_ccsHandler->stopLink();

			m_linkStatus = LS_NONE;
			m_linkRepeater.clear();

			restoreLinks();
		}
	}

	// Icom heard timer has expired
	if (m_heardTimer.isRunning() && m_heardTimer.hasExpired() && m_irc != NULL) {
		m_irc->sendHeard(m_heardUser, "    ", "        ", m_heardRepeater, "        ", 0x00U, 0x00U, 0x00U);
		m_heardTimer.stop();
	}

	// If the watchdog timer has expired, clean up
	if (m_watchdogTimer.isRunning() && m_watchdogTimer.hasExpired()) {
		CUtils::lprint("Radio watchdog timer for %s has expired", m_rptCallsign.c_str());
		m_watchdogTimer.stop();

		if (m_repeaterId != 0x00U) {
			if (0 == m_text.size())
				sendHeard();

			if (m_drats != NULL)
				m_drats->writeEnd();

			sendStats();

			switch (m_g2Status) {
				case G2_USER:
				case G2_REPEATER:
					m_queryTimer.stop();
					delete m_g2Header;
					m_g2Header = NULL;
					break;

				case G2_XBAND:
					m_xBandRptr = NULL;
					break;

				case G2_STARNET:
					m_starNet = NULL;
					break;

				case G2_ECHO:
					m_echo->end();
					break;

				case G2_VERSION:
					m_version->sendVersion();
					break;

				default:
					break;
			}

			if (m_infoNeeded) {
				m_infoAudio->sendStatus();
				m_infoNeeded = false;
			}

			if (m_msgNeeded) {
				m_msgAudio->sendAnnouncement();
				m_msgNeeded = false;
			}

			if (m_wxNeeded) {
				m_wxAudio->sendAnnouncement();
				m_wxNeeded = false;
			}

			m_repeaterId = 0x00U;
			m_g2Status   = G2_NONE;
		}

		if (m_busyId != 0x00U) {
			if (m_infoNeeded) {
				m_infoAudio->sendStatus();
				m_infoNeeded = false;
			}

			if (m_msgNeeded) {
				m_msgAudio->sendAnnouncement();
				m_msgNeeded = false;
			}

			if (m_wxNeeded) {
				m_wxAudio->sendAnnouncement();
				m_wxNeeded = false;
			}

			if (m_g2Status == G2_VERSION)
				m_version->sendVersion();

			m_g2Status = G2_NONE;
			m_busyId = 0x00U;
		}
	}
}

void CRepeaterHandler::linkUp(DSTAR_PROTOCOL protocol, const std::string& callsign)
{
	if (protocol == DP_DEXTRA && m_linkStatus == LS_LINKING_DEXTRA) {
		CUtils::lprint("DExtra link to %s established", callsign.c_str());
		m_linkStatus = LS_LINKED_DEXTRA;
		writeLinkedTo(callsign);
		triggerInfo();
	}

	if (protocol == DP_DPLUS && m_linkStatus == LS_LINKING_DPLUS) {
		CUtils::lprint("D-Plus link to %s established", callsign.c_str());
		m_linkStatus = LS_LINKED_DPLUS;
		writeLinkedTo(callsign);
		triggerInfo();
	}

	if (protocol == DP_DCS && m_linkStatus == LS_LINKING_DCS) {
		CUtils::lprint("DCS link to %s established", callsign.c_str());
		m_linkStatus = LS_LINKED_DCS;
		writeLinkedTo(callsign);
		triggerInfo();
	}

	if (protocol == DP_DCS && m_linkStatus == LS_LINKING_LOOPBACK) {
		CUtils::lprint("Loopback link to %s established", callsign.c_str());
		m_linkStatus = LS_LINKED_LOOPBACK;
		writeLinkedTo(callsign);
		triggerInfo();
	}
}

bool CRepeaterHandler::linkFailed(DSTAR_PROTOCOL protocol, const std::string& callsign, bool isRecoverable)
{
	// Is relink to another module required?
	if (!isRecoverable && m_linkRelink) {
		m_linkRelink = false;
		CUtils::lprint("Relinking %s from %s to %s", m_rptCallsign.c_str(), callsign.c_str(), m_linkRepeater.c_str());
		linkInt(m_linkRepeater);
		return false;
	}

	// Have we linked to something else in the meantime?
	if (m_linkStatus == LS_NONE || m_linkRepeater.compare(callsign)) {
		switch (protocol) {
			case DP_DCS:
				CUtils::lprint("DCS link to %s has failed", callsign.c_str());
				break;
			case DP_DEXTRA:
				CUtils::lprint("DExtra link to %s has failed", callsign.c_str());
				break;
			case DP_DPLUS:
				CUtils::lprint("D-Plus link to %s has failed", callsign.c_str());
				break;
			default:
				break;
		}

		return false;
	}

	if (!isRecoverable) {
		if (protocol == DP_DEXTRA && 0==callsign.compare(m_linkRepeater)) {
			CUtils::lprint("DExtra link to %s has failed", m_linkRepeater.c_str());
			m_linkRepeater.clear();
			m_linkStatus = LS_NONE;
			writeNotLinked();
			triggerInfo();
		}

		if (protocol == DP_DPLUS && 0==callsign.compare(m_linkRepeater)) {
			CUtils::lprint("D-Plus link to %s has failed", m_linkRepeater.c_str());
			m_linkRepeater.clear();
			m_linkStatus = LS_NONE;
			writeNotLinked();
			triggerInfo();
		}

		if (protocol == DP_DCS && 0==callsign.compare(m_linkRepeater)) {
			if (m_linkStatus == LS_LINKED_DCS || m_linkStatus == LS_LINKING_DCS)
				CUtils::lprint("DCS link to %s has failed", m_linkRepeater.c_str());
			else
				CUtils::lprint("Loopback link to %s has failed", m_linkRepeater.c_str());
			m_linkRepeater.clear();
			m_linkStatus = LS_NONE;
			writeNotLinked();
			triggerInfo();
		}

		return false;
	}

	if (protocol == DP_DEXTRA) {
		switch (m_linkStatus) {
			case LS_LINKED_DEXTRA:
				CUtils::lprint("DExtra link to %s has failed, relinking", m_linkRepeater.c_str());
				m_linkStatus = LS_LINKING_DEXTRA;
				writeLinkingTo(m_linkRepeater);
				triggerInfo();
				return true;

			case LS_LINKING_DEXTRA:
				return true;

			default:
				return false;
		}
	}

	if (protocol == DP_DPLUS) {
		switch (m_linkStatus) {
			case LS_LINKED_DPLUS:
				CUtils::lprint("D-Plus link to %s has failed, relinking", m_linkRepeater.c_str());
				m_linkStatus = LS_LINKING_DPLUS;
				writeLinkingTo(m_linkRepeater);
				triggerInfo();
				return true;

			case LS_LINKING_DPLUS:
				return true;

			default:
				return false;
		}
	}

	if (protocol == DP_DCS) {
		switch (m_linkStatus) {
			case LS_LINKED_DCS:
				CUtils::lprint("DCS link to %s has failed, relinking", m_linkRepeater.c_str());
				m_linkStatus = LS_LINKING_DCS;
				writeLinkingTo(m_linkRepeater);
				triggerInfo();
				return true;

			case LS_LINKED_LOOPBACK:
				CUtils::lprint("Loopback link to %s has failed, relinking", m_linkRepeater.c_str());
				m_linkStatus = LS_LINKING_LOOPBACK;
				writeLinkingTo(m_linkRepeater);
				triggerInfo();
				return true;

			case LS_LINKING_DCS:
			case LS_LINKING_LOOPBACK:
				return true;

			default:
				return false;
		}
	}

	return false;
}

void CRepeaterHandler::linkRefused(DSTAR_PROTOCOL protocol, const std::string& callsign)
{
	if (protocol == DP_DEXTRA && 0==callsign.compare(m_linkRepeater)) {
		CUtils::lprint("DExtra link to %s was refused", m_linkRepeater.c_str());
		m_linkRepeater.clear();
		m_linkStatus = LS_NONE;
		writeIsBusy(callsign);
		triggerInfo();
	}

	if (protocol == DP_DPLUS && 0==callsign.compare(m_linkRepeater)) {
		CUtils::lprint("D-Plus link to %s was refused", m_linkRepeater.c_str());
		m_linkRepeater.clear();
		m_linkStatus = LS_NONE;
		writeIsBusy(callsign);
		triggerInfo();
	}

	if (protocol == DP_DCS && 0==callsign.compare(m_linkRepeater)) {
		if (m_linkStatus == LS_LINKED_DCS || m_linkStatus == LS_LINKING_DCS)
			CUtils::lprint("DCS link to %s was refused", m_linkRepeater.c_str());
		else
			CUtils::lprint("Loopback link to %s was refused", m_linkRepeater.c_str());
		m_linkRepeater.clear();
		m_linkStatus = LS_NONE;
		writeIsBusy(callsign);
		triggerInfo();
	}
}

void CRepeaterHandler::link(RECONNECT reconnect, const std::string& reflector)
{
	// CCS removal
	if (m_linkStatus == LS_LINKING_CCS || m_linkStatus == LS_LINKED_CCS) {
		CUtils::lprint("Dropping CCS link to %s", m_linkRepeater.c_str());

		m_ccsHandler->stopLink();

		m_linkStatus = LS_NONE;
		m_linkRepeater.clear();
		m_queryTimer.stop();
	}

	m_linkStartup   = reflector;
	m_linkReconnect = reconnect;

	m_linkReconnectTimer.stop();

	switch (m_linkReconnect) {
		case RECONNECT_5MINS:
			m_linkReconnectTimer.start(5U * 60U);
			break;
		case RECONNECT_10MINS:
			m_linkReconnectTimer.start(10U * 60U);
			break;
		case RECONNECT_15MINS:
			m_linkReconnectTimer.start(15U * 60U);
			break;
		case RECONNECT_20MINS:
			m_linkReconnectTimer.start(20U * 60U);
			break;
		case RECONNECT_25MINS:
			m_linkReconnectTimer.start(25U * 60U);
			break;
		case RECONNECT_30MINS:
			m_linkReconnectTimer.start(30U * 60U);
			break;
		case RECONNECT_60MINS:
			m_linkReconnectTimer.start(60U * 60U);
			break;
		case RECONNECT_90MINS:
			m_linkReconnectTimer.start(90U * 60U);
			break;
		case RECONNECT_120MINS:
			m_linkReconnectTimer.start(120U * 60U);
			break;
		case RECONNECT_180MINS:
			m_linkReconnectTimer.start(180U * 60U);
			break;
		default:
			break;
	}

	// Nothing to do
	if ((m_linkStatus != LS_NONE && 0==m_linkRepeater.compare(reflector)) ||
		(m_linkStatus == LS_NONE && (0==reflector.size() || 0==reflector.compare("        "))))
		return;

	// Handle unlinking
	if (m_linkStatus != LS_NONE && (0==reflector.size() || 0==reflector.compare("        "))) {
		CUtils::lprint("Unlinking %s from %s", m_rptCallsign.c_str(), m_linkRepeater.c_str());

		CDExtraHandler::unlink(this);
		CDPlusHandler::unlink(this);
		CDCSHandler::unlink(this);

		m_linkStatus = LS_NONE;
		m_linkRepeater.clear();

		writeNotLinked();
		triggerInfo();

		return;
	}

	CUtils::lprint("Linking %s to %s", m_rptCallsign.c_str(), reflector.c_str());

	// Check for just a change of letter
	if (m_linkStatus != LS_NONE) {
		std::string oldCall = m_linkRepeater.substr(0, LONG_CALLSIGN_LENGTH - 1U);
		std::string newCall = reflector.substr(0, LONG_CALLSIGN_LENGTH - 1U);

		// Just a change of port?
		if (0 == oldCall.compare(newCall)) {
			switch (m_linkStatus) {
				case LS_LINKING_DEXTRA:
				case LS_LINKED_DEXTRA:
					m_linkRelink = true;
					m_linkRepeater = reflector;
					CDExtraHandler::unlink(this, m_linkRepeater);

					m_linkStatus = LS_LINKING_DEXTRA;
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
					break;

				case LS_LINKING_DCS:
				case LS_LINKED_DCS:
					m_linkRelink = true;
					m_linkRepeater = reflector;
					CDCSHandler::unlink(this, m_linkRepeater);

					m_linkStatus = LS_LINKING_DCS;
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
					break;

				case LS_LINKING_LOOPBACK:
				case LS_LINKED_LOOPBACK:
					m_linkRelink = true;
					m_linkRepeater = reflector;
					CDCSHandler::unlink(this, m_linkRepeater);

					m_linkStatus = LS_LINKING_LOOPBACK;
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
					break;

				case LS_LINKING_DPLUS:
					m_linkRepeater = reflector;
					CDPlusHandler::relink(this, m_linkRepeater);
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
					break;

				case LS_LINKED_DPLUS:
					m_linkRepeater = reflector;
					CDPlusHandler::relink(this, m_linkRepeater);
					writeLinkedTo(m_linkRepeater);
					triggerInfo();
					break;

				default:
					break;
			}

			return;
		}
	}

	CDExtraHandler::unlink(this);
	CDPlusHandler::unlink(this);
	CDCSHandler::unlink(this);

	linkInt(m_linkStartup);
}

void CRepeaterHandler::unlink(PROTOCOL protocol, const std::string& reflector)
{
	if (protocol == PROTO_CCS) {
		m_ccsHandler->unlink(reflector);
		return;
	}

	if (m_linkReconnect == RECONNECT_FIXED && 0==m_linkRepeater.compare(reflector)) {
		CUtils::lprint("Cannot unlink %s because it is fixed", reflector.c_str());
		return;
	}

	switch (protocol) {
		case PROTO_DPLUS:
			CDPlusHandler::unlink(this, reflector, false);
			break;

		case PROTO_DEXTRA:
			CDExtraHandler::unlink(this, reflector, false);
			break;

		case PROTO_DCS:
			CDCSHandler::unlink(this, reflector, false);
			break;

		default:
			break;
	}
}

void CRepeaterHandler::g2CommandHandler(const std::string& callsign, const std::string& user, CHeaderData& header)
{
	if (m_linkStatus == LS_LINKING_CCS || m_linkStatus == LS_LINKED_CCS)
		return;

	if (callsign.at(0) == '/') {
		if (m_irc == NULL) {
			CUtils::lprint("%s is trying to G2 route with ircDDB disabled", user.c_str());
			m_g2Status = G2_LOCAL;
			return;
		}

		// This a repeater route
		// Convert "/1234567" to "123456 7"
		std::string repeater = callsign.substr(1, LONG_CALLSIGN_LENGTH - 2U);
		repeater.push_back(' ');
		repeater.push_back(callsign.back());

		if (0==repeater.compare(m_rptCallsign)) {
			CUtils::lprint("%s is trying to G2 route to self, ignoring", user.c_str());
			m_g2Status = G2_LOCAL;
			return;
		}

		if (0==repeater.compare(0, 3, "REF") || 0==repeater.compare(0, 3, "XRF") || 0==repeater.compare(0, 3, "DCS")) {
			CUtils::lprint("%s is trying to G2 route to reflector %s, ignoring", user.c_str(), repeater.c_str());
			m_g2Status = G2_LOCAL;
			return;
		}

		CUtils::lprint("%s is trying to G2 route to repeater %s", user.c_str(), repeater.c_str());

		m_g2Repeater = repeater;
		m_g2User = "CQCQCQ  ";

		CRepeaterData* data = m_cache->findRepeater(m_g2Repeater);

		if (data == NULL) {
			m_g2Status = G2_REPEATER;
			m_irc->findRepeater(m_g2Repeater);
			m_g2Header = new CHeaderData(header);
			m_queryTimer.start();
		} else {
			m_g2Status = G2_OK;
			m_g2Address = data->getAddress();
			m_g2Gateway = data->getGateway();
			header.setDestination(m_g2Address, G2_DV_PORT);
			header.setRepeaters(m_g2Gateway, m_g2Repeater);
			m_g2Handler->writeHeader(header);
			delete data;
		}
	} else if ('L'!=callsign.back() && 'U'!=callsign.back()) {
		if (m_irc == NULL) {
			CUtils::lprint("%s is trying to G2 route with ircDDB disabled", user.c_str());
			m_g2Status = G2_LOCAL;
			return;
		}

		// This a callsign route
		if (0==callsign.compare(0, 3,"REF") || 0==callsign.compare(0, 3, "XRF") || 0==callsign.compare(0, 3, "DCS")) {
			CUtils::lprint("%s is trying to G2 route to reflector %s, ignoring", user.c_str(), callsign.c_str());
			m_g2Status = G2_LOCAL;
			return;
		}

		CUtils::lprint("%s is trying to G2 route to callsign %s", user.c_str(), callsign.c_str());

		CUserData* data = m_cache->findUser(callsign);

		if (data == NULL) {
			m_g2User   = callsign;
			m_g2Status = G2_USER;
			m_irc->findUser(m_g2User);
			m_g2Header = new CHeaderData(header);
			m_queryTimer.start();
		} else {
			// No point G2 routing to yourself
			if (0==data->getRepeater().compare(m_rptCallsign)) {
				m_g2Status = G2_LOCAL;
				delete data;
				return;
			}

			m_g2Status   = G2_OK;
			m_g2User     = callsign;
			m_g2Address  = data->getAddress();
			m_g2Repeater = data->getRepeater();
			m_g2Gateway  = data->getGateway();
			header.setDestination(m_g2Address, G2_DV_PORT);
			header.setRepeaters(m_g2Gateway, m_g2Repeater);
			m_g2Handler->writeHeader(header);

			delete data;
		}
	}
}

void CRepeaterHandler::ccsCommandHandler(const std::string& callsign, const std::string& user, const std::string& type)
{
	if (0 == callsign.compare("CA      ")) {
		m_ccsHandler->stopLink(user, type);
	} else {
		CCS_STATUS status = m_ccsHandler->getStatus();
		if (status == CS_CONNECTED) {
			suspendLinks();
			m_queryTimer.start();
			m_linkStatus   = LS_LINKING_CCS;
			m_linkRepeater = callsign.substr(1U);
			m_ccsHandler->startLink(m_linkRepeater, user, type);
		}
	}
}

void CRepeaterHandler::reflectorCommandHandler(const std::string& callsign, const std::string& user, const std::string& type)
{
	if (m_linkStatus == LS_LINKING_CCS || m_linkStatus == LS_LINKED_CCS)
		return;

	if (m_linkReconnect == RECONNECT_FIXED)
		return;

	m_queryTimer.stop();

	char letter = callsign.back();

	if ('U' == letter) {
		// Ignore duplicate unlink requests
		if (m_linkStatus == LS_NONE)
			return;

		CUtils::lprint("Unlink command issued via %s by %s", type.c_str(), user.c_str());

		CDExtraHandler::unlink(this);
		CDPlusHandler::unlink(this);
		CDCSHandler::unlink(this);

		m_linkStatus = LS_NONE;
		m_linkRepeater.clear();

		writeNotLinked();
		triggerInfo();
	} else if ('L' == letter) {
		std::string reflector;

		// Handle the special case of "       L"
		if (0 == callsign.compare("       L")) {
			if (0 == m_linkStartup.size())
				return;

			reflector = m_linkStartup;
		} else {
			// Extract the callsign "1234567L" -> "123456 7"
			reflector = callsign.substr(0, LONG_CALLSIGN_LENGTH - 2U);
			reflector.push_back(' ');
			reflector.push_back(callsign.at(LONG_CALLSIGN_LENGTH - 2U));
		}

		// Ensure duplicate link requests aren't acted on
		if (m_linkStatus != LS_NONE && 0==reflector.compare(m_linkRepeater))
			return;

		// We can't link to ourself
		if (0 == reflector.compare(m_rptCallsign)) {
			CUtils::lprint("%s is trying to link with self via %s, ignoring", user.c_str(), type.c_str());
			triggerInfo();
			return;
		}

		CUtils::lprint("Link command from %s to %s issued via %s by %s", m_rptCallsign.c_str(), reflector.c_str(), type.c_str(), user.c_str());

		// Check for just a change of letter
		if (m_linkStatus != LS_NONE) {
			std::string oldCall = m_linkRepeater.substr(0, LONG_CALLSIGN_LENGTH - 1U);
			std::string newCall = reflector.substr(0, LONG_CALLSIGN_LENGTH - 1U);

			// Just a change of port?
			if (0 == oldCall.compare(newCall)) {
				switch (m_linkStatus) {
					case LS_LINKING_DEXTRA:
					case LS_LINKED_DEXTRA:
						m_linkRelink = true;
						m_linkRepeater = reflector;
						CDExtraHandler::unlink(this, m_linkRepeater);

						m_linkStatus = LS_LINKING_DEXTRA;
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
						break;

					case LS_LINKING_DCS:
					case LS_LINKED_DCS:
						m_linkRelink = true;
						m_linkRepeater = reflector;
						CDCSHandler::unlink(this, m_linkRepeater);

						m_linkStatus = LS_LINKING_DCS;
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
						break;

					case LS_LINKING_LOOPBACK:
					case LS_LINKED_LOOPBACK:
						m_linkRelink = true;
						m_linkRepeater = reflector;
						CDCSHandler::unlink(this, m_linkRepeater);

						m_linkStatus = LS_LINKING_LOOPBACK;
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
						break;

					case LS_LINKING_DPLUS:
						m_linkRepeater = reflector;
						CDPlusHandler::relink(this, m_linkRepeater);
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
						break;

					case LS_LINKED_DPLUS:
						m_linkRepeater = reflector;
						CDPlusHandler::relink(this, m_linkRepeater);
						writeLinkedTo(m_linkRepeater);
						triggerInfo();
						break;

					default:
						break;
				}

				return;
			}
		}

		CDExtraHandler::unlink(this);
		CDPlusHandler::unlink(this);
		CDCSHandler::unlink(this);

		linkInt(reflector);
	}
}

void CRepeaterHandler::linkInt(const std::string& callsign)
{
	// Find the repeater to link to
	CRepeaterData* data = m_cache->findRepeater(callsign);

	// Are we trying to link to an unknown DExtra, D-Plus, or DCS reflector?
	if (data == NULL && (0==callsign.compare(0, 3, "REF") || 0==callsign.compare(0, 3, "XRF") || 0==callsign.compare(0, 3U, "DCS"))) {
		CUtils::lprint("%s is unknown, ignoring link request", callsign.c_str());
		triggerInfo();
		return;
	}

	m_linkRepeater = callsign;

	if (data != NULL) {
		m_linkGateway = data->getGateway();

		switch (data->getProtocol()) {
			case DP_DPLUS:
				if (m_dplusEnabled) {
					m_linkStatus = LS_LINKING_DPLUS;
					CDPlusHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
				} else {
					CUtils::lprint("Require D-Plus for linking to %s, but D-Plus is disabled", callsign.c_str());
					m_linkStatus = LS_NONE;
					writeNotLinked();
					triggerInfo();
				}
				break;

			case DP_DCS:
				if (m_dcsEnabled) {
					m_linkStatus = LS_LINKING_DCS;
					CDCSHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
				} else {
					CUtils::lprint("Require DCS for linking to %s, but DCS is disabled", callsign.c_str());
					m_linkStatus = LS_NONE;
					writeNotLinked();
					triggerInfo();
				}
				break;

			case DP_LOOPBACK:
				m_linkStatus = LS_LINKING_LOOPBACK;
				CDCSHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
				writeLinkingTo(m_linkRepeater);
				triggerInfo();
				break;

			default:
				if (m_dextraEnabled) {
					m_linkStatus = LS_LINKING_DEXTRA;
					CDExtraHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
				} else {
					CUtils::lprint("Require DExtra for linking to %s, but DExtra is disabled", callsign.c_str());
					m_linkStatus = LS_NONE;
					writeNotLinked();
					triggerInfo();
				}
				break;
		}

		delete data;
	} else {
		if (m_irc != NULL) {
			m_linkStatus = LS_PENDING_IRCDDB;
			m_irc->findRepeater(callsign);
			m_queryTimer.start();
			writeLinkingTo(callsign);
			triggerInfo();
		} else {
			m_linkStatus = LS_NONE;
			writeNotLinked();
			triggerInfo();
		}
	}
}

void CRepeaterHandler::sendToOutgoing(const CHeaderData& header)
{
	CHeaderData temp(header);

	temp.setCQCQCQ();
	temp.setFlags(0x00U, 0x00U, 0x00U);

	// Outgoing DPlus links change the RPT1 and RPT2 values in the DPlus handler
	CDPlusHandler::writeHeader(this, temp, DIR_OUTGOING);

	// Outgoing DExtra links have the currently linked repeater/gateway
	// as the RPT1 and RPT2 values
	temp.setRepeaters(m_linkGateway, m_linkRepeater);
	CDExtraHandler::writeHeader(this, temp, DIR_OUTGOING);

	// Outgoing DCS links have the currently linked repeater and repeater callsign
	// as the RPT1 and RPT2 values
	temp.setRepeaters(m_rptCallsign, m_linkRepeater);
	CDCSHandler::writeHeader(this, temp, DIR_OUTGOING);
}

void CRepeaterHandler::sendToOutgoing(const CAMBEData& data)
{
	CAMBEData temp(data);

	CDExtraHandler::writeAMBE(this, temp, DIR_OUTGOING);

	CDPlusHandler::writeAMBE(this, temp, DIR_OUTGOING);

	CDCSHandler::writeAMBE(this, temp, DIR_OUTGOING);
}

void CRepeaterHandler::sendToIncoming(const CHeaderData& header)
{
	CHeaderData temp(header);

	temp.setCQCQCQ();
	temp.setFlags(0x00U, 0x00U, 0x00U);

	// Incoming DPlus links
	temp.setRepeaters(m_rptCallsign, m_gateway);
	CDPlusHandler::writeHeader(this, temp, DIR_INCOMING);

	// Incoming DExtra links have RPT1 and RPT2 swapped
	temp.setRepeaters(m_gwyCallsign, m_rptCallsign);
	CDExtraHandler::writeHeader(this, temp, DIR_INCOMING);

	// Incoming DCS links have RPT1 and RPT2 swapped
	temp.setRepeaters(m_gwyCallsign, m_rptCallsign);
	CDCSHandler::writeHeader(this, temp, DIR_INCOMING);
}

void CRepeaterHandler::sendToIncoming(const CAMBEData& data)
{
	CAMBEData temp(data);

	CDExtraHandler::writeAMBE(this, temp, DIR_INCOMING);

	CDPlusHandler::writeAMBE(this, temp, DIR_INCOMING);

	CDCSHandler::writeAMBE(this, temp, DIR_INCOMING);
}

void CRepeaterHandler::startupInt()
{
	// Report our existence to ircDDB
	if (m_irc != NULL) {
		std::string callsign = m_rptCallsign;
		if (m_ddMode)
			callsign.push_back('D');

		if (m_frequency > 0.0)
			m_irc->rptrQRG(callsign, m_frequency, m_offset, m_range * 1000.0, m_agl);

		if (m_latitude != 0.0 && m_longitude != 0.0)
			m_irc->rptrQTH(callsign, m_latitude, m_longitude, m_description1, m_description2, m_url);
	}


	m_ccsHandler = new CCCSHandler(this, m_rptCallsign, m_index + 1U, m_latitude, m_longitude, m_frequency, m_offset, m_description1, m_description2, m_url, CCS_PORT + m_index);

	// Start up our CCS link if we are DV mode
	if (!m_ddMode)
		m_ccsHandler->connect();

	// Link to a startup reflector/repeater
	if (m_linkAtStartup && m_linkStartup.size()) {
		CUtils::lprint("Linking %s at startup to %s", m_rptCallsign.c_str(), m_linkStartup.c_str());

		// Find the repeater to link to
		CRepeaterData* data = m_cache->findRepeater(m_linkStartup);

		m_linkRepeater = m_linkStartup;

		if (data != NULL) {
			m_linkGateway = data->getGateway();

			DSTAR_PROTOCOL protocol = data->getProtocol();
			switch (protocol) {
				case DP_DPLUS:
					if (m_dplusEnabled) {
						m_linkStatus = LS_LINKING_DPLUS;
						CDPlusHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
					} else {
						CUtils::lprint("Require D-Plus for linking to %s, but D-Plus is disabled", m_linkRepeater.c_str());
						m_linkStatus = LS_NONE;
						writeNotLinked();
						triggerInfo();
					}
					break;

				case DP_DCS:
					if (m_dcsEnabled) {
						m_linkStatus = LS_LINKING_DCS;
						CDCSHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
					} else {
						CUtils::lprint("Require DCS for linking to %s, but DCS is disabled", m_linkRepeater.c_str());
						m_linkStatus = LS_NONE;
						writeNotLinked();
						triggerInfo();
					}
					break;

				case DP_LOOPBACK:
					m_linkStatus = LS_LINKING_LOOPBACK;
					CDCSHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
					writeLinkingTo(m_linkRepeater);
					triggerInfo();
					break;

				default:
					if (m_dextraEnabled) {
						m_linkStatus = LS_LINKING_DEXTRA;
						CDExtraHandler::link(this, m_rptCallsign, m_linkRepeater, data->getAddress());
						writeLinkingTo(m_linkRepeater);
						triggerInfo();
					} else {
						CUtils::lprint("Require DExtra for linking to %s, but DExtra is disabled", m_linkRepeater.c_str());
						m_linkStatus = LS_NONE;
						writeNotLinked();
						triggerInfo();
					}
					break;
			}

			delete data;
		} else {
			if (m_irc != NULL) {
				m_linkStatus = LS_PENDING_IRCDDB;
				m_irc->findRepeater(m_linkStartup);
				m_queryTimer.start();
				writeLinkingTo(m_linkStartup);
				triggerInfo();
			} else {
				m_linkStatus = LS_NONE;
				writeNotLinked();
				triggerInfo();
			}
		}
	} else {
		writeNotLinked();
		triggerInfo();
	}
}

void CRepeaterHandler::writeLinkingTo(const std::string &callsign)
{
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Verbinde mit ";
			break;
		case TL_DANSK:
			text = "Linker til ";
			break;
		case TL_FRANCAIS:
			text = "Connexion a ";
			break;
		case TL_ITALIANO:
			text = "In conn con ";
			break;
		case TL_POLSKI:
			text = "Linkuje do ";
			break;
		case TL_ESPANOL:
			text = "Enlazando ";
			break;
		case TL_SVENSKA:
			text = "Lankar till ";
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Linken naar ";
			break;
		case TL_NORSK:
			text = "Kobler til ";
			break;
		case TL_PORTUGUES:
			text = "Conectando, ";
			break;
		default:
			text = "Linking to ";
			break;
	}

	text.append(callsign);

	CTextData textData(m_linkStatus, callsign, text, m_address, m_port);
	m_repeaterHandler->writeText(textData);

	m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
	triggerInfo();

	m_ccsHandler->setReflector();
}

void CRepeaterHandler::writeLinkedTo(const std::string &callsign)
{
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Verlinkt zu ";
			break;
		case TL_DANSK:
			text = "Linket til ";
			break;
		case TL_FRANCAIS:
			text = "Connecte a ";
			break;
		case TL_ITALIANO:
			text = "Connesso a ";
			break;
		case TL_POLSKI:
			text = "Polaczony z ";
			break;
		case TL_ESPANOL:
			text = "Enlazado ";
			break;
		case TL_SVENSKA:
			text = "Lankad till ";
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Gelinkt met ";
			break;
		case TL_NORSK:
			text = "Tilkoblet ";
			break;
		case TL_PORTUGUES:
			text = "Conectado a ";
			break;
		default:
			text = "Linked to ";
			break;
	}
	text.append(callsign);
	CTextData textData(m_linkStatus, callsign, text, m_address, m_port);
	m_repeaterHandler->writeText(textData);

	m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
	triggerInfo();

	m_ccsHandler->setReflector(callsign);
}

void CRepeaterHandler::writeNotLinked()
{
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Nicht verbunden";
			break;
		case TL_DANSK:
			text = "Ikke forbundet";
			break;
		case TL_FRANCAIS:
			text = "Non connecte";
			break;
		case TL_ITALIANO:
			text = "Non connesso";
			break;
		case TL_POLSKI:
			text = "Nie polaczony";
			break;
		case TL_ESPANOL:
			text = "No enlazado";
			break;
		case TL_SVENSKA:
			text = "Ej lankad";
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Niet gelinkt";
			break;
		case TL_NORSK:
			text = "Ikke linket";
			break;
		case TL_PORTUGUES:
			text = "Desconectado";
			break;
		default:
			text = "Not linked";
			break;
	}

	CTextData textData(LS_NONE, std::string(""), text, m_address, m_port);
	m_repeaterHandler->writeText(textData);

	m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
	triggerInfo();

	m_ccsHandler->setReflector();
}

void CRepeaterHandler::writeIsBusy(const std::string& callsign)
{
	std::string tempText(callsign);
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Nicht verbunden";
			tempText.append(" ist belegt");
			break;
		case TL_DANSK:
			text = "Ikke forbundet";
			tempText = std::string("Optaget fra ") + callsign;
			break;
		case TL_FRANCAIS:
			text = "Non connecte";
			tempText = std::string("Occupe par ") + callsign;
			break;
		case TL_ITALIANO:
			text = "Non connesso";
			tempText = std::string("Occupado da") + callsign;
			break;
		case TL_POLSKI:
			text = "Nie polaczony";
			tempText.append(" jest zajety");
			break;
		case TL_ESPANOL:
			text = "No enlazado";
			tempText.append(" ocupado");
			break;
		case TL_SVENSKA:
			text = "Ej lankad";
			tempText.append(" ar upptagen");
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Niet gelinkt";
			tempText.append(" is bezet");
			break;
		case TL_NORSK:
			text = "Ikke linket";
			tempText.append(" er opptatt");
			break;
		case TL_PORTUGUES:
			text = "Desconectado";
			tempText.append(", ocupado");
			break;
		default:
			text = "Not linked";
			tempText.append(" is busy");
			break;
	}

	CTextData textData1(m_linkStatus, m_linkRepeater, tempText, m_address, m_port, true);
	m_repeaterHandler->writeText(textData1);

	CTextData textData2(m_linkStatus, m_linkRepeater, text, m_address, m_port);
	m_repeaterHandler->writeText(textData2);

	m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
	m_infoAudio->setTempStatus(m_linkStatus, m_linkRepeater, tempText);
	triggerInfo();

	m_ccsHandler->setReflector();
}

void CRepeaterHandler::ccsLinkMade(const std::string& callsign, DIRECTION direction)
{
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Verlinkt zu ";
			break;
		case TL_DANSK:
			text = "Linket til ";
			break;
		case TL_FRANCAIS:
			text = "Connecte a ";
			break;
		case TL_ITALIANO:
			text = "Connesso a ";
			break;
		case TL_POLSKI:
			text = "Polaczony z ";
			break;
		case TL_ESPANOL:
			text = "Enlazado ";
			break;
		case TL_SVENSKA:
			text = "Lankad till ";
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Gelinkt met ";
			break;
		case TL_NORSK:
			text = "Tilkoblet ";
			break;
		case TL_PORTUGUES:
			text = "Conectado a ";
			break;
		default:
			text = "Linked to ";
			break;
	}
	
	text.append(callsign);
	
	if (direction == DIR_OUTGOING) {
		suspendLinks();

		m_linkStatus   = LS_LINKED_CCS;
		m_linkRepeater = callsign;
		m_queryTimer.stop();

		CTextData textData(m_linkStatus, callsign, text, m_address, m_port);
		m_repeaterHandler->writeText(textData);

		m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
		triggerInfo();
	} else {
		CTextData textData(m_linkStatus, m_linkRepeater, text, m_address, m_port, true);
		m_repeaterHandler->writeText(textData);

		m_infoAudio->setTempStatus(LS_LINKED_CCS, callsign, text);
		triggerInfo();
	}
}

void CRepeaterHandler::ccsLinkEnded(const std::string&, DIRECTION direction)
{
	std::string tempText;
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Nicht verbunden";
			tempText = "CCS ist beendet";
			break;
		case TL_DANSK:
			text = "Ikke forbundet";
			tempText = "CCS er afsluttet";
			break;
		case TL_FRANCAIS:
			text = "Non connecte";
			tempText = "CCS a pris fin";
			break;
		case TL_ITALIANO:
			text = "Non connesso";
			tempText = "CCS e finita";
			break;
		case TL_POLSKI:
			text = "Nie polaczony";
			tempText = "CCS zakonczyl";
			break;
		case TL_ESPANOL:
			text = "No enlazado";
			tempText = "CCS ha terminado";
			break;
		case TL_SVENSKA:
			text = "Ej lankad";
			tempText = "CCS har upphort";
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Niet gelinkt";
			tempText = "CCS is afgelopen";
			break;
		case TL_NORSK:
			text = "Ikke linket";
			tempText = "CCS er avsluttet";
			break;
		case TL_PORTUGUES:
			text = "Desconectado";
			tempText = "CCS terminou";
			break;
		default:
			text = "Not linked";
			tempText = "CCS has ended";
			break;
	}

	if (direction == DIR_OUTGOING) {
		m_linkStatus = LS_NONE;
		m_linkRepeater.clear();
		m_queryTimer.stop();

		bool res = restoreLinks();
		if (!res) {
			CTextData textData1(m_linkStatus, m_linkRepeater, tempText, m_address, m_port, true);
			m_repeaterHandler->writeText(textData1);

			CTextData textData2(m_linkStatus, m_linkRepeater, text, m_address, m_port);
			m_repeaterHandler->writeText(textData2);

			m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
			m_infoAudio->setTempStatus(m_linkStatus, m_linkRepeater, tempText);
			triggerInfo();
		}
	} else {
		CTextData textData(m_linkStatus, m_linkRepeater, tempText, m_address, m_port, true);
		m_repeaterHandler->writeText(textData);

		m_infoAudio->setTempStatus(m_linkStatus, m_linkRepeater, tempText);
		triggerInfo();
	}
}

void CRepeaterHandler::ccsLinkFailed(const std::string& dtmf, DIRECTION direction)
{
	std::string tempText(dtmf);
	std::string text;

	switch (m_language) {
		case TL_DEUTSCH:
			text = "Nicht verbunden";
			tempText += " unbekannt";
			break;
		case TL_DANSK:
			text = "Ikke forbundet";
			tempText += " unknown";
			break;
		case TL_FRANCAIS:
			text = "Non connecte";
			tempText += " inconnu";
			break;
		case TL_ITALIANO:
			text = "Non connesso";
			tempText = std::string("Sconosciuto ") + dtmf;
			break;
		case TL_POLSKI:
			text = "Nie polaczony";
			tempText += " nieznany";
			break;
		case TL_ESPANOL:
			text = "No enlazado";
			tempText = std::string("Desconocido ") + dtmf;
			break;
		case TL_SVENSKA:
			text = "Ej lankad";
			tempText += " okand";
			break;
		case TL_NEDERLANDS_NL:
		case TL_NEDERLANDS_BE:
			text = "Niet gelinkt";
			tempText += " bekend";
			break;
		case TL_NORSK:
			text = "Ikke linket";
			tempText += " ukjent";
			break;
		case TL_PORTUGUES:
			text = "Desconectado";
			tempText += " desconhecido";
			break;
		default:
			text = "Not linked";
			tempText += " unknown";
			break;
	}

	if (direction == DIR_OUTGOING) {
		m_linkStatus = LS_NONE;
		m_linkRepeater.clear();
		m_queryTimer.stop();

		bool res = restoreLinks();
		if (!res) {
			CTextData textData1(m_linkStatus, m_linkRepeater, tempText, m_address, m_port, true);
			m_repeaterHandler->writeText(textData1);

			CTextData textData2(m_linkStatus, m_linkRepeater, text, m_address, m_port);
			m_repeaterHandler->writeText(textData2);

			m_infoAudio->setStatus(m_linkStatus, m_linkRepeater, text);
			m_infoAudio->setTempStatus(m_linkStatus, m_linkRepeater, tempText);
			triggerInfo();
		}
	} else {
		CTextData textData(m_linkStatus, m_linkRepeater, tempText, m_address, m_port, true);
		m_repeaterHandler->writeText(textData);

		m_infoAudio->setTempStatus(m_linkStatus, m_linkRepeater, tempText);
		triggerInfo();
	}
}

void CRepeaterHandler::writeStatus(CStatusData& statusData)
{
	for (unsigned int i = 0U; i < m_maxRepeaters; i++) {
		if (m_repeaters[i] != NULL) {
			statusData.setDestination(m_repeaters[i]->m_address, m_repeaters[i]->m_port);
			m_repeaters[i]->m_repeaterHandler->writeStatus(statusData);
		}
	}
}

void CRepeaterHandler::sendHeard(const std::string& text)
{
	if (m_irc == NULL)
		return;

	std::string destination;

	if (m_g2Status == G2_OK) {
		destination = m_g2Repeater;
	} else if (m_g2Status == G2_NONE && (m_linkStatus == LS_LINKED_DPLUS || m_linkStatus == LS_LINKED_DEXTRA || m_linkStatus == LS_LINKED_DCS)) {
		if (0==m_linkRepeater.compare(0, 3, "REF") || 0==m_linkRepeater.compare(0, 3, "XRF") || 0==m_linkRepeater.compare(0, 3, "DCS"))
			destination = m_linkRepeater;
	}

	m_irc->sendHeardWithTXMsg(m_myCall1, m_myCall2, m_yourCall, m_rptCall1, m_rptCall2, m_flag1, m_flag2, m_flag3, destination, text);
}

void CRepeaterHandler::sendStats()
{
	if (m_irc != NULL)
		m_irc->sendHeardWithTXStats(m_myCall1, m_myCall2, m_yourCall, m_rptCall1, m_rptCall2, m_flag1, m_flag2, m_flag3, m_frames, m_silence, m_errors / 2U);
}

void CRepeaterHandler::suspendLinks()
{
	if (m_linkStatus == LS_LINKING_DCS      || m_linkStatus == LS_LINKED_DCS    ||
        m_linkStatus == LS_LINKING_DEXTRA   || m_linkStatus == LS_LINKED_DEXTRA ||
	    m_linkStatus == LS_LINKING_DPLUS    || m_linkStatus == LS_LINKED_DPLUS  ||
		m_linkStatus == LS_LINKING_LOOPBACK || m_linkStatus == LS_LINKED_LOOPBACK) {
		m_lastReflector = m_linkRepeater;
		CUtils::Trim(m_lastReflector);
	}

	CDPlusHandler::unlink(this);
	CDExtraHandler::unlink(this);
	CDCSHandler::unlink(this);

	m_linkStatus = LS_NONE;
	m_linkRepeater.clear();
	m_linkReconnectTimer.stop();

	m_ccsHandler->setReflector();
}

bool CRepeaterHandler::restoreLinks()
{
	if (m_linkReconnect == RECONNECT_FIXED) {
		if (m_lastReflector.size()) {
			linkInt(m_linkStartup);
			m_lastReflector.clear();
			return true;
		}
	} else if (m_linkReconnect == RECONNECT_NEVER) {
		if (m_lastReflector.size()) {
			linkInt(m_lastReflector);
			m_lastReflector.clear();
			return true;
		}
	} else {
		m_linkReconnectTimer.start();
		if (m_lastReflector.size()) {
			linkInt(m_lastReflector);
			m_lastReflector.clear();
			return true;
		}
	}

	m_lastReflector.clear();
	return false;
}

void CRepeaterHandler::triggerInfo()
{
	if (!m_infoEnabled)
		return;

	// Either send the audio now, or queue it until the end of the transmission
	if (m_repeaterId != 0x00U || m_busyId != 0x00U) {
		m_infoNeeded = true;
	} else {
		m_infoAudio->sendStatus();
		m_infoNeeded = false;
	}
}

bool CRepeaterHandler::isCCSCommand(const std::string& command) const
{
	if (0==command.compare("CA      "))
		return true;

	char c = command.at(0U);
	if (c != 'C')
		return false;

	c = command.at(1U);
	if (c < '0' || c > '9')
		return false;

	c = command.at(2U);
	if (c < '0' || c > '9')
		return false;

	c = command.at(3U);
	if (c < '0' || c > '9')
		return false;

	return true;
}