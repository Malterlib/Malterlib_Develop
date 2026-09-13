// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_TextLayout.h"

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;

	CTextLineMap::CTextLineMap(CStr const &_Text)
		: mp_nTextLength(_Text.f_GetLen())
	{
		auto pStart = _Text.f_GetStr();
		umint iOffset = 0;
		while (iOffset < mp_nTextLength)
		{
			auto iContent = iOffset;
			while (iOffset < mp_nTextLength && pStart[iOffset] != '\n' && pStart[iOffset] != '\r')
				++iOffset;

			auto &Line = mp_Lines.f_Insert();
			Line.m_iOffset = iContent;
			Line.m_nLength = iOffset - iContent;
			if (iOffset == mp_nTextLength)
				break;

			if (pStart[iOffset] == '\n')
			{
				Line.m_Ending = ETextLineEnding::mc_LF;
				++iOffset;
			}
			else if (iOffset + 1 < mp_nTextLength && pStart[iOffset + 1] == '\n')
			{
				Line.m_Ending = ETextLineEnding::mc_CRLF;
				iOffset += 2;
			}
			else
			{
				Line.m_Ending = ETextLineEnding::mc_CR;
				++iOffset;
			}
		}

		// A terminated final line is followed by an empty, unterminated line so that
		// every byte offset, including the end of the text, maps to a line.
		if (mp_Lines.f_IsEmpty() || mp_Lines.f_GetLast().m_Ending != ETextLineEnding::mc_None)
		{
			auto &Line = mp_Lines.f_Insert();
			Line.m_iOffset = mp_nTextLength;
		}
	}

	umint CTextLineMap::f_GetLineCount() const
	{
		return mp_Lines.f_GetLen();
	}

	CTextLine const &CTextLineMap::f_GetLine(umint _iLine) const
	{
		return mp_Lines[_iLine];
	}

	umint CTextLineMap::f_GetLineStart(umint _iLine) const
	{
		return mp_Lines[_iLine].m_iOffset;
	}

	umint CTextLineMap::f_GetLineContentEnd(umint _iLine) const
	{
		auto const &Line = mp_Lines[_iLine];

		return Line.m_iOffset + Line.m_nLength;
	}

	umint CTextLineMap::f_GetTerminatorLength(umint _iLine) const
	{
		switch (mp_Lines[_iLine].m_Ending)
		{
			case ETextLineEnding::mc_None: return 0;
			case ETextLineEnding::mc_CRLF: return 2;
			default: return 1;
		}
	}

	umint CTextLineMap::f_GetLineEnd(umint _iLine) const
	{
		return f_GetLineContentEnd(_iLine) + f_GetTerminatorLength(_iLine);
	}

	umint CTextLineMap::f_FindLine(umint _iOffset) const
	{
		umint iLow = 0;
		umint iHigh = mp_Lines.f_GetLen() - 1;
		while (iLow < iHigh)
		{
			auto iMiddle = iLow + (iHigh - iLow + 1) / 2;
			if (mp_Lines[iMiddle].m_iOffset <= _iOffset)
				iLow = iMiddle;
			else
				iHigh = iMiddle - 1;
		}

		return iLow;
	}

	bool CTextLineMap::f_IsEmpty() const
	{
		return !mp_nTextLength;
	}

	umint fg_GetTextBomLength(CStr const &_Text)
	{
		return _Text.f_StartsWith("\xEF\xBB\xBF") ? 3 : 0;
	}

	CStr fg_GetTextLineEndingBytes(ETextLineEnding _Ending)
	{
		switch (_Ending)
		{
			case ETextLineEnding::mc_LF: return "\n";
			case ETextLineEnding::mc_CRLF: return "\r\n";
			case ETextLineEnding::mc_CR: return "\r";
			default: return {};
		}
	}

	bool fg_MeasureTextColumns(ch8 const *_pText, umint _nLength, umint _nTabWidth, umint &o_nColumns)
	{
		// The iterator's character value may be NUL before the end of its input, so the
		// loop is bounded by the iterator distance rather than by a terminator.
		umint nColumns = 0;
		umint iPrevious = 0;
		if (_nLength)
		{
			CStrIteratorUTF8 End(_pText + _nLength, 0);
			for (CStrIteratorUTF8 Iterator(_pText, _nLength); ; ++Iterator)
			{
				auto iEnd = _nLength - umint(End - Iterator);
				umint nAdvance = 1;
				if (Iterator.f_IsBroken() || !Iterator.f_IsWholeCodePoint())
					nAdvance = iEnd - iPrevious;
				else if (*Iterator == '\t')
					nAdvance = _nTabWidth - nColumns % _nTabWidth;

				if (nColumns > TCLimitsInt<umint>::mc_Max - nAdvance)
					return false;

				nColumns += nAdvance;
				iPrevious = iEnd;

				if (Iterator - End >= 0)
					break;
			}
		}

		o_nColumns = nColumns;

		return true;
	}

	bool fg_MeasureTextColumns(CStr const &_Text, umint _nTabWidth, umint &o_nColumns)
	{
		return fg_MeasureTextColumns(_Text.f_GetStr(), _Text.f_GetLen(), _nTabWidth, o_nColumns);
	}
}
