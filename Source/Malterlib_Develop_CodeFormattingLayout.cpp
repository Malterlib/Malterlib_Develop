// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace NMib::NDevelop::NPrivate
{
	CStr CFormattingAnalyzer::fp_MakeIndent(umint _nColumns) const
	{
		auto const &Settings = m_Request.m_Settings;
		CStr Indent;

		if (!Settings.m_bIndentWithTabs)
		{
			Indent.f_AddChars(' ', aint(_nColumns));

			return Indent;
		}

		Indent.f_AddChars('\t', aint(_nColumns / Settings.m_nTabWidth));
		Indent.f_AddChars(' ', aint(_nColumns % Settings.m_nTabWidth));

		return Indent;
	}

	// The indentation fp_MakeIndent spells, written where a text being built ends.
	void CFormattingAnalyzer::fp_AppendIndent(CStr::CAppender &o_Appender, umint _nColumns) const
	{
		auto const &Settings = m_Request.m_Settings;
		auto nTabs = Settings.m_bIndentWithTabs ? _nColumns / Settings.m_nTabWidth : 0;
		auto nSpaces = Settings.m_bIndentWithTabs ? _nColumns % Settings.m_nTabWidth : _nColumns;

		for (umint i = 0; i < nTabs; ++i)
			o_Appender.f_AddChar('\t');

		for (umint i = 0; i < nSpaces; ++i)
			o_Appender.f_AddChar(' ');
	}

	// A statement's own indentation is where its first token already sits. Splitting places
	// new lines relative to that, so no separate model of scope depth is needed.
	// The indentation a statement starting at the token is laid out against: the one the
	// layout gave it, or else the column the source wrote it at.
	umint CFormattingAnalyzer::fp_GetStatementIndent(umint _iToken) const
	{
		if (fp_IsBreakGap(_iToken))
			return m_GapIndent[_iToken];

		// Code on a label's line stands where the spacing rules write it behind the label, however far the source aligned
		// it, also where the layout takes it onto that line.
		bool bJoined = EGap(m_GapState[_iToken]) == EGap::mc_Inline;
		if (!fp_IsFirstOnLine(_iToken) || bJoined)
		{
			auto const &Tokens = m_Tokens.f_GetTokens();
			auto iLineFirst = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[_iToken].m_iOffset)));
			while (iLineFirst < _iToken && Tokens[iLineFirst].m_Kind == ECodeTokenKind::mc_Whitespace)
				++iLineFirst;

			if (bJoined)
			{
				iLineFirst = _iToken;
				for (auto iBefore = fp_PreviousCode(iLineFirst); iBefore >= 0 && EGap(m_GapState[iLineFirst]) == EGap::mc_Inline; iBefore = fp_PreviousCode(iLineFirst))
					iLineFirst = umint(iBefore);
			}

			// A label is a statement of its own that its ':' ends: a case's, a jump target's or an access specifier's.
			bool bLabel = false;
			auto const &Nodes = m_Structure.f_GetNodes();
			for (auto i = iLineFirst; i < _iToken && !bLabel; ++i)
			{
				if (!m_Tokens.f_IsText(Tokens[i], ":"))
					continue;

				auto iStatement = m_Structure.f_FindStatementEndingAt(i);
				bLabel = iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_iFirstToken == iLineFirst;
			}

			// So does code behind the start of a line the layout places: it moves with that start, measured as the spacing
			// rules write the line.
			umint nJoined = 0;
			bool bPlacedLine = iLineFirst < _iToken && fg_IsCodeToken(Tokens[iLineFirst]) && fp_IsBreakGap(iLineFirst);
			if ((bLabel || bPlacedLine) && fp_MeasureJoinedWidth(iLineFirst, _iToken, nJoined))
				return fp_GetStatementIndent(iLineFirst) + nJoined - fp_GetTokenColumns(Tokens[_iToken]);
		}

		// Code behind the end of a block comment over lines starts no line of its own: it stands where the line the comment
		// starts on does, '/* A' over 'B */ {'.
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto iBefore = fp_PreviousSignificant(_iToken);
		auto const *pBefore = iBefore >= 0 ? &Tokens[umint(iBefore)] : nullptr;
		bool bBehindComment = pBefore && pBefore->m_Kind == ECodeTokenKind::mc_BlockComment && pBefore->m_bMultiLine && fp_IsFirstOnLine(umint(iBefore));
		if (bBehindComment)
		{
			auto iComment = umint(iBefore);
			if (iComment < m_CommentMoved.f_GetLen() && m_CommentMoved[iComment])
				return m_CommentIndent[iComment];

			return fp_GetSourceLineIndent(iComment);
		}

		return fp_GetColumn(Tokens[_iToken].m_iOffset) - 1;
	}

	// A block comment on one line that the code behind it follows on that line names that code, '/*bFlag=*/true', and
	// moves with it: joined, the break in front of it is spelled as the code's own gap is, and broken, the line starts
	// at the comment. One in front of a closer, a separator or a terminator describes what stands in front of it.
	bool CFormattingAnalyzer::fp_IsAttachedComment(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Comment = Tokens[_iToken];
		if (Comment.m_Kind != ECodeTokenKind::mc_BlockComment || Comment.m_bMultiLine)
			return false;

		for (auto i = _iToken + 1; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || (Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine))
				continue;

			if (!fg_IsCodeToken(Token))
				return false;

			for (auto pEnd : {"}", ")", "]", ";", ","})
			{
				if (m_Tokens.f_IsText(Token, pEnd))
					return false;
			}

			return true;
		}

		return false;
	}

	// A block comment on one line behind an operand on its line, with nothing but a separator, closer or terminator behind
	// it, which describes what stands in front of it and goes with it: '2 /* Size */,'.
	bool CFormattingAnalyzer::fp_IsTrailingBlockComment(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Comment = Tokens[_iToken];
		if (Comment.m_Kind != ECodeTokenKind::mc_BlockComment || Comment.m_bMultiLine)
			return false;

		auto fIsInlineComment = [&](aint _iBefore)
			{
				auto const &Before = Tokens[umint(_iBefore)];

				return Before.m_Kind == ECodeTokenKind::mc_BlockComment && !Before.m_bMultiLine;
			}
		;

		auto iBefore = fp_PreviousSignificant(_iToken);
		while (iBefore >= 0 && fIsInlineComment(iBefore))
			iBefore = fp_PreviousSignificant(umint(iBefore));

		if (iBefore < 0 || !fg_IsCodeToken(Tokens[umint(iBefore)]))
			return false;

		// One behind a statement's end or a label trails the statement, which its own rules place, and one behind a separator
		// or an opener describes no operand, nor does one standing for the name a declarator hugs: 'CFoo const &/*_Foo*/)'.
		for (auto pBoundary : {";", "{", "}", ":", ",", "(", "[", "&", "&&", "*"})
		{
			if (m_Tokens.f_IsText(Tokens[umint(iBefore)], pBoundary))
				return false;
		}

		auto iBehind = fp_NextCode(_iToken);
		for (auto iGap = _iToken + 1; iBehind >= 0 && iGap < umint(iBehind); ++iGap)
		{
			auto Kind = Tokens[iGap].m_Kind;
			bool bBlank = Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline;
			if (!bBlank && !(Kind == ECodeTokenKind::mc_BlockComment && !Tokens[iGap].m_bMultiLine))
				return false;
		}

		if (iBehind < 0)
			return false;

		for (auto pEnd : {"}", ")", "]", ";", ","})
		{
			if (m_Tokens.f_IsText(Tokens[umint(iBehind)], pEnd))
				return true;
		}

		return false;
	}

	// A block comment on the line of a declarator, standing for the name the declarator hugs, with the end of the
	// declaration behind it: 'CFoo const &/*_Foo*/)'. It keeps its place and its spacing wherever the declaration's line goes.
	bool CFormattingAnalyzer::fp_IsNameComment(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Comment = Tokens[_iToken];
		if (Comment.m_Kind != ECodeTokenKind::mc_BlockComment || Comment.m_bMultiLine)
			return false;

		auto iBefore = fp_PreviousCode(_iToken);
		auto iBehind = fp_NextCode(_iToken);
		if (iBefore < 0 || iBehind < 0 || fp_SpansLines(umint(iBefore), _iToken))
			return false;

		bool bDeclarator = false;
		for (auto pDeclarator : {"&", "&&", "*"})
			bDeclarator |= m_Tokens.f_IsText(Tokens[umint(iBefore)], pDeclarator);

		bool bEnd = false;
		for (auto pEnd : {")", ",", "=", ";", "["})
			bEnd |= m_Tokens.f_IsText(Tokens[umint(iBehind)], pEnd);

		return bDeclarator && bEnd;
	}

	// True when the range holds nothing that fixes its own line structure.
	bool CFormattingAnalyzer::fp_IsRangeJoinable(umint _iNode, umint _iFirst, umint _iLast) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			// A block in the range, a lambda's body, is laid out on lines of its own, whatever it holds.
			if (i < m_BlockEnds.f_GetLen() && m_BlockEnds[i] <= _iLast)
			{
				i = m_BlockEnds[i];

				continue;
			}

			auto Kind = Tokens[i].m_Kind;

			// A block comment on a line of its own ends its line as a line comment does.
			bool bFixedComment = Kind == ECodeTokenKind::mc_BlockComment && !fp_IsAttachedComment(i) && !fp_IsCommentLine(i) && !fp_IsTrailingBlockComment(i) && !fp_IsNameComment(i);
			if (bFixedComment || (Kind == ECodeTokenKind::mc_Preprocessor && m_OpaqueDirective[i]))
				return false;

			if (Kind == ECodeTokenKind::mc_BlockComment)
				continue;

			// A line break is layout; only a token whose own text spans lines is fixed. A
			// line comment ends its line and so forbids joining across it, which measuring
			// it as wider than any line takes care of, while the lines around it are laid
			// out as usual. A transparent directive ends its line the same way.
			bool bLayout
				= Kind == ECodeTokenKind::mc_Newline
				|| Kind == ECodeTokenKind::mc_LineSplice
				|| Kind == ECodeTokenKind::mc_Whitespace
				|| Kind == ECodeTokenKind::mc_LineComment
				|| Kind == ECodeTokenKind::mc_Preprocessor
			;
			if (bLayout)
				continue;

			if (Tokens[i].m_bMultiLine)
				return false;
		}

		// A braced initializer written across lines, or anything else that fixes its own
		// lines, makes the construct around it unable to render inline. The node flags are
		// propagated from descendants, so the direct children answer for all. A block is
		// the exception: a lambda body inside a group opens on a line of its own, and the
		// construct around it is laid out as a split one.
		// The children stand in order, so the first one that can lie in the range is searched for.
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Children = Nodes[_iNode].m_Children;
		umint iLow = 0;
		umint iHigh = Children.f_GetLen();
		while (iLow < iHigh)
		{
			auto iMiddle = (iLow + iHigh) / 2;
			if (Nodes[Children[iMiddle]].m_iFirstToken < _iFirst)
				iLow = iMiddle + 1;
			else
				iHigh = iMiddle;
		}

		for (auto iIndex = iLow; iIndex < Children.f_GetLen(); ++iIndex)
		{
			auto iChild = Children[iIndex];
			auto const &Child = Nodes[iChild];
			if (Child.m_iFirstToken > _iLast)
				break;

			if (Child.m_iLastToken > _iLast)
				continue;

			bool bFixed
				= Child.m_Kind == ECodeNodeKind::mc_Block
				|| Child.m_Kind == ECodeNodeKind::mc_Unsupported
				|| fp_HasOpaqueDirective(Child.m_iFirstToken, Child.m_iLastToken)
				|| Child.m_bHasMultiLineToken
				|| (iChild < m_KeepsListLines.f_GetLen() && m_KeepsListLines[iChild])
			;
			if (bFixed)
				return false;
		}

		return true;
	}

	bool CFormattingAnalyzer::fp_FitsInline(umint _iFirst, umint _iLast, umint _iIndent) const
	{
		umint nColumns = 0;
		if (!fp_MeasureJoinedWidth(_iFirst, _iLast, nColumns) || nColumns >= gc_nBlockWidth)
			return false;

		// A line comment behind the last token of a range written across lines ends the line the range is joined
		// onto. Behind a range already on one line, its width is the trailing comment rule's to settle, and so is it
		// behind an element whose comma alone stands on the line above, as the comma leads it on its line either way.
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto iComment = _iLast + 1;
		while (iComment < Tokens.f_GetLen() && Tokens[iComment].m_Kind == ECodeTokenKind::mc_Whitespace)
			++iComment;

		auto iSpanFirst = aint(_iFirst);
		if (m_Tokens.f_IsText(Tokens[_iFirst], ","))
			iSpanFirst = fp_NextCode(_iFirst);

		bool bSpans = iSpanFirst >= 0 && umint(iSpanFirst) <= _iLast && fp_SpansLines(umint(iSpanFirst), _iLast);
		if (iComment < Tokens.f_GetLen() && Tokens[iComment].m_Kind == ECodeTokenKind::mc_LineComment && bSpans)
		{
			umint nComment = 0;
			auto pComment = m_pSource + Tokens[iComment].m_iOffset;
			auto nCommentLength = Tokens[iComment].m_nLength;
			while (nCommentLength && fg_IsSpaceOrTab(pComment[nCommentLength - 1]))
				--nCommentLength;

			if (!fg_MeasureTextColumns(pComment, nCommentLength, m_Request.m_Settings.m_nTabWidth, nComment))
				return false;

			nColumns += 1 + nComment;
		}

		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;

		return !nMaxColumns || _iIndent + nColumns <= nMaxColumns;
	}

	// Starts a new line before the token, at the given indentation.
	void CFormattingAnalyzer::fp_BreakBefore(umint _iToken, umint _iIndent)
	{
		m_GapState[_iToken] = uint8(EGap::mc_Break);
		m_GapIndent[_iToken] = _iIndent;
	}

	void CFormattingAnalyzer::fp_BreakAfter(umint _iToken, umint _iIndent)
	{
		auto iNext = fp_NextCode(_iToken);
		if (iNext >= 0)
			fp_BreakBefore(umint(iNext), _iIndent);
	}

	void CFormattingAnalyzer::fp_OwnLineBefore(umint _iToken, umint _iIndent)
	{
		m_GapState[_iToken] = uint8(EGap::mc_OwnLine);
		m_GapIndent[_iToken] = _iIndent;
	}

	void CFormattingAnalyzer::fp_IndentBefore(umint _iToken, umint _iIndent)
	{
		m_GapState[_iToken] = uint8(EGap::mc_Indent);
		m_GapIndent[_iToken] = _iIndent;
	}

	// True when the layout has decided that the token starts a line.
	bool CFormattingAnalyzer::fp_IsBreakGap(umint _iToken) const
	{
		if (_iToken >= m_GapState.f_GetLen())
			return false;

		auto State = EGap(m_GapState[_iToken]);

		return State == EGap::mc_Break || State == EGap::mc_OwnLine || State == EGap::mc_Indent;
	}

	// The range is written on one line: every gap inside it takes its inline spelling.
	void CFormattingAnalyzer::fp_MarkInline(umint _iFirst, umint _iLast)
	{
		for (umint i = _iFirst + 1; i <= _iLast; ++i)
			m_GapState[i] = uint8(EGap::mc_Inline);
	}

	// clang-tidy's NOLINT suppresses the diagnostics of the line it stands on, and NOLINTNEXTLINE those of the line
	// below it. Empty when no comment suppresses any.
	TCVector<uint8> const &CFormattingAnalyzer::fp_GetSuppressedLines() const
	{
		if (m_bSuppressedLinesFound)
			return m_SuppressedLines;

		m_bSuppressedLinesFound = true;

		// Each marker's own line counts, which in a block comment over lines need not be its last.
		auto fMarkComment = [&](umint _iOffset, umint _nLength)
			{
				auto pText = m_pSource + _iOffset;
				auto fHas = [&](ch8 const *_pWord)
					{
						return NStr::fg_StrFindLengthAware<false>(pText, _nLength, _pWord) >= 0;
					}
				;

				if (!fHas("NOLINT") || fHas("NOLINTBEGIN") || fHas("NOLINTEND"))
					return;

				if (m_SuppressedLines.f_IsEmpty())
				{
					m_SuppressedLines.f_SetLen(m_Lines.f_GetLineCount() + 1);
					for (auto &bLine : m_SuppressedLines)
						bLine = 0;
				}

				for (aint iMarker = NStr::fg_StrFindLengthAware<false>(pText, _nLength, "NOLINT"); iMarker >= 0;)
				{
					bool bNextLine = umint(iMarker) + 14 <= _nLength && fg_IsSameText("NOLINTNEXTLINE", pText + iMarker, 14);
					auto iLine = m_Lines.f_FindLine(_iOffset + umint(iMarker));
					m_SuppressedLines[bNextLine ? iLine + 1 : iLine] = 1;

					auto iFrom = umint(iMarker) + 6;
					auto iNext = NStr::fg_StrFindLengthAware<false>(pText + iFrom, _nLength - iFrom, "NOLINT");
					iMarker = iNext < 0 ? aint(-1) : aint(iFrom) + iNext;
				}
			}
		;

		for (auto const &Token : m_Tokens.f_GetTokens())
		{
			if (Token.m_Kind == ECodeTokenKind::mc_LineComment || Token.m_Kind == ECodeTokenKind::mc_BlockComment)
			{
				fMarkComment(Token.m_iOffset, Token.m_nLength);

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			// A directive's comments are part of its token, '#define A 1 // NOLINTNEXTLINE', and its literals are data:
			// '#define Message "NOLINTNEXTLINE"'. A quote inside a number separates digits: '0x7FFF'FFFF', not 'u8'a''.
			auto pText = m_pSource + Token.m_iOffset;
			auto nLength = Token.m_nLength;
			auto fIsWordCharacter = [](ch8 _Character)
				{
					return fg_CharIsAnsiAlphabetical(_Character) || fg_CharIsNumber(_Character) || _Character == '_' || _Character == '\'';
				}
			;

			for (umint i = 0; i < nLength; ++i)
			{
				auto Character = pText[i];
				umint iWord = i;
				while (Character == '\'' && iWord && fIsWordCharacter(pText[iWord - 1]))
					--iWord;

				bool bSeparator = iWord < i && fg_CharIsNumber(pText[iWord]);
				if ((Character == '"' || Character == '\'') && !bSeparator)
				{
					for (++i; i < nLength && pText[i] != Character && pText[i] != '\n'; ++i)
					{
						if (pText[i] == '\\')
							++i;
					}

					continue;
				}

				if (Character != '/' || i + 1 >= nLength || (pText[i + 1] != '/' && pText[i + 1] != '*'))
					continue;

				auto iEnd = nLength;
				if (pText[i + 1] == '*')
				{
					auto iClose = NStr::fg_StrFindLengthAware<false>(pText + i + 2, nLength - i - 2, "*/");
					iEnd = iClose < 0 ? nLength : i + 2 + umint(iClose) + 2;
				}

				fMarkComment(Token.m_iOffset + i, iEnd - i);
				i = iEnd - 1;
			}
		}

		return m_SuppressedLines;
	}

	// A conversion that rewrites text on a suppressed line would move code out of it or onto it.
	bool CFormattingAnalyzer::fp_IsSuppressed(umint _iOffset, umint _nLength) const
	{
		auto const &SuppressedLines = fp_GetSuppressedLines();
		if (SuppressedLines.f_IsEmpty())
			return false;

		auto iLast = m_Lines.f_FindLine(_nLength ? _iOffset + _nLength - 1 : _iOffset);
		for (auto iLine = m_Lines.f_FindLine(_iOffset); iLine <= iLast; ++iLine)
		{
			if (SuppressedLines[iLine])
				return true;
		}

		return false;
	}

	// The code on a suppressed line is neither split nor joined with another line's.
	void CFormattingAnalyzer::fp_KeepSuppressedLines()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &SuppressedLines = fp_GetSuppressedLines();
		if (SuppressedLines.f_IsEmpty())
			return;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto State = EGap(m_GapState[i]);
			if (State == EGap::mc_Keep || State == EGap::mc_Indent || !fg_IsCodeToken(Tokens[i]))
				continue;

			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious < 0)
				continue;

			auto iPreviousLine = m_Lines.f_FindLine(Tokens[umint(iPrevious)].m_iOffset);
			auto iLine = m_Lines.f_FindLine(Tokens[i].m_iOffset);
			bool bJoins = State == EGap::mc_Inline && iLine != iPreviousLine && (SuppressedLines[iLine] || SuppressedLines[iPreviousLine]);
			bool bSplits = State != EGap::mc_Inline && iLine == iPreviousLine && SuppressedLines[iLine];
			if (bJoins || bSplits)
				m_GapState[i] = uint8(EGap::mc_Keep);
		}
	}

	// Writes the layout decisions out as edits. A gap that keeps what the source has, one an
	// opaque directive stands in, and an inline gap the source already writes on one line
	// are left to the other rules. A gap holding a comment or a transparent directive keeps
	// its lines, and those lines their own text, so only the indentation the token starts
	// its line at is written.
	void CFormattingAnalyzer::fp_EmitLayout()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());

		auto fIsBlockBrace = [&](umint _iToken, bool _bOpening)
			{
				auto iNode = _bOpening ? m_Structure.f_FindNodeOpeningAt(_iToken) : m_Structure.f_FindNodeClosingAt(_iToken);

				return iNode >= Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Group;
			}
		;

		// A blank line cuts what continues across it: a line ending in an opening bracket or an operator, or one
		// followed by the closing bracket of what it stands in. Between a list's elements, statements, and a
		// block's braces it separates, and stays.
		auto fContinues = [&](umint _iPrevious, umint _iNext)
			{
				auto const &Previous = Tokens[_iPrevious];
				auto const &Next = Tokens[_iNext];
				bool bCloses
					= Next.m_Kind == ECodeTokenKind::mc_Punctuator
					&& (m_Tokens.f_IsText(Next, ")") || m_Tokens.f_IsText(Next, "]") || (m_Tokens.f_IsText(Next, "}") && !fIsBlockBrace(_iNext, false)))
				;
				if (bCloses)
					return !m_Tokens.f_IsText(Previous, ",") && !m_Tokens.f_IsText(Previous, ";");

				if (Previous.m_Kind != ECodeTokenKind::mc_Punctuator)
					return false;

				if (m_Tokens.f_IsText(Previous, "{"))
					return !fIsBlockBrace(_iPrevious, true);

				return !m_Tokens.f_IsText(Previous, ")")
					&& !m_Tokens.f_IsText(Previous, "]")
					&& !m_Tokens.f_IsText(Previous, "}")
					&& !m_Tokens.f_IsText(Previous, ";")
					&& !m_Tokens.f_IsText(Previous, ",")
					&& !m_Tokens.f_IsText(Previous, ":")
					&& !m_Tokens.f_IsText(Previous, ">")
				;
			}
		;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];

			// Only a code token owns the gap in front of it; a range's marks also land on
			// the trivia inside it.
			switch (Token.m_Kind)
			{
			case ECodeTokenKind::mc_ByteOrderMark:
			case ECodeTokenKind::mc_Whitespace:
			case ECodeTokenKind::mc_Newline:
			case ECodeTokenKind::mc_LineSplice:
			case ECodeTokenKind::mc_LineComment:
			case ECodeTokenKind::mc_BlockComment:
			case ECodeTokenKind::mc_Preprocessor:
				continue;
			default: break;
			}

			auto State = EGap(m_GapState[i]);
			auto nGapIndent = m_GapIndent[i];
			if (State == EGap::mc_Keep && !fp_IsFirstOnLine(i))
				continue;

			// The first code in the file has no gap in front of it to write, only its line's indentation, which a gap the
			// layout kept leaves as the source wrote it.
			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious < 0)
			{
				if (State == EGap::mc_Inline || State == EGap::mc_Keep)
					continue;

				auto iLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Token.m_iOffset));
				bool bIndentation = true;
				for (auto iByte = iLineStart; iByte < Token.m_iOffset && bIndentation; ++iByte)
					bIndentation = fg_IsSpaceOrTab(m_pSource[iByte]);

				auto Indent = fp_MakeIndent(nGapIndent);
				auto nLength = Token.m_iOffset - iLineStart;
				if (bIndentation && !fg_IsSameText(Indent, m_pSource + iLineStart, nLength))
					fp_AddEdit("line-break", iLineStart, nLength, Indent, "a statement stands at its scope's depth");

				continue;
			}

			bool bNewline = false;
			umint nNewlines = 0;
			bool bKeepLines = false;
			bool bOwned = false;
			for (umint iGap = umint(iPrevious) + 1; iGap < i; ++iGap)
			{
				auto Kind = Tokens[iGap].m_Kind;
				if (Kind == ECodeTokenKind::mc_Newline)
				{
					bNewline = true;
					++nNewlines;
				}
				else if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
					bKeepLines = true;
				else if (Kind == ECodeTokenKind::mc_Preprocessor)
				{
					// Behind an opaque directive the line only takes the indentation a block it moves with gives it.
					if (m_OpaqueDirective[iGap] && State != EGap::mc_Indent)
						bOwned = true;
					else
						bKeepLines = true;
				}
				else if (Kind != ECodeTokenKind::mc_Whitespace)
					bOwned = true;
			}

			if (bOwned)
				continue;

			bool bBlankInStatement = nNewlines > 1 && !bKeepLines && fContinues(umint(iPrevious), i);
			if (State == EGap::mc_Keep)
			{
				if (bBlankInStatement)
				{
					auto iBlank = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[umint(iPrevious)].f_GetEnd() - 1) + 1);
					auto iTokenLine = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Token.m_iOffset));
					fp_AddEdit("blank-line", iBlank, iTokenLine - iBlank, {}, "no blank line cuts a construct continued on the next line");
				}

				continue;
			}

			auto iStart = Tokens[umint(iPrevious)].f_GetEnd();
			auto nLength = Token.m_iOffset - iStart;

			// A gap holding a comment or a directive keeps its lines, and one keeping its
			// lines keeps any blank lines too; only the indentation of the line the token
			// starts moves, so the edit covers that indentation alone.
			auto fKeepLines = [&]
				{
					auto pGap = m_pSource + iStart;

					// The line break is the gap's own, never one inside a block comment in it: behind '*/' on the
					// token's line there is no indentation to write.
					umint nFloor = 0;
					for (auto iGap = umint(iPrevious) + 1; iGap < i; ++iGap)
					{
						auto const &Gap = Tokens[iGap];
						auto GapKind = Gap.m_Kind;
						bool bLayout = GapKind == ECodeTokenKind::mc_Whitespace || GapKind == ECodeTokenKind::mc_Newline || GapKind == ECodeTokenKind::mc_LineSplice;
						if (Gap.m_bMultiLine && !bLayout)
							nFloor = Gap.f_GetEnd() - iStart;
					}

					umint nKeep = nLength;
					for (; nKeep > nFloor; --nKeep)
					{
						auto Char = pGap[nKeep - 1];
						if (Char == '\n' || Char == '\r')
							break;
					}

					if (nKeep == nFloor && nFloor)
					{
						iStart += nKeep;
						nLength = 0;

						return CStr();
					}

					// A comment on the token's own line stays in front of it, so only the
					// blanks between the line break and that comment are the indentation.
					umint nIndent = 0;
					for (; nKeep + nIndent < nLength; ++nIndent)
					{
						auto Char = pGap[nKeep + nIndent];
						if (Char != ' ' && Char != '\t')
							break;
					}

					iStart += nKeep;
					nLength = nIndent;

					return fp_MakeIndent(nGapIndent);
				}
			;

			// Comments the token names, '/*bFlag=*/true', move with it: only what stands in front of the first is the gap.
			umint iAttached = TCLimitsInt<umint>::mc_Max;
			bool bAttachedOnly = bKeepLines;
			for (umint iGap = umint(iPrevious) + 1; iGap < i && bAttachedOnly; ++iGap)
			{
				auto Kind = Tokens[iGap].m_Kind;
				if (Kind == ECodeTokenKind::mc_BlockComment && iAttached == TCLimitsInt<umint>::mc_Max)
				{
					bAttachedOnly = fp_IsAttachedComment(iGap);
					iAttached = iGap;
				}
				else if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline && Kind != ECodeTokenKind::mc_BlockComment)
					bAttachedOnly = false;
			}

			bAttachedOnly = bAttachedOnly && iAttached != TCLimitsInt<umint>::mc_Max;

			CStr Replacement;
			CStr Explanation;

			// A break the source already has keeps its lines as any other gap holding a comment does.
			bool bAttachedJoin = State == EGap::mc_Inline && bNewline;
			bool bAttachedBreak = (State == EGap::mc_Break || State == EGap::mc_OwnLine) && !bNewline;
			if (bAttachedOnly && (bAttachedJoin || bAttachedBreak))
			{
				nLength = Tokens[iAttached].m_iOffset - iStart;

				if (State == EGap::mc_Inline)
				{
					auto Spacing = fp_GetInlineSpacing(umint(iPrevious), i);
					if (Spacing == ECodeSpacing::mc_Preserve)
						continue;

					Replacement = Spacing == ECodeSpacing::mc_Space ? " " : "";
					Explanation = "the construct fits on one line";
				}
				else
				{
					Replacement = (m_BlankBefore[i] && !bBlankInStatement ? Ending + Ending : Ending) + fp_MakeIndent(nGapIndent);
					Explanation = State == EGap::mc_Break ? "a split construct puts this on its own line" : "a block's braces and each of its statements take a line of their own";
				}

				if (!fg_IsSameText(Replacement, m_pSource + iStart, nLength))
					fp_AddEdit("line-break", iStart, nLength, Replacement, Explanation);

				continue;
			}

			// Comments that trail the code in front, '2 /* Size */', or stand for its name, 'int */*_Size*/', stay behind it
			// where the token behind them is moved to a line of its own: only what stands behind the last is the gap.
			if ((State == EGap::mc_Break || State == EGap::mc_OwnLine) && bKeepLines && !bNewline)
			{
				umint iTrailingEnd = 0;
				bool bTrailingOnly = true;
				for (umint iGap = umint(iPrevious) + 1; iGap < i && bTrailingOnly; ++iGap)
				{
					auto Kind = Tokens[iGap].m_Kind;
					if (Kind == ECodeTokenKind::mc_BlockComment && (fp_IsTrailingBlockComment(iGap) || fp_IsNameComment(iGap)))
						iTrailingEnd = Tokens[iGap].f_GetEnd();
					else
						bTrailingOnly = Kind == ECodeTokenKind::mc_Whitespace;
				}

				if (bTrailingOnly && iTrailingEnd)
				{
					Replacement = Ending + fp_MakeIndent(nGapIndent);
					if (!fg_IsSameText(Replacement, m_pSource + iTrailingEnd, Token.m_iOffset - iTrailingEnd))
						fp_AddEdit("line-break", iTrailingEnd, Token.m_iOffset - iTrailingEnd, Replacement, "a split construct puts this on its own line");
				}

				continue;
			}

			if (State == EGap::mc_Break || State == EGap::mc_OwnLine)
			{
				if (bKeepLines && !bNewline)
					continue;

				Replacement = bKeepLines ? fKeepLines() : (m_BlankBefore[i] && !bBlankInStatement ? Ending + Ending : Ending) + fp_MakeIndent(nGapIndent);
				Explanation = State == EGap::mc_Break ? "a split construct puts this on its own line" : "a block's braces and each of its statements take a line of their own";
			}
			else if (State == EGap::mc_Indent)
			{
				if (!bNewline)
					continue;

				Replacement = bBlankInStatement ? Ending + fp_MakeIndent(nGapIndent) : fKeepLines();
				Explanation = "the body's lines move with its brace";
			}

			else
			{
				if (!bNewline || bKeepLines)
					continue;

				// Joined the way it was measured: the operator a spacing rule writes apart,
				// 'cA<t_C> || cB<t_C>', is apart here too.
				auto Spacing = fp_GetJoinSpacing(umint(iPrevious), i);
				if (Spacing == ECodeSpacing::mc_Preserve)
					continue;

				Replacement = Spacing == ECodeSpacing::mc_Space ? " " : "";
				Explanation = "the construct fits on one line";
			}

			if (fg_IsSameText(Replacement, m_pSource + iStart, nLength))
				continue;

			fp_AddEdit("line-break", iStart, nLength, Replacement, Explanation);
		}

		// A run of comment lines above code stands where that code does, blank lines and other runs between them or not;
		// in front of a block's closing brace it stands at the block's statements' level. One continuing a trailing comment
		// and one a directive parts from the code stay where they are.
		{
			auto nTab = m_Request.m_Settings.m_nTabWidth;

			auto fCommentLine = [&](umint _iToken)
				{
					auto Kind = Tokens[_iToken].m_Kind;

					return (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment) && fp_IsFirstOnLine(_iToken) && !fp_IsCommentedDirective(_iToken);
				}
			;

			auto fNewlines = [&](umint _iFrom, umint _iTo)
				{
					umint nNewlines = 0;
					for (auto iGap = _iFrom + 1; iGap < _iTo; ++iGap)
					{
						if (Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline)
							++nNewlines;
						else if (Tokens[iGap].m_Kind != ECodeTokenKind::mc_Whitespace)
							return TCLimitsInt<umint>::mc_Max;
					}

					return nNewlines;
				}
			;

			// The next token that is no blank, and the one in front of a token that is no blank.
			auto fNextFilled = [&](umint _iToken) -> aint
				{
					for (auto iNext = _iToken + 1; iNext < Tokens.f_GetLen(); ++iNext)
					{
						auto Kind = Tokens[iNext].m_Kind;
						if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
							return aint(iNext);
					}

					return -1;
				}
			;

			auto fPreviousFilled = [&](umint _iToken) -> aint
				{
					for (auto iPrevious = aint(_iToken) - 1; iPrevious >= 0; --iPrevious)
					{
						auto Kind = Tokens[umint(iPrevious)].m_Kind;
						if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
							return iPrevious;
					}

					return -1;
				}
			;

			// The trailing comment the lines above continue, and the last of them; the first line below them that does not
			// continue it starts a run of its own.
			aint iContinuedTrailing = -1;
			aint iLastContinuation = -1;
			for (umint i = 0; i < Tokens.f_GetLen(); ++i)
			{
				if (!fCommentLine(i))
					continue;

				auto iBefore = fPreviousFilled(i);
				if (iBefore >= 0 && fCommentLine(umint(iBefore)) && fNewlines(umint(iBefore), i) == 1)
				{
					if (iBefore != iLastContinuation)
						continue;

					if (fp_IsContinuationComment(i) && fp_GetSourceLineIndent(i) > fp_GetTrailedCodeIndent(umint(iContinuedTrailing)))
					{
						iLastContinuation = aint(i);

						continue;
					}
				}

				// A comment written further in than the code a trailing line comment above it trails continues that one, also
				// behind the blank line a rule puts behind a type where it is further in than the code the layout lays out
				// there too, as the next pass reads it. Code the layout gives another start is measured where it starts.
				auto fLaidOutLineStart = [&](umint _iTrailing)
					{
						auto iLineStart = fp_PreviousCode(_iTrailing);
						while
						(
							iLineStart > 0
							&& !fp_IsBreakGap(umint(iLineStart))
							&& (EGap(m_GapState[umint(iLineStart)]) == EGap::mc_Inline || (!fp_IsFirstOnLine(umint(iLineStart)) && !fp_StartsLineBehindComments(umint(iLineStart))))
						)
						{
							iLineStart = fp_PreviousCode(umint(iLineStart));
						}

						return iLineStart;
					}
				;

				auto fLaidOutLineIndent = [&](umint _iTrailing)
					{
						auto iLineStart = fLaidOutLineStart(_iTrailing);
						if (iLineStart < 0)
							return umint(0);

						return fp_GetStatementIndent(umint(iLineStart)) + (fp_StartsSharedCaseLabel(umint(iLineStart), _iTrailing) ? nTab : 0);
					}
				;

				// The line the layout gives the code another start, joined or split.
				auto fIsReshaped = [&](umint _iTrailing)
					{
						auto iLineStart = fLaidOutLineStart(_iTrailing);
						auto iSourceFirst = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[_iTrailing].m_iOffset)));
						while (iSourceFirst < _iTrailing && !fg_IsCodeToken(Tokens[iSourceFirst]))
							++iSourceFirst;

						return iLineStart >= 0 && umint(iLineStart) != iSourceFirst;
					}
				;

				bool bContinues
					= iBefore >= 0
					&& fp_IsContinuationComment(i)
					&& Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_LineComment
					&& !fp_IsFirstOnLine(umint(iBefore))
					&&
					(
						(
							fp_GetSourceLineIndent(i) > fp_GetTrailedCodeIndent(umint(iBefore))
							&& (fNewlines(umint(iBefore), i) == 1 || fp_GetSourceLineIndent(i) > fLaidOutLineIndent(umint(iBefore)))
						)
						|| (fIsReshaped(umint(iBefore)) && fp_GetSourceLineIndent(i) > fLaidOutLineIndent(umint(iBefore)))
					)
					&& !fp_StartsBodyBehind(umint(iBefore), fp_GetSourceLineIndent(i))
				;
				if (bContinues)
				{
					iContinuedTrailing = iBefore;
					iLastContinuation = aint(i);

					continue;
				}

				TCVector<umint> Run;
				Run.f_Insert(i);
				auto iLast = i;

				// Code a comment stands in front of on its line places nothing above it: 'LINT */ class C19'.
				auto fStartsCodeLine = [&](umint _iToken)
					{
						auto iPrevious = fp_PreviousSignificant(_iToken);

						return fg_IsCodeToken(Tokens[_iToken]) && (iPrevious < 0 || Tokens[umint(iPrevious)].m_Kind == ECodeTokenKind::mc_Newline);
					}
				;

				for (auto iNext = fNextFilled(iLast); iNext >= 0 && fNewlines(iLast, umint(iNext)) == 1 && fCommentLine(umint(iNext)); iNext = fNextFilled(iLast))
				{
					Run.f_Insert(umint(iNext));
					iLast = umint(iNext);
				}

				aint iCode = fp_FindCodeBelowComments(iLast);

				// A comment with code behind it on its last line is no comment line, and the comment lines in front of a list's
				// or a call's closing marker stand among its elements by a rule of their own. A line comment behind it is no code:
				// '}*/ // Not supported'.
				bool bCodeBehind = false;
				for (auto iComment : Run)
				{
					auto iBehind = fp_NextSignificant(iComment);
					bool bLineComment = iBehind >= 0 && Tokens[umint(iBehind)].m_Kind == ECodeTokenKind::mc_LineComment;
					bCodeBehind = bCodeBehind || (!fp_IsLastOnLine(iComment) && !bLineComment);
				}

				auto iClosedGroup = iCode >= 0 ? m_Structure.f_FindNodeClosingAt(umint(iCode)) : Nodes.f_GetLen();
				bool bGroupCloser = iClosedGroup < Nodes.f_GetLen() && Nodes[iClosedGroup].m_Kind == ECodeNodeKind::mc_Group;

				// Outside functions, in front of a directive other than a conditional's or a pragma, which go with the code
				// behind them, or the file's end, the run stands where the statement above it does.
				aint iAbove = -1;
				if (iCode < 0)
				{
					auto iStop = fNextFilled(iLast);
					while (iStop >= 0 && fCommentLine(umint(iStop)))
						iStop = fNextFilled(umint(iStop));

					auto iPrevious = fp_PreviousCode(i);
					auto iStatement = iPrevious >= 0 ? m_Structure.f_FindStatementEndingAt(umint(iPrevious)) : Nodes.f_GetLen();

					bool bOpenEnd = iStop < 0;
					if (iStop >= 0 && Tokens[umint(iStop)].m_Kind == ECodeTokenKind::mc_Preprocessor)
					{
						auto Keyword = fp_GetDirectiveKeyword(umint(iStop));
						bOpenEnd = !Keyword.f_StartsWith("if") && !Keyword.f_StartsWith("el") && Keyword != "endif" && Keyword != "pragma";
					}

					// The statement is measured from the line its clause starts, the one a block or a body belongs to: 'if (A) {'
					// or 'if (A)' over 'return B;'.
					auto fLineFirst = [&](umint _iToken)
						{
							auto iLineFirst = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[_iToken].m_iOffset)));
							while (iLineFirst < _iToken && !fg_IsCodeToken(Tokens[iLineFirst]))
								++iLineFirst;

							return iLineFirst;
						}
					;

					// Inside a function the directives are its code's, whose own rules place the comments around them.
					bool bInFunction = false;
					for (auto iNode = iStatement; !bInFunction && iNode < Nodes.f_GetLen() && iNode; iNode = Nodes[iNode].m_iParent)
						bInFunction = fg_IsFunctionBody(m_Tokens, m_Structure, iNode);

					// A statement ended by its terminator or its block, not a macro's call the structure cannot delimit.
					auto const *pPrevious = iPrevious >= 0 ? &Tokens[umint(iPrevious)] : nullptr;
					bool bEnded = pPrevious && (m_Tokens.f_IsText(*pPrevious, ";") || m_Tokens.f_IsText(*pPrevious, "}"));
					if (bOpenEnd && bEnded && !bInFunction && iStatement < Nodes.f_GetLen())
					{
						auto iLineFirst = fLineFirst(Nodes[iStatement].m_iFirstToken);
						for (auto iInFront = fp_PreviousCode(iLineFirst); iInFront >= 0; iInFront = fp_PreviousCode(iLineFirst))
						{
							auto const &InFront = Tokens[umint(iInFront)];
							bool bEnds
								= m_Tokens.f_IsText(InFront, ";")
								|| m_Tokens.f_IsText(InFront, "{")
								|| m_Tokens.f_IsText(InFront, "}")
								|| m_Tokens.f_IsText(InFront, ":")
								|| InFront.m_Kind == ECodeTokenKind::mc_Preprocessor
							;
							if (bEnds)
								break;

							iLineFirst = fLineFirst(umint(iInFront));
						}

						if (fStartsCodeLine(iLineFirst))
							iAbove = aint(iLineFirst);
					}
				}

				if ((iCode < 0 && iAbove < 0) || bCodeBehind || bGroupCloser)
				{
					i = iLast;

					continue;
				}

				// In front of a block's closing brace the run stands where the block's last statement above it does, labels
				// and clause bodies standing out of it and a case body on its label's line one level in from the label, one
				// level in from the brace where none does.
				umint nTarget = fp_GetStatementIndent(umint(iCode >= 0 ? iCode : iAbove));
				auto iClosed = iCode >= 0 ? m_Structure.f_FindNodeClosingAt(umint(iCode)) : Nodes.f_GetLen();
				if (iCode >= 0 && m_Tokens.f_IsText(Tokens[umint(iCode)], "}") && iClosed < Nodes.f_GetLen() && Nodes[iClosed].m_Kind == ECodeNodeKind::mc_Block)
				{
					nTarget += nTab;

					for (auto iStatement : Nodes[iClosed].m_Children)
					{
						auto const &Statement = Nodes[iStatement];
						if (Statement.m_iLastToken >= i || m_Tokens.f_IsText(Tokens[Statement.m_iLastToken], ":"))
							continue;

						// A clause's body is its clause's.
						auto iPrevious = fp_PreviousCode(Statement.m_iFirstToken);
						auto const *pPrevious = iPrevious >= 0 ? &Tokens[umint(iPrevious)] : nullptr;
						if
						(
							pPrevious
							&& !m_Tokens.f_IsText(*pPrevious, ";")
							&& !m_Tokens.f_IsText(*pPrevious, "{")
							&& !m_Tokens.f_IsText(*pPrevious, "}")
							&& !m_Tokens.f_IsText(*pPrevious, ":")
						)
						{
							continue;
						}

						if (auto iLabel = fp_FindSharedCaseLabel(Statement.m_iFirstToken); iLabel >= 0)
							nTarget = fp_GetStatementIndent(umint(iLabel)) + nTab;
						else if (fp_IsFirstOnLine(Statement.m_iFirstToken) && EGap(m_GapState[Statement.m_iFirstToken]) != EGap::mc_Inline)
							nTarget = fp_GetStatementIndent(Statement.m_iFirstToken);
					}
				}

				// In front of a label the run ends the body above it or introduces the label, and stands at the body's level or
				// the label's, as the source has it deeper than the label or not; the first label in a block it introduces.
				if (iCode >= 0)
				{
					auto iColon = fp_NextCode(umint(iCode));
					auto const &Code = Tokens[umint(iCode)];
					bool bLabel
						= m_Tokens.f_IsText(Code, "case")
						|| m_Tokens.f_IsText(Code, "default")
						|| m_Tokens.f_IsText(Code, "public")
						|| m_Tokens.f_IsText(Code, "private")
						|| m_Tokens.f_IsText(Code, "protected")
						|| (Code.m_Kind == ECodeTokenKind::mc_Identifier && iColon >= 0 && m_Tokens.f_IsText(Tokens[umint(iColon)], ":"))
					;

					// Behind a block's opening brace there is no body above to end: the run introduces a switch's first case, and
					// stands among a class's members, as an access label stands at the brace's level.
					bool bCase = m_Tokens.f_IsText(Code, "case") || m_Tokens.f_IsText(Code, "default");
					bool bBehindOpening = iBefore >= 0 && m_Tokens.f_IsText(Tokens[umint(iBefore)], "{");
					if (bLabel && !bCase && bBehindOpening)
						nTarget += nTab;
					else if (bLabel && iBefore >= 0 && !bBehindOpening && fp_GetSourceLineIndent(i) > fp_GetSourceLineIndent(umint(iCode)))
						nTarget += nTab;
				}

				// Under a label, also behind the blank line the layout takes out there, the run written deeper than it is the
				// label's body, one level in from it, whatever follows: 'case 1:' over '// Handled elsewhere' over 'default:'.
				if (iBefore >= 0 && m_Tokens.f_IsText(Tokens[umint(iBefore)], ":") && fNewlines(umint(iBefore), i) >= 1)
				{
					auto iLabel = m_Structure.f_FindStatementEndingAt(umint(iBefore));
					bool bLabel = iLabel < Nodes.f_GetLen() && Nodes[iLabel].m_iLastToken == umint(iBefore);
					if (bLabel && fp_GetSourceLineIndent(i) > fp_GetSourceLineIndent(Nodes[iLabel].m_iFirstToken))
						nTarget = fp_GetStatementIndent(Nodes[iLabel].m_iFirstToken) + nTab;
				}

				// Under a trailing line comment a comment further in than the code it trails continues it, so a run that
				// does not stands no further in than that code: 'Derived(). // RUN:' kept where the source has it.
				if
				(
					iBefore >= 0
					&& fp_IsContinuationComment(i)
					&& Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_LineComment
					&& !fp_IsFirstOnLine(umint(iBefore))
					&& fp_PreviousCode(umint(iBefore)) >= 0
					&& !m_Tokens.f_IsText(Tokens[umint(fp_PreviousCode(umint(iBefore)))], "{")
				)
				{
					nTarget = fg_Min(nTarget, fLaidOutLineIndent(umint(iBefore)));
				}

				// Every line of the run stands there, its text's own indentation behind the markers. One the layout gave a line
				// of its own is where the layout placed it, behind a label among others.
				for (auto iComment : Run)
				{
					if (m_ProtectedStart[m_Lines.f_FindLine(Tokens[iComment].m_iOffset)] || fp_IsBreakGap(iComment))
						continue;

					m_CommentMoved[iComment] = 2;
					m_CommentIndent[iComment] = nTarget;
				}

				i = iLast;
			}
		}

		// A directive taken out of the code stands where directives do.
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (fp_IsCommentedDirective(i) && !m_ProtectedStart[m_Lines.f_FindLine(Tokens[i].m_iOffset)])
			{
				m_CommentMoved[i] = 2;
				m_CommentIndent[i] = 0;
			}
		}

		auto fIsInlineComment = [&](aint _iBehind)
			{
				auto const &Behind = Tokens[umint(_iBehind)];

				return Behind.m_Kind == ECodeTokenKind::mc_BlockComment && !Behind.m_bMultiLine;
			}
		;

		// A comment standing on a line of its own belongs to the block around it, and its
		// line is written at the depth that block moved to.
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (!m_CommentMoved[i])
				continue;

			// A line the code behind the comment starts is written where that code is placed.
			auto iBehind = fp_NextSignificant(i);
			while (iBehind >= 0 && fIsInlineComment(iBehind))
				iBehind = fp_NextSignificant(umint(iBehind));

			if (iBehind >= 0 && fg_IsCodeToken(Tokens[umint(iBehind)]) && fp_IsBreakGap(umint(iBehind)) && !fp_SpansLines(i, umint(iBehind)))
				continue;

			// So is one the code a comment names on it is joined onto the line in front with, the comment going along.
			if (iBehind >= 0 && fg_IsCodeToken(Tokens[umint(iBehind)]) && EGap(m_GapState[umint(iBehind)]) == EGap::mc_Inline && fp_IsAttachedComment(i))
				continue;

			auto iLine = m_Lines.f_FindLine(Tokens[i].m_iOffset);
			if (m_ProtectedStart[iLine])
				continue;

			auto nDelta = aint(m_CommentIndent[i]) - aint(fp_GetSourceLineIndent(i));
			if (!fp_CanShiftCommentLines(i, nDelta))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto nLength = Tokens[i].m_iOffset - iStart;
			CStr Replacement = fp_MakeIndent(m_CommentIndent[i]);
			if (fg_IsSameText(Replacement, m_pSource + iStart, nLength))
				continue;

			auto pExplanation = m_CommentMoved[i] == 2 ? "a comment line stands where the code under it does" : "the body's lines move with its brace";
			fp_AddEdit("line-break", iStart, nLength, Replacement, pExplanation);
			fp_ShiftCommentLines(i, nDelta, "line-break", pExplanation);
		}
	}

	bool CFormattingAnalyzer::fp_LayoutGroup(umint _iNode, umint _iIndent, bool _bBreakBefore)
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_Kind != ECodeNodeKind::mc_Group)
			return false;

		// An empty group has nothing to put on its own line. Reporting that keeps the
		// statement from being treated as split, which would strand its terminator on a
		// line of its own without making anything fit.
		// Nor has one holding a name alone in front of what opens instead: 'NAME(Begin)' over '(' over the arguments of
		// the call on what it yields, or 'DPhase(Destroy)' in front of the declaration it marks. A clause's condition is
		// opened all the same.
		// A list written open with nothing in it yet stands open: '{' over '}'.
		auto iInner = fp_NextCode(Node.m_iFirstToken);
		if ((iInner < 0 || umint(iInner) == Node.m_iLastToken) && Node.m_bWrittenOpen && _iNode < m_KeepsListLines.f_GetLen() && !m_KeepsListLines[_iNode])
		{
			if (_bBreakBefore)
				fp_BreakBefore(Node.m_iFirstToken, _iIndent);

			fp_BreakBefore(Node.m_iLastToken, _iIndent);

			return true;
		}

		if (iInner < 0 || umint(iInner) == Node.m_iLastToken)
			return false;

		if (fp_HoldsNameAlone(_iNode))
		{
			auto const &Tokens = m_Tokens.f_GetTokens();
			auto iBehind = fp_NextCode(Node.m_iLastToken);
			auto iClause = fp_PreviousCode(Node.m_iFirstToken);
			auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
			bool bOpensBehind = pBehind && (m_Tokens.f_IsText(*pBehind, "(") || m_Tokens.f_IsText(*pBehind, "{") || pBehind->m_Kind == ECodeTokenKind::mc_Identifier);

			bool bCondition = false;
			for (auto pClause : {"if", "for", "while", "switch", "catch"})
				bCondition |= iClause >= 0 && m_Tokens.f_IsText(Tokens[umint(iClause)], pClause);

			if (bOpensBehind && !bCondition)
				return false;
		}

		if (_bBreakBefore)
			fp_BreakBefore(Node.m_iFirstToken, _iIndent);

		fp_BreakAfter(Node.m_iFirstToken, _iIndent + m_Request.m_Settings.m_nTabWidth);

		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto iSplit : Node.m_SplitPoints)
		{
			fp_BreakBefore(iSplit, _iIndent + m_Request.m_Settings.m_nTabWidth);

			// A blank line between the elements of a list written open sets them apart, on whichever side of the comma the
			// source wrote it, except among a DSL's data.
			if (!Node.m_bWrittenOpen || iSplit >= m_BlankBefore.f_GetLen() || fp_IsDSLData(_iNode))
				continue;

			auto iBefore = fp_PreviousCode(iSplit);
			auto iBehind = fp_NextCode(iSplit);

			auto fBlank = [&](aint _iFrom, aint _iTo)
				{
					umint nNewlines = 0;
					for (auto i = _iFrom + 1; _iFrom >= 0 && i < _iTo; ++i)
					{
						auto Kind = Tokens[umint(i)].m_Kind;
						if (Kind == ECodeTokenKind::mc_Newline)
							++nNewlines;
						else if (Kind != ECodeTokenKind::mc_Whitespace)
							return false;
					}

					return nNewlines > 1;
				}
			;

			if (fBlank(iBefore, aint(iSplit)) || (iBehind >= 0 && fBlank(aint(iSplit), iBehind)))
				m_BlankBefore[iSplit] = 1;
		}

		fp_BreakBefore(Node.m_iLastToken, _iIndent);
		fp_LayoutElements(_iNode, _iIndent + m_Request.m_Settings.m_nTabWidth);

		return true;
	}

	// Whether the range, written across lines, is broken only inside the groups it holds, which join with what they belong
	// to where it fits on a line, as the next pass reads one written on one line: 'std::regex(' over 'R"(...)")'.
	bool CFormattingAnalyzer::fp_IsBrokenInsideGroups(umint _iNode, umint _iFirst, umint _iLast) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &AllTokens = m_Tokens.f_GetTokens();
		bool bInsideGroups = true;
		for (auto i = _iFirst + 1; i < _iLast && bInsideGroups; ++i)
		{
			auto Kind = AllTokens[i].m_Kind;
			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor)
				bInsideGroups = false;
			else if (Kind == ECodeTokenKind::mc_Newline && m_Structure.f_FindEnclosingNode(i) == _iNode)
			{
				// Nor does a cast's operand stand apart from the cast: '(GenericFnPtr)' over '(void (*)(void *))&::f'.
				// A call's parenthesis and a template argument list join with the name in front of them, and the
				// parenthesis with the list: 'static_cast' over '<std::size_t>' over '(p - start)'.
				auto iCast = fp_PreviousCode(i);
				auto const *pCast = iCast >= 0 ? &AllTokens[umint(iCast)] : nullptr;
				auto iCastGroup = pCast && m_Tokens.f_IsText(*pCast, ")") ? m_Structure.f_FindNodeClosingAt(umint(iCast)) : Nodes.f_GetLen();
				auto iCall = fp_NextCode(i);
				auto const *pCall = iCall >= 0 ? &AllTokens[umint(iCall)] : nullptr;
				bool bOwner = pCast && (pCast->m_Kind == ECodeTokenKind::mc_Identifier || (m_Tokens.f_IsText(*pCast, ">") && m_Structure.f_IsAngleBracket(umint(iCast))));
				bool bOpener
					= pCall
					&& (m_Tokens.f_IsText(*pCall, "(") || (m_Tokens.f_IsText(*pCall, "<") && m_Structure.f_IsAngleBracket(umint(iCall))))
					&& m_Structure.f_FindNodeOpeningAt(umint(iCall)) < Nodes.f_GetLen()
				;
				bool bCall = bOwner && bOpener;

				// So does a member access or a qualification with what it is applied to: 'Saver' over '.save(...)'.
				bool bOperand
					= pCast
					&&
					(
						pCast->m_Kind == ECodeTokenKind::mc_Identifier
						|| m_Tokens.f_IsText(*pCast, ")")
						|| m_Tokens.f_IsText(*pCast, "]")
						|| (m_Tokens.f_IsText(*pCast, ">") && m_Structure.f_IsAngleBracket(umint(iCast)))
					)
				;
				bool bAccess = bOperand && pCall && (m_Tokens.f_IsText(*pCall, ".") || m_Tokens.f_IsText(*pCall, "->") || m_Tokens.f_IsText(*pCall, "::"));
				bInsideGroups = bCall || bAccess || (iCastGroup < Nodes.f_GetLen() && fp_IsCastGroup(iCastGroup));
			}
		}

		return bInsideGroups;
	}

	// The last token of the attributes a declaration starts with, specifiers among or in front of them included, or -1
	// where it starts with none or nothing follows them: '__attribute__((weak)) [[nodiscard]]' in front of 'void f()'.
	aint CFormattingAnalyzer::fp_FindLeadingAttributesEnd(umint _iFirst, umint _iLast) const
	{
		constexpr ch8 const *c_pSpecifiers[] =
			{
				"extern"
				, "static"
				, "inline"
				, "__inline"
				, "__inline__"
				, "__forceinline"
				, "virtual"
				, "explicit"
				, "friend"
				, "constexpr"
				, "consteval"
				, "constinit"
				, "thread_local"
			}
		;

		constexpr ch8 const *c_pAttributes[] =
			{
				"__attribute__"
				, "__attribute"
				, "__declspec"
				, "alignas"
				, "_Alignas"
			}
		;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		aint iAttributesEnd = -1;
		for (aint i = aint(_iFirst); i >= 0 && umint(i) <= _iLast; i = fp_NextCode(umint(i)))
		{
			auto const &Token = Tokens[umint(i)];
			bool bSpecifier = false;
			for (auto pSpecifier : c_pSpecifiers)
				bSpecifier |= m_Tokens.f_IsText(Token, pSpecifier);

			if (bSpecifier)
				continue;

			bool bAttribute = false;
			for (auto pAttribute : c_pAttributes)
				bAttribute |= m_Tokens.f_IsText(Token, pAttribute);

			auto iGroupStart = i;
			if (bAttribute)
				iGroupStart = fp_NextCode(umint(i));
			else if (!m_Tokens.f_IsText(Token, "[") || fp_NextCode(umint(i)) < 0 || !m_Tokens.f_IsText(Tokens[umint(fp_NextCode(umint(i)))], "["))
				break;

			auto iGroup = iGroupStart >= 0 ? m_Structure.f_FindNodeOpeningAt(umint(iGroupStart)) : Nodes.f_GetLen();
			auto const *pGroup = iGroup < Nodes.f_GetLen() ? &Nodes[iGroup] : nullptr;
			if (!pGroup || pGroup->m_Kind != ECodeNodeKind::mc_Group || pGroup->m_iLastToken >= _iLast)
				return -1;

			iAttributesEnd = aint(pGroup->m_iLastToken);
			i = iAttributesEnd;
		}

		return iAttributesEnd;
	}

	// The braced list whose one element the braced list is, written against its markers: the outer list of '{{' over
	// the elements over '}}'. The node count where there is none.
	umint CFormattingAnalyzer::fp_FindHuggingList(umint _iNode) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Brace || Node.m_iParent >= Nodes.f_GetLen())
			return Nodes.f_GetLen();

		auto const &Outer = Nodes[Node.m_iParent];
		bool bHugs
			= Outer.m_Kind == ECodeNodeKind::mc_Group
			&& Outer.m_Bracket == ECodeBracket::mc_Brace
			&& fp_PreviousCode(Node.m_iFirstToken) == aint(Outer.m_iFirstToken)
			&& fp_NextCode(Node.m_iLastToken) == aint(Outer.m_iLastToken)
			&& !fp_IsFirstOnLine(Node.m_iFirstToken)
			&& !fp_IsFirstOnLine(Outer.m_iLastToken)
		;

		return bHugs ? Node.m_iParent : Nodes.f_GetLen();
	}

	// A group whose markers both stand on lines of their own, and whose lines nothing in it fixes.
	bool CFormattingAnalyzer::fp_IsLevelled(umint _iNode) const
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];

		// A marker the layout joins onto the line in front stands on no line of its own.
		auto fStartsLine = [&](umint _iToken)
			{
				return fp_IsFirstOnLine(_iToken) && EGap(m_GapState[_iToken]) != EGap::mc_Inline;
			}
		;

		return fStartsLine(Node.m_iFirstToken)
			&& fStartsLine(Node.m_iLastToken)
			&& (Node.m_Bracket != ECodeBracket::mc_Brace || Node.m_bHasMultiLineBrace)
			&& !fp_HasOpaqueDirective(Node.m_iFirstToken, Node.m_iLastToken)
			&& !Node.m_bHasMultiLineToken
		;
	}

	// A parenthesis holding one name and nothing else, '(CFoo)' or '(NFoo::CBar)'.
	bool CFormattingAnalyzer::fp_HoldsNameAlone(umint _iGroup) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		if (_iGroup >= Nodes.f_GetLen())
			return false;

		auto const &Group = Nodes[_iGroup];
		if (Group.m_Kind != ECodeNodeKind::mc_Group || Group.m_Bracket != ECodeBracket::mc_Paren)
			return false;

		auto iName = fp_NextCode(Group.m_iFirstToken);
		bool bName = iName >= 0 && umint(iName) != Group.m_iLastToken;
		bool bWord = false;
		for (; bName && iName >= 0 && umint(iName) < Group.m_iLastToken; iName = fp_NextCode(umint(iName)))
		{
			auto const &Name = m_Tokens.f_GetTokens()[umint(iName)];
			bool bIdentifier = Name.m_Kind == ECodeTokenKind::mc_Identifier;
			bName = bIdentifier ? !bWord : m_Tokens.f_IsText(Name, "::");
			bWord = bIdentifier;
		}

		return bName && bWord;
	}

	// Each element of a split group that still does not fit has its own groups split in turn.
	void CFormattingAnalyzer::fp_LayoutElements(umint _iNode, umint _iIndent)
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		fp_LayoutElementRange(_iNode, Node.m_iFirstToken + 1, Node.m_iLastToken, Node.m_SplitPoints, _iIndent);
	}

	// The elements from the first token up to the end token, which closes them, separated at the splits.
	void CFormattingAnalyzer::fp_LayoutElementRange(umint _iNode, umint _iFirst, umint _iEnd, TCVector<umint> const &_Splits, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		umint iElement = _iFirst;
		umint iSplit = 0;
		while (iElement <= _iEnd)
		{
			auto iEnd = iSplit < _Splits.f_GetLen() ? _Splits[iSplit] : _iEnd;
			if (iEnd > iElement)
			{
				// A range ends on a code token: a comment trailing the element belongs to
				// the gap in front of the separator, and is no part of the element's width.
				auto iLast = fp_PreviousCode(iEnd);
				auto iStart = fg_IsCodeToken(m_Tokens.f_GetTokens()[iElement]) ? aint(iElement) : fp_NextCode(iElement);
				if (iStart >= 0 && iLast >= 0 && umint(iStart) <= umint(iLast))
				{
					fp_LayoutRange(_iNode, umint(iStart), umint(iLast), _iIndent, false, false);

					// A group the layout leaves with its lines, as a directive in it does, has its closing marker
					// levelled under the opening one the layout moved, as it is once it stands there.
					auto iGroup = m_Structure.f_FindNodeOpeningAt(umint(iStart));
					auto const *pGroup = iGroup < Nodes.f_GetLen() ? &Nodes[iGroup] : nullptr;
					bool bWholeGroup = pGroup && pGroup->m_Kind == ECodeNodeKind::mc_Group && pGroup->m_iLastToken == umint(iLast) && EGap(m_GapState[umint(iLast)]) == EGap::mc_Keep;
					if (bWholeGroup && fp_IsLevelled(iGroup) && fp_GetStatementIndent(umint(iLast)) != fp_GetStatementIndent(umint(iStart)))
						fp_IndentBefore(umint(iLast), fp_GetStatementIndent(umint(iStart)));
				}

				// The separator leads its element on the element's line, also where the element is too long for it:
				// ', "a long string"' rather than ',' over the string.
				auto iBehind = iStart >= 0 && umint(iStart) < umint(iLast) && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iStart)], ",") ? fp_NextCode(umint(iStart)) : aint(-1);

				// A line a block move only re-indents is no decision of the layout's either. A block's brace takes a line of
				// its own.
				auto BehindGap = iBehind >= 0 ? EGap(m_GapState[umint(iBehind)]) : EGap::mc_Inline;
				auto iBehindNode = iBehind >= 0 ? m_Structure.f_FindNodeOpeningAt(umint(iBehind)) : Nodes.f_GetLen();
				bool bBlock = iBehindNode < Nodes.f_GetLen() && Nodes[iBehindNode].m_Kind == ECodeNodeKind::mc_Block;
				bool bUndecided = (BehindGap == EGap::mc_Keep || BehindGap == EGap::mc_Indent) && !bBlock;
				if (bUndecided && fp_IsRangeJoinable(_iNode, umint(iStart), umint(iBehind)))
					fp_MarkInline(umint(iStart), umint(iBehind));
			}

			if (iSplit >= _Splits.f_GetLen())
				break;

			iElement = _Splits[iSplit];
			++iSplit;
		}
	}

	// A file of a list's elements is laid out as they would be inside the list's braces, the braces standing outside the
	// file: each comma at the start of its line, the elements at the file's level.
	void CFormattingAnalyzer::fp_LayoutFileElements(umint _iNode)
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		auto const &Tokens = m_Tokens.f_GetTokens();
		TCVector<umint> Splits;
		umint nDepth = 0;
		for (auto i = Node.m_iFirstToken; i <= Node.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				continue;

			if (m_Tokens.f_IsText(Token, "{") || m_Tokens.f_IsText(Token, "(") || m_Tokens.f_IsText(Token, "["))
				++nDepth;
			else if ((m_Tokens.f_IsText(Token, "}") || m_Tokens.f_IsText(Token, ")") || m_Tokens.f_IsText(Token, "]")) && nDepth)
				--nDepth;
			else if (!nDepth && m_Tokens.f_IsText(Token, ","))
			{
				Splits.f_Insert(i);
				fp_BreakBefore(i, 0);
			}
		}

		// A comma ending the file separates the last element from what the including file has behind it, and ends the
		// elements as a closing brace would.
		auto iEnd = Node.m_iLastToken + 1;
		if (!Splits.f_IsEmpty() && Splits.f_GetLast() == Node.m_iLastToken)
		{
			iEnd = Splits.f_GetLast();
			Splits.f_Remove(Splits.f_GetLen() - 1);
		}

		fp_LayoutElementRange(_iNode, Node.m_iFirstToken, iEnd, Splits, 0);
	}

	// A clause that ends at its condition, and a keyword that only introduces the
	// statement after it, guard that statement.
	bool CFormattingAnalyzer::fp_IsGuard(umint _iNode) const
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_Kind != ECodeNodeKind::mc_Statement)
			return false;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &First = Tokens[Node.m_iFirstToken];
		if (Node.m_iFirstToken == Node.m_iLastToken)
			return m_Tokens.f_IsText(First, "else") || m_Tokens.f_IsText(First, "do") || m_Tokens.f_IsText(First, "try");

		bool bClause
			= m_Tokens.f_IsText(First, "if")
			|| m_Tokens.f_IsText(First, "for")
			|| m_Tokens.f_IsText(First, "while")
			|| m_Tokens.f_IsText(First, "switch")
			|| m_Tokens.f_IsText(First, "catch")
			|| Node.m_bClauseMacro
		;

		return bClause && m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ")");
	}

	// Every brace of a block and every statement in it takes a line of its own: the
	// statements at the block's level, what a clause guards one level in, and a guarded
	// block at the clause's own level. Only what shares a line is moved; the depth of a
	// line the source already starts is not the layout's to decide, so the level a moved
	// statement takes is read from the lines around it. A case that shares its label's
	// line is left there, since 'case 1: return 1;' is written that way on purpose, and so
	// is the 'if' of an 'else if', and an attribute on a clause's line. Behind a closing
	// brace only a keyword starts a statement of its own; a name there declares a variable
	// of the type just defined.
	// True when the statement's lines cannot move with its first: a comment spanning lines behind code spells its own,
	// where one starting its line moves with its lines. Fixed line breaks move with it, as they only keep where the lines
	// break. A string literal's lines are its value, which no move changes. Without _bListsMove so does a braced list
	// written across lines, which its layout otherwise places against the statement's first line.
	bool CFormattingAnalyzer::fp_KeepsOwnLines(umint _iNode, bool _bListsMove) const
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_bHasMultiLineBrace && !_bListsMove)
			return true;

		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto i = Node.m_iFirstToken; (Node.m_bHasMultiLineToken || Node.m_bHasComment) && i <= Node.m_iLastToken; ++i)
		{
			if (i < m_BlockEnds.f_GetLen() && m_BlockEnds[i] <= Node.m_iLastToken)
			{
				i = m_BlockEnds[i];

				continue;
			}

			auto Kind = Tokens[i].m_Kind;
			bool bLayout = Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_LineSplice;
			bool bOwnLineComment = Kind == ECodeTokenKind::mc_BlockComment && fp_IsFirstOnLine(i);
			if (Tokens[i].m_bMultiLine && !bLayout && Kind != ECodeTokenKind::mc_StringLiteral && !bOwnLineComment)
				return true;
		}

		return false;
	}

	void CFormattingAnalyzer::fp_LayoutBlockLines(umint _iNode, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		bool bBraced = Node.m_Kind == ECodeNodeKind::mc_Block;

		// A conditional whose branches cut a construct leaves the statements around it no
		// depth of their own, since the token stream runs through every branch at once. Such
		// a block keeps the depths the source gave its lines. One inside a block nested in it
		// cuts that block's statements only.
		bool bDepths = !fp_HasOpaqueDirective(Node.m_iFirstToken, Node.m_iLastToken);
		if (!bDepths)
		{
			// The file's scope ends with its last token, which can close a block of its own.
			bDepths = true;
			auto iScopeEnd = bBraced ? Node.m_iLastToken : fg_Min(Node.m_iLastToken + 1, Tokens.f_GetLen());
			for (auto i = bBraced ? Node.m_iFirstToken + 1 : Node.m_iFirstToken; bDepths && i < iScopeEnd; ++i)
			{
				if (m_BlockEnds[i] < iScopeEnd)
					i = m_BlockEnds[i];
				else
					bDepths = Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor || !m_OpaqueDirective[i];
			}
		}

		if (bBraced)
		{
			if (!fp_IsFirstOnLine(Node.m_iLastToken))
				fp_OwnLineBefore(Node.m_iLastToken, _iIndent);
			else if (fp_GetStatementIndent(Node.m_iLastToken) != _iIndent)
				fp_IndentBefore(Node.m_iLastToken, _iIndent);
		}

		// A block without depths of its own keeps its lines' depths relative to each other between the directives
		// standing in it: each stretch moves as a whole, its first line one level in from the block's brace, or at the
		// brace's level for a label, and at the file's depth outside any block. A stretch whose first line does not
		// start a statement at the block's level, such as the body of a guard a directive cuts or an argument, moves as
		// far as the one in front of it; one opening a body stands under the head of the statement it belongs to. An
		// alternative branch starts where its conditional does, so what stands in front of the '#if' says whether it
		// starts one. The comment lines in front of a stretch's first statement stand at its depth.
		if (!bDepths)
		{
			auto nBase = bBraced ? _iIndent + nTab : _iIndent;

			auto fStartsLine = [&](umint _iToken)
				{
					auto iLine = m_Lines.f_FindLine(Tokens[_iToken].m_iOffset);
					auto iLineStart = m_Lines.f_GetLineStart(iLine);
					for (auto iByte = iLineStart; iByte < Tokens[_iToken].m_iOffset; ++iByte)
					{
						if (!fg_IsSpaceOrTab(m_pSource[iByte]))
							return false;
					}

					return !m_ProtectedStart[iLine];
				}
			;

			aint nDelta = 0;
			bool bNeedsReference = true;
			umint iNestedEnd = 0;
			auto nBodyDepth = bBraced ? m_TokenDepth[Node.m_iFirstToken] + 1 : umint(0);

			struct CConditional
			{
				aint m_iBefore;
				aint m_nDelta;
			};

			TCVector<CConditional> Conditionals;
			aint iStretchBefore = -2; // The code in front of the stretch when it opens an alternative branch.
			aint nStretchDelta = 0;
			TCVector<umint> PendingComments;

			auto fCommentIndent = [&](umint _iComment)
				{
					return m_CommentMoved[_iComment] ? m_CommentIndent[_iComment] : fp_GetSourceLineIndent(_iComment);
				}
			;

			auto fPlaceComment = [&](umint _iComment, aint _nIndent)
				{
					if (_nIndent >= 0 && umint(_nIndent) != fCommentIndent(_iComment))
					{
						m_CommentMoved[_iComment] = 1;
						m_CommentIndent[_iComment] = umint(_nIndent);
					}
				}
			;

			auto fMoveComment = [&](umint _iComment)
				{
					if (nDelta)
						fPlaceComment(_iComment, aint(fCommentIndent(_iComment)) + nDelta);
				}
			;

			// The file's scope has no braces of its own.
			auto iStretchesEnd = bBraced ? Node.m_iLastToken : fg_Min(Node.m_iLastToken + 1, Tokens.f_GetLen());
			for (auto i = bBraced ? Node.m_iFirstToken + 1 : Node.m_iFirstToken; i < iStretchesEnd; ++i)
			{
				auto const &Token = Tokens[i];
				auto iBlockEnd = m_BlockEnds[i];
				auto Depth = m_TokenDepth[i];
				bool bNested = i <= iNestedEnd && iNestedEnd;
				if (!bNested && iBlockEnd < iStretchesEnd)
					iNestedEnd = iBlockEnd;

				auto Kind = Token.m_Kind;
				if (Kind == ECodeTokenKind::mc_Preprocessor)
				{
					// A directive inside brackets is part of the statement they are in.
					if (bNested || Depth != nBodyDepth)
						continue;

					bNeedsReference = true;
					iStretchBefore = -2;

					auto Keyword = fp_GetDirectiveKeyword(i);
					if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
						Conditionals.f_Insert(CConditional{fp_PreviousCode(i), nDelta});
					else if (!Conditionals.f_IsEmpty() && (Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef" || Keyword == "else"))
					{
						iStretchBefore = Conditionals.f_GetLast().m_iBefore;
						nStretchDelta = Conditionals.f_GetLast().m_nDelta;
					}
					else if (!Conditionals.f_IsEmpty() && Keyword == "endif")
						Conditionals.f_Remove(Conditionals.f_GetLen() - 1);

					continue;
				}

				bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
				if ((!bComment && !fg_IsCodeToken(Token)) || EGap(m_GapState[i]) == EGap::mc_Inline || !fStartsLine(i))
					continue;

				if (bComment)
				{
					if (bNeedsReference && !bNested)
						PendingComments.f_Insert(i);
					else
						fMoveComment(i);

					continue;
				}

				if (bNeedsReference && !bNested)
				{
					bNeedsReference = false;

					auto iPrevious = iStretchBefore != -2 ? iStretchBefore : fp_PreviousCode(i);
					auto const *pPrevious = iPrevious >= 0 ? &Tokens[umint(iPrevious)] : nullptr;
					if (iStretchBefore != -2)
						nDelta = nStretchDelta;

					// A label ends a statement of its own, and the brace behind it opens the next: 'case 1:' over '{'.
					bool bBehindLabel = false;
					if (pPrevious && m_Tokens.f_IsText(*pPrevious, ":"))
					{
						auto iLabel = m_Structure.f_FindStatementEndingAt(umint(iPrevious));
						auto const *pLabelFirst = iLabel < Nodes.f_GetLen() ? &Tokens[Nodes[iLabel].m_iFirstToken] : nullptr;
						bBehindLabel = pLabelFirst && (m_Tokens.f_IsText(*pLabelFirst, "case") || m_Tokens.f_IsText(*pLabelFirst, "default"));
					}

					bool bStatement
						= Depth == nBodyDepth
						&& !m_Tokens.f_IsText(Token, "}")
						&& !m_Tokens.f_IsText(Token, ")")
						&& !m_Tokens.f_IsText(Token, "]")
						&&
						(
							!pPrevious
							|| (bBraced && umint(iPrevious) == Node.m_iFirstToken)
							|| m_Tokens.f_IsText(*pPrevious, ";")
							|| m_Tokens.f_IsText(*pPrevious, "{")
							|| m_Tokens.f_IsText(*pPrevious, "}")
							|| bBehindLabel
						)
					;
					if (bStatement)
					{
						auto iNext = fp_NextCode(i);
						bool bLabel
							= m_Tokens.f_IsText(Token, "case")
							|| m_Tokens.f_IsText(Token, "default")
							|| fg_IsObjectiveCVisibility(m_Tokens, Token)
							||
							(
								(m_Tokens.f_IsText(Token, "public") || m_Tokens.f_IsText(Token, "private") || m_Tokens.f_IsText(Token, "protected"))
								&& iNext >= 0
								&& m_Tokens.f_IsText(Tokens[umint(iNext)], ":")
							)
						;

						// A target to jump to is a label as well, a statement of its own ending in its colon: 'L:'.
						if (!bLabel && Token.m_Kind == ECodeTokenKind::mc_Identifier && iNext >= 0 && m_Tokens.f_IsText(Tokens[umint(iNext)], ":"))
						{
							auto iLabel = m_Structure.f_FindStatementEndingAt(umint(iNext));
							bLabel = iLabel < Nodes.f_GetLen() && Nodes[iLabel].m_iFirstToken == i;
						}

						auto nTarget = bLabel ? _iIndent : nBase;
						nDelta = aint(nTarget) - aint(fp_GetStatementIndent(i));

						for (auto iComment : PendingComments)
							fPlaceComment(iComment, aint(nTarget));

						PendingComments.f_Clear();
					}
					else if (pPrevious && m_Tokens.f_IsText(*pPrevious, "else") && Depth == nBodyDepth && !m_Tokens.f_IsText(Token, "{"))
					{
						// A statement an 'else' a directive cuts it from guards in some configurations only stands one level in
						// from the 'else' where the source has it there, and at the block's level otherwise.
						// An 'else' behind a closing brace, '} else', takes a line of its own at the brace's level.
						auto iElseLine = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(pPrevious->m_iOffset)));
						while (iElseLine < umint(iPrevious) && !fg_IsCodeToken(Tokens[iElseLine]))
							++iElseLine;

						bool bGuarded = fp_GetSourceLineIndent(i) == fp_GetSourceLineIndent(umint(iPrevious)) + nTab;
						auto nTarget = bGuarded ? fp_GetStatementIndent(iElseLine) + nTab : nBase;
						nDelta = aint(nTarget) - aint(fp_GetStatementIndent(i));
					}
					else if (m_Tokens.f_IsText(Token, "{") && iBlockEnd < Tokens.f_GetLen())
					{
						// The head starts behind the last terminator or brace at the body's depth, in whichever branch.
						auto iHead = i;
						for (auto iBefore = fp_PreviousCode(i); iBefore >= 0; iBefore = fp_PreviousCode(umint(iBefore)))
						{
							auto const &Before = Tokens[umint(iBefore)];
							auto BeforeDepth = m_TokenDepth[umint(iBefore)];
							if (BeforeDepth < Depth)
								break;

							if (BeforeDepth == Depth && (m_Tokens.f_IsText(Before, ";") || m_Tokens.f_IsText(Before, "{") || m_Tokens.f_IsText(Before, "}")))
								break;

							iHead = umint(iBefore);
						}

						// A head behind a closing brace on its line, '} else', stands where that line does. An 'else' stands where
						// its 'if' does, wherever the source wrote it.
						auto iHeadLine = iHead;
						if (!fp_IsFirstOnLine(iHead))
						{
							iHeadLine = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[iHead].m_iOffset)));
							while (iHeadLine < iHead && !fg_IsCodeToken(Tokens[iHeadLine]))
								++iHeadLine;
						}

						// The 'if' heading its chain, as an 'else if' stands where that one does.
						for (umint iChild = Node.m_Children.f_GetLen(); m_Tokens.f_IsText(Tokens[iHead], "else") && iChild > 0; --iChild)
						{
							auto const &Before = Nodes[Node.m_Children[iChild - 1]];
							if (Before.m_iFirstToken >= iHead || !m_Tokens.f_IsText(Tokens[Before.m_iFirstToken], "if"))
								continue;

							iHeadLine = Before.m_iFirstToken;
							if (fp_LeadingElse(Before.m_iFirstToken) < 0)
								break;
						}

						if (iHead != i)
							nDelta = aint(fp_GetStatementIndent(iHeadLine)) - aint(fp_GetStatementIndent(i));
					}

					for (auto iComment : PendingComments)
						fMoveComment(iComment);

					PendingComments.f_Clear();
				}

				auto nIndent = aint(fp_GetStatementIndent(i)) + nDelta;
				if (nDelta && nIndent >= 0)
					fp_IndentBefore(i, umint(nIndent));
			}

			for (auto iComment : PendingComments)
				fMoveComment(iComment);
		}

		constexpr ch8 const *c_pStatementKeywords[] =
			{
				"else"
				, "while"
				, "catch"
				, "if"
				, "for"
				, "do"
				, "switch"
				, "try"
				, "return"
				, "co_return"
				, "break"
				, "continue"
				, "goto"
				, "case"
				, "default"
				, "{"
			}
		;

		umint nLevel = bBraced ? _iIndent + nTab : _iIndent;
		umint nPlaced = nLevel;
		bool bEnumBody = bBraced && m_Structure.f_IsEnumBody(_iNode);

		// A comma a directive stands behind is one the conversion could not move in front of
		// the enumerator after it, and the enum keeps all of its commas behind its enumerators.
		bool bEnumTrailingCommas = false;
		for (auto i = Node.m_iFirstToken + 1; bEnumBody && i < Node.m_iLastToken && !bEnumTrailingCommas; ++i)
		{
			if (!m_Tokens.f_IsText(Tokens[i], ","))
				continue;

			auto iNext = fp_NextCode(i);
			for (auto iGap = i + 1; iNext >= 0 && iGap < umint(iNext) && !bEnumTrailingCommas; ++iGap)
				bEnumTrailingCommas = Tokens[iGap].m_Kind == ECodeTokenKind::mc_Preprocessor;
		}

		// The clause whose statement is still to come, and the depth that clause was
		// written at: what it guards stands one level in from there.
		umint iGuard = TCLimitsInt<umint>::mc_Max;
		umint nGuard = 0;
		umint iPrevious = TCLimitsInt<umint>::mc_Max;
		bool bOnLabelLine = false;

		auto fIsCaseLabel = [&](umint _iIndex)
			{
				if (_iIndex >= Node.m_Children.f_GetLen())
					return false;

				auto const &Label = Nodes[Node.m_Children[_iIndex]];
				auto const &LabelFirst = Tokens[Label.m_iFirstToken];

				return m_Tokens.f_IsText(Tokens[Label.m_iLastToken], ":") && (m_Tokens.f_IsText(LabelFirst, "case") || m_Tokens.f_IsText(LabelFirst, "default"));
			}
		;

		// A case whose last statement does not jump falls through to the label behind it. A
		// macro the naming lists as never returning jumps as 'throw' does.
		auto fFallsThrough = [&](umint _iLast)
			{
				if (!fIsCaseLabel(_iLast + 1))
					return false;

				constexpr ch8 const *c_pJumps[] =
					{
						"break"
						, "return"
						, "co_return"
						, "continue"
						, "throw"
						, "goto"
					}
				;

				auto const &Last = Tokens[Nodes[Node.m_Children[_iLast]].m_iFirstToken];
				if (m_Tokens.f_HasRole(Last, ECodeNameRole::mc_NoReturnMacro))
					return false;

				for (auto pJump : c_pJumps)
				{
					if (m_Tokens.f_IsText(Last, pJump))
						return false;
				}

				return true;
			}
		;

		// The last statement a label took onto its line, and the label itself.
		umint iJoinedLabel = TCLimitsInt<umint>::mc_Max;
		umint iJoinedUntil = TCLimitsInt<umint>::mc_Max;
		umint iChildIndex = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			++iChildIndex;
			auto const &Child = Nodes[iChild];
			auto iFirst = Child.m_iFirstToken;
			auto const &First = Tokens[iFirst];
			bool bFirstOnLine = fp_IsFirstOnLine(iFirst);
			bool bCutFromGuard = false;
			bool bEveryBranchGuards = false;

			// In a block a conditional cuts, only the statements it stands in or in front of keep their depths. One the
			// conditional cuts from the guard in front of it is the guard's to place, where the two are compiled together:
			// 'if (a)' over '#endif' over 'g();'.
			bool bPlaced = bDepths;
			if (!bPlaced)
			{
				bPlaced = true;
				bool bBehindGuard = iPrevious != TCLimitsInt<umint>::mc_Max && iGuard == iPrevious;
				auto iFrom = iPrevious != TCLimitsInt<umint>::mc_Max ? Nodes[iPrevious].m_iLastToken + 1 : Node.m_iFirstToken + 1;
				for (auto i = iFrom; bPlaced && i <= Child.m_iLastToken && i < Node.m_iLastToken; ++i)
				{
					if (i >= iFirst && m_BlockEnds[i] <= Child.m_iLastToken)
					{
						i = m_BlockEnds[i];

						continue;
					}

					if (Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor || !m_OpaqueDirective[i])
						continue;

					auto Keyword = fp_GetDirectiveKeyword(i);
					bool bAlternative = Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef";
					bPlaced = bBehindGuard && i < iFirst && !bAlternative;

					// One a conditional opens behind the guard is compiled with the guard alone, and one behind an '#endif'
					// wherever each of its branches leaves a guard in front of it.
					if (bPlaced)
					{
						bEveryBranchGuards = (bEveryBranchGuards || !bCutFromGuard) && (Keyword != "endif" || fp_IsGuardedInEveryBranch(i));
						bCutFromGuard = true;
					}
				}
			}

			bool bGuarded = iGuard != TCLimitsInt<umint>::mc_Max;
			bool bLabelled = false;
			bool bAfterBlock = false;
			// Behind a function's body, a namespace's or a linkage specification's braces, or an Objective-C section's header
			// nothing can be declared of what they define.
			bool bAfterDefinition = false;
			if (iPrevious != TCLimitsInt<umint>::mc_Max)
			{
				auto const &Previous = Nodes[iPrevious];
				bLabelled = m_Tokens.f_IsText(Tokens[Previous.m_iLastToken], ":");
				bAfterBlock = m_Tokens.f_IsText(Tokens[Previous.m_iLastToken], "}");
				bAfterDefinition = Previous.m_bObjectiveCHeader;

				if (bAfterBlock)
				{
					auto iHead = aint(Previous.m_iFirstToken);
					if (m_Tokens.f_IsText(Tokens[umint(iHead)], "inline"))
						iHead = fp_NextCode(umint(iHead));

					auto const *pHead = iHead >= 0 ? &Tokens[umint(iHead)] : nullptr;
					auto iNext = pHead ? fp_NextCode(umint(iHead)) : -1;
					bAfterDefinition
						|= iNext >= 0 && (m_Tokens.f_IsText(*pHead, "namespace") || (m_Tokens.f_IsText(*pHead, "extern") && Tokens[umint(iNext)].m_Kind == ECodeTokenKind::mc_StringLiteral))
					;
				}

				for (auto iPreviousChild : Previous.m_Children)
				{
					auto const &PreviousChild = Nodes[iPreviousChild];
					if (PreviousChild.m_Kind == ECodeNodeKind::mc_Block && PreviousChild.m_iLastToken == Previous.m_iLastToken)
						bAfterDefinition |= fg_IsFunctionBody(m_Tokens, m_Structure, iPreviousChild);
				}
			}

			// A case written on its label's line stays there where it is one statement and no
			// block, a 'break' behind it included: 'case 1: a = 1; break;'. A block, or more
			// statements than that, takes lines of its own under the label like any other body.
			bool bSharedLabel = bLabelled && iChildIndex >= 2 && fIsCaseLabel(iChildIndex - 2);

			if (bLabelled && !bFirstOnLine && bSharedLabel)
				bOnLabelLine = false;
			else if (bLabelled && !bFirstOnLine)
			{
				umint nOnLine = 0;
				bool bBlockOnLine = false;
				umint iLastOnLine = iChildIndex;
				for (auto iOther = iChildIndex; iOther < Node.m_Children.f_GetLen(); ++iOther)
				{
					// The next case starts a line of its own wherever the source wrote it.
					auto const &Other = Nodes[Node.m_Children[iOther]];
					if (iOther > iChildIndex && (fp_IsFirstOnLine(Other.m_iFirstToken) || fIsCaseLabel(iOther)))
						break;

					bool bBreak = iOther > iChildIndex && m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "break");
					nOnLine += !bBreak;
					bBlockOnLine |= Other.m_Kind == ECodeNodeKind::mc_Block || m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "{");
					iLastOnLine = iOther;
				}

				// A clause guards what stands below it as well, which goes under the label with it: 'case 1: if (a)' over '{'.
				bOnLabelLine = nOnLine == 1 && !bBlockOnLine && !fFallsThrough(iLastOnLine) && !fp_IsGuard(Node.m_Children[iChildIndex]);

				// Where the line holding the label and its body is too long, the body takes lines under the label, as one
				// the source wrote there does. A line the layout cannot join keeps the shape the source gives it.
				if (bOnLabelLine)
				{
					auto nLabelIndent = nLevel >= nTab ? nLevel - nTab : nLevel;
					auto iLineLast = Nodes[Node.m_Children[iLastOnLine]].m_iLastToken;
					auto iLabelFirst = Nodes[iPrevious].m_iFirstToken;
					bOnLabelLine = !fp_IsRangeJoinable(_iNode, iLabelFirst, iLineLast) || fp_FitsInline(iLabelFirst, iLineLast, nLabelIndent);
				}
			}
			else
				bOnLabelLine = !bFirstOnLine && bOnLabelLine && !fIsCaseLabel(iChildIndex);

			// The clause the statement stands directly behind, which is the one whose braces
			// the standard decides; an attribute on its line stands between the two.
			bool bDirectlyGuarded = bGuarded && iPrevious == iGuard;
			bool bElseIf = bDirectlyGuarded && m_Tokens.f_IsText(Tokens[Nodes[iPrevious].m_iFirstToken], "else") && m_Tokens.f_IsText(First, "if");

			// One only comments part from its 'else' is the statement the 'else' guards, one level in; one a directive
			// parts from it keeps the depth its source gives it.
			bool bCommentedElseIf = bElseIf;
			for (auto i = bElseIf ? Nodes[iPrevious].m_iLastToken + 1 : iFirst; bCommentedElseIf && i < iFirst; ++i)
				bCommentedElseIf = Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor;

			// The 'if' an 'else' guards stands on the 'else''s line, wherever the source wrote it: 'else' over 'if (a)' is
			// 'else if (a)'.
			auto const *pPrevious = iPrevious != TCLimitsInt<umint>::mc_Max ? &Nodes[iPrevious] : nullptr;
			bool bBehindElse = pPrevious && m_Tokens.f_IsText(Tokens[pPrevious->m_iLastToken], "else");
			bool bJoinedToElse
				= bBehindElse
				&& m_Tokens.f_IsText(First, "if")
				&& bFirstOnLine
				&& bPlaced
				&& fp_IsRangeJoinable(_iNode, pPrevious->m_iLastToken, iFirst)
				&& fp_FitsInline(pPrevious->m_iLastToken, iFirst, 0)
			;
			if (bJoinedToElse)
			{
				fp_MarkInline(Nodes[iPrevious].m_iLastToken, iFirst);
				bFirstOnLine = false;
			}

			if (m_bProbing && m_bAllowConversions && bDirectlyGuarded && !bElseIf)
			{
				if (m_Tokens.f_IsText(First, "{"))
					fp_DropBraces(iChild, iPrevious);
				else
					fp_AddBraces(iChild, iPrevious);
			}

			// An attribute is written on the clause's line: 'if (x) [[unlikely]]'.
			auto iSecond = fp_NextCode(iFirst);
			bool bAttribute = m_Tokens.f_IsText(First, "[") && iSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSecond)], "[");

			// A label stands one level out from the statements written under it, whether it
			// is a case, an access specifier or a target to jump to.
			bool bLabel = m_Tokens.f_IsText(Tokens[Child.m_iLastToken], ":") || fg_IsObjectiveCVisibility(m_Tokens, First);
			umint nPlace = nLevel;
			if (bGuarded)
				nPlace = m_Tokens.f_IsText(First, "{") ? nGuard : nGuard + nTab;
			else if (bLabel && nLevel >= nTab)
				nPlace = nLevel - nTab;
			else if (m_Tokens.f_IsText(First, "else") && iChildIndex)
			{
				// An 'else' stands where the 'if' it answers does, the innermost one in front of it, which a chain of
				// clauses without braces sets in from the block's level: 'if (a)' over 'if (b)' over 'else'.
				// One an alternative branch separates from that 'if' is never compiled with it, and keeps the depth its
				// source gives it. Each 'else' passed on the way answers an 'if' in front of it.
				umint nAnswered = 0;
				for (auto iBefore = iChildIndex; iBefore > 0; --iBefore)
				{
					auto const &Before = Nodes[Node.m_Children[iBefore - 1]];
					if (m_Tokens.f_IsText(Tokens[Before.m_iFirstToken], "else"))
						++nAnswered;

					if (!m_Tokens.f_IsText(Tokens[Before.m_iFirstToken], "if"))
						continue;

					if (nAnswered)
					{
						--nAnswered;

						continue;
					}

					bool bCut = false;
					for (auto i = Before.m_iFirstToken; i < iFirst && !bCut; ++i)
					{
						if (Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor)
							continue;

						auto Keyword = fp_GetDirectiveKeyword(i);
						bCut = Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef";
					}

					if (bCut)
						break;

					auto iElse = fp_LeadingElse(Before.m_iFirstToken);
					auto iIfLine = iElse >= 0 ? umint(iElse) : Before.m_iFirstToken;
					if (fp_IsFirstOnLine(iIfLine) || fp_IsBreakGap(iIfLine))
						nPlace = fp_GetStatementIndent(iIfLine);

					break;
				}
			}

			// A 'do' loop's 'while' stands where its 'do' does, which a guard can set in from the block's level. The terminators
			// passed on the way back end loops of their own.
			// The terminator is read as a clause guarding an empty statement: 'while (b)' over ';'.
			auto fIsTerminator = [&](CCodeNode const &_Statement)
				{
					if (!m_Tokens.f_IsText(Tokens[_Statement.m_iFirstToken], "while"))
						return false;

					auto iNext = fp_NextCode(_Statement.m_iLastToken);
					return m_Tokens.f_IsText(Tokens[_Statement.m_iLastToken], ";") || (iNext >= 0 && m_Tokens.f_IsText(Tokens[umint(iNext)], ";"));
				}
			;
			if (!bGuarded && fIsTerminator(Child))
			{
				umint nTerminators = 0;
				for (auto iBefore = iChildIndex; iBefore > 0; --iBefore)
				{
					auto const &Before = Nodes[Node.m_Children[iBefore - 1]];
					auto const &BeforeFirst = Tokens[Before.m_iFirstToken];
					if (fIsTerminator(Before))
						++nTerminators;
					else if (m_Tokens.f_IsText(BeforeFirst, "do") && Before.m_iFirstToken == Before.m_iLastToken)
					{
						if (nTerminators)
						{
							--nTerminators;

							continue;
						}

						if (fp_IsFirstOnLine(Before.m_iFirstToken) || fp_IsBreakGap(Before.m_iFirstToken))
							nPlace = fp_GetStatementIndent(Before.m_iFirstToken);

						break;
					}
				}
			}

			// Where only some configurations compile the guard in front of it, one is guarded and another not, and the
			// depth its source gives it is kept where it is the one of either; any other is the unguarded one.
			if (bCutFromGuard && !bEveryBranchGuards && fp_IsFirstOnLine(iFirst) && fp_GetStatementIndent(iFirst) != nPlace)
				nPlace = nLevel;

			// An enumerator list is no statement, but takes the line under the enum's brace as one
			// does, also where it is written over lines.
			// One already on a line of its own has its first line at the body's level, where the commas
			// in front of the others stand.
			bool bEnumerators = false;
			if (bEnumBody && iPrevious == TCLimitsInt<umint>::mc_Max)
			{
				auto iChildLast = fp_PreviousCode(fg_Min(Child.m_iLastToken, Node.m_iLastToken - 1) + 1);
				bEnumerators = bFirstOnLine || iChildLast >= aint(iFirst);
			}

			// A case whose body is one statement, a 'break' behind it included, takes it onto
			// its label's line where the whole fits there: 'case 1: a = 1; break;'. A block, a
			// clause and a comment keep the body under the label.
			bool bJoinedToLabel = iJoinedLabel != TCLimitsInt<umint>::mc_Max && iChildIndex > iJoinedLabel && iChildIndex <= iJoinedUntil;

			// A body that labels in front of this one fall through to stays under all of them.
			bool bCaseLabel = bLabel && (m_Tokens.f_IsText(First, "case") || m_Tokens.f_IsText(First, "default"));
			if (bCaseLabel && !bLabelled && !bGuarded && Child.m_Kind == ECodeNodeKind::mc_Statement)
			{
				umint iBodyLast = iChildIndex;
				umint nStatements = 0;
				bool bSimple = true;
				for (auto iOther = iChildIndex + 1; iOther < Node.m_Children.f_GetLen() && bSimple; ++iOther)
				{
					auto const &Other = Nodes[Node.m_Children[iOther]];
					auto const &OtherFirst = Tokens[Other.m_iFirstToken];
					if (m_Tokens.f_IsText(Tokens[Other.m_iLastToken], ":") || m_Tokens.f_IsText(OtherFirst, "}"))
						break;

					bool bBreak = m_Tokens.f_IsText(OtherFirst, "break");
					if (bBreak && nStatements <= 1)
					{
						iBodyLast = iOther;

						break;
					}

					constexpr ch8 const *c_pCompound[] =
						{
							"if"
							, "for"
							, "while"
							, "switch"
							, "do"
							, "else"
							, "try"
							, "{"
							, "case"
							, "default"
						}
					;

					bool bCompound = false;
					for (auto pKeyword : c_pCompound)
						bCompound |= m_Tokens.f_IsText(OtherFirst, pKeyword);

					bSimple = Other.m_Kind == ECodeNodeKind::mc_Statement && !bCompound && ++nStatements == 1;
					iBodyLast = iOther;
				}

				auto iLast = Nodes[Node.m_Children[iBodyLast]].m_iLastToken;

				// A blank line between the body's statements is one its author put there; one
				// behind the label goes anyway, and so does the one in front of the 'break', which
				// the case written under its label has to have. One inside a statement is the
				// layout's to take out.
				bool bBlank = false;
				auto iGapsLast = iBodyLast;
				if (iBodyLast > iChildIndex + 1 && m_Tokens.f_IsText(Tokens[Nodes[Node.m_Children[iBodyLast]].m_iFirstToken], "break"))
					iGapsLast = iBodyLast - 1;

				for (auto iGap = iChildIndex + 1; iGap < iGapsLast && !bBlank; ++iGap)
				{
					umint nNewlines = 0;
					for (auto i = Nodes[Node.m_Children[iGap]].m_iLastToken + 1; i < Nodes[Node.m_Children[iGap + 1]].m_iFirstToken && !bBlank; ++i)
					{
						if (Tokens[i].m_Kind == ECodeTokenKind::mc_Newline)
							bBlank = ++nNewlines > 1;
						else if (Tokens[i].m_Kind != ECodeTokenKind::mc_Whitespace)
							nNewlines = 0;
					}
				}

				bool bJoins
					= bSimple
					&& !bBlank
					&& !fFallsThrough(iBodyLast)
					&& iBodyLast > iChildIndex
					&& nStatements <= 1
					&& fp_IsFirstOnLine(Nodes[Node.m_Children[iChildIndex + 1]].m_iFirstToken)
					&& fp_IsRangeJoinable(_iNode, Child.m_iFirstToken, iLast)
					&& fp_FitsInline(Child.m_iFirstToken, iLast, nPlace)
				;
				if (bJoins)
				{
					fp_MarkInline(Child.m_iLastToken, iLast);
					iJoinedLabel = iChildIndex;
					iJoinedUntil = iBodyLast;
				}
			}

			bool bStays
				= (Child.m_Kind == ECodeNodeKind::mc_Unsupported && !bEnumerators)
				|| bOnLabelLine
				|| (bElseIf && (!bFirstOnLine || !bCommentedElseIf))
				|| bJoinedToElse
				|| (bAttribute && bGuarded && !bFirstOnLine)
				|| bJoinedToLabel
				|| (m_Tokens.f_IsText(First, ";") && (!bFirstOnLine || !bDirectlyGuarded))
			;

			// Each enumerator takes a line of its own, the comma in front of it: 'EA' over ', EB',
			// or behind the one in front of it in an enum holding a directive. A comma with a
			// comment or a directive behind it keeps its place, and so does a trailing one,
			// which ends the list rather than starting an enumerator.
			if (bEnumBody && iPrevious == TCLimitsInt<umint>::mc_Max)
			{
				for (auto i = iFirst; i < Node.m_iLastToken && i <= Child.m_iLastToken; ++i)
				{
					if (m_TokenDepth[i] != m_TokenDepth[iFirst] || !m_Tokens.f_IsText(Tokens[i], ","))
						continue;

					// One already starting its line stands at the body's level as well.
					if (fp_IsFirstOnLine(i))
					{
						bool bProtected = m_ProtectedStart[m_Lines.f_FindLine(Tokens[i].m_iOffset)];
						if (!bProtected && i > iFirst && fp_GetStatementIndent(i) != nPlace)
							fp_IndentBefore(i, nPlace);

						continue;
					}

					auto iNextCode = fp_NextCode(i);
					bool bTrailing = iNextCode < 0 || umint(iNextCode) >= Node.m_iLastToken;
					bool bTrivia = false;
					for (auto iBetween = i + 1; !bTrailing && iBetween < umint(iNextCode) && !bTrivia; ++iBetween)
					{
						auto Kind = Tokens[iBetween].m_Kind;
						bTrivia = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor;
					}

					if (bTrailing || (bTrivia && !bEnumTrailingCommas))
						continue;

					// Behind a comma kept behind its enumerator the next enumerator starts a line at the body's level, and so
					// do the comment lines in front of it.
					if (bEnumTrailingCommas)
					{
						auto iNext = umint(iNextCode);
						bool bProtected = m_ProtectedStart[m_Lines.f_FindLine(Tokens[iNext].m_iOffset)];
						if (!fp_IsFirstOnLine(iNext) && !bTrivia)
							fp_OwnLineBefore(iNext, nPlace);
						else if (fp_IsFirstOnLine(iNext) && !bProtected && fp_GetStatementIndent(iNext) != nPlace)
							fp_IndentBefore(iNext, nPlace);

						for (auto iBetween = i + 1; iBetween < iNext; ++iBetween)
						{
							auto Kind = Tokens[iBetween].m_Kind;
							bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
							if (bComment && fp_IsFirstOnLine(iBetween) && !m_ProtectedStart[m_Lines.f_FindLine(Tokens[iBetween].m_iOffset)])
							{
								m_CommentMoved[iBetween] = 1;
								m_CommentIndent[iBetween] = nPlace;
							}
						}

						continue;
					}

					// A comma ending its line moves to the front of the enumerator behind it, and a
					// blank line in front of that enumerator moves in front of the comma.
					fp_OwnLineBefore(i, nPlace);
					if (fp_IsLastOnLine(i))
					{
						fp_MarkInline(i, umint(iNextCode));

						umint nNewlines = 0;
						for (auto iBetween = i + 1; iBetween < umint(iNextCode); ++iBetween)
							nNewlines += Tokens[iBetween].m_Kind == ECodeTokenKind::mc_Newline;

						if (nNewlines > 1 && i < m_BlankBefore.f_GetLen())
							m_BlankBefore[i] = 1;
					}
				}

				// An enumerator too long for its line is laid out as a list's element is, its value's '='
				// starting the first operand's line at the enumerator's level.
				if (!bEnumTrailingCommas)
				{
					auto iListLast = fg_Min(Child.m_iLastToken, Node.m_iLastToken - 1);
					auto iStart = iFirst;
					for (auto i = iFirst; i <= iListLast + 1; ++i)
					{
						bool bEnd = i > iListLast || (m_TokenDepth[i] == m_TokenDepth[iFirst] && m_Tokens.f_IsText(Tokens[i], ","));
						if (!bEnd)
							continue;

						auto iLast = fp_PreviousCode(i);
						if (iLast >= 0 && umint(iLast) >= iStart && !fp_FitsInline(iStart, umint(iLast), nPlace) && fp_IsRangeJoinable(iChild, iStart, umint(iLast)))
							fp_LayoutRange(iChild, iStart, umint(iLast), nPlace, false, false);

						iStart = i;
					}
				}
			}

			// Behind a closing brace only a keyword starts a statement of its own, since a
			// name there declares a variable of the type just defined.
			if (!bStays && !bFirstOnLine && bAfterBlock && !bAfterDefinition)
			{
				bool bKeyword = false;
				for (auto pKeyword : c_pStatementKeywords)
					bKeyword |= m_Tokens.f_IsText(First, pKeyword);

				bStays = !bKeyword;
			}

			// A body the layout takes onto its label's line stands there as one the source wrote there does.
			umint nWritten = bFirstOnLine && !bJoinedToLabel ? fp_GetStatementIndent(iFirst) : nPlaced;

			// A statement behind the end of a comment over several lines stays where the comment leaves it, and so do its
			// other lines.
			auto iBeforeFirst = fp_PreviousSignificant(iFirst);
			auto const *pBeforeFirst = iBeforeFirst >= 0 ? &Tokens[umint(iBeforeFirst)] : nullptr;
			bool bBehindComment
				= pBeforeFirst && pBeforeFirst->m_bMultiLine && (pBeforeFirst->m_Kind == ECodeTokenKind::mc_BlockComment || pBeforeFirst->m_Kind == ECodeTokenKind::mc_LineComment)
			;

			auto fMoveLines = [&](aint _nDelta)
				{
					// The statement's other lines move with its first, where its layout keeps them; a block inside it
					// places its own from its brace, which moves with them, and an enum's enumerators stand at the body's
					// level each, whatever they hold.
					// The delta is measured from where the statement stands, a block around it moved included, so its lines
					// move on from where that move left them.
					auto fMoves = [&](umint _iToken)
						{
							auto Gap = EGap(m_GapState[_iToken]);

							return Gap == EGap::mc_Keep || Gap == EGap::mc_Indent;
						}
					;

					// A block inside an expression whose brace ends a line, a lambda's in a statement keeping its lines, moves with
					// them line by line. The statement's own body is placed by its layout.
					for (auto i = iFirst + 1; !bEnumBody && i <= Child.m_iLastToken; ++i)
					{
						bool bOwnBody = false;
						if (i < m_BlockEnds.f_GetLen() && m_BlockEnds[i] <= Child.m_iLastToken && !fp_IsFirstOnLine(i))
						{
							auto iBlock = m_Structure.f_FindNodeOpeningAt(i);
							bOwnBody = iBlock >= Nodes.f_GetLen() || Nodes[iBlock].m_iParent == iChild;
						}

						if (i < m_BlockEnds.f_GetLen() && m_BlockEnds[i] <= Child.m_iLastToken && (fp_IsFirstOnLine(i) || bOwnBody))
						{
							auto nBraceIndent = aint(fp_GetStatementIndent(i)) + _nDelta;
							if (fMoves(i) && fp_IsFirstOnLine(i) && nBraceIndent >= 0)
								fp_IndentBefore(i, umint(nBraceIndent));

							i = m_BlockEnds[i];

							continue;
						}

						// A comment starting one of its lines moves with them.
						auto const &Token = Tokens[i];
						bool bComment = Token.m_Kind == ECodeTokenKind::mc_LineComment || Token.m_Kind == ECodeTokenKind::mc_BlockComment;
						if ((!fg_IsCodeToken(Token) && !bComment) || !fMoves(i) || !fp_IsFirstOnLine(i))
							continue;

						auto nCurrent = bComment ? (m_CommentMoved[i] ? m_CommentIndent[i] : fp_GetSourceLineIndent(i)) : fp_GetStatementIndent(i);
						auto nIndent = aint(nCurrent) + _nDelta;
						if (nIndent < 0 || m_ProtectedStart[m_Lines.f_FindLine(Tokens[i].m_iOffset)])
							continue;

						if (!bComment)
							fp_IndentBefore(i, umint(nIndent));
						else if (nCurrent)
						{
							m_CommentMoved[i] = 1;
							m_CommentIndent[i] = umint(nIndent);
						}
					}

					// The comment lines in front of it that a stretch moved with it move on with it.
					auto iCommentsFirst = iPrevious != TCLimitsInt<umint>::mc_Max ? Nodes[iPrevious].m_iLastToken + 1 : Node.m_iFirstToken + 1;
					for (auto i = iCommentsFirst; i < iFirst; ++i)
					{
						auto Kind = Tokens[i].m_Kind;
						bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
						auto nIndent = aint(m_CommentIndent[i]) + _nDelta;
						if (bComment && m_CommentMoved[i] && fp_IsFirstOnLine(i) && nIndent >= 0)
							m_CommentIndent[i] = umint(nIndent);
					}
				}
			;

			// The lines move only with their first, whose move a range or a disabled region can leave out: the edit in the gap
			// in front of it is kept only where the range holds the gap and formatting is on there.
			auto iGapStart = iFirst;
			while (iGapStart && (Tokens[iGapStart - 1].m_Kind == ECodeTokenKind::mc_Whitespace || Tokens[iGapStart - 1].m_Kind == ECodeTokenKind::mc_Newline))
				--iGapStart;

			auto iGapOffset = iGapStart < iFirst ? Tokens[iGapStart].m_iOffset : Tokens[iFirst].m_iOffset;
			auto nGap = Tokens[iFirst].m_iOffset - iGapOffset;
			bool bFirstMoves = fp_IsSelected(iGapOffset, nGap) && !fp_IsDisabled(iGapOffset, nGap) && !fp_IsFormatOff(iGapOffset, nGap);
			bool bMovesLines = bPlaced && !bBehindComment && (bEnumerators || !fp_KeepsOwnLines(iChild, true));
			if (!bStays && !bFirstOnLine)
			{
				// One behind code or a comment on its line takes a line of its own, and its other lines move as far as the
				// line it started on: '{ /* B */ return' over '0; }'.
				auto nLineWritten = fp_GetSourceLineIndent(iFirst);
				if (bMovesLines && bFirstMoves && nLineWritten != nPlace)
					fMoveLines(aint(nPlace) - aint(nLineWritten));

				fp_OwnLineBefore(iFirst, nPlace);
				nWritten = nPlace;
			}
			else if (!bStays && bMovesLines && nWritten != nPlace)
			{
				if (bFirstMoves)
					fMoveLines(aint(nPlace) - aint(nWritten));

				fp_IndentBefore(iFirst, nPlace);
				nWritten = nPlace;
			}

			// A template's headers and the declaration under them stand at the statement's indentation; a requires
			// clause behind a header is the layout's to place.
			for
			(
				auto iHead = iFirst
				, iNext = fp_SkipTemplateHeader(iFirst)
				; !bStays && bPlaced && iNext != iHead && iNext <= Child.m_iLastToken
				; iHead = iNext
				, iNext = fp_SkipTemplateHeader(iNext)
			)
			{
				if (m_Tokens.f_IsText(Tokens[iNext], "requires"))
					break;

				// The statement's move indents the line as well, which is no placement of its own.
				auto Gap = EGap(m_GapState[iNext]);
				bool bUndecided = Gap == EGap::mc_Keep || Gap == EGap::mc_Indent;
				if (fp_IsFirstOnLine(iNext) && bUndecided && fp_GetStatementIndent(iNext) != nWritten)
					fp_IndentBefore(iNext, nWritten);
			}

			// A guarded label guards on, as the statement it labels is the guarded one: 'if (b)' over 'Retry: f();'. One a
			// conditional cuts from the guard keeps its depth, and so does what follows it.
			if (fp_IsGuard(iChild))
			{
				iGuard = iChild;
				nGuard = nWritten;
			}
			else if (!bGuarded || !bLabel || !bPlaced)
				iGuard = TCLimitsInt<umint>::mc_Max;

			nPlaced = nWritten;
			iPrevious = iChild;
		}
	}

	// Whether the conditional the '#endif' ends leaves a guard in front of what follows it in every configuration: it has
	// an '#else', and each of its branches ends in a guard. 'if (a)' '#else' 'if (b)' '#endif' guards what follows.
	bool CFormattingAnalyzer::fp_IsGuardedInEveryBranch(umint _iEndif) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();

		auto fEndsInGuard = [&](umint _iDirective)
			{
				auto iBefore = fp_PreviousCode(_iDirective);
				if (iBefore < 0)
					return false;

				auto iStatement = m_Structure.f_FindStatementEndingAt(umint(iBefore));

				return iStatement < Nodes.f_GetLen() && fp_IsGuard(iStatement);
			}
		;

		if (!fEndsInGuard(_iEndif))
			return false;

		umint nDepth = 0;
		bool bElse = false;
		for (auto i = _iEndif; i > 0; --i)
		{
			auto iDirective = i - 1;
			if (Tokens[iDirective].m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			auto Keyword = fp_GetDirectiveKeyword(iDirective);
			if (Keyword == "endif")
				++nDepth;
			else if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
			{
				if (!nDepth)
					return bElse;

				--nDepth;
			}
			else if (!nDepth && (Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef"))
			{
				bElse |= Keyword == "else";
				if (!fEndsInGuard(iDirective))
					return false;
			}
		}

		return false;
	}

	// A body a directive cuts from its head is a statement of its own, which stands under the line its head starts once
	// that is laid out: 'if (a)' or '} else' over '#endif' over '{'.
	void CFormattingAnalyzer::fp_PlaceCutBody(umint _iHead, umint _iBody)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Head = Nodes[_iHead];
		auto iBody = Nodes[_iBody].m_iFirstToken;

		// A label ends a statement of its own, which the brace behind it does not belong to.
		auto const &HeadLast = Tokens[Head.m_iLastToken];
		if (m_Tokens.f_IsText(HeadLast, ";") || m_Tokens.f_IsText(HeadLast, "}") || m_Tokens.f_IsText(HeadLast, ":"))
			return;

		if (!m_Tokens.f_IsText(Tokens[iBody], "{") || m_BlockEnds[iBody] >= Tokens.f_GetLen() || !fp_IsFirstOnLine(iBody))
			return;

		// One behind an alternative branch is never compiled with a head in front of it.
		bool bCut = false;
		bool bAlternative = false;
		for (auto i = Head.m_iLastToken + 1; i < iBody; ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			bCut |= m_OpaqueDirective[i];
			auto Keyword = fp_GetDirectiveKeyword(i);
			bAlternative |= Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef";
		}

		auto Gap = EGap(m_GapState[iBody]);
		bool bUndecided = Gap == EGap::mc_Keep || Gap == EGap::mc_Indent;
		if (!bCut || bAlternative || !bUndecided || m_ProtectedStart[m_Lines.f_FindLine(Tokens[iBody].m_iOffset)])
			return;

		// The head starts behind the last terminator or brace at the body's depth, and a head behind a closing brace on
		// its line stands where that line does.
		auto iHead = iBody;
		for (auto iBefore = fp_PreviousCode(iBody); iBefore >= 0; iBefore = fp_PreviousCode(umint(iBefore)))
		{
			auto const &Before = Tokens[umint(iBefore)];
			if (m_TokenDepth[umint(iBefore)] < m_TokenDepth[iBody])
				break;

			if (m_TokenDepth[umint(iBefore)] == m_TokenDepth[iBody] && (m_Tokens.f_IsText(Before, ";") || m_Tokens.f_IsText(Before, "{") || m_Tokens.f_IsText(Before, "}")))
				break;

			iHead = umint(iBefore);
		}

		if (iHead == iBody)
			return;

		auto iHeadLine = iHead;
		if (!fp_IsFirstOnLine(iHead))
		{
			iHeadLine = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[iHead].m_iOffset)));
			while (iHeadLine < iHead && !fg_IsCodeToken(Tokens[iHeadLine]))
				++iHeadLine;
		}

		auto nIndent = fp_GetStatementIndent(iHeadLine);
		if (fp_GetStatementIndent(iBody) != nIndent)
			fp_IndentBefore(iBody, nIndent);
	}

	void CFormattingAnalyzer::fp_LayoutNode(umint _iNode, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		switch (Node.m_Kind)
		{
		case ECodeNodeKind::mc_File:
			{
				fp_LayoutBlockLines(_iNode, 0);

				for (auto iChild : Node.m_Children)
					fp_LayoutNode(iChild, 0);

				return;
			}
		case ECodeNodeKind::mc_Block:
			{
				// A block whose brace still shares a line has no level to place its lines at;
				// the head that owns the brace decides where it goes first.
				if (fp_IsFirstOnLine(Node.m_iFirstToken))
					fp_LayoutBlockLines(_iNode, fp_GetStatementIndent(Node.m_iFirstToken));

				auto nTab = m_Request.m_Settings.m_nTabWidth;
				for (umint iIndex = 0; iIndex < Node.m_Children.f_GetLen(); ++iIndex)
				{
					if (iIndex)
						fp_PlaceCutBody(Node.m_Children[iIndex - 1], Node.m_Children[iIndex]);

					fp_LayoutNode(Node.m_Children[iIndex], _iIndent + nTab);
				}

				return;
			}
		case ECodeNodeKind::mc_Unsupported: return;
		default: break;
		}

		if (Node.m_Kind != ECodeNodeKind::mc_Statement)
			return;

		if (Node.m_bFileElements)
		{
			fp_LayoutFileElements(_iNode);

			return;
		}

		// A statement that opens with an attribute on its clause's line, 'if (x)
		// [[unlikely]]' with the block below, has no line of its own to be measured
		// from: it is laid out at the clause's indentation, where its block opens. The
		// 'if' of an 'else if' is laid out against the line the 'else' starts.
		auto iElse = fp_LeadingElse(Node.m_iFirstToken);
		auto nIndent = fp_GetStatementIndent(iElse >= 0 ? umint(iElse) : Node.m_iFirstToken);
		auto iSecond = fp_NextCode(Node.m_iFirstToken);
		bool bAttribute = m_Tokens.f_IsText(m_Tokens.f_GetTokens()[Node.m_iFirstToken], "[") && iSecond >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iSecond)], "[");
		if (bAttribute && !fp_IsFirstOnLine(Node.m_iFirstToken) && Node.m_iParent < Nodes.f_GetLen())
		{
			umint iPrevious = TCLimitsInt<umint>::mc_Max;
			for (auto iSibling : Nodes[Node.m_iParent].m_Children)
			{
				if (iSibling == _iNode)
					break;

				iPrevious = iSibling;
			}

			if (iPrevious != TCLimitsInt<umint>::mc_Max && fp_IsGuard(iPrevious))
				nIndent = fp_GetStatementIndent(Nodes[iPrevious].m_iFirstToken);
		}

		fp_LayoutStatement(_iNode, nIndent);
	}

	void CFormattingAnalyzer::fp_RuleLineBreaks()
	{
		// Every gap starts out keeping what the source has, so the rules that read the
		// decisions have them to read even where no layout is made.
		auto nTokens = m_Tokens.f_GetTokens().f_GetLen();
		m_GapState.f_SetLen(nTokens);
		m_GapIndent.f_SetLen(nTokens);
		m_CommentMoved.f_SetLen(nTokens);
		m_CommentIndent.f_SetLen(nTokens);

		for (umint i = 0; i < nTokens; ++i)
		{
			m_GapState[i] = uint8(EGap::mc_Keep);
			m_GapIndent[i] = 0;
			m_CommentMoved[i] = 0;
			m_CommentIndent[i] = 0;
		}

		// Brackets that do not nest as written make every line position a guess, so the
		// file keeps its layout. Say so rather than silently leaving it unformatted.
		if (!m_Structure.f_IsComplete())
		{
			// A conditional whose branches each spell a piece of one construct is the one
			// shape where brackets that do not nest is what the source means, so it is
			// named as itself rather than reported as the construct it left open.
			bool bConditional = !m_Conditionals.f_IsEmpty();
			if (!m_bProbing)
			{
				CStr Explanation = "this construct's brackets do not nest as written, so the file's line structure was left alone";
				auto iOffset = m_Structure.f_GetIncompleteOffset();
				if (m_Structure.f_IsTooDeep())
					Explanation = "brackets nest deeper than {} levels here, so the file's line structure was left alone"_f << CCodeStructure::mc_MaxBracketDepth;
				else if (bConditional)
				{
					Explanation = "the branches of this conditional each hold a piece of one construct, so the file's line structure was left alone";

					// An enclosing conditional holds the pieces of the one inside it too; the innermost is the one to name.
					iOffset = m_Conditionals[0].m_iOffset;
					for (auto const &Conditional : m_Conditionals)
					{
						bool bEncloses = false;
						for (auto const &Other : m_Conditionals)
							bEncloses = bEncloses || (&Other != &Conditional && Other.m_iOffset > Conditional.m_iOffset && Other.f_GetEnd() <= Conditional.f_GetEnd());

						if (!bEncloses)
						{
							iOffset = Conditional.m_iOffset;

							break;
						}
					}
				}

				fp_AddDiagnostic("structure", iOffset, 0, Explanation, false);
			}

			return;
		}

		fp_PrepareBlockEnds();
		fp_LayoutNode(0, 0);

		// A braced list whose elements all stand on its opening brace's line was never
		// opened, so a closing brace left on a line of its own goes back behind the last
		// of them: '{1, 0' over '}' is '{1, 0}'. An opened list around it keeps its lines,
		// which is what would leave such a brace where it is.
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto const &Node : m_Structure.f_GetNodes())
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Brace || !fp_IsFirstOnLine(Node.m_iLastToken))
				continue;

			auto iLast = fp_PreviousCode(Node.m_iLastToken);
			if (iLast < 0 || umint(iLast) <= Node.m_iFirstToken)
				continue;

			// Only a list the layout left as the source wrote it, closing brace included.
			bool bOneLine = EGap(m_GapState[Node.m_iLastToken]) == EGap::mc_Keep;
			for (auto i = Node.m_iFirstToken + 1; i < Node.m_iLastToken && bOneLine; ++i)
			{
				if (fp_IsBreakGap(i))
				{
					bOneLine = false;

					break;
				}

				auto Kind = Tokens[i].m_Kind;
				if (i > umint(iLast))
					bOneLine = Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline;
				else
					bOneLine = Kind != ECodeTokenKind::mc_Newline && Kind != ECodeTokenKind::mc_LineComment && Kind != ECodeTokenKind::mc_Preprocessor;
			}

			if (bOneLine && !Tokens[umint(iLast)].m_bMultiLine)
				fp_MarkInline(umint(iLast), Node.m_iLastToken);
		}

		// A comparison's '<' behind a name with a line break on both sides reads as a template argument list opening, so
		// its operand stays on its line: 'aName' over '< (' over the operand's lines.
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (!m_Tokens.f_IsText(Tokens[i], "<") || m_Structure.f_IsAngleBracket(i))
				continue;

			auto iName = fp_PreviousCode(i);
			auto iOperand = fp_NextCode(i);
			if (iName < 0 || iOperand < 0 || !fp_IsBreakGap(umint(iOperand)))
				continue;

			auto const &Name = Tokens[umint(iName)];
			if (Name.m_Kind != ECodeTokenKind::mc_Identifier && !m_Tokens.f_IsText(Name, "]"))
				continue;

			bool bPlain = true;
			for (auto iGap = i + 1; iGap < umint(iOperand) && bPlain; ++iGap)
			{
				auto GapKind = Tokens[iGap].m_Kind;
				bPlain = GapKind == ECodeTokenKind::mc_Whitespace || GapKind == ECodeTokenKind::mc_Newline;
			}

			if (bPlain)
				fp_MarkInline(i, umint(iOperand));
		}
	}
}
