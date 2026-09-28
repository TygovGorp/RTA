#pragma once

/*
 *	Fully written by Claude
*/


#include "CoreMinimal.h"
#include "Echogram.h"

namespace RTA
{
	/**
	 * Writes an echogram to an .xlsx with two sheets:
	 */
	bool WriteEchogramXlsx(const FString& Path, const FEchogram& Echogram,
	                       const FString& Note, int32& OutNonFiniteCount);
}
