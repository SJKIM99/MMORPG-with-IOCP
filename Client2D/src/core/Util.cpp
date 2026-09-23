#include "core/Util.h"

namespace util
{
	std::string FormatNumber(long long v)
	{
		const bool neg = v < 0;
		unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
		std::string digits = std::to_string(u);
		std::string out;
		out.reserve(digits.size() + digits.size() / 3 + 1);
		int count = 0;
		for (int i = (int)digits.size() - 1; i >= 0; --i)
		{
			out.push_back(digits[i]);
			if (++count % 3 == 0 && i > 0)
				out.push_back(',');
		}
		if (neg)
			out.push_back('-');
		std::reverse(out.begin(), out.end());
		return out;
	}
}
