// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

	// The build system's syntax has no C++ structure; its rules read brackets and lines alone.
	CCodeStructure fg_BuildStructure(CCodeTokenStream &_Tokens, CCodeFormattingSettings const &_Settings)
	{
		if (_Settings.f_GetLanguage() == ECodeLanguage::mc_BuildSystem)
			return {};

		return CCodeStructure(_Tokens, _Settings.m_nTabWidth);
	}

	// Whether the splice joins text in front of it to text behind it, through further splices, with no blank between.
	bool fg_SpliceJoinsText(ch8 const *_pSource, umint _nSource, CCodeToken const &_Splice)
	{
		auto fBlank = [](ch8 _Char)
			{
				return _Char == ' ' || _Char == '\t' || _Char == '\n' || _Char == '\r';
			}
		;

		if (!_Splice.m_iOffset || fBlank(_pSource[_Splice.m_iOffset - 1]))
			return false;

		auto iChar = _Splice.f_GetEnd();
		while (iChar + 1 < _nSource && _pSource[iChar] == '\\' && (_pSource[iChar + 1] == '\n' || _pSource[iChar + 1] == '\r'))
		{
			iChar += 2;
			if (_pSource[iChar - 1] == '\r' && iChar < _nSource && _pSource[iChar] == '\n')
				++iChar;
		}

		return iChar < _nSource && !fBlank(_pSource[iChar]);
	}

	bool fg_IsDigraph(ch8 const *_pText, umint _nLength)
	{
		auto First = _pText[0];
		auto Second = _pText[1];
		if (_nLength == 4)
			return First == '%' && Second == ':' && _pText[2] == '%' && _pText[3] == ':';

		return (First == '<' && (Second == '%' || Second == ':')) || (First == '%' && (Second == '>' || Second == ':')) || (First == ':' && Second == '>');
	}
}

namespace NMib::NDevelop::NPrivate
{
	CFormattingAnalyzer::CFormattingAnalyzer(CCodeFormattingRequest const &_Request, bool _bAllowConversions, bool _bAllowQualifiers)
		: mp_Request(_Request)
		, mp_pSource(_Request.m_Source.f_GetStr())
		, mp_Tokens(_Request.m_Source, _Request.m_pStyle.f_Get(), _Request.m_Settings.f_GetLanguage())
		, mp_Structure(fg_BuildStructure(mp_Tokens, _Request.m_Settings))
		, mp_Lines(_Request.m_Source)
		, mp_bAllowConversions(_bAllowConversions)
		, mp_bAllowQualifiers(_bAllowQualifiers)
	{
	}

	void CFormattingAnalyzer::fp_PrepareLineProtection()
	{
		mp_ProtectedStart.f_SetLen(mp_Lines.f_GetLineCount());
		mp_ProtectedEnd.f_SetLen(mp_Lines.f_GetLineCount());

		for (umint iLine = 0; iLine < mp_Lines.f_GetLineCount(); ++iLine)
		{
			mp_ProtectedStart[iLine] = 0;
			mp_ProtectedEnd[iLine] = 0;
		}

		for (auto const &Token : mp_Tokens.f_GetTokens())
		{
			if (!Token.m_bMultiLine || Token.m_Kind == ECodeTokenKind::mc_Newline)
				continue;

			auto iFirst = mp_Lines.f_FindLine(Token.m_iOffset);
			auto iLast = mp_Lines.f_FindLine(Token.f_GetEnd() ? Token.f_GetEnd() - 1 : 0);

			for (umint iLine = iFirst; iLine < iLast; ++iLine)
				mp_ProtectedEnd[iLine] = 1;

			// A splice only protects the end of the line carrying the backslash; the
			// spliced line's own indentation stays ordinary layout.
			if (Token.m_Kind == ECodeTokenKind::mc_LineSplice)
				continue;

			for (umint iLine = iFirst + 1; iLine <= iLast; ++iLine)
				mp_ProtectedStart[iLine] = 1;
		}
	}

	// An MSVC '__asm' without braces takes the rest of its line, which no line break may then join anything to: '__asm nop'.
	// GCC's is a call: '__asm("nop")', '__asm __volatile("nop")'.
	bool CFormattingAnalyzer::fp_CheckUnbracedAssembly(CStr &o_Explanation) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			auto const &Token = Tokens[iToken];
			if (Token.m_Kind != ECodeTokenKind::mc_Identifier || (!mp_Tokens.f_IsText(Token, "__asm") && !mp_Tokens.f_IsText(Token, "_asm")))
				continue;

			auto iNext = fp_NextCode(iToken);
			if (iNext < 0)
				continue;

			// A block's instructions are its own, '__asm { __asm mov eax, 1 __asm nop }' included.
			if (mp_Tokens.f_IsText(Tokens[umint(iNext)], "{"))
			{
				auto iClose = fp_FindAssemblyClose(umint(iNext));
				if (iClose < 0)
				{
					o_Explanation = "The end of the MSVC '__asm' block at line {} is not a token of its own, so the file is left untouched"_f << mp_Lines.f_FindLine(Token.m_iOffset) + 1;

					return false;
				}

				iToken = umint(iClose);

				continue;
			}

			bool bGnu = false;
			for (auto pOpen : {"(", "volatile", "__volatile", "__volatile__", "goto", "inline"})
				bGnu = bGnu || mp_Tokens.f_IsText(Tokens[umint(iNext)], pOpen);

			if (bGnu)
				continue;

			o_Explanation = "The MSVC '__asm' at line {} takes the rest of its line, which is left untouched"_f << mp_Lines.f_FindLine(Token.m_iOffset) + 1;

