#ifndef ZAPAROO_SUPPORT_H
#define ZAPAROO_SUPPORT_H

#include <string>

class Zaparoo
{
public:
	static bool isZaparooEnabled();
	static bool writeZaparooCard(const std::string& name);
};

#endif // ZAPAROO_SUPPORT_H
