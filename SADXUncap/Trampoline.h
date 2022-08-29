#pragma once

#include <SADXModLoader.h>
#include <vector>

class Trampoline
{
	private:
		void *target;
		void *detour;
		LPVOID codeData;
		size_t originalSize;
		const bool revert;

	public:
		Trampoline(intptr_t start, intptr_t end, void *func, bool destructRevert = true);
		~Trampoline();

		// Pointer to original code.
		LPVOID Target() const
		{
			return codeData;
		}
		// Pointer to your detour.
		void *Detour() const
		{
			return detour;
		}
		// Original data size.
		size_t OriginalSize() const
		{
			return originalSize;
		}
		// Size of Target including appended jump to remaining original code.
		size_t CodeSize() const
		{
			return originalSize + 5;
		}
};