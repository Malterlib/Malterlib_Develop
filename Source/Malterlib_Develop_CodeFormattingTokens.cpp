// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"
#include "Malterlib_Develop_CodeFormattingStructurePrivate.h"

namespace NMib::NDevelop::NPrivate
{
	aint CFormattingAnalyzer::fp_PreviousSignificant(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		aint iToken = aint(_iToken) - 1;

		while (iToken >= 0 && Tokens[umint(iToken)].m_Kind == ECodeTokenKind::mc_Whitespace)
			--iToken;

		return iToken;
	}

	aint CFormattingAnalyzer::fp_NextSignificant(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		umint iToken = _iToken + 1;

		while (iToken < Tokens.f_GetLen() && Tokens[iToken].m_Kind == ECodeTokenKind::mc_Whitespace)
			++iToken;

		return iToken < Tokens.f_GetLen() ? aint(iToken) : -1;
	}

	// The previous token that carries meaning, skipping layout and comments.
	aint CFormattingAnalyzer::fp_PreviousCode(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		aint iToken = aint(_iToken) - 1;

		while (iToken >= 0)
		{
			switch (Tokens[umint(iToken)].m_Kind)
			{
			case ECodeTokenKind::mc_ByteOrderMark:
			case ECodeTokenKind::mc_Whitespace:
			case ECodeTokenKind::mc_Newline:
			case ECodeTokenKind::mc_LineSplice:
			case ECodeTokenKind::mc_LineComment:
			case ECodeTokenKind::mc_BlockComment:
			case ECodeTokenKind::mc_Preprocessor:
				--iToken;

				continue;
			default: return iToken;
			}
		}

		return -1;
	}

	// The next token that carries meaning, skipping layout and comments.
	aint CFormattingAnalyzer::fp_NextCode(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		umint iToken = _iToken + 1;

		while (iToken < Tokens.f_GetLen())
		{
			switch (Tokens[iToken].m_Kind)
			{
			case ECodeTokenKind::mc_ByteOrderMark:
			case ECodeTokenKind::mc_Whitespace:
			case ECodeTokenKind::mc_Newline:
			case ECodeTokenKind::mc_LineSplice:
			case ECodeTokenKind::mc_LineComment:
			case ECodeTokenKind::mc_BlockComment:
			case ECodeTokenKind::mc_Preprocessor:
				++iToken;

				continue;
			default: return aint(iToken);
			}
		}

		return -1;
	}

	bool CFormattingAnalyzer::fp_IsFirstOnLine(umint _iToken) const
	{
		if (fp_IsBreakGap(_iToken))
			return true;

		auto iPrevious = fp_PreviousSignificant(_iToken);

		return iPrevious < 0 || mp_Tokens.f_GetTokens()[umint(iPrevious)].m_bMultiLine;
	}

	// A DSL's array or object, '_o[1, 2]' or '{"Key"_o= 5', the second told by its first key, which the DSL's marker makes one.
	bool CFormattingAnalyzer::fp_IsDSLData(umint _iNode) const
	{
		auto const &Node = mp_Structure.f_GetNodes()[_iNode];
		auto const &Tokens = mp_Tokens.f_GetTokens();

		if (Node.m_Kind != ECodeNodeKind::mc_Group)
			return false;

		auto iMarker = fp_PreviousCode(Node.m_iFirstToken);
		if (Node.m_Bracket == ECodeBracket::mc_Square)
			return iMarker >= 0 && mp_Tokens.f_HasRole(Tokens[umint(iMarker)], ECodeNameRole::mc_DSLMarker);

		auto iFirstElement = fp_NextCode(Node.m_iFirstToken);
		auto iKeyMarker = iFirstElement >= 0 ? fp_NextCode(umint(iFirstElement)) : aint(-1);
		auto iKeyAssign = iKeyMarker >= 0 ? fp_NextCode(umint(iKeyMarker)) : aint(-1);

		return Node.m_Bracket == ECodeBracket::mc_Brace
			&& iFirstElement >= 0
			&& Tokens[umint(iFirstElement)].m_Kind == ECodeTokenKind::mc_StringLiteral
			&& iKeyMarker >= 0
			&& mp_Tokens.f_HasRole(Tokens[umint(iKeyMarker)], ECodeNameRole::mc_DSLMarker)
			&& iKeyAssign >= 0
			&& mp_Tokens.f_IsText(Tokens[umint(iKeyAssign)], "=")
		;
	}

	// Two words, whose gap the standard leaves as written on a line, '__declspec(x)   int' among aligned ones, and which
	// one space joins where a line break stands between them: 'ares_uint64_t' over 'm_Total;'. A macro can expand to
	// anything, a statement or a parameter of a list included, and is no such word: 'DMibSuppressStart' over 'int a;',
	// 'auto tf_pMember' over 'DMibIfNotSupported(, uint32 tf_NameHash)'.
	bool CFormattingAnalyzer::fp_AreWords(umint _iLeft, umint _iRight) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto const &Left = Tokens[_iLeft];
		auto const &Right = Tokens[_iRight];

