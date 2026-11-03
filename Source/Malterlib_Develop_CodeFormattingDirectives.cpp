// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

	// The name the directive spells, such as 'if' or 'endif'.
	// The lines a span of the source stands on, the newline ending the last one included.
	CCodeFormattingRange fg_LineSpan(CTextLineMap const &_Lines, umint _iOffset, umint _iEnd, umint _nSource)
	{
		auto iLast = _Lines.f_FindLine(_iEnd ? _iEnd - 1 : 0);
		auto iEnd = iLast + 1 < _Lines.f_GetLineCount() ? _Lines.f_GetLineStart(iLast + 1) : _nSource;
		auto iStart = _Lines.f_GetLineStart(_Lines.f_FindLine(_iOffset));

		return CCodeFormattingRange{iStart, iEnd - iStart};
	}
}

namespace NMib::NDevelop::NPrivate
{
	void CFormattingAnalyzer::fp_PrepareBlockEnds()
	{
		auto nTokens = mp_Tokens.f_GetTokens().f_GetLen();

		mp_BlockEnds.f_SetLen(nTokens);
		for (auto &iEnd : mp_BlockEnds)
			iEnd = nTokens;

		mp_OpenListEnds.f_SetLen(nTokens);
		for (auto &iEnd : mp_OpenListEnds)
			iEnd = nTokens;

		auto const &Nodes = mp_Structure.f_GetNodes();
		auto const &Tokens = mp_Tokens.f_GetTokens();

		mp_KeepsListLines.f_SetLen(Nodes.f_GetLen());
		for (auto &bKeeps : mp_KeepsListLines)
			bKeeps = 0;

		for (umint iNode = 0; iNode < Nodes.f_GetLen(); ++iNode)
		{
			auto const &Node = Nodes[iNode];
			if (Node.m_Kind == ECodeNodeKind::mc_Block && Node.m_Bracket == ECodeBracket::mc_Brace)
				mp_BlockEnds[Node.m_iFirstToken] = Node.m_iLastToken;

			if (!Node.m_bWrittenOpen)
				continue;

			// A list holding a directive keeps its lines, and so does one a layout directive or a disabled region owns, and
			// with it the constructs around it.
			auto iInside = Tokens[Node.m_iFirstToken].f_GetEnd();
			bool bKeeps = Node.m_bHasDirective || fp_IsDisabled(iInside, Tokens[Node.m_iLastToken].m_iOffset - iInside);
			if (!bKeeps)
			{
				mp_OpenListEnds[Node.m_iFirstToken] = Node.m_iLastToken;

				continue;
			}

			for (auto iKeeps = iNode; iKeeps < Nodes.f_GetLen() && !mp_KeepsListLines[iKeeps]; iKeeps = iKeeps ? Nodes[iKeeps].m_iParent : Nodes.f_GetLen())
				mp_KeepsListLines[iKeeps] = 1;
		}
	}

	// Finds the conditionals whose branches each leave the same brackets open and close none they did not open, the
	// alternative heads of one body. False when a conditional is cut some other way, never closes, or holds alternative
	// heads inside alternative heads.
	bool CFormattingAnalyzer::f_FindAlternativeHeads(TCVector<CAlternativeHeads> &o_Groups) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto nSource = mp_Request.m_Source.f_GetLen();
		TCVector<TCVector<umint>> Groups;
		TCVector<umint> Open;

		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			if (Tokens[iToken].m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			auto Keyword = fp_GetDirectiveKeyword(iToken);
			if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
			{
				Open.f_Insert(Groups.f_GetLen());
				Groups.f_Insert().f_Insert(iToken);

				continue;
			}

			bool bBranch = Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef";
			if (!bBranch && Keyword != "endif")
				continue;

			if (Open.f_IsEmpty())
				return false;

			Groups[Open.f_GetLast()].f_Insert(iToken);
			if (!bBranch)
				Open.f_Remove(Open.f_GetLen() - 1);
		}

		if (!Open.f_IsEmpty())
			return false;

		// An enclosing conditional reads one alternative of each it holds, as the structure built from a variant does.
		TCVector<bool> Skipped;
		Skipped.f_SetLen(Tokens.f_GetLen());
		for (auto &bSkipped : Skipped)
			bSkipped = false;

