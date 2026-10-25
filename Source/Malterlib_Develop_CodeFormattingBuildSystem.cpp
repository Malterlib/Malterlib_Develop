// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

	bool fg_IsBuildSystemOpener(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Token.m_Kind == ECodeTokenKind::mc_Punctuator && (_Tokens.f_IsText(_Token, "{") || _Tokens.f_IsText(_Token, "[") || _Tokens.f_IsText(_Token, "("));
	}

	bool fg_IsBuildSystemCloser(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Token.m_Kind == ECodeTokenKind::mc_Punctuator && (_Tokens.f_IsText(_Token, "}") || _Tokens.f_IsText(_Token, "]") || _Tokens.f_IsText(_Token, ")"));
	}
}

namespace NMib::NDevelop::NPrivate
{
	// A build system file is indented by its brackets alone, the scopes of the registry and
	// the objects, arrays and calls of a value alike: a line stands at the level of the
	// innermost bracket open at its start, and a line that starts by closing it one level
	// out. The brackets a line leaves open take one level more than the line, together,
	// unless the outer one holds lines of its own behind the inner one's, as the objects of
	// '[{ ... }, { ... }]' do, where each takes a level. A line a backslash continues aligns
	// with the value it continues, its tabs filled out with spaces up to that column.
	bool CFormattingAnalyzer::fp_PrepareBuildSystemLines(CStr &o_Explanation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Settings = m_Request.m_Settings;
		auto nTab = Settings.m_nTabWidth;
		auto nLines = m_Lines.f_GetLineCount();
		m_BuildSystemLines.f_SetLen(nLines);

		// The line each opener's bracket closes on, indexed by token.
		TCVector<umint> CloseLine;
		CloseLine.f_SetLen(Tokens.f_GetLen());
		TCVector<aint> OpenerPrevious; // The code in front of each opener, indexed by token.
		OpenerPrevious.f_SetLen(Tokens.f_GetLen());
		TCVector<umint> Pending;
		aint iPreviousCode = -1;
		umint iWordsLine = TCLimitsInt<umint>::mc_Max;
		umint nWords = 0; // The words, literals and closed brackets on line iWordsLine so far, those written together as one.
		aint iLastClosedOpenerPrevious = -1; // The code in front of the opener the last closer closed.
		bool bPreviousSign = false; // The code in front is a sign standing apart from what precedes it.
		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			auto const &Token = Tokens[iToken];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_ByteOrderMark)
				continue;

			auto iLine = m_Lines.f_FindLine(Token.m_iOffset);

			// The registry opens a child scope behind an entry's value on its line, '%Group "Name" {', '%Group ("A") {',
			// 'A 1 {', which the rules, reading a brace that does not start its line as a value's, would take apart. A word
			// that starts its line is a key, which a brace behind it gives an object value, and a brace behind a function
			// type's parameters gives its return type: 'function(string _Key) {...: string}'.
			bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
			if (m_Tokens.f_IsText(Token, "{") && iPreviousCode >= 0 && iWordsLine == iLine)
			{
				auto const &Previous = Tokens[umint(iPreviousCode)];
				// An accessor's '>' ends a value as well: 'Flag Config<Enabled> {'.
				bool bValueEnd
					= Previous.m_Kind == ECodeTokenKind::mc_StringLiteral
					|| ((Previous.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Previous, ">")) && nWords > 1)
					|| m_Tokens.f_IsText(Previous, "]")
					|| m_Tokens.f_IsText(Previous, "}")
				;
				if (m_Tokens.f_IsText(Previous, ")"))
					bValueEnd = iLastClosedOpenerPrevious < 0 || !m_Tokens.f_IsText(Tokens[umint(iLastClosedOpenerPrevious)], "function");

