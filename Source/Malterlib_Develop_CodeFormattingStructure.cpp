// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingStructurePrivate.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

	bool fg_IsAsmQualifier(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		for (auto pQualifier : {"volatile", "__volatile__", "__volatile", "inline", "__inline__", "goto"})
		{
			if (_Tokens.f_IsText(_Token, pQualifier))
				return true;
		}

		return false;
	}

	// Whether the parenthesis opening at the token holds an asm statement's operands: 'asm volatile ('.
	bool fg_OpensAsmOperands(CCodeTokenStream const &_Tokens, umint _iOpen)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!_Tokens.f_IsText(Tokens[_iOpen], "("))
			return false;

		auto iBefore = fg_PreviousCode(_Tokens, _iOpen);
		while (iBefore >= 0 && fg_IsAsmQualifier(_Tokens, Tokens[umint(iBefore)]))
			iBefore = fg_PreviousCode(_Tokens, umint(iBefore));

		if (iBefore < 0)
			return false;

		auto const &Keyword = Tokens[umint(iBefore)];

		return _Tokens.f_IsText(Keyword, "asm") || _Tokens.f_IsText(Keyword, "__asm__") || _Tokens.f_IsText(Keyword, "__asm");
	}

	ECodeBracket fg_GetOpeningBracket(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Punctuator)
			return ECodeBracket::mc_None;

		if (_Tokens.f_IsText(_Token, "("))
			return ECodeBracket::mc_Paren;
		if (_Tokens.f_IsText(_Token, "["))
			return ECodeBracket::mc_Square;
		if (_Tokens.f_IsText(_Token, "{"))
			return ECodeBracket::mc_Brace;

		return ECodeBracket::mc_None;
	}

	CStr fg_GetClosingText(ECodeBracket _Bracket)
	{
		switch (_Bracket)
		{
		case ECodeBracket::mc_Paren: return ")";
		case ECodeBracket::mc_Square: return "]";
		case ECodeBracket::mc_Brace: return "}";
		case ECodeBracket::mc_Angle: return ">";
		default: return {};
		}
	}

	bool fg_IsClosingBracket(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Punctuator)
			return false;

		return _Tokens.f_IsText(_Token, ")") || _Tokens.f_IsText(_Token, "]") || _Tokens.f_IsText(_Token, "}");
	}
}

namespace NMib::NDevelop::NPrivate
{
	bool fg_IsSignificant(ECodeTokenKind _Kind)
	{
		switch (_Kind)
		{
		case ECodeTokenKind::mc_ByteOrderMark:
		case ECodeTokenKind::mc_Whitespace:
		case ECodeTokenKind::mc_Newline:
		case ECodeTokenKind::mc_LineSplice:
		case ECodeTokenKind::mc_LineComment:
		case ECodeTokenKind::mc_BlockComment:
		case ECodeTokenKind::mc_Preprocessor:
			return false;
		default: return true;
		}
	}

	// Whether the token separates an asm statement's operand sections: ':', or '::' where a section is empty.
	bool fg_IsAsmSectionSeparator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Token = _Tokens.f_GetTokens()[_iToken];
		if (!_Tokens.f_IsText(Token, ":") && !_Tokens.f_IsText(Token, "::"))
			return false;

		auto iGroup = _Structure.f_FindEnclosingNode(_iToken);
		auto const &Nodes = _Structure.f_GetNodes();
		if (iGroup >= Nodes.f_GetLen() || Nodes[iGroup].m_Kind != ECodeNodeKind::mc_Group)
			return false;

		return fg_OpensAsmOperands(_Tokens, Nodes[iGroup].m_iFirstToken);
	}

	enum EGapFlag : uint8
	{
		EGapFlag_None             = 0
		, EGapFlag_Comment        = 1
		, EGapFlag_Directive      = 2
		, EGapFlag_MultiLineToken = 4
		, EGapFlag_LineSplice     = 8
	};
}

namespace NMib::NDevelop
{
	using namespace NPrivate;

	// Whether the token is the first behind an '#else' or '#elif', where a conditional's other branch starts.
	bool CCodeStructure::fp_StartsAlternativeBranch(umint _iToken) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		for (auto iRaw = aint(_iToken) - 1; iRaw >= 0; --iRaw)
		{
			auto const &Raw = Tokens[umint(iRaw)];
			if (Raw.m_Kind == ECodeTokenKind::mc_Whitespace || Raw.m_Kind == ECodeTokenKind::mc_Newline)
				continue;

			if (Raw.m_Kind != ECodeTokenKind::mc_Preprocessor)
				return false;

			auto pText = mp_pTokens->f_GetTextPointer(Raw);

			auto fIsLead = [&](umint _iChar)
				{
					auto Char = pText[_iChar];

					return Char == '#' || Char == ' ' || Char == '\t';
				}
			;

			umint iWord = 0;
			while (iWord < Raw.m_nLength && fIsLead(iWord))
				++iWord;

			if (Raw.m_nLength - iWord < 4)
				return false;

			return NMemory::fg_MemCmp((uint8 const *)(pText + iWord), (uint8 const *)"else", 4) == 0 || NMemory::fg_MemCmp((uint8 const *)(pText + iWord), (uint8 const *)"elif", 4) == 0;
		}

