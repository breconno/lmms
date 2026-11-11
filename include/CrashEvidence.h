#pragma once

namespace lmms
{

class CrashEvidence final
{
public:
	static void initialize();
	static void shutdown();
	static void breadcrumb(const char *text);
	static bool enabled();
};

} // namespace lmms
