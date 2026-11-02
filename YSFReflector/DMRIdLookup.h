/*
 *   Copyright (C) 2026
 *
 *   Callsign <-> DMR ID table for the OpenBridge link.
 *   File format matches DMRIds.dat (id, then callsign) and RadioID CSV.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#if !defined(DMRIDLOOKUP_H)
#define	DMRIDLOOKUP_H

#include <string>
#include <unordered_map>

class CDMRIdLookup {
public:
	CDMRIdLookup(const std::string& filename, unsigned int fallbackId);

	bool load();

	unsigned int size() const;

	// Callsign (upper case, not padded) to DMR ID. Unknown callsigns return
	// the configured fallback, which may be 0.
	unsigned int findId(const std::string& callsign) const;

	// DMR ID to a callsign suitable for a YSF header. Unknown IDs are rendered
	// as the decimal ID.
	std::string findCallsign(unsigned int id) const;

private:
	std::string                                      m_filename;
	unsigned int                                     m_fallbackId;
	std::unordered_map<unsigned int, std::string>    m_idToCall;
	std::unordered_map<std::string, unsigned int>    m_callToId;

	static std::string normalize(const char* text);
};

#endif