		for (umint iGroup = Groups.f_GetLen(); iGroup--;)
		{
			auto const &Directives = Groups[iGroup];
			bool bBalanced = true;
			bool bSameOpen = Directives.f_GetLen() > 2;
			CStr FirstOpen;

			for (umint iBranch = 0; iBranch + 1 < Directives.f_GetLen(); ++iBranch)
			{
				CStr Opened;
				bool bUnderflow = false;

				for (auto iToken = Directives[iBranch] + 1; iToken < Directives[iBranch + 1]; ++iToken)
				{
					auto const &Token = Tokens[iToken];
					if (Token.m_Kind != ECodeTokenKind::mc_Punctuator || Skipped[iToken])
						continue;

					if (mp_Tokens.f_IsText(Token, "(") || mp_Tokens.f_IsText(Token, "[") || mp_Tokens.f_IsText(Token, "{"))
						Opened.f_AddChar(mp_pSource[Token.m_iOffset]);
					else if (mp_Tokens.f_IsText(Token, ")") || mp_Tokens.f_IsText(Token, "]") || mp_Tokens.f_IsText(Token, "}"))
					{
						if (Opened.f_IsEmpty())
							bUnderflow = true;
						else
							Opened = Opened.f_Left(Opened.f_GetLen() - 1);
					}
				}

				bBalanced = bBalanced && !bUnderflow && Opened.f_IsEmpty();
				bSameOpen = bSameOpen && !bUnderflow && !Opened.f_IsEmpty() && (iBranch == 0 || Opened == FirstOpen);
				if (iBranch == 0)
					FirstOpen = Opened;
			}

			if (bBalanced)
				continue;

			if (!bSameOpen)
				return false;

			for (auto iToken = Directives[1]; iToken < Directives.f_GetLast(); ++iToken)
				Skipped[iToken] = true;

			auto &Group = o_Groups.f_Insert();

			for (umint iDirective = 0; iDirective < Directives.f_GetLen(); ++iDirective)
			{
				auto const &Directive = Tokens[Directives[iDirective]];
				auto Line = fg_LineSpan(mp_Lines, Directive.m_iOffset, Directive.f_GetEnd(), nSource);
				Group.m_Directives.f_Insert(Line);

				if (iDirective)
				{
					auto const &Previous = Group.m_Directives[iDirective - 1];
					auto iBranchStart = Previous.m_iOffset + Previous.m_nLength;
					Group.m_Branches.f_Insert(CCodeFormattingRange{iBranchStart, Line.m_iOffset - iBranchStart});
				}
			}
		}

		// Alternative heads inside a branch of others would need a variant for every pairing.
		for (auto const &Outer : o_Groups)
		{
			for (auto const &Inner : o_Groups)
			{
				if (&Inner == &Outer)
					continue;

				auto iInner = Inner.m_Directives[0].m_iOffset;
				auto iOuterStart = Outer.m_Directives[0].m_iOffset;
				auto iOuterEnd = Outer.m_Directives.f_GetLast().m_iOffset;
				if (iInner > iOuterStart && iInner < iOuterEnd)
					return false;
			}
		}