				if (bValueEnd && m_Lines.f_FindLine(Previous.m_iOffset) == iLine)
				{
					o_Explanation = "The scope opened behind a value at line {} is left untouched"_f << iLine + 1;

					return false;
				}
			}

			bool bWord
				= Kind == ECodeTokenKind::mc_Identifier
				|| Kind == ECodeTokenKind::mc_StringLiteral
				|| m_Tokens.f_IsText(Token, ">")
				|| m_Tokens.f_IsText(Token, ")")
				|| m_Tokens.f_IsText(Token, "]")
				|| m_Tokens.f_IsText(Token, "}")
			;
			// Tokens written together are one word, an escaped name's, 'B\+\+C', and a type's, '#<CType>', but a sign standing
			// apart starts a value of its own: 'Count -1 {'.
			bool bJoined = iPreviousCode >= 0 && Tokens[umint(iPreviousCode)].f_GetEnd() == Token.m_iOffset;
			bool bBehindSign = bJoined && bPreviousSign;
			if (bWord)
			{
				if (iWordsLine != iLine)
				{
					iWordsLine = iLine;
					nWords = 0;
				}

				if (!bJoined || bBehindSign || !nWords)
					++nWords;
			}

			if (!bComment)
				bPreviousSign = !bJoined && (m_Tokens.f_IsText(Token, "-") || m_Tokens.f_IsText(Token, "+"));

			auto iCodeBefore = iPreviousCode;

			if (!bComment)
				iPreviousCode = aint(iToken);

			auto &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0)
				Line.m_iFirst = iToken;

			Line.m_iLast = iToken;

			if (fg_IsBuildSystemOpener(m_Tokens, Token))
			{
				Pending.f_Insert(iToken);
				OpenerPrevious[iToken] = iCodeBefore;
			}
			else if (fg_IsBuildSystemCloser(m_Tokens, Token))
			{
				if (Pending.f_IsEmpty())
				{
					o_Explanation = "'{}' at line {} closes no bracket"_f << m_Tokens.f_GetText(Token) << iLine + 1;

					return false;
				}

				auto const &Opener = Tokens[Pending.f_GetLast()];
				bool bMatches
					= (m_Tokens.f_IsText(Opener, "{") && m_Tokens.f_IsText(Token, "}"))
					|| (m_Tokens.f_IsText(Opener, "[") && m_Tokens.f_IsText(Token, "]"))
					|| (m_Tokens.f_IsText(Opener, "(") && m_Tokens.f_IsText(Token, ")"))
				;
				if (!bMatches)
				{
					o_Explanation
						= "'{}' at line {} closes the '{}' at line {}"_f << m_Tokens.f_GetText(Token) << iLine + 1 << m_Tokens.f_GetText(Opener) << m_Lines.f_FindLine(Opener.m_iOffset) + 1
					;

					return false;
				}

				CloseLine[Pending.f_GetLast()] = iLine;
				iLastClosedOpenerPrevious = OpenerPrevious[Pending.f_GetLast()];
				Pending.f_Remove(Pending.f_GetLen() - 1);
			}
		}

		if (!Pending.f_IsEmpty())
		{
			o_Explanation = "The bracket opened at line {} is never closed"_f << m_Lines.f_FindLine(Tokens[Pending.f_GetLast()].m_iOffset) + 1;

			return false;
		}

		struct COpen
		{
			umint m_Level = 0; // The level of the lines inside the bracket.
			umint m_iCloseLine = 0;
		};

		TCVector<COpen> Open;
		bool bContinued = false;
		umint nChainColumns = 0;

		for (umint iLine = 0; iLine < nLines; ++iLine)
		{
			auto &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0)
				continue;

			bool bCloses = fg_IsBuildSystemCloser(m_Tokens, Tokens[umint(Line.m_iFirst)]);
			Line.m_Level = Open.f_IsEmpty() ? 0 : Open.f_GetLast().m_Level - (bCloses ? 1 : 0);
			Line.m_bContinued = bContinued;
			Line.m_nContinuationColumns = nChainColumns;

			auto nOpenAtStart = Open.f_GetLen();
			for (auto iToken = umint(Line.m_iFirst); iToken <= umint(Line.m_iLast); ++iToken)
			{
				if (fg_IsBuildSystemOpener(m_Tokens, Tokens[iToken]))
					Open.f_Insert(COpen{0, CloseLine[iToken]});
				else if (fg_IsBuildSystemCloser(m_Tokens, Tokens[iToken]))
				{
					Open.f_Remove(Open.f_GetLen() - 1);
					nOpenAtStart = fg_Min(nOpenAtStart, Open.f_GetLen());
				}
			}

			for (auto iOpen = nOpenAtStart; iOpen < Open.f_GetLen(); ++iOpen)
			{
				auto &Opened = Open[iOpen];
				if (iOpen == nOpenAtStart)
					Opened.m_Level = Line.m_Level + 1;
				else
				{
					auto const &Outer = Open[iOpen - 1];
					bool bOuterHoldsMore = false;
					for (auto iBetween = Opened.m_iCloseLine + 1; iBetween < Outer.m_iCloseLine && !bOuterHoldsMore; ++iBetween)
						bOuterHoldsMore = m_BuildSystemLines[iBetween].m_iFirst >= 0;

					Opened.m_Level = Outer.m_Level + (bOuterHoldsMore ? 1 : 0);
				}
			}

			bool bSplices = Tokens[umint(Line.m_iLast)].m_Kind == ECodeTokenKind::mc_LineSplice;
			if (bSplices && !bContinued)
			{
				nChainColumns = (Line.m_Level + 1) * nTab;
				auto iFirstOffset = Tokens[umint(Line.m_iFirst)].m_iOffset;
				for (auto iToken = umint(Line.m_iFirst); iToken <= umint(Line.m_iLast); ++iToken)
				{
					if (Tokens[iToken].m_Kind != ECodeTokenKind::mc_StringLiteral)
						continue;

					// The line is measured as it will be indented, which starts on a tab stop.
					umint nColumns = 0;
					if (fg_MeasureTextColumns(m_pSource + iFirstOffset, Tokens[iToken].m_iOffset - iFirstOffset, nTab, nColumns))
						nChainColumns = Line.m_Level * nTab + nColumns;

					break;
				}
			}

			bContinued = bSplices;
		}

		return true;
	}

	void CFormattingAnalyzer::fp_RuleBuildSystemIndentation()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Source = m_Request.m_Source;
		auto nTab = m_Request.m_Settings.m_nTabWidth;

		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			auto const &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0 || m_ProtectedStart[iLine] || !fp_IsLineSelected(iLine))
				continue;

			// A line the value layout places has its indentation from it.
			if (umint(Line.m_iFirst) < m_BuildSystemPlaced.f_GetLen() && m_BuildSystemPlaced[umint(Line.m_iFirst)])
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			if (!iLine)
				iStart += fg_GetTextBomLength(Source);

			auto const &First = Tokens[umint(Line.m_iFirst)];

			// A comment at the very start of a line comments out what it stands in front of.
			bool bComment = First.m_Kind == ECodeTokenKind::mc_LineComment || First.m_Kind == ECodeTokenKind::mc_BlockComment;
			if (bComment && First.m_iOffset == iStart)
				continue;

			auto Canonical = fp_MakeIndent(Line.m_bContinued ? Line.m_nContinuationColumns : Line.m_Level * nTab);
			if (fg_IsSameText(Canonical, m_pSource + iStart, First.m_iOffset - iStart))
				continue;

			fp_AddEdit
				(
					"indentation"
					, iStart
					, First.m_iOffset - iStart
					, Canonical
					, Line.m_bContinued ? "a continued value aligns with the value it continues" : "a line stands one level deeper than the bracket open around it"
				)
			;
		}
	}

	// A value is laid out by the rules C++ code is, with its openers ending their lines: a value written across lines
	// is brought back onto one where it fits, unless it holds an array or object written across lines, which keeps
	// its lines with each element on one of its own. Otherwise the widest bracket the value holds whole opens, or
	// the one holding what keeps its lines: the opener ends its line, the members stand one level in, split at
	// their commas with each comma in front, or, without commas, in front of the operators that bind loosest, and
	// the closer stands at the line's level. A member laid out the same way opens in turn where it does not fit. A
	// string ending a line counts for no width, as for line-length. A statement holding a comment, other than one
	// ending it, or continued by a backslash keeps its lines.
	void CFormattingAnalyzer::fp_RuleBuildSystemLineBreaks()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());

		m_BuildSystemPlaced.f_SetLen(Tokens.f_GetLen());
		for (auto &bPlaced : m_BuildSystemPlaced)
			bPlaced = 0;

		auto fIsCode = [&](umint _iToken)
			{
				auto Kind = Tokens[_iToken].m_Kind;

				return Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline && Kind != ECodeTokenKind::mc_ByteOrderMark;
			}
		;

		auto fNext = [&](umint _iToken)
			{
				auto i = _iToken + 1;
				while (i < Tokens.f_GetLen() && !fIsCode(i))
					++i;

				return i;
			}
		;

		auto fPrevious = [&](umint _iToken) -> aint
			{
				aint i = aint(_iToken) - 1;
				while (i >= 0 && !fIsCode(umint(i)))
					--i;

				return i;
			}
		;

		auto fLine = [&](umint _iToken)
			{
				return m_Lines.f_FindLine(Tokens[_iToken].m_iOffset);
			}
		;

		auto fIsText = [&](umint _iToken, ch8 const *_pText)
			{
				return m_Tokens.f_IsText(Tokens[_iToken], _pText);
			}
		;

		// The closer of each opener, and whether a bracket is a scope of the registry rather than part of a value.
		TCVector<umint> Closers;
		Closers.f_SetLen(Tokens.f_GetLen());
		TCVector<uint8> ScopeBrackets;
		ScopeBrackets.f_SetLen(Tokens.f_GetLen());

		{
			TCVector<umint> Open;
			umint nValueDepth = 0;

			for (umint i = 0; i < Tokens.f_GetLen(); ++i)
			{
				Closers[i] = Tokens.f_GetLen();
				auto &bScope = ScopeBrackets[i];
				bScope = false;
				if (!fIsCode(i))
					continue;

				auto const &Token = Tokens[i];
				if (fg_IsBuildSystemOpener(m_Tokens, Token))
				{
					auto iBefore = fPrevious(i);
					bool bFirstOnLine = iBefore < 0 || fLine(umint(iBefore)) != fLine(i);
					bScope = fIsText(i, "{") && bFirstOnLine && !nValueDepth;
					nValueDepth += !bScope;
					Open.f_Insert(i);
				}
				else if (fg_IsBuildSystemCloser(m_Tokens, Token) && !Open.f_IsEmpty())
				{
					auto iOpener = Open.f_GetLast();
					Open.f_Remove(Open.f_GetLen() - 1);
					Closers[iOpener] = i;
					bScope = ScopeBrackets[iOpener];
					nValueDepth -= !bScope;
				}
			}
		}

		// The arguments in front of the key and value pairs of a call to one of the language's switch functions, or -1
		// for any other bracket: the value switched on, unless it is the object the function is called on, and the
		// error or default.
		auto fSwitchSingles = [&](umint _iOpener) -> aint
			{
				if (!fIsText(_iOpener, "("))
					return -1;

				auto iName = fPrevious(_iOpener);
				if (iName < 0)
					return -1;

				auto nSingles = fIsText(umint(iName), "Switch") ? 1 : (fIsText(umint(iName), "SwitchWithError") || fIsText(umint(iName), "SwitchWithDefault")) ? 2 : 0;
				if (!nSingles)
					return -1;

				// The build system's lexer spells '->' as '-' and '>'.
				auto iBefore = fPrevious(umint(iName));
				bool bMember = iBefore >= 0 && fIsText(umint(iBefore), "->");
				if (iBefore >= 0 && fIsText(umint(iBefore), ">"))
				{
					auto iMinus = fPrevious(umint(iBefore));
					bMember = iMinus >= 0 && fIsText(umint(iMinus), "-") && Tokens[umint(iMinus)].f_GetEnd() == Tokens[umint(iBefore)].m_iOffset;
				}

				return nSingles - (bMember ? 1 : 0);
			}
		;

		// A switch's pairs always stand on lines of their own, and so does an array or object written across lines.
		auto fIsKept = [&](umint _iOpener)
			{
				auto iClose = Closers[_iOpener];
				if (iClose >= Tokens.f_GetLen())
					return false;

				if (fSwitchSingles(_iOpener) >= 0)
					return fNext(_iOpener) != iClose;

				return (fIsText(_iOpener, "[") || fIsText(_iOpener, "{")) && fLine(_iOpener) != fLine(iClose);
			}
		;

		// The spelling a gap takes on one line: the source's where it has no line break, and otherwise the one the
		// tokens around it are written with.
		auto fGapText = [&](umint _iLeft, umint _iRight) -> CStr
			{
				auto iGap = Tokens[_iLeft].f_GetEnd();
				CStr Gap(m_pSource + iGap, Tokens[_iRight].m_iOffset - iGap);

				// What follows a '<' says whether it compares or reaches into its value: a blank makes it compare. The
				// gap behind one is never rewritten.
				if ((Gap.f_FindChar('\n') < 0 && Gap.f_FindChar('\r') < 0) || fIsText(_iLeft, "<"))
					return Gap;

				if (fIsText(_iLeft, "(") || fIsText(_iLeft, "[") || fIsText(_iRight, ")") || fIsText(_iRight, "]") || fIsText(_iRight, ","))
					return {};

				// A member is reached without blanks, '->' spelled as '-' and '>', and so are a subscript, '<[1]>', and a
				// named value, '<Name>'.
				auto iAfterRight = fNext(_iRight);
				bool bAttached = iAfterRight < Tokens.f_GetLen() && Tokens[_iRight].f_GetEnd() == Tokens[iAfterRight].m_iOffset;
				bool bArrow = bAttached && fIsText(_iRight, "-") && fIsText(iAfterRight, ">");
				bool bAccess = bAttached && fIsText(_iRight, "<") && (fIsText(iAfterRight, "[") || Tokens[iAfterRight].m_Kind == ECodeTokenKind::mc_Identifier);
				if (bArrow || bAccess || fIsText(_iRight, ".") || fIsText(_iLeft, "."))
					return {};

				return " ";
			}
		;

		auto fWidth = [&](umint _nIndent, umint _iFirst, umint _iLast, umint &o_nColumns)
			{
				CStr Text;
				{
					CStr::CAppender Appender(Text);
					for (auto i = _iFirst; i <= _iLast; i = fNext(i))
					{
						if (i != _iFirst)
							Appender += fGapText(umint(fPrevious(i)), i);

						auto const &Token = Tokens[i];
						Appender.f_AddString(m_pSource + Token.m_iOffset, Token.m_nLength);
					}
				}

				umint nColumns = 0;
				if (!fg_MeasureTextColumns(Text, nTab, nColumns))
					return false;

				o_nColumns = _nIndent + nColumns;

				return true;
			}
		;

		auto fFits = [&](umint _nIndent, umint _iFirst, umint _iLast)
			{
				umint nColumns = 0;

				return fWidth(_nIndent, _iFirst, _iLast, nColumns) && (!nMaxColumns || nColumns <= nMaxColumns);
			}
		;

		auto fHoldsKept = [&](umint _iFirst, umint _iLast)
			{
				for (auto i = _iFirst; i <= _iLast; i = fNext(i))
				{
					if (fg_IsBuildSystemOpener(m_Tokens, Tokens[i]) && fIsKept(i))
						return true;
				}

				return false;
			}
		;

		struct CGap
		{
			bool m_bBreak = false;
			umint m_nIndent = 0;
		};

		TCMap<umint, CGap> Gaps;

		auto fJoin = [&](umint _iFirst, umint _iLast)
			{
				for (auto i = fNext(_iFirst); i <= _iLast; i = fNext(i))
				{
					if (fLine(umint(fPrevious(i))) != fLine(i))
						Gaps[i] = CGap{false, 0};
				}
			}
		;

		auto fBreak = [&](umint _iToken, umint _nIndent)
			{
				auto iBefore = fPrevious(_iToken);
				if (iBefore < 0 || !fIsText(umint(iBefore), "<"))
					Gaps[_iToken] = CGap{true, _nIndent};
			}
		;

		auto fAttached = [&](umint _iLeft, umint _iRight)
			{
				auto const *pRight = _iRight < Tokens.f_GetLen() ? &Tokens[_iRight] : nullptr;

				return pRight && pRight->m_Kind == ECodeTokenKind::mc_Punctuator && Tokens[_iLeft].f_GetEnd() == pRight->m_iOffset;
			}
		;

		// How loosely an operator binds where a group without commas splits, by the build system's precedence, or zero for
		// no operator. Its lexer spells each punctuator as a token, so a binary operator is a run of operator characters
		// with a blank on both sides, which tells it from a prefix operator and from an accessor's '<' and '>'.
		auto fLooseness = [&](umint _iToken, bool _bTernary) -> umint
			{
				auto fIsOperatorCharacter = [&](umint _iCharacter)
					{
						auto const &Token = Tokens[_iCharacter];

						return Token.m_Kind == ECodeTokenKind::mc_Punctuator && Token.m_nLength == 1 && NStr::fg_StrFindChar("+-*/%<>=!&^|?", m_pSource[Token.m_iOffset]) >= 0;
					}
				;

				if (_bTernary && fIsText(_iToken, ":"))
					return 16;

				auto iBefore = fPrevious(_iToken);
				if (!fIsOperatorCharacter(_iToken) || (iBefore >= 0 && Tokens[umint(iBefore)].f_GetEnd() == Tokens[_iToken].m_iOffset))
					return 0;

				CStr Operator = m_Tokens.f_GetText(Tokens[_iToken]);
				auto iLast = _iToken;

				for (auto i = fNext(_iToken); i < Tokens.f_GetLen() && fAttached(iLast, i) && fIsOperatorCharacter(i); i = fNext(i))
				{
					auto const &Token = Tokens[i];
					Operator.f_AddStr(m_pSource + Token.m_iOffset, Token.m_nLength);
					iLast = i;
				}

				auto iAfter = fNext(iLast);
				if (iAfter < Tokens.f_GetLen() && Tokens[iLast].f_GetEnd() == Tokens[iAfter].m_iOffset)
					return 0;

				if (Operator == "?")
					return 16;
				if (Operator == "||" || Operator == "??")
					return 15;
				if (Operator == "&&")
					return 14;
				if (Operator == "|")
					return 13;
				if (Operator == "^")
					return 12;
				if (Operator == "&")
					return 11;
				if (Operator == "==" || Operator == "!=" || Operator == "<==>" || Operator == "<!=>")
					return 10;
				if (Operator == "<" || Operator == "<=" || Operator == ">" || Operator == ">=")
					return 9;
				if (Operator == "<<" || Operator == ">>")
					return 7;
				if (Operator == "+" || Operator == "-")
					return 6;
				if (Operator == "*" || Operator == "/" || Operator == "%")
					return 5;

				return 0;
			}
		;

		// The closer of a bracket opening at the token and closing by _iLast, which a walk at the range's own level steps to, or -1.
		auto fNestedCloser = [&](umint _iToken, umint _iLast) -> aint
			{
				if (!fg_IsBuildSystemOpener(m_Tokens, Tokens[_iToken]))
					return -1;

				auto iClose = Closers[_iToken];

				return iClose <= _iLast ? aint(iClose) : -1;
			}
		;

		// The loosest operators at a range's own level, outside the brackets it holds.
		auto fOperatorSplits = [&](umint _iFirst, umint _iLast)
			{
				bool bTernary = false;
				for (auto i = _iFirst; i <= _iLast; i = fNext(i))
				{
					if (auto iClose = fNestedCloser(i, _iLast); iClose >= 0)
						i = umint(iClose);
					else if (fIsText(i, "?"))
						bTernary = true;
				}

				umint nLoosest = 0;
				for (auto i = _iFirst; i <= _iLast; i = fNext(i))
				{
					if (auto iClose = fNestedCloser(i, _iLast); iClose >= 0)
						i = umint(iClose);
					else if (i != _iFirst)
						nLoosest = fg_Max(nLoosest, fLooseness(i, bTernary));
				}

				TCVector<umint> Splits;
				for (auto i = _iFirst; nLoosest && i <= _iLast; i = fNext(i))
				{
					if (auto iClose = fNestedCloser(i, _iLast); iClose >= 0)
						i = umint(iClose);
					else if (i != _iFirst && fLooseness(i, bTernary) == nLoosest)
						Splits.f_Insert(i);
				}

				return Splits;
			}
		;

		auto fLayout = [&](auto &&_fLayout, umint _nIndent, umint _iFirst, umint _iLast, bool _bInBracket) -> void
			{
				bool bKept = fHoldsKept(_iFirst, _iLast);
				if (!bKept && fFits(_nIndent, _iFirst, _iLast))
				{
					fJoin(_iFirst, _iLast);

					return;
				}

				// The outermost break comes first, as in C++: a range's own operators split it before a bracket in it opens,
				// and what follows the first operator continues one level in. A line break ends a statement no bracket is
				// open in, so only a range inside a bracket splits there.
				if (!bKept && _bInBracket)
				{
					auto Splits = fOperatorSplits(_iFirst, _iLast);
					if (!Splits.f_IsEmpty())
					{
						auto iPart = _iFirst;
						for (auto iSplit : Splits)
						{
							_fLayout(_fLayout, iPart == _iFirst ? _nIndent : _nIndent + nTab, iPart, umint(fPrevious(iSplit)), _bInBracket);
							fBreak(iSplit, _nIndent + nTab);
							iPart = iSplit;
						}

						_fLayout(_fLayout, _nIndent + nTab, iPart, _iLast, _bInBracket);

						return;
					}
				}

				// The bracket that opens: one holding what keeps its lines, and otherwise the widest.
				aint iOpen = -1;
				umint iOpenClose = 0;
				umint nWidest = 0;

				for (auto i = _iFirst; i <= _iLast; i = fNext(i))
				{
					auto iNestedClose = fNestedCloser(i, _iLast);
					if (iNestedClose < 0 || ScopeBrackets[i])
						continue;

					auto iClose = umint(iNestedClose);
					bool bInnerKept = fIsKept(i) || fHoldsKept(i, iClose);
					umint nColumns = 0;
					fWidth(0, i, iClose, nColumns);
					if (fNext(i) != iClose && ((bInnerKept && (iOpen < 0 || !fHoldsKept(umint(iOpen), iOpenClose))) || (!bKept && nColumns > nWidest)))
					{
						iOpen = aint(i);
						iOpenClose = iClose;
						nWidest = nColumns;
					}

					i = iClose;
				}

				// A member chain gives at every member one level in, as in C++, starting at the first access on what a bracket
				// yields, where opening a bracket does not leave the lines in front of and behind it fitting.
				TCVector<umint> Links;
				for (auto i = _iFirst; !bKept && _bInBracket && i <= _iLast; i = fNext(i))
				{
					if (auto iClose = fNestedCloser(i, _iLast); iClose >= 0)
					{
						i = umint(iClose);

						continue;
					}

					auto iArrow = fNext(i);
					bool bLink = i != _iFirst && fIsText(i, "-") && fAttached(i, iArrow) && fIsText(iArrow, ">");
					if (bLink && fg_IsBuildSystemCloser(m_Tokens, Tokens[umint(fPrevious(i))]))
						Links.f_Insert(i);
				}

				bool bOpenFits = iOpen >= 0 && fFits(_nIndent, _iFirst, umint(iOpen)) && fFits(_nIndent, iOpenClose, _iLast);
				if (!Links.f_IsEmpty() && !bOpenFits)
				{
					_fLayout(_fLayout, _nIndent, _iFirst, umint(fPrevious(Links[0])), _bInBracket);
					for (umint iLink = 0; iLink < Links.f_GetLen(); ++iLink)
					{
						auto iEnd = iLink + 1 < Links.f_GetLen() ? umint(fPrevious(Links[iLink + 1])) : _iLast;
						fBreak(Links[iLink], _nIndent + nTab);
						_fLayout(_fLayout, _nIndent + nTab, Links[iLink], iEnd, _bInBracket);
					}

					return;
				}

				if (iOpen < 0)
				{
					if (!bKept)
						fJoin(_iFirst, _iLast);

					return;
				}

				auto iOpener = umint(iOpen);
				auto iClose = iOpenClose;
				if (iOpener != _iFirst)
					fJoin(_iFirst, iOpener);

				auto nInner = _nIndent + nTab;
				auto iFirstMember = fNext(iOpener);
				auto iLastMember = umint(fPrevious(iClose));

				// A comma ending the list goes.
				if (fIsText(iLastMember, ",") && iLastMember != iFirstMember)
				{
					auto iBefore = umint(fPrevious(iLastMember));
					auto iRemove = Tokens[iBefore].f_GetEnd();
					fp_AddEdit("line-break", iRemove, Tokens[iLastMember].f_GetEnd() - iRemove, {}, "no comma ends a list");
					iLastMember = iBefore;
				}

				// The member separators at the bracket's own level: its commas, or where it has none the operators that bind loosest.
				TCVector<umint> Splits;
				auto nSingles = fSwitchSingles(iOpener);
				umint iArgument = 0;

				for (auto i = iFirstMember; i <= iLastMember; i = fNext(i))
				{
					if (auto iClose = fNestedCloser(i, iLastMember); iClose >= 0)
						i = umint(iClose);
					else if (fIsText(i, ","))
					{
						// A switch's key and value share a line.
						++iArgument;
						if (nSingles < 0 || aint(iArgument) <= nSingles || (iArgument - umint(nSingles)) % 2 == 0)
							Splits.f_Insert(i);
					}
				}

				// A grouping parenthesis's operands stand at its level, as a C++ condition's do; a call's argument and an
				// element continue one level in.
				auto iBeforeOpener = fPrevious(iOpener);
				auto const *pBeforeOpener = iBeforeOpener >= 0 ? &Tokens[umint(iBeforeOpener)] : nullptr;
				bool bGroup = fIsText(iOpener, "(") && (!pBeforeOpener || pBeforeOpener->m_Kind != ECodeTokenKind::mc_Identifier || pBeforeOpener->f_GetEnd() != Tokens[iOpener].m_iOffset);
				if (!iArgument && bGroup)
					Splits = fOperatorSplits(iFirstMember, iLastMember);

				fBreak(iFirstMember, nInner);
				auto iMember = iFirstMember;
				for (auto iSplit : Splits)
				{
					_fLayout(_fLayout, nInner, iMember, umint(fPrevious(iSplit)), true);
					fBreak(iSplit, nInner);
					iMember = iSplit;
				}

				_fLayout(_fLayout, nInner, iMember, iLastMember, true);
				fBreak(iClose, _nIndent);

				// Two objects that both open meet on the line one closes on: '}, {'.
				auto fOpens = [&](umint _iCloser)
					{
						auto pGap = Gaps.f_FindEqual(_iCloser);

						return pGap && pGap->m_bBreak;
					}
				;

				for (auto iSplit : Splits)
				{
					auto iBefore = fPrevious(iSplit);
					auto iAfter = fNext(iSplit);
					if (!fIsText(iSplit, ",") || iBefore < 0 || !fIsText(umint(iBefore), "}") || !fIsText(iAfter, "{"))
						continue;

					auto iAfterClose = Closers[iAfter];
					if (iAfterClose < Tokens.f_GetLen() && fOpens(umint(iBefore)) && fOpens(iAfterClose))
					{
						Gaps[iSplit] = CGap{false, 0};
						Gaps[iAfter] = CGap{false, 0};
					}
				}

				// What follows the closer on its line is laid out from there: '])->F(...)'.
				if (iClose != _iLast)
					_fLayout(_fLayout, _nIndent, iClose, _iLast, _bInBracket);
			}
		;

		// The statements: what stands from a line no value is open at to the line all of its values close on.
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			auto const &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0 || Line.m_bContinued)
				continue;

			auto iFirst = umint(Line.m_iFirst);
			if (ScopeBrackets[iFirst] || fg_IsBuildSystemCloser(m_Tokens, Tokens[iFirst]))
				continue;

			// The statement runs on while a bracket its lines open stays open.
			umint iLast = iFirst;
			umint nOpen = 0;
			auto iEndLine = iLine;
			bool bPlain = true;

			for (auto i = iFirst; i < Tokens.f_GetLen(); ++i)
			{
				if (!fIsCode(i))
				{
					if (Tokens[i].m_Kind == ECodeTokenKind::mc_Newline && !nOpen)
						break;

					continue;
				}

				auto const &Token = Tokens[i];
				auto Kind = Token.m_Kind;
				if (Kind == ECodeTokenKind::mc_LineSplice)
					bPlain = false;

				if (fg_IsBuildSystemOpener(m_Tokens, Token) && !ScopeBrackets[i])
					++nOpen;
				else if (fg_IsBuildSystemCloser(m_Tokens, Token) && !ScopeBrackets[i] && nOpen)
					--nOpen;
				else if (fg_IsBuildSystemOpener(m_Tokens, Token) || fg_IsBuildSystemCloser(m_Tokens, Token))
					break;

				iLast = i;
				iEndLine = fLine(i);
			}

			// A comment ending the statement is no part of its value; one inside it keeps its lines.
			auto LastKind = Tokens[iLast].m_Kind;
			if (LastKind == ECodeTokenKind::mc_LineComment || LastKind == ECodeTokenKind::mc_BlockComment)
			{
				auto iBefore = fPrevious(iLast);
				if (iBefore < 0 || umint(iBefore) < iFirst)
				{
					iLine = iEndLine;

					continue;
				}

				iLast = umint(iBefore);
			}

			for (auto i = iFirst; bPlain && i <= iLast; ++i)
			{
				auto Kind = Tokens[i].m_Kind;
				bPlain = Kind != ECodeTokenKind::mc_LineComment && Kind != ECodeTokenKind::mc_BlockComment && (!fIsCode(i) || !Tokens[i].m_bMultiLine);
			}

			bool bSelected = true;
			for (auto iSelected = iLine; iSelected <= iEndLine && bSelected; ++iSelected)
				bSelected = fp_IsLineSelected(iSelected) && !m_ProtectedStart[iSelected];

			auto iStatementStart = m_Lines.f_GetLineStart(iLine);
			auto iStatementEnd = m_Lines.f_GetLineStart(iEndLine) + m_Lines.f_GetLine(iEndLine).m_nLength;
			if (bPlain && bSelected && !fp_IsFormatOff(iStatementStart, iStatementEnd - iStatementStart))
				fLayout(fLayout, Line.m_Level * nTab, iFirst, iLast, false);

			iLine = iEndLine;
		}

		for (auto iGap = Gaps.f_GetIterator(); iGap; ++iGap)
		{
			auto iToken = Gaps.fs_GetKey(*iGap);
			auto const &Gap = *iGap;
			auto iPrevious = umint(fPrevious(iToken));
			auto iStart = Tokens[iPrevious].f_GetEnd();
			auto pCurrent = m_pSource + iStart;
			auto nCurrent = Tokens[iToken].m_iOffset - iStart;

			CStr Replacement;
			if (Gap.m_bBreak)
			{
				// A blank line between two members keeps setting them apart, whatever ends its lines: LF, CRLF or a lone CR.
				umint nNewlines = 0;
				for (umint i = 0; i < nCurrent; ++i)
					nNewlines += pCurrent[i] == '\n' || (pCurrent[i] == '\r' && (i + 1 >= nCurrent || pCurrent[i + 1] != '\n'));

				bool bBlank = nNewlines > 1 && !fg_IsBuildSystemOpener(m_Tokens, Tokens[iPrevious]) && !fg_IsBuildSystemCloser(m_Tokens, Tokens[iToken]);
				Replacement = (bBlank ? Ending + Ending : Ending) + fp_MakeIndent(Gap.m_nIndent);
			}
			else
				Replacement = fGapText(iPrevious, iToken);

			m_BuildSystemPlaced[iToken] = 1;

			if (fg_IsSameText(Replacement, pCurrent, nCurrent))
				continue;

			fp_AddEdit("line-break", iStart, nCurrent, Replacement, Gap.m_bBreak ? "a value split across lines opens its brackets" : "a value that fits on one line is written on one");
		}
	}

	// A blank line separates what it separates; a second adds nothing, and none belongs at
	// the start or end of the file or right inside a bracket.
	void CFormattingAnalyzer::fp_RuleBuildSystemBlankLines()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLines = m_Lines.f_GetLineCount();

		// The empty line behind the final line terminator is where the file ends, not a line.
		auto nContentLines = nLines;
		if (nLines && !m_Lines.f_GetLine(nLines - 1).m_nLength && m_Lines.f_GetLine(nLines - 1).m_Ending == ETextLineEnding::mc_None)
			--nContentLines;

		auto fIsBlank = [&](umint _iLine)
			{
				return m_BuildSystemLines[_iLine].m_iFirst < 0 && !m_ProtectedStart[_iLine];
			}
		;

		umint iLine = 0;
		while (iLine < nContentLines)
		{
			if (!fIsBlank(iLine))
			{
				++iLine;

				continue;
			}

			auto iRunEnd = iLine;
			while (iRunEnd < nContentLines && fIsBlank(iRunEnd))
				++iRunEnd;

			bool bAfterOpen = iLine && m_BuildSystemLines[iLine - 1].m_iLast >= 0 && fg_IsBuildSystemOpener(m_Tokens, Tokens[umint(m_BuildSystemLines[iLine - 1].m_iLast)]);
			bool bBeforeClose = iRunEnd < nContentLines && m_BuildSystemLines[iRunEnd].m_iFirst >= 0 && fg_IsBuildSystemCloser(m_Tokens, Tokens[umint(m_BuildSystemLines[iRunEnd].m_iFirst)]);
			bool bKeep = iLine && iRunEnd < nContentLines && !bAfterOpen && !bBeforeClose;
			auto iRemove = iLine + (bKeep ? 1 : 0);
			if (iRemove < iRunEnd)
			{
				// A byte order mark starting the file stays in front of the run, and in a file holding nothing else with the
				// line ending the final newline rule gives it.
				auto iRemoveStart = m_Lines.f_GetLineStart(iRemove);
				if (!iRemoveStart)
				{
					iRemoveStart = fg_GetTextBomLength(m_Request.m_Source);
					if (iRemoveStart && iRunEnd >= nContentLines)
						iRemoveStart = m_Lines.f_GetLineEnd(0);
				}

				auto iRemoveEnd = m_Request.m_Source.f_GetLen();
				if (iRunEnd < nLines)
					iRemoveEnd = m_Lines.f_GetLineStart(iRunEnd);
				else if (iLine)
				{
					// The file ends without a terminator on a blank line, so the terminator in front
					// of the run goes with it, and the final newline rule writes the file's last one.
					iRemoveStart = m_Lines.f_GetLineContentEnd(iLine - 1);
				}

				CStr Explanation
					= bKeep
					? "one blank line separates, and a second adds nothing"
					: bAfterOpen || bBeforeClose ? "no blank line stands right inside a bracket" : "no blank line starts or ends the file"
				;
				if (iRemoveEnd > iRemoveStart)
					fp_AddEdit("blank-line", iRemoveStart, iRemoveEnd - iRemoveStart, {}, Explanation);
			}

			iLine = iRunEnd;
		}
	}
}
