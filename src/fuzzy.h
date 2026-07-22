#pragma once
#include <string>

// Scores how well `query` fuzzy-matches `target` (case-insensitive subsequence).
// Higher is better. Returns -1 if the query is not a subsequence of the target.
int FuzzyScore(const std::wstring& query, const std::wstring& target);