		return true;
	}

	// The lines every directive of the source stands on, with the line ending that ends them, which the plain analysis writes
	// as it does the rest of the line.
	TCVector<CCodeFormattingRange> CFormattingAnalyzer::f_GetDirectiveSpans() const
	{
		TCVector<CCodeFormattingRange> Spans;

		for (auto const &Token : mp_Tokens.f_GetTokens())
		{
			if (Token.m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			auto iLast = mp_Lines.f_FindLine(Token.f_GetEnd() ? Token.f_GetEnd() - 1 : 0);
			auto iStart = mp_Lines.f_GetLineStart(mp_Lines.f_FindLine(Token.m_iOffset));
			auto iEnd = fg_Max(mp_Lines.f_GetLineEnd(iLast), Token.f_GetEnd());
			Spans.f_Insert(CCodeFormattingRange{iStart, iEnd - iStart});
		}

		return Spans;
	}

	CStr CFormattingAnalyzer::fp_GetDirectiveKeyword(umint _iToken) const
	{
		auto const &Token = mp_Tokens.f_GetTokens()[_iToken];
		ch8 Keyword[16];
		umint iAfter = 0;
		auto nKeyword = fg_ReadDirectiveKeyword(mp_pSource + Token.m_iOffset, Token.m_nLength, Keyword, iAfter);
		if (nKeyword > sizeof(Keyword))
			return CStr(mp_pSource + Token.m_iOffset, Token.m_nLength); // No keyword is that long, so it matches none.

		// The standard's keywords are constants, so asking for one allocates nothing.
		static constexpr CStr const *c_pKeywords[] =
			{
				&gc_Str<"if">.m_Str
				, &gc_Str<"ifdef">.m_Str
				, &gc_Str<"ifndef">.m_Str
				, &gc_Str<"elif">.m_Str
				, &gc_Str<"elifdef">.m_Str
				, &gc_Str<"elifndef">.m_Str
				, &gc_Str<"else">.m_Str
				, &gc_Str<"endif">.m_Str
				, &gc_Str<"define">.m_Str
				, &gc_Str<"undef">.m_Str
				, &gc_Str<"include">.m_Str
				, &gc_Str<"include_next">.m_Str
				, &gc_Str<"import">.m_Str
				, &gc_Str<"pragma">.m_Str
				, &gc_Str<"error">.m_Str
				, &gc_Str<"warning">.m_Str
				, &gc_Str<"line">.m_Str
			}
		;

		for (auto pKeyword : c_pKeywords)
		{
			if (fg_IsSameText(*pKeyword, Keyword, nKeyword))
				return *pKeyword;
		}

		return CStr(Keyword, nKeyword);
	}

	// True when nothing the directive stands in the middle of is cut by it: every construct
	// spanning the directive spans the whole conditional group, so the branches are
	// alternatives inside one construct rather than pieces of one spread over several.
	bool CFormattingAnalyzer::fp_IsDirectiveParallel(umint _iDirective, umint _iOpen, umint _iClose) const
	{
		auto const &Nodes = mp_Structure.f_GetNodes();
		if (Nodes.f_IsEmpty())
			return false;

		umint iNode = 0;
		for (;;)
		{
			auto const &Node = Nodes[iNode];

			// A directive is trivia, so a node never starts or ends on one: a node spanning
			// the group has its first token in front of the opening directive and its last
			// behind the closing one.
			if (iNode && (Node.m_iFirstToken > _iOpen || Node.m_iLastToken < _iClose))
				return false;

			umint iNext = iNode;
			for (auto iChild : Node.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken < _iDirective && Child.m_iLastToken > _iDirective)
				{
					iNext = iChild;

					break;
				}
			}

			if (iNext == iNode)
			{
				// A branch that ends on a clause leaves the statement it guards to the next
				// one, which is a construct cut in two that no node shows: the builder ends
				// a clause at its condition, so the pieces are separate children.
				umint iBefore = TCLimitsInt<umint>::mc_Max;
				for (auto iChild : Node.m_Children)
				{
					if (Nodes[iChild].m_iLastToken < _iDirective)
						iBefore = iChild;
				}

				return iBefore == TCLimitsInt<umint>::mc_Max || !fp_IsGuard(iBefore);
			}

			iNode = iNext;
		}
	}

	// A conditional group holds alternatives: the token stream reads its branches one after
	// another, which is the same shape as any single branch only where no construct is cut
	// by a branch boundary. Where one is, every directive of the group is opaque and the
	// construct around it keeps the lines the source gave it, as before. A directive that
	// is transparent only fixes the line it stands on, exactly as a line comment does.
	void CFormattingAnalyzer::fp_PrepareDirectives()
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();

		mp_OpaqueDirective.f_SetLen(Tokens.f_GetLen());
		for (auto &bOpaque : mp_OpaqueDirective)
			bOpaque = 0;

		struct CConditional
		{
			TCVector<umint> m_Directives;
			bool m_bClosed = false;
		};

		TCVector<CConditional> Conditionals;
		TCVector<umint> Open;

		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			if (Tokens[iToken].m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			auto Keyword = fp_GetDirectiveKeyword(iToken);
			if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
			{
				Open.f_Insert(Conditionals.f_GetLen());
				Conditionals.f_Insert().m_Directives.f_Insert(iToken);

				continue;
			}

			bool bBranch = Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef";
			if (!bBranch && Keyword != "endif")
				continue;

			// A branch or an end whose opening directive the file does not hold leaves the
			// group unknown, so nothing may be read across it.
			if (Open.f_IsEmpty())
			{
				mp_OpaqueDirective[iToken] = 1;

				continue;
			}

			auto &Conditional = Conditionals[Open.f_GetLast()];
			Conditional.m_Directives.f_Insert(iToken);
			if (!bBranch)
			{
				Conditional.m_bClosed = true;
				Open.f_Remove(Open.f_GetLen() - 1);
			}
		}

		for (auto const &Conditional : Conditionals)
		{
			auto const &Directives = Conditional.m_Directives;

			// A branch that opens a bracket it does not close, or closes one it did not
			// open, spells a piece of a construct rather than a whole alternative. The
			// token stream then holds no construct the file ever compiles, and the file is
			// reported as such rather than laid out against a shape nothing has.
			bool bBalanced = Conditional.m_bClosed;
			for (umint iBranch = 0; bBalanced && iBranch + 1 < Directives.f_GetLen(); ++iBranch)
			{
				aint nDepth = 0;
				aint nLowest = 0;

				for (auto iToken = Directives[iBranch] + 1; iToken < Directives[iBranch + 1]; ++iToken)
				{
					auto const &Token = Tokens[iToken];
					if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
						continue;

					if (mp_Tokens.f_IsText(Token, "(") || mp_Tokens.f_IsText(Token, "[") || mp_Tokens.f_IsText(Token, "{"))
						++nDepth;
					else if (mp_Tokens.f_IsText(Token, ")") || mp_Tokens.f_IsText(Token, "]") || mp_Tokens.f_IsText(Token, "}"))
					{
						--nDepth;
						nLowest = nDepth < nLowest ? nDepth : nLowest;
					}
				}

				bBalanced = !nDepth && !nLowest;
			}

			bool bParallel = bBalanced;
			for (umint iDirective = 0; bParallel && iDirective < Directives.f_GetLen(); ++iDirective)
				bParallel = fp_IsDirectiveParallel(Directives[iDirective], Directives[0], Directives.f_GetLast());

			if (bParallel)
				continue;

			if (!bBalanced)
			{
				auto &Span = mp_Conditionals.f_Insert();
				Span.m_iOffset = Tokens[Directives[0]].m_iOffset;
				Span.m_nLength = Tokens[Directives.f_GetLast()].f_GetEnd() - Span.m_iOffset;
			}

			for (auto iDirective : Directives)
				mp_OpaqueDirective[iDirective] = 1;
		}

		mp_OpaqueBefore.f_SetLen(Tokens.f_GetLen() + 1);
		mp_OpaqueBefore[0] = 0;
		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
			mp_OpaqueBefore[iToken + 1] = mp_OpaqueBefore[iToken] + mp_OpaqueDirective[iToken];
	}

	bool CFormattingAnalyzer::fp_SpansLines(umint _iFirst, umint _iLast) const
	{
		return mp_Lines.f_FindLine(mp_Tokens.f_GetTokens()[_iFirst].m_iOffset) != mp_Lines.f_FindLine(mp_Tokens.f_GetTokens()[_iLast].f_GetEnd() - 1);
	}

	// A node the builder could not close names a token past the end, so the range is taken
	// to where the source does end.
	bool CFormattingAnalyzer::fp_HasOpaqueDirective(umint _iFirst, umint _iLast) const
	{
		auto nTokens = mp_Tokens.f_GetTokens().f_GetLen();
		if (_iFirst >= nTokens)
			return false;

		return mp_OpaqueBefore[(_iLast < nTokens ? _iLast : nTokens - 1) + 1] > mp_OpaqueBefore[_iFirst];
	}

	// The indentation of the line the token stands on in the source, whatever the layout
	// has decided since: the lines a body was written against.
	umint CFormattingAnalyzer::fp_GetSourceLineIndent(umint _iToken) const
	{
		auto iLine = mp_Lines.f_FindLine(mp_Tokens.f_GetTokens()[_iToken].m_iOffset);
		auto iStart = mp_Lines.f_GetLineStart(iLine);
		auto iEnd = mp_Lines.f_GetLineContentEnd(iLine);
		auto iIndent = iStart;
		while (iIndent < iEnd && fg_IsSpaceOrTab(mp_pSource[iIndent]))
			++iIndent;

		umint nColumns = 0;
		if (!fg_MeasureTextColumns(mp_pSource + iStart, iIndent - iStart, mp_Request.m_Settings.m_nTabWidth, nColumns))
			return 0;

		return nColumns;
	}
}