		return false;
	}

	bool CCodeNode::f_IsJoinable() const
	{
		return !m_bHasComment && !m_bHasDirective && !m_bHasBlock && !m_bHasMultiLineToken && !m_bHasMultiLineBrace && m_Kind != ECodeNodeKind::mc_Unsupported;
	}

	CCodeStructure::CCodeStructure(CCodeTokenStream &_Tokens, umint _nTabWidth)
		: mp_pTokens(&_Tokens)
		, mp_nTabWidth(fg_Max(_nTabWidth, umint(1)))
	{
		fp_CollectSignificant();
		if (fp_SplitSharedAngleClosers())
			fp_CollectSignificant();

		auto const &Tokens = _Tokens.f_GetTokens();
		mp_AngleBracket.f_SetLen(Tokens.f_GetLen());
		for (auto &Value : mp_AngleBracket)
			Value = 0;

		auto iRoot = fp_AddNode(ECodeNodeKind::mc_File, 0);
		if (mp_Significant.f_IsEmpty())
		{
			mp_Nodes[iRoot].m_Kind = ECodeNodeKind::mc_File;
			fp_BuildIndex();

			return;
		}

		mp_Nodes[iRoot].m_iFirstToken = mp_Significant.f_GetFirst();

		// The builder recurses once per bracket level, so a file nested deeper than any compiler accepts by default is
		// left unbuilt rather than allowed to exhaust the stack.
		umint nDepth = 0;
		for (auto iToken : mp_Significant)
		{
			auto const &Token = Tokens[iToken];
			if (fg_GetOpeningBracket(*mp_pTokens, Token) != ECodeBracket::mc_None && Token.m_Kind == ECodeTokenKind::mc_Punctuator)
			{
				if (++nDepth > mc_MaxBracketDepth)
				{
					mp_bComplete = false;
					mp_bTooDeep = true;
					mp_iIncompleteOffset = Token.m_iOffset;
					mp_Nodes[iRoot].m_iLastToken = mp_Significant.f_GetLast();
					fp_BuildIndex();

					return;
				}
			}
			else if (fg_IsClosingBracket(*mp_pTokens, Token) && nDepth)
				--nDepth;
		}

		auto iNext = fp_BuildBlock(iRoot, 0, false);

		// Template argument lists the scan above does not count can still nest past the limit, which leaves the build half
		// done.
		if (mp_bTooDeep)
		{
			mp_Nodes.f_SetLen(iRoot + 1);
			mp_Nodes[iRoot].m_Children.f_Clear();
			mp_Nodes[iRoot].m_iLastToken = mp_Significant.f_GetLast();
			fp_BuildIndex();

			return;
		}

		mp_Nodes[iRoot].m_iLastToken = mp_Significant[fg_Min(iNext, mp_Significant.f_GetLen() - 1)];

		fp_BuildIndex();
	}

	// Every predicate asks which node a marker belongs to, so the answer is written down
	// once per token. A node is added before its children, so the last node written over a
	// token is the innermost one around it.
	void CCodeStructure::fp_BuildIndex()
	{
		auto nTokens = mp_pTokens->f_GetTokens().f_GetLen();
		auto nNodes = mp_Nodes.f_GetLen();

		mp_OpeningAt.f_SetLen(nTokens);
		mp_ClosingAt.f_SetLen(nTokens);
		mp_Enclosing.f_SetLen(nTokens);
		mp_StatementEndingAt.f_SetLen(nTokens);
		for (umint i = 0; i < nTokens; ++i)
			mp_OpeningAt[i] = mp_ClosingAt[i] = mp_Enclosing[i] = mp_StatementEndingAt[i] = nNodes;

		for (umint iNode = 0; iNode < nNodes; ++iNode)
		{
			auto const &Node = mp_Nodes[iNode];
			if (Node.m_iLastToken >= nTokens || Node.m_iFirstToken > Node.m_iLastToken)
				continue;

			if (Node.m_Kind == ECodeNodeKind::mc_Group || Node.m_Kind == ECodeNodeKind::mc_Block)
			{
				mp_OpeningAt[Node.m_iFirstToken] = iNode;
				mp_ClosingAt[Node.m_iLastToken] = iNode;
			}
			else if (Node.m_Kind == ECodeNodeKind::mc_Statement)
				mp_StatementEndingAt[Node.m_iLastToken] = iNode;

			for (auto i = Node.m_iFirstToken + 1; i < Node.m_iLastToken; ++i)
				mp_Enclosing[i] = iNode;
		}
	}

	umint CCodeStructure::f_FindNodeOpeningAt(umint _iToken) const
	{
		return _iToken < mp_OpeningAt.f_GetLen() ? mp_OpeningAt[_iToken] : mp_Nodes.f_GetLen();
	}

	umint CCodeStructure::f_FindNodeClosingAt(umint _iToken) const
	{
		return _iToken < mp_ClosingAt.f_GetLen() ? mp_ClosingAt[_iToken] : mp_Nodes.f_GetLen();
	}

	umint CCodeStructure::f_FindEnclosingNode(umint _iToken) const
	{
		return _iToken < mp_Enclosing.f_GetLen() ? mp_Enclosing[_iToken] : mp_Nodes.f_GetLen();
	}

	umint CCodeStructure::f_FindStatementEndingAt(umint _iToken) const
	{
		return _iToken < mp_StatementEndingAt.f_GetLen() ? mp_StatementEndingAt[_iToken] : mp_Nodes.f_GetLen();
	}

	void CCodeStructure::fp_CollectSignificant()
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		mp_Significant.f_Clear();
		mp_NoAngleCloser.f_Clear();
		mp_GapFlags.f_Clear();

		uint8 Flags = EGapFlag_None;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
			{
				if (Token.m_Kind == ECodeTokenKind::mc_LineComment || Token.m_Kind == ECodeTokenKind::mc_BlockComment)
					Flags |= EGapFlag_Comment;
				else if (Token.m_Kind == ECodeTokenKind::mc_Preprocessor)
					Flags |= EGapFlag_Directive;
				else if (Token.m_Kind == ECodeTokenKind::mc_LineSplice)
					Flags |= EGapFlag_LineSplice;

				continue;
			}

			if (Token.m_bMultiLine)
				Flags |= EGapFlag_MultiLineToken;

			mp_Significant.f_Insert(i);
			mp_GapFlags.f_Insert(Flags);
			Flags = EGapFlag_None;
		}
	}

	// C++ reads a '>>' that ends a template argument list as two '>' tokens. Spelling it that
	// way gives each list a closing marker of its own, so the layout can put the two markers
	// on separate lines like any other pair of nested scopes. Returns true when a token was split.
	// Splits each '>>' that closes two template argument lists into two '>', as the structure does before it is built, and
	// leaves every other '>>' a shift.
	void CCodeStructure::fs_SplitSharedAngleClosers(CCodeTokenStream &_Tokens)
	{
		CCodeStructure Structure;
		Structure.mp_pTokens = &_Tokens;
		Structure.fp_CollectSignificant();
		Structure.fp_SplitSharedAngleClosers();
	}

	bool CCodeStructure::fp_SplitSharedAngleClosers()
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();

		struct CMatch
		{
			umint m_iOpen = 0;
			umint m_iClose = 0;
		};

		TCVector<CMatch> Matches;
		TCVector<uint8> Matched;
		Matched.f_SetLen(mp_Significant.f_GetLen());
		for (auto &bMatched : Matched)
			bMatched = 0;

		for (umint i = 0; i < mp_Significant.f_GetLen(); ++i)
		{
			if (auto iClose = fp_MatchAngleGroup(i))
			{
				Matches.f_Insert(CMatch{i, iClose});
				Matched[i] = 1;
			}
		}

		// The '<' no match opened, in order, where a nested match may find a list all the same.
		TCVector<umint> Unmatched;
		for (umint i = 0; i < mp_Significant.f_GetLen(); ++i)
		{
			if (!Matched[i] && mp_pTokens->f_IsText(Tokens[mp_Significant[i]], "<"))
				Unmatched.f_Insert(i);
		}

		// A list that its own spacing leaves unmatched, 'tuple<weak_ptr  <char>> wp', closes on a '>>' all the same where a
		// matched list holds it, as the structure builds it nested. The matches come in order of their openers, so those
		// still open around one are a stack, the outermost at its bottom.
		TCVector<umint> Splits;
		TCVector<CMatch> Open;
		for (auto const &Match : Matches)
		{
			while (!Open.f_IsEmpty() && Open.f_GetLast().m_iClose < Match.m_iOpen)
				Open.f_Remove(Open.f_GetLen() - 1);

			umint iScan = Match.m_iOpen;
			for (auto const &Outer : Open)
			{
				if (Outer.m_iClose > Match.m_iClose)
				{
					iScan = Outer.m_iOpen;

					break;
				}
			}

			Open.f_Insert(Match);

			auto iToken = mp_Significant[Match.m_iClose];
			auto const &Close = Tokens[iToken];
			if (!mp_pTokens->f_IsText(Close, ">>"))
				continue;

			Splits.f_Insert(iToken);

			umint iLow = 0;
			umint iHigh = Unmatched.f_GetLen();
			while (iLow < iHigh)
			{
				auto iMiddle = iLow + (iHigh - iLow) / 2;
				if (Unmatched[iMiddle] <= iScan)
					iLow = iMiddle + 1;
				else
					iHigh = iMiddle;
			}

			for (auto iUnmatched = iLow; iUnmatched < Unmatched.f_GetLen() && Unmatched[iUnmatched] < Match.m_iClose; ++iUnmatched)
			{
				if (fp_MatchAngleGroup(Unmatched[iUnmatched], true) == Match.m_iClose)
					Splits.f_Insert(iToken);
			}
		}

		if (Splits.f_IsEmpty())
			return false;

		Splits.f_Sort();

		// A '>>' that one '<' matches and an operand follows may as well be a shift, 'a<b>>c' or 'a<b>>-c', which split in two
		// could be broken across lines; it stays one token. Where two '<' match it, or nothing an operand starts with follows
		// it, it closes two lists: 'A<B<C>> Value'. A comparison chained into a shift, 'a<b<c>>d', reads as the lists by
		// decision: only the declarations tell the two apart, and reading the context instead misreads real template lists.
		// So does a shift by a parenthesis or a qualified name, 'a<b>>(c)', 'a<b>>::c', the calls and the scopes of lists far
		// more common: 'f<T<int>>()', 'A<B<C>>::value'.
		// Lists spaced inside their markers that close on one '>>', 'T < U < int >> >', match only once a pass has tightened
		// them, and the file is reported as not converging: such spacing is out of scope.
		TCVector<umint> Distinct;
		for (umint i = 0; i < Splits.f_GetLen(); ++i)
		{
			auto iSplit = Splits[i];
			if (!Distinct.f_IsEmpty() && Distinct.f_GetLast() == iSplit)
				continue;

			bool bTwice = i + 1 < Splits.f_GetLen() && Splits[i + 1] == iSplit;
			bool bOperandFollows = false;
			for (auto iNext = iSplit + 1; iNext < Tokens.f_GetLen(); ++iNext)
			{
				auto const &Next = Tokens[iNext];
				auto Kind = Next.m_Kind;
				bool bTrivia
					= Kind == ECodeTokenKind::mc_Whitespace
					|| Kind == ECodeTokenKind::mc_Newline
					|| Kind == ECodeTokenKind::mc_LineSplice
					|| Kind == ECodeTokenKind::mc_LineComment
					|| Kind == ECodeTokenKind::mc_BlockComment
				;
				if (bTrivia)
					continue;

				bOperandFollows
					= Next.m_Kind == ECodeTokenKind::mc_Identifier
					|| Next.m_Kind == ECodeTokenKind::mc_Number
					|| Next.m_Kind == ECodeTokenKind::mc_StringLiteral
					|| Next.m_Kind == ECodeTokenKind::mc_CharLiteral
					|| mp_pTokens->f_IsText(Next, "[")
					|| mp_pTokens->f_IsText(Next, "+")
					|| mp_pTokens->f_IsText(Next, "-")
					|| mp_pTokens->f_IsText(Next, "!")
					|| mp_pTokens->f_IsText(Next, "~")
					|| mp_pTokens->f_IsText(Next, "*")
					|| mp_pTokens->f_IsText(Next, "&")
					|| mp_pTokens->f_IsText(Next, "++")
					|| mp_pTokens->f_IsText(Next, "--")
					|| mp_pTokens->f_GetText(Next).f_StartsWith("@") // An Objective-C literal: '@(c)', '@[a]', '@"s"'.
				;

				break;
			}

			if (bTwice || !bOperandFollows)
				Distinct.f_Insert(iSplit);
		}

		if (Distinct.f_IsEmpty())
			return false;

		mp_pTokens->f_SplitTokens(Distinct, 1);

		return true;
	}

	TCVector<CCodeNode> const &CCodeStructure::f_GetNodes() const
	{
		return mp_Nodes;
	}

	CCodeNode const &CCodeStructure::f_GetRoot() const
	{
		return mp_Nodes[0];
	}

	bool CCodeStructure::f_IsComplete() const
	{
		return mp_bComplete;
	}

	umint CCodeStructure::f_GetIncompleteOffset() const
	{
		return mp_iIncompleteOffset;
	}

	bool CCodeStructure::f_IsTooDeep() const
	{
		return mp_bTooDeep;
	}

	bool CCodeStructure::f_IsAngleBracket(umint _iToken) const
	{
		return _iToken < mp_AngleBracket.f_GetLen() && mp_AngleBracket[_iToken];
	}

	// Records what the trivia before a token, and the token itself, mean for joining.
	void CCodeStructure::fp_Note(umint _iNode, umint _iSignificant)
	{
		auto Flags = mp_GapFlags[_iSignificant];
		auto &Node = mp_Nodes[_iNode];

		Node.m_bHasComment |= (Flags & EGapFlag_Comment) != 0;
		Node.m_bHasDirective |= (Flags & EGapFlag_Directive) != 0;
		Node.m_bHasMultiLineToken |= (Flags & EGapFlag_MultiLineToken) != 0;

		// A line splice inside a braced list joins two of its lines into one the layout cannot write, so the construct keeps
		// its lines. A statement's own line behind a splice keeps its columns.
		if ((Flags & EGapFlag_LineSplice) && Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Brace)
			Node.m_Kind = ECodeNodeKind::mc_Unsupported;

		// So does a statement a splice joins to the line of the one in front of it: 'a = 1; \' over 'b = 2;'.
		bool bStatementStart = Node.m_Kind == ECodeNodeKind::mc_Statement && Node.m_iFirstToken == mp_Significant[_iSignificant];
		if ((Flags & EGapFlag_LineSplice) && bStatementStart)
			Node.m_Kind = ECodeNodeKind::mc_Unsupported;
	}

	umint CCodeStructure::fp_AddNode(ECodeNodeKind _Kind, umint _iParent)
	{
		auto iNode = mp_Nodes.f_GetLen();
		auto &Node = mp_Nodes.f_Insert();
		Node.m_Kind = _Kind;
		Node.m_iParent = _iParent;
		if (iNode)
			mp_Nodes[_iParent].m_Children.f_Insert(iNode);

		return iNode;
	}

	// Propagates the properties that forbid joining from a finished child to its parent.
	void CCodeStructure::fp_Finish(umint _iNode, umint _iLastToken)
	{
		auto &Node = mp_Nodes[_iNode];
		Node.m_iLastToken = _iLastToken;
		if (!_iNode)
			return;

		// A braced initializer, and a DSL's array, is written one element per line on purpose,
		// so a construct around one keeps its lines instead of collapsing the list into an
		// expression. A
		// compound requirement opens its statement, which no initializer does but a file of a list's elements, and holds
		// an expression rather than data.
		auto const *pParent = Node.m_iParent < mp_Nodes.f_GetLen() ? &mp_Nodes[Node.m_iParent] : nullptr;
		bool bRequirement
			= pParent
			&& pParent->m_Kind == ECodeNodeKind::mc_Statement
			&& pParent->m_iFirstToken == Node.m_iFirstToken
			&& !pParent->m_bFileElements
			&& !fp_StartsAlternativeBranch(Node.m_iFirstToken)
		;

		// A DSL's array is data like an initializer: '"Names"_o= _o["--a", "--b"]'.
		bool bDSLArray = false;
		if (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Square)
		{
			auto iMarker = fg_PreviousCode(*mp_pTokens, Node.m_iFirstToken);
			bDSLArray = iMarker >= 0 && fg_IsDSLMarker(*mp_pTokens, mp_pTokens->f_GetTokens()[umint(iMarker)]);
		}

		// Only a list opened on purpose keeps its lines: one whose first element starts the line
		// behind the opening marker. An element on the marker's line spells a list written on
		// one line, whatever broke it.
		if (Node.m_Kind == ECodeNodeKind::mc_Group && ((Node.m_Bracket == ECodeBracket::mc_Brace && !bRequirement) || bDSLArray))
		{
			mp_Nodes[_iNode].m_bList = true;

			auto pSource = mp_pTokens->f_GetSource().f_GetStr();
			auto const &Tokens = mp_pTokens->f_GetTokens();
			bool bOpened = false;
			for (auto i = Node.m_iFirstToken + 1; i <= _iLastToken && i < Tokens.f_GetLen(); ++i)
			{
				auto Kind = Tokens[i].m_Kind;
				if (Kind == ECodeTokenKind::mc_Newline)
				{
					bOpened = true;

					break;
				}

				if (Kind != ECodeTokenKind::mc_Whitespace)
					break;
			}

			mp_Nodes[_iNode].m_bWrittenOpen = bOpened;

			for (auto i = Node.m_iFirstToken; bOpened && i <= _iLastToken && i < Tokens.f_GetLen() && !Node.m_bHasMultiLineBrace; ++i)
			{
				auto const &Token = Tokens[i];
				for (umint iByte = Token.m_iOffset; iByte < Token.f_GetEnd(); ++iByte)
				{
					auto Char = pSource[iByte];
					mp_Nodes[_iNode].m_bHasMultiLineBrace |= Char == '\n' || Char == '\r';
				}
			}
		}

		auto &Parent = mp_Nodes[Node.m_iParent];
		Parent.m_bHasComment |= Node.m_bHasComment;
		Parent.m_bHasDirective |= Node.m_bHasDirective;
		Parent.m_bHasMultiLineToken |= Node.m_bHasMultiLineToken;
		Parent.m_bHasMultiLineBrace |= mp_Nodes[_iNode].m_bHasMultiLineBrace;
		Parent.m_bHasBlock |= Node.m_bHasBlock || Node.m_Kind == ECodeNodeKind::mc_Block;

		// An unclassified construct makes the expression around it unclassified too, but a
		// block and the file are only containers: their other statements stay layoutable.
		bool bContainer = Parent.m_Kind == ECodeNodeKind::mc_Block || Parent.m_Kind == ECodeNodeKind::mc_File;
		if (Node.m_Kind == ECodeNodeKind::mc_Unsupported && !bContainer)
			Parent.m_Kind = ECodeNodeKind::mc_Unsupported;
	}

	umint CCodeStructure::fp_BuildBlock(umint _iNode, umint _iToken, bool _bBraced)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto i = _iToken;
		while (i < mp_Significant.f_GetLen())
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (_bBraced && mp_pTokens->f_IsText(Token, "}"))
				return i;

			if (!_bBraced && fg_IsClosingBracket(*mp_pTokens, Token))
			{
				// A closer with no opener means the file's brackets do not balance.
				if (mp_bComplete)
					mp_iIncompleteOffset = Token.m_iOffset;

				mp_bComplete = false;
				mp_Nodes[_iNode].m_Kind = ECodeNodeKind::mc_Unsupported;

				return mp_Significant.f_GetLen();
			}

			auto iNext = fp_BuildStatement(_iNode, i);
			if (iNext == i)
				return mp_Significant.f_GetLen();

			i = iNext;
		}

		return i;
	}

	// A '<' opens a template argument list only when a matching '>' exists before the
	// enclosing construct ends, and it is written tight against the name before it or that
	// name is one the naming lists as a type's or a function's. Malterlib spells comparisons
	// with spaces, so the tight spelling is a reliable discriminator where the name is not.
	umint CCodeStructure::fp_MatchAngleGroup(umint _iToken, bool _bNested) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		if (!_iToken)
			return 0;

		auto const &Open = Tokens[mp_Significant[_iToken]];
		if (!mp_pTokens->f_IsText(Open, "<"))
			return 0;

		// C has no templates, so its '<' compares, but where Objective-C qualifies a type by protocols or generics, which hold
		// names only and leave nothing a comparison could go on with behind the '>': 'id<P> value', '(NSArray<id> *)'. A name
		// behind it goes on with a chained comparison, 'if (z < a > y)', unless the '<' is tight against the type or the type
		// starts the statement: 'id < A > obj;'.
		if (mp_pTokens->f_IsC(Open))
		{
			auto const &Type = Tokens[mp_Significant[_iToken - 1]];
			if (Type.m_Kind != ECodeTokenKind::mc_Identifier)
				return 0;

			auto const *pBeforeType = _iToken >= 2 ? &Tokens[mp_Significant[_iToken - 2]] : nullptr;
			bool bDeclares
				= Type.f_GetEnd() == Open.m_iOffset
				|| !pBeforeType
				|| mp_pTokens->f_IsText(*pBeforeType, ";")
				|| mp_pTokens->f_IsText(*pBeforeType, "{")
				|| mp_pTokens->f_IsText(*pBeforeType, "}")
			;

			umint nDepth = 0;
			for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
			{
				auto const &Token = Tokens[mp_Significant[i]];
				if (mp_pTokens->f_IsText(Token, "<"))
					++nDepth;
				else if (mp_pTokens->f_IsText(Token, ">"))
				{
					if (--nDepth)
						continue;

					if (i + 1 >= mp_Significant.f_GetLen())
						return i;

					auto const &Behind = Tokens[mp_Significant[i + 1]];
					constexpr ch8 const *c_pEnds[] = {"*", ")", "{", ";", ",", ">"};
					bool bEnds = bDeclares && Behind.m_Kind == ECodeTokenKind::mc_Identifier;
					for (auto pEnd : c_pEnds)
						bEnds = bEnds || mp_pTokens->f_IsText(Behind, pEnd);

					// An '@interface''s protocols end its line, and a method can follow: '- (void)f;'.
					auto pSource = mp_pTokens->f_GetSource().f_GetStr();
					for (auto iGap = Token.f_GetEnd(); !bEnds && iGap < Behind.m_iOffset; ++iGap)
						bEnds = pSource[iGap] == '\n';

					return bEnds ? i : 0;
				}
				else if (Token.m_Kind != ECodeTokenKind::mc_Identifier && !mp_pTokens->f_IsText(Token, ",") && !mp_pTokens->f_IsText(Token, "*"))
					return 0;
			}

			return 0;
		}

		// A template argument list follows a name. A lambda writes its own template
		// parameter list behind the capture list, which is the other thing a '<' can follow.
		// The call operator's name ends in its own parentheses, and nothing but a template
		// argument list can follow that name: 'fCheck.template operator ()<void>("Void")'.
		auto const &Previous = Tokens[mp_Significant[_iToken - 1]];
		bool bCallOperator
			= _iToken >= 3
			&& mp_pTokens->f_IsText(Previous, ")")
			&& mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken - 2]], "(")
			&& mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken - 3]], "operator")
		;

		// So can only a template argument list follow another operator's name: 'operator = <EJsonType>(EJsonType &&)'.
		bool bOperatorName = _iToken >= 2 && Previous.m_Kind == ECodeTokenKind::mc_Punctuator && mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken - 2]], "operator");
		bCallOperator |= bOperatorName;
		if (Previous.m_Kind != ECodeTokenKind::mc_Identifier && !mp_pTokens->f_IsText(Previous, "]") && !bCallOperator)
			return 0;

		// A template header's list can only be one, however it is spaced, and so can the
		// one behind the call operator's name, and the one behind a type or a function,
		// which is no operand of a comparison: 'TCVector< CStr >' is a list, and so is the
		// 'TCVector < CStr >' an earlier pass made of it.
		// So is one whose first argument starts with 'typename' or 'class', which no operand of a comparison can, a lambda's
		// '[&] <class T>' as well, and one behind a
		// name only a type can stand at, however qualified: the one a class key declares, 'struct N::TCFoo< int, 0>', and
		// a type 'typedef', 'typename' or an alias declaration's '=' starts with: 'typedef N::CFoo <CDelegate> CBar;'.
		auto fTypeOnly = [&]() -> bool
			{
				if (Previous.m_Kind != ECodeTokenKind::mc_Identifier)
					return false;

				auto fText = [&](aint _i, ch8 const *_pText)
					{
						return _i >= 0 && mp_pTokens->f_IsText(Tokens[mp_Significant[umint(_i)]], _pText);
					}
				;

				auto fKey = [&](aint _i)
					{
						return fText(_i, "struct") || fText(_i, "class") || fText(_i, "union") || fText(_i, "typedef") || fText(_i, "typename");
					}
				;

				// A keyword is lexed as a name, and a leading '::' follows one: 'typedef ::N::CFoo'.
				auto iKey = aint(_iToken) - 2;
				while (iKey >= 0 && !fKey(iKey))
				{
					auto const &Token = Tokens[mp_Significant[umint(iKey)]];
					bool bQualifier = fText(iKey, "::") || fText(iKey, "template") || fText(iKey, "const") || fText(iKey, "volatile");
					bool bScope = Token.m_Kind == ECodeTokenKind::mc_Identifier && fText(iKey + 1, "::");
					if (!bQualifier && !bScope)
						break;

					--iKey;
				}

				if (fText(iKey, "=") && iKey >= 2 && fText(iKey - 2, "using"))
					return true;

				return fKey(iKey);
			}
		;

		bool bHeader
			= mp_pTokens->f_IsText(Previous, "template")
			|| bCallOperator
			|| mp_pTokens->f_HasRole(Previous, ECodeNameRole::mc_Type)
			|| mp_pTokens->f_HasRole(Previous, ECodeNameRole::mc_Function)
			|| (_iToken + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken + 1]], "typename"))
			|| (_iToken + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken + 1]], "class"))
			|| fTypeOnly()
		;

		// Malterlib writes a comparison with spaces and a template argument list tight
		// against its name, so a gap that is neither empty nor a line break is a comparison.
		auto fIsTightOrBroken = [&](umint _iEnd, umint _iStart)
			{
				if (_iEnd == _iStart)
					return true;

				auto pSource = mp_pTokens->f_GetSource().f_GetStr();
				for (umint i = _iEnd; i < _iStart; ++i)
				{
					auto Char = pSource[i];
					if (Char == '\n' || Char == '\r')
						return true;
				}

				return false;
			}
		;

		if (!_bNested && !bHeader && !fIsTightOrBroken(Previous.f_GetEnd(), Open.m_iOffset))
			return 0;

		// A list spaced inside its markers is spaced alike at both, and tight against its name: 'enable_if_t< cB >'. A
		// comparison is never written tight against the name in front of it.
		// A comment written tight against the '<' names the first argument and spaces nothing: 'f</*bSin=*/ false>'.
		auto iBehindOpen = mp_Significant[_iToken] + 1;
		auto const *pBehindOpen = iBehindOpen < Tokens.f_GetLen() ? &Tokens[iBehindOpen] : nullptr;
		bool bNamedFirst = pBehindOpen && pBehindOpen->m_Kind == ECodeTokenKind::mc_BlockComment && pBehindOpen->m_iOffset == Open.f_GetEnd();
		bool bSpacedInside
			= !_bNested && !bHeader && !bNamedFirst && _iToken + 1 < mp_Significant.f_GetLen() && !fIsTightOrBroken(Open.f_GetEnd(), Tokens[mp_Significant[_iToken + 1]].m_iOffset)
		;
		if (bSpacedInside && Previous.f_GetEnd() != Open.m_iOffset)
			return 0;

		auto fClose = [&](umint _iClose) -> umint
			{
				if (!bSpacedInside)
					return _iClose;

				auto const &Close = Tokens[mp_Significant[_iClose]];
				auto iBefore = Tokens[mp_Significant[_iClose - 1]].f_GetEnd();

				return iBefore != Close.m_iOffset ? _iClose : 0;
			}
		;

		// A search that meets no '>' fails alike from every '<' it passes at its own level, which a table of comparisons holds
		// thousands of: 'a<b, c<d'. Those fail at once.
		if (_iToken < mp_NoAngleCloser.f_GetLen() && mp_NoAngleCloser[_iToken])
			return 0;

		bool bCloserSeen = false;
		auto fFail = [&](umint _iStop) -> umint
			{
				if (bCloserSeen)
					return 0;

				if (mp_NoAngleCloser.f_GetLen() != mp_Significant.f_GetLen())
				{
					mp_NoAngleCloser.f_SetLen(mp_Significant.f_GetLen());
					for (auto &bNoCloser : mp_NoAngleCloser)
						bNoCloser = 0;
				}

				umint nInside = 0;
				for (auto i = _iToken; i < _iStop; ++i)
				{
					auto const &Token = Tokens[mp_Significant[i]];
					bool bSkipped = mp_pTokens->f_IsText(Token, "{") && (nInside || !fp_IsBlockBrace(i) || fp_IsLambdaBody(i));
					if (mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "[") || bSkipped)
						++nInside;
					else if (nInside && (mp_pTokens->f_IsText(Token, ")") || mp_pTokens->f_IsText(Token, "]") || mp_pTokens->f_IsText(Token, "}")))
						--nInside;
					else if (!nInside && mp_pTokens->f_IsText(Token, "<") && !(i > _iToken && mp_pTokens->f_IsText(Tokens[mp_Significant[i - 1]], "operator")))
						mp_NoAngleCloser[i] = 1;
				}

				return 0;
			}
		;

		umint nDepth = 0;
		umint nBrackets = 0;
		for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				continue;

			// A template argument can be a function type, an array bound, a braced value or a
			// lambda, so a balanced group inside the list is skipped rather than ending the
			// search. Any other brace that opens a block belongs to whatever the '<' really
			// was, and ends it.
			bool bSkippedBrace = mp_pTokens->f_IsText(Token, "{") && (nBrackets || !fp_IsBlockBrace(i) || fp_IsLambdaBody(i));
			if (mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "[") || bSkippedBrace)
			{
				++nBrackets;

				continue;
			}

			if (nBrackets)
			{
				if (mp_pTokens->f_IsText(Token, ")") || mp_pTokens->f_IsText(Token, "]") || mp_pTokens->f_IsText(Token, "}"))
					--nBrackets;

				continue;
			}

			// Behind 'operator' the token names the operator, and opens or closes no list: 'operator <' and 'operator >'.
			if (i > _iToken && mp_pTokens->f_IsText(Tokens[mp_Significant[i - 1]], "operator"))
				continue;

			if (mp_pTokens->f_IsText(Token, "<"))
				++nDepth;
			else if (mp_pTokens->f_IsText(Token, ">"))
			{
				bCloserSeen = true;
				if (!--nDepth)
					return fClose(i);
			}
			else if (mp_pTokens->f_IsText(Token, ">>"))
			{
				// Before the token is split, a '>>' closes this level and leaves the other
				// half to the list around it. A '>>=' is a shift assignment wherever it stands.
				bCloserSeen = true;
				if (nDepth < 2)
					return fClose(i);

				nDepth -= 2;
				if (!nDepth)
					return fClose(i);
			}
			else if (mp_pTokens->f_IsText(Token, ";") || mp_pTokens->f_IsText(Token, "{") || mp_pTokens->f_IsText(Token, "}") || fg_IsClosingBracket(*mp_pTokens, Token))
				return fFail(i);
		}

		return fFail(mp_Significant.f_GetLen());
	}

	// A compound requirement inside a requires expression opens with a brace where a statement
	// does, and holds an expression rather than statements: '{ _fOnEntry(_Key) } -> cFoo;'.
	// A block is never followed by an arrow or 'noexcept', and one holding an expression
	// alone, with no terminator or brace of its own, is none either.
	bool CCodeStructure::fp_IsRequirementBrace(umint _iToken) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		umint nDepth = 0;
		bool bInnerTerminator = false;
		bool bEmpty = true;
		for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (i > _iToken && nDepth == 1 && !mp_pTokens->f_IsText(Token, "}"))
				bEmpty = false;

			if (mp_pTokens->f_IsText(Token, "{") || mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "["))
			{
				if (nDepth == 1 && mp_pTokens->f_IsText(Token, "{"))
					bInnerTerminator = true;

				++nDepth;
			}
			else if (mp_pTokens->f_IsText(Token, "}") || mp_pTokens->f_IsText(Token, ")") || mp_pTokens->f_IsText(Token, "]"))
			{
				if (--nDepth)
					continue;

				if (i + 1 >= mp_Significant.f_GetLen())
					return false;

				auto const &After = Tokens[mp_Significant[i + 1]];
				if (mp_pTokens->f_IsText(After, "->") || mp_pTokens->f_IsText(After, "noexcept"))
					return true;

				return mp_pTokens->f_IsText(After, ";") && !bEmpty && !bInnerTerminator;
			}
			else if (nDepth == 1 && mp_pTokens->f_IsText(Token, ";"))
				bInnerTerminator = true;
		}

		return false;
	}

	// A brace behind a lambda's introducer is its body whatever it holds, and an empty
	// one holds nothing to tell it by. The introducer is a capture list, or that and a
	// parameter list with 'mutable' or 'noexcept' behind it; a capture list is told
	// from a subscript by standing where no operand is in front of it.
	bool CCodeStructure::fp_IsLambdaBody(umint _iToken) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		if (mp_pTokens->f_IsC(Tokens[mp_Significant[_iToken]]))
			return false;

		auto fOpens = [&](umint _iClose, ch8 const *_pOpen, ch8 const *_pClose) -> aint
			{
				umint nNested = 0;
				for (auto i = aint(_iClose); i >= 0; --i)
				{
					auto const &Token = Tokens[mp_Significant[umint(i)]];
					if (mp_pTokens->f_IsText(Token, _pClose))
						++nNested;
					else if (mp_pTokens->f_IsText(Token, _pOpen) && !--nNested)
						return i;
				}

				return -1;
			}
		;

		auto fIsSpecifier = [&](aint _iSignificant)
			{
				auto const &Token = Tokens[mp_Significant[umint(_iSignificant)]];

				return mp_pTokens->f_IsText(Token, "mutable") || mp_pTokens->f_IsText(Token, "noexcept");
			}
		;

		auto iBefore = aint(_iToken) - 1;
		while (iBefore >= 0 && fIsSpecifier(iBefore))
			--iBefore;

		if (iBefore >= 0 && mp_pTokens->f_IsText(Tokens[mp_Significant[umint(iBefore)]], ")"))
			iBefore = fOpens(umint(iBefore), "(", ")") - 1;

		if (iBefore >= 0 && mp_pTokens->f_IsText(Tokens[mp_Significant[umint(iBefore)]], "]"))
		{
			auto iOpen = fOpens(umint(iBefore), "[", "]");
			if (iOpen == 0)
				return true;

			if (iOpen > 0)
			{
				auto const &Front = Tokens[mp_Significant[umint(iOpen) - 1]];
				bool bOperand
					= (Front.m_Kind == ECodeTokenKind::mc_Identifier && !mp_pTokens->f_IsText(Front, "return") && !mp_pTokens->f_IsText(Front, "co_return"))
					|| mp_pTokens->f_IsText(Front, ")")
					|| mp_pTokens->f_IsText(Front, "]")
				;
				if (!bOperand)
					return true;
			}
		}

		return false;
	}

	// A brace that holds statements is a block, even inside an argument list, where it is a
	// lambda body. A braced initializer holds expressions: it never has a statement
	// terminator at its own level, and no element of one can start with a keyword only a
	// statement begins with, which is what a body whose every statement is compound has
	// instead of a terminator: 'mutable { for (auto &Entry : Entries) { ... } }'.
	bool CCodeStructure::fp_IsBlockBrace(umint _iToken) const
	{
		constexpr ch8 const *c_pStatementKeywords[] =
			{
				"if"
				, "else"
				, "for"
				, "while"
				, "do"
				, "switch"
				, "try"
				, "catch"
				, "return"
				, "co_return"
				, "break"
				, "continue"
				, "goto"
				, "case"
				, "default"
			}
		;
		auto const &Tokens = mp_pTokens->f_GetTokens();

		if (fp_IsLambdaBody(_iToken))
			return true;

		umint nDepth = 0;
		for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				if (nDepth != 1)
					continue;

				for (auto pKeyword : c_pStatementKeywords)
				{
					if (mp_pTokens->f_IsText(Token, pKeyword))
						return true;
				}

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				continue;

			if (mp_pTokens->f_IsText(Token, "{") || mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "["))
				++nDepth;
			else if (mp_pTokens->f_IsText(Token, "}") || mp_pTokens->f_IsText(Token, ")") || mp_pTokens->f_IsText(Token, "]"))
			{
				if (!--nDepth)
					return false;
			}
			else if (nDepth == 1 && mp_pTokens->f_IsText(Token, ";"))
				return true;
		}

		return false;
	}

	// Objective-C's message send, '[Receiver Selector]' or '[Receiver Keyword: Argument Keyword: Argument]', writes
	// the receiver and the selector side by side, which no C++ bracket does. A selector part starts at a name standing
	// next to an operand, the first one with a ':' or the closer behind it and every other one with a ':'; a
	// conditional's 'b :' has an operator in front of the name. A bracket behind an operand subscripts it, and a
	// separator or an assignment in front of the first selector part makes a lambda's capture list:
	// 'Names[(int)_Value]' and '[&, pValue = (CFoo *)nullptr]'.
	void CCodeStructure::fp_ClassifyMessageSend(umint _iNode, umint _iOpen, umint _iClose)
	{
		constexpr ch8 const *c_pPrefixKeywords[] =
			{
				"return"
				, "co_return"
				, "co_await"
				, "co_yield"
				, "throw"
				, "new"
				, "delete"
				, "sizeof"
				, "alignof"
				, "typeid"
				, "case"
				, "else"
				, "do"
			}
		;

		auto const &Tokens = mp_pTokens->f_GetTokens();

		auto fEndsOperand = [&](CCodeToken const &_Token)
			{
				switch (_Token.m_Kind)
				{
				case ECodeTokenKind::mc_Identifier:
					for (auto pKeyword : c_pPrefixKeywords)
					{
						if (mp_pTokens->f_IsText(_Token, pKeyword))
							return false;
					}

					return true;
				case ECodeTokenKind::mc_Number:
				case ECodeTokenKind::mc_StringLiteral:
				case ECodeTokenKind::mc_CharLiteral:
					return true;
				default: return mp_pTokens->f_IsText(_Token, ")") || mp_pTokens->f_IsText(_Token, "]") || mp_pTokens->f_IsText(_Token, "}");
				}
			}
		;

		if (_iOpen)
		{
			auto const &Before = Tokens[mp_Significant[_iOpen - 1]];
			bool bSubscript = fEndsOperand(Before) && !mp_pTokens->f_IsText(Before, "}");
			if (bSubscript && mp_pTokens->f_IsText(Before, ")"))
			{
				// A clause's condition is no operand: 'if (bValid) [pObject f_Run];'.
				umint nClose = 0;
				for (auto iParen = _iOpen - 1;; --iParen)
				{
					auto const &Paren = Tokens[mp_Significant[iParen]];
					if (fg_IsClosingBracket(*mp_pTokens, Paren))
						++nClose;
					else if (fg_GetOpeningBracket(*mp_pTokens, Paren) != ECodeBracket::mc_None && !--nClose)
					{
						if (iParen)
						{
							auto const &Clause = Tokens[mp_Significant[iParen - 1]];
							constexpr ch8 const *c_pClauses[] =
								{
									"if"
									, "while"
									, "for"
									, "switch"
									, "catch"
									, "@synchronized"
									, "@catch"
								}
							;

							for (auto pClause : c_pClauses)
								bSubscript &= !mp_pTokens->f_IsText(Clause, pClause);
						}

						break;
					}

					if (!iParen)
						break;
				}
			}

			if (bSubscript)
				return;
		}

		TCVector<umint> Parts;
		umint nDepth = 0;
		bool bOperandBefore = false;
		for (auto i = _iOpen + 1; i < _iClose; ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (Parts.f_IsEmpty() && !nDepth && (mp_pTokens->f_IsText(Token, ",") || mp_pTokens->f_IsText(Token, "=")))
				return;

			if (fg_GetOpeningBracket(*mp_pTokens, Token) != ECodeBracket::mc_None)
			{
				++nDepth;

				continue;
			}

			if (fg_IsClosingBracket(*mp_pTokens, Token))
			{
				if (nDepth && !--nDepth)
					bOperandBefore = true;

				continue;
			}

			if (nDepth)
				continue;

			if (Token.m_Kind == ECodeTokenKind::mc_Identifier && bOperandBefore)
			{
				auto const &Next = Tokens[mp_Significant[i + 1]];
				bool bKeyword = mp_pTokens->f_IsText(Next, ":");
				if (bKeyword || (Parts.f_IsEmpty() && i + 1 == _iClose))
					Parts.f_Insert(mp_Significant[i]);
			}

			bOperandBefore = fEndsOperand(Token);
		}

		if (Parts.f_IsEmpty())
			return;

		auto &Node = mp_Nodes[_iNode];
		Node.m_bMessageSend = true;
		Node.m_SplitPoints = fg_Move(Parts);
	}

	umint CCodeStructure::fp_BuildGroup(umint _iParent, umint _iToken, ECodeBracket _Bracket, umint _iClose)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		if (mp_nGroupDepth >= mc_MaxBracketDepth)
		{
			mp_bComplete = false;
			mp_bTooDeep = true;
			mp_iIncompleteOffset = Tokens[mp_Significant[_iToken]].m_iOffset;

			return mp_Significant.f_GetLen();
		}

		++mp_nGroupDepth;
		auto LeaveGroup = g_OnScopeExit / [&]
			{
				--mp_nGroupDepth;
			}
		;

		auto iNode = fp_AddNode(ECodeNodeKind::mc_Group, _iParent);
		mp_Nodes[iNode].m_Bracket = _Bracket;
		mp_Nodes[iNode].m_iFirstToken = mp_Significant[_iToken];
		if (_Bracket == ECodeBracket::mc_Angle)
			mp_AngleBracket[mp_Significant[_iToken]] = 1;

		auto Closing = fg_GetClosingText(_Bracket);

		// An asm statement's operand sections start their lines like its operands do.
		bool bAsm = false;
		if (_Bracket == ECodeBracket::mc_Paren)
		{
			auto iBefore = aint(_iToken) - 1;
			while (iBefore >= 0 && fg_IsAsmQualifier(*mp_pTokens, Tokens[mp_Significant[umint(iBefore)]]))
				--iBefore;

			if (iBefore >= 0)
			{
				auto const &Keyword = Tokens[mp_Significant[umint(iBefore)]];
				bAsm = mp_pTokens->f_IsText(Keyword, "asm") || mp_pTokens->f_IsText(Keyword, "__asm__") || mp_pTokens->f_IsText(Keyword, "__asm");
			}
		}

		auto i = _iToken + 1;
		while (i < mp_Significant.f_GetLen())
		{
			fp_Note(iNode, i);
			auto const &Token = Tokens[mp_Significant[i]];

			// A template argument list closes where its match found its closer, which a '>' counted against a '<' inside it
			// that opens no list of its own stands in front of.
			if (mp_pTokens->f_IsText(Token, Closing) && (!_iClose || i == _iClose))
			{
				if (_Bracket == ECodeBracket::mc_Angle)
					mp_AngleBracket[mp_Significant[i]] = 1;

				if (_Bracket == ECodeBracket::mc_Square)
					fp_ClassifyMessageSend(iNode, _iToken, i);

				fp_Finish(iNode, mp_Significant[i]);

				return i + 1;
			}

			// A top-level separator is where the canonical split form starts a new line.
			bool bSeparator = mp_pTokens->f_IsText(Token, ",") || mp_pTokens->f_IsText(Token, ";") || (bAsm && (mp_pTokens->f_IsText(Token, ":") || mp_pTokens->f_IsText(Token, "::")));
			if (Token.m_Kind == ECodeTokenKind::mc_Punctuator && bSeparator)
			{
				mp_Nodes[iNode].m_SplitPoints.f_Insert(mp_Significant[i]);
				++i;

				continue;
			}

			auto Nested = fg_GetOpeningBracket(*mp_pTokens, Token);
			if (Nested == ECodeBracket::mc_Brace && fp_IsBlockBrace(i))
			{
				auto iBlock = fp_AddNode(ECodeNodeKind::mc_Block, iNode);
				mp_Nodes[iBlock].m_Bracket = ECodeBracket::mc_Brace;
				mp_Nodes[iBlock].m_iFirstToken = mp_Significant[i];

				auto iClose = fp_BuildBlock(iBlock, i + 1, true);
				if (iClose >= mp_Significant.f_GetLen())
				{
					mp_Nodes[iBlock].m_Kind = ECodeNodeKind::mc_Unsupported;
					fp_Finish(iBlock, mp_Significant.f_GetLast());
					fp_Finish(iNode, mp_Significant.f_GetLast());
					if (mp_bComplete)
						mp_iIncompleteOffset = Tokens[mp_Nodes[iBlock].m_iFirstToken].m_iOffset;

					mp_bComplete = false;

					return mp_Significant.f_GetLen();
				}

				fp_Finish(iBlock, mp_Significant[iClose]);
				i = iClose + 1;

				continue;
			}

			if (Nested != ECodeBracket::mc_None)
			{
				i = fp_BuildGroup(iNode, i, Nested);

				continue;
			}

			// Inside a template argument list a '<' the list's match counted against a '>' opens a list of its own, however
			// it is spaced: C++ would read the '>' as closing the outer list otherwise.
			if (auto iClose = fp_MatchAngleGroup(i, _Bracket == ECodeBracket::mc_Angle && _iClose))
			{
				i = fp_BuildGroup(iNode, i, ECodeBracket::mc_Angle, iClose);

				continue;
			}

			if (fg_IsClosingBracket(*mp_pTokens, Token))
			{
				// Another scope's closer stands where this one's belongs, so the brackets do
				// not nest as written and the opener is the one left unclosed.
				mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
				fp_Finish(iNode, mp_Significant[i]);
				if (mp_bComplete)
					mp_iIncompleteOffset = Tokens[mp_Nodes[iNode].m_iFirstToken].m_iOffset;

				mp_bComplete = false;

				return i;
			}

			++i;
		}

		mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
		fp_Finish(iNode, mp_Significant.f_GetLast());
		if (mp_bComplete)
			mp_iIncompleteOffset = Tokens[mp_Nodes[iNode].m_iFirstToken].m_iOffset;

		mp_bComplete = false;

		return mp_Significant.f_GetLen();
	}

	// An Objective-C section opens with a line of its own and no terminator: '@interface CFoo : NSObject <CDelegate>',
	// with its instance variables in braces below it. A forward declaration, '@protocol CFoo;', is an ordinary
	// statement, which this leaves to the caller by returning zero.
	umint CCodeStructure::fp_BuildObjectiveCHeader(umint _iNode, umint _iToken)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto pSource = mp_pTokens->f_GetSource().f_GetStr();

		auto fSameLine = [&](umint _iLeft, umint _iRight)
			{
				for (auto i = Tokens[mp_Significant[_iLeft]].f_GetEnd(); i < Tokens[mp_Significant[_iRight]].m_iOffset; ++i)
				{
					auto Char = pSource[i];
					if (Char == '\n' || Char == '\r')
						return false;
				}

				return true;
			}
		;

		// The header ends where its instance variables' block opens, a method starts or the section ends, also on the
		// header's line: '@interface CFoo : NSObject { int m_X; }', '@protocol CFoo - (void)f_Run; @end'.
		auto fEndsHeader = [&](umint _iNext)
			{
				auto const &Next = Tokens[mp_Significant[_iNext]];
				if (mp_pTokens->f_IsText(Next, "{") || mp_pTokens->f_IsText(Next, "@end"))
					return true;

				bool bSign = mp_pTokens->f_IsText(Next, "-") || mp_pTokens->f_IsText(Next, "+");

				return bSign && _iNext + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iNext + 1]], "(");
			}
		;

		// A line break inside its brackets goes on with the header: '@interface CFoo<t_CA,' over 't_CB> (CBar)'.
		auto iLast = _iToken;
		smint Depth = 0;
		while (iLast + 1 < mp_Significant.f_GetLen() && (Depth > 0 || fSameLine(iLast, iLast + 1)) && !fEndsHeader(iLast + 1))
		{
			auto const &Next = Tokens[mp_Significant[iLast + 1]];
			if (mp_pTokens->f_IsText(Next, ";"))
				return 0;

			if (mp_pTokens->f_IsText(Next, "(") || mp_pTokens->f_IsText(Next, "<"))
				++Depth;
			else if (mp_pTokens->f_IsText(Next, ")") || mp_pTokens->f_IsText(Next, ">"))
				--Depth;
			else if (mp_pTokens->f_IsText(Next, ">>"))
				Depth -= 2;

			++iLast;
		}

		auto &Node = mp_Nodes[_iNode];
		Node.m_bObjectiveCHeader = true;

		auto i = _iToken;
		while (i <= iLast)
		{
			fp_Note(_iNode, i);
			auto const &Token = Tokens[mp_Significant[i]];
			if (mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "<"))
			{
				i = fp_BuildGroup(_iNode, i, mp_pTokens->f_IsText(Token, "(") ? ECodeBracket::mc_Paren : ECodeBracket::mc_Angle);

				continue;
			}

			++i;
		}

		if (i < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[i]], "{"))
		{
			auto iBlock = fp_AddNode(ECodeNodeKind::mc_Block, _iNode);
			mp_Nodes[iBlock].m_Bracket = ECodeBracket::mc_Brace;
			mp_Nodes[iBlock].m_iFirstToken = mp_Significant[i];

			auto iClose = fp_BuildBlock(iBlock, i + 1, true);
			if (iClose >= mp_Significant.f_GetLen())
			{
				mp_Nodes[iBlock].m_Kind = ECodeNodeKind::mc_Unsupported;
				fp_Finish(iBlock, mp_Significant.f_GetLast());
				fp_Finish(_iNode, mp_Significant.f_GetLast());
				if (mp_bComplete)
					mp_iIncompleteOffset = Tokens[mp_Nodes[iBlock].m_iFirstToken].m_iOffset;

				mp_bComplete = false;

				return mp_Significant.f_GetLen();
			}

			fp_Finish(iBlock, mp_Significant[iClose]);
			fp_Finish(_iNode, mp_Significant[iClose]);

			return iClose + 1;
		}

		fp_Finish(_iNode, mp_Significant[i - 1]);

		return i;
	}

	// A method's declaration ends at its terminator and its definition with its body, which is a block whatever
	// it holds: '- (BOOL)windowShouldClose: (NSWindow *)_pSender {}'.
	umint CCodeStructure::fp_BuildObjectiveCMethod(umint _iNode, umint _iToken)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		mp_Nodes[_iNode].m_bObjectiveCMethod = true;

		auto i = _iToken;
		while (i < mp_Significant.f_GetLen())
		{
			fp_Note(_iNode, i);
			auto const &Token = Tokens[mp_Significant[i]];
			if (mp_pTokens->f_IsText(Token, ";"))
			{
				fp_Finish(_iNode, mp_Significant[i]);

				return i + 1;
			}

			if (mp_pTokens->f_IsText(Token, "{"))
			{
				auto iBlock = fp_AddNode(ECodeNodeKind::mc_Block, _iNode);
				mp_Nodes[iBlock].m_Bracket = ECodeBracket::mc_Brace;
				mp_Nodes[iBlock].m_iFirstToken = mp_Significant[i];

				auto iClose = fp_BuildBlock(iBlock, i + 1, true);
				if (iClose >= mp_Significant.f_GetLen())
				{
					mp_Nodes[iBlock].m_Kind = ECodeNodeKind::mc_Unsupported;
					fp_Finish(iBlock, mp_Significant.f_GetLast());
					fp_Finish(_iNode, mp_Significant.f_GetLast());
					if (mp_bComplete)
						mp_iIncompleteOffset = Tokens[mp_Nodes[iBlock].m_iFirstToken].m_iOffset;

					mp_bComplete = false;

					return mp_Significant.f_GetLen();
				}

				fp_Finish(iBlock, mp_Significant[iClose]);
				fp_Finish(_iNode, mp_Significant[iClose]);

				return iClose + 1;
			}

			auto Bracket = fg_GetOpeningBracket(*mp_pTokens, Token);
			if (Bracket != ECodeBracket::mc_None)
			{
				i = fp_BuildGroup(_iNode, i, Bracket);

				continue;
			}

			if (fg_IsClosingBracket(*mp_pTokens, Token))
				break;

			++i;
		}

		mp_Nodes[_iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
		fp_Finish(_iNode, mp_Significant[fg_Min(i, mp_Significant.f_GetLen() - 1)]);
		if (mp_bComplete)
			mp_iIncompleteOffset = Tokens[mp_Nodes[_iNode].m_iFirstToken].m_iOffset;

		mp_bComplete = false;

		return fg_Min(i, mp_Significant.f_GetLen());
	}

	// Whether the block holds an enum's enumerators, which are one list however macros spell them.
	bool CCodeStructure::f_IsEnumBody(umint _iBlock) const
	{
		if (_iBlock >= mp_Nodes.f_GetLen() || mp_Nodes[_iBlock].m_Kind != ECodeNodeKind::mc_Block)
			return false;

		auto iOwner = mp_Nodes[_iBlock].m_iParent;
		if (iOwner >= mp_Nodes.f_GetLen() || mp_Nodes[iOwner].m_Kind != ECodeNodeKind::mc_Statement)
			return false;

		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto iKey = mp_Nodes[iOwner].m_iFirstToken;
		if (mp_pTokens->f_IsText(Tokens[iKey], "typedef"))
		{
			do
				++iKey;
			while (iKey < Tokens.f_GetLen() && !fg_IsSignificant(Tokens[iKey].m_Kind));
		}

		return iKey < Tokens.f_GetLen() && mp_pTokens->f_IsText(Tokens[iKey], "enum");
	}

	// The offset of the token from the start of its line, which the layout's moves of the line it starts are measured by.
	umint CCodeStructure::fp_GetColumn(umint _iToken) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto iLineStart = _iToken;
		while (iLineStart && Tokens[iLineStart - 1].m_Kind != ECodeTokenKind::mc_Newline)
			--iLineStart;

		// Measured as the formatter measures it, a tab to the next stop, so a decision reads the line as the next pass does.
		umint nColumns = 0;
		auto const &LineStart = Tokens[iLineStart];
		auto const &Token = Tokens[_iToken];
		if (!fg_MeasureTextColumns(mp_pTokens->f_GetSource().f_GetStr() + LineStart.m_iOffset, Token.m_iOffset - LineStart.m_iOffset, mp_nTabWidth, nColumns))
			return Token.m_iOffset - LineStart.m_iOffset;

		return nColumns;
	}

	// Whether the block is a function's body or one nested in it, where a statement is code to run rather than a
	// declaration of a class's or a namespace's.
	bool CCodeStructure::fp_IsInFunctionBlock(umint _iBlock) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		for (auto iScope = _iBlock; iScope < mp_Nodes.f_GetLen(); iScope = mp_Nodes[iScope].m_iParent)
		{
			auto const &Scope = mp_Nodes[iScope];
			if (Scope.m_Kind != ECodeNodeKind::mc_Block || Scope.m_iParent >= mp_Nodes.f_GetLen())
				return false;

			auto const &Owner = mp_Nodes[Scope.m_iParent];
			if (Owner.m_Kind == ECodeNodeKind::mc_File)
				return false;

			constexpr ch8 const *c_pScopes[] =
				{
					"namespace"
					, "extern"
					, "struct"
					, "class"
					, "union"
					, "template"
					, "typedef"
					, "enum"
				}
			;

			if (fg_IsAnyText(*mp_pTokens, Tokens[Owner.m_iFirstToken], c_pScopes))
				return false;

			if (Owner.m_Kind == ECodeNodeKind::mc_Statement)
				return true;
		}

		return false;
	}

	// Whether a call standing alone on its line in a function's body, 'IOTRACE((...))', ends its statement where the next
	// line starts another at its column with a name: 'rc = f();'. In a body a statement is code, which no declaration's
	// head on the line above continues at the head's own column.
	bool CCodeStructure::fp_EndsCallStatement(umint _iBlock, umint _iFirst, umint _iNext) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		if (_iNext >= mp_Significant.f_GetLen() || _iFirst + 1 >= _iNext || !fp_IsInFunctionBlock(_iBlock))
			return false;

		if (!mp_pTokens->f_IsText(Tokens[mp_Significant[_iFirst + 1]], "(") || !mp_pTokens->f_IsText(Tokens[mp_Significant[_iNext - 1]], ")"))
			return false;

		// An attribute or a type's operator belongs to what follows it, and a keyword taking a parenthesis goes on behind it:
		// 'new (pMemory)' over 'CFoo(1);'.
		constexpr ch8 const *c_pLeading[] =
			{
				"typeof"
				, "__typeof__"
				, "__typeof"
				, "typeof_unqual"
				, "__typeof_unqual__"
				, "__typeof_unqual"
				, "decltype"
				, "_Alignas"
				, "alignas"
				, "__attribute__"
				, "__attribute"
				, "__declspec"
				, "new"
				, "delete"
				, "sizeof"
				, "alignof"
				, "typeid"
				, "return"
				, "co_return"
				, "co_yield"
				, "co_await"
				, "throw"
				, "noexcept"
				, "static_assert"
				, "_Static_assert"
				, "operator"
			}
		;

		if (fg_IsAnyText(*mp_pTokens, Tokens[mp_Significant[_iFirst]], c_pLeading))
			return false;

		umint nNewlines = 0;
		for (auto iGap = mp_Significant[_iNext - 1] + 1; iGap < mp_Significant[_iNext]; ++iGap)
			nNewlines += Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

		return nNewlines == 1 && Tokens[mp_Significant[_iNext]].m_Kind == ECodeTokenKind::mc_Identifier && fp_GetColumn(mp_Significant[_iNext]) == fp_GetColumn(mp_Significant[_iFirst]);
	}

	// Whether the name at _iFirst, the statement's first, is a macro the naming does not list that the statement ends
	// with, the next statement starting at _iNext: that one has to start no further in than the macro, since the layout puts
	// what it moves behind an opened argument list one level in, and with a keyword opening a declaration, which no
	// layout of a declaration starts a line with.
	bool CCodeStructure::fp_IsUnlistedMacro(umint _iFirst, umint _iNext) const
	{
		constexpr ch8 const *c_pTakesParenthesis[] =
			{
				"return"
				, "throw"
				, "co_return"
				, "co_yield"
				, "co_await"
				, "sizeof"
				, "alignof"
				, "alignas"
				, "_Alignas"
				, "decltype"
				, "typeof"
				, "__typeof__"
				, "__typeof"
				, "typeof_unqual"
				, "__typeof_unqual__"
				, "__typeof_unqual"
				, "typeid"
				, "static_assert"
				, "_Static_assert"
				, "new"
				, "delete"
				, "operator"
				, "explicit"
				, "noexcept"
				, "requires"
				, "case"
				, "__attribute__"
				, "__attribute"
				, "__declspec"
				, "_Pragma"
				, "__pragma"
				, "asm"
				, "__asm__"
				, "__asm"
				, "_Generic"
				, "using"
				, "typedef"
				, "template"
				, "default"
				, "else"
				, "do"
				, "break"
				, "continue"
				, "goto"
				, "public"
				, "private"
				, "protected"
			}
		;
		constexpr ch8 const *c_pDeclarationStarts[] =
			{
				"namespace"
				, "class"
				, "struct"
				, "union"
				, "enum"
				, "template"
				, "typedef"
				, "using"
				, "extern"
				, "static"
				, "inline"
				, "virtual"
				, "friend"
				, "void"
				, "auto"
				, "bool"
				, "char"
				, "int"
				, "long"
				, "short"
				, "signed"
				, "unsigned"
				, "float"
				, "double"
			}
		;

		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto const &First = Tokens[mp_Significant[_iFirst]];
		constexpr ch8 const *c_pKeywords[] =
			{
				"const"
				, "volatile"
				, "constexpr"
				, "consteval"
				, "constinit"
				, "mutable"
				, "register"
				, "thread_local"
				, "typename"
				, "export"
				, "module"
				, "import"
				, "concept"
				, "wchar_t"
				, "char8_t"
				, "char16_t"
				, "char32_t"
				, "final"
				, "override"
			}
		;

		bool bKeyword = fg_IsAnyText(*mp_pTokens, First, c_pTakesParenthesis) || fg_IsAnyText(*mp_pTokens, First, c_pDeclarationStarts) || fg_IsAnyText(*mp_pTokens, First, c_pKeywords);
		if (First.m_Kind != ECodeTokenKind::mc_Identifier || bKeyword || mp_pTokens->f_GetTextPointer(First)[0] == '@')
			return false;

		if (_iNext >= mp_Significant.f_GetLen())
			return true;

		// A case's label goes on with no statement, wherever it stands: 'deliberate_fall_through' over 'case 2:'. Nor
		// does a template's header or a namespace, which only keywords stand in front of.
		auto const &Label = Tokens[mp_Significant[_iNext]];
		bool bDefault = mp_pTokens->f_IsText(Label, "default") && _iNext + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iNext + 1]], ":");
		if (mp_pTokens->f_IsText(Label, "case") || bDefault || mp_pTokens->f_IsText(Label, "template") || mp_pTokens->f_IsText(Label, "namespace"))
			return true;

		// Nor does a name go on into a statement a keyword starts: 'BOOST_MATH_STD_USING' over 'if (a)'.
		constexpr ch8 const *c_pStatementStarts[] =
			{
				"if"
				, "for"
				, "while"
				, "switch"
				, "do"
				, "return"
				, "co_return"
				, "break"
				, "continue"
				, "goto"
				, "try"
			}
		;

		if (fg_IsAnyText(*mp_pTokens, Label, c_pStatementStarts))
			return true;

		// Nor does a name alone on its line go on behind a blank line or a directive, however far in what follows starts:
		// 'FT_BEGIN_HEADER' over a blank line over '  typedef FT_Error (*Func)(...);'.
		if (_iFirst + 1 == _iNext)
		{
			umint nNewlines = 0;
			bool bDirective = false;
			for (auto iGap = mp_Significant[_iFirst] + 1; iGap < mp_Significant[_iNext]; ++iGap)
			{
				nNewlines += Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;
				bDirective = bDirective || Tokens[iGap].m_Kind == ECodeTokenKind::mc_Preprocessor;
			}

			if (nNewlines > 1 || bDirective)
				return true;
		}

		if (fp_GetColumn(mp_Significant[_iNext]) > fp_GetColumn(mp_Significant[_iFirst]))
			return false;

		if (fg_IsAnyText(*mp_pTokens, Tokens[mp_Significant[_iNext]], c_pDeclarationStarts))
			return true;

		// Nor into a label, a name with a ':' behind it: 'THROW(...)' over 'bailout:'.
		if (_iNext + 1 < mp_Significant.f_GetLen() && Tokens[mp_Significant[_iNext]].m_Kind == ECodeTokenKind::mc_Identifier && mp_pTokens->f_IsText(Tokens[mp_Significant[_iNext + 1]], ":"))
			return true;

		// A call a blank line parts from what follows it is a statement of its own: 'DEnd(A)' over a blank line over
		// '[[nodiscard]] int f();'.
		if (_iFirst + 1 < _iNext && mp_pTokens->f_IsText(Tokens[mp_Significant[_iFirst + 1]], "(") && mp_pTokens->f_IsText(Tokens[mp_Significant[_iNext - 1]], ")"))
		{
			umint nNewlines = 0;
			for (auto iGap = mp_Significant[_iNext - 1] + 1; iGap < mp_Significant[_iNext]; ++iGap)
				nNewlines += Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

			if (nNewlines > 1)
				return true;
		}

		// Or a name alone on its line, as another macro of the kind is, behind a blank line or a directive, which no
		// declaration's words stand apart by: 'BOOL' over 'APIENTRY' over 'CreatePipeEx(...)' is one.
		auto const &Next = Tokens[mp_Significant[_iNext]];
		if (Next.m_Kind != ECodeTokenKind::mc_Identifier || fg_IsAnyText(*mp_pTokens, Next, c_pTakesParenthesis) || fg_IsAnyText(*mp_pTokens, Next, c_pKeywords))
			return false;

		// Or the macro's next invocation, a call of the same name: 'DFoo(A)' over 'DFoo(B)'.
		bool bInvoked = _iFirst + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iFirst + 1]], "(");
		bool bInvokedAgain = _iNext + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iNext + 1]], "(");
		bool bSameName
			= Next.m_nLength == First.m_nLength
			&& NMemory::fg_MemCmp((uint8 const *)mp_pTokens->f_GetTextPointer(Next), (uint8 const *)mp_pTokens->f_GetTextPointer(First), First.m_nLength) == 0
		;
		if (bInvoked && bInvokedAgain && bSameName)
			return true;

		// Or another macro's call standing where it does: a run of calls each starting a line at one column is a run of
		// statements where what follows the last starts a line at that column, a blank line parts it from the next, or the file or scope
		// ends, and otherwise the head of what follows: 'DBegin(A)' over 'DEnd(A)' over 'int g_A;', but 'GLOBAL(void)' over
		// 'f(int a)' over '{' is a function's head. Every call of a run ends the same way, so a run is walked once.
		if (bInvoked && bInvokedAgain && fp_GetColumn(mp_Significant[_iNext]) == fp_GetColumn(mp_Significant[_iFirst]))
		{
			if (mp_CallRunEnds.f_GetLen() != mp_Significant.f_GetLen())
			{
				mp_CallRunEnds.f_SetLen(mp_Significant.f_GetLen());
				for (auto &End : mp_CallRunEnds)
					End = 0;
			}

			if (!mp_CallRunEnds[_iNext])
			{
				auto fBehindCall = [&](umint _iName) -> umint
					{
						umint nDepth = 0;
						for (auto iBehind = _iName + 1; iBehind < mp_Significant.f_GetLen(); ++iBehind)
						{
							auto const &Bracket = Tokens[mp_Significant[iBehind]];
							if (mp_pTokens->f_IsText(Bracket, "(") || mp_pTokens->f_IsText(Bracket, "[") || mp_pTokens->f_IsText(Bracket, "{"))
								++nDepth;
							else if (fg_IsClosingBracket(*mp_pTokens, Bracket) && !--nDepth)
								return iBehind + 1;
						}

						return mp_Significant.f_GetLen();
					}
				;

				auto fNewlines = [&](umint _iSignificant)
					{
						umint nNewlines = 0;
						for (auto iGap = mp_Significant[_iSignificant - 1] + 1; iGap < mp_Significant[_iSignificant]; ++iGap)
							nNewlines += Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

						return nNewlines;
					}
				;

				TCVector<umint> Run;
				bool bStatements = false;
				for (auto iCall = _iNext; true;)
				{
					Run.f_Insert(iCall);

					auto iBehind = fBehindCall(iCall);
					if (iBehind >= mp_Significant.f_GetLen())
					{
						bStatements = true;

						break;
					}

					auto const &Behind = Tokens[mp_Significant[iBehind]];
					auto nNewlines = fNewlines(iBehind);
					bool bName = Behind.m_Kind == ECodeTokenKind::mc_Identifier;
					bool bCall = bName && iBehind + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[iBehind + 1]], "(");
					bool bInRun
						= bCall && nNewlines == 1 && fp_GetColumn(mp_Significant[iBehind]) == fp_GetColumn(mp_Significant[_iFirst]) && !fg_IsAnyText(*mp_pTokens, Behind, c_pTakesParenthesis)
					;
					if (bInRun)
					{
						iCall = iBehind;

						continue;
					}

					// An attribute keyword on the next line goes on with the declaration the calls are a head of: 'DExport(A)'
					// over '__attribute__((x))' over 'void f();'.
					bool bAttribute = nNewlines == 1 && fg_IsAnyText(*mp_pTokens, Behind, c_pTakesParenthesis);
					bool bAtRun = fp_GetColumn(mp_Significant[iBehind]) == fp_GetColumn(mp_Significant[_iFirst]);
					bStatements = mp_pTokens->f_IsText(Behind, "}") || nNewlines > 1 || (nNewlines && bName && !bAttribute && bAtRun);

					break;
				}

				for (auto iRunCall : Run)
					mp_CallRunEnds[iRunCall] = bStatements ? 2 : 1;
			}

			if (mp_CallRunEnds[_iNext] == 2)
				return true;
		}

		bool bParted = false;
		umint nNewlines = 0;
		for (auto iGap = mp_Significant[_iFirst] + 1; iGap < mp_Significant[_iNext] && !bParted; ++iGap)
		{
			auto Kind = Tokens[iGap].m_Kind;
			if (Kind == ECodeTokenKind::mc_Newline)
				bParted = ++nNewlines > 1;
			else if (Kind == ECodeTokenKind::mc_Preprocessor)
				bParted = true;
			else if (Kind != ECodeTokenKind::mc_Whitespace)
				nNewlines = 0;
		}

		if (!bParted)
			return false;

		// The layout writes the names of a declaration's head on one line, so they are read up to where the declarator
		// goes on wherever the source breaks them: 'BOOL APIENTRY CreatePipeEx(...)', 'kmp_int32 volatile tail_id;'. Short
		// of a parameter list a declaration has a type and a name, where one name alone is assigned or called: 'a = 1;'.
		constexpr ch8 const *c_pDeclaratorEnds[] =
			{
				";"
				, "="
				, "["
				, "{"
				, ","
				, ":"
			}
		;

		umint nNames = 1;
		for (auto iGap = mp_Significant[_iNext] + 1; iGap < Tokens.f_GetLen(); ++iGap)
		{
			auto const &Gap = Tokens[iGap];
			if (mp_pTokens->f_IsText(Gap, "("))
				return true;

			if (fg_IsAnyText(*mp_pTokens, Gap, c_pDeclaratorEnds))
				return nNames > 1;

			nNames += Gap.m_Kind == ECodeTokenKind::mc_Identifier;

			bool bName
				= !fg_IsSignificant(Gap.m_Kind)
				|| (Gap.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(*mp_pTokens, Gap, c_pTakesParenthesis))
				|| mp_pTokens->f_IsText(Gap, "::")
				|| mp_pTokens->f_IsText(Gap, "*")
				|| mp_pTokens->f_IsText(Gap, "&")
			;
			if (!bName)
				return false;
		}

		return true;
	}

	umint CCodeStructure::fp_BuildStatement(umint _iParent, umint _iToken)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto iNode = fp_AddNode(ECodeNodeKind::mc_Statement, _iParent);
		mp_Nodes[iNode].m_iFirstToken = mp_Significant[_iToken];

		auto i = _iToken;
		bool bAfterCloseParen = false;
		bool bRequiresClause = false;
		// A parameter list in front of a body makes it a function's, whatever type the function returns: 'enum EFoo f() {'.
		bool bParameterList = false;
		bool bAfterTrailingReturn = false;
		bool bConditional = false;

		auto const &First = Tokens[mp_Significant[_iToken]];
		bool bLabel = mp_pTokens->f_IsText(First, "case");

		// An access specifier with a section's name labels what follows as it does alone: Qt's 'private Q_SLOTS:' or
		// 'public slots:'.
		bool bAccess = mp_pTokens->f_IsText(First, "public") || mp_pTokens->f_IsText(First, "private") || mp_pTokens->f_IsText(First, "protected");
		if
		(
			bAccess
			&& _iToken + 2 < mp_Significant.f_GetLen()
			&& Tokens[mp_Significant[_iToken + 1]].m_Kind == ECodeTokenKind::mc_Identifier
			&& mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken + 2]], ":")
		)
		{
			bLabel = true;
		}

		bool bDefines = mp_pTokens->f_IsText(First, "enum") || mp_pTokens->f_IsText(First, "struct") || mp_pTokens->f_IsText(First, "class") || mp_pTokens->f_IsText(First, "union");
		bool bClause
			= mp_pTokens->f_IsText(First, "if")
			|| mp_pTokens->f_IsText(First, "for")
			|| mp_pTokens->f_IsText(First, "while")
			|| mp_pTokens->f_IsText(First, "switch")
			|| mp_pTokens->f_IsText(First, "catch")
			|| mp_pTokens->f_IsText(First, "@catch")
			|| mp_pTokens->f_IsText(First, "@synchronized")
		;

		// A macro invoked where a clause stands, 'BOOST_IF_CONSTEXPR(c)' over a block and an 'else', is the 'if' it expands
		// to, as only an 'if' takes an 'else'.
		if (!bClause && First.m_Kind == ECodeTokenKind::mc_Identifier && _iParent < mp_Nodes.f_GetLen() && mp_Nodes[_iParent].m_Kind == ECodeNodeKind::mc_Block)
		{
			auto fSkipGroup = [&](umint _iOpen)
				{
					umint nDepth = 0;
					for (auto iGroup = _iOpen; iGroup < mp_Significant.f_GetLen(); ++iGroup)
					{
						auto const &Bracket = Tokens[mp_Significant[iGroup]];
						if (Bracket.m_Kind != ECodeTokenKind::mc_Punctuator)
							continue;

						if (mp_pTokens->f_IsText(Bracket, "(") || mp_pTokens->f_IsText(Bracket, "[") || mp_pTokens->f_IsText(Bracket, "{"))
							++nDepth;
						else if (fg_IsClosingBracket(*mp_pTokens, Bracket) && (!nDepth || !--nDepth))
							return iGroup + 1;
					}

					return mp_Significant.f_GetLen();
				}
			;

			auto fIs = [&](umint _iSignificant, ch8 const *_pText)
				{
					return _iSignificant < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iSignificant]], _pText);
				}
			;

			// So is one ending its line with the statement it guards on the next, one level in: 'Catch(Fault)' over
			// 'd.ps = Fault;', as a loop macro's 'list_for_each(p, Head)' over 'f(p);'.
			if (fIs(_iToken + 1, "("))
			{
				auto iBlock = fSkipGroup(_iToken + 1);
				bool bGuards = fIs(iBlock, "{") && fIs(fSkipGroup(iBlock), "else");
				if (!bGuards && iBlock < mp_Significant.f_GetLen() && !mp_pTokens->f_HasRole(First, ECodeNameRole::mc_Macro))
				{
					auto const &Guarded = Tokens[mp_Significant[iBlock]];
					umint nNewlines = 0;
					for (auto iGap = mp_Significant[iBlock - 1] + 1; iGap < mp_Significant[iBlock]; ++iGap)
						nNewlines += Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

					// Only a function's statements guard: in a class's or a namespace's body the call heads a declaration
					// written on the lines below it, as a qualifier or a requires clause behind it does.
					constexpr ch8 const *c_pDeclarationTails[] =
						{
							"requires"
							, "noexcept"
							, "const"
							, "volatile"
							, "override"
							, "final"
							, "throw"
							, "try"
							, "mutable"
							, "constexpr"
							, "else"
							, "case"
							, "default"
							, "break"
							, "continue"
							, "goto"
							, "return"
							, "public"
							, "private"
							, "protected"
						}
					;

					bool bInBody = fp_IsInFunctionBlock(_iParent);

					// No keyword or operator naming a type is a clause's: 'typeof(x) y', 'return (int)' over 'f()'.
					constexpr ch8 const *c_pTypeOperators[] =
						{
							"typeof"
							, "__typeof__"
							, "__typeof"
							, "typeof_unqual"
							, "__typeof_unqual__"
							, "__typeof_unqual"
							, "decltype"
							, "_Alignas"
							, "alignas"
							, "__attribute__"
							, "__attribute"
							, "__declspec"
							, "_Atomic"
							, "return"
							, "co_return"
							, "co_await"
							, "co_yield"
							, "throw"
							, "new"
							, "delete"
							, "sizeof"
							, "alignof"
							, "typeid"
							, "static_assert"
							, "noexcept"
							, "case"
							, "else"
							, "do"
							, "goto"
							, "static_cast"
							, "dynamic_cast"
							, "reinterpret_cast"
							, "const_cast"
						}
					;

					// Nor is one holding a body, a type's or a statement expression's: '_Atomic(struct { int a; }) x;'.
					bool bHoldsBrace = false;
					for (auto iInside = _iToken + 2; iInside + 1 < iBlock && !bHoldsBrace; ++iInside)
						bHoldsBrace = mp_pTokens->f_IsText(Tokens[mp_Significant[iInside]], "{");

					bGuards
						= bInBody
						&& !bHoldsBrace
						&& !fg_IsAnyText(*mp_pTokens, First, c_pTypeOperators)
						&& nNewlines == 1
						&& Guarded.m_Kind == ECodeTokenKind::mc_Identifier
						&& !fg_IsAnyText(*mp_pTokens, Guarded, c_pDeclarationTails)
						&& fp_GetColumn(mp_Significant[iBlock]) > fp_GetColumn(mp_Significant[_iToken])
					;
				}

				if (bGuards)
				{
					bClause = true;
					mp_Nodes[iNode].m_bClauseMacro = true;
				}
			}
		}

		// C++26's expansion statement guards what follows it as a 'for' does, which no rule reads it as yet, so it keeps
		// its lines: 'template for (auto x : Tuple) f(x);'.
		if (mp_pTokens->f_IsText(First, "template") && _iToken + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken + 1]], "for"))
			mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;

		// So does C++23's 'if consteval', whose branches have no condition to end at and must keep their braces, which no
		// rule reads either: 'if !consteval { f(); } else { g(); }'. The statement runs on over its 'else' branches.
		if (mp_pTokens->f_IsText(First, "if"))
		{
			auto iWord = _iToken + 1;
			if (iWord < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[iWord]], "!"))
				++iWord;

			if (iWord < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[iWord]], "consteval"))
				mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
		}

		// A keyword that only introduces the statement after it ends here, and so does one of Objective-C's that
		// ends a section or opens a visibility of its own.
		constexpr ch8 const *c_pAlone[] =
			{
				"else"
				, "do"
				, "try"
				, "@try"
				, "@finally"
				, "@autoreleasepool"
				, "@end"
				, "@public"
				, "@private"
				, "@protected"
				, "@package"
				, "@optional"
				, "@required"
			}
		;

		bool bAlone = false;
		for (auto pKeyword : c_pAlone)
			bAlone |= mp_pTokens->f_IsText(First, pKeyword);

		if (bAlone)
		{
			fp_Note(iNode, _iToken);
			fp_Finish(iNode, mp_Significant[_iToken]);

			return _iToken + 1;
		}

		if (mp_pTokens->f_IsText(First, "@interface") || mp_pTokens->f_IsText(First, "@implementation") || mp_pTokens->f_IsText(First, "@protocol"))
		{
			if (auto iNext = fp_BuildObjectiveCHeader(iNode, _iToken))
				return iNext;
		}

		// Only a method starts a statement at file level with a sign and a parenthesis: '- (void)f_Run;'.
		bool bSign = mp_pTokens->f_IsText(First, "-") || mp_pTokens->f_IsText(First, "+");
		if (bSign && mp_Nodes[_iParent].m_Kind == ECodeNodeKind::mc_File && _iToken + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken + 1]], "("))
			return fp_BuildObjectiveCMethod(iNode, _iToken);

		// A name alone on its line is a macro that is a statement of its own where what follows could not go on with it:
		// '_LIBCPP_BEGIN_NAMESPACE_STD' over 'template <...>'.
		if (First.m_Kind == ECodeTokenKind::mc_Identifier && _iParent < mp_Nodes.f_GetLen() && mp_Nodes[_iParent].m_Kind != ECodeNodeKind::mc_Group && !f_IsEnumBody(_iParent))
		{
			bool bAlone = _iToken + 1 >= mp_Significant.f_GetLen();
			for (auto iGap = mp_Significant[_iToken] + 1; !bAlone && iGap < mp_Significant[_iToken + 1]; ++iGap)
				bAlone = Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

			if (bAlone && fp_IsUnlistedMacro(_iToken, _iToken + 1))
			{
				fp_Note(iNode, _iToken);
				fp_Finish(iNode, mp_Significant[_iToken]);

				return _iToken + 1;
			}
		}

		// A template header and a requires clause each occupy their own line. The header
		// only holds the line it is on; the declaration behind it is laid out as usual.
		if (mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken]], "template"))
			mp_Nodes[iNode].m_bTemplateHeader = true;

		constexpr ch8 const *c_pSectionWords[] =
			{
				"@interface"
				, "@implementation"
				, "@end"
				, "@property"
				, "@optional"
				, "@required"
				, "@class"
			}
		;

		while (i < mp_Significant.f_GetLen())
		{
			auto const &Token = Tokens[mp_Significant[i]];

			// An Objective-C section's word starts a statement of its own, behind an attribute in front of it too:
			// '__attribute__((objc_root_class))' over '@interface CFoo'.
			bool bProtocolSection = mp_pTokens->f_IsText(Token, "@protocol") && !(i + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[i + 1]], "("));
			if (i > _iToken && (bProtocolSection || fg_IsAnyText(*mp_pTokens, Token, c_pSectionWords)))
			{
				fp_Finish(iNode, mp_Significant[i - 1]);

				return i;
			}

			fp_Note(iNode, i);
			if (mp_pTokens->f_IsText(Token, "requires"))
			{
				mp_Nodes[iNode].m_bFixedLineBreaks = true;

				// A requires clause stands behind a declarator's parameter list or its qualifiers, where no operand is in front.
				if (i > _iToken)
				{
					auto const &Previous = Tokens[mp_Significant[i - 1]];
					constexpr ch8 const *c_pDeclaratorEnds[] =
						{
							")"
							, "const"
							, "volatile"
							, "noexcept"
							, "override"
							, "final"
							, "&"
							, "&&"
						}
					;

					bRequiresClause |= fg_IsAnyText(*mp_pTokens, Previous, c_pDeclaratorEnds) && !mp_pTokens->f_IsText(Previous, "requires");
				}
			}

			// A constructor's initializers or a pure, defaulted or deleted specifier end the clause: 'requires cA<T>' over
			// ': m_A{a}', whose braces initialize.
			if (mp_pTokens->f_IsText(Token, ":") || mp_pTokens->f_IsText(Token, "="))
				bRequiresClause = false;

			if (mp_pTokens->f_IsText(Token, "?"))
				bConditional = true;

			// A brace behind a trailing return type is the body: 'auto f() -> T {'. The
			// arrow follows the parameter list or the qualifiers behind it.
			if (mp_pTokens->f_IsText(Token, "->") && i > _iToken)
			{
				auto const &Previous = Tokens[mp_Significant[i - 1]];
				bAfterTrailingReturn
					|= bAfterCloseParen
					|| mp_pTokens->f_IsText(Previous, "const")
					|| mp_pTokens->f_IsText(Previous, "volatile")
					|| mp_pTokens->f_IsText(Previous, "noexcept")
					|| mp_pTokens->f_IsText(Previous, "override")
					|| mp_pTokens->f_IsText(Previous, "final")
					|| mp_pTokens->f_IsText(Previous, "&")
					|| mp_pTokens->f_IsText(Previous, "&&")
				;
			}

			// A label ends its statement: the body after it belongs on its own line. A
			// definition's underlying type is spelled the same way and is no label:
			// 'enum : uint32' and 'struct : CBase' name what they are built on.
			if (mp_pTokens->f_IsText(Token, ":") && !bConditional && (bLabel || i == _iToken + 1) && !bDefines)
			{
				// A width behind the ':' makes it an unnamed bit-field, 'uint32 : 3', which
				// is spelled like a label and declares one thing rather than naming a place. So
				// does a fundamental type in front of it, whatever names the width: 'unsigned : NumBits'.
				constexpr ch8 const *c_pFundamental[] =
					{
						"unsigned"
						, "signed"
						, "int"
						, "char"
						, "short"
						, "long"
						, "bool"
						, "char8_t"
						, "char16_t"
						, "char32_t"
						, "wchar_t"
					}
				;

				bool bWidth
					= !bLabel
					&& ((i + 1 < mp_Significant.f_GetLen() && Tokens[mp_Significant[i + 1]].m_Kind == ECodeTokenKind::mc_Number) || fg_IsAnyText(*mp_pTokens, First, c_pFundamental))
				;
				if (!bWidth && (bLabel || First.m_Kind == ECodeTokenKind::mc_Identifier))
				{
					fp_Finish(iNode, mp_Significant[i]);

					return i + 1;
				}
			}

			if (mp_pTokens->f_IsText(Token, ";"))
			{
				fp_Finish(iNode, mp_Significant[i]);

				return i + 1;
			}

			if (mp_pTokens->f_IsText(Token, "{"))
			{
				// A brace directly after a parameter list, or at statement position, opens a
				// block of statements. Anywhere else it is an initializer.
				// 'T x{...}' is an initializer while 'struct C {...}' is a body, and both have
				// an identifier in front of the brace. A statement terminator at the brace's
				// own level is what tells the two apart.
				// A template header stands in front of the head it declares, so the head is
				// what the statement spells behind it: 'template <typename t_C> struct TCFoo {'.
				// The builder reads such a header as an argument list only when its '<' stands
				// tight against 'template', which is why it is stepped over by its brackets.
				auto iHead = _iToken;
				while (iHead + 1 < i && mp_pTokens->f_IsText(Tokens[mp_Significant[iHead]], "template") && mp_pTokens->f_IsText(Tokens[mp_Significant[iHead + 1]], "<"))
				{
					umint nHeader = 0;
					auto iEnd = iHead + 1;
					for (; iEnd < i; ++iEnd)
					{
						auto const &Header = Tokens[mp_Significant[iEnd]];
						if (mp_pTokens->f_IsText(Header, "<"))
							++nHeader;
						else if (mp_pTokens->f_IsText(Header, ">"))
							--nHeader;
						else if (mp_pTokens->f_IsText(Header, ">>"))
							nHeader = nHeader < 2 ? 0 : nHeader - 2;

						if (!nHeader)
							break;
					}

					if (iEnd >= i)
						break;

					iHead = iEnd + 1;
				}

				// A typedef names the type its class key defines: 'typedef enum { ... } EFoo;'.
				if (iHead + 1 < i && mp_pTokens->f_IsText(Tokens[mp_Significant[iHead]], "typedef"))
					++iHead;

				// An inline or exported namespace is one all the same, 'inline namespace DVersion {', and the specifiers of the
				// variables a definition declares stand in front of its class key: 'static const struct {' over '} g_A[] ='.
				auto fSpecifier = [&](umint _iHead)
					{
						constexpr ch8 const *c_pSpecifiers[] =
							{
								"inline"
								, "export"
								, "const"
								, "volatile"
								, "static"
								, "constexpr"
								, "constinit"
								, "thread_local"
								, "extern"
								, "mutable"
							}
						;

						auto const &Head = Tokens[mp_Significant[_iHead]];
						bool bLinkage
							= mp_pTokens->f_IsText(Head, "extern") && _iHead + 1 < mp_Significant.f_GetLen() && Tokens[mp_Significant[_iHead + 1]].m_Kind == ECodeTokenKind::mc_StringLiteral
						;

						return fg_IsAnyText(*mp_pTokens, Head, c_pSpecifiers) && !bLinkage;
					}
				;

				while (iHead + 1 < i && fSpecifier(iHead))
					++iHead;

				// A brace behind an '=' initializes a variable of the type the class key names, and
				// is no body of it: 'struct timespec Time = {0};'.
				auto const &First = Tokens[mp_Significant[iHead]];
				bool bInitializer = i && mp_pTokens->f_IsText(Tokens[mp_Significant[i - 1]], "=");

				// So does one holding what no class body can, a designator or a nested list in front, or a last member with no
				// terminator: 'struct tm Time{.tm_sec = 0}'.
				constexpr ch8 const *c_pClassKeys[] = {"struct", "class", "union"};
				if (!bInitializer && fg_IsAnyText(*mp_pTokens, First, c_pClassKeys))
				{
					umint nDepth = 0;
					umint iClose = i;
					for (auto iInside = i; iInside < mp_Significant.f_GetLen(); ++iInside)
					{
						auto const &Inside = Tokens[mp_Significant[iInside]];
						if (fg_GetOpeningBracket(*mp_pTokens, Inside) != ECodeBracket::mc_None)
							++nDepth;
						else if (fg_IsClosingBracket(*mp_pTokens, Inside) && !--nDepth)
						{
							iClose = iInside;

							break;
						}
					}

					if (iClose > i + 1)
					{
						auto const &Front = Tokens[mp_Significant[i + 1]];
						auto const &Back = Tokens[mp_Significant[iClose - 1]];
						bool bMemberEnd = mp_pTokens->f_IsText(Back, ";") || mp_pTokens->f_IsText(Back, "}") || mp_pTokens->f_IsText(Back, ":");
						bInitializer = mp_pTokens->f_IsText(Front, "{") || mp_pTokens->f_IsText(Front, ".") || !bMemberEnd;
					}
				}

				bool bDefinition
					= (!bInitializer && mp_pTokens->f_IsText(First, "struct"))
					|| (!bInitializer && mp_pTokens->f_IsText(First, "class"))
					|| (!bInitializer && mp_pTokens->f_IsText(First, "union"))
					|| (!bInitializer && mp_pTokens->f_IsText(First, "enum"))
					|| mp_pTokens->f_IsText(First, "namespace")
					// A linkage specification's braces hold declarations: 'extern "C" {'.
					|| (mp_pTokens->f_IsText(First, "extern") && Tokens[mp_Significant[i - 1]].m_Kind == ECodeTokenKind::mc_StringLiteral)
				;

				// A conditional's other branch starts behind its directive with what the branch in front of it holds, which can
				// be the rest of a construct: '= ' over '#if A' over '{1};' over '#else' over '{2};'. Its brace is a list where it
				// holds no statements.
				bool bBranchStart = i == _iToken && fp_StartsAlternativeBranch(mp_Significant[i]);

				// No compound statement starts a file, so a brace there is a list where it holds no statements: a table's rows a
				// file of their own includes into its definition. A K&R definition's body follows its parameters' declarations.
				bool bFileStart = !i && mp_Nodes[mp_Nodes[iNode].m_iParent].m_Kind == ECodeNodeKind::mc_File;
				if (bFileStart && !fp_IsBlockBrace(i))
					mp_Nodes[iNode].m_bFileElements = true;

				bool bStatementBrace = i == _iToken && !fp_IsRequirementBrace(i) && !((bBranchStart || bFileStart) && !fp_IsBlockBrace(i));

				// Behind a requires clause's constraint the brace is the body it constrains, where it is no requires expression's own:
				// 'requires cA<T> && requires { T::a; }' over '{' over '}'.
				bool bConstrainedBody = false;
				if (bRequiresClause && i > _iToken)
				{
					auto iLead = i - 1;
					if (mp_pTokens->f_IsText(Tokens[mp_Significant[iLead]], ")"))
					{
						umint nDepth = 0;
						for (auto iOpen = iLead; iOpen > _iToken; --iOpen)
						{
							auto const &Bracket = Tokens[mp_Significant[iOpen]];
							if (mp_pTokens->f_IsText(Bracket, ")"))
								++nDepth;
							else if (mp_pTokens->f_IsText(Bracket, "(") && !--nDepth)
							{
								iLead = iOpen - 1;

								break;
							}
						}
					}

					bConstrainedBody = !mp_pTokens->f_IsText(Tokens[mp_Significant[iLead]], "requires");
				}

				bool bBlock = bAfterCloseParen || bAfterTrailingReturn || bStatementBrace || bDefinition || bConstrainedBody;
				if (!bBlock)
				{
					// A statement terminator at the brace's own level settles it wherever the
					// brace appears. An empty body has no terminator to find, so a brace that
					// follows a closing brace is taken as a block on its own: an initializer's
					// brace always follows a name, ')', '>' or ']'. The file's first token has nothing in front of it.
					auto fPrevious = [&](ch8 const *_pText)
						{
							return i && mp_pTokens->f_IsText(Tokens[mp_Significant[i - 1]], _pText);
						}
					;

					bBlock
						= fPrevious("}")
						|| fPrevious("else")
						|| fPrevious("do")
						|| fPrevious("try")
						|| fPrevious("const")
						|| fPrevious("volatile")
						|| fPrevious("noexcept")
						|| fPrevious("override")
						|| fPrevious("final")
						|| fPrevious("&")
						|| fPrevious("&&")
						|| fp_IsBlockBrace(i)
					;
				}

				if (!bBlock)
				{
					i = fp_BuildGroup(iNode, i, ECodeBracket::mc_Brace);
					bAfterCloseParen = false;

					continue;
				}

				auto iBlockStart = i;
				auto iBlock = fp_AddNode(ECodeNodeKind::mc_Block, iNode);
				mp_Nodes[iBlock].m_Bracket = ECodeBracket::mc_Brace;
				mp_Nodes[iBlock].m_iFirstToken = mp_Significant[i];

				auto iClose = fp_BuildBlock(iBlock, i + 1, true);
				if (iClose >= mp_Significant.f_GetLen())
				{
					mp_Nodes[iBlock].m_Kind = ECodeNodeKind::mc_Unsupported;
					fp_Finish(iBlock, mp_Significant.f_GetLast());
					fp_Finish(iNode, mp_Significant.f_GetLast());
					if (mp_bComplete)
						mp_iIncompleteOffset = Tokens[mp_Nodes[iBlock].m_iFirstToken].m_iOffset;

					mp_bComplete = false;

					return mp_Significant.f_GetLen();
				}

				fp_Finish(iBlock, mp_Significant[iClose]);
				i = iClose + 1;
				bAfterCloseParen = false;

				// A block ends the statement unless it is a lambda body, which sits inside an
				// expression, so an operator or closer after it continues the same statement.
				// A statement that is nothing but the block ends with it: a clause ends at
				// its condition, so what follows its body opens a statement of its own and
				// must not be swallowed here.
				bool bBlockIsStatement = iBlockStart == _iToken;
				if (i < mp_Significant.f_GetLen())
				{
					auto const &Next = Tokens[mp_Significant[i]];
					if (mp_pTokens->f_IsText(Next, ";"))
					{
						fp_Note(iNode, i);
						fp_Finish(iNode, mp_Significant[i]);

						return i + 1;
					}

					if (bBlockIsStatement)
					{
						fp_Finish(iNode, mp_Significant[iClose]);

						return i;
					}

					if (mp_pTokens->f_IsText(Next, "else") || mp_pTokens->f_IsText(Next, "while") || mp_pTokens->f_IsText(Next, "catch"))
						continue;

					// A type's definition goes on with the declarators it declares, up to its terminator: '} CFoo, *pFoo;'.
					bool bTypeDefinition = bDefinition && !bParameterList && !mp_pTokens->f_IsText(First, "namespace") && !mp_pTokens->f_IsText(First, "extern");
					if (bTypeDefinition && !mp_pTokens->f_IsText(Next, "{") && !mp_pTokens->f_IsText(Next, "}"))
						continue;

					// Two brackets open an attribute, which starts the next declaration rather than
					// subscripting anything: '[[nodiscard]] bool f_IsEmpty() const' behind a body.
					bool bAttribute = mp_pTokens->f_IsText(Next, "[") && i + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[i + 1]], "[");

					// A '~' is only ever unary, so behind a body it continues no expression and starts the
					// next declaration, a destructor: 'CData() {}' over '~CData() {}'.
					bool bUnary = mp_pTokens->f_IsText(Next, "~");
					if (Next.m_Kind == ECodeTokenKind::mc_Punctuator && !mp_pTokens->f_IsText(Next, "{") && !mp_pTokens->f_IsText(Next, "}") && !bAttribute && !bUnary)
						continue;
				}

				fp_Finish(iNode, mp_Significant[iClose]);

				return i;
			}

			auto Bracket = fg_GetOpeningBracket(*mp_pTokens, Token);
			if (Bracket != ECodeBracket::mc_None)
			{
				auto iOpen = i;
				if (Bracket == ECodeBracket::mc_Paren && i > _iToken)
				{
					auto const &Before = Tokens[mp_Significant[i - 1]];
					bool bAttribute
						= mp_pTokens->f_IsText(Before, "alignas")
						|| mp_pTokens->f_IsText(Before, "__attribute__")
						|| mp_pTokens->f_IsText(Before, "__attribute")
						|| mp_pTokens->f_IsText(Before, "__declspec")
					;
					bParameterList |= !bAttribute;
				}

				mp_Nodes[iNode].m_SplitPoints.f_Insert(mp_Significant[i]);
				i = fp_BuildGroup(iNode, i, Bracket);
				bAfterCloseParen = Bracket == ECodeBracket::mc_Paren;

				// A macro invoked as a statement of its own needs no terminator: its argument list
				// ends the statement where a name on the next line starts another one, or where
				// the file ends. For a name the naming does not list, the next line has to start a
				// declaration with a keyword, or be another call standing alone on its line, or what
				// follows stands behind a blank line.
				bool bAttributeNext = i + 1 < mp_Significant.f_GetLen() && mp_pTokens->f_IsText(Tokens[mp_Significant[i]], "[") && mp_pTokens->f_IsText(Tokens[mp_Significant[i + 1]], "[");
				bool bMacroStatement
					= Bracket == ECodeBracket::mc_Paren
					&& iOpen == _iToken + 1
					&& First.m_Kind == ECodeTokenKind::mc_Identifier
					&& (i >= mp_Significant.f_GetLen() || Tokens[mp_Significant[i]].m_Kind == ECodeTokenKind::mc_Identifier || bAttributeNext)
				;
				if (bMacroStatement && !mp_pTokens->f_HasRole(First, ECodeNameRole::mc_Macro))
					bMacroStatement = !f_IsEnumBody(_iParent) && (fp_IsUnlistedMacro(_iToken, i) || fp_EndsCallStatement(_iParent, _iToken, i));
				else if (bMacroStatement && i < mp_Significant.f_GetLen())
					bMacroStatement = fp_GetColumn(mp_Significant[i]) <= fp_GetColumn(mp_Significant[_iToken]);

				for (auto iGap = mp_Significant[i - 1] + 1; bMacroStatement && i < mp_Significant.f_GetLen() && iGap <= mp_Significant[i]; ++iGap)
				{
					if (iGap == mp_Significant[i])
						bMacroStatement = false;
					else if (Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline)
						break;
				}

				if (bMacroStatement)
				{
					fp_Finish(iNode, mp_Significant[i - 1]);

					return i;
				}

				// A clause owns only its condition; the statement it guards is its own.
				if (bClause && Bracket == ECodeBracket::mc_Paren)
				{
					fp_Finish(iNode, mp_Significant[i - 1]);

					return i;
				}

				continue;
			}

			if (auto iClose = fp_MatchAngleGroup(i))
			{
				i = fp_BuildGroup(iNode, i, ECodeBracket::mc_Angle, iClose);
				bAfterCloseParen = false;

				continue;
			}

			if (fg_IsClosingBracket(*mp_pTokens, Token))
			{
				// The brace of the block the statement stands in ends it, which is how a list
				// written without a terminator ends: the enumerators of an 'enum' body, or a
				// member the source left without its ';'. Any other closer has no opener of
				// its own, and the brackets do not nest as written.
				auto const &Parent = mp_Nodes[_iParent];
				bool bBlockEnd = mp_pTokens->f_IsText(Token, "}") && Parent.m_Kind == ECodeNodeKind::mc_Block && Parent.m_Bracket == ECodeBracket::mc_Brace;

				mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
				fp_Finish(iNode, mp_Significant[i]);
				if (!bBlockEnd)
				{
					if (mp_bComplete)
						mp_iIncompleteOffset = Token.m_iOffset;

					mp_bComplete = false;
				}

				return i;
			}

			bAfterCloseParen = false;
			++i;
		}

		// A file of a list's elements ends with them; anything else the file's end cuts off is not understood.
		if (!mp_Nodes[iNode].m_bFileElements)
			mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;

		fp_Finish(iNode, mp_Significant.f_GetLast());

		return mp_Significant.f_GetLen();
	}
}
