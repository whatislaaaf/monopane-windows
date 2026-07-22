#pragma once
#include <string>

// Learned search aliases: each activation with a non-empty query remembers
// query -> chosen app's exe path (last choice wins), so repeating the query
// ranks that app's windows first. Persisted under HKCU\Software\Monopane\Aliases.

void LoadAliases();

// Remembers `query` (case-insensitive) as an alias for `exePath`.
void SaveAlias(const std::wstring& query, const std::wstring& exePath);

// Returns the exe path aliased to `query`, or an empty string.
std::wstring LookupAlias(const std::wstring& query);

// Forgets all learned aliases.
void ClearAliases();
