// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Develop_EditorConfig.h"
#include "Malterlib_Develop_TextLayout.h"

#include <Mib/Container/Vector>
#include <Mib/Storage/Optional>

namespace NMib::NDevelop
{
	// The opt-in property selects a formatter profile, not a language.
	enum class ECodeFormattingProfile
	{
		mc_Disabled
		, mc_Malterlib
	};

	enum class ECodeLanguage
	{
		mc_Unknown
		, mc_Cpp
	};

	// Resolved and validated formatting configuration for one file. The constructor
	// rejects invalid values; a missing opt-in property leaves formatting disabled.
	struct CCodeFormattingSettings
	{
		CCodeFormattingSettings() = default;
		explicit CCodeFormattingSettings(CEditorConfigProperties const &_Properties);

		bool f_IsFormattingEnabled() const;

		ECodeFormattingProfile m_Profile = ECodeFormattingProfile::mc_Disabled;
		bool m_bIndentWithTabs = true;
		umint m_nIndentSize = 4;
		umint m_nTabWidth = 4;
		umint m_nMaxColumns = 0;						// Zero disables the column limit.
		bool m_bHasExplicitMaxColumns = false;
		bool m_bTrimTrailingWhitespace = true;
		bool m_bInsertFinalNewline = true;
		NStorage::TCOptional<ETextLineEnding> m_EndOfLine;			// Unset preserves the file's representation.
		NStr::CStr m_Charset;										// Empty when unconstrained.
	};

	ECodeLanguage fg_DetectCodeLanguage(NStr::CStr const &_Path);

	struct CCodeFormattingRange
	{
		umint m_iOffset = 0;
		umint m_nLength = 0;

		umint f_GetEnd() const;
	};

	enum class ECodeRangePolicy
	{
		mc_Expand			// Grow a selection to the formatting units it touches.
		, mc_Strict			// Never modify anything outside the requested range.
	};

	struct CCodeFormattingRequest
	{
		NStr::CStr m_Source;							// Immutable original bytes.
		NStr::CStr m_Path;								// Diagnostics only; the engine performs no I/O.
		ECodeLanguage m_Language = ECodeLanguage::mc_Unknown;
		CCodeFormattingSettings m_Settings;
		NContainer::TCVector<CCodeFormattingRange> m_Ranges;		// Empty selects the whole file.
		ECodeRangePolicy m_RangePolicy = ECodeRangePolicy::mc_Expand;
	};

	struct CCodeFormattingEdit
	{
		umint m_iOffset = 0;							// Original byte coordinates.
		umint m_nLength = 0;
		NStr::CStr m_Replacement;
		NStr::CStr m_Rule;

		umint f_GetEnd() const;
	};

	enum class ECodeFormattingSeverity
	{
		mc_Warning
		, mc_Error
	};

	struct CCodeFormattingDiagnostic
	{
		NStr::CStr m_Rule;
		ECodeFormattingSeverity m_Severity = ECodeFormattingSeverity::mc_Warning;
		umint m_iOffset = 0;							// Original byte coordinates.
		umint m_nLength = 0;
		umint m_iLine = 1;								// One-based original source line.
		umint m_iColumn = 1;							// One-based column, using the configured tab stops.
		NStr::CStr m_Explanation;
		bool m_bHasAutomaticFix = false;
	};

	enum class ECodeFormattingStatus
	{
		mc_Complete				// The edit plan is canonical for the requested ranges.
		, mc_Unsupported		// The language, encoding, or source structure is not handled.
		, mc_Failed				// Analysis could not produce a usable plan.
	};

	struct CCodeFormattingResult
	{
		ECodeFormattingStatus m_Status = ECodeFormattingStatus::mc_Unsupported;
		NContainer::TCVector<CCodeFormattingEdit> m_Edits;					// Ordered by offset and non-overlapping.
		NContainer::TCVector<CCodeFormattingRange> m_EffectiveRanges;
		NContainer::TCVector<CCodeFormattingDiagnostic> m_Diagnostics;
		NStr::CStr m_Explanation;											// Set when the status is not complete.

		bool f_HasEdits() const;
		bool f_HasUnfixableDiagnostics() const;
	};

	// Pure analysis: produces a canonical edit plan plus residual diagnostics. No filesystem access.
	CCodeFormattingResult fg_AnalyzeCodeFormatting(CCodeFormattingRequest const &_Request);

	// Validates that the plan is ordered, non-overlapping, and inside the source, then applies it.
	NStr::CStr fg_ApplyCodeFormattingEdits(NStr::CStr const &_Source, NContainer::TCVector<CCodeFormattingEdit> const &_Edits);

	// True when the two sources have the same significant token sequence, ignoring layout.
	// Every rule in the current matrix is whitespace-only, so this holds for every plan.
	bool fg_HasEquivalentCodeTokens(NStr::CStr const &_First, NStr::CStr const &_Second);

	// Describes the first token difference between two sources, for formatter diagnostics.
	NStr::CStr fg_DescribeCodeTokenDifference(NStr::CStr const &_First, NStr::CStr const &_Second);
}
