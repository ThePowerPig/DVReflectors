/*
 *   Copyright (C) 2026
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "DMRIdLookup.h"
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

CDMRIdLookup::CDMRIdLookup(const std::string& filename, unsigned int fallbackId) :
m_filename(filename),
m_fallbackId(fallbackId),
m_idToCall(),
m_callToId()
{
}

static void trimInPlace(char* text)
{
	if (text == NULL)
		return;

	char* start = text;
	while (*start == ' ' || *start == '\t')
		start++;

	if (start != text)
		::memmove(text, start, ::strlen(start) + 1U);

	size_t len = ::strlen(text);
	while (len > 0U && (text[len - 1U] == ' ' || text[len - 1U] == '\t' || text[len - 1U] == '\r' || text[len - 1U] == '\n')) {
		text[len - 1U] = '\0';
		len--;
	}
}

std::string CDMRIdLookup::normalize(const char* text)
{
	if (text == NULL)
		return std::string();

	std::string out;
	for (const char* p = text; *p != '\0'; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == ' ' || c == '\t')
			break;
		out.push_back((char)::toupper(c));
		if (out.size() >= 10U)
			break;
	}

	return out;
}

bool CDMRIdLookup::load()
{
	m_idToCall.clear();
	m_callToId.clear();

	if (m_filename.empty())
		return true;

	FILE* fp = ::fopen(m_filename.c_str(), "rt");
	if (fp == NULL) {
		LogWarning("OpenBridge cannot open the DMR ID file - %s", m_filename.c_str());
		return false;
	}

	char buffer[256U];
	while (::fgets(buffer, 256U, fp) != NULL) {
		if (buffer[0U] == '#' || buffer[0U] == '\0' || buffer[0U] == '\n' || buffer[0U] == '\r')
			continue;

		char* idText = buffer;
		char* callText = NULL;

		char* comma = ::strchr(buffer, ',');
		if (comma != NULL) {
			*comma = '\0';
			callText = comma + 1;
			char* comma2 = ::strchr(callText, ',');
			if (comma2 != NULL)
				*comma2 = '\0';
		} else {
			idText = ::strtok(buffer, " \t\r\n");
			callText = ::strtok(NULL, " \t\r\n");
		}

		if (idText == NULL || callText == NULL)
			continue;

		trimInPlace(idText);
		trimInPlace(callText);

		if (idText[0U] == '\0' || callText[0U] == '\0')
			continue;

		unsigned int id = (unsigned int)::strtoul(idText, NULL, 10);
		std::string callsign = normalize(callText);
		if (id == 0U || callsign.empty())
			continue;

		m_idToCall[id] = callsign;
		if (m_callToId.find(callsign) == m_callToId.end())
			m_callToId[callsign] = id;
	}

	::fclose(fp);

	LogInfo("OpenBridge loaded %u DMR IDs from %s", (unsigned int)m_idToCall.size(), m_filename.c_str());

	return !m_idToCall.empty();
}

unsigned int CDMRIdLookup::size() const
{
	return (unsigned int)m_idToCall.size();
}

unsigned int CDMRIdLookup::findId(const std::string& callsign) const
{
	std::string key = normalize(callsign.c_str());
	std::unordered_map<std::string, unsigned int>::const_iterator it = m_callToId.find(key);
	if (it == m_callToId.end())
		return m_fallbackId;

	return it->second;
}

std::string CDMRIdLookup::findCallsign(unsigned int id) const
{
	std::unordered_map<unsigned int, std::string>::const_iterator it = m_idToCall.find(id);
	if (it != m_idToCall.end())
		return it->second;

	char text[16U];
	::sprintf(text, "%u", id);
	return std::string(text);
}
