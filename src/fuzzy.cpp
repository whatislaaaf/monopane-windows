#include "fuzzy.h"
#include <cwctype>

int FuzzyScore(const std::wstring& query, const std::wstring& target)
{
    if (query.empty())
        return 0;

    int score = 0;
    int streak = 0;
    size_t ti = 0;

    for (size_t qi = 0; qi < query.size(); ++qi) {
        const wchar_t qc = towlower(query[qi]);
        bool found = false;

        while (ti < target.size()) {
            const wchar_t tc = towlower(target[ti]);
            if (tc == qc) {
                score += 1;

                const bool boundary = ti == 0 || !iswalnum(target[ti - 1]);
                const bool camel = ti > 0 && iswupper(target[ti]) && iswlower(target[ti - 1]);
                if (boundary)
                    score += 8;
                else if (camel)
                    score += 6;

                if (streak > 0)
                    score += 4 + streak;
                ++streak;

                if (ti < 3)
                    score += 3 - static_cast<int>(ti);

                ++ti;
                found = true;
                break;
            }
            streak = 0;
            ++ti;
        }

        if (!found)
            return -1;
    }

    return score;
}