			return false;
		}

		return true;
	}

	// The brace closing the MSVC '__asm' block the brace opens, matched as the assembler reads the block: a ';' comments out
	// the rest of its line as '//' does, braces in it included. Negative where that brace starts no token of its own.
	aint CFormattingAnalyzer::fp_FindAssemblyClose(umint _iOpen) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto nSource = mp_Request.m_Source.f_GetLen();
		auto fLineEnd = [&](umint _iChar)
			{
				while (_iChar < nSource && mp_pSource[_iChar] != '\n' && mp_pSource[_iChar] != '\r')
					++_iChar;

				return _iChar;
			}
		;

		umint nDepth = 0;
		for (auto iChar = Tokens[_iOpen].m_iOffset; iChar < nSource; ++iChar)
		{
			auto Char = mp_pSource[iChar];
			if (Char == ';' || (Char == '/' && iChar + 1 < nSource && mp_pSource[iChar + 1] == '/'))
				iChar = fLineEnd(iChar);
			else if (Char == '/' && iChar + 1 < nSource && mp_pSource[iChar + 1] == '*')
			{
				auto iClose = mp_Request.m_Source.f_Find(aint(iChar + 2), "*/");
				if (iClose < 0)
					return -1;

				iChar = umint(iClose) + 1;
			}
			else if (Char == '{')
				++nDepth;
			else if (Char == '}' && !--nDepth)
			{
				auto iToken = mp_Tokens.f_FindToken(iChar);
				if (iToken < Tokens.f_GetLen() && Tokens[iToken].m_iOffset == iChar && mp_Tokens.f_IsText(Tokens[iToken], "}"))
					return aint(iToken);

				return -1;
			}
		}

		return -1;
	}

	bool CFormattingAnalyzer::fp_CollectDisabledRegions(CStr &o_Explanation)
	{
		bool bDisabled = false;
		umint iDisabledStart = 0;
		umint iDirective = 0;

		auto fFormatOff = [&](umint _iOffset)
			{
				if (bDisabled)
				{
					o_Explanation = "Nested '{}' directive at line {}"_f << gc_FormatOffDirective << mp_Lines.f_FindLine(_iOffset) + 1;

					return false;
				}

				bDisabled = true;
				iDisabledStart = mp_Lines.f_GetLineStart(mp_Lines.f_FindLine(_iOffset));
				iDirective = _iOffset;

				return true;
			}
		;

		auto fFormatOn = [&](umint _iOffset)
			{
				if (!bDisabled)
				{
					o_Explanation = "'{}' at line {} has no matching '{}'"_f << gc_FormatOnDirective << mp_Lines.f_FindLine(_iOffset) + 1 << gc_FormatOffDirective;

					return false;
				}

				auto &Range = mp_Disabled.f_Insert();
				Range.m_iOffset = iDisabledStart;
				Range.m_nLength = mp_Lines.f_GetLineEnd(mp_Lines.f_FindLine(_iOffset)) - iDisabledStart;

				bDisabled = false;

				return true;
			}
		;

		for (auto const &Token : mp_Tokens.f_GetTokens())
		{
			if (Token.m_Kind == ECodeTokenKind::mc_BlockComment)
			{
				// Inside a block comment the directive stands on a line of its own, without the '//', and disables the lines
				// from the one it stands on, as it does in front of code commented out.
				auto pText = mp_pSource + Token.m_iOffset;
				umint iLineStart = 0;

				while (iLineStart < Token.m_nLength)
				{
					auto pLine = pText + iLineStart;
					umint nLine = 0;
					while (iLineStart + nLine < Token.m_nLength && pLine[nLine] != '\n' && pLine[nLine] != '\r')
						++nLine;

					// The line's text between its blanks, read in place, and without the comment's markers.
					auto pBegin = pLine;
					auto pEnd = pLine + nLine;

					auto fTrim = [&]
						{
							while (pBegin < pEnd && fg_CharIsWhiteSpace(*pBegin))
								++pBegin;

							while (pEnd > pBegin && fg_CharIsWhiteSpace(pEnd[-1]))
								--pEnd;
						}
					;

					fTrim();

					if (pEnd - pBegin >= 2 && pBegin[0] == '/' && pBegin[1] == '*')
					{
						pBegin += 2;
						fTrim();
					}

					if (pEnd - pBegin >= 2 && pEnd[-2] == '*' && pEnd[-1] == '/')
					{
						pEnd -= 2;
						fTrim();
					}

					auto nText = umint(pEnd - pBegin);
					if (fg_IsSameText(gc_Str<"malterlib-format off">.m_Str, pBegin, nText) && !fFormatOff(Token.m_iOffset + iLineStart))
						return false;

					if (fg_IsSameText(gc_Str<"malterlib-format on">.m_Str, pBegin, nText) && !fFormatOn(Token.m_iOffset + iLineStart))
						return false;

					iLineStart += nLine + 1;
					if (iLineStart < Token.m_nLength && pLine[nLine] == '\r' && pLine[nLine + 1] == '\n')
						++iLineStart;
				}

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_LineComment)
				continue;

			// Read in place: only a directive becomes a string.
			auto pComment = mp_pSource + Token.m_iOffset;
			auto nComment = Token.m_nLength;
			while (nComment && fg_IsSpaceOrTab(pComment[nComment - 1]))
				--nComment;

			if (fg_IsSameText(gc_FormatOffDirective, pComment, nComment))
			{
				if (!fFormatOff(Token.m_iOffset))
					return false;

				continue;
			}

			if (!fg_IsSameText(gc_FormatOnDirective, pComment, nComment))
			{
				// The layout directives are read by the rules that act on them; any other is a mistake.
				auto const &Prefix = gc_Str<"// malterlib-format ">.m_Str;
				if (nComment < Prefix.f_GetLen() || !fg_IsSameText(Prefix, pComment, Prefix.f_GetLen()))
					continue;

				CStr Text(pComment, nComment);
				auto Directive = Text.f_Extract(Prefix.f_GetLen());
				auto iToken = umint(&Token - mp_Tokens.f_GetTokens().f_GetArray());

				if (Directive == "table")
					mp_ListLayouts.f_Insert(CListLayout{.m_iDirective = iToken, .m_Layout = EListLayout::mc_Table});
				else if (Directive == "rows")
					mp_ListLayouts.f_Insert(CListLayout{.m_iDirective = iToken, .m_Layout = EListLayout::mc_Rows});
				else if (Directive == "columns: auto")
					mp_ListLayouts.f_Insert(CListLayout{.m_iDirective = iToken, .m_Layout = EListLayout::mc_Columns});
				else if (Directive == "align: =")
					mp_AlignDirectives.f_Insert(iToken);
				else if (Directive.f_StartsWith("columns: "))
				{
					auto Count = Directive.f_Extract(9);
					umint nColumns = 0;
					bool bNumber = !Count.f_IsEmpty();
					auto pCount = Count.f_GetStr();

					for (umint iCharacter = 0; iCharacter < Count.f_GetLen() && bNumber; ++iCharacter)
					{
						auto Character = pCount[iCharacter];
						bNumber = fg_CharIsNumber(Character) && iCharacter < 5;
						nColumns = nColumns * 10 + umint(Character - '0');
					}

					if (!bNumber || !nColumns || nColumns > 1024)
					{
						o_Explanation = "'{}' at line {} needs a column count from 1 to 1024 or 'auto'"_f << Text << mp_Lines.f_FindLine(Token.m_iOffset) + 1;

						return false;
					}

					mp_ListLayouts.f_Insert(CListLayout{.m_iDirective = iToken, .m_Layout = EListLayout::mc_Columns, .m_nColumns = nColumns});
				}
				else
				{
					o_Explanation = "Unknown directive '{}' at line {}"_f << Text << mp_Lines.f_FindLine(Token.m_iOffset) + 1;

					return false;
				}

				continue;
			}

			if (!fFormatOn(Token.m_iOffset))
				return false;
		}

		if (bDisabled)
		{
			o_Explanation = "'{}' at line {} has no matching '{}'"_f << gc_FormatOffDirective << mp_Lines.f_FindLine(iDirective) + 1 << gc_FormatOnDirective;

			return false;
		}

		// The braces of an MSVC '__asm' block hold assembly, which keeps its spelling: 'mov dword ptr fs:[0h], eax'.
		// The parentheses of a call the naming lists as unformatted keep theirs too, as a macro turning its argument into
		// a string holds text of another language: 'BSON_STR({"a" : [1, 2]})'. Each range takes in its closer, so nothing
		// is inserted in front of it either.
		auto const &Tokens = mp_Tokens.f_GetTokens();

		auto fIsTrivia = [&](umint _iToken)
			{
				auto const &Token = Tokens[_iToken];
				return !fg_IsCodeToken(Token) && Token.m_Kind != ECodeTokenKind::mc_Preprocessor;
			}
		;

		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			auto const &Token = Tokens[iToken];
			if (Token.m_Kind == ECodeTokenKind::mc_Identifier && mp_Tokens.f_HasRole(Token, ECodeNameRole::mc_UnformattedCall))
			{
				auto iOpen = iToken + 1;
				while (iOpen < Tokens.f_GetLen() && fIsTrivia(iOpen))
					++iOpen;

				// The call's parenthesis stands behind its template argument list: 'f_Unformatted<int>( 1,2 )'.
				if (iOpen < Tokens.f_GetLen() && mp_Tokens.f_IsText(Tokens[iOpen], "<") && mp_Structure.f_IsAngleBracket(iOpen))
				{
					auto iList = mp_Structure.f_FindNodeOpeningAt(iOpen);
					if (iList < mp_Structure.f_GetNodes().f_GetLen())
					{
						iOpen = mp_Structure.f_GetNodes()[iList].m_iLastToken + 1;
						while (iOpen < Tokens.f_GetLen() && fIsTrivia(iOpen))
							++iOpen;
					}
				}

				if (iOpen >= Tokens.f_GetLen() || !mp_Tokens.f_IsText(Tokens[iOpen], "("))
					continue;

				umint nDepth = 0;
				for (auto iClose = iOpen; iClose < Tokens.f_GetLen(); ++iClose)
				{
					auto const &Close = Tokens[iClose];
					if (mp_Tokens.f_IsText(Close, "("))
						++nDepth;
					else if (mp_Tokens.f_IsText(Close, ")") && !--nDepth)
					{
						auto &Range = mp_Disabled.f_Insert();
						Range.m_iOffset = Tokens[iOpen].f_GetEnd();
						Range.m_nLength = Close.f_GetEnd() - Range.m_iOffset;

						iToken = iClose;

						break;
					}
				}

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_Identifier || (!mp_Tokens.f_IsText(Token, "__asm") && !mp_Tokens.f_IsText(Token, "_asm")))
				continue;

			auto iOpen = iToken + 1;
			while (iOpen < Tokens.f_GetLen() && fIsTrivia(iOpen))
				++iOpen;

			if (iOpen >= Tokens.f_GetLen() || !mp_Tokens.f_IsText(Tokens[iOpen], "{"))
				continue;

			auto iClose = fp_FindAssemblyClose(iOpen);
			if (iClose < 0)
				continue;

			auto &Range = mp_Disabled.f_Insert();
			Range.m_iOffset = Tokens[iOpen].f_GetEnd();
			Range.m_nLength = Tokens[umint(iClose)].f_GetEnd() - Range.m_iOffset;

			iToken = umint(iClose);
		}

		return true;
	}

	bool CFormattingAnalyzer::fp_ResolveRanges(CStr &o_Explanation)
	{
		auto nSource = mp_Request.m_Source.f_GetLen();

		mp_bWholeFile = mp_Request.m_Ranges.f_IsEmpty();
		if (mp_bWholeFile)
		{
			auto &Range = mp_Effective.f_Insert();
			Range.m_nLength = nSource;

			return true;
		}

		NContainer::TCVector<CCodeFormattingRange> Requested;
		for (auto const &Range : mp_Request.m_Ranges)
		{
			if (Range.m_nLength > nSource || Range.m_iOffset > nSource - Range.m_nLength)
			{
				o_Explanation = "Range {}+{} is outside the {} byte source"_f << Range.m_iOffset << Range.m_nLength << nSource;

				return false;
			}

			if (!fg_IsCodePointBoundary(mp_Request.m_Source, Range.m_iOffset) || !fg_IsCodePointBoundary(mp_Request.m_Source, Range.f_GetEnd()))
			{
				o_Explanation = "Range {}+{} starts or ends inside an encoded code point"_f << Range.m_iOffset << Range.m_nLength;

				return false;
			}

			auto fSplitsLineEnding = [&](umint _iOffset)
				{
					return _iOffset && _iOffset < nSource && mp_pSource[_iOffset - 1] == '\r' && mp_pSource[_iOffset] == '\n';
				}
			;

			if (fSplitsLineEnding(Range.m_iOffset) || fSplitsLineEnding(Range.f_GetEnd()))
			{
				o_Explanation = "Range {}+{} starts or ends inside a CRLF pair"_f << Range.m_iOffset << Range.m_nLength;

				return false;
			}

			auto Resolved = Range;
			if (mp_Request.m_RangePolicy == ECodeRangePolicy::mc_Expand)
			{
				// A zero-length range is a cursor: it selects the line it sits on, and at
				// end of file the preceding line. An empty file has an empty selection.
				bool bCursorAtEnd = !Range.m_nLength && nSource && Range.m_iOffset == nSource;
				auto iFirst = mp_Lines.f_FindLine(bCursorAtEnd ? nSource - 1 : Range.m_iOffset);
				auto iLast = Range.m_nLength ? mp_Lines.f_FindLine(Range.f_GetEnd() - 1) : iFirst;
				Resolved.m_iOffset = mp_Lines.f_GetLineStart(iFirst);
				Resolved.m_nLength = mp_Lines.f_GetLineEnd(iLast) - Resolved.m_iOffset;
			}

			Requested.f_Insert(Resolved);
		}

		Requested.f_Sort
			(
				[](CCodeFormattingRange const &_Left, CCodeFormattingRange const &_Right)
				{
					return _Left.m_iOffset <=> _Right.m_iOffset;
				}
			)
		;

		for (auto const &Range : Requested)
		{
			if (!mp_Effective.f_IsEmpty() && Range.m_iOffset <= mp_Effective.f_GetLast().f_GetEnd())
			{
				auto &Last = mp_Effective.f_GetLast();
				Last.m_nLength = fg_Max(Last.f_GetEnd(), Range.f_GetEnd()) - Last.m_iOffset;

				continue;
			}

			mp_Effective.f_Insert(Range);
		}

		return true;
	}

	// Whether an edit there is forbidden: the formatter is turned off for the text, or a directive lays it out itself.
	bool CFormattingAnalyzer::fp_IsDisabled(umint _iOffset, umint _nLength) const
	{
		if (fp_IsFormatOff(_iOffset, _nLength))
			return true;

		for (auto const &Range : mp_Owned)
		{
			if (_iOffset < Range.f_GetEnd() && Range.m_iOffset < _iOffset + fg_Max(_nLength, umint(1)))
				return true;
		}

		return false;
	}

	bool CFormattingAnalyzer::fp_IsFormatOff(umint _iOffset, umint _nLength) const
	{
		for (auto const &Range : mp_Disabled)
		{
			if (_iOffset < Range.f_GetEnd() && Range.m_iOffset < _iOffset + fg_Max(_nLength, umint(1)))
				return true;
		}

		return false;
	}

	// A 'columns' or 'table' directive stands on the first line inside the braced list it lays out, which no other
	// rule then edits inside.
	bool CFormattingAnalyzer::fp_CollectListLayouts(CStr &o_Explanation)
	{
		auto const &Nodes = mp_Structure.f_GetNodes();
		auto const &Tokens = mp_Tokens.f_GetTokens();

		mp_ListLayoutOfNode.f_SetLen(Nodes.f_GetLen());
		for (auto &iLayout : mp_ListLayoutOfNode)
			iLayout = 0;

		auto fIsBlank = [&](umint _iToken)
			{
				auto Kind = Tokens[_iToken].m_Kind;
				return Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline;
			}
		;

		for (umint iLayout = 0; iLayout < mp_ListLayouts.f_GetLen(); ++iLayout)
		{
			auto &Layout = mp_ListLayouts[iLayout];
			auto iOpen = aint(Layout.m_iDirective) - 1;
			while (iOpen >= 0 && fIsBlank(umint(iOpen)))
				--iOpen;

			auto iList = iOpen >= 0 && mp_Tokens.f_IsText(Tokens[umint(iOpen)], "{") ? mp_Structure.f_FindNodeOpeningAt(umint(iOpen)) : Nodes.f_GetLen();
			auto const *pList = iList < Nodes.f_GetLen() ? &Nodes[iList] : nullptr;
			bool bList = pList && pList->m_Kind == ECodeNodeKind::mc_Group && pList->m_Bracket == ECodeBracket::mc_Brace && fp_SpansLines(umint(iOpen), Layout.m_iDirective);
			if (!bList)
			{
				auto const &Directive = Tokens[Layout.m_iDirective];
				o_Explanation
					= "'{}' at line {} has to stand on the first line inside a braced list"_f
					<< fg_TrimCommentTrailingSpace(mp_Tokens.f_GetText(Directive))
					<< mp_Lines.f_FindLine(Directive.m_iOffset) + 1
				;

				return false;
			}

			Layout.m_iList = iList;
			mp_ListLayoutOfNode[iList] = iLayout + 1;

			auto &Owned = mp_Owned.f_Insert();
			Owned.m_iOffset = Tokens[umint(iOpen)].f_GetEnd();
			// The closing brace's indentation is inserted at its own offset, which is part of what the list owns.
			Owned.m_nLength = Tokens[pList->m_iLastToken].m_iOffset - Owned.m_iOffset + 1;
		}

		return true;
	}

	// An edit a directive makes in the text it owns.
	void CFormattingAnalyzer::fp_AddOwnedEdit(umint _iOffset, umint _nLength, CStr const &_Replacement, CStr const &_Explanation)
	{
		if (fp_IsFormatOff(_iOffset, _nLength) || fg_IsSameText(_Replacement, mp_pSource + _iOffset, _nLength))
			return;

		// The rows a directive lays out would move code off a line a NOLINT suppresses, or onto it.
		if (fp_IsSuppressed(_iOffset, _nLength))
			return;

		// The edit spans the whole construct, which a strict range holding part of it is told of rather than left unchanged.
		if (!fp_IsSelected(_iOffset, _nLength))
		{
			bool bTouches = false;
			for (auto const &Range : mp_Effective)
				bTouches = bTouches || (Range.m_iOffset < _iOffset + _nLength && _iOffset < Range.f_GetEnd());

			if (mp_Request.m_RangePolicy == ECodeRangePolicy::mc_Strict && bTouches)
			{
				fp_AddDiagnostic
					(
						"range-boundary"
						, _iOffset
						, _nLength
						, "the layout its directive asks for requires an edit outside the requested range; no edit was applied to this unit"
						, false
					)
				;
			}

			return;
		}

		auto &Edit = mp_Edits.f_Insert();
		Edit.m_iOffset = _iOffset;
		Edit.m_nLength = _nLength;
		Edit.m_Replacement = _Replacement;
		Edit.m_Rule = "layout-directive";
		mp_EditExplanations.f_Insert(_Explanation);
		mp_EditDropped.f_Insert(uint8(0));
	}

	// Lays out each list a 'columns' or 'table' directive owns, now that the layout has placed its braces: its rows
	// start with their comma one level in from the opening brace, the first indented as far as the comma takes,
	// and its columns are as wide as their widest cell, numbers aligned to the right and the rest to the left. A
	// comment behind an element ends its row. A list holding what such a row cannot spell keeps its lines.
	void CFormattingAnalyzer::fp_RenderListLayouts()
	{
		auto const &Nodes = mp_Structure.f_GetNodes();
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto nTab = mp_Request.m_Settings.m_nTabWidth;
		auto nMaxColumns = mp_Request.m_Settings.m_nMaxColumns;
		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());

		struct CCell
		{
			CStr m_Text;
			umint m_nWidth = 0;
			bool m_bNumber = false;
		};

		struct CElement
		{
			TCVector<CCell> m_Cells;
			CStr m_Comment;
			TCVector<CStr> m_Leading; // The comment lines standing above the element.
			bool m_bStartsLine = false;
		};

		auto fIsNumber = [&](umint _iFirst, umint _iLast)
			{
				umint nNumbers = 0;
				bool bSign = false;
				for (auto iToken = _iFirst; iToken <= _iLast; ++iToken)
				{
					auto const &Token = Tokens[iToken];
					if (Token.m_Kind == ECodeTokenKind::mc_Whitespace || Token.m_Kind == ECodeTokenKind::mc_Newline)
						continue;

					if (Token.m_Kind == ECodeTokenKind::mc_Number)
						++nNumbers;
					else if (!nNumbers && !bSign && (mp_Tokens.f_IsText(Token, "-") || mp_Tokens.f_IsText(Token, "+") || mp_Tokens.f_IsText(Token, "~")))
						bSign = true;
					else
						return false;
				}

				return nNumbers == 1;
			}
		;

		// A row written as the source wrote it keeps its strings continued on the lines below it.
		CStr Continuation;

		auto fCell = [&](umint _iFirst, umint _iLast, CCell &o_Cell)
			{
				if (!fp_RenderElement(_iFirst, _iLast, o_Cell.m_Text, Continuation.f_IsEmpty() ? nullptr : &Continuation))
					return false;

				o_Cell.m_bNumber = fIsNumber(_iFirst, _iLast);

				return !Continuation.f_IsEmpty() || fg_MeasureTextColumns(o_Cell.m_Text, nTab, o_Cell.m_nWidth);
			}
		;

		for (auto const &Layout : mp_ListLayouts)
		{
			auto const &List = Nodes[Layout.m_iList];
			auto iOpen = List.m_iFirstToken;
			auto iClose = List.m_iLastToken;
			if (!fp_IsFirstOnLine(iOpen) || !fp_IsFirstOnLine(iClose))
				continue;

			auto nBraceIndent = fp_GetStatementIndent(iOpen);
			auto nElementIndent = nBraceIndent + nTab;
			Continuation = Layout.m_Layout == EListLayout::mc_Rows ? Ending + fp_MakeIndent(nElementIndent) : CStr();

			// The elements between the list's commas, each with the comment behind it on its line and the comment
			// lines above it. A comment line between a comma ending its line and the element behind it would have
			// the comma move past it, and keeps the list's lines.
			TCVector<CElement> Elements;
			TCVector<CStr> Leading;
			bool bValid = true;
			bool bTrailingComma = false;
			umint iElement = iOpen + 1;

			for (umint iSplit = 0; bValid; ++iSplit)
			{
				auto iEnd = iSplit < List.m_SplitPoints.f_GetLen() ? List.m_SplitPoints[iSplit] : iClose;
				aint iFirst = -1;
				aint iLast = -1;
				CStr Comment;
				TCVector<CStr> NextLeading;

				for (auto iToken = iElement; iToken < iEnd && bValid; ++iToken)
				{
					auto const &Token = Tokens[iToken];
					auto Kind = Token.m_Kind;
					if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline)
						continue;

					if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
					{
						if (iToken == Layout.m_iDirective)
							continue;

						auto Text = fg_TrimCommentTrailingSpace(mp_Tokens.f_GetText(Token));
						bValid = !Token.m_bMultiLine;
						if (iLast < 0)
						{
							// A comment behind a comma on the element's line describes the element in front of it.
							if (!Elements.f_IsEmpty() && Elements.f_GetLast().m_Comment.f_IsEmpty() && !fp_SpansLines(iElement - 1, iToken))
								Elements.f_GetLast().m_Comment = Text;
							else
							{
								bValid = bValid && iSplit == 0 && fp_IsFirstOnLine(iToken);
								Leading.f_Insert(Text);
							}
						}
						else if (fp_SpansLines(umint(iLast), iToken))
							NextLeading.f_Insert(Text);
						else
						{
							bValid = bValid && Comment.f_IsEmpty();
							Comment = Text;
						}

						continue;
					}

					bValid = Comment.f_IsEmpty() && NextLeading.f_IsEmpty();
					if (iFirst < 0)
						iFirst = aint(iToken);

					iLast = aint(iToken);
				}

				if (!bValid)
					break;

				if (iFirst < 0)
				{
					// Nothing but the directive, or a comma ending the list, which stays behind the last element.
					bTrailingComma = iSplit && iSplit >= List.m_SplitPoints.f_GetLen() && Comment.f_IsEmpty() && Leading.f_IsEmpty();
					bValid = bTrailingComma || (iSplit == 0 && iSplit >= List.m_SplitPoints.f_GetLen() && Leading.f_IsEmpty());
					break;
				}

				auto &Element = Elements.f_Insert();
				Element.m_Comment = Comment;
				Element.m_Leading = fg_Move(Leading);
				Element.m_bStartsLine = fp_IsFirstOnLine(umint(iFirst)) || (iSplit && fp_IsFirstOnLine(iElement - 1));

				Leading = fg_Move(NextLeading);
				bValid = bValid && (iSplit < List.m_SplitPoints.f_GetLen() || Leading.f_IsEmpty());

				if (Layout.m_Layout == EListLayout::mc_Table)
				{
					// A record's fields are the cells of its row.
					auto iRecord = mp_Structure.f_FindNodeOpeningAt(umint(iFirst));
					auto const *pRecord = iRecord < Nodes.f_GetLen() ? &Nodes[iRecord] : nullptr;
					bValid = pRecord && pRecord->m_Kind == ECodeNodeKind::mc_Group && pRecord->m_Bracket == ECodeBracket::mc_Brace && pRecord->m_iLastToken == umint(iLast);
					auto const &Record = bValid ? *pRecord : Nodes[0];

					umint iField = umint(iFirst) + 1;
					for (umint iFieldSplit = 0; bValid; ++iFieldSplit)
					{
						auto iFieldEnd = iFieldSplit < Record.m_SplitPoints.f_GetLen() ? Record.m_SplitPoints[iFieldSplit] : Record.m_iLastToken;
						auto iFieldFirst = fp_NextCode(iField - 1);
						auto iFieldLast = fp_PreviousCode(iFieldEnd);
						if (iFieldFirst >= 0 && iFieldLast >= 0 && umint(iFieldFirst) < iFieldEnd && umint(iFieldLast) >= umint(iFieldFirst) && umint(iFieldFirst) > iField - 1)
							bValid = fCell(umint(iFieldFirst), umint(iFieldLast), Element.m_Cells.f_Insert());
						else
							bValid = iFieldSplit == 0 && iFieldSplit >= Record.m_SplitPoints.f_GetLen();

						if (iFieldSplit >= Record.m_SplitPoints.f_GetLen())
							break;

						iField = Record.m_SplitPoints[iFieldSplit] + 1;
					}
				}
				else
					bValid = fCell(umint(iFirst), umint(iLast), Element.m_Cells.f_Insert());

				if (iSplit >= List.m_SplitPoints.f_GetLen())
					break;

				iElement = List.m_SplitPoints[iSplit] + 1;
			}

			if (!bValid)
			{
				auto const &Directive = Tokens[Layout.m_iDirective];
				fp_AddDiagnostic("layout-directive", Directive.m_iOffset, Directive.m_nLength, "the list holds a comment or a construct its rows cannot spell, and keeps its lines", false);

				continue;
			}

			// The rows: a table's records one to each, the lines the elements were written on, and otherwise the given
			// number of elements, or as many as fit. Comment lines above an element start its row.
			bool bRows = Layout.m_Layout == EListLayout::mc_Rows;

			auto fRows = [&](umint _nColumns, TCVector<TCVector<umint>> &o_Rows)
				{
					o_Rows.f_Clear();
					for (umint iItem = 0; iItem < Elements.f_GetLen(); ++iItem)
					{
						auto const &Element = Elements[iItem];
						bool bNewRow
							= o_Rows.f_IsEmpty()
							|| !Elements[o_Rows.f_GetLast().f_GetLast()].m_Comment.f_IsEmpty()
							|| !Element.m_Leading.f_IsEmpty()
							|| (bRows ? Element.m_bStartsLine : o_Rows.f_GetLast().f_GetLen() >= _nColumns)
						;
						if (bNewRow)
							o_Rows.f_Insert();

						o_Rows.f_GetLast().f_Insert(iItem);
					}
				}
			;

			struct CColumn
			{
				umint m_nWidth = 0;
				bool m_bNumber = true;
			};

			TCVector<TCVector<umint>> Rows;
			TCVector<CColumn> Columns;

			auto fColumns = [&]
				{
					Columns.f_Clear();
					for (auto const &Row : Rows)
					{
						for (umint iCell = 0; iCell < Row.f_GetLen(); ++iCell)
						{
							auto const &Cells = Elements[Row[iCell]].m_Cells;
							for (umint iField = 0; iField < Cells.f_GetLen(); ++iField)
							{
								auto iColumn = Layout.m_Layout == EListLayout::mc_Table ? iField : iCell;
								Columns.f_SetAtLeastLen(iColumn + 1, 0);

								auto &Column = Columns[iColumn];
								Column.m_nWidth = fg_Max(Column.m_nWidth, Cells[iField].m_nWidth);
								Column.m_bNumber &= Cells[iField].m_bNumber;
							}
						}
					}
				}
			;

			// A number stands right-aligned in its column, anything else left-aligned with the padding behind its comma.
			auto fAppendCellText = [&](CStr::CAppender &o_Appender, CCell const &_Cell, CColumn const &_Column, bool _bLast)
				{
					auto fAppendPadding = [&]
						{
							for (auto nSpace = _Cell.m_nWidth; nSpace < _Column.m_nWidth; ++nSpace)
								o_Appender.f_AddChar(' ');
						}
					;

					if (!bRows && _Column.m_bNumber)
						fAppendPadding();

					o_Appender += _Cell.m_Text;
					if (_bLast)
						return;

					o_Appender += ",";
					if (!bRows && !_Column.m_bNumber)
						fAppendPadding();

					o_Appender += " ";
				}
			;

			auto fRowText = [&](TCVector<umint> const &_Row, bool _bFirst)
				{
					// Rows aligned to columns have their first indented as far as the comma takes the others.
					CStr Text = _bFirst ? (bRows ? "" : "  ") : ", ";
					{
						CStr::CAppender Appender(Text);
						for (umint iCell = 0; iCell < _Row.f_GetLen(); ++iCell)
						{
							auto const &Element = Elements[_Row[iCell]];
							bool bLastCell = iCell + 1 == _Row.f_GetLen();
							if (Layout.m_Layout == EListLayout::mc_Table)
							{
								Appender += "{";
								for (umint iField = 0; iField < Element.m_Cells.f_GetLen(); ++iField)
									fAppendCellText(Appender, Element.m_Cells[iField], Columns[iField], iField + 1 == Element.m_Cells.f_GetLen());

								Appender += "}";
							}
							else
								fAppendCellText(Appender, Element.m_Cells[0], Columns[iCell], bLastCell);

							if (bTrailingComma && _Row[iCell] + 1 == Elements.f_GetLen())
								Appender += ",";

							if (bLastCell && !Element.m_Comment.f_IsEmpty())
							{
								Appender += " ";
								Appender += Element.m_Comment;
							}
						}
					}

					// A last cell padded to its column ends in blanks where nothing follows it.
					auto pText = Text.f_GetStr();
					auto nLength = Text.f_GetLen();
					while (nLength && pText[nLength - 1] == ' ')
						--nLength;

					return Text.f_Left(nLength);
				}
			;

			auto fFits = [&]
				{
					for (umint iRow = 0; iRow < Rows.f_GetLen(); ++iRow)
					{
						umint nWidth = 0;
						if (!fg_MeasureTextColumns(fRowText(Rows[iRow], iRow == 0), nTab, nWidth) || (nMaxColumns && nElementIndent + nWidth > nMaxColumns))
							return false;
					}

					return true;
				}
			;

			if (Layout.m_Layout == EListLayout::mc_Table)
				fRows(1, Rows);
			else if (bRows)
				fRows(0, Rows);
			else if (Layout.m_nColumns)
				fRows(Layout.m_nColumns, Rows);
			else
			{
				// No row holds more elements than the narrowest of them fit in side by side, which bounds the search: a
				// table of thousands would otherwise lay out every count from all of them down.
				auto nMostColumns = Elements.f_GetLen();
				if (nMaxColumns && nMaxColumns <= nElementIndent)
					nMostColumns = 1;
				else if (nMaxColumns)
				{
					TCVector<umint> Widths;
					for (auto const &Element : Elements)
						Widths.f_Insert(Element.m_Cells.f_IsEmpty() ? 0 : Element.m_Cells[0].m_nWidth);

					Widths.f_Sort();
					umint nUsed = 2;
					umint nFit = 0;
					for (auto nWidth : Widths)
					{
						nUsed += nWidth + (nFit ? 2 : 0);
						if (nUsed > nMaxColumns - nElementIndent)
							break;

						++nFit;
					}

					nMostColumns = fg_Max(nFit, umint(1));
				}

				for (auto nColumns = nMostColumns; nColumns >= 1; --nColumns)
				{
					fRows(nColumns, Rows);
					fColumns();
					if (nColumns == 1 || fFits())
						break;
				}
			}

			fColumns();

			CStr Interior;
			{
				CStr::CAppender Appender(Interior);

				auto const &Directive = Tokens[Layout.m_iDirective];
				auto nDirective = Directive.m_nLength;
				while (nDirective && fg_IsSpaceOrTab(mp_pSource[Directive.m_iOffset + nDirective - 1]))
					--nDirective;

				Appender += Ending;
				fp_AppendIndent(Appender, nElementIndent);
				Appender.f_AddString(mp_pSource + Directive.m_iOffset, nDirective);

				for (umint iRow = 0; iRow < Rows.f_GetLen(); ++iRow)
				{
					for (auto const &Comment : Elements[Rows[iRow][0]].m_Leading)
					{
						Appender += Ending;
						fp_AppendIndent(Appender, nElementIndent);
						Appender += Comment;
					}

					Appender += Ending;
					fp_AppendIndent(Appender, nElementIndent);
					Appender += fRowText(Rows[iRow], iRow == 0);
				}

				Appender += Ending;
				fp_AppendIndent(Appender, nBraceIndent);
			}

			auto iInterior = Tokens[iOpen].f_GetEnd();
			fp_AddOwnedEdit(iInterior, Tokens[iClose].m_iOffset - iInterior, Interior, "a list its directive lays out stands in aligned rows");
		}
	}

	// An 'align: =' directive aligns the '=' of each line in the run below it, which ends at a blank line or a line
	// holding none, at one column: one space behind the longest of them.
	void CFormattingAnalyzer::fp_RenderAlignments()
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		for (auto iDirective : mp_AlignDirectives)
		{
			struct CLine
			{
				umint m_iAssign = 0;
				umint m_nColumn = 0;
			};

			TCVector<CLine> Lines;
			umint nTarget = 0;
			auto iLineEnd = iDirective;

			while (true)
			{
				// The next line holds code at its start, directly below the one before it.
				umint nNewlines = 0;
				auto iToken = iLineEnd + 1;
				while (iToken < Tokens.f_GetLen())
				{
					auto Kind = Tokens[iToken].m_Kind;
					if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
						break;

					nNewlines += Kind == ECodeTokenKind::mc_Newline;
					++iToken;
				}

				auto const *pFirst = iToken < Tokens.f_GetLen() ? &Tokens[iToken] : nullptr;
				bool bCode
					= pFirst && pFirst->m_Kind != ECodeTokenKind::mc_LineComment && pFirst->m_Kind != ECodeTokenKind::mc_BlockComment && pFirst->m_Kind != ECodeTokenKind::mc_Preprocessor
				;
				if (!bCode || nNewlines != 1 || mp_Tokens.f_IsText(*pFirst, "}"))
					break;

				auto iFirst = iToken;
				umint iAssign = 0;
				for (; iToken < Tokens.f_GetLen() && Tokens[iToken].m_Kind != ECodeTokenKind::mc_Newline; ++iToken)
				{
					if (!iAssign && mp_TokenDepth[iToken] == mp_TokenDepth[iFirst] && iToken > iFirst && fp_IsAssignment(iToken) && !fp_IsKeyAssign(iToken))
						iAssign = iToken;
				}

				iLineEnd = iToken - 1;

				auto iHeadEnd = iAssign ? fp_PreviousCode(iAssign) : aint(-1);
				umint nHead = 0;
				if (iHeadEnd < 0 || umint(iHeadEnd) < iFirst || !fp_MeasureJoinedWidth(iFirst, umint(iHeadEnd), nHead))
					break;

				auto &Line = Lines.f_Insert();
				Line.m_iAssign = iAssign;
				Line.m_nColumn = fp_GetStatementIndent(iFirst) + nHead;

				nTarget = fg_Max(nTarget, Line.m_nColumn + 1);
			}

			for (auto const &Line : Lines)
			{
				auto iHeadEnd = umint(fp_PreviousCode(Line.m_iAssign));
				auto &Owned = mp_Owned.f_Insert();
				Owned.m_iOffset = Tokens[iHeadEnd].m_iOffset;
				Owned.m_nLength = Tokens[Line.m_iAssign].f_GetEnd() - Owned.m_iOffset;

				CStr Spaces;
				Spaces.f_AddChars(' ', fg_Max(aint(nTarget) - aint(Line.m_nColumn), aint(0)));

				auto iGap = Tokens[iHeadEnd].f_GetEnd();
				fp_AddOwnedEdit(iGap, Tokens[Line.m_iAssign].m_iOffset - iGap, Spaces, "the run its directive aligns has its '=' at one column");
			}
		}
	}

	// The tokens from _iFirst to _iLast written on one line at the spacing the rules give them, which fails for a range
	// holding a comment, a directive, a token spanning lines or a gap whose spacing is not settled across a line break.
	// With _pContinuation, a string continued on the next line stays continued there, behind that line start.
	bool CFormattingAnalyzer::fp_RenderElement(umint _iFirst, umint _iLast, CStr &o_Text, CStr const *_pContinuation) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		o_Text.f_Clear();
		CStr::CAppender Appender(o_Text);
		aint iPrevious = -1;

		for (auto iToken = _iFirst; iToken <= _iLast; ++iToken)
		{
			auto const &Token = Tokens[iToken];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline)
				continue;

			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor || Kind == ECodeTokenKind::mc_LineSplice)
				return false;

			if (Token.m_bMultiLine)
				return false;

			if (iPrevious >= 0)
			{
				bool bNewline = false;
				for (auto iGap = umint(iPrevious) + 1; iGap < iToken; ++iGap)
					bNewline |= Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

				auto Spacing = fp_GetInlineSpacing(umint(iPrevious), iToken);
				if (bNewline && _pContinuation && fp_ContinuesLiteral(iToken) == iPrevious)
					Appender += *_pContinuation;
				else if (Spacing == ECodeSpacing::mc_Space)
					Appender += " ";
				else if (Spacing == ECodeSpacing::mc_Preserve)
				{
					if (bNewline)
						return false;

					auto const &Previous = Tokens[umint(iPrevious)];
					Appender.f_AddString(mp_pSource + Previous.f_GetEnd(), Token.m_iOffset - Previous.f_GetEnd());
				}
			}

			Appender.f_AddString(mp_pSource + Token.m_iOffset, Token.m_nLength);
			iPrevious = aint(iToken);
		}

		return iPrevious >= 0;
	}

	bool CFormattingAnalyzer::fp_IsSelected(umint _iOffset, umint _nLength) const
	{
		for (auto const &Range : mp_Effective)
		{
			if (_iOffset >= Range.m_iOffset && _iOffset + _nLength <= Range.f_GetEnd())
				return true;
		}

		return false;
	}

	bool CFormattingAnalyzer::fp_IsLineSelected(umint _iLine) const
	{
		auto iStart = mp_Lines.f_GetLineStart(_iLine);
		auto iEnd = mp_Lines.f_GetLineEnd(_iLine);

		for (auto const &Range : mp_Effective)
		{
			if (iStart < Range.f_GetEnd() && Range.m_iOffset < fg_Max(iEnd, iStart + 1))
				return true;
		}

		return false;
	}

	umint CFormattingAnalyzer::fp_GetColumn(umint _iOffset) const
	{
		auto iLine = mp_Lines.f_FindLine(_iOffset);
		auto iStart = mp_Lines.f_GetLineStart(iLine);
		auto nTab = mp_Request.m_Settings.m_nTabWidth;

		// Nearly every question is about the token that starts its line, whose column is
		// the line's indentation: blanks alone, counted without decoding the text, and
		// remembered per line since the layout asks for every statement's more than once.
		umint nIndent = 0;
		auto iChar = iStart;
		for (; iChar < _iOffset; ++iChar)
		{
			if (mp_pSource[iChar] == ' ')
				++nIndent;
			else if (mp_pSource[iChar] == '\t')
				nIndent = nTab ? (nIndent / nTab + 1) * nTab : nIndent;
			else
				break;
		}

		if (iChar == _iOffset)
			return nIndent + 1;

		umint nColumns = 0;
		if (!fg_MeasureTextColumns(mp_pSource + iStart, _iOffset - iStart, nTab, nColumns))
			return 1;

		return nColumns + 1;
	}

	ETextLineEnding CFormattingAnalyzer::fp_GetDefaultLineEnding() const
	{
		if (mp_Request.m_Settings.m_EndOfLine)
			return *mp_Request.m_Settings.m_EndOfLine;

		for (umint iLine = 0; iLine < mp_Lines.f_GetLineCount(); ++iLine)
		{
			if (mp_Lines.f_GetLine(iLine).m_Ending != ETextLineEnding::mc_None)
				return mp_Lines.f_GetLine(iLine).m_Ending;
		}

		return ETextLineEnding::mc_LF;
	}

	void CFormattingAnalyzer::fp_AddDiagnostic(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Explanation, bool _bHasAutomaticFix)
	{
		auto &Diagnostic = mp_Diagnostics.f_Insert();
		Diagnostic.m_Rule = _Rule;
		Diagnostic.m_Severity = _bHasAutomaticFix ? ECodeFormattingSeverity::mc_Warning : ECodeFormattingSeverity::mc_Error;
		Diagnostic.m_iOffset = _iOffset;
		Diagnostic.m_nLength = _nLength;
		Diagnostic.m_iLine = mp_Lines.f_FindLine(_iOffset) + 1;
		Diagnostic.m_iColumn = fp_GetColumn(_iOffset);
		Diagnostic.m_Explanation = _Explanation;
		Diagnostic.m_bHasAutomaticFix = _bHasAutomaticFix;
	}

	// A conversion edits two places, both of which a range has to select. Under a strict range one it selects whose other
	// place lies outside is reported, as an edit outside it is.
	bool CFormattingAnalyzer::fp_IsConversionSelected(umint _iFirst, umint _nFirst, umint _iSecond, umint _nSecond)
	{
		bool bFirst = fp_IsSelected(_iFirst, _nFirst);
		bool bSecond = fp_IsSelected(_iSecond, _nSecond);
		if (bFirst && bSecond)
			return true;

		auto fTouches = [&](umint _iOffset, umint _nLength)
			{
				for (auto const &Range : mp_Effective)
				{
					if (_iOffset < Range.f_GetEnd() && Range.m_iOffset < fg_Max(_iOffset + _nLength, _iOffset + 1))
						return true;
				}

				return false;
			}
		;

		if (mp_Request.m_RangePolicy == ECodeRangePolicy::mc_Strict && (fTouches(_iFirst, _nFirst) || fTouches(_iSecond, _nSecond)))
		{
			auto iOutside = bFirst ? _iSecond : _iFirst;
			auto nOutside = bFirst ? _nSecond : _nFirst;
			fp_AddDiagnostic("range-boundary", iOutside, nOutside, "A conversion requires an edit outside the requested range; no edit was applied to this unit", false);
		}

		return false;
	}

	// An edit outside a strict range is the selection's to report only where the statement it stands in, up to the block around
	// it, reaches into the range; one in a statement the range leaves alone is no business of it. Without a structure every
	// edit is.
	bool CFormattingAnalyzer::fp_IsInSelectedStatement(umint _iOffset) const
	{
		auto const &Nodes = mp_Structure.f_GetNodes();
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iToken = mp_Tokens.f_FindToken(_iOffset);

		// A gap belongs to the statement it leads up to, whose indentation it is, not to the block around both.
		while (iToken < Tokens.f_GetLen() && !fg_IsCodeToken(Tokens[iToken]))
			++iToken;

		if (Nodes.f_GetLen() <= 1 || iToken >= Tokens.f_GetLen())
			return true;

		// The enclosing node holds the token strictly inside, so a statement the token starts is one of its children.
		auto iInnermost = mp_Structure.f_FindEnclosingNode(iToken);
		if (iInnermost < Nodes.f_GetLen())
		{
			for (auto iChild : Nodes[iInnermost].m_Children)
			{
				if (Nodes[iChild].m_iFirstToken <= iToken && iToken <= Nodes[iChild].m_iLastToken)
				{
					iInnermost = iChild;

					break;
				}
			}
		}

		umint iStatement = 0;
		for (auto iNode = iInnermost; iNode && iNode < Nodes.f_GetLen(); iNode = Nodes[iNode].m_iParent)
		{
			if (Nodes[iNode].m_Kind == ECodeNodeKind::mc_Block && iStatement)
				break;

			if (Nodes[iNode].m_Kind == ECodeNodeKind::mc_Statement)
				iStatement = iNode;
		}

		if (!iStatement)
			return true;

		// The gap leading up to the statement is the statement's.
		auto iStart = fg_Min(_iOffset, Tokens[Nodes[iStatement].m_iFirstToken].m_iOffset);
		auto iEnd = Tokens[Nodes[iStatement].m_iLastToken].f_GetEnd();
		for (auto const &Range : mp_Effective)
		{
			if (Range.m_iOffset < iEnd && iStart < fg_Max(Range.f_GetEnd(), Range.m_iOffset + 1))
				return true;
		}

		return false;
	}

	void CFormattingAnalyzer::fp_AddEdit(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Replacement, CStr const &_Explanation)
	{
		if (fp_IsDisabled(_iOffset, _nLength))
			return;

		if (!fp_IsSelected(_iOffset, _nLength))
		{
			if (mp_Request.m_RangePolicy == ECodeRangePolicy::mc_Strict && fp_IsInSelectedStatement(_iOffset))
				fp_AddDiagnostic("range-boundary", _iOffset, _nLength, "{} requires an edit outside the requested range; no edit was applied to this unit"_f << _Rule, false);

			return;
		}

		auto &Edit = mp_Edits.f_Insert();
		Edit.m_iOffset = _iOffset;
		Edit.m_nLength = _nLength;
		Edit.m_Replacement = _Replacement;
		Edit.m_Rule = _Rule;
		mp_EditExplanations.f_Insert(_Explanation);
		mp_EditDropped.f_Insert(uint8(0));
	}

	CCodeFormattingResult CFormattingAnalyzer::f_Analyze(bool _bVerify)
	{
		CCodeFormattingResult Result;
		auto const &Settings = mp_Request.m_Settings;

		auto fUnsupported = [&](CStr const &_Explanation)
			{
				Result.m_Status = ECodeFormattingStatus::mc_Unsupported;
				Result.m_Explanation = _Explanation;
				Result.m_Edits.f_Clear();

				return Result;
			}
		;

		auto fFailed = [&](CStr const &_Explanation)
			{
				Result.m_Status = ECodeFormattingStatus::mc_Failed;
				Result.m_Explanation = _Explanation;
				Result.m_Edits.f_Clear();

				return Result;
			}
		;

		if (!Settings.f_IsFormattingEnabled())
			return fUnsupported("Malterlib formatting is not enabled for this file");

		auto Language = Settings.f_GetLanguage();
		bool bBuildSystem = Language == ECodeLanguage::mc_BuildSystem;

		// The layout steps a level by a tab's width, which spaces only follow where an indentation level is as wide.
		if (!Settings.m_bIndentWithTabs && Settings.m_nIndentSize != Settings.m_nTabWidth)
			return fUnsupported("indent_size = {} differs from tab_width = {}, which indenting with spaces does not support"_f << Settings.m_nIndentSize << Settings.m_nTabWidth);

		if (Settings.m_Charset && Settings.m_Charset != "utf-8" && Settings.m_Charset != "utf-8-bom")
			return fUnsupported("charset = {} is not a supported formatting encoding"_f << Settings.m_Charset);

		if (!fg_IsValidUtf8(mp_Request.m_Source))
			return fUnsupported("The source is not valid UTF-8 and is left untouched");

		if (!mp_Tokens.f_IsComplete())
			return fUnsupported("The source ends inside a comment or literal");

		for (auto const &Token : mp_Tokens.f_GetTokens())
		{
			if (Token.m_Kind == ECodeTokenKind::mc_Unknown)
				return fUnsupported("The source contains a byte that cannot start a token");

			bool bDigraph
				= !bBuildSystem
				&& Token.m_Kind == ECodeTokenKind::mc_Punctuator
				&& (Token.m_nLength == 2 || Token.m_nLength == 4)
				&& fg_IsDigraph(mp_pSource + Token.m_iOffset, Token.m_nLength)
			;
			if (bDigraph)
				return fUnsupported("The source spells a punctuator with a digraph, which is left untouched");

			// A universal character name is part of the identifier it stands in, which the lexer ends at its backslash: '\\u00E9',
			// '\\U000000E9' and a named one, '\\N{LATIN SMALL LETTER E WITH ACUTE}'.
			bool bUniversalName
				= !bBuildSystem
				&& Token.m_Kind == ECodeTokenKind::mc_Punctuator
				&& mp_Tokens.f_IsText(Token, "\\")
				&& Token.f_GetEnd() < mp_Request.m_Source.f_GetLen()
				&& (mp_pSource[Token.f_GetEnd()] == 'u' || mp_pSource[Token.f_GetEnd()] == 'U' || mp_pSource[Token.f_GetEnd()] == 'N')
			;
			if (bUniversalName)
				return fUnsupported("The source spells a name with a universal character name, which is left untouched");

			// A trigraph is replaced before tokens are read, which the lexer, reading its characters as punctuators, does not.
			bool bTrigraph
				= !bBuildSystem
				&& mp_Tokens.f_IsText(Token, "?")
				&& Token.f_GetEnd() + 1 < mp_Request.m_Source.f_GetLen()
				&& mp_pSource[Token.f_GetEnd()] == '?'
				&& NStr::CStr("=/'()!<>-").f_FindChar(mp_pSource[Token.f_GetEnd() + 1]) >= 0
			;
			if (bTrigraph)
				return fUnsupported("The source spells a punctuator with a trigraph, which is left untouched");

			// Where trigraphs are read, a line comment ending in '??/' goes on onto the next line, which the lexer ends it before.
			if (!bBuildSystem && Token.m_Kind == ECodeTokenKind::mc_LineComment)
			{
				auto nComment = Token.m_nLength;
				while (nComment && (mp_pSource[Token.m_iOffset + nComment - 1] == ' ' || mp_pSource[Token.m_iOffset + nComment - 1] == '\t'))
					--nComment;

				auto pEnd = mp_pSource + Token.m_iOffset + nComment;
				if (nComment >= 3 && pEnd[-3] == '?' && pEnd[-2] == '?' && pEnd[-1] == '/')
					return fUnsupported("A line comment ends in the trigraph of a line splice, which is left untouched");
			}

			// So does a directive's line, which a '??/' at its end continues onto the next: '#define DBody ??/' over 'f();'.
			if (!bBuildSystem && Token.m_Kind == ECodeTokenKind::mc_Preprocessor && NStr::fg_StrFindLengthAware<false>(mp_pSource + Token.m_iOffset, Token.m_nLength, "?" "?/") >= 0)
				return fUnsupported("A directive holds the trigraph of a line splice, which is left untouched");

			// A splice inside a directive can join a '/' to the '*' or '/' opening a comment, which the lexer, reading the
			// directive's lines apart, does not see: '#define D /\' over '* comment */'.
			if (!bBuildSystem && Token.m_Kind == ECodeTokenKind::mc_Preprocessor)
			{
				auto pText = mp_pSource + Token.m_iOffset;
				auto nText = Token.m_nLength;
				for (umint iChar = 0; iChar + 1 < nText; ++iChar)
				{
					if (pText[iChar] != '/' || pText[iChar + 1] != '\\')
						continue;

					auto iNext = iChar + 1;
					while (iNext < nText && pText[iNext] == '\\')
					{
						auto iBreak = iNext + 1;
						while (iBreak < nText && (pText[iBreak] == ' ' || pText[iBreak] == '\t'))
							++iBreak;

						if (iBreak >= nText || (pText[iBreak] != '\n' && pText[iBreak] != '\r'))
							break;

						iNext = iBreak + (pText[iBreak] == '\r' && iBreak + 1 < nText && pText[iBreak + 1] == '\n' ? 2 : 1);
					}

					if (iNext < nText && (pText[iNext] == '*' || pText[iNext] == '/'))
						return fUnsupported("A directive opens a comment across a line splice, which is left untouched");
				}
			}

			// The header a header unit import names is one token, whose spaces would change it: 'import <vector>;'. Where 'import' is
			// a name its '<' compares, which no '>' and terminator behind it can follow: 'if (import < 3)'. What stands in front
			// tells neither apart, as a macro can end the declaration before it: 'DDeclare()' over 'import <vector>;'.
			bool bImportStarts = !bBuildSystem && Token.m_Kind == ECodeTokenKind::mc_Identifier && mp_Tokens.f_IsText(Token, "import");

			if (bImportStarts)
			{
				auto iNext = &Token - mp_Tokens.f_GetTokens().f_GetArray() + 1;
				while (iNext < aint(mp_Tokens.f_GetTokens().f_GetLen()) && !fg_IsCodeToken(mp_Tokens.f_GetTokens()[iNext]))
					++iNext;

				// A header name ends on its line, and the declaration behind it, where an expression goes on: 'import < 3 ? a : b;'.
				// Its bytes are read as written, since the name is one token whatever it holds: '<sys//types.h>' is no comment.
				auto const &AllTokens = mp_Tokens.f_GetTokens();
				bool bHeaderUnit = false;
				if (iNext < aint(AllTokens.f_GetLen()) && mp_Tokens.f_IsText(AllTokens[iNext], "<"))
				{
					auto nSource = mp_Request.m_Source.f_GetLen();
					auto iClose = AllTokens[iNext].f_GetEnd();
					while (iClose < nSource && mp_pSource[iClose] != '>' && mp_pSource[iClose] != '\n' && mp_pSource[iClose] != '\r')
						++iClose;

					// Blanks and comments can stand before the terminator: 'import <a.h> /* why */;'.
					auto iAfter = iClose + 1;
					while (iAfter < nSource)
					{
						if (mp_pSource[iAfter] == ' ' || mp_pSource[iAfter] == '\t' || mp_pSource[iAfter] == '\n' || mp_pSource[iAfter] == '\r')
							++iAfter;
						else if (mp_pSource[iAfter] == '/' && iAfter + 1 < nSource && mp_pSource[iAfter + 1] == '*')
						{
							auto iEnd = NStr::fg_StrFindLengthAware<false>(mp_pSource + iAfter + 2, nSource - iAfter - 2, "*/");
							iAfter = iEnd < 0 ? nSource : iAfter + 2 + umint(iEnd) + 2;
						}
						else if (mp_pSource[iAfter] == '/' && iAfter + 1 < nSource && mp_pSource[iAfter + 1] == '/')
						{
							while (iAfter < nSource && mp_pSource[iAfter] != '\n' && mp_pSource[iAfter] != '\r')
								++iAfter;
						}
						else if (mp_pSource[iAfter] == '\\')
						{
							// A line splice, whose line break the blanks above go past.
							auto iBreak = iAfter + 1;
							while (iBreak < nSource && (mp_pSource[iBreak] == ' ' || mp_pSource[iBreak] == '\t'))
								++iBreak;

							if (iBreak >= nSource || (mp_pSource[iBreak] != '\n' && mp_pSource[iBreak] != '\r'))
								break;

							iAfter = iBreak;
						}
						else
							break;
					}

					// The terminator can stand in the branches of a conditional behind it: 'import <vector>' over '#if A'.
					bool bTerminated = iAfter >= nSource || mp_pSource[iAfter] == ';' || mp_pSource[iAfter] == '[' || mp_pSource[iAfter] == '#';
					bHeaderUnit = iClose < nSource && mp_pSource[iClose] == '>' && bTerminated;
				}

				if (bHeaderUnit)
					return fUnsupported("The source imports a header unit, whose header name is left untouched");
			}

			// Splicing comes before tokens are read, so text a splice joins with no blank on either side reads as one token,
			// '1e\' over '+2', or opens a comment, '/\' over '/', which the lexer, reading the lines apart, does not see.
			if (!bBuildSystem && Token.m_Kind == ECodeTokenKind::mc_LineSplice && fg_SpliceJoinsText(mp_pSource, mp_Request.m_Source.f_GetLen(), Token))
				return fUnsupported("The source joins text across a line splice, which is left untouched");

			// So does a comment's end, '*\\' over '/', which the lexer reads past.
			if (!bBuildSystem && Token.m_Kind == ECodeTokenKind::mc_BlockComment)
			{
				auto pText = mp_pSource + Token.m_iOffset;
				// Any number of splices can stand between the two: '*\\' over '\\' over '/'.
				auto fSkipSplices = [&](umint _iFrom)
					{
						auto iAt = _iFrom;
						// Where trigraphs are read, '?' '?/' is a backslash too.
						while (iAt < Token.m_nLength && (pText[iAt] == '\\' || (iAt + 2 < Token.m_nLength && pText[iAt] == '?' && pText[iAt + 1] == '?' && pText[iAt + 2] == '/')))
						{
							auto iAfter = iAt + (pText[iAt] == '\\' ? 1 : 3);
							while (iAfter < Token.m_nLength && (pText[iAfter] == ' ' || pText[iAfter] == '\t' || pText[iAfter] == '\v' || pText[iAfter] == '\f'))
								++iAfter;

							if (iAfter < Token.m_nLength && pText[iAfter] == '\r')
								++iAfter;

							if (iAfter < Token.m_nLength && pText[iAfter] == '\n')
								++iAfter;

							if (pText[iAfter - 1] != '\n' && pText[iAfter - 1] != '\r')
								break;

							iAt = iAfter;
						}

						return iAt;
					}
				;

				for (umint iChar = 0; iChar + 1 < Token.m_nLength; ++iChar)
				{
					if (pText[iChar] != '*' || (pText[iChar + 1] != '\\' && pText[iChar + 1] != '?'))
						continue;

					auto iAfter = fSkipSplices(iChar + 1);
					if (iAfter > iChar + 1 && iAfter < Token.m_nLength && pText[iAfter] == '/')
						return fUnsupported("The source ends a comment across a line splice, which is left untouched");
				}
			}
		}

		fp_PrepareLineProtection();

		CStr Explanation;
		if (bBuildSystem)
		{
			if (!fp_PrepareBuildSystemLines(Explanation))
				return fUnsupported(Explanation);
		}
		else
		{
			fp_PrepareTokenDepth();
			fp_PrepareDirectives();
		}

		if (!bBuildSystem && !fp_CheckUnbracedAssembly(Explanation))
			return fUnsupported(Explanation);

		if (!fp_CollectDisabledRegions(Explanation))
			return fFailed(Explanation);

		if (!bBuildSystem && !fp_CollectListLayouts(Explanation))
			return fFailed(Explanation);

		if (!fp_ResolveRanges(Explanation))
			return fFailed(Explanation);

		// Moving a return type behind its parameter list, and dropping the braces around a
		// single guarded statement, change tokens, which no other rule does. Those
		// conversions are decided first, and the layout is then made on the converted
		// source, so the lines it settles on are the lines a later pass sees.
		// A qualifier in front of its type moves behind it before anything else is decided,
		// and a terminator that ends nothing goes. The other conversions rewrite text a
		// qualifier can stand in, a return type above all, and count the statements a
		// block holds, so they are made on the source this stage leaves, in one of their own.
		mp_Baseline = mp_Request.m_Source;

		if (mp_bAllowQualifiers && !bBuildSystem)
		{
			fp_ConvertQualifiers();
			fp_ConvertSpecifiers();
			fp_ConvertEmptyStatements();
			fp_ConvertEnumCommas();
			fp_ConvertTrailingComments();
			fp_ConvertCommentPrefixes();
		}

		if (mp_bAllowQualifiers)
			fp_ConvertBlockCommentLines();

		bool bQualifierStage = !mp_Structural.f_IsEmpty();
		if (!bQualifierStage && mp_bAllowConversions && !bBuildSystem)
		{
			mp_bProbing = true;
			fp_RuleLineBreaks();
			mp_bProbing = false;
		}

		if (!mp_Structural.f_IsEmpty())
		{
			// A closing brace added behind a chain whose own last block loses its braces is inserted where that block's
			// closing brace and the gap behind it are taken out, behind them, and the layout places it from there.
			for (auto &Insert : mp_Structural)
			{
				if (Insert.m_nLength || Insert.m_Rule != "braces")
					continue;

				for (auto const &Remove : mp_Structural)
				{
					if (Remove.m_nLength && Remove.m_Rule == "braces" && Remove.m_iOffset < Insert.m_iOffset && Insert.m_iOffset < Remove.f_GetEnd())
						Insert.m_iOffset = Remove.f_GetEnd();
				}
			}

			mp_Structural.f_Sort
				(
					[](CCodeFormattingEdit const &_Left, CCodeFormattingEdit const &_Right)
					{
						// The closing braces of nested guards are inserted at one offset, and the inner one, the deeper
						// indented, closes first: 'if (a)' over 'if (b)' over 'g();' gets '    }' before '  }'.
						if (_Left.m_iOffset == _Right.m_iOffset && !_Left.m_nLength && !_Right.m_nLength && _Left.m_Rule == "braces" && _Right.m_Rule == "braces")
							return _Right.m_Replacement.f_GetLen() <=> _Left.m_Replacement.f_GetLen();

						// An insertion goes in front of the text replaced where it is made: a comment moved above a line in
						// front of the qualifier moved out of that line's start.
						if (_Left.m_iOffset == _Right.m_iOffset)
							return _Left.m_nLength <=> _Right.m_nLength;

						return _Left.m_iOffset <=> _Right.m_iOffset;
					}
				)
			;

			auto ConvertedSource = fg_ApplyCodeFormattingEdits(mp_Request.m_Source, mp_Structural);

			// Moving a qualifier changes nothing but where it stands, which is checked here
			// since the result is only ever compared against the converted source.
			if (bQualifierStage && fg_GetReorderInvariant(mp_Request.m_Source, Language) != fg_GetReorderInvariant(ConvertedSource, Language))
				return fFailed("Reordering qualifiers and specifiers would change more than where they stand; no edits were produced");

			// No conversion moves or rewrites a directive, and the result is only ever compared against the converted source.
			if (!fg_HasEquivalentDirectives(mp_Request.m_Source, ConvertedSource, Language))
				return fFailed("A conversion would change a preprocessor directive; no edits were produced");

			CCodeFormattingRequest Nested = mp_Request;
			Nested.m_Source = ConvertedSource;

			// An expanded selection is carried over expanded: the conversion can move its units apart over lines.
			if (mp_Request.m_RangePolicy == ECodeRangePolicy::mc_Expand && !Nested.m_Ranges.f_IsEmpty())
				Nested.m_Ranges = mp_Effective;

			// A selection the conversions took all of is no cursor, which the inner pass would expand onto the line behind it:
			// it selects nothing, and where every selection is gone, nothing is laid out.
			TCVector<CCodeFormattingRange> Mapped;
			bool bCollapsed = false;
			for (auto &Range : Nested.m_Ranges)
			{
				auto iStart = fg_MapOffsetToConverted(mp_Structural, Range.m_iOffset, false);
				auto iEnd = fg_MapOffsetToConverted(mp_Structural, Range.f_GetEnd(), true);
				if (Range.m_nLength && iEnd <= iStart)
				{
					bCollapsed = true;

					continue;
				}

				auto &Converted = Mapped.f_Insert();
				Converted.m_iOffset = iStart;
				Converted.m_nLength = iEnd - iStart;
			}

			if (bCollapsed && Mapped.f_IsEmpty())
			{
				auto &Empty = Mapped.f_Insert();
				Empty.m_iOffset = 0;
				Empty.m_nLength = 0;
				Nested.m_RangePolicy = ECodeRangePolicy::mc_Strict;
			}

			if (!Nested.m_Ranges.f_IsEmpty())
				Nested.m_Ranges = fg_Move(Mapped);

			CFormattingAnalyzer Inner(Nested, bQualifierStage && mp_bAllowConversions, false);
			auto Converted = Inner.f_Analyze(false);
			if (Converted.m_Status != ECodeFormattingStatus::mc_Complete)
				return fFailed("Laying out the converted source failed: {}"_f << Converted.m_Explanation);

			mp_Baseline = Inner.mp_Baseline;

			if (!fg_ComposeEdits(mp_Structural, ConvertedSource, Converted.m_Edits, Result.m_Edits, Explanation))
				return fFailed(Explanation);

			for (auto const &Edit : mp_Structural)
			{
				CStr Explanation = "the return type moves behind the parameter list";
				if (Edit.m_Rule == "braces")
					Explanation = "a guarded statement on one line stands without braces, one across lines within them";
				else if (Edit.m_Rule == "east-qualifier")
					Explanation = "a qualifier stands behind the type it qualifies";
				else if (Edit.m_Rule == "specifier-order")
					Explanation = "'static' stands in front of 'constexpr'";
				else if (Edit.m_Rule == "empty-statement")
					Explanation = "a terminator that ends nothing is taken out";
				else if (Edit.m_Rule == "enum-comma")
					Explanation = "an enumerator's comma stands in front of it, and the last one has none";
				else if (Edit.m_Rule == "list-comma")
					Explanation = "a comment describes the element in front of it, which the comma behind it goes past, and no comma ends a braced list";
				else if (Edit.m_Rule == "trailing-comment")
					Explanation = "a comment trailing a line is a line comment, on a line of its own above it where it makes the line too long";
				else if (Edit.m_Rule == "comment-space")
					Explanation = "the blanks every line of a comment starts with behind its markers are one space";

				fp_AddDiagnostic(Edit.m_Rule, Edit.m_iOffset, Edit.m_nLength, Explanation, true);
			}

			COffsetToOriginal ToOriginal{mp_Structural};
			COffsetToOriginal EndToOriginal{mp_Structural};
			for (auto Diagnostic : Converted.m_Diagnostics)
			{
				auto iEnd = fg_MapOffsetToOriginal(EndToOriginal, Diagnostic.m_iOffset + Diagnostic.m_nLength, true);
				Diagnostic.m_iOffset = fg_MapOffsetToOriginal(ToOriginal, Diagnostic.m_iOffset);
				Diagnostic.m_nLength = fg_Max(iEnd, Diagnostic.m_iOffset) - Diagnostic.m_iOffset;
				Diagnostic.m_iLine = mp_Lines.f_FindLine(Diagnostic.m_iOffset) + 1;
				Diagnostic.m_iColumn = fp_GetColumn(Diagnostic.m_iOffset);
				mp_Diagnostics.f_Insert(Diagnostic);
			}
		}
		else
		{
			fp_RuleTrailingWhitespace();
			fp_RuleLineEndings();
			fp_RuleFinalNewline();

			if (bBuildSystem)
			{
				fp_RuleBuildSystemLineBreaks();
				fp_RuleBuildSystemBlankLines();
				fp_RuleBuildSystemIndentation();
			}
			else
			{
				fp_RuleBlankLines();
				fp_RuleLineBreaks();
				fp_RenderListLayouts();
				fp_RenderAlignments();
				fp_RuleTokenSpacing();
				fp_KeepSuppressedLines();
				fp_EmitLayout();

				// The layout writes the indentation of every line it places, so what this rule
				// is left to spell is only the lines it did not.
				fp_RuleIndentation();
				fp_RuleDirectiveIndentation();

				fp_LimitJoinedLines();
				fp_AlignContinuations();
				fp_AlignListColumns();
			}

			NContainer::TCVector<umint> Sources;
			fp_BuildPlan(Result.m_Edits, Sources);

			for (auto iEdit : Sources)
			{
				auto const &Edit = mp_Edits[iEdit];
				auto &Diagnostic = mp_Diagnostics.f_Insert();
				Diagnostic.m_Rule = Edit.m_Rule;
				Diagnostic.m_Severity = ECodeFormattingSeverity::mc_Warning;
				Diagnostic.m_iOffset = Edit.m_iOffset;
				Diagnostic.m_nLength = Edit.m_nLength;
				Diagnostic.m_iLine = mp_Lines.f_FindLine(Edit.m_iOffset) + 1;
				Diagnostic.m_iColumn = fp_GetColumn(Edit.m_iOffset);
				Diagnostic.m_Explanation = mp_EditExplanations[iEdit];
				Diagnostic.m_bHasAutomaticFix = true;
			}

			fp_DiagnoseLineLength(Result.m_Edits);
		}

		for (auto const &Diagnostic : mp_Diagnostics)
			Result.m_Diagnostics.f_Insert(Diagnostic);

		Result.m_Diagnostics.f_Sort
			(
				[](CCodeFormattingDiagnostic const &_Left, CCodeFormattingDiagnostic const &_Right)
				{
					if (_Left.m_iOffset != _Right.m_iOffset)
						return _Left.m_iOffset <=> _Right.m_iOffset;

					return _Left.m_Rule <=> _Right.m_Rule;
				}
			)
		;

		Result.m_EffectiveRanges = mp_Effective;
		Result.m_Status = ECodeFormattingStatus::mc_Complete;

		if (!_bVerify)
			return Result;

		// A pass that changes nothing is its own proof: the source it would be analyzed
		// against again is the very source it was analyzed on, and the analysis is a
		// function of that source alone.
		if (!Result.f_HasEdits() && mp_Structural.f_IsEmpty())
			return Result;

		// Every rule but the conversion leaves the token stream alone, so the result has to
		// match the converted source token for token.
		auto Formatted = fg_ApplyCodeFormattingEdits(mp_Request.m_Source, Result.m_Edits);
		if (!fg_HasEquivalentCodeTokens(mp_Baseline, Formatted, Language))
			return fFailed("Formatting would change the token stream ({}); no edits were produced"_f << fg_DescribeCodeTokenDifference(mp_Baseline, Formatted, Language));

		// A pass can leave work for the next, where a decision depends on a layout the pass itself made: the formatted source is
		// analyzed again until it is stable, each pass's edits composed with the plan, so the plan takes the original straight
		// to the stable source. Only a file still changing after the last pass is refused. Ranges are analyzed again where the
		// plan has moved them to, and a further pass reaching out of them leaves the plan as it is.
		{
			constexpr umint c_nMaxPasses = 4;
			auto Plan = Result.m_Edits;
			for (umint iPass = 0;; ++iPass)
			{
				CCodeFormattingRequest Nested = mp_Request;
				Nested.m_Source = Formatted;
				if (!mp_bWholeFile)
				{
					Nested.m_Ranges.f_Clear();
					for (auto const &Range : mp_Effective)
					{
						auto &Mapped = Nested.m_Ranges.f_Insert();
						Mapped.m_iOffset = fg_MapOffsetToConverted(Plan, Range.m_iOffset, false);
						Mapped.m_nLength = fg_MapOffsetToConverted(Plan, Range.f_GetEnd(), true) - Mapped.m_iOffset;
					}
				}

				CFormattingAnalyzer Analyzer(Nested);
				auto Next = Analyzer.f_Analyze(false);
				if (Next.m_Status != ECodeFormattingStatus::mc_Complete)
					return fFailed("Formatting did not reach a stable result: {}"_f << Next.m_Explanation);

				if (!Next.f_HasEdits())
				{
					// The diagnostics left without a fix describe the layout the passes ended on, which the stable pass alone
					// measured.
					if (iPass)
						f_ReplaceResidualDiagnostics(Plan, Next.m_Diagnostics, Result.m_Diagnostics);

					break;
				}

				if (iPass + 1 == c_nMaxPasses)
				{
					CTextLineMap FormattedLines(Formatted);
					auto const &Edit = Next.m_Edits[0];
					auto iLine = FormattedLines.f_FindLine(Edit.m_iOffset);
					auto iStart = FormattedLines.f_GetLineStart(iLine);
					CStr Line(Formatted.f_GetStr() + iStart, FormattedLines.f_GetLineContentEnd(iLine) - iStart);

					// The line itself is what says which construct kept changing.
					return fFailed("Formatting did not reach a stable result: {} would still change formatted line {}: {}"_f << Edit.m_Rule << iLine + 1 << Line);
				}

				auto Further = fg_ApplyCodeFormattingEdits(Formatted, Next.m_Edits);
				if (!fg_HasEquivalentCodeTokens(Analyzer.mp_Baseline, Further, Language))
					return fFailed("Formatting would change the token stream ({}); no edits were produced"_f << fg_DescribeCodeTokenDifference(Analyzer.mp_Baseline, Further, Language));

				TCVector<CCodeFormattingEdit> Composed;
				CStr Explanation;
				if (!fg_ComposeEdits(Plan, Formatted, Next.m_Edits, Composed, Explanation))
					return fFailed(Explanation);

				if (fg_ApplyCodeFormattingEdits(mp_Request.m_Source, Composed) != Further)
					return fFailed("Formatting did not reach a stable result: the passes' edits do not compose");

				bool bInRanges = true;
				for (auto const &Edit : Composed)
				{
					bool bInRange = mp_bWholeFile;
					for (auto const &Range : mp_Effective)
						bInRange = bInRange || (Range.m_iOffset <= Edit.m_iOffset && Edit.f_GetEnd() <= Range.f_GetEnd());

					bInRanges = bInRanges && bInRange;
				}

				if (!bInRanges)
					break;

				// The pass's edits join the plan, and so do the diagnostics that report them.
				f_AddPassDiagnostics(Plan, Next.m_Diagnostics, true, Result.m_Diagnostics);
				Plan = fg_Move(Composed);
				Formatted = fg_Move(Further);
			}

			Result.m_Edits = fg_Move(Plan);
		}

		return Result;
	}

	// Maps the diagnostics a pass made on the source the plan formats this one to back onto this one, those with a fix or
	// those without, and adds them to o_Diagnostics, kept sorted.
	void CFormattingAnalyzer::f_AddPassDiagnostics
		(
			TCVector<CCodeFormattingEdit> const &_Plan
			, TCVector<CCodeFormattingDiagnostic> const &_Pass
			, bool _bFixable
			, TCVector<CCodeFormattingDiagnostic> &o_Diagnostics
		)
		const
	{
		COffsetToOriginal ToOriginal{_Plan};
		COffsetToOriginal EndToOriginal{_Plan};
		for (auto Diagnostic : _Pass)
		{
			if (Diagnostic.m_bHasAutomaticFix != _bFixable)
				continue;

			auto iEnd = fg_MapOffsetToOriginal(EndToOriginal, Diagnostic.m_iOffset + Diagnostic.m_nLength, true);
			Diagnostic.m_iOffset = fg_MapOffsetToOriginal(ToOriginal, Diagnostic.m_iOffset);
			Diagnostic.m_nLength = fg_Max(iEnd, Diagnostic.m_iOffset) - Diagnostic.m_iOffset;
			Diagnostic.m_iLine = mp_Lines.f_FindLine(Diagnostic.m_iOffset) + 1;
			Diagnostic.m_iColumn = fp_GetColumn(Diagnostic.m_iOffset);
			o_Diagnostics.f_Insert(Diagnostic);
		}

		o_Diagnostics.f_Sort
			(
				[](CCodeFormattingDiagnostic const &_Left, CCodeFormattingDiagnostic const &_Right)
				{
					if (_Left.m_iOffset != _Right.m_iOffset)
						return _Left.m_iOffset <=> _Right.m_iOffset;

					return _Left.m_Rule <=> _Right.m_Rule;
				}
			)
		;
	}

	// The diagnostics without a fix are replaced with those of the stable source the plan formats this one to: what is left
	// is measured on the layout the passes end on.
	void CFormattingAnalyzer::f_ReplaceResidualDiagnostics
		(
			TCVector<CCodeFormattingEdit> const &_Plan
			, TCVector<CCodeFormattingDiagnostic> const &_Residual
			, TCVector<CCodeFormattingDiagnostic> &o_Diagnostics
		)
		const
	{
		TCVector<CCodeFormattingDiagnostic> Diagnostics;
		for (auto const &Diagnostic : o_Diagnostics)
		{
			if (Diagnostic.m_bHasAutomaticFix)
				Diagnostics.f_Insert(Diagnostic);
		}

		f_AddPassDiagnostics(_Plan, _Residual, false, Diagnostics);
		o_Diagnostics = fg_Move(Diagnostics);
	}

	// A tab inside a token, in a literal or a comment, reaches a tab stop that depends on where the token starts, which the
	// token alone does not say: each one adds a whole tab to the width measured from column zero, more than it can take, so
	// that a line is never taken to fit when it does not.
	umint CFormattingAnalyzer::fp_GetTokenColumns(CCodeToken const &_Token) const
	{
		auto fMeasure = [&]() -> umint
			{
				auto nTab = mp_Request.m_Settings.m_nTabWidth;
				umint nColumns = 0;
				if (!fg_MeasureTextColumns(mp_pSource + _Token.m_iOffset, _Token.m_nLength, nTab, nColumns))
					return TCLimitsInt<umint>::mc_Max;

				if (_Token.m_Kind != ECodeTokenKind::mc_Whitespace)
				{
					for (umint iChar = 0; iChar < _Token.m_nLength; ++iChar)
						nColumns += mp_pSource[_Token.m_iOffset + iChar] == '\t' ? nTab : 0;
				}

				return nColumns;
			}
		;

		// A token of this source is measured once; the width of its text never changes.
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto pFirst = Tokens.f_GetArray();
		if (&_Token >= pFirst && &_Token < pFirst + Tokens.f_GetLen())
		{
			umint iToken = umint(&_Token - pFirst);

			if (mp_TokenColumns.f_GetLen() != Tokens.f_GetLen())
			{
				mp_TokenColumns.f_SetLen(Tokens.f_GetLen());
				for (auto &Value : mp_TokenColumns)
					Value = 0;
			}

			if (!mp_TokenColumns[iToken])
			{
				auto nColumns = fMeasure();
				mp_TokenColumns[iToken] = nColumns < TCLimitsInt<umint>::mc_Max ? nColumns + 1 : TCLimitsInt<umint>::mc_Max;
			}

			auto nStoredColumns = mp_TokenColumns[iToken];
			return nStoredColumns == TCLimitsInt<umint>::mc_Max ? TCLimitsInt<umint>::mc_Max : nStoredColumns - 1;
		}

		return fMeasure();
	}

	// Measures the construct as a single line, gap by gap: a gap the source writes on one
	// line keeps its width, since only a gap holding a line break is ever rewritten, and one
	// holding a line break is measured at the width joining it writes. Returns false when a
	// gap has no inline spelling: a line break the standard does not settle, a comment, or
	// a directive. With _bBlockComments, a block comment on one line is measured as written, and so are the gaps beside it,
	// which no spacing rule writes into.
	bool CFormattingAnalyzer::fp_MeasureJoinedWidth(umint _iFirstToken, umint _iLastToken, umint &o_nColumns, bool _bBlockComments) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		umint nColumns = 0;
		umint iPrevious = _iFirstToken;

		for (umint iToken = _iFirstToken; iToken <= _iLastToken; ++iToken)
		{
			// A block never fits on a line: it counts as wider than any line, its interior
			// is not measured, and what follows resumes behind its closing brace.
			if (iToken < mp_BlockEnds.f_GetLen() && mp_BlockEnds[iToken] <= _iLastToken)
			{
				nColumns += gc_nBlockWidth;
				iToken = mp_BlockEnds[iToken];
				iPrevious = iToken;

				continue;
			}

			// So does a list whose opening marker the source ends its line with: it is written open, one element per line.
			if (iToken < mp_OpenListEnds.f_GetLen() && mp_OpenListEnds[iToken] <= _iLastToken)
			{
				nColumns += gc_nBlockWidth;
				iToken = mp_OpenListEnds[iToken];
				iPrevious = iToken;

				continue;
			}

			auto const &Token = Tokens[iToken];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineSplice || Kind == ECodeTokenKind::mc_Whitespace)
				continue;

			// A line comment and a directive each end the line they stand on, so a construct
			// holding one is wider than any line rather than unmeasurable. So does a block comment on a line of its own.
			if (Kind == ECodeTokenKind::mc_LineComment || (Kind == ECodeTokenKind::mc_Preprocessor && !mp_OpaqueDirective[iToken]) || fp_IsCommentLine(iToken))
			{
				nColumns += gc_nBlockWidth;

				continue;
			}

			bool bAttached = Kind == ECodeTokenKind::mc_BlockComment && fp_IsAttachedComment(iToken);
			bool bBlockComment
				= (_bBlockComments && Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine)
				|| bAttached
				|| (Kind == ECodeTokenKind::mc_BlockComment && fp_IsTrailingBlockComment(iToken))
				|| (Kind == ECodeTokenKind::mc_BlockComment && fp_IsNameComment(iToken))
			;
			if ((Kind == ECodeTokenKind::mc_BlockComment && !bBlockComment) || Kind == ECodeTokenKind::mc_Preprocessor)
				return false;

			if (Token.m_bMultiLine)
				return false;

			if (iToken != _iFirstToken)
			{
				bool bNewline = false;
				bool bComment = false;
				umint nGap = 0;
				for (umint iGap = iPrevious + 1; iGap < iToken; ++iGap)
				{
					auto GapKind = Tokens[iGap].m_Kind;
					if (GapKind == ECodeTokenKind::mc_Newline || GapKind == ECodeTokenKind::mc_LineSplice)
						bNewline = true;
					else if (GapKind == ECodeTokenKind::mc_Whitespace)
						nGap += fp_GetTokenColumns(Tokens[iGap]);
					else if (GapKind == ECodeTokenKind::mc_LineComment || (GapKind == ECodeTokenKind::mc_Preprocessor && !mp_OpaqueDirective[iGap]) || fp_IsCommentLine(iGap))
						bComment = true;
					else
						return false;
				}

				// A string continued on a line of its own inside a bracket keeps that line, as a
				// line comment ends the line in front of it, so the bracket around it opens.
				if (bNewline && !bComment && mp_TokenDepth[iToken] > mp_TokenDepth[_iFirstToken] && fp_ContinuesLiteral(iToken) >= 0)
				{
					nColumns += gc_nBlockWidth + fp_GetTokenColumns(Token);
					iPrevious = iToken;

					continue;
				}

				// Two closers of nested template argument lists are written as one '>>'
				// whatever the source has between them.
				bool bClosers
					= mp_Structure.f_IsAngleBracket(iPrevious) && mp_Structure.f_IsAngleBracket(iToken) && mp_Tokens.f_IsText(Tokens[iPrevious], ">") && mp_Tokens.f_IsText(Token, ">")
				;
				bool bBesideComment = bBlockComment || Tokens[iPrevious].m_Kind == ECodeTokenKind::mc_BlockComment;

				// A gap the standard settles is measured as the spacing rule writes it, so a
				// line measured to fit still fits once its gaps are respaced.
				if (bClosers || bComment)
					nColumns += 0;
				else if (bBesideComment)
				{
					// A break in front of a comment the code behind it names is spelled as that code's gap is.
					if (bNewline && bAttached)
					{
						auto iNamed = fp_NextCode(iToken);
						auto Spacing = iNamed >= 0 ? fp_GetInlineSpacing(iPrevious, umint(iNamed)) : ECodeSpacing::mc_Preserve;
						if (Spacing == ECodeSpacing::mc_Preserve)
							return false;

						nColumns += Spacing == ECodeSpacing::mc_Space;
					}
					else if (bNewline)
						return false;
					else if (Kind == ECodeTokenKind::mc_BlockComment && fp_IsTrailingBlockComment(iToken))
						nColumns += 1;
					else
					{
						// As the spacing rules write it where they settle it: a comma's space in front of a comment too.
						auto Spacing = fp_GetInlineSpacing(iPrevious, iToken);
						nColumns += Spacing == ECodeSpacing::mc_Preserve ? nGap : umint(Spacing == ECodeSpacing::mc_Space);
					}
				}
				else
				{
					// A gap on one line is measured at the width the spacing rules leave it,
					// which is its own only where the standard settles nothing.
					auto Spacing = bNewline ? fp_GetJoinSpacing(iPrevious, iToken) : fp_GetInlineSpacing(iPrevious, iToken);
					if (Spacing == ECodeSpacing::mc_Preserve)
					{
						if (bNewline)
							return false;

						nColumns += nGap;
					}
					else
						nColumns += Spacing == ECodeSpacing::mc_Space;
				}
			}

			nColumns += fp_GetTokenColumns(Token);
			iPrevious = iToken;
		}

		o_nColumns = nColumns;

		return true;
	}

	// Replaces every gap inside the construct that holds a line break with its canonical
	// inline separator. Gaps already on one line keep the spacing the other rules govern.
	bool CFormattingAnalyzer::fp_TryJoin(umint _iFirstToken, umint _iLastToken, umint _iStartColumn)
	{
		umint nColumns = 0;
		if (!fp_MeasureJoinedWidth(_iFirstToken, _iLastToken, nColumns) || nColumns >= gc_nBlockWidth)
			return false;

		auto nMaxColumns = mp_Request.m_Settings.m_nMaxColumns;
		if (nMaxColumns && _iStartColumn + nColumns > nMaxColumns)
			return false;

		// Only the edits this join emits belong to its candidate. Splitting shares the rule
		// name, and dropping a split for an overlong line would undo the very fix for it.
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iFirstEdit = mp_Edits.f_GetLen();
		umint iPrevious = TCLimitsInt<umint>::mc_Max;

		for (umint iToken = _iFirstToken; iToken <= _iLastToken; ++iToken)
		{
			auto Kind = Tokens[iToken].m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineSplice)
				continue;

			if (iPrevious != TCLimitsInt<umint>::mc_Max && iPrevious + 1 != iToken)
			{
				bool bNewline = false;
				for (umint iGap = iPrevious + 1; iGap < iToken; ++iGap)
				{
					auto GapKind = Tokens[iGap].m_Kind;
					bNewline |= GapKind == ECodeTokenKind::mc_Newline || GapKind == ECodeTokenKind::mc_LineSplice;
				}

				if (bNewline)
				{
					// A line a move only re-indents is joined instead, and the move leaves it.
					if (EGap(mp_GapState[iToken]) == EGap::mc_Indent)
						mp_GapState[iToken] = uint8(EGap::mc_Keep);

					// Written the way it was measured, the spacing rules' spelling included.
					auto Spacing = fp_GetJoinSpacing(iPrevious, iToken);
					auto iStart = Tokens[iPrevious].f_GetEnd();
					fp_AddEdit("line-break", iStart, Tokens[iToken].m_iOffset - iStart, Spacing == ECodeSpacing::mc_Space ? " " : CStr(), "the construct fits on one line");
				}
			}

			iPrevious = iToken;
		}

		auto &Candidate = mp_JoinCandidates.f_Insert();
		Candidate.m_iOffset = Tokens[_iFirstToken].m_iOffset;
		for (auto iEdit = iFirstEdit; iEdit < mp_Edits.f_GetLen(); ++iEdit)
			Candidate.m_Edits.f_Insert(iEdit);

		return true;
	}

	// Orders the collected edits and resolves the overlaps independent rules can produce,
	// for example a blank line inside a removed run that also carries trailing whitespace.
	void CFormattingAnalyzer::fp_BuildPlan(TCVector<CCodeFormattingEdit> &o_Edits, TCVector<umint> &o_Sources) const
	{
		o_Edits.f_Clear();
		o_Sources.f_Clear();

		TCVector<umint> Order;
		for (umint iEdit = 0; iEdit < mp_Edits.f_GetLen(); ++iEdit)
		{
			if (!mp_EditDropped[iEdit])
				Order.f_Insert(iEdit);
		}

		Order.f_Sort
			(
				[this](umint _Left, umint _Right)
				{
					auto const &Left = mp_Edits[_Left];
					auto const &Right = mp_Edits[_Right];
					if (Left.m_iOffset != Right.m_iOffset)
						return Left.m_iOffset <=> Right.m_iOffset;

					if (Left.m_nLength != Right.m_nLength)
						return Right.m_nLength <=> Left.m_nLength;

					// A layout edit of a line's terminator writes it in the configured line ending too, so it goes first, and the
					// line-ending rule's edit of the same bytes is left out behind it.
					bool bLeftEnding = Left.m_Rule == "line-ending";
					bool bRightEnding = Right.m_Rule == "line-ending";
					return bLeftEnding <=> bRightEnding;
				}
			)
		;

		umint iCovered = 0;
		umint iLastInsertion = 0;
		bool bHasInsertion = false;

		for (auto iEdit : Order)
		{
			auto const &Edit = mp_Edits[iEdit];
			if (Edit.m_iOffset < iCovered)
				continue;

			if (!Edit.m_nLength && bHasInsertion && Edit.m_iOffset == iLastInsertion)
				continue;

			o_Edits.f_Insert(Edit);
			o_Sources.f_Insert(iEdit);

			if (!Edit.m_nLength)
			{
				iLastInsertion = Edit.m_iOffset;
				bHasInsertion = true;
			}

			iCovered = Edit.f_GetEnd();
		}
	}

	// Joining is decided per construct, so two constructs that end up on the same line can
	// each fit and still overflow together. The plan is applied and remeasured, and every
	// join landing on an overlong line is dropped, until no join makes a line too long.
	void CFormattingAnalyzer::fp_LimitJoinedLines()
	{
		auto nMaxColumns = mp_Request.m_Settings.m_nMaxColumns;
		if (!nMaxColumns || mp_JoinCandidates.f_IsEmpty())
			return;

		for (umint iPass = 0; iPass < 8; ++iPass)
		{
			TCVector<CCodeFormattingEdit> Plan;
			TCVector<umint> Sources;
			fp_BuildPlan(Plan, Sources);
			auto Formatted = fg_ApplyCodeFormattingEdits(mp_Request.m_Source, Plan);
			CTextLineMap FormattedLines(Formatted);
			auto pFormatted = Formatted.f_GetStr();

			bool bDropped = false;
			umint iPlan = 0;
			aint nDelta = 0;

			for (auto &Candidate : mp_JoinCandidates)
			{
				if (Candidate.m_bDropped || Candidate.m_Edits.f_IsEmpty())
					continue;

				while (iPlan < Plan.f_GetLen() && Plan[iPlan].f_GetEnd() <= Candidate.m_iOffset)
				{
					auto const &Edit = Plan[iPlan];
					nDelta += aint(Edit.m_Replacement.f_GetLen()) - aint(Edit.m_nLength);
					++iPlan;
				}

				auto iLine = FormattedLines.f_FindLine(umint(aint(Candidate.m_iOffset) + nDelta));
				umint nColumns = 0;
				auto iStart = FormattedLines.f_GetLineStart(iLine);
				bool bMeasured = fg_MeasureTextColumns(pFormatted + iStart, FormattedLines.f_GetLine(iLine).m_nLength, mp_Request.m_Settings.m_nTabWidth, nColumns);
				if (bMeasured && nColumns <= nMaxColumns)
					continue;

				Candidate.m_bDropped = true;
				bDropped = true;
				for (auto iEdit : Candidate.m_Edits)
					mp_EditDropped[iEdit] = 1;
			}

			if (!bDropped)
				return;
		}

		// The passes did not settle, so no construct is joined rather than risking a long line.
		for (auto &Candidate : mp_JoinCandidates)
		{
			for (auto iEdit : Candidate.m_Edits)
				mp_EditDropped[iEdit] = 1;
		}
	}
}
