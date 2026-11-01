#pragma once
#include <cstddef>
#include <cstdint>

namespace oc2_death_notice {
// ReGameDLL's optional DeathMsg tail. Legacy and truncated packets have no assist.
inline int Assister(const void* packet, int size)
{
	if (!packet || size < 4) return 0;
	const auto* p = static_cast<const unsigned char*>(packet);
	std::size_t cursor = 3, length = static_cast<std::size_t>(size);
	while (cursor < length && p[cursor]) ++cursor;
	if (cursor == length || length - ++cursor < 4) return 0;
	const std::uint32_t flags = std::uint32_t(p[cursor]) | (std::uint32_t(p[cursor+1]) << 8)
		| (std::uint32_t(p[cursor+2]) << 16) | (std::uint32_t(p[cursor+3]) << 24);
	cursor += 4;
	if (flags & ~7u) return 0;
	const std::size_t needed = ((flags & 1u) ? 6 : 0) + ((flags & 2u) ? 1 : 0) + ((flags & 4u) ? 4 : 0);
	if (length - cursor < needed || !(flags & 2u)) return 0;
	if (flags & 1u) cursor += 6;
	const int slot = p[cursor];
	return slot >= 1 && slot <= 32 && slot != p[0] && slot != p[1] ? slot : 0;
}
}
