/*
 *   Copyright (C) 2015,2016,2020 by Jonathan Naylor G4KLX
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

#include "Conf.h"
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

const int BUFFER_SIZE = 500;

enum SECTION {
  SECTION_NONE,
  SECTION_GENERAL,
  SECTION_ID_LOOKUP,
  SECTION_LOG,
  SECTION_NETWORK,
  SECTION_OPENBRIDGE,
  SECTION_VOCODER
};

CConf::CConf(const std::string& file) :
m_file(file),
m_daemon(false),
m_lookupName(),
m_lookupTime(0U),
m_logDisplayLevel(0U),
m_logFileLevel(0U),
m_logFilePath(),
m_logFileRoot(),
m_logFileRotate(true),
m_networkPort(0U),
m_networkDebug(false),
m_obpEnabled(false),
m_obpAddress(),
m_obpPeerPort(0U),
m_obpPort(0U),
m_obpPassphrase(),
m_obpId(0U),
m_obpCallsign(),
m_obpDebug(false),
m_vocoderEnabled(false),
m_vocoderBuiltin(true),
m_vocoderAddress(),
m_vocoderAmbePort(0U),
m_vocoderImbePort(0U)
{
}

CConf::~CConf()
{
}

bool CConf::read()
{
  FILE* fp = ::fopen(m_file.c_str(), "rt");
  if (fp == NULL) {
    ::fprintf(stderr, "Couldn't open the .ini file - %s\n", m_file.c_str());
    return false;
  }

  SECTION section = SECTION_NONE;

  char buffer[BUFFER_SIZE];
  while (::fgets(buffer, BUFFER_SIZE, fp) != NULL) {
	  if (buffer[0U] == '#')
		  continue;

	  if (buffer[0U] == '[') {
		  if (::strncmp(buffer, "[General]", 9U) == 0)
			  section = SECTION_GENERAL;
		  else if (::strncmp(buffer, "[Id Lookup]", 11U) == 0)
			  section = SECTION_ID_LOOKUP;
		  else if (::strncmp(buffer, "[Log]", 5U) == 0)
			  section = SECTION_LOG;
		  else if (::strncmp(buffer, "[Network]", 9U) == 0)
		  section = SECTION_NETWORK;
		  else if (::strncmp(buffer, "[OpenBridge]", 12U) == 0)
		  section = SECTION_OPENBRIDGE;
		  else if (::strncmp(buffer, "[Vocoder]", 9U) == 0)
		  section = SECTION_VOCODER;
		  else
			  section = SECTION_NONE;

		  continue;
	  }

	  char* key = ::strtok(buffer, " \t=\r\n");
	  if (key == NULL)
		  continue;

	  char* value = ::strtok(NULL, "\r\n");
	  if (value == NULL)
		  continue;

	  // Remove quotes from the value
	  size_t len = ::strlen(value);
	  if (len > 1U && *value == '"' && value[len - 1U] == '"') {
		  value[len - 1U] = '\0';
		  value++;
	  } else {
		  char *p;

		  // if value is not quoted, remove after # (to make comment)
		  if ((p = strchr(value, '#')) != NULL)
			  *p = '\0';

		  // remove trailing tab/space
		  for (p = value + strlen(value) - 1U; p >= value && (*p == '\t' || *p == ' '); p--)
			  *p = '\0';
	  }

	  if (section == SECTION_GENERAL) {
		  if (::strcmp(key, "Daemon") == 0)
			  m_daemon = ::atoi(value) == 1;
	  } else if (section == SECTION_ID_LOOKUP) {
		  if (::strcmp(key, "Name") == 0)
			  m_lookupName = value;
		  else if (::strcmp(key, "Time") == 0)
			  m_lookupTime = (unsigned int)::atoi(value);
	  } else if (section == SECTION_LOG) {
		  if (::strcmp(key, "FilePath") == 0)
			  m_logFilePath = value;
		  else if (::strcmp(key, "FileRoot") == 0)
			  m_logFileRoot = value;
		  else if (::strcmp(key, "FileLevel") == 0)
			  m_logFileLevel = (unsigned int)::atoi(value);
		  else if (::strcmp(key, "DisplayLevel") == 0)
			  m_logDisplayLevel = (unsigned int)::atoi(value);
		  else if (::strcmp(key, "FileRotate") == 0)
			  m_logFileRotate = ::atoi(value) == 1;
	  } else if (section == SECTION_NETWORK) {
		  if (::strcmp(key, "Port") == 0)
			  m_networkPort = (unsigned short)::atoi(value);
		  else if (::strcmp(key, "Debug") == 0)
			  m_networkDebug = ::atoi(value) == 1;
	  } else if (section == SECTION_OPENBRIDGE) {
		  if (::strcmp(key, "Enabled") == 0)
			  m_obpEnabled = ::atoi(value) == 1;
		  else if (::strcmp(key, "Address") == 0)
			  m_obpAddress = value;
		  else if (::strcmp(key, "PeerPort") == 0)
			  m_obpPeerPort = (unsigned short)::atoi(value);
		  else if (::strcmp(key, "Port") == 0)
			  m_obpPort = (unsigned short)::atoi(value);
		  else if (::strcmp(key, "Passphrase") == 0)
			  m_obpPassphrase = value;
		  else if (::strcmp(key, "ID") == 0)
			  m_obpId = (unsigned int)::atoi(value);
		  else if (::strcmp(key, "Callsign") == 0)
			  m_obpCallsign = value;
		  else if (::strcmp(key, "Debug") == 0)
			  m_obpDebug = ::atoi(value) == 1;
	  } else if (section == SECTION_VOCODER) {
		  if (::strcmp(key, "Enabled") == 0)
			  m_vocoderEnabled = ::atoi(value) == 1;
		  else if (::strcmp(key, "Builtin") == 0)
			  m_vocoderBuiltin = ::atoi(value) == 1;
		  else if (::strcmp(key, "Address") == 0)
			  m_vocoderAddress = value;
		  else if (::strcmp(key, "AmbePort") == 0)
			  m_vocoderAmbePort = (unsigned short)::atoi(value);
		  else if (::strcmp(key, "ImbePort") == 0)
			  m_vocoderImbePort = (unsigned short)::atoi(value);
	  }
  }

  ::fclose(fp);

  return true;
}

bool CConf::getDaemon() const
{
	return m_daemon;
}

std::string CConf::getLookupName() const
{
	return m_lookupName;
}

unsigned int CConf::getLookupTime() const
{
	return m_lookupTime;
}

unsigned int CConf::getLogDisplayLevel() const
{
	return m_logDisplayLevel;
}

unsigned int CConf::getLogFileLevel() const
{
	return m_logFileLevel;
}

std::string CConf::getLogFilePath() const
{
  return m_logFilePath;
}

std::string CConf::getLogFileRoot() const
{
  return m_logFileRoot;
}

bool CConf::getLogFileRotate() const
{
	return m_logFileRotate;
}

unsigned short CConf::getNetworkPort() const
{
	return m_networkPort;
}

bool CConf::getNetworkDebug() const
{
	return m_networkDebug;
}

bool CConf::getObpEnabled() const
{
	return m_obpEnabled;
}

std::string CConf::getObpAddress() const
{
	return m_obpAddress;
}

unsigned short CConf::getObpPeerPort() const
{
	return m_obpPeerPort;
}

unsigned short CConf::getObpPort() const
{
	return m_obpPort;
}

std::string CConf::getObpPassphrase() const
{
	return m_obpPassphrase;
}

unsigned int CConf::getObpId() const
{
	return m_obpId;
}

std::string CConf::getObpCallsign() const
{
	return m_obpCallsign;
}

bool CConf::getObpDebug() const
{
	return m_obpDebug;
}

bool CConf::getVocoderEnabled() const
{
	return m_vocoderEnabled;
}

bool CConf::getVocoderBuiltin() const
{
	return m_vocoderBuiltin;
}

std::string CConf::getVocoderAddress() const
{
	return m_vocoderAddress;
}

unsigned short CConf::getVocoderAmbePort() const
{
	return m_vocoderAmbePort;
}

unsigned short CConf::getVocoderImbePort() const
{
	return m_vocoderImbePort;
}