		return Left.m_Kind == ECodeTokenKind::mc_Identifier
			&& Right.m_Kind == ECodeTokenKind::mc_Identifier
			&& !mp_Tokens.f_HasRole(Left, ECodeNameRole::mc_Macro)
			&& !mp_Tokens.f_HasRole(Right, ECodeNameRole::mc_Macro)
		;
	}

	// How a line break between two tokens is spelled where they are joined: as the spacing rules write them, and where
	// the standard leaves their gap as written, as two words are, with one space. An operator it does not settle, '&',
	// '&&' or '*', starting a line is spaced in front as the source spaces it behind: 'a' over '&& b' is 'a && b', where
	// a declarator hugs what it declares and has no space behind it. The space in front of one ending a line tells
	// neither apart.
	ECodeSpacing CFormattingAnalyzer::fp_GetJoinSpacing(umint _iLeft, umint _iRight) const
	{
		auto Spacing = fp_GetInlineSpacing(_iLeft, _iRight);
		if (Spacing != ECodeSpacing::mc_Preserve)
			return Spacing;

		if (fp_AreWords(_iLeft, _iRight))
			return ECodeSpacing::mc_Space;

		// A function's name behind a call's parenthesis, which no cast's is, stands one space apart: 'GLOBAL(void)' over
		// 'f(int a)'.
		auto const &AllTokens = mp_Tokens.f_GetTokens();
		auto iRightCall = fp_NextCode(_iRight);
		bool bNamesCall = iRightCall >= 0 && mp_Tokens.f_IsText(AllTokens[umint(iRightCall)], "(");
		if (mp_Tokens.f_IsText(AllTokens[_iLeft], ")") && AllTokens[_iRight].m_Kind == ECodeTokenKind::mc_Identifier && bNamesCall)
		{
			auto iGroup = mp_Structure.f_FindNodeClosingAt(_iLeft);
			auto const &Nodes = mp_Structure.f_GetNodes();
			auto iCallee = iGroup < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iGroup].m_iFirstToken) : aint(-1);
			if (iCallee >= 0 && AllTokens[umint(iCallee)].m_Kind == ECodeTokenKind::mc_Identifier)
				return ECodeSpacing::mc_Space;
		}

		auto const &Tokens = mp_Tokens.f_GetTokens();

		auto fAmbiguous = [&](umint _iToken)
			{
				auto const &Token = Tokens[_iToken];

				return mp_Tokens.f_IsText(Token, "&") || mp_Tokens.f_IsText(Token, "&&") || mp_Tokens.f_IsText(Token, "*");
			}
		;

		auto fMirror = [&](aint _iFrom, aint _iTo)
			{
				if (_iFrom < 0 || _iTo < 0)
					return ECodeSpacing::mc_Preserve;

				bool bSpace = false;
				for (auto iToken = umint(_iFrom) + 1; iToken < umint(_iTo); ++iToken)
				{
					if (Tokens[iToken].m_Kind != ECodeTokenKind::mc_Whitespace)
						return ECodeSpacing::mc_Preserve;

					bSpace = true;
				}

				return bSpace ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;
			}
		;

		// Written together the two could read as one operator, '&' over '&value' as '&&', which they stay apart from.
		if (fAmbiguous(_iRight))
		{
			auto Mirrored = fMirror(aint(_iRight), fp_NextCode(_iRight));
			if (Mirrored == ECodeSpacing::mc_None && fg_CodeTokensMerge(mp_Tokens, Tokens[_iLeft], Tokens[_iRight]))
				return ECodeSpacing::mc_Space;

			return Mirrored;
		}

		return ECodeSpacing::mc_Preserve;
	}

	// A block comment on one line with nothing else on it, which ends its line as a line comment does.
	bool CFormattingAnalyzer::fp_IsCommentLine(umint _iToken) const
	{
		auto const &Token = mp_Tokens.f_GetTokens()[_iToken];

		return Token.m_Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine && fp_IsFirstOnLine(_iToken) && fp_IsLastOnLine(_iToken);
	}

	// Whether a line comment of its own line holds a directive taken out of the code: '//#define DDebug' or '// #endif'.
	bool CFormattingAnalyzer::fp_IsCommentedDirective(umint _iToken) const
	{
		auto const &Token = mp_Tokens.f_GetTokens()[_iToken];
		if (Token.m_Kind != ECodeTokenKind::mc_LineComment || !fp_IsFirstOnLine(_iToken))
			return false;

		auto pText = mp_pSource + Token.m_iOffset;
		auto pEnd = pText + Token.m_nLength;
		auto pChar = pText + 2;

		while (pChar < pEnd && fg_IsSpaceOrTab(*pChar))
			++pChar;

		if (pChar >= pEnd || *pChar != '#')
			return false;

		++pChar;
		while (pChar < pEnd && fg_IsSpaceOrTab(*pChar))
			++pChar;

		auto pKeyword = pChar;
		while (pChar < pEnd && (fg_CharIsLowerCaseAnsiAlphabetical(*pChar) || *pChar == '_'))
			++pChar;

		if (pChar < pEnd && (fg_CharIsAnsiAlphabetical(*pChar) || fg_CharIsNumber(*pChar)))
			return false;

		static constexpr ch8 const *c_Keywords[] =
			{
				"if"
				, "ifdef"
				, "ifndef"
				, "elif"
				, "elifdef"
				, "elifndef"
				, "else"
				, "endif"
				, "define"
				, "undef"
				, "include"
				, "include_next"
				, "import"
				, "pragma"
				, "error"
				, "warning"
				, "line"
			}
		;

		auto nKeyword = umint(pChar - pKeyword);
		for (auto pCandidate : c_Keywords)
		{
			if (umint(fg_StrLen(pCandidate)) == nKeyword && NMemory::fg_MemCmp((uint8 const *)pKeyword, (uint8 const *)pCandidate, nKeyword) == 0)
				return true;
		}

		return false;
	}

	// The code the comment lines ending at the one given stand above, starting its line: the code behind the directives
	// between where a construct other than a namespace holds it, as a directive there is no level of the code's.
	aint CFormattingAnalyzer::fp_FindCodeBelowComments(umint _iLast) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto const &Nodes = mp_Structure.f_GetNodes();

		auto fInConstruct = [&](umint _iToken)
			{
				auto iNode = mp_Structure.f_FindNodeClosingAt(_iToken);
				if (iNode >= Nodes.f_GetLen())
					iNode = mp_Structure.f_FindEnclosingNode(_iToken);

				while (iNode < Nodes.f_GetLen() && iNode && Nodes[iNode].m_Kind == ECodeNodeKind::mc_Statement)
					iNode = Nodes[iNode].m_iParent;

				if (iNode >= Nodes.f_GetLen() || !iNode)
					return false;

				auto iOwner = Nodes[iNode].m_iParent;
				if (Nodes[iNode].m_Kind != ECodeNodeKind::mc_Block || iOwner >= Nodes.f_GetLen() || Nodes[iOwner].m_Kind != ECodeNodeKind::mc_Statement)
					return true;

				auto const &First = Tokens[Nodes[iOwner].m_iFirstToken];

				return !mp_Tokens.f_IsText(First, "namespace") && !mp_Tokens.f_IsText(First, "extern");
			}
		;

		bool bPastDirective = false;
		for (auto iNext = _iLast + 1; iNext < Tokens.f_GetLen(); ++iNext)
		{
			auto const &Next = Tokens[iNext];
			if (Next.m_Kind == ECodeTokenKind::mc_Whitespace || Next.m_Kind == ECodeTokenKind::mc_Newline)
				continue;

			if (fp_IsCommentedDirective(iNext) || (Next.m_Kind == ECodeTokenKind::mc_Preprocessor && !mp_OpaqueDirective[iNext]))
			{
				bPastDirective = true;

				continue;
			}

			// A line comment ends its line, behind code or a comment, and is never the code.
			if (Next.m_Kind == ECodeTokenKind::mc_LineComment || (Next.m_Kind == ECodeTokenKind::mc_BlockComment && fp_IsFirstOnLine(iNext)))
				continue;

			// Code a comment stands in front of on its line places nothing above it: 'LINT */ class C19'.
			auto iPrevious = fp_PreviousSignificant(iNext);
			bool bStartsLine = iPrevious < 0 || Tokens[umint(iPrevious)].m_Kind == ECodeTokenKind::mc_Newline;
			if (fg_IsCodeToken(Next) && bStartsLine && (!bPastDirective || fInConstruct(iNext)))
				return aint(iNext);

			return -1;
		}

		return -1;
	}

	// Whether only block comments stand in front of the token on its line, '/*bFlag=*/true': the line is the token's to
	// place, and the comments move with it.
	bool CFormattingAnalyzer::fp_StartsLineBehindComments(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		bool bComment = false;
		for (auto iToken = aint(_iToken) - 1; iToken >= 0; --iToken)
		{
			auto const &Token = Tokens[umint(iToken)];
			if (Token.m_Kind == ECodeTokenKind::mc_Whitespace)
				continue;

			if (Token.m_Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine)
			{
				bComment = true;

				continue;
			}

			return bComment && Token.m_bMultiLine;
		}

		return bComment;
	}

	// The 'else' that starts the line an 'else if' is written on, or -1. The 'if' has no
	// line of its own, so its statement is laid out against the one the 'else' starts.
	aint CFormattingAnalyzer::fp_LeadingElse(umint _iToken) const
	{
		// One the layout joins onto the 'else''s line has no line of its own either.
		auto const &Tokens = mp_Tokens.f_GetTokens();
		if (!mp_Tokens.f_IsText(Tokens[_iToken], "if") || (fp_IsFirstOnLine(_iToken) && EGap(mp_GapState[_iToken]) != EGap::mc_Inline))
			return -1;

		auto iElse = fp_PreviousCode(_iToken);
		if (iElse < 0 || !mp_Tokens.f_IsText(Tokens[umint(iElse)], "else") || !fp_IsFirstOnLine(umint(iElse)))
			return -1;

		return iElse;
	}

	bool CFormattingAnalyzer::fp_IsLastOnLine(umint _iToken) const
	{
		auto iNext = fp_NextSignificant(_iToken);

		return iNext < 0 || mp_Tokens.f_GetTokens()[umint(iNext)].m_bMultiLine;
	}

	// Decides whether an operator token has an operand on the given side. An adjacent
	// opening delimiter or separator means there is none.
	bool CFormattingAnalyzer::fp_HasOperand(umint _iToken, bool _bBefore) const
	{
		if (!_bBefore)
		{
			auto iNext = fp_NextCode(_iToken);
			if (iNext < 0)
				return false;

			_iToken = umint(iNext);
			auto const &Next = mp_Tokens.f_GetTokens()[_iToken];

			return !mp_Tokens.f_IsText(Next, ")") && !mp_Tokens.f_IsText(Next, "]") && !mp_Tokens.f_IsText(Next, "}") && !mp_Tokens.f_IsText(Next, ",") && !mp_Tokens.f_IsText(Next, ";");
		}

		auto const &Previous = mp_Tokens.f_GetTokens()[_iToken];

		return !mp_Tokens.f_IsText(Previous, "(")
			&& !mp_Tokens.f_IsText(Previous, "[")
			&& !mp_Tokens.f_IsText(Previous, "{")
			&& !mp_Tokens.f_IsText(Previous, ",")
			&& !mp_Tokens.f_IsText(Previous, ";")
		;
	}

	// The string literal in front of the token when the token continues it on a line of its
	// own, which the source asks for and the layout keeps; -1 otherwise.
	aint CFormattingAnalyzer::fp_ContinuesLiteral(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto const &Continuation = Tokens[_iToken];
		if (Continuation.m_Kind != ECodeTokenKind::mc_StringLiteral || Continuation.m_bMultiLine || !fp_IsFirstOnLine(_iToken))
			return -1;

		// A conditional's directives between two strings leave the run whole, as its branches are lines of it.
		auto fTransparent = [&](aint _iToken)
			{
				auto const &Token = Tokens[umint(_iToken)];

				return Token.m_Kind == ECodeTokenKind::mc_Newline
					|| (Token.m_Kind == ECodeTokenKind::mc_Preprocessor && umint(_iToken) < mp_OpaqueDirective.f_GetLen() && !mp_OpaqueDirective[umint(_iToken)])
				;
			}
		;

		auto iPrevious = fp_PreviousSignificant(_iToken);
		while (iPrevious >= 0 && fTransparent(iPrevious))
			iPrevious = fp_PreviousSignificant(umint(iPrevious));

		// So does a macro behind a literal on its line, a format's part: '"%" PRId32' over '" matched"'.
		auto iLiteral = iPrevious;
		if (iLiteral >= 0 && Tokens[umint(iLiteral)].m_Kind == ECodeTokenKind::mc_Identifier)
		{
			auto iInFront = fp_PreviousCode(umint(iLiteral));
			if (iInFront >= 0 && Tokens[umint(iInFront)].m_Kind == ECodeTokenKind::mc_StringLiteral && !fp_SpansLines(umint(iInFront), umint(iLiteral)))
				iLiteral = iInFront;
		}

		auto const *pLiteral = iLiteral >= 0 ? &Tokens[umint(iLiteral)] : nullptr;
		bool bContinues = pLiteral && pLiteral->m_Kind == ECodeTokenKind::mc_StringLiteral && !pLiteral->m_bMultiLine && mp_TokenDepth[umint(iLiteral)] == mp_TokenDepth[_iToken];

		return bContinues ? iPrevious : aint(-1);
	}

	// Whether the string the token continues is a value given behind an '=' or a macro's name
	// on its line: 'Value = "a"' over '"b"', 'DPrefix " a"' over '"b"'. Such a continuation
	// stands one level in from that line.
	bool CFormattingAnalyzer::fp_ContinuesValue(umint _iContinued) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iLead = fp_FindContinuedStringLead(_iContinued);
		if (iLead < 0)
			return false;

		auto const &Lead = Tokens[umint(iLead)];
		if (Lead.m_Kind == ECodeTokenKind::mc_Identifier)
			return true;

		return mp_Tokens.f_IsText(Lead, "=") && !fp_IsKeyAssign(umint(iLead));
	}

	// Whether the '=' gives a DSL key its value: '"Key"_o='. The strings a key's value
	// continues over lines stand at the key's level, and so do the operators behind them.
	bool CFormattingAnalyzer::fp_IsKeyAssign(umint _iAssign) const
	{
		auto iKey = fp_PreviousCode(_iAssign);

		return iKey >= 0 && mp_Tokens.f_HasRole(mp_Tokens.f_GetTokens()[umint(iKey)], ECodeNameRole::mc_DSLMarker);
	}

	// The first literal of the run of strings the literal ends, over the lines the run continues: a macro standing
	// between two literals on a line is part of it, '"a" DFormat " b"' over '"c"' running from '"a"'.
	umint CFormattingAnalyzer::fp_FindStringRunStart(umint _iLiteral) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iRun = _iLiteral;

		while (true)
		{
			// Back over the literals the run holds on its line, then to the line it continues.
			auto iBefore = fp_PreviousCode(iRun);
			if (iBefore >= 0 && !fp_SpansLines(umint(iBefore), iRun))
			{
				if (Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_StringLiteral)
				{
					iRun = umint(iBefore);

					continue;
				}

				auto iLiteral = fp_PreviousCode(umint(iBefore));
				bool bBetween
					= Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_Identifier
					&& iLiteral >= 0
					&& Tokens[umint(iLiteral)].m_Kind == ECodeTokenKind::mc_StringLiteral
					&& !fp_SpansLines(umint(iLiteral), umint(iBefore))
				;
				if (bBetween)
				{
					iRun = umint(iLiteral);

					continue;
				}
			}

			auto iEarlier = fp_ContinuesLiteral(iRun);
			if (iEarlier < 0)
				break;

			iRun = umint(iEarlier);
		}

		return iRun;
	}

	// The token in front of the string run the token continues, on the run's first line, or -1
	// where the token continues no string or the run starts a line the layout leaves it.
	aint CFormattingAnalyzer::fp_FindContinuedStringLead(umint _iContinued) const
	{
		auto iPrevious = fp_ContinuesLiteral(_iContinued);
		if (iPrevious < 0)
			return -1;

		// One the layout joins onto the line in front has that line's last token in front of it.
		auto iRun = fp_FindStringRunStart(umint(iPrevious));
		auto iLead = fp_PreviousCode(iRun);
		if (iLead < 0 || (fp_SpansLines(umint(iLead), iRun) && EGap(mp_GapState[iRun]) != EGap::mc_Inline))
			return -1;

		return iLead;
	}

	bool CFormattingAnalyzer::fp_EndsOperand(aint _iToken) const
	{
		if (_iToken < 0)
			return false;

		auto const &Token = mp_Tokens.f_GetTokens()[umint(_iToken)];
		switch (Token.m_Kind)
		{
		case ECodeTokenKind::mc_Number:
		case ECodeTokenKind::mc_StringLiteral:
		case ECodeTokenKind::mc_CharLiteral:
			return true;
		case ECodeTokenKind::mc_Identifier:
			{
				constexpr ch8 const *c_LeadsOperand[] =
					{
						"return"
						, "co_return"
						, "co_await"
						, "co_yield"
						, "throw"
						, "case"
						, "new"
						, "delete"
						, "else"
						, "do"
						, "sizeof"
						, "alignof"
					}
				;
				for (auto pKeyword : c_LeadsOperand)
				{
					if (mp_Tokens.f_IsText(Token, pKeyword))
						return false;
				}

				return true;
			}
		case ECodeTokenKind::mc_Punctuator:
			return mp_Tokens.f_IsText(Token, ")")
				|| mp_Tokens.f_IsText(Token, "]")
				|| mp_Tokens.f_IsText(Token, "}")
				|| mp_Tokens.f_IsText(Token, "++")
				|| mp_Tokens.f_IsText(Token, "--")
				|| (mp_Structure.f_IsAngleBracket(umint(_iToken)) && mp_Tokens.f_IsText(Token, ">"))
			;
		default: return false;
		}
	}

	void CFormattingAnalyzer::fp_PrepareTokenDepth()
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		mp_TokenDepth.f_SetLen(Tokens.f_GetLen());
		umint nDepth = 0;

		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			auto const &Token = Tokens[iToken];
			bool bClosing
				= mp_Tokens.f_IsText(Token, ")")
				|| mp_Tokens.f_IsText(Token, "]")
				|| mp_Tokens.f_IsText(Token, "}")
				|| (mp_Structure.f_IsAngleBracket(iToken) && mp_Tokens.f_IsText(Token, ">"))
			;
			if (bClosing && nDepth)
				--nDepth;

			mp_TokenDepth[iToken] = nDepth;

			bool bOpening
				= mp_Tokens.f_IsText(Token, "(")
				|| mp_Tokens.f_IsText(Token, "[")
				|| mp_Tokens.f_IsText(Token, "{")
				|| (mp_Structure.f_IsAngleBracket(iToken) && mp_Tokens.f_IsText(Token, "<"))
			;
			if (bOpening)
				++nDepth;
		}
	}

	// Finds the binary operators that bind loosest at the range's own bracket level. Those
	// are the outermost places the range can be broken, so they are split first and the
	// scopes inside them only if a resulting line is still too long.
	void CFormattingAnalyzer::fp_FindLooseOperators(umint _iFirst, umint _iLast, TCVector<umint> &o_Operators) const
	{
		struct CPrecedence
		{
			ch8 const *m_pText;
			umint m_Level;
		};

		// Assignment keeps its right hand side, and a comma is a separator a group owns.
		constexpr CPrecedence c_Operators[] =
			{
				{"*", 5}
				, {"/", 5}
				, {"%", 5}
				, {"+", 6}
				, {"-", 6}
				, {"<<", 7}
				, {">>", 7}
				, {"<=>", 8}
				, {"<", 9}
				, {">", 9}
				, {"<=", 9}
				, {">=", 9}
				, {"==", 10}
				, {"!=", 10}
				, {"&", 11}
				, {"^", 12}
				, {"|", 13}
				, {"&&", 14}
				, {"||", 15}
				, {"?", 16}
				, {":", 16}
			}
		;

		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto nLevel = mp_TokenDepth[_iFirst];

		// Assignment is not a split point, and what stands to its left is a declarator, not
		// an expression: '&' and '*' spell a reference or a pointer there, never an operator.
		for (umint iToken = _iLast; iToken > _iFirst; --iToken)
		{
			if (mp_TokenDepth[iToken] == nLevel && fp_IsAssignment(iToken))
			{
				_iFirst = iToken;

				break;
			}
		}

		umint nLoosest = 0;
		for (umint iPass = 0; iPass < 2; ++iPass)
		{
			for (umint iToken = _iFirst; iToken <= _iLast; ++iToken)
			{
				auto const &Token = Tokens[iToken];
				if (Token.m_Kind != ECodeTokenKind::mc_Punctuator || mp_TokenDepth[iToken] != nLevel || mp_Structure.f_IsAngleBracket(iToken))
					continue;

				if (fg_IsBlockLiteralCaret(mp_Tokens, mp_Structure, iToken))
					continue;

				// A trailing return type names a type, which no operation stands in: '-> void *'.
				if (mp_Tokens.f_IsText(Token, "->") && fp_IsTrailingReturnArrow(iToken))
					break;

				umint nPrecedence = 0;
				for (auto const &Operator : c_Operators)
				{
					if (mp_Tokens.f_IsText(Token, Operator.m_pText))
						nPrecedence = Operator.m_Level;
				}

				// Without operands on both sides the token is a declarator or a unary form: what
				// stands in front of an infix operator ends an operand, and an operator, an opener
				// or a keyword such as 'return' ends none, so '= &Value' takes an address.
				if (!nPrecedence || iToken == _iFirst || !fp_EndsOperand(fp_PreviousCode(iToken)) || !fp_HasOperand(iToken, false))
					continue;

				// A colon is the conditional's only behind the '?' it answers; any other one
				// ends a label, or introduces a base clause, an initializer list or a range.
				if (mp_Tokens.f_IsText(Token, ":"))
				{
					bool bAnswers = false;
					for (umint iQuestion = _iFirst; iQuestion < iToken && !bAnswers; ++iQuestion)
						bAnswers = mp_TokenDepth[iQuestion] == nLevel && mp_Tokens.f_IsText(Tokens[iQuestion], "?");

					if (!bAnswers)
						continue;
				}

				// Behind 'operator' the token spells the function's name, not an operation.
				auto iName = fp_PreviousCode(iToken);
				if (iName >= 0 && mp_Tokens.f_IsText(Tokens[umint(iName)], "operator"))
					continue;

				// A '*', '&' or '&&' that declares a pointer or reference is not an operation:
				// what stands in front of it is a type, and 'TCFoo<int> *' cannot multiply.
				if (fg_IsDeclaratorToken(mp_Tokens, mp_Structure, iToken))
					continue;

				// A '&' or '&&' is the function's ref-qualifier when nothing that could be an
				// operand follows it: what comes after one is the trailing return type, the
				// body, a pure specifier, a requires clause, or another qualifier. That is never
				// a second ref-qualifier, so a '&' behind it takes an address: 'a && &b == p'.
				if (mp_Tokens.f_IsText(Token, "&") || mp_Tokens.f_IsText(Token, "&&"))
				{
					auto iNext = fp_NextCode(iToken);
					auto const *pNext = iNext >= 0 ? &Tokens[umint(iNext)] : nullptr;
					bool bReference = pNext && (mp_Tokens.f_IsText(*pNext, "&") || mp_Tokens.f_IsText(*pNext, "&&"));
					bool bQualifier
						= pNext
						&& !bReference
						&&
						(
							mp_Tokens.f_IsText(*pNext, "->")
							|| mp_Tokens.f_IsText(*pNext, "{")
							|| mp_Tokens.f_IsText(*pNext, "=")
							|| (mp_Tokens.f_IsText(*pNext, "requires") && !fg_IsInfixOperator(mp_Tokens, mp_Structure, iToken))
							|| fp_IsFunctionQualifier(umint(iNext))
						)
					;
					if (bQualifier)
						continue;
				}

				if (!iPass)
				{
					nLoosest = fg_Max(nLoosest, nPrecedence);

					continue;
				}

				if (nPrecedence == nLoosest)
					o_Operators.f_Insert(iToken);
			}

			if (!nLoosest)
				return;
		}

		// A conditional groups to the right, so only the first '?' and the ':' that answers it
		// are this range's: a conditional in either branch is one operand of the outer one.
		if (nLoosest != 16 || o_Operators.f_IsEmpty())
			return;

		umint nOpen = 0;
		for (umint iOperator = 0; iOperator < o_Operators.f_GetLen(); ++iOperator)
		{
			if (mp_Tokens.f_IsText(Tokens[o_Operators[iOperator]], "?"))
				++nOpen;
			else if (!--nOpen)
			{
				TCVector<umint> Outer{o_Operators[0], o_Operators[iOperator]};
				o_Operators = fg_Move(Outer);

				return;
			}
		}
	}

	// A lambda follows the operator at _iToken when the next thing is a capture list with
	// a parameter list or a body behind it.
	bool CFormattingAnalyzer::fp_IsLambdaIntroducer(umint _iToken) const
	{
		auto iNext = fp_NextCode(_iToken);
		if (iNext < 0 || mp_Tokens.f_IsC(mp_Tokens.f_GetTokens()[umint(iNext)]) || !mp_Tokens.f_IsText(mp_Tokens.f_GetTokens()[umint(iNext)], "["))
			return false;

		auto const &Nodes = mp_Structure.f_GetNodes();
		auto iNode = mp_Structure.f_FindNodeOpeningAt(umint(iNext));
		if (iNode >= Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Group)
			return false;

		auto iAfter = fp_NextCode(Nodes[iNode].m_iLastToken);
		if (iAfter < 0)
			return false;

		// The capture list is followed by the parameter list, by the body, or by the
		// lambda's own template parameter list.
		auto const &After = mp_Tokens.f_GetTokens()[umint(iAfter)];

		return mp_Tokens.f_IsText(After, "(") || mp_Tokens.f_IsText(After, "{") || mp_Tokens.f_IsText(After, "<");
	}

	// A template header is spelled 'template <...>'. The space in front of the list is what
	// tells a template argument list from a comparison, so the header's own list is not a
	// resolved group and its end has to be found by hand.
	umint CFormattingAnalyzer::fp_SkipTemplateHeader(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		if (!mp_Tokens.f_IsText(Tokens[_iToken], "template"))
			return _iToken;

		auto iOpen = fp_NextCode(_iToken);
		if (iOpen < 0 || !mp_Tokens.f_IsText(Tokens[umint(iOpen)], "<"))
			return _iToken;

		umint nDepth = 0;
		for (auto iToken = iOpen; iToken >= 0; iToken = fp_NextCode(umint(iToken)))
		{
			auto const &Token = Tokens[umint(iToken)];
			if (mp_Tokens.f_IsText(Token, "<"))
			{
				++nDepth;

				continue;
			}

			if (!mp_Tokens.f_IsText(Token, ">"))
				continue;

			if (nDepth > 1)
			{
				--nDepth;

				continue;
			}

			auto iNext = fp_NextCode(umint(iToken));

			return iNext < 0 ? _iToken : umint(iNext);
		}

		return _iToken;
	}

	// Steps over the template headers and the requires clauses behind them in front of the head a declaration spells:
	// 'template <typename t_C>' over 'requires cA<t_C>' over 'struct CFoo'.
	umint CFormattingAnalyzer::fp_SkipHeadIntroducers(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iToken = _iToken;
		for (;;)
		{
			auto iNext = fp_SkipTemplateHeader(iToken);
			if (iNext == iToken && mp_Tokens.f_IsText(Tokens[iToken], "requires"))
			{
				auto iLast = fp_FindConstraintLast(iToken, Tokens.f_GetLen() - 1);
				auto iBehind = iLast >= 0 ? fp_NextCode(umint(iLast)) : aint(-1);
				if (iBehind >= 0)
					iNext = umint(iBehind);
			}

			if (iNext == iToken)
				return iToken;

			iToken = iNext;
		}
	}

	// A capture list, or a lambda's own template parameter list, ends one part of an
	// introducer. What follows stands on its own rather than belonging to what came before.
	// An arrow introduces a trailing return type when a parameter list, or a function's
	// qualifiers behind one, stands in front of it; behind a call's arguments it is a
	// member access: 'fg_Get()->f_Call()'.
	bool CFormattingAnalyzer::fp_IsTrailingReturnArrow(umint _iToken) const
	{
		return fg_IsTrailingReturnArrow(mp_Tokens, mp_Structure, _iToken);
	}

	bool CFormattingAnalyzer::fp_ClosesLambdaIntroducer(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		if (mp_Structure.f_IsAngleBracket(_iToken))
			return fp_FollowsScope(_iToken);

		if (!mp_Tokens.f_IsText(Tokens[_iToken], "]"))
			return false;

		// A subscript stands behind what it indexes; a capture list stands on its own, and
		// behind a keyword it is one however that keyword reads: 'return [&] { ... }'.
		if (!fg_IsCaptureList(mp_Tokens, mp_Structure, _iToken))
			return false;

		// A bare name behind the list, such as an attribute macro, stands between it and
		// the parameter list or the body: '[&] mark_no_coroutine_debug () mutable'.
		auto iAfter = fp_NextCode(_iToken);
		if (iAfter >= 0 && Tokens[umint(iAfter)].m_Kind == ECodeTokenKind::mc_Identifier && !fp_IsFunctionQualifier(umint(iAfter)))
			iAfter = fp_NextCode(umint(iAfter));

		// A lambda that takes nothing may leave its parameter list out, and then its
		// qualifiers and its return type stand behind the list: '[pState] mutable -> int'.
		while (iAfter >= 0 && fp_IsFunctionQualifier(umint(iAfter)))
			iAfter = fp_NextCode(umint(iAfter));

		if (iAfter < 0)
			return false;

		auto const &After = Tokens[umint(iAfter)];
		if (mp_Tokens.f_IsText(After, "->"))
			return fp_IsTrailingReturnArrow(umint(iAfter));

		return mp_Tokens.f_IsText(After, "(") || mp_Tokens.f_IsText(After, "{") || mp_Tokens.f_IsText(After, "<");
	}

	// The words that may stand between a parameter list and a trailing return type.
	// A compound assignment gives its left hand side a value the way '=' does.
	bool CFormattingAnalyzer::fp_IsAssignment(umint _iToken) const
	{
		constexpr ch8 const *c_Assignments[] =
			{
				"="
				, "+="
				, "-="
				, "*="
				, "/="
				, "%="
				, "&="
				, "|="
				, "^="
				, "<<="
				, ">>="
			}
		;

		auto const &Token = mp_Tokens.f_GetTokens()[_iToken];
		if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
			return false;

		for (auto pAssignment : c_Assignments)
		{
			if (mp_Tokens.f_IsText(Token, pAssignment))
				return true;
		}

		return false;
	}

	bool CFormattingAnalyzer::fp_IsFunctionQualifier(umint _iToken) const
	{
		constexpr ch8 const *c_Qualifiers[] =
			{
				"const"
				, "volatile"
				, "noexcept"
				, "mutable"
				, "override"
				, "final"
				, "&"
				, "&&"
			}
		;

		auto const &Token = mp_Tokens.f_GetTokens()[_iToken];
		for (auto pQualifier : c_Qualifiers)
		{
			if (mp_Tokens.f_IsText(Token, pQualifier))
				return true;
		}

		return false;
	}

	// A bracket's closing marker is where one scope ends and the next may start on a line
	// of its own. Anything else in front of a scope owns it: a name and its argument list
	// are one call, and the line cannot be broken between them. A template argument list
	// closes with '>' but is part of the name it follows, so it does not end a scope here.
	bool CFormattingAnalyzer::fp_FollowsScope(umint _iToken) const
	{
		auto const &Token = mp_Tokens.f_GetTokens()[_iToken];
		if (mp_Tokens.f_IsText(Token, ")") || mp_Tokens.f_IsText(Token, "]"))
		{
			// The brackets behind 'operator' spell the function's name: 'operator ()', 'operator []'.
			auto iOpen = fp_PreviousCode(_iToken);
			if (iOpen >= 0 && (mp_Tokens.f_IsText(mp_Tokens.f_GetTokens()[umint(iOpen)], "(") || mp_Tokens.f_IsText(mp_Tokens.f_GetTokens()[umint(iOpen)], "[")))
			{
				auto iKeyword = fp_PreviousCode(umint(iOpen));
				if (iKeyword >= 0 && mp_Tokens.f_IsText(mp_Tokens.f_GetTokens()[umint(iKeyword)], "operator"))
					return false;
			}

			return true;
		}

		// A lambda's template parameter list is a scope of its own rather than part of a
		// name, which is what a template argument list behind an identifier is.
		if (!mp_Structure.f_IsAngleBracket(_iToken))
			return false;

		auto const &Nodes = mp_Structure.f_GetNodes();
		auto iNode = mp_Structure.f_FindNodeClosingAt(_iToken);
		auto const *pNode = iNode < Nodes.f_GetLen() ? &Nodes[iNode] : nullptr;
		if (!pNode || pNode->m_Kind != ECodeNodeKind::mc_Group || pNode->m_Bracket != ECodeBracket::mc_Angle)
			return false;

		auto iBefore = fp_PreviousCode(pNode->m_iFirstToken);

		return iBefore >= 0 && mp_Tokens.f_IsText(mp_Tokens.f_GetTokens()[umint(iBefore)], "]");
	}

	// Whether the parenthesis opening at the token holds the operand of a named cast:
	// 'static_cast<CFoo &>(_Value)'.
	bool CFormattingAnalyzer::fp_IsNamedCast(umint _iOpen) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iClose = fp_PreviousCode(_iOpen);
		if (iClose < 0 || !mp_Structure.f_IsAngleBracket(umint(iClose)) || !mp_Tokens.f_IsText(Tokens[umint(iClose)], ">"))
			return false;

		auto const &Nodes = mp_Structure.f_GetNodes();
		auto iNode = mp_Structure.f_FindNodeClosingAt(umint(iClose));
		auto const *pNode = iNode < Nodes.f_GetLen() ? &Nodes[iNode] : nullptr;
		if (pNode && pNode->m_Kind == ECodeNodeKind::mc_Group && pNode->m_Bracket == ECodeBracket::mc_Angle)
		{
			auto iKeyword = fp_PreviousCode(pNode->m_iFirstToken);
			if (iKeyword < 0)
				return false;

			auto const &Keyword = Tokens[umint(iKeyword)];

			return mp_Tokens.f_IsText(Keyword, "static_cast")
				|| mp_Tokens.f_IsText(Keyword, "reinterpret_cast")
				|| mp_Tokens.f_IsText(Keyword, "const_cast")
				|| mp_Tokens.f_IsText(Keyword, "dynamic_cast")
			;
		}

		return false;
	}

	// The first token of the case label whose line the statement starting at the token shares, or -1: 'case 1: return 2;'.
	// Its body stands one level in from the label wherever it is written.
	aint CFormattingAnalyzer::fp_FindSharedCaseLabel(umint _iToken) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();

		// The rules that run ahead of the layout find no decision yet, and the gap keeps what the source has.
		auto iColon = fp_PreviousCode(_iToken);
		auto Gap = _iToken < mp_GapState.f_GetLen() ? EGap(mp_GapState[_iToken]) : EGap::mc_Keep;
		if (iColon < 0 || !mp_Tokens.f_IsText(Tokens[umint(iColon)], ":") || fp_IsBreakGap(_iToken) || (fp_IsFirstOnLine(_iToken) && Gap != EGap::mc_Inline))
			return -1;

		auto const &Nodes = mp_Structure.f_GetNodes();
		auto iLabel = mp_Structure.f_FindStatementEndingAt(umint(iColon));
		if (iLabel >= Nodes.f_GetLen())
			return -1;

		auto const &First = Tokens[Nodes[iLabel].m_iFirstToken];
		if (!mp_Tokens.f_IsText(First, "case") && !mp_Tokens.f_IsText(First, "default"))
			return -1;

		return aint(Nodes[iLabel].m_iFirstToken);
	}

	// Whether a comment line at the given column below a comment trailing a block's opening brace is the block's own
	// line rather than the comment's continuation, which only one aligned with the comment is.
	bool CFormattingAnalyzer::fp_StartsBodyBehind(umint _iComment, umint _nColumns) const
	{
		auto iBrace = fp_PreviousCode(_iComment);
		if (iBrace < 0 || !mp_Tokens.f_IsText(mp_Tokens.f_GetTokens()[umint(iBrace)], "{"))
			return false;

		auto iBlock = mp_Structure.f_FindNodeOpeningAt(umint(iBrace));
		if (iBlock >= mp_Structure.f_GetNodes().f_GetLen() || mp_Structure.f_GetNodes()[iBlock].m_Kind != ECodeNodeKind::mc_Block)
			return false;

		auto iOffset = mp_Tokens.f_GetTokens()[_iComment].m_iOffset;
		auto iStart = mp_Lines.f_GetLineStart(mp_Lines.f_FindLine(iOffset));
		umint nCommentColumns = 0;
		if (!fg_MeasureTextColumns(mp_pSource + iStart, iOffset - iStart, mp_Request.m_Settings.m_nTabWidth, nCommentColumns))
			return false;

		return _nColumns != nCommentColumns;
	}

	// The indentation of the code a comment trails on its line, against which a comment line below it continues it or
	// stands on its own. The code of a case body on its label's line stands one level in from the label.
	umint CFormattingAnalyzer::fp_GetTrailedCodeIndent(umint _iComment) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto nIndent = fp_GetSourceLineIndent(_iComment);
		auto iLineFirst = mp_Tokens.f_FindToken(mp_Lines.f_GetLineStart(mp_Lines.f_FindLine(Tokens[_iComment].m_iOffset)));
		while (iLineFirst < _iComment && !fg_IsCodeToken(Tokens[iLineFirst]))
			++iLineFirst;

		if (fp_StartsSharedCaseLabel(iLineFirst, _iComment))
			return nIndent + mp_Request.m_Settings.m_nTabWidth;

		return nIndent;
	}

	// Whether the line starting at the first token is a case label with code of its body on it in front of the end token.
	bool CFormattingAnalyzer::fp_StartsSharedCaseLabel(umint _iLineFirst, umint _iEnd) const
	{
		for (auto iCode = aint(_iLineFirst); iCode >= 0 && umint(iCode) < _iEnd; iCode = fp_NextCode(umint(iCode)))
		{
			if (fp_FindSharedCaseLabel(umint(iCode)) == aint(_iLineFirst))
				return true;
		}

		return false;
	}

	// The brackets of a structured binding declare its names: 'auto const &[A, B] = f_Pair()'. Only 'auto', its
	// qualifiers and its references can stand in front of them.
	bool CFormattingAnalyzer::fp_IsStructuredBinding(umint _iNode) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto const &Node = mp_Structure.f_GetNodes()[_iNode];
		if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Square || Node.m_bMessageSend)
			return false;

		auto iAfter = fp_NextCode(Node.m_iLastToken);
		auto const *pAfter = iAfter >= 0 ? &Tokens[umint(iAfter)] : nullptr;
		if (!pAfter || (!mp_Tokens.f_IsText(*pAfter, ":") && !mp_Tokens.f_IsText(*pAfter, "=")))
			return false;

		auto fQualifier = [&](aint _iToken)
			{
				for (auto pQualifier : {"&", "&&", "const", "volatile", "static", "thread_local", "constexpr"})
				{
					if (mp_Tokens.f_IsText(Tokens[umint(_iToken)], pQualifier))
						return true;
				}

				return false;
			}
		;

		auto iBefore = fp_PreviousCode(Node.m_iFirstToken);
		while (iBefore >= 0 && fQualifier(iBefore))
			iBefore = fp_PreviousCode(umint(iBefore));

		return iBefore >= 0 && mp_Tokens.f_IsText(Tokens[umint(iBefore)], "auto");
	}

	// A parenthesised type in front of an operand is a cast: nothing separates the closing
	// parenthesis from what follows, while a call or a clause has a name or a keyword in
	// front of the opening one.
	bool CFormattingAnalyzer::fp_IsCastGroup(umint _iNode) const
	{
		auto const &Nodes = mp_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (Node.m_Bracket != ECodeBracket::mc_Paren)
			return false;

		// A type holds no lambda, so a parenthesis around one is a value: '([] { return 1; })()'. It can define a class:
		// '(struct { int a; } *)0'.
		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Group && Nodes[iChild].m_Bracket == ECodeBracket::mc_Square && fp_ClosesLambdaIntroducer(Nodes[iChild].m_iLastToken))
				return false;
		}

		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto iBefore = fp_PreviousCode(Node.m_iFirstToken);
		if (iBefore >= 0)
		{
			auto const &Before = Tokens[umint(iBefore)];

			// An argument list closes a name and a template parameter list closes a lambda's
			// introducer. What follows either is a call, never a cast.
			if (Before.m_Kind == ECodeTokenKind::mc_Identifier || mp_Tokens.f_IsText(Before, ")") || mp_Tokens.f_IsText(Before, "]") || mp_Structure.f_IsAngleBracket(umint(iBefore)))
				return false;
		}

		auto iAfter = fp_NextCode(Node.m_iLastToken);
		if (iAfter < 0)
			return false;

		auto const &After = Tokens[umint(iAfter)];

		return After.m_Kind == ECodeTokenKind::mc_Identifier
			|| After.m_Kind == ECodeTokenKind::mc_Number
			|| After.m_Kind == ECodeTokenKind::mc_StringLiteral
			|| After.m_Kind == ECodeTokenKind::mc_CharLiteral
			|| mp_Tokens.f_IsText(After, "(")
			|| fg_IsBlockLiteralCaret(mp_Tokens, mp_Structure, umint(iAfter))
		;
	}

	// A call or a subscript on what a call or a subscript yields is a postfix operation
	// like a member access, and binds the same way: 'f_CallActor(&C::f_Fn)(_Params)[0]'.
	// A lambda's parameter list stands behind a closing marker too and is part of its
	// introducer instead, a cast's operand is what the cast converts, and an empty list
	// holds nothing to give a line to: '(*pFunctor)()'.
	bool CFormattingAnalyzer::fp_IsYieldedScope(umint _iOpen) const
	{
		auto const &Tokens = mp_Tokens.f_GetTokens();
		auto const &Open = Tokens[_iOpen];
		if (!mp_Tokens.f_IsText(Open, "(") && !mp_Tokens.f_IsText(Open, "["))
			return false;

		auto iBefore = fp_PreviousCode(_iOpen);
		auto const *pBefore = iBefore >= 0 ? &Tokens[umint(iBefore)] : nullptr;
		if (!pBefore || (!mp_Tokens.f_IsText(*pBefore, ")") && !mp_Tokens.f_IsText(*pBefore, "]")))
			return false;

		auto iInner = fp_NextCode(_iOpen);
		auto const *pInner = iInner >= 0 ? &Tokens[umint(iInner)] : nullptr;
		if (!pInner || mp_Tokens.f_IsText(*pInner, ")") || mp_Tokens.f_IsText(*pInner, "]"))
			return false;

		if (mp_Tokens.f_IsText(*pBefore, "]") && fg_IsCaptureList(mp_Tokens, mp_Structure, umint(iBefore)))
			return false;

		auto const &Nodes = mp_Structure.f_GetNodes();
		auto iGroup = mp_Structure.f_FindNodeClosingAt(umint(iBefore));

		return iGroup >= Nodes.f_GetLen() || Nodes[iGroup].m_Kind != ECodeNodeKind::mc_Group || !fp_IsCastGroup(iGroup);
	}
}
