// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Vector>
#include <Mib/String/String>

namespace NMib::NDevelop
{
	enum class ETextLineEnding
	{
		mc_None			// The final line is not terminated.
		, mc_LF
		, mc_CRLF
		, mc_CR
	};

	struct CTextLine
	{
		umint m_iOffset = 0;		// Byte offset of the first content byte.
		umint m_nLength = 0;		// Content length in bytes, excluding the terminator.
		ETextLineEnding m_Ending = ETextLineEnding::mc_None;
	};

	// Splits text into lines on LF, CRLF, and CR, recording each terminator so a
	// rewritten file can preserve or normalize the original representation.
	struct CTextLineMap
	{
		CTextLineMap() = default;
		explicit CTextLineMap(NStr::CStr const &_Text);

		umint f_GetLineCount() const;
		CTextLine const &f_GetLine(umint _iLine) const;
		umint f_GetLineStart(umint _iLine) const;
		umint f_GetLineContentEnd(umint _iLine) const;
		umint f_GetLineEnd(umint _iLine) const;					// Past the terminator.
		umint f_GetTerminatorLength(umint _iLine) const;

		// Zero-based line containing the byte offset. An offset at or past the end maps to the last line.
		umint f_FindLine(umint _iOffset) const;

		bool f_IsEmpty() const;

	private:
		NContainer::TCVector<CTextLine> mp_Lines;
		umint mp_nTextLength = 0;
	};

	// Length of a leading UTF-8 byte-order mark, or zero.
	umint fg_GetTextBomLength(NStr::CStr const &_Text);
	NStr::CStr fg_GetTextLineEndingBytes(ETextLineEnding _Ending);

	// Measures display columns using EditorConfig tab stops. Malformed or partial UTF-8
	// sequences count their remaining bytes, matching the existing validation model.
	// Returns false when the column counter would overflow; o_nColumns is then unspecified.
	bool fg_MeasureTextColumns(ch8 const *_pText, umint _nLength, umint _nTabWidth, umint &o_nColumns);
	bool fg_MeasureTextColumns(NStr::CStr const &_Text, umint _nTabWidth, umint &o_nColumns);
}
