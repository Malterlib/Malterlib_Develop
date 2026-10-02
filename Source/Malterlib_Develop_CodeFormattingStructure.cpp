// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingStructure.h"

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
}

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;

	aint fg_PreviousCode(CCodeTokenStream const &_Tokens, umint _iToken);
	bool fg_IsDSLMarker(CCodeTokenStream const &_Tokens, CCodeToken const &_Token);
	bool fg_IsCast(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose);

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

	enum EGapFlag : uint8
	{
		EGapFlag_None = 0
		, EGapFlag_Comment = 1
		, EGapFlag_Directive = 2
		, EGapFlag_MultiLineToken = 4
	};

	bool fg_IsClosingBracket(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Punctuator)
			return false;

		return _Tokens.f_IsText(_Token, ")") || _Tokens.f_IsText(_Token, "]") || _Tokens.f_IsText(_Token, "}");
	}
}

namespace NMib::NDevelop
{
	bool CCodeNode::f_IsJoinable() const
	{
		return !m_bHasComment && !m_bHasDirective && !m_bHasBlock && !m_bHasMultiLineToken && !m_bHasMultiLineBrace && m_Kind != ECodeNodeKind::mc_Unsupported;
	}

	CCodeStructure::CCodeStructure(CCodeTokenStream &_Tokens)
		: mp_pTokens(&_Tokens)
	{
		fp_CollectSignificant();
		if (fp_SplitSharedAngleClosers())
			fp_CollectSignificant();

		auto const &Tokens = _Tokens.f_GetTokens();
		mp_bAngleBracket.f_SetLen(Tokens.f_GetLen());
		for (auto &Value : mp_bAngleBracket)
			Value = 0;

		auto iRoot = fp_AddNode(ECodeNodeKind::mc_File, 0);
		if (mp_Significant.f_IsEmpty())
		{
			mp_Nodes[iRoot].m_Kind = ECodeNodeKind::mc_File;
			fp_BuildIndex();

			return;
		}

		mp_Nodes[iRoot].m_iFirstToken = mp_Significant.f_GetFirst();
		auto iNext = fp_BuildBlock(iRoot, 0, false);
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
		mp_iOpeningAt.f_SetLen(nTokens);
		mp_iClosingAt.f_SetLen(nTokens);
		mp_iEnclosing.f_SetLen(nTokens);
		mp_iStatementEndingAt.f_SetLen(nTokens);
		for (umint i = 0; i < nTokens; ++i)
			mp_iOpeningAt[i] = mp_iClosingAt[i] = mp_iEnclosing[i] = mp_iStatementEndingAt[i] = nNodes;

		for (umint iNode = 0; iNode < nNodes; ++iNode)
		{
			auto const &Node = mp_Nodes[iNode];
			if (Node.m_iLastToken >= nTokens || Node.m_iFirstToken > Node.m_iLastToken)
				continue;

			if (Node.m_Kind == ECodeNodeKind::mc_Group || Node.m_Kind == ECodeNodeKind::mc_Block)
			{
				mp_iOpeningAt[Node.m_iFirstToken] = iNode;
				mp_iClosingAt[Node.m_iLastToken] = iNode;
			}
			else if (Node.m_Kind == ECodeNodeKind::mc_Statement)
				mp_iStatementEndingAt[Node.m_iLastToken] = iNode;

			for (auto i = Node.m_iFirstToken + 1; i < Node.m_iLastToken; ++i)
				mp_iEnclosing[i] = iNode;
		}
	}

	umint CCodeStructure::f_FindNodeOpeningAt(umint _iToken) const
	{
		return _iToken < mp_iOpeningAt.f_GetLen() ? mp_iOpeningAt[_iToken] : mp_Nodes.f_GetLen();
	}

	umint CCodeStructure::f_FindNodeClosingAt(umint _iToken) const
	{
		return _iToken < mp_iClosingAt.f_GetLen() ? mp_iClosingAt[_iToken] : mp_Nodes.f_GetLen();
	}

	umint CCodeStructure::f_FindEnclosingNode(umint _iToken) const
	{
		return _iToken < mp_iEnclosing.f_GetLen() ? mp_iEnclosing[_iToken] : mp_Nodes.f_GetLen();
	}

	umint CCodeStructure::f_FindStatementEndingAt(umint _iToken) const
	{
		return _iToken < mp_iStatementEndingAt.f_GetLen() ? mp_iStatementEndingAt[_iToken] : mp_Nodes.f_GetLen();
	}

	void CCodeStructure::fp_CollectSignificant()
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		mp_Significant.f_Clear();
		mp_GapFlags.f_Clear();
		uint8 Flags = EGapFlag_None;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (!fg_IsSignificant(Tokens[i].m_Kind))
			{
				if (Tokens[i].m_Kind == ECodeTokenKind::mc_LineComment || Tokens[i].m_Kind == ECodeTokenKind::mc_BlockComment)
					Flags |= EGapFlag_Comment;
				else if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
					Flags |= EGapFlag_Directive;

				continue;
			}

			if (Tokens[i].m_bMultiLine)
				Flags |= EGapFlag_MultiLineToken;

			mp_Significant.f_Insert(i);
			mp_GapFlags.f_Insert(Flags);
			Flags = EGapFlag_None;
		}
	}

	// C++ reads a '>>' that ends a template argument list as two '>' tokens. Spelling it that
	// way gives each list a closing marker of its own, so the layout can put the two markers
	// on separate lines like any other pair of nested scopes. Returns true when a token was split.
	bool CCodeStructure::fp_SplitSharedAngleClosers()
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		TCVector<umint> Splits;
		for (umint i = 0; i < mp_Significant.f_GetLen(); ++i)
		{
			auto iClose = fp_MatchAngleGroup(i);
			if (!iClose)
				continue;

			auto iToken = mp_Significant[iClose];
			if (mp_pTokens->f_IsText(Tokens[iToken], ">>") || mp_pTokens->f_IsText(Tokens[iToken], ">>="))
				Splits.f_Insert(iToken);
		}

		if (Splits.f_IsEmpty())
			return false;

		Splits.f_Sort();
		for (umint i = Splits.f_GetLen(); i; --i)
		{
			if (i < Splits.f_GetLen() && Splits[i - 1] == Splits[i])
				continue;

			mp_pTokens->f_SplitToken(Splits[i - 1], 1);
		}

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

	bool CCodeStructure::f_IsAngleBracket(umint _iToken) const
	{
		return _iToken < mp_bAngleBracket.f_GetLen() && mp_bAngleBracket[_iToken];
	}

	// Records what the trivia before a token, and the token itself, mean for joining.
	void CCodeStructure::fp_Note(umint _iNode, umint _iSignificant)
	{
		auto Flags = mp_GapFlags[_iSignificant];
		auto &Node = mp_Nodes[_iNode];
		Node.m_bHasComment |= (Flags & EGapFlag_Comment) != 0;
		Node.m_bHasDirective |= (Flags & EGapFlag_Directive) != 0;
		Node.m_bHasMultiLineToken |= (Flags & EGapFlag_MultiLineToken) != 0;
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
		// compound requirement opens its statement, which no initializer does, and holds an
		// expression rather than data.
		bool bRequirement = Node.m_iParent < mp_Nodes.f_GetLen()
			&& mp_Nodes[Node.m_iParent].m_Kind == ECodeNodeKind::mc_Statement
			&& mp_Nodes[Node.m_iParent].m_iFirstToken == Node.m_iFirstToken
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
			auto const &Source = mp_pTokens->f_GetSource();
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

			for (auto i = Node.m_iFirstToken; bOpened && i <= _iLastToken && i < Tokens.f_GetLen() && !Node.m_bHasMultiLineBrace; ++i)
			{
				auto const &Token = Tokens[i];
				for (umint iByte = Token.m_iOffset; iByte < Token.f_GetEnd(); ++iByte)
					mp_Nodes[_iNode].m_bHasMultiLineBrace |= Source.f_GetStr()[iByte] == '\n' || Source.f_GetStr()[iByte] == '\r';
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
}

namespace NMib::NDevelop
{
	// A '<' opens a template argument list only when a matching '>' exists before the
	// enclosing construct ends, and it is written tight against the name before it or that
	// name is one the naming lists as a type's or a function's. Malterlib spells comparisons
	// with spaces, so the tight spelling is a reliable discriminator where the name is not.
	umint CCodeStructure::fp_MatchAngleGroup(umint _iToken) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		if (!_iToken)
			return 0;

		auto const &Open = Tokens[mp_Significant[_iToken]];
		if (!mp_pTokens->f_IsText(Open, "<"))
			return 0;

		// A template argument list follows a name. A lambda writes its own template
		// parameter list behind the capture list, which is the other thing a '<' can follow.
		// The call operator's name ends in its own parentheses, and nothing but a template
		// argument list can follow that name: 'fCheck.template operator ()<void>("Void")'.
		auto const &Previous = Tokens[mp_Significant[_iToken - 1]];
		bool bCallOperator = _iToken >= 3
			&& mp_pTokens->f_IsText(Previous, ")")
			&& mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken - 2]], "(")
			&& mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken - 3]], "operator")
		;
		if (Previous.m_Kind != ECodeTokenKind::mc_Identifier && !mp_pTokens->f_IsText(Previous, "]") && !bCallOperator)
			return 0;

		// A template header's list can only be one, however it is spaced, and so can the
		// one behind the call operator's name, and the one behind a type or a function,
		// which is no operand of a comparison: 'TCVector< CStr >' is a list, and so is the
		// 'TCVector < CStr >' an earlier pass made of it.
		bool bHeader = mp_pTokens->f_IsText(Previous, "template")
			|| bCallOperator
			|| mp_pTokens->f_HasRole(Previous, ECodeNameRole::mc_Type)
			|| mp_pTokens->f_HasRole(Previous, ECodeNameRole::mc_Function)
		;

		// Malterlib writes a comparison with spaces and a template argument list tight
		// against its name, so a gap that is neither empty nor a line break is a comparison.
		auto fIsTightOrBroken = [&](umint _iEnd, umint _iStart)
			{
				if (_iEnd == _iStart)
					return true;

				for (umint i = _iEnd; i < _iStart; ++i)
				{
					if (mp_pTokens->f_GetSource().f_GetStr()[i] == '\n' || mp_pTokens->f_GetSource().f_GetStr()[i] == '\r')
						return true;
				}

				return false;
			}
		;
		if (!bHeader && !fIsTightOrBroken(Previous.f_GetEnd(), Open.m_iOffset))
			return 0;

		if (!bHeader && _iToken + 1 < mp_Significant.f_GetLen() && !fIsTightOrBroken(Open.f_GetEnd(), Tokens[mp_Significant[_iToken + 1]].m_iOffset))
			return 0;

		umint nDepth = 0;
		umint nBrackets = 0;
		for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				continue;

			// A template argument can be a function type, an array bound or a braced value,
			// so a balanced group inside the list is skipped rather than ending the search.
			// A brace that opens a block belongs to whatever the '<' really was, and ends it.
			if (mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "[") || (mp_pTokens->f_IsText(Token, "{") && !fp_IsBlockBrace(i)))
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

			if (mp_pTokens->f_IsText(Token, "<"))
				++nDepth;
			else if (mp_pTokens->f_IsText(Token, ">"))
			{
				if (!--nDepth)
					return i;
			}
			else if (mp_pTokens->f_IsText(Token, ">>") || mp_pTokens->f_IsText(Token, ">>="))
			{
				// Before the token is split, a '>>' closes this level and leaves the other
				// half to the list around it.
				if (nDepth < 2)
					return i;

				nDepth -= 2;
				if (!nDepth)
					return i;
			}
			else if
			(
				mp_pTokens->f_IsText(Token, ";") || mp_pTokens->f_IsText(Token, "{") || mp_pTokens->f_IsText(Token, "}")
				|| fg_IsClosingBracket(*mp_pTokens, Token)
			)
			{
				return 0;
			}
		}

		return 0;
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

	// A brace that holds statements is a block, even inside an argument list, where it is a
	// lambda body. A braced initializer holds expressions: it never has a statement
	// terminator at its own level, and no element of one can start with a keyword only a
	// statement begins with, which is what a body whose every statement is compound has
	// instead of a terminator: 'mutable { for (auto &Entry : Entries) { ... } }'.
	bool CCodeStructure::fp_IsBlockBrace(umint _iToken) const
	{
		constexpr ch8 const *c_pStatementKeywords[] =
			{
				"if", "else", "for", "while", "do", "switch", "try", "catch"
				, "return", "co_return", "break", "continue", "goto", "case", "default"
			}
		;
		auto const &Tokens = mp_pTokens->f_GetTokens();

		// A brace behind a lambda's introducer is its body whatever it holds, and an empty
		// one holds nothing to tell it by. The introducer is a capture list, or that and a
		// parameter list with 'mutable' or 'noexcept' behind it; a capture list is told
		// from a subscript by standing where no operand is in front of it.
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
		auto iBefore = aint(_iToken) - 1;
		while (iBefore >= 0 && (mp_pTokens->f_IsText(Tokens[mp_Significant[umint(iBefore)]], "mutable") || mp_pTokens->f_IsText(Tokens[mp_Significant[umint(iBefore)]], "noexcept")))
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
				bool bOperand = (Front.m_Kind == ECodeTokenKind::mc_Identifier && !mp_pTokens->f_IsText(Front, "return") && !mp_pTokens->f_IsText(Front, "co_return"))
					|| mp_pTokens->f_IsText(Front, ")")
					|| mp_pTokens->f_IsText(Front, "]")
				;
				if (!bOperand)
					return true;
			}
		}

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

	umint CCodeStructure::fp_BuildGroup(umint _iParent, umint _iToken, ECodeBracket _Bracket)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto iNode = fp_AddNode(ECodeNodeKind::mc_Group, _iParent);
		mp_Nodes[iNode].m_Bracket = _Bracket;
		mp_Nodes[iNode].m_iFirstToken = mp_Significant[_iToken];
		if (_Bracket == ECodeBracket::mc_Angle)
			mp_bAngleBracket[mp_Significant[_iToken]] = 1;
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
			if (mp_pTokens->f_IsText(Token, Closing))
			{
				if (_Bracket == ECodeBracket::mc_Angle)
					mp_bAngleBracket[mp_Significant[i]] = 1;

				fp_Finish(iNode, mp_Significant[i]);

				return i + 1;
			}

			// A top-level separator is where the canonical split form starts a new line.
			bool bSeparator = mp_pTokens->f_IsText(Token, ",")
				|| mp_pTokens->f_IsText(Token, ";")
				|| (bAsm && (mp_pTokens->f_IsText(Token, ":") || mp_pTokens->f_IsText(Token, "::")))
			;
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

			if (fp_MatchAngleGroup(i))
			{
				i = fp_BuildGroup(iNode, i, ECodeBracket::mc_Angle);

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

	umint CCodeStructure::fp_BuildStatement(umint _iParent, umint _iToken)
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		auto iNode = fp_AddNode(ECodeNodeKind::mc_Statement, _iParent);
		mp_Nodes[iNode].m_iFirstToken = mp_Significant[_iToken];
		auto i = _iToken;
		bool bAfterCloseParen = false;
		bool bAfterTrailingReturn = false;
		bool bConditional = false;
		auto const &First = Tokens[mp_Significant[_iToken]];
		bool bLabel = mp_pTokens->f_IsText(First, "case");
		bool bDefines = mp_pTokens->f_IsText(First, "enum")
			|| mp_pTokens->f_IsText(First, "struct")
			|| mp_pTokens->f_IsText(First, "class")
			|| mp_pTokens->f_IsText(First, "union")
		;
		bool bClause = mp_pTokens->f_IsText(First, "if")
			|| mp_pTokens->f_IsText(First, "for")
			|| mp_pTokens->f_IsText(First, "while")
			|| mp_pTokens->f_IsText(First, "switch")
			|| mp_pTokens->f_IsText(First, "catch")
		;
		// A keyword that only introduces the statement after it ends here.
		if (mp_pTokens->f_IsText(First, "else") || mp_pTokens->f_IsText(First, "do") || mp_pTokens->f_IsText(First, "try"))
		{
			fp_Note(iNode, _iToken);
			fp_Finish(iNode, mp_Significant[_iToken]);

			return _iToken + 1;
		}

		// A template header and a requires clause each occupy their own line. The header
		// only holds the line it is on; the declaration behind it is laid out as usual.
		if (mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken]], "template"))
			mp_Nodes[iNode].m_bTemplateHeader = true;

		while (i < mp_Significant.f_GetLen())
		{
			fp_Note(iNode, i);
			auto const &Token = Tokens[mp_Significant[i]];
			if (mp_pTokens->f_IsText(Token, "requires"))
				mp_Nodes[iNode].m_bFixedLineBreaks = true;

			if (mp_pTokens->f_IsText(Token, "?"))
				bConditional = true;

			// A brace behind a trailing return type is the body: 'auto f() -> T {'. The
			// arrow follows the parameter list or the qualifiers behind it.
			if (mp_pTokens->f_IsText(Token, "->") && i > _iToken)
			{
				auto const &Previous = Tokens[mp_Significant[i - 1]];
				bAfterTrailingReturn |= bAfterCloseParen
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
				// is spelled like a label and declares one thing rather than naming a place.
				bool bWidth = !bLabel && i + 1 < mp_Significant.f_GetLen() && Tokens[mp_Significant[i + 1]].m_Kind == ECodeTokenKind::mc_Number;
				if (!bWidth && (bLabel || Tokens[mp_Significant[_iToken]].m_Kind == ECodeTokenKind::mc_Identifier))
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
				while
				(
					iHead + 1 < i
					&& mp_pTokens->f_IsText(Tokens[mp_Significant[iHead]], "template")
					&& mp_pTokens->f_IsText(Tokens[mp_Significant[iHead + 1]], "<")
				)
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

				// A brace behind an '=' initializes a variable of the type the class key names, and
				// is no body of it: 'struct timespec Time = {0};'.
				auto const &First = Tokens[mp_Significant[iHead]];
				bool bInitializer = i && mp_pTokens->f_IsText(Tokens[mp_Significant[i - 1]], "=");
				bool bDefinition = (!bInitializer && mp_pTokens->f_IsText(First, "struct"))
					|| (!bInitializer && mp_pTokens->f_IsText(First, "class"))
					|| (!bInitializer && mp_pTokens->f_IsText(First, "union"))
					|| (!bInitializer && mp_pTokens->f_IsText(First, "enum"))
					|| mp_pTokens->f_IsText(First, "namespace")
					// A linkage specification's braces hold declarations: 'extern "C" {'.
					|| (mp_pTokens->f_IsText(First, "extern") && Tokens[mp_Significant[i - 1]].m_Kind == ECodeTokenKind::mc_StringLiteral)
				;
				bool bBlock = bAfterCloseParen || bAfterTrailingReturn || (i == _iToken && !fp_IsRequirementBrace(i)) || bDefinition;
				if (!bBlock)
				{
					// A statement terminator at the brace's own level settles it wherever the
					// brace appears. An empty body has no terminator to find, so a brace that
					// follows a closing brace is taken as a block on its own: an initializer's
					// brace always follows a name, ')', '>' or ']'.
					auto const &Previous = Tokens[mp_Significant[i - 1]];
					bBlock = mp_pTokens->f_IsText(Previous, "}")
						|| mp_pTokens->f_IsText(Previous, "else")
						|| mp_pTokens->f_IsText(Previous, "do")
						|| mp_pTokens->f_IsText(Previous, "try")
						|| mp_pTokens->f_IsText(Previous, "const")
						|| mp_pTokens->f_IsText(Previous, "noexcept")
						|| mp_pTokens->f_IsText(Previous, "override")
						|| mp_pTokens->f_IsText(Previous, "final")
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

					// Two brackets open an attribute, which starts the next declaration rather than
					// subscripting anything: '[[nodiscard]] bool f_IsEmpty() const' behind a body.
					bool bAttribute = mp_pTokens->f_IsText(Next, "[")
						&& i + 1 < mp_Significant.f_GetLen()
						&& mp_pTokens->f_IsText(Tokens[mp_Significant[i + 1]], "[")
					;
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
				mp_Nodes[iNode].m_SplitPoints.f_Insert(mp_Significant[i]);
				i = fp_BuildGroup(iNode, i, Bracket);
				bAfterCloseParen = Bracket == ECodeBracket::mc_Paren;

				// A macro invoked as a statement of its own needs no terminator: its argument list
				// ends the statement where a name on the next line starts another one, or where
				// the file ends.
				bool bMacroStatement = Bracket == ECodeBracket::mc_Paren
					&& iOpen == _iToken + 1
					&& First.m_Kind == ECodeTokenKind::mc_Identifier
					&& mp_pTokens->f_HasRole(First, ECodeNameRole::mc_Macro)
					&& (i >= mp_Significant.f_GetLen() || Tokens[mp_Significant[i]].m_Kind == ECodeTokenKind::mc_Identifier)
				;
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

			if (fp_MatchAngleGroup(i))
			{
				i = fp_BuildGroup(iNode, i, ECodeBracket::mc_Angle);
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
				bool bBlockEnd = mp_pTokens->f_IsText(Token, "}")
					&& Parent.m_Kind == ECodeNodeKind::mc_Block
					&& Parent.m_Bracket == ECodeBracket::mc_Brace
				;
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

		mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
		fp_Finish(iNode, mp_Significant.f_GetLast());

		return mp_Significant.f_GetLen();
	}
}

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;

	template <umint t_nTexts>
	bool fg_IsAnyText(CCodeTokenStream const &_Tokens, CCodeToken const &_Token, ch8 const *const (&_pTexts)[t_nTexts])
	{
		for (auto pText : _pTexts)
		{
			if (_Tokens.f_IsText(_Token, pText))
				return true;
		}

		return false;
	}

	aint fg_PreviousCode(CCodeTokenStream const &_Tokens, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		for (auto i = _iToken; i; --i)
		{
			if (fg_IsSignificant(Tokens[i - 1].m_Kind))
				return aint(i - 1);
		}

		return -1;
	}

	aint fg_NextCode(CCodeTokenStream const &_Tokens, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		for (auto i = _iToken + 1; i < Tokens.f_GetLen(); ++i)
		{
			if (fg_IsSignificant(Tokens[i].m_Kind))
				return aint(i);
		}

		return -1;
	}

	bool fg_IsDeclaratorText(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Punctuator)
			return false;

		return _Tokens.f_IsText(_Token, "*") || _Tokens.f_IsText(_Token, "&") || _Tokens.f_IsText(_Token, "&&");
	}

	// Whether the token ends the name of an operator function. The name is the 'operator'
	// keyword and what follows it: a symbol, the call operator's own parentheses, the
	// subscript operator's brackets, a literal's suffix, or the type a conversion yields,
	// which can be qualified and carry template arguments.
	bool fg_NamesOperator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto i = aint(_iToken);
		// The keyword alone ends no name: what follows it is the rest of one.
		if (_Tokens.f_IsText(Tokens[umint(i)], "operator"))
			return false;

		bool bParen = _Tokens.f_IsText(Tokens[umint(i)], ")");
		if (bParen || _Tokens.f_IsText(Tokens[umint(i)], "]"))
		{
			// The name's own brackets hold nothing; any other pair closes something else.
			auto iOpen = fg_PreviousCode(_Tokens, umint(i));
			if (iOpen < 0 || !_Tokens.f_IsText(Tokens[umint(iOpen)], bParen ? "(" : "["))
				return false;

			i = iOpen;
		}

		for (umint nSteps = 0; nSteps < 16; ++nSteps)
		{
			auto iPrevious = fg_PreviousCode(_Tokens, umint(i));
			if (iPrevious < 0)
				return false;

			auto const &Previous = Tokens[umint(iPrevious)];
			if (_Tokens.f_IsText(Previous, "operator"))
				return true;

			// Only what a name is made of stands between the keyword and the list.
			bool bName = Previous.m_Kind == ECodeTokenKind::mc_Identifier
				|| Previous.m_Kind == ECodeTokenKind::mc_StringLiteral
				|| Previous.m_Kind == ECodeTokenKind::mc_Number
				|| _Tokens.f_IsText(Previous, "::")
				|| _Tokens.f_IsText(Previous, ",")
				|| fg_IsDeclaratorText(_Tokens, Previous)
				|| _Structure.f_IsAngleBracket(umint(iPrevious))
			;
			if (!bName)
				return false;

			i = iPrevious;
		}

		return false;
	}

	// A '&' or '&&' behind a parameter list, with only the cv-qualifiers of the function
	// between, is its ref-qualifier: 'f_Get() const &noexcept'. It declares nothing, and
	// what follows it is the rest of the declaration rather than a name.
	bool fg_IsRefQualifier(CCodeTokenStream const &_Tokens, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!fg_IsDeclaratorText(_Tokens, Tokens[_iToken]))
			return false;

		auto iBefore = fg_PreviousCode(_Tokens, _iToken);
		while (iBefore >= 0 && (_Tokens.f_IsText(Tokens[umint(iBefore)], "const") || _Tokens.f_IsText(Tokens[umint(iBefore)], "volatile")))
			iBefore = fg_PreviousCode(_Tokens, umint(iBefore));

		return iBefore >= 0 && _Tokens.f_IsText(Tokens[umint(iBefore)], ")");
	}

	// Keywords that stand in front of a name or a parenthesis without declaring anything.
	ch8 const *const gc_pExpressionKeywords[] =
		{
			"if", "for", "while", "switch", "return", "co_return", "co_await", "co_yield", "throw", "new", "delete", "sizeof"
			, "alignof", "decltype", "typeid", "static_assert", "noexcept", "alignas", "case", "else", "do", "goto"
			, "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast"
		}
	;

	// The group closing at the token when it has the bracket, or -1.
	aint fg_FindGroupClosingAt(CCodeStructure const &_Structure, umint _iClose, ECodeBracket _Bracket)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto iNode = _Structure.f_FindNodeClosingAt(_iClose);
		if (iNode >= Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Group)
			return -1;

		return _Bracket == ECodeBracket::mc_None || Nodes[iNode].m_Bracket == _Bracket ? aint(iNode) : aint(-1);
	}

	// The node that most closely encloses the token, or the node count when none does.
	umint fg_FindEnclosingNode(CCodeStructure const &_Structure, umint _iToken)
	{
		return _Structure.f_FindEnclosingNode(_iToken);
	}

	// Whether the statement is a class head: 'struct', 'class' or 'union' stands at its
	// own level, behind any template header and requires clause.
	bool fg_IsClassHead(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iStatement)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Statement = Nodes[_iStatement];
		if (Statement.m_Kind != ECodeNodeKind::mc_Statement)
			return false;

		for (auto i = Statement.m_iFirstToken; i <= Statement.m_iLastToken; ++i)
		{
			bool bNested = false;
			for (auto iChild : Statement.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					if (Child.m_Kind == ECodeNodeKind::mc_Block)
						return false;

					i = Child.m_iLastToken;
					bNested = true;

					break;
				}
			}

			if (bNested)
				continue;

			if (_Tokens.f_IsText(Tokens[i], "struct") || _Tokens.f_IsText(Tokens[i], "class") || _Tokens.f_IsText(Tokens[i], "union"))
				return true;
		}

		return false;
	}

	// Whether the statement's tokens from its start through _iLast spell a type or a
	// specifier: names, qualification, template argument lists, and declarators, with
	// nothing an expression would need. An attribute is stepped over without counting,
	// and so is a template header, which the builder reads as an argument list only when
	// its '<' stands tight against 'template'.
	bool fg_SpellsType(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iStatement, umint _iLast)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Statement = Nodes[_iStatement];
		// A conditional's branch spells its own head, which starts behind the directive in front of it:
		// '#if' 'void *f_A()' '#else' 'void *f_B()' '#endif' over one body.
		auto iFirst = Statement.m_iFirstToken;
		for (auto i = _iLast; i > Statement.m_iFirstToken; --i)
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
			{
				iFirst = i + 1;

				break;
			}
		}

		bool bSpelled = false;
		for (auto i = iFirst; i <= _iLast; ++i)
		{
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
				continue;

			if (_Tokens.f_IsText(Token, "template"))
			{
				auto iOpen = fg_NextCode(_Tokens, i);
				if (iOpen >= 0 && _Tokens.f_IsText(Tokens[umint(iOpen)], "<"))
				{
					// A header declares what follows it, which is what a constructor template
					// has in front of its name in place of a return type.
					bSpelled = true;
					umint nDepth = 0;
					for (i = umint(iOpen); i <= _iLast; ++i)
					{
						if (_Tokens.f_IsText(Tokens[i], "<"))
							++nDepth;
						else if (_Tokens.f_IsText(Tokens[i], ">"))
							--nDepth;
						else if (_Tokens.f_IsText(Tokens[i], ">>"))
							nDepth = nDepth < 2 ? 0 : nDepth - 2;

						if (!nDepth)
							break;
					}
				}

				continue;
			}

			bool bNested = false;
			for (auto iChild : Statement.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					// A requires clause stands between a template header and the declaration,
					// and 'decltype' with its operand names a type: 'decltype(auto) fg_Get()'.
					auto iBeforeChild = fg_PreviousCode(_Tokens, Child.m_iFirstToken);
					bool bParen = Child.m_Bracket == ECodeBracket::mc_Paren && iBeforeChild >= 0;
					bool bRequires = bParen && _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "requires");
					// A macro the naming lists expands to the type a declarator follows, as 'decltype' does.
					bool bDecltype = bParen
						&& (_Tokens.f_IsText(Tokens[umint(iBeforeChild)], "decltype") || _Tokens.f_HasRole(Tokens[umint(iBeforeChild)], ECodeNameRole::mc_Macro))
					;
					bool bTypePart = Child.m_Bracket == ECodeBracket::mc_Angle || Child.m_Bracket == ECodeBracket::mc_Square || bRequires || bDecltype;
					if (Child.m_Kind != ECodeNodeKind::mc_Group || !bTypePart)
						return false;

					bSpelled |= Child.m_Bracket == ECodeBracket::mc_Angle || bDecltype;
					bNested = true;
					i = Child.m_iLastToken;

					break;
				}
			}

			if (bNested)
				continue;

			// A linkage specification's string is one of the declaration's specifiers: 'extern "C" void'.
			auto iExtern = Token.m_Kind == ECodeTokenKind::mc_StringLiteral ? fg_PreviousCode(_Tokens, i) : aint(-1);
			if (iExtern >= 0 && _Tokens.f_IsText(Tokens[umint(iExtern)], "extern"))
				continue;

			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				if (fg_IsAnyText(_Tokens, Token, gc_pExpressionKeywords) && !_Tokens.f_IsText(Token, "decltype"))
					return false;
			}
			else if (!fg_IsDeclaratorText(_Tokens, Token) && !_Tokens.f_IsText(Token, "::") && !_Tokens.f_IsText(Token, "~"))
				return false;

			bSpelled = true;
		}

		return bSpelled;
	}

	// Malterlib's naming says a function's name, and the sources the engine formats opt in
	// by naming that standard: a function carries one of the function prefixes, as in
	// 'fg_GetHash', and nothing else does. A type name says less than it seems to, since
	// 'TCFoo<CBindActorOptions(_Type, _Call)>' constructs a value where
	// 'TCFunction<CFoo (int _A)>' spells a function type, so only a function's name is read
	// here.
	bool fg_NamesFunction(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Tokens.f_HasRole(_Token, ECodeNameRole::mc_Function);
	}

	// Whether the name is a fundamental type's, or a type's by the project's naming.
	bool fg_NamesType(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Identifier)
			return false;

		constexpr ch8 const *c_pFundamental[] =
			{
				"bool", "char", "short", "int", "long", "unsigned", "signed", "float", "double", "void", "wchar_t", "char8_t", "char16_t", "char32_t"
			}
		;

		return fg_IsAnyText(_Tokens, _Token, c_pFundamental) || _Tokens.f_HasRole(_Token, ECodeNameRole::mc_Type);
	}

	// The name a parenthesis stands behind, read through the template argument list it may
	// end in: '::NMib::fg_GetHash<t_pMember>' is named by 'fg_GetHash'.
	bool fg_NamesCall(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iName)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto iName = aint(_iName);
		if (_Structure.f_IsAngleBracket(_iName) && _Tokens.f_IsText(Tokens[_iName], ">"))
		{
			auto iNode = fg_FindGroupClosingAt(_Structure, _iName, ECodeBracket::mc_Angle);
			iName = iNode >= 0 ? fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken) : aint(-1);
		}

		return iName >= 0 && fg_NamesFunction(_Tokens, Tokens[umint(iName)]);
	}

	// Whether the group is a parameter list: a template header's, a lambda's, a catch
	// clause's, or a function's. A function's is one when something is declared in front
	// of the name, since C++ then reads the parenthesis as a parameter list even where an
	// initializer would also parse, and otherwise when what follows the list can only
	// follow a function.
	// True when the ellipsis introduces a pack rather than expanding one, which the name
	// behind it says: 'typename ...tp_CParams' declares, 'tp_CParams...>' expands.
	bool fg_DeclaresPack(CCodeTokenStream const &_Tokens, umint _iEllipsis)
	{
		auto iAfter = fg_NextCode(_Tokens, _iEllipsis);

		return iAfter >= 0 && _Tokens.f_GetTokens()[umint(iAfter)].m_Kind == ECodeTokenKind::mc_Identifier;
	}

	// The 'operator' of a subscript operator's name, whose brackets are the only thing a
	// ']' in front of a parameter list can close besides a capture list.
	aint fg_NameSubscriptOperator(CCodeTokenStream const &_Tokens, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto iOpen = fg_PreviousCode(_Tokens, _iClose);
		if (iOpen < 0 || !_Tokens.f_IsText(Tokens[umint(iOpen)], "["))
			return -1;

		auto iOperator = fg_PreviousCode(_Tokens, umint(iOpen));

		return iOperator >= 0 && _Tokens.f_IsText(Tokens[umint(iOperator)], "operator") ? iOperator : aint(-1);
	}

	// Whether the parenthesis closing at the token is a pointer to function's declarator:
	// it opens with '*' or '&', or with a calling convention macro in front of one, names
	// what it declares, holds no separator, and a parenthesis follows it.
	bool fg_IsFunctionDeclaratorParenthesis(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto iNode = fg_FindGroupClosingAt(_Structure, _iClose, ECodeBracket::mc_Paren);
		if (iNode < 0)
			return false;

		auto const &Node = _Structure.f_GetNodes()[umint(iNode)];
		if (!Node.m_SplitPoints.f_IsEmpty())
			return false;

		// The declarator stands behind the type it returns. Behind a clause keyword or
		// another parenthesis the same spelling is a condition or a call's operand,
		// 'if (*pManager) (*pManager)->f_Destroy()'.
		auto iType = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
		if (iType < 0)
			return false;

		auto const &Type = Tokens[umint(iType)];
		bool bBehindType = (Type.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Type, gc_pExpressionKeywords))
			|| (_Structure.f_IsAngleBracket(umint(iType)) && _Tokens.f_IsText(Type, ">"))
			|| fg_IsDeclaratorText(_Tokens, Type)
		;
		if (!bBehindType)
			return false;

		auto iInner = fg_NextCode(_Tokens, Node.m_iFirstToken);
		if (iInner >= 0 && Tokens[umint(iInner)].m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Tokens[umint(iInner)], gc_pExpressionKeywords))
			iInner = fg_NextCode(_Tokens, umint(iInner));

		if (iInner < 0 || (!_Tokens.f_IsText(Tokens[umint(iInner)], "*") && !_Tokens.f_IsText(Tokens[umint(iInner)], "&")))
			return false;

		auto iBehind = fg_NextCode(_Tokens, _iClose);

		return iBehind >= 0 && _Tokens.f_IsText(Tokens[umint(iBehind)], "(");
	}

	// Whether the parenthesis is a pointer to function's declarator: a calling convention macro or
	// none, declarators, and the name or none, with the parameter list or the bound behind it:
	// '(DMibCrossmoduleAPI *m_fAlloc)(umint _Size)', '(*)(int)', '(&_Array)[4]'.
	bool fg_IsFunctionPointerDeclarator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Group = Nodes[_iGroup];
		if (Group.m_Kind != ECodeNodeKind::mc_Group || Group.m_Bracket != ECodeBracket::mc_Paren || !Group.m_SplitPoints.f_IsEmpty())
			return false;

		auto iBehind = fg_NextCode(_Tokens, Group.m_iLastToken);
		if (iBehind < 0 || (!_Tokens.f_IsText(Tokens[umint(iBehind)], "(") && !_Tokens.f_IsText(Tokens[umint(iBehind)], "[")))
			return false;

		auto i = fg_NextCode(_Tokens, Group.m_iFirstToken);
		while (i >= 0 && umint(i) < Group.m_iLastToken && Tokens[umint(i)].m_Kind == ECodeTokenKind::mc_Identifier && _Tokens.f_HasRole(Tokens[umint(i)], ECodeNameRole::mc_Macro))
			i = fg_NextCode(_Tokens, umint(i));

		// A block pointer, Clang's extension, is declared with '^' where a pointer to function has '*':
		// 'void (^fReport)(void *_pMemory)'.
		bool bDeclarator = false;
		while (i >= 0 && umint(i) < Group.m_iLastToken && (fg_IsDeclaratorText(_Tokens, Tokens[umint(i)]) || _Tokens.f_IsText(Tokens[umint(i)], "^")))
		{
			bDeclarator = true;
			i = fg_NextCode(_Tokens, umint(i));
		}

		if (!bDeclarator || i < 0)
			return false;

		if (umint(i) < Group.m_iLastToken && Tokens[umint(i)].m_Kind == ECodeTokenKind::mc_Identifier)
			i = fg_NextCode(_Tokens, umint(i));

		return i >= 0 && umint(i) == Group.m_iLastToken;
	}

	bool fg_IsParameterList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Group = Nodes[_iGroup];
		if (Group.m_Kind != ECodeNodeKind::mc_Group)
			return false;

		auto const &Tokens = _Tokens.f_GetTokens();
		auto iName = fg_PreviousCode(_Tokens, Group.m_iFirstToken);
		if (iName < 0)
			return false;

		// A template header's list declares its parameters; an argument list does not.
		if (Group.m_Bracket == ECodeBracket::mc_Angle)
			return _Tokens.f_IsText(Tokens[umint(iName)], "template");

		if (Group.m_Bracket != ECodeBracket::mc_Paren)
			return false;

		if (_Tokens.f_IsText(Tokens[umint(iName)], "catch"))
			return true;

		// Behind 'requires' a parenthesis declares a requires expression's parameters, and a
		// requires expression stands where an operand does. A requires clause's parenthesis holds
		// a constraint, however much a template header in front of it looks like a lambda's:
		// 'template <...> requires (...)'.
		if (_Tokens.f_IsText(Tokens[umint(iName)], "requires"))
		{
			constexpr ch8 const *c_pOperandLeads[] =
				{
					"=", "(", ",", "||", "&&", "!", "?", ":", "return", "requires"
				}
			;
			auto iLead = fg_PreviousCode(_Tokens, umint(iName));
			auto iBehind = fg_NextCode(_Tokens, Group.m_iLastToken);

			return iLead >= 0
				&& fg_IsAnyText(_Tokens, Tokens[umint(iLead)], c_pOperandLeads)
				&& iBehind >= 0
				&& _Tokens.f_IsText(Tokens[umint(iBehind)], "{")
			;
		}

		// 'if constexpr (...)' holds a condition, whatever follows it.
		if (_Tokens.f_IsText(Tokens[umint(iName)], "constexpr"))
			return false;

		// Directly inside a template argument list a parenthesis behind a type spells a
		// function type, whose parameters it declares: 'TCFunction<void (CFoo &&_Value)>'.
		// A template argument is as often a value the same tokens yield, by a call or a
		// construction, so a name Malterlib spells as a function's is one, and every other
		// is read by its spelling: a call hugs its parentheses where a type stands apart
		// from them.
		if (Group.m_iParent < Nodes.f_GetLen() && Nodes[Group.m_iParent].m_Kind == ECodeNodeKind::mc_Group && Nodes[Group.m_iParent].m_Bracket == ECodeBracket::mc_Angle)
		{
			auto const &Name = Tokens[umint(iName)];
			bool bApart = Name.f_GetEnd() != Tokens[Group.m_iFirstToken].m_iOffset;
			bool bType = bApart && !fg_NamesCall(_Tokens, _Structure, umint(iName));
			if (bType && _Structure.f_IsAngleBracket(umint(iName)) && _Tokens.f_IsText(Name, ">"))
				return true;

			if (bType && Name.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Name, gc_pExpressionKeywords))
				return true;
		}

		// A lambda stands anywhere an expression does; a subscript operator's name is a
		// declaration's, and what stands in front of it is read as one.
		if (_Tokens.f_IsText(Tokens[umint(iName)], "]") && fg_NameSubscriptOperator(_Tokens, umint(iName)) < 0)
			return fg_IsCaptureList(_Tokens, _Structure, umint(iName));

		// A bare name behind a lambda's capture list or template parameter list, such as an
		// attribute macro, stands in front of its parameters: '[&] mark_nodebug (int _A)'.
		auto iIntroducer = iName;
		if (Tokens[umint(iName)].m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Tokens[umint(iName)], gc_pExpressionKeywords))
		{
			auto iBehind = fg_PreviousCode(_Tokens, umint(iName));
			bool bBehindList = iBehind >= 0
				&& (_Tokens.f_IsText(Tokens[umint(iBehind)], "]") || (_Structure.f_IsAngleBracket(umint(iBehind)) && _Tokens.f_IsText(Tokens[umint(iBehind)], ">")))
			;
			if (bBehindList)
				iIntroducer = iBehind;
		}

		if (iIntroducer != iName && _Tokens.f_IsText(Tokens[umint(iIntroducer)], "]") && fg_NameSubscriptOperator(_Tokens, umint(iIntroducer)) < 0)
		{
			if (fg_IsCaptureList(_Tokens, _Structure, umint(iIntroducer)))
				return true;
		}

		// A lambda's own template parameter list stands between its capture list and its
		// parameters: '[&]<typename ...tfp_C>(tfp_C ...p_Params)'.
		if (_Structure.f_IsAngleBracket(umint(iIntroducer)) && _Tokens.f_IsText(Tokens[umint(iIntroducer)], ">"))
		{
			auto iNode = fg_FindGroupClosingAt(_Structure, umint(iIntroducer), ECodeBracket::mc_Angle);
			if (iNode >= 0)
			{
				auto iCapture = fg_PreviousCode(_Tokens, Nodes[umint(iNode)].m_iFirstToken);
				bool bCaptures = iCapture >= 0 && _Tokens.f_IsText(Tokens[umint(iCapture)], "]") && fg_NameSubscriptOperator(_Tokens, umint(iCapture)) < 0;
				if (bCaptures && fg_IsCaptureList(_Tokens, _Structure, umint(iCapture)))
					return true;
			}
		}

		// A function's parameter list is the first parenthesis of its statement; the ones
		// after it belong to a constructor's initializers or to expressions.
		auto const &Parent = Nodes[Group.m_iParent];
		if (Parent.m_Kind != ECodeNodeKind::mc_Statement)
		{
			// A pointer to function declared as a parameter has its own list behind its declarator:
			// 'void f(bool (*_fCall)(void *_pContext))'.
			if (!_Tokens.f_IsText(Tokens[umint(iName)], ")") || !fg_IsParameterList(_Tokens, _Structure, Group.m_iParent))
				return false;

			auto iDeclarator = _Structure.f_FindNodeClosingAt(umint(iName));

			return iDeclarator < Nodes.f_GetLen() && Nodes[iDeclarator].m_iParent == Group.m_iParent && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator);
		}

		for (auto iChild : Parent.m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_Bracket != ECodeBracket::mc_Paren)
				continue;

			// The parenthesis of 'decltype(auto)' or 'sizeof(x)' in front of the name is an
			// operand, not the list, the empty one of 'operator ()' is the name, and a
			// requires clause's holds a constraint.
			auto iBeforeChild = fg_PreviousCode(_Tokens, Child.m_iFirstToken);
			bool bOperand = iBeforeChild >= 0
				&& (fg_IsAnyText(_Tokens, Tokens[umint(iBeforeChild)], gc_pExpressionKeywords)
					|| _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "operator")
					|| _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "requires")
					|| _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "__attribute__")
					|| _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "__declspec"))
			;
			if (iChild != _iGroup && bOperand)
				continue;

			// A pointer to function's declarator stands in front of the list: 'void (*pCall)(int _A)'.
			if (iChild != _iGroup && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iChild))
				continue;

			if (iChild != _iGroup)
				return false;

			break;
		}

		// An operator function's name ends in front of its parameter list, whatever the name
		// is spelled with, and such a function has one wherever it is declared.
		if (fg_NamesOperator(_Tokens, _Structure, umint(iName)))
			return true;

		// A name that ends in a template argument list starts in front of that list.
		if (_Structure.f_IsAngleBracket(umint(iName)) && _Tokens.f_IsText(Tokens[umint(iName)], ">"))
		{
			auto iNode = fg_FindGroupClosingAt(_Structure, umint(iName), ECodeBracket::mc_Angle);
			iName = iNode >= 0 ? fg_PreviousCode(_Tokens, Nodes[umint(iNode)].m_iFirstToken) : aint(-1);
			if (iName < 0)
				return false;
		}

		if (_Tokens.f_IsText(Tokens[umint(iName)], "]"))
		{
			auto iOperator = fg_NameSubscriptOperator(_Tokens, umint(iName));
			if (iOperator < 0)
				return fg_IsCaptureList(_Tokens, _Structure, umint(iName));

			iName = iOperator;
		}
		// The parenthesis behind a pointer to function's declarator holds its parameters:
		// 'void (*pCall)(int _A)', 'void (DMibCrossmoduleAPI *m_fFree)(void *_pMemory)'.
		else if (_Tokens.f_IsText(Tokens[umint(iName)], ")") && fg_IsFunctionDeclaratorParenthesis(_Tokens, _Structure, umint(iName)))
			return true;
		// An operator function is named by the keyword and its symbol, and the call
		// operator by the keyword and its own parentheses: 'operator () ('.
		else if (Tokens[umint(iName)].m_Kind != ECodeTokenKind::mc_Identifier)
		{
			auto iOperator = fg_PreviousCode(_Tokens, umint(iName));
			if (iOperator >= 0 && _Tokens.f_IsText(Tokens[umint(iName)], ")") && _Tokens.f_IsText(Tokens[umint(iOperator)], "("))
				iOperator = fg_PreviousCode(_Tokens, umint(iOperator));

			if (iOperator < 0 || !_Tokens.f_IsText(Tokens[umint(iOperator)], "operator"))
				return false;

			iName = iOperator;
		}
		else if (fg_IsAnyText(_Tokens, Tokens[umint(iName)], gc_pExpressionKeywords))
		{
			// 'operator co_await', 'operator new' and 'operator delete' are named by the
			// keyword an expression uses, and are declarations for all that.
			auto iOperator = fg_PreviousCode(_Tokens, umint(iName));
			if (iOperator < 0 || !_Tokens.f_IsText(Tokens[umint(iOperator)], "operator"))
				return false;

			iName = iOperator;
		}

		// What the statement spells in front of the name is a type or a specifier when it
		// is made of names, qualification, template argument lists, and declarators. The
		// name's own qualification is part of the name, and what stands in front of it
		// is what is read: 'NMemory::fg_MemMove(...)' has nothing there and is a call.
		auto iBefore = fg_PreviousCode(_Tokens, umint(iName));
		while (iBefore >= 0 && _Tokens.f_IsText(Tokens[umint(iBefore)], "::"))
		{
			auto iScope = fg_PreviousCode(_Tokens, umint(iBefore));
			if (iScope >= 0 && _Structure.f_IsAngleBracket(umint(iScope)) && _Tokens.f_IsText(Tokens[umint(iScope)], ">"))
			{
				auto iScopeGroup = fg_FindGroupClosingAt(_Structure, umint(iScope), ECodeBracket::mc_Angle);
				aint iOpen = iScopeGroup >= 0 ? aint(Nodes[umint(iScopeGroup)].m_iFirstToken) : aint(-1);

				iScope = iOpen >= 0 ? fg_PreviousCode(_Tokens, umint(iOpen)) : aint(-1);
			}

			if (iScope < 0 || Tokens[umint(iScope)].m_Kind != ECodeTokenKind::mc_Identifier)
			{
				iBefore = iScope;

				break;
			}

			iBefore = fg_PreviousCode(_Tokens, umint(iScope));
		}

		if (iBefore >= 0 && iBefore < aint(Parent.m_iFirstToken))
			iBefore = -1;

		// In a function's body a type in front of the name defines a variable, whatever
		// the parenthesis holds: 'TCUniquePointer<CFoo> pFoo(fg_Construct())'.
		bool bFunctionBody = Parent.m_iParent < Nodes.f_GetLen() && fg_IsFunctionBody(_Tokens, _Structure, Parent.m_iParent);
		if (iBefore >= 0 && fg_SpellsType(_Tokens, _Structure, Group.m_iParent, umint(iBefore)))
			return !bFunctionBody;

		// Behind a bare name only what follows the list can tell a constructor from a
		// call: a body, an initializer list, a qualifier, or a defaulted or deleted
		// definition. A ternary's ':' follows a call, so the initializer list counts only
		// when the name opens the statement or is qualified. In a class body there are no
		// calls, so a bare name declares whatever follows.
		auto iAfter = fg_NextCode(_Tokens, Group.m_iLastToken);
		if (iAfter < 0)
			return false;

		if (Parent.m_iParent < Nodes.f_GetLen())
		{
			auto const &Body = Nodes[Parent.m_iParent];
			if (Body.m_Kind == ECodeNodeKind::mc_Block && Body.m_iParent < Nodes.f_GetLen())
			{
				if (fg_IsClassHead(_Tokens, _Structure, Body.m_iParent) && iBefore < 0)
					return true;
			}
		}

		// A deduction guide names its template twice, in front of the parenthesis and behind
		// the arrow, which no call followed by a member access does: 'TCFoo(int) -> TCFoo<int>'.
		if (_Tokens.f_IsText(Tokens[umint(iAfter)], "->") && Tokens[umint(iName)].m_Kind == ECodeTokenKind::mc_Identifier)
		{
			auto iTarget = fg_NextCode(_Tokens, umint(iAfter));
			bool bOpens = iBefore < 0 || _Tokens.f_IsText(Tokens[umint(iBefore)], "explicit");
			if (bOpens && iTarget >= 0 && _Tokens.f_HasSameText(Tokens[umint(iTarget)], Tokens[umint(iName)]))
				return true;
		}

		auto const &After = Tokens[umint(iAfter)];
		constexpr ch8 const *c_pFunctionTails[] =
			{
				"{", "const", "volatile", "noexcept", "override", "final", "mutable", "requires"
			}
		;
		if (fg_IsAnyText(_Tokens, After, c_pFunctionTails))
			return true;

		if (_Tokens.f_IsText(After, ":"))
			return iBefore < 0 || _Tokens.f_IsText(Tokens[umint(iBefore)], "::");

		if (_Tokens.f_IsText(After, "="))
		{
			auto iValue = fg_NextCode(_Tokens, umint(iAfter));
			if (iValue < 0)
				return false;

			auto const &Value = Tokens[umint(iValue)];

			return _Tokens.f_IsText(Value, "0") || _Tokens.f_IsText(Value, "default") || _Tokens.f_IsText(Value, "delete");
		}

		return false;
	}

	// A colon answers a conditional's '?' when one stands in front of it at the level of
	// what encloses both; every other colon ends a label or introduces something.
	bool fg_IsConditionalColon(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iColon)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Nodes = _Structure.f_GetNodes();
		auto iNode = fg_FindEnclosingNode(_Structure, _iColon);
		if (iNode == Nodes.f_GetLen())
			return false;

		auto const &Node = Nodes[iNode];
		for (auto i = Node.m_iFirstToken; i < _iColon; ++i)
		{
			for (auto iChild : Node.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					i = Child.m_iLastToken;

					break;
				}
			}

			if (i < _iColon && _Tokens.f_IsText(Tokens[i], "?"))
				return true;
		}

		return false;
	}

	// A ':' at a declaration's own level gives the width of a bit-field, 'uint8 m_Flags:2'.
	// What stands in front of it is the name a type declares, which no other ':' has: a
	// label ends its statement at the ':', a base clause follows a definition's keyword, an
	// initializer list follows a parameter list, and a conditional's answers a '?'.
	bool fg_IsBitFieldColon(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iColon)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Nodes = _Structure.f_GetNodes();
		auto iNode = fg_FindEnclosingNode(_Structure, _iColon);
		if (iNode == Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Statement)
			return false;

		auto const &Statement = Nodes[iNode];
		auto const &First = Tokens[Statement.m_iFirstToken];
		constexpr ch8 const *c_pOther[] =
			{
				"case", "default", "public", "private", "protected", "struct", "class", "union", "enum"
				, "namespace", "template", "using", "friend", "operator"
			}
		;
		if (fg_IsAnyText(_Tokens, First, c_pOther) || fg_IsAnyText(_Tokens, First, gc_pExpressionKeywords))
			return false;

		auto iName = fg_PreviousCode(_Tokens, _iColon);
		if (iName < 0 || Tokens[umint(iName)].m_Kind != ECodeTokenKind::mc_Identifier)
			return false;

		// Without a name in front of it the type itself stands there, and only the width
		// behind the ':' tells the declaration from a label.
		if (umint(iName) <= Statement.m_iFirstToken)
		{
			auto iWidth = fg_NextCode(_Tokens, _iColon);
			if (iWidth < 0 || Tokens[umint(iWidth)].m_Kind != ECodeTokenKind::mc_Number)
				return false;
		}

		// A parameter list in front of the ':' makes it the one that opens an initializer
		// list, whatever stands between the two: 'C(int _A) noexcept : m_A(_A)'.
		for (auto iChild : Statement.m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_Bracket == ECodeBracket::mc_Paren && Child.m_iLastToken < _iColon)
				return false;
		}

		for (auto i = Statement.m_iFirstToken; i < _iColon; ++i)
		{
			for (auto iChild : Statement.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					i = Child.m_iLastToken;

					break;
				}
			}

			if (i < _iColon && _Tokens.f_IsText(Tokens[i], "?"))
				return false;
		}

		return true;
	}

	// A DSL marker, '_o', '_j' or '_' in Malterlib's naming, marks a key or an array rather
	// than naming something declared.
	bool fg_IsDSLMarker(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Tokens.f_HasRole(_Token, ECodeNameRole::mc_DSLMarker);
	}

	// A closing brace ends an operand where it closes a lambda's body inside an expression:
	// the statement the body stands in goes on behind it, 'g_Dispatch / [] { ... } + g_Other'.
	bool fg_ClosesExpressionBody(CCodeStructure const &_Structure, umint _iBrace)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto iNode = _Structure.f_FindNodeClosingAt(_iBrace);
		if (iNode >= Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Block)
			return false;

		auto const &Node = Nodes[iNode];
		if (Node.m_iParent >= Nodes.f_GetLen())
			return false;

		// One inside a group is a lambda's body wherever it stands.
		auto const &Parent = Nodes[Node.m_iParent];
		if (Parent.m_Kind == ECodeNodeKind::mc_Group)
			return true;

		return Parent.m_Kind == ECodeNodeKind::mc_Statement && Parent.m_iLastToken > _iBrace;
	}

	// A parenthesis that holds nothing but fundamental type words, and stands where no call
	// or subscript could have produced it, is a cast: '(smint)-1', '(unsigned int)x'. The
	// same parenthesis behind 'sizeof' is the operator's operand, '(sizeof(int) - 1)'.
	bool fg_ClosesFundamentalCast(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		constexpr ch8 const *c_pFundamental[] =
			{
				"void", "bool", "char", "short", "int", "long", "unsigned", "signed", "float", "double", "const", "volatile"
			}
		;
		auto iNode = fg_FindGroupClosingAt(_Structure, _iClose, ECodeBracket::mc_Paren);
		if (iNode < 0)
			return false;

		auto const &Node = _Structure.f_GetNodes()[umint(iNode)];
		auto iBefore = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
		if (iBefore >= 0)
		{
			auto const &Before = Tokens[umint(iBefore)];
			bool bCalled = (Before.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords))
				|| _Tokens.f_IsText(Before, "sizeof")
				|| _Tokens.f_IsText(Before, "alignof")
				|| _Tokens.f_IsText(Before, ")")
				|| _Tokens.f_IsText(Before, "]")
				|| _Structure.f_IsAngleBracket(umint(iBefore))
			;
			if (bCalled)
				return false;
		}

		bool bType = false;
		for (auto i = Node.m_iFirstToken + 1; i < Node.m_iLastToken; ++i)
		{
			if (!fg_IsSignificant(Tokens[i].m_Kind))
				continue;

			if (!fg_IsAnyText(_Tokens, Tokens[i], c_pFundamental) && !_Tokens.f_HasRole(Tokens[i], ECodeNameRole::mc_Type))
				return false;

			bType = true;
		}

		return bType;
	}

	// Whether the parenthesis closing at the token spells a type and nothing else: it ends
	// in a declarator or a qualifier, '(CFoo *)', or holds one name Malterlib spells as a
	// type's, '(aint)'.
	bool fg_IsCastParenthesis(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto iInner = fg_PreviousCode(_Tokens, _iClose);
		if (iInner < 0)
			return false;

		// A parenthesis behind a name, a call's arguments or a subscript is an operator's
		// operand, 'sizeof(void *)', or a call, never a cast.
		auto iNode = fg_FindGroupClosingAt(_Structure, _iClose, ECodeBracket::mc_Paren);
		if (iNode < 0)
			return false;

		aint iOpen = aint(_Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);

		auto iBeforeOpen = fg_PreviousCode(_Tokens, umint(iOpen));
		if (iBeforeOpen >= 0)
		{
			// A cast converts what another cast yields: '(umint)(void *)*pBuffer'.
			auto const &BeforeOpen = Tokens[umint(iBeforeOpen)];
			bool bOperand = BeforeOpen.m_Kind == ECodeTokenKind::mc_Identifier
				|| (_Tokens.f_IsText(BeforeOpen, ")") && !fg_IsCast(_Tokens, _Structure, umint(iBeforeOpen)))
				|| _Tokens.f_IsText(BeforeOpen, "]")
				|| (_Structure.f_IsAngleBracket(umint(iBeforeOpen)) && _Tokens.f_IsText(BeforeOpen, ">"))
			;
			if (bOperand)
				return false;
		}

		auto const &Inner = Tokens[umint(iInner)];
		if (fg_IsDeclaratorText(_Tokens, Inner) || _Tokens.f_IsText(Inner, "const") || _Tokens.f_IsText(Inner, "volatile"))
			return !fg_ClosesParameterList(_Tokens, _Structure, _iClose);

		return fg_PreviousCode(_Tokens, umint(iInner)) == iOpen && fg_NamesType(_Tokens, Inner);
	}

	// Whether the parenthesis closing at the token is a cast by either reading.
	bool fg_IsCast(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		return fg_ClosesFundamentalCast(_Tokens, _Structure, _iClose) || fg_IsCastParenthesis(_Tokens, _Structure, _iClose);
	}

	// A postfix '++' or '--' ends the operand it stands behind: '*pParse++ - '0''.
	bool fg_IsPostfixStep(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!_Tokens.f_IsText(Tokens[_iToken], "++") && !_Tokens.f_IsText(Tokens[_iToken], "--"))
			return false;

		auto iBefore = fg_PreviousCode(_Tokens, _iToken);
		if (iBefore < 0)
			return false;

		auto const &Before = Tokens[umint(iBefore)];

		return (Before.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords))
			|| _Tokens.f_IsText(Before, ")")
			|| _Tokens.f_IsText(Before, "]")
			|| (_Structure.f_IsAngleBracket(umint(iBefore)) && _Tokens.f_IsText(Before, ">"))
		;
	}

	// An operator's spelling says what it does only where an operand stands on both sides
	// of it. Without one in front it is the unary form, '-1' and '*pValue'; without one
	// behind it names something else, a cast's '(CFoo *)' or a pack's '&& ...'. '*', '&'
	// and '&&' are ambiguous even in that position, since a name in front of one can be a
	// type as easily as an operand, and behind a parameter list the same token qualifies
	// the function: 'f_Get() const &'.
	bool fg_IsInfixOperator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Token = Tokens[_iToken];
		if (Token.m_Kind != ECodeTokenKind::mc_Punctuator || _Structure.f_IsAngleBracket(_iToken))
			return false;

		constexpr ch8 const *c_pOperators[] =
			{
				"*", "/", "%", "+", "-", "<<", ">>", "<", ">", "<=", ">=", "<=>", "==", "!=", "&", "^", "|", "&&", "||"
			}
		;
		if (!fg_IsAnyText(_Tokens, Token, c_pOperators))
			return false;

		auto iBefore = fg_PreviousCode(_Tokens, _iToken);
		auto iAfter = fg_NextCode(_Tokens, _iToken);
		if (iBefore < 0 || iAfter < 0)
			return false;

		// Behind 'operator' the token spells a function's name, not an operation.
		auto const &Before = Tokens[umint(iBefore)];
		if (_Tokens.f_IsText(Before, "operator"))
			return false;

		bool bOperand = Before.m_Kind == ECodeTokenKind::mc_Number
			|| Before.m_Kind == ECodeTokenKind::mc_StringLiteral
			|| Before.m_Kind == ECodeTokenKind::mc_CharLiteral
			|| (Before.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords))
			|| _Tokens.f_IsText(Before, ")")
			|| _Tokens.f_IsText(Before, "]")
			|| (_Structure.f_IsAngleBracket(umint(iBefore)) && _Tokens.f_IsText(Before, ">"))
			|| (_Tokens.f_IsText(Before, "}") && fg_ClosesExpressionBody(_Structure, umint(iBefore)))
			|| fg_IsPostfixStep(_Tokens, _Structure, umint(iBefore))
		;
		if (!bOperand)
			return false;

		auto const &After = Tokens[umint(iAfter)];
		constexpr ch8 const *c_pCloses[] =
			{
				")", "]", "}", ",", ";", "..."
			}
		;
		if (fg_IsAnyText(_Tokens, After, c_pCloses))
			return false;

		// A cast is no operand of its own: what follows it is the unary form, '(aint)-1'. A
		// cast holds fundamental type words, ends in a declarator or a qualifier, or holds
		// one name Malterlib spells as a type's.
		if (_Tokens.f_IsText(Before, ")") && fg_IsCast(_Tokens, _Structure, umint(iBefore)))
			return false;

		if (!fg_IsDeclaratorText(_Tokens, Token))
			return true;

		// Behind an alias declaration's '=' stands a type, whose declarators are no operators:
		// 'using FDuplicate = void *(void const *_pImpl)'.
		{
			auto iStatement = fg_FindEnclosingNode(_Structure, _iToken);
			auto const &AliasNodes = _Structure.f_GetNodes();
			if (iStatement < AliasNodes.f_GetLen() && AliasNodes[iStatement].m_Kind == ECodeNodeKind::mc_Statement)
			{
				auto iUsing = AliasNodes[iStatement].m_iFirstToken;
				auto iAlias = fg_NextCode(_Tokens, iUsing);
				auto iAssign = iAlias >= 0 ? fg_NextCode(_Tokens, umint(iAlias)) : aint(-1);
				bool bAlias = _Tokens.f_IsText(Tokens[iUsing], "using")
					&& iAlias >= 0
					&& Tokens[umint(iAlias)].m_Kind == ECodeTokenKind::mc_Identifier
					&& iAssign >= 0
					&& _Tokens.f_IsText(Tokens[umint(iAssign)], "=")
					&& umint(iAssign) < _iToken
				;
				if (bAlias)
					return false;
			}
		}

		// Behind a declarator stands a name, never a literal or an operator spelled as a
		// keyword, so an operand of that kind settles the reading: 'nMove * sizeof(t_CKey)'.
		constexpr ch8 const *c_pValueOperands[] =
			{
				"sizeof", "alignof", "this", "nullptr", "true", "false"
			}
		;
		// Behind a closing parenthesis the rules below tell a call from a condition or a cast.
		bool bValueOperand = !_Tokens.f_IsText(Before, ")") && !_Tokens.f_IsText(Before, "]") && (After.m_Kind == ECodeTokenKind::mc_Number
			|| After.m_Kind == ECodeTokenKind::mc_StringLiteral
			|| After.m_Kind == ECodeTokenKind::mc_CharLiteral
			|| fg_IsAnyText(_Tokens, After, c_pValueOperands))
		;
		if (bValueOperand)
			return true;

		if (fg_IsDeclaratorToken(_Tokens, _Structure, _iToken))
			return false;

		// What follows a ref-qualifier is the rest of the declaration: the trailing return
		// type, the body, a pure specifier, a requires clause or another qualifier.
		constexpr ch8 const *c_pTails[] =
			{
				"->", "{", "=", "requires", "const", "volatile", "noexcept", "override", "final", "&", "&&"
			}
		;
		if (fg_IsAnyText(_Tokens, After, c_pTails))
			return false;

		// What is left is the spelling C++ itself cannot tell apart, 'C(CStr &_A)' against
		// 'C(a & b)', so the reading is settled only where a declaration cannot stand: behind
		// a literal, behind the '=' that ends the declarator part of what the token stands
		// in, or in a condition, which declares nothing without an '=' of its own.
		if (Before.m_Kind == ECodeTokenKind::mc_Number || Before.m_Kind == ECodeTokenKind::mc_StringLiteral || Before.m_Kind == ECodeTokenKind::mc_CharLiteral)
			return true;

		// Nor behind an operator, which a declaration never starts behind: 'Sum - m_Count*(a + b)',
		// 'Value += a & b'. The left operand's qualified name is stepped over to find what stands in front.
		if (Before.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			auto iLead = fg_PreviousCode(_Tokens, umint(iBefore));
			while (iLead >= 0 && _Tokens.f_IsText(Tokens[umint(iLead)], "::"))
			{
				auto iScope = fg_PreviousCode(_Tokens, umint(iLead));
				if (iScope < 0 || Tokens[umint(iScope)].m_Kind != ECodeTokenKind::mc_Identifier)
					break;

				iLead = fg_PreviousCode(_Tokens, umint(iScope));
			}

			constexpr ch8 const *c_pOperators[] =
				{
					"+", "-", "/", "%", "|", "^", "<<", ">>", "==", "!=", "<=", ">=", "<=>", "&&", "||", "?"
					, "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
				}
			;
			if (iLead >= 0 && !_Structure.f_IsAngleBracket(umint(iLead)) && fg_IsAnyText(_Tokens, Tokens[umint(iLead)], c_pOperators))
				return true;
		}

		// Nor does a declaration stand in front of what only an operand starts with: 'Value & ~Mask'.
		bool bOperandAfter = _Tokens.f_IsText(After, "~")
			|| _Tokens.f_IsText(After, "!")
			|| After.m_Kind == ECodeTokenKind::mc_Number
			|| After.m_Kind == ECodeTokenKind::mc_CharLiteral
		;
		if (bOperandAfter)
			return true;

		auto const &Nodes = _Structure.f_GetNodes();

		// A ')' closes a cast as well as a call, and '(CFoo)*pValue' spells the tokens of a
		// multiplication, so only a call or a subscript in front of the token settles it. An
		// operator spelled like a call yields a value the same way, apart from 'decltype',
		// which names a type: 'decltype(m_Value) *pValue'.
		if (_Tokens.f_IsText(Before, ")") || _Tokens.f_IsText(Before, "]"))
		{
			constexpr ch8 const *c_pValueOperators[] =
				{
					"sizeof", "alignof", "typeid", "noexcept"
				}
			;
			auto iGroup = fg_FindGroupClosingAt(_Structure, umint(iBefore), ECodeBracket::mc_None);
			if (iGroup < 0)
				return false;

			auto iName = fg_PreviousCode(_Tokens, Nodes[umint(iGroup)].m_iFirstToken);
			if (iName < 0)
				return false;

			auto const &Name = Tokens[umint(iName)];
			if (Name.m_Kind == ECodeTokenKind::mc_Identifier)
				return !fg_IsAnyText(_Tokens, Name, gc_pExpressionKeywords) || fg_IsAnyText(_Tokens, Name, c_pValueOperators);

			// A parenthesis opening behind another, an '=' or an operator groups an expression, a cast
			// having been told apart above: '((a + b) & ~Mask)'.
			constexpr ch8 const *c_pExpressionLeads[] =
				{
					"(", "=", ",", "+", "-", "*", "/", "%", "|", "^", "&&", "||", "?", ":", "<<", ">>", "==", "!=", "<", ">", "<=", ">="
				}
			;
			// One holding a name and nothing more converts as readily as it groups: '(task_info_t)&Info'.
			if (_Tokens.f_IsText(Before, ")") && fg_IsAnyText(_Tokens, Name, c_pExpressionLeads) && !_Structure.f_IsAngleBracket(umint(iName)))
			{
				bool bExpression = false;
				for (auto i = Nodes[umint(iGroup)].m_iFirstToken + 1; i < umint(iBefore) && !bExpression; ++i)
				{
					auto const &Inner = Tokens[i];
					bExpression = Inner.m_Kind == ECodeTokenKind::mc_Number
						|| (Inner.m_Kind == ECodeTokenKind::mc_Punctuator && !_Tokens.f_IsText(Inner, "::") && !_Structure.f_IsAngleBracket(i))
					;
				}

				if (bExpression)
					return true;
			}

			return _Tokens.f_IsText(Name, ")") || _Tokens.f_IsText(Name, "]");
		}

		auto iNode = fg_FindEnclosingNode(_Structure, _iToken);
		if (iNode == Nodes.f_GetLen())
			return false;

		auto const &Node = Nodes[iNode];
		if (Node.m_Kind == ECodeNodeKind::mc_Statement && fg_IsAnyText(_Tokens, Tokens[Node.m_iFirstToken], gc_pExpressionKeywords))
			return true;

		bool bCondition = false;
		if (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Paren)
		{
			auto iClause = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
			// A requires clause's parenthesis holds a constraint, which declares nothing.
			if (iClause >= 0 && _Tokens.f_IsText(Tokens[umint(iClause)], "requires"))
				return !fg_IsParameterList(_Tokens, _Structure, iNode);

			if (iClause >= 0 && _Tokens.f_IsText(Tokens[umint(iClause)], "constexpr"))
				iClause = fg_PreviousCode(_Tokens, umint(iClause));

			bCondition = iClause >= 0
				&& (_Tokens.f_IsText(Tokens[umint(iClause)], "if")
					|| _Tokens.f_IsText(Tokens[umint(iClause)], "while")
					|| _Tokens.f_IsText(Tokens[umint(iClause)], "switch"))
			;
		}

		bool bAssigned = false;
		auto iEnd = bCondition ? Node.m_iLastToken : _iToken;
		for (auto i = Node.m_iFirstToken; i < iEnd; ++i)
		{
			for (auto iChild : Node.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					i = Child.m_iLastToken;

					break;
				}
			}

			if (i >= iEnd)
				break;

			// A separator starts the next declarator: 'int *pA = f(), *pB;'.
			if (!bCondition && (_Tokens.f_IsText(Tokens[i], ",") || _Tokens.f_IsText(Tokens[i], ";")))
				bAssigned = false;
			else if (_Tokens.f_IsText(Tokens[i], "="))
				bAssigned = true;
		}

		return bCondition ? !bAssigned : bAssigned;
	}
}

namespace NMib::NDevelop
{
	// Whether the block holds a function's statements rather than a class's, a
	// namespace's or an enumeration's members: it belongs to a function, a lambda, a
	// control clause or a bare block. There a name behind a type in front of a parenthesis
	// defines a variable, since no function is ever declared inside another.
	bool fg_IsFunctionBody(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iBlock)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Tokens = _Tokens.f_GetTokens();
		if (Nodes[_iBlock].m_Kind != ECodeNodeKind::mc_Block || Nodes[_iBlock].m_iParent >= Nodes.f_GetLen())
			return false;

		auto iOwner = Nodes[_iBlock].m_iParent;
		auto const &Owner = Nodes[iOwner];
		if (Owner.m_Kind != ECodeNodeKind::mc_Statement || fg_IsClassHead(_Tokens, _Structure, iOwner))
			return false;

		auto const &First = Tokens[Owner.m_iFirstToken];

		return !_Tokens.f_IsText(First, "namespace") && !_Tokens.f_IsText(First, "extern") && !_Tokens.f_IsText(First, "enum");
	}

	// A capture list stands where an operand cannot: a subscript follows a name, a call, a
	// template argument list, another subscript, or a literal.
	// Whether an element of a parenthesis can only be an argument, which a parameter
	// list never holds: one that opens with a function's name, a literal, a keyword that
	// is a value, a unary operator or a brace. 'TCSet<int> Set(fg_Construct(&Allocator))'
	// constructs a variable, where the same tokens with types in the parenthesis would
	// declare a function.
	bool fg_HoldsArguments(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Group = Nodes[_iGroup];
		auto const &Tokens = _Tokens.f_GetTokens();
		constexpr ch8 const *c_pValues[] =
			{
				"this", "nullptr", "true", "false"
			}
		;
		constexpr ch8 const *c_pUnary[] =
			{
				"&", "*", "!", "-", "+", "~", "{"
			}
		;
		bool bElementStart = true;
		for (auto i = Group.m_iFirstToken + 1; i < Group.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
				continue;

			if (bElementStart)
			{
				bool bArgument = Token.m_Kind == ECodeTokenKind::mc_Number
					|| Token.m_Kind == ECodeTokenKind::mc_StringLiteral
					|| Token.m_Kind == ECodeTokenKind::mc_CharLiteral
					|| fg_IsAnyText(_Tokens, Token, c_pValues)
					|| fg_IsAnyText(_Tokens, Token, c_pUnary)
					|| fg_NamesFunction(_Tokens, Token)
				;
				if (bArgument)
					return true;

				bElementStart = false;
			}

			bool bNested = false;
			for (auto iChild : Group.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					i = Child.m_iLastToken;
					bNested = true;

					break;
				}
			}

			if (!bNested && _Tokens.f_IsText(Token, ","))
				bElementStart = true;
		}

		return false;
	}

	bool fg_IsCaptureList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto iNode = fg_FindGroupClosingAt(_Structure, _iClose, ECodeBracket::mc_Square);
		if (iNode < 0)
			return false;

		auto const &Node = _Structure.f_GetNodes()[umint(iNode)];
		// Two brackets open an attribute, which captures nothing: '[[maybe_unused]]'. One
		// behind a capture list is the lambda's, and ends its introducer the way the list
		// does: '[] [[nodiscard]] () -> int'.
		auto iInner = fg_NextCode(_Tokens, Node.m_iFirstToken);
		if (iInner >= 0 && _Tokens.f_IsText(Tokens[umint(iInner)], "["))
		{
			auto iBeforeAttribute = fg_PreviousCode(_Tokens, Node.m_iFirstToken);

			return iBeforeAttribute >= 0 && _Tokens.f_IsText(Tokens[umint(iBeforeAttribute)], "]") && fg_IsCaptureList(_Tokens, _Structure, umint(iBeforeAttribute));
		}

		auto iBefore = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
		if (iBefore < 0)
			return true;

		// The brackets of 'delete []' say what is deleted and capture nothing, and those of
		// 'operator new[]' name the function.
		auto const &Before = Tokens[umint(iBefore)];
		if (Before.m_Kind == ECodeTokenKind::mc_Identifier)
			return fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords) && !_Tokens.f_IsText(Before, "delete") && !_Tokens.f_IsText(Before, "new");

		if (Before.m_Kind != ECodeTokenKind::mc_Punctuator)
			return false;

		return !_Tokens.f_IsText(Before, ")") && !_Tokens.f_IsText(Before, "]") && !_Structure.f_IsAngleBracket(umint(iBefore));
	}

	// An arrow introduces a trailing return type when a parameter list, or a function's
	// qualifiers behind one, stands in front of it; behind a call's arguments it is a
	// member access: 'fg_Get()->f_Call()'.
	bool fg_IsTrailingReturnArrow(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iArrow)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!_Tokens.f_IsText(Tokens[_iArrow], "->"))
			return false;

		constexpr ch8 const *c_pQualifiers[] =
			{
				"const", "volatile", "noexcept", "override", "final", "mutable", "&", "&&"
			}
		;
		// A 'noexcept' may take its condition in a parenthesis of its own, which is stepped
		// over with it: ') const noexcept(noexcept(fg_A())) -> COrdering'.
		auto fSkipNoexceptCondition = [&](aint _iToken) -> aint
			{
				if (_iToken < 0 || !_Tokens.f_IsText(Tokens[umint(_iToken)], ")"))
					return _iToken;

				auto iNode = fg_FindGroupClosingAt(_Structure, umint(_iToken), ECodeBracket::mc_Paren);
				if (iNode < 0)
					return _iToken;

				auto iKeyword = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);
				if (iKeyword >= 0 && _Tokens.f_IsText(Tokens[umint(iKeyword)], "noexcept"))
					return iKeyword;

				return _iToken;
			}
		;
		// The arrow behind a compound requirement names the concept its expression satisfies:
		// '{ _fOnEntry(_Key) } -> cFoo'.
		auto iRequirement = fg_PreviousCode(_Tokens, _iArrow);
		if (iRequirement >= 0 && _Tokens.f_IsText(Tokens[umint(iRequirement)], "}"))
		{
			return fg_FindGroupClosingAt(_Structure, umint(iRequirement), ECodeBracket::mc_Brace) >= 0;
		}

		auto iBefore = fSkipNoexceptCondition(fg_PreviousCode(_Tokens, _iArrow));
		while (iBefore >= 0 && fg_IsAnyText(_Tokens, Tokens[umint(iBefore)], c_pQualifiers))
			iBefore = fSkipNoexceptCondition(fg_PreviousCode(_Tokens, umint(iBefore)));

		if (iBefore < 0)
			return false;

		// A lambda that takes nothing may leave its parameter list out, and then the arrow
		// stands behind the capture list, or behind the attribute macro that follows one:
		// '[pState] -> TCFuture<void>'.
		auto iCapture = iBefore;
		if (Tokens[umint(iCapture)].m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Tokens[umint(iCapture)], gc_pExpressionKeywords))
			iCapture = fg_PreviousCode(_Tokens, umint(iCapture));

		if (iCapture >= 0 && _Tokens.f_IsText(Tokens[umint(iCapture)], "]"))
			return fg_NameSubscriptOperator(_Tokens, umint(iCapture)) < 0 && fg_IsCaptureList(_Tokens, _Structure, umint(iCapture));

		// An attribute macro can stand behind a parameter list as well, where no name has a
		// place of its own: '(umint _nChars) inline_always_lambda -> CChar *'.
		if (iCapture >= 0 && iCapture != iBefore && _Tokens.f_IsText(Tokens[umint(iCapture)], ")") && fg_ClosesParameterList(_Tokens, _Structure, umint(iCapture)))
			return true;

		if (!_Tokens.f_IsText(Tokens[umint(iBefore)], ")"))
			return false;

		if (fg_ClosesParameterList(_Tokens, _Structure, umint(iBefore)))
			return true;

		// A macro that opens a function, named as a macro by the project's naming, is
		// followed by the return type and the body the way a parameter list is:
		// 'DMibTestSuite("Name") -> TCFuture<void> {'. A member access behind a macro's
		// result, 'DEPTR(p)->m_Value', never reaches a body through a type alone.
		auto iMacroGroup = fg_FindGroupClosingAt(_Structure, umint(iBefore), ECodeBracket::mc_Paren);
		if (iMacroGroup < 0)
			return false;

		auto iName = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iMacroGroup)].m_iFirstToken);
		if (iName < 0 || Tokens[umint(iName)].m_Kind != ECodeTokenKind::mc_Identifier)
			return false;

		if (!_Tokens.f_HasRole(Tokens[umint(iName)], ECodeNameRole::mc_Macro))
			return false;

		umint nAngle = 0;
		for (auto iType = fg_NextCode(_Tokens, _iArrow); iType >= 0; iType = fg_NextCode(_Tokens, umint(iType)))
		{
			auto const &Type = Tokens[umint(iType)];
			if (_Structure.f_IsAngleBracket(umint(iType)))
			{
				if (_Tokens.f_IsText(Type, "<"))
					++nAngle;
				else if (nAngle)
					--nAngle;

				continue;
			}

			if (nAngle)
				continue;

			if (_Tokens.f_IsText(Type, "{"))
				return true;

			bool bTypePart = (Type.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Type, gc_pExpressionKeywords))
				|| _Tokens.f_IsText(Type, "::")
				|| fg_IsDeclaratorText(_Tokens, Type)
			;
			if (!bTypePart)
				return false;
		}

		return false;
	}

	bool fg_ClosesParameterList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto iNode = fg_FindGroupClosingAt(_Structure, _iClose, ECodeBracket::mc_Paren);

		return iNode >= 0 && fg_IsParameterList(_Tokens, _Structure, umint(iNode));
	}

	// A '*', '&' or '&&' declares a pointer or reference where only a type can stand in
	// front of it: behind 'const', 'volatile', or another declarator; behind a name or a
	// template argument list when nothing that could be an operand follows it, or when
	// it stands in a parameter list, outside a default argument. Elsewhere the same
	// token is an operator, or has no settled reading: 'TCFoo<T> &&_Other' and
	// 'cFoo<T> && cBar<T>' spell the same tokens.
	// Whether the declarator token in a clause's parenthesis declares the clause's variable:
	// 'for (auto &&Element : Range)', 'if (CFoo &&Value = fg_Get())'. A type stands in front of
	// it and a name behind it that a ':' or an '=' follows, which no operand of '&&' or '&' has.
	bool fg_DeclaresInClause(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Group = Nodes[_iGroup];
		if (Group.m_Kind != ECodeNodeKind::mc_Group || Group.m_Bracket != ECodeBracket::mc_Paren)
			return false;

		auto iKeyword = fg_PreviousCode(_Tokens, Group.m_iFirstToken);
		constexpr ch8 const *c_pClauses[] = {"for", "if", "while", "switch"};
		if (iKeyword < 0 || !fg_IsAnyText(_Tokens, Tokens[umint(iKeyword)], c_pClauses))
			return false;

		bool bType = false;
		umint iChild = 0;
		for (auto i = Group.m_iFirstToken + 1; i < _iToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
				continue;

			while (iChild < Group.m_Children.f_GetLen() && Nodes[Group.m_Children[iChild]].m_iLastToken < i)
				++iChild;

			if (iChild < Group.m_Children.f_GetLen() && Nodes[Group.m_Children[iChild]].m_iFirstToken <= i)
			{
				if (Nodes[Group.m_Children[iChild]].m_Bracket != ECodeBracket::mc_Angle)
					return false;

				i = Nodes[Group.m_Children[iChild]].m_iLastToken;

				continue;
			}

			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				if (fg_IsAnyText(_Tokens, Token, gc_pExpressionKeywords))
					return false;

				bType = true;
			}
			else if (!fg_IsDeclaratorText(_Tokens, Token) && !_Tokens.f_IsText(Token, "::"))
				return false;
		}

		if (!bType)
			return false;

		auto iName = fg_NextCode(_Tokens, _iToken);
		while (iName >= 0 && fg_IsDeclaratorText(_Tokens, Tokens[umint(iName)]))
			iName = fg_NextCode(_Tokens, umint(iName));

		if (iName < 0)
			return false;

		auto iAfter = aint(-1);
		if (Tokens[umint(iName)].m_Kind == ECodeTokenKind::mc_Identifier)
			iAfter = fg_NextCode(_Tokens, umint(iName));
		else if (_Tokens.f_IsText(Tokens[umint(iName)], "["))
		{
			auto iBinding = _Structure.f_FindNodeOpeningAt(umint(iName));
			if (iBinding < Nodes.f_GetLen())
				iAfter = fg_NextCode(_Tokens, Nodes[iBinding].m_iLastToken);
		}

		return iAfter >= 0 && (_Tokens.f_IsText(Tokens[umint(iAfter)], ":") || _Tokens.f_IsText(Tokens[umint(iAfter)], "="));
	}

	bool fg_IsDeclaratorToken(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!fg_IsDeclaratorText(_Tokens, Tokens[_iToken]))
			return false;

		auto iPrevious = fg_PreviousCode(_Tokens, _iToken);
		if (iPrevious < 0)
			return false;

		auto const &Previous = Tokens[umint(iPrevious)];
		if (_Tokens.f_IsText(Previous, "const") || _Tokens.f_IsText(Previous, "volatile"))
			return !fg_IsRefQualifier(_Tokens, _iToken);

		if (fg_IsDeclaratorText(_Tokens, Previous))
			return !_Tokens.f_IsText(Previous, "&&") && fg_IsDeclaratorToken(_Tokens, _Structure, umint(iPrevious));

		bool bBehindTemplate = _Structure.f_IsAngleBracket(umint(iPrevious)) && _Tokens.f_IsText(Previous, ">");
		// 'decltype' and its operand name a type the way a template argument list ends one.
		if (_Tokens.f_IsText(Previous, ")"))
		{
			auto iNode = fg_FindGroupClosingAt(_Structure, umint(iPrevious), ECodeBracket::mc_None);
			if (iNode >= 0)
			{
				// So does a macro the naming lists, whose expansion is a type where a declarator follows it.
				auto iKeyword = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);
				bBehindTemplate = iKeyword >= 0
					&& (_Tokens.f_IsText(Tokens[umint(iKeyword)], "decltype") || _Tokens.f_HasRole(Tokens[umint(iKeyword)], ECodeNameRole::mc_Macro))
				;
			}
		}

		// A trailing return type is a type, so a declarator in it declares: '-> void *'.
		{
			auto iBack = iPrevious;
			while (iBack >= 0)
			{
				auto const &Back = Tokens[umint(iBack)];
				if (_Structure.f_IsAngleBracket(umint(iBack)) && _Tokens.f_IsText(Back, ">"))
				{
					auto iArguments = fg_FindGroupClosingAt(_Structure, umint(iBack), ECodeBracket::mc_Angle);
					if (iArguments < 0)
						break;

					iBack = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iArguments)].m_iFirstToken);

					continue;
				}

				bool bTypePart = (Back.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Back, gc_pExpressionKeywords))
					|| _Tokens.f_IsText(Back, "::")
					|| fg_IsDeclaratorText(_Tokens, Back)
				;
				if (!bTypePart)
					break;

				iBack = fg_PreviousCode(_Tokens, umint(iBack));
			}

			if (iBack >= 0 && umint(iBack) != umint(iPrevious) && _Tokens.f_IsText(Tokens[umint(iBack)], "->") && fg_IsTrailingReturnArrow(_Tokens, _Structure, umint(iBack)))
				return true;
		}

		// The first declarator of a pointer to function's declarator opens it: 'int (*g_fAccept)(int)'.
		if (_Tokens.f_IsText(Previous, "("))
		{
			auto iDeclarator = _Structure.f_FindNodeOpeningAt(umint(iPrevious));

			return iDeclarator < _Structure.f_GetNodes().f_GetLen() && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator);
		}

		if (!bBehindTemplate && Previous.m_Kind != ECodeTokenKind::mc_Identifier)
			return false;

		// The declarators behind this one are part of the same declarator: 'CFoo **' and
		// 'CFoo * &' end where the last of them does.
		auto iNext = fg_NextCode(_Tokens, _iToken);
		while (iNext >= 0 && fg_IsDeclaratorText(_Tokens, Tokens[umint(iNext)]))
			iNext = fg_NextCode(_Tokens, umint(iNext));

		if (iNext >= 0)
		{
			auto const &Next = Tokens[umint(iNext)];
			// A cv-qualifier is never an operand, so a declarator qualified by one declares:
			// '(char * const *)'.
			bool bUnnamed = _Tokens.f_IsText(Next, ",") || _Tokens.f_IsText(Next, ")") || _Tokens.f_IsText(Next, "...") || _Tokens.f_IsText(Next, "=")
				|| _Tokens.f_IsText(Next, "const") || _Tokens.f_IsText(Next, "volatile")
				|| (_Structure.f_IsAngleBracket(umint(iNext)) && _Tokens.f_IsText(Next, ">"))
			;
			if (bUnnamed)
				return true;
		}

		auto iGroup = fg_FindEnclosingNode(_Structure, _iToken);
		auto const &Nodes = _Structure.f_GetNodes();
		if (iGroup == Nodes.f_GetLen())
			return false;

		// At the statement's own level the token declares when everything in front of it
		// spells a type: 'CFoo &&operator ()', 'TCActor<t_C> &f_Get()'.
		if (Nodes[iGroup].m_Kind == ECodeNodeKind::mc_Statement)
			return fg_SpellsType(_Tokens, _Structure, iGroup, umint(iPrevious));

		if (fg_DeclaresInClause(_Tokens, _Structure, iGroup, _iToken))
			return true;

		if (fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iGroup))
			return true;

		if (!fg_IsParameterList(_Tokens, _Structure, iGroup))
			return false;

		// A default argument is an expression, so an '=' earlier in the same parameter
		// makes the token an operator.
		auto const &Group = Nodes[iGroup];
		auto iStart = Group.m_iFirstToken;
		for (auto iSplit : Group.m_SplitPoints)
		{
			if (iSplit < _iToken)
				iStart = iSplit;
		}

		// The children stand in order, so each is stepped over once.
		umint iChild = 0;
		for (auto i = iStart + 1; i < _iToken; ++i)
		{
			while (iChild < Group.m_Children.f_GetLen() && Nodes[Group.m_Children[iChild]].m_iLastToken < i)
				++iChild;

			if (iChild < Group.m_Children.f_GetLen() && Nodes[Group.m_Children[iChild]].m_iFirstToken <= i)
			{
				i = Nodes[Group.m_Children[iChild]].m_iLastToken;

				continue;
			}

			if (_Tokens.f_IsText(Tokens[i], "="))
				return false;
		}

		return true;
	}
}

namespace NMib::NDevelop
{
	// Decides the inline separator between two adjacent significant tokens. Only spellings
	// the standard settles are decided; everything else keeps whatever the source has, which
	// is what stops a rewrite from guessing at an ambiguous construct.
	ECodeSpacing fg_GetCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Left = Tokens[_iLeft];
		auto const &Right = Tokens[_iRight];
		auto fLeft = [&](ch8 const *_pText)
			{
				return _Tokens.f_IsText(Left, _pText);
			}
		;
		auto fRight = [&](ch8 const *_pText)
			{
				return _Tokens.f_IsText(Right, _pText);
			}
		;

		// An operator function is named by the keyword and what follows it, and that name
		// stands apart from both: 'operator + (', 'operator () ('. The rules below read the
		// name's own tokens as the operators and scopes they are spelled with.
		if (fLeft("operator"))
			return ECodeSpacing::mc_Space;

		if (fRight("(") && fg_NamesOperator(_Tokens, _Structure, _iLeft))
			return ECodeSpacing::mc_Space;

		// An asm statement's section separators stand apart from its operands, and the ones of
		// empty sections together: '"" :: "r"(_pFirst) : "memory"', '"yield" ::: "memory"'.
		{
			bool bLeftSeparator = fg_IsAsmSectionSeparator(_Tokens, _Structure, _iLeft);
			bool bRightSeparator = fg_IsAsmSectionSeparator(_Tokens, _Structure, _iRight);
			if (bLeftSeparator && bRightSeparator)
				return ECodeSpacing::mc_None;

			if (bLeftSeparator || bRightSeparator)
				return ECodeSpacing::mc_Space;
		}

		// A resolved template bracket hugs its arguments and separates the list from the
		// declarator after it. An unresolved '<' or '>' is an ordinary binary operator.
		if (_Structure.f_IsAngleBracket(_iRight))
		{
			// A template header is the exception: 'template <typename t_CType>'.
			if (fLeft("template"))
				return ECodeSpacing::mc_Space;

			return ECodeSpacing::mc_None;
		}

		if (_Structure.f_IsAngleBracket(_iLeft))
		{
			if (fLeft("<"))
				return ECodeSpacing::mc_None;

			if (Right.m_Kind == ECodeTokenKind::mc_Identifier)
				return ECodeSpacing::mc_Space;

			// A pack declared behind the list is separated from it and one expanded is
			// written tight: 'TCDecay<tp_CParams> ...p_Params' against 'tp_CParams...>'.
			if (fRight("..."))
				return fg_DeclaresPack(_Tokens, _iRight) ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;

			// A declarator behind the list is separated from it: 'TCVector<int> &'.
			if (fg_IsDeclaratorText(_Tokens, Right))
				return ECodeSpacing::mc_Space;

			// A parameter list after a template argument's type spells a function type,
			// which the standard separates: TCActorFunctor<TCFuture<void> (CStr _Host)>.
			// A template argument is as often a value the same tokens yield, so a name
			// Malterlib spells as a function's is read as the call it is, and every other
			// keeps its spelling. Anywhere else a parenthesis behind an argument list is a
			// call or a construction.
			if (fRight("("))
			{
				auto const &Nodes = _Structure.f_GetNodes();
				auto iNode = fg_FindGroupClosingAt(_Structure, _iLeft, ECodeBracket::mc_Angle);
				if (iNode < 0 || Nodes[Nodes[umint(iNode)].m_iParent].m_Bracket != ECodeBracket::mc_Angle)
					return ECodeSpacing::mc_None;

				bool bType = Left.f_GetEnd() != Right.m_iOffset && !fg_NamesCall(_Tokens, _Structure, _iLeft);

				return bType ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;
			}

			if (fRight("::") || fRight(",") || fRight(";") || fRight(")") || fRight("[") || fRight("]"))
				return ECodeSpacing::mc_None;

			// Behind a whole argument list these take it for their left operand, or give a
			// default to what it names: 'cHas<t_C> ? a : b', 'typename t_C = TCFoo<int>'. A
			// colon there as often starts a specialization's base clause, on a line of its own.
			constexpr ch8 const *c_pInfix[] =
				{
					"?", "=", "==", "!=", "<=", ">=", "<=>", "||", "|", "^", "+", "-", "/", "%"
				}
			;
			if (fg_IsAnyText(_Tokens, Right, c_pInfix))
				return ECodeSpacing::mc_Space;

			return ECodeSpacing::mc_Preserve;
		}

		// '!' and '~' are always unary and hug their operand: '!(a && b)', '!!x'. Behind
		// 'operator' they name a function instead, and keep that spelling.
		if (fLeft("!") || fLeft("~"))
		{
			bool bOperatorName = false;
			for (auto i = _iLeft; i; --i)
			{
				auto const &Token = _Tokens.f_GetTokens()[i - 1];
				if (Token.m_Kind == ECodeTokenKind::mc_Whitespace || Token.m_Kind == ECodeTokenKind::mc_Newline || Token.m_Kind == ECodeTokenKind::mc_LineSplice)
					continue;

				bOperatorName = _Tokens.f_IsText(Token, "operator");

				break;
			}

			if (!bOperatorName)
				return ECodeSpacing::mc_None;
		}

		// A compound requirement's braces stand apart from the expression they hold, where an
		// initializer's hug their elements: '{ _fOnEntry(_Key) } -> cFoo', 'CFoo{1, 2}'. A
		// requirement is a brace group that opens a statement, which no initializer does.
		auto fIsRequirement = [&](umint _iBrace)
			{
				auto const &Nodes = _Structure.f_GetNodes();
				auto iNode = _Structure.f_FindNodeOpeningAt(_iBrace);
				if (iNode >= Nodes.f_GetLen())
					iNode = _Structure.f_FindNodeClosingAt(_iBrace);

				if (iNode >= Nodes.f_GetLen())
					return false;

				auto const &Node = Nodes[iNode];
				if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Brace || Node.m_iParent >= Nodes.f_GetLen())
					return false;

				auto const &Parent = Nodes[Node.m_iParent];

				return Parent.m_Kind == ECodeNodeKind::mc_Statement && Parent.m_iFirstToken == Node.m_iFirstToken;
			}
		;
		if ((fLeft("{") && !fRight("}") && fIsRequirement(_iLeft)) || (fRight("}") && !fLeft("{") && fIsRequirement(_iRight)))
			return ECodeSpacing::mc_Space;

		// Scope markers hug their contents, including a separator that ends the last element.
		if (fLeft("(") || fLeft("[") || fLeft("{"))
			return ECodeSpacing::mc_None;

		if (fRight(")") || fRight("]") || fRight("}"))
			return ECodeSpacing::mc_None;

		// Separators bind tightly to what they follow and loosely to what follows them.
		if (fRight(",") || fRight(";"))
			return ECodeSpacing::mc_None;

		if (fLeft(",") || fLeft(";"))
			return ECodeSpacing::mc_Space;

		// An unresolved '<' or '>' is a comparison, written apart. The separators above
		// come first, since a macro passes an operator as a bare argument: 'DMibExpect(a, <, b)'.
		if (fLeft("<") || fRight(">") || fLeft(">") || fRight("<"))
			return ECodeSpacing::mc_Space;

		// A '/' written tight between two names is a path in a macro argument, such as a
		// log category, and keeps that spelling.
		if (fLeft("/") || fRight("/"))
		{
			auto iSlash = fLeft("/") ? _iLeft : _iRight;
			auto iBefore = fg_PreviousCode(_Tokens, iSlash);
			auto iAfter = fg_NextCode(_Tokens, iSlash);
			bool bTight = iBefore >= 0 && iAfter >= 0
				&& Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_Identifier
				&& Tokens[umint(iAfter)].m_Kind == ECodeTokenKind::mc_Identifier
				&& Tokens[umint(iBefore)].f_GetEnd() == Tokens[iSlash].m_iOffset
				&& Tokens[iSlash].f_GetEnd() == Tokens[umint(iAfter)].m_iOffset
			;
			if (bTight)
				return ECodeSpacing::mc_Preserve;
		}

		// A DSL marker the naming lists hugs what it marks: the '=' that makes a key of the
		// literal in front of it, '"Names"_o= 5', and the brackets of an array, '_o[1, 2]'.
		// A marker with no key in front of it spells an object, and its '=' hugs the brace
		// as well: '_o={"Key"_o= 5}'.
		if (Left.m_Kind == ECodeTokenKind::mc_Identifier && fg_IsDSLMarker(_Tokens, Left) && (fRight("=") || fRight("[")))
			return ECodeSpacing::mc_None;

		if (fLeft("=") && fRight("{"))
		{
			auto iMarker = fg_PreviousCode(_Tokens, _iLeft);
			auto iKey = iMarker >= 0 ? fg_PreviousCode(_Tokens, umint(iMarker)) : aint(-1);
			bool bMarker = iMarker >= 0 && Tokens[umint(iMarker)].m_Kind == ECodeTokenKind::mc_Identifier && fg_IsDSLMarker(_Tokens, Tokens[umint(iMarker)]);
			bool bKeyed = iKey >= 0
				&& Tokens[umint(iKey)].m_Kind == ECodeTokenKind::mc_StringLiteral
				&& Tokens[umint(iKey)].f_GetEnd() == Tokens[umint(iMarker)].m_iOffset
			;
			if (bMarker && !bKeyed)
				return ECodeSpacing::mc_None;
		}

		// Plain '=' assigns and initializes, and is written apart from both sides. A capture
		// default is settled by the markers around it, and an operator function's name keeps
		// whatever spelling it has.
		if (fLeft("=") || fRight("="))
			return ECodeSpacing::mc_Space;

		// An operator in an infix position is written apart from both of its operands,
		// whatever they are spelled with: 'nFlags & mc_Mask', '5 * 5', 'a * (b + c)'. The
		// rules below read a parenthesis, a name or a declarator beside the operator as
		// part of some other construct, so this stands in front of them.
		if (fg_IsInfixOperator(_Tokens, _Structure, _iLeft) || fg_IsInfixOperator(_Tokens, _Structure, _iRight))
			return ECodeSpacing::mc_Space;

		// A subscript on what a call or another subscript yields hugs it, as one behind a
		// name does: 'f_Get()[0]', 'm_Rows[0][1]'. An attribute opens with two brackets and
		// stands apart from the parenthesis or the capture list in front of it:
		// 'if (bFlag) [[unlikely]]', '[] [[nodiscard]] ()'.
		if (fRight("[") && (fLeft(")") || fLeft("]")))
		{
			auto iInner = fg_NextCode(_Tokens, _iRight);
			if (iInner < 0 || !_Tokens.f_IsText(Tokens[umint(iInner)], "["))
				return ECodeSpacing::mc_None;

			return ECodeSpacing::mc_Space;
		}

		// A conditional's '?' and ':' stand apart from both of their operands, a
		// parenthesised one included: 'bFlag ? (a + b) : (c + d)'.
		if (fLeft("?") || fRight("?"))
			return ECodeSpacing::mc_Space;

		if ((fLeft(":") && fg_IsConditionalColon(_Tokens, _Structure, _iLeft)) || (fRight(":") && fg_IsConditionalColon(_Tokens, _Structure, _iRight)))
			return ECodeSpacing::mc_Space;

		// A keyword is separated from a parenthesis that follows it; a call name is not.
		// Operators spelled like a call, such as sizeof and decltype, stay tight.
		if (fRight("("))
		{
			constexpr ch8 const *c_pSpacedKeywords[] =
				{
					"if", "for", "while", "switch", "catch", "return", "co_return", "co_await", "co_yield"
					, "throw", "delete", "case", "requires", "constexpr"
				}
			;
			for (auto pKeyword : c_pSpacedKeywords)
			{
				if (_Tokens.f_IsText(Left, pKeyword))
					return ECodeSpacing::mc_Space;
			}

			// A block pointer's declarator stands apart from the type in front of it, which a '^' opening a
			// parenthesis, never an operand of the operator, says: 'void (^fReport)(void *_pMemory)'.
			if (Left.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Left, gc_pExpressionKeywords))
			{
				auto iCaret = fg_NextCode(_Tokens, _iRight);
				auto iDeclarator = _Structure.f_FindNodeOpeningAt(_iRight);
				bool bBlock = iCaret >= 0
					&& _Tokens.f_IsText(Tokens[umint(iCaret)], "^")
					&& iDeclarator < _Structure.f_GetNodes().f_GetLen()
					&& fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator)
				;
				if (bBlock)
					return ECodeSpacing::mc_Space;
			}

			// A function type's pointer return type hugs its parameter list the way it hugs a pointer to
			// function's declarator, where a function type stands: in an alias declaration or a template
			// argument list, behind a type's name. 'using FDuplicate = void *(void const *_pImpl)'.
			// Behind a cast it is unary and hugs its operand: '(int)*(pF)'.
			if (fLeft("*") || fLeft("&"))
			{
				auto iCast = fg_PreviousCode(_Tokens, _iLeft);
				if (iCast >= 0 && _Tokens.f_IsText(Tokens[umint(iCast)], ")") && fg_IsCast(_Tokens, _Structure, umint(iCast)))
					return ECodeSpacing::mc_None;
			}

			if ((fLeft("*") || fLeft("&")) && !fg_ClosesParameterList(_Tokens, _Structure, _iLeft))
			{
				auto iType = fg_PreviousCode(_Tokens, _iLeft);
				while (iType >= 0 && (fg_IsDeclaratorText(_Tokens, Tokens[umint(iType)]) || _Tokens.f_IsText(Tokens[umint(iType)], "const")))
					iType = fg_PreviousCode(_Tokens, umint(iType));

				bool bType = iType >= 0 && fg_NamesType(_Tokens, Tokens[umint(iType)]);
				bool bFunctionTypePlace = false;
				if (bType)
				{
					auto iGroup = fg_FindEnclosingNode(_Structure, _iLeft);
					auto const &Nodes = _Structure.f_GetNodes();
					if (iGroup < Nodes.f_GetLen() && Nodes[iGroup].m_Kind == ECodeNodeKind::mc_Group && Nodes[iGroup].m_Bracket == ECodeBracket::mc_Angle)
						bFunctionTypePlace = true;
					else if (iGroup < Nodes.f_GetLen() && Nodes[iGroup].m_Kind == ECodeNodeKind::mc_Statement)
					{
						auto iUsing = Nodes[iGroup].m_iFirstToken;
						auto iAlias = fg_NextCode(_Tokens, iUsing);
						auto iAssign = iAlias >= 0 ? fg_NextCode(_Tokens, umint(iAlias)) : aint(-1);
						bFunctionTypePlace = _Tokens.f_IsText(Tokens[iUsing], "using") && iAssign >= 0 && _Tokens.f_IsText(Tokens[umint(iAssign)], "=");
					}
				}

				if (bFunctionTypePlace)
					return ECodeSpacing::mc_None;
			}

			// A pointer return type's declarator hugs the pointer to function's declarator behind it:
			// 'void *(DMibCrossmoduleAPI *m_fAlloc)(umint _Size)'.
			if (fg_IsDeclaratorText(_Tokens, Left) && fg_IsDeclaratorToken(_Tokens, _Structure, _iLeft))
			{
				auto iDeclarator = _Structure.f_FindNodeOpeningAt(_iRight);
				if (iDeclarator < _Structure.f_GetNodes().f_GetLen() && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator))
					return ECodeSpacing::mc_None;
			}

			// A lambda's attribute behind its capture list stands apart from the parameter list
			// behind it, as an attribute macro does: '[] [[nodiscard]] (int _Value)'.
			if (fLeft("]") && fg_IsCaptureList(_Tokens, _Structure, _iLeft))
			{
				auto iNode = fg_FindGroupClosingAt(_Structure, _iLeft, ECodeBracket::mc_Square);
				auto iInner = iNode >= 0 ? fg_NextCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken) : aint(-1);
				if (iInner >= 0 && _Tokens.f_IsText(Tokens[umint(iInner)], "["))
					return ECodeSpacing::mc_Space;
			}

			// A bare name behind a capture list, such as an attribute macro, stands apart from
			// the parameter list behind it: '[&] mark_nodebug (int _Value)'.
			if (Left.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				auto iBeforeName = fg_PreviousCode(_Tokens, _iLeft);
				if (iBeforeName >= 0 && _Structure.f_IsAngleBracket(umint(iBeforeName)) && _Tokens.f_IsText(Tokens[umint(iBeforeName)], ">"))
				{
					auto iNode = fg_FindGroupClosingAt(_Structure, umint(iBeforeName), ECodeBracket::mc_Angle);
					if (iNode >= 0)
						iBeforeName = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);
				}

				if (iBeforeName >= 0 && _Tokens.f_IsText(Tokens[umint(iBeforeName)], "]") && fg_IsCaptureList(_Tokens, _Structure, umint(iBeforeName)))
					return ECodeSpacing::mc_Space;
			}

			// Directly inside a template argument list a name in front of a parameter list
			// can spell a function type, 'TCFunction<FCallback (int)>', which is written
			// with a space, as well as a call in a value argument, which is not. A name
			// Malterlib spells as a function's is that call; every other keeps its
			// spelling. An alias spells function types the same way, 'using FCall = void
			// (int)', and so does a pointer to function anywhere: 'void (*)(int)'.
			if (Left.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				auto const &Nodes = _Structure.f_GetNodes();
				auto iOpening = _Structure.f_FindNodeOpeningAt(_iRight);
				if (iOpening < Nodes.f_GetLen() && Nodes[iOpening].m_Kind == ECodeNodeKind::mc_Group)
				{
					auto const &Node = Nodes[iOpening];
					// A cv-qualifier ends the type a pointer or reference declarator in parentheses
					// follows, as a type's name does: 'char const (&_String)[4]', 'TCFoo<int const (&)[4]>'.
					if (_Tokens.f_IsText(Left, "const") || _Tokens.f_IsText(Left, "volatile"))
					{
						auto iInner = fg_NextCode(_Tokens, _iRight);
						auto iBehind = fg_NextCode(_Tokens, Node.m_iLastToken);
						bool bDeclarator = iInner >= 0
							&& (_Tokens.f_IsText(Tokens[umint(iInner)], "*") || _Tokens.f_IsText(Tokens[umint(iInner)], "&"))
							&& iBehind >= 0
							&& (_Tokens.f_IsText(Tokens[umint(iBehind)], "(") || _Tokens.f_IsText(Tokens[umint(iBehind)], "["))
							&& Node.m_SplitPoints.f_IsEmpty()
						;
						if (bDeclarator)
							return ECodeSpacing::mc_Space;
					}

					if (Nodes[Node.m_iParent].m_Bracket == ECodeBracket::mc_Angle)
					{
						// A template argument that can be read as a type is one, so a fundamental type in
						// front of a parenthesis there spells a function type: 'TCFunction<void ()>'.
						if (!_Tokens.f_HasRole(Left, ECodeNameRole::mc_Type) && fg_NamesType(_Tokens, Left))
							return ECodeSpacing::mc_Space;

						return fg_NamesFunction(_Tokens, Left) ? ECodeSpacing::mc_None : ECodeSpacing::mc_Preserve;
					}

					// A pointer to function's declarator is followed by its parameter list and
					// holds no separator, which is what tells 'void (*pCall)(int)' from a call
					// whose first argument takes an address: 'f_Call(&CFoo::f_Get, _Value)'.
					// A calling convention macro may stand in front of the declarator:
					// 'void (DMibCrossmoduleAPI *m_fFree)(CFoo *_pModule)'.
					auto iInner = fg_NextCode(_Tokens, _iRight);
					if (iInner >= 0 && Tokens[umint(iInner)].m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Tokens[umint(iInner)], gc_pExpressionKeywords))
					{
						auto iStar = fg_NextCode(_Tokens, umint(iInner));
						if (iStar >= 0 && (_Tokens.f_IsText(Tokens[umint(iStar)], "*") || _Tokens.f_IsText(Tokens[umint(iStar)], "&")))
							iInner = iStar;
						else
						{
							// A pointer to member names its class in front of the '*': 'tf_CReturn (CFoo::* _pMember)(int)'.
							auto iScope = umint(iInner);
							while (true)
							{
								auto iNext = fg_NextCode(_Tokens, iScope);
								if (iNext >= 0 && _Structure.f_IsAngleBracket(umint(iNext)) && _Tokens.f_IsText(Tokens[umint(iNext)], "<"))
								{
									auto iArguments = _Structure.f_FindNodeOpeningAt(umint(iNext));
									if (iArguments >= Nodes.f_GetLen())
										break;

									iNext = fg_NextCode(_Tokens, Nodes[iArguments].m_iLastToken);
								}

								if (iNext < 0 || !_Tokens.f_IsText(Tokens[umint(iNext)], "::"))
									break;

								auto iAfter = fg_NextCode(_Tokens, umint(iNext));
								if (iAfter >= 0 && _Tokens.f_IsText(Tokens[umint(iAfter)], "*"))
								{
									iInner = iAfter;

									break;
								}

								if (iAfter < 0 || Tokens[umint(iAfter)].m_Kind != ECodeTokenKind::mc_Identifier)
									break;

								iScope = umint(iAfter);
							}
						}
					}

					// A pointer or a reference to an array is followed by the bound instead: 'ch8
					// (&_Dest)[t_Size]'. Behind a name the naming lists as a type's, which cannot be
					// called, the declarator stands apart from it.
					auto iBehind = fg_NextCode(_Tokens, Node.m_iLastToken);
					bool bDeclarator = iInner >= 0
						&& (_Tokens.f_IsText(Tokens[umint(iInner)], "*") || _Tokens.f_IsText(Tokens[umint(iInner)], "&"))
						&& iBehind >= 0
						&& (_Tokens.f_IsText(Tokens[umint(iBehind)], "(") || _Tokens.f_IsText(Tokens[umint(iBehind)], "["))
						&& Node.m_SplitPoints.f_IsEmpty()
					;
					// A parameter list outside a default argument holds declarations only, which settles
					// what the name in front of the declarator is: 'tf_CType (&_Array)[t_nSize]'.
					if (bDeclarator && !fg_NamesType(_Tokens, Left) && Node.m_iParent < Nodes.f_GetLen() && fg_IsParameterList(_Tokens, _Structure, Node.m_iParent))
					{
						auto const &List = Nodes[Node.m_iParent];
						auto iStart = List.m_iFirstToken;
						for (auto iSplit : List.m_SplitPoints)
						{
							if (iSplit < Node.m_iFirstToken)
								iStart = iSplit;
						}

						bool bDefault = false;
						for (auto i = iStart + 1; i < Node.m_iFirstToken && !bDefault; ++i)
						{
							auto iInner = _Structure.f_FindEnclosingNode(i);
							bDefault = iInner == Node.m_iParent && _Tokens.f_IsText(Tokens[i], "=");
						}

						if (!bDefault)
							return ECodeSpacing::mc_Space;
					}

					if (bDeclarator)
						return fg_NamesType(_Tokens, Left) ? ECodeSpacing::mc_Space : ECodeSpacing::mc_Preserve;

					umint iStatement = Node.m_iParent;
					while (iStatement && Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement)
						iStatement = Nodes[iStatement].m_iParent;

					auto const &StatementFirst = Tokens[Nodes[iStatement].m_iFirstToken];
					if (_Tokens.f_IsText(StatementFirst, "using") || _Tokens.f_IsText(StatementFirst, "typedef"))
						return ECodeSpacing::mc_Preserve;

					return ECodeSpacing::mc_None;
				}

				return ECodeSpacing::mc_None;
			}

			if (fLeft(")") || fLeft("]"))
				return ECodeSpacing::mc_None;

			return ECodeSpacing::mc_Preserve;
		}

		// '->' is a trailing return type as well as member access. After a parameter list
		// or a function's qualifiers it can only be the former, which is written apart on
		// both sides; after a call's arguments or a name it can only be the latter, which
		// hugs its operands.
		if (fLeft("->"))
			return fg_IsTrailingReturnArrow(_Tokens, _Structure, _iLeft) ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;

		if (fRight("->"))
		{
			if (fg_IsTrailingReturnArrow(_Tokens, _Structure, _iRight))
				return ECodeSpacing::mc_Space;

			if (fLeft(")") || Left.m_Kind == ECodeTokenKind::mc_Identifier || fLeft("]"))
				return ECodeSpacing::mc_None;

			return ECodeSpacing::mc_Preserve;
		}

		// A pointer to member is declared with its '*' apart from the class it qualifies and on
		// what it declares, the way a pointer's is: 'tf_CReturn (CFoo:: *_pMember)(int)'.
		// No expression puts a '*' behind a '::'.
		if (fLeft("::") && fRight("*"))
		{
			auto iClass = fg_PreviousCode(_Tokens, _iLeft);
			bool bClass = iClass >= 0
				&& (Tokens[umint(iClass)].m_Kind == ECodeTokenKind::mc_Identifier || (_Structure.f_IsAngleBracket(umint(iClass)) && _Tokens.f_IsText(Tokens[umint(iClass)], ">")))
			;

			return bClass ? ECodeSpacing::mc_Space : ECodeSpacing::mc_Preserve;
		}

		if (fLeft("*"))
		{
			auto iColons = fg_PreviousCode(_Tokens, _iLeft);
			if (iColons >= 0 && _Tokens.f_IsText(Tokens[umint(iColons)], "::"))
			{
				if (fRight("const") || fRight("volatile"))
					return ECodeSpacing::mc_Space;

				if (Right.m_Kind == ECodeTokenKind::mc_Identifier || fRight(")") || fRight(",") || fRight(">") || fRight("*") || fRight("&"))
					return ECodeSpacing::mc_None;
			}
		}

		// A name qualified from the global scope behind a keyword stands apart from the keyword:
		// 'using ::size_t', 'return ::fg_Get()', 'class CFoo : public ::NA::CBase'.
		if (fRight("::") && Left.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			constexpr ch8 const *c_pDeclarationKeywords[] =
				{
					"using", "typename", "namespace", "const", "volatile", "static", "constexpr", "consteval", "constinit", "inline", "virtual"
					, "friend", "extern", "struct", "class", "enum", "union", "mutable", "thread_local", "public", "private", "protected"
				}
			;
			if (fg_IsAnyText(_Tokens, Left, gc_pExpressionKeywords) || fg_IsAnyText(_Tokens, Left, c_pDeclarationKeywords))
				return ECodeSpacing::mc_Space;
		}

		// Member access and qualification never take spaces.
		if (fLeft(".") || fLeft("::") || fRight(".") || fRight("::"))
			return ECodeSpacing::mc_None;

		// A declarator is separated from the type it modifies and hugs what it declares:
		// 'CStr const &_Name', 'TCVector<int> **ppList', 'ch8 const * const'. Another
		// declarator hugs it.
		if (fg_IsDeclaratorText(_Tokens, Right) && fg_IsDeclaratorToken(_Tokens, _Structure, _iRight))
		{
			// A reference to a pointer refers to the pointer, and stands apart from it the way
			// a cv-qualifier of the pointer does: 'ch8 const * &o_pParse', 'CFoo * const &'.
			// A pointer to a pointer is written tight: 'ch8 **ppArgv'.
			if (fLeft("*") && (fRight("&") || fRight("&&")))
				return ECodeSpacing::mc_Space;

			return fg_IsDeclaratorText(_Tokens, Left) ? ECodeSpacing::mc_None : ECodeSpacing::mc_Space;
		}

		// A cv-qualifier behind a pointer declarator qualifies the pointer, and stands apart
		// from the declarator as it does from a type: 'CFoo * const pFoo', 'ch8 const * const *'.
		if (fLeft("*") && (fRight("const") || fRight("volatile")) && fg_IsDeclaratorToken(_Tokens, _Structure, _iLeft))
			return ECodeSpacing::mc_Space;

		if (fg_IsDeclaratorText(_Tokens, Left) && fg_IsDeclaratorToken(_Tokens, _Structure, _iLeft))
		{
			// A name behind the declarator that is followed by the declared name is a calling
			// convention macro, and the declarator stands apart from it, as the sources write
			// 'void * DMibCrossmoduleAPI fs_Alloc(umint _Size)'.
			bool bQualifier = _Tokens.f_IsText(Right, "const") || _Tokens.f_IsText(Right, "volatile");
			if (Right.m_Kind == ECodeTokenKind::mc_Identifier && !bQualifier && !fg_IsAnyText(_Tokens, Right, gc_pExpressionKeywords))
			{
				// The name has to follow on the same branch of a conditional, and a word that
				// never names a declaration is no name: 'auto &&_fThis' / '#else' / 'this auto'.
				constexpr ch8 const *c_pNoName[] =
					{
						"this", "auto", "const", "volatile", "typename", "struct", "class", "enum", "static", "constexpr", "inline"
					}
				;
				auto iBehind = fg_NextCode(_Tokens, _iRight);
				bool bDirective = false;
				for (auto i = _iRight + 1; iBehind >= 0 && i < umint(iBehind); ++i)
					bDirective |= Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor;

				bool bName = iBehind >= 0
					&& !bDirective
					&& Tokens[umint(iBehind)].m_Kind == ECodeTokenKind::mc_Identifier
					&& !fg_IsAnyText(_Tokens, Tokens[umint(iBehind)], gc_pExpressionKeywords)
					&& !fg_IsAnyText(_Tokens, Tokens[umint(iBehind)], c_pNoName)
				;
				if (bName)
					return ECodeSpacing::mc_Space;
			}

			// A pack's ellipsis stands apart from the declarator in front of it, as it does
			// from a type, wherever it stands: 'tfp_CParams && ...p_Params', '&& ...' where
			// the pack has no name, and 'tp_CParams && ...>'.
			if (fRight("..."))
				return ECodeSpacing::mc_Space;

			// A specifier behind a trailing return type declares nothing: '-> CFoo * override'.
			constexpr ch8 const *c_pSpecifiers[] = {"override", "final", "noexcept", "requires", "mutable"};
			if (fg_IsAnyText(_Tokens, Right, c_pSpecifiers))
				return ECodeSpacing::mc_Space;

			// A structured binding's names hug the declarator the way a name does: 'auto &[A, B]'.
			if (Right.m_Kind == ECodeTokenKind::mc_Identifier || fRight("["))
				return ECodeSpacing::mc_None;

			// So does a pointer to function's declarator behind a pointer return type: 'void *(*m_fAlloc)(umint _Size)'.
			if (fRight("("))
			{
				auto iDeclarator = _Structure.f_FindNodeOpeningAt(_iRight);
				if (iDeclarator < _Structure.f_GetNodes().f_GetLen() && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator))
					return ECodeSpacing::mc_None;
			}
		}

		// Behind a template argument list a '&&' in front of a name is a declarator as
		// often as it is the operator between two concepts.
		if (fg_IsDeclaratorText(_Tokens, Left) && Right.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			auto iBefore = fg_PreviousCode(_Tokens, _iLeft);
			if (iBefore >= 0 && _Structure.f_IsAngleBracket(umint(iBefore)))
				return ECodeSpacing::mc_Preserve;
		}

		// A pack's ellipsis goes with what the pack is: a declaration's hugs the name it
		// introduces and stands apart from the type in front of it, as in
		// 'NTraits::TCDecay<tp_CParams> ...p_Params' and 'typename ...tp_CParams'; an
		// expansion has no name to hug and is written tight against what it expands, as in
		// 'tp_CParams...>', 'fg_Forward<tp_CParams>(p_Params)...' and 'sizeof...(X)'. One
		// behind a declarator is settled above.
		bool bPackOperand = Left.m_Kind == ECodeTokenKind::mc_Identifier || fLeft(">") || fLeft(")") || fLeft("]");
		if (fRight("...") && bPackOperand)
		{
			return fg_DeclaresPack(_Tokens, _iRight) ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;
		}

		if (fLeft("...") && Right.m_Kind == ECodeTokenKind::mc_Identifier)
			return ECodeSpacing::mc_None;

		// What follows a parameter list is the function's qualifiers and specifiers, and
		// they are separated from it and from each other. Without a spelling for these the
		// declaration cannot be measured as one line, and so could never be joined.
		constexpr ch8 const *c_pQualifiers[] =
			{
				"const", "volatile", "noexcept", "override", "final", "mutable", "requires", "&", "&&"
			}
		;
		// The placement arguments of a 'new' stand apart from the type it constructs, and a
		// declaration's alignment and a type 'decltype' names stand apart from what follows
		// them: 'new (_pMemory) CFoo(1)', 'alignas(CData) uint8 m_Space[4]'.
		if (fLeft(")") && (Right.m_Kind == ECodeTokenKind::mc_Identifier || fRight("::")))
		{
			auto iNode = fg_FindGroupClosingAt(_Structure, _iLeft, ECodeBracket::mc_None);
			if (iNode >= 0)
			{
				constexpr ch8 const *c_pApart[] =
					{
						"new", "alignas", "decltype", "__attribute__", "__declspec"
					}
				;
				// A macro's argument list with a name behind it spells a type or a prefix for
				// what follows: 'DMibFloatConstexprCache(TCFloat) Cache'.
				auto iKeyword = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);
				bool bApart = iKeyword >= 0
					&& (fg_IsAnyText(_Tokens, Tokens[umint(iKeyword)], c_pApart)
						|| (Right.m_Kind == ECodeTokenKind::mc_Identifier && _Tokens.f_HasRole(Tokens[umint(iKeyword)], ECodeNameRole::mc_Macro)))
				;
				if (bApart && !fg_IsAnyText(_Tokens, Right, c_pQualifiers))
					return ECodeSpacing::mc_Space;
			}

			// An attribute behind a parameter list stands apart from it: '(void *) __attribute__((weak))'.
			if ((fRight("__attribute__") || fRight("__declspec")) && fg_ClosesParameterList(_Tokens, _Structure, _iLeft))
				return ECodeSpacing::mc_Space;

			// An attribute macro behind a parameter list, in front of the trailing return type,
			// stands apart from both: '(int _A) DMibSuppressUndefinedSanitizer -> void *'.
			auto iArrow = fg_NextCode(_Tokens, _iRight);
			bool bAttribute = Right.m_Kind == ECodeTokenKind::mc_Identifier
				&& iArrow >= 0
				&& _Tokens.f_IsText(Tokens[umint(iArrow)], "->")
				&& fg_ClosesParameterList(_Tokens, _Structure, _iLeft)
				&& fg_IsTrailingReturnArrow(_Tokens, _Structure, umint(iArrow))
			;
			if (bAttribute)
				return ECodeSpacing::mc_Space;
		}

		// A parenthesis whose last word is a declarator or a qualifier spells a type and
		// nothing else, so it is a cast, and what it converts hugs it like any operand of a
		// unary operator: '(ch8 const *)&Value'.
		if (fLeft(")"))
		{
			// A parenthesis behind a name is that name's parameter list or call, whatever it holds:
			// 'extern "C" void _ZdaPv(void *) __attribute__((weak_import))'.
			auto iInner = fg_PreviousCode(_Tokens, _iLeft);
			bool bCast = fg_IsCast(_Tokens, _Structure, _iLeft);
			// A parenthesis holding nothing but a type's name is a cast as well: '(aint)-1'.
			// Malterlib's naming says which names are types, and the fundamental aliases
			// are a closed set.
			// A parenthesis behind a name is a call's or a keyword's operand instead.
			if (!bCast && iInner >= 0)
			{
				auto iOpen = fg_PreviousCode(_Tokens, umint(iInner));
				auto iBeforeOpen = iOpen >= 0 ? fg_PreviousCode(_Tokens, umint(iOpen)) : aint(-1);
				bCast = iOpen >= 0
					&& _Tokens.f_IsText(Tokens[umint(iOpen)], "(")
					&& fg_NamesType(_Tokens, Tokens[umint(iInner)])
					&& !(iBeforeOpen >= 0 && Tokens[umint(iBeforeOpen)].m_Kind == ECodeTokenKind::mc_Identifier)
				;
			}

			bool bOperand = (Right.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Right, c_pQualifiers))
				|| Right.m_Kind == ECodeTokenKind::mc_Number
				|| fg_IsDeclaratorText(_Tokens, Right)
				|| fRight("-")
				|| fRight("+")
				|| fRight("!")
				|| fRight("~")
			;
			if (bCast && bOperand && !fg_ClosesParameterList(_Tokens, _Structure, _iLeft))
				return ECodeSpacing::mc_None;
		}

		// A '&' or '&&' behind a parenthesis qualifies a function only where the parenthesis
		// is its parameter list. Behind any other it takes an address or joins two operands.
		bool bRefQualifies = !fLeft(")") || (!fRight("&") && !fRight("&&")) || fg_ClosesParameterList(_Tokens, _Structure, _iLeft);
		bool bAfterDeclarator = (fLeft(")") && bRefQualifies) || fg_IsRefQualifier(_Tokens, _iLeft);
		for (auto pQualifier : c_pQualifiers)
			bAfterDeclarator |= fLeft(pQualifier) && !fLeft("&") && !fLeft("&&");

		if (bAfterDeclarator)
		{
			for (auto pQualifier : c_pQualifiers)
			{
				if (fRight(pQualifier))
					return ECodeSpacing::mc_Space;
			}
		}

		// A keyword and the name beside it are separated by one space: 'auto' and the name
		// it declares, 'template' and an instantiated name, 'return' and its operand. Two
		// plain names are settled only on one line, where one space separates them however
		// the source aligned them: a macro written on a line of its own inside a list
		// stands next to a name too, and keeps that line.
		if (Left.m_Kind == ECodeTokenKind::mc_Identifier && Right.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			constexpr ch8 const *c_pKeywords[] =
				{
					"auto", "template", "extern", "static", "inline", "constexpr", "consteval", "constinit", "virtual", "explicit"
					, "friend", "typename", "const", "volatile", "mutable", "struct", "class", "union", "enum", "namespace", "using"
					, "return", "co_return", "co_await", "co_yield", "throw", "new", "delete", "case", "goto", "sizeof", "alignof"
					, "void", "bool", "int", "char", "short", "long", "unsigned", "signed", "float", "double", "operator", "requires"
				}
			;
			for (auto pKeyword : c_pKeywords)
			{
				if (_Tokens.f_IsText(Left, pKeyword) || _Tokens.f_IsText(Right, pKeyword))
					return ECodeSpacing::mc_Space;
			}

			for (auto i = _iLeft + 1; i < _iRight; ++i)
			{
				auto Kind = Tokens[i].m_Kind;
				if (Kind != ECodeTokenKind::mc_Whitespace)
					return ECodeSpacing::mc_Preserve;
			}

			return ECodeSpacing::mc_Space;
		}

		// A bit-field's width hugs the ':' that introduces it, on both sides:
		// 'uint8 mp_Priority:2 = 0'.
		if (fLeft(":") || fRight(":"))
		{
			if (fg_IsBitFieldColon(_Tokens, _Structure, fLeft(":") ? _iLeft : _iRight))
				return ECodeSpacing::mc_None;
		}

		// A colon that ends a label hugs it: 'case 1:', 'public:'. The builder ends a
		// statement at such a colon, which is how it is told from the one of a conditional,
		// an initializer list or a class head. An anonymous enumeration's base type is
		// written both ways, tight after 'enum' and apart behind a name, so it keeps what
		// it has.
		if (fRight(":"))
		{
			if (fLeft("enum"))
				return ECodeSpacing::mc_Preserve;

			auto iStatement = _Structure.f_FindStatementEndingAt(_iRight);
			if (iStatement < _Structure.f_GetNodes().f_GetLen() && _Structure.f_GetNodes()[iStatement].m_iFirstToken != _iRight)
				return ECodeSpacing::mc_None;
		}

		// A sign with no operand in front of it is unary and hugs its operand: '-1',
		// 'a - -b'. Behind an operand it is the binary operator, written apart.
		if (fLeft("+") || fLeft("-"))
		{
			auto iBefore = fg_PreviousCode(_Tokens, _iLeft);
			bool bOperand = false;
			if (iBefore >= 0)
			{
				auto const &Before = Tokens[umint(iBefore)];
				bOperand = Before.m_Kind == ECodeTokenKind::mc_Number
					|| Before.m_Kind == ECodeTokenKind::mc_StringLiteral
					|| Before.m_Kind == ECodeTokenKind::mc_CharLiteral
					|| (Before.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords))
					|| (_Tokens.f_IsText(Before, ")") && !fg_IsCast(_Tokens, _Structure, umint(iBefore)))
					|| _Tokens.f_IsText(Before, "]")
					|| (_Structure.f_IsAngleBracket(umint(iBefore)) && _Tokens.f_IsText(Before, ">"))
					|| (_Tokens.f_IsText(Before, "}") && fg_ClosesExpressionBody(_Structure, umint(iBefore)))
					|| fg_IsPostfixStep(_Tokens, _Structure, umint(iBefore))
				;
			}

			if (!bOperand)
				return ECodeSpacing::mc_None;
		}

		// A block pointer's '^' hugs what it declares: '(^fReport)'.
		if (fLeft("^"))
		{
			auto iOpen = fg_PreviousCode(_Tokens, _iLeft);
			auto iDeclarator = iOpen >= 0 ? _Structure.f_FindNodeOpeningAt(umint(iOpen)) : _Structure.f_GetNodes().f_GetLen();
			if (iDeclarator < _Structure.f_GetNodes().f_GetLen() && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator))
				return ECodeSpacing::mc_None;
		}

		// Behind a cast a '*' or '&' is unary and hugs its operand: '(void *)*pJumpBuffer'.
		if (fLeft("*") || fLeft("&"))
		{
			auto iBefore = fg_PreviousCode(_Tokens, _iLeft);
			if (iBefore >= 0 && _Tokens.f_IsText(Tokens[umint(iBefore)], ")") && fg_IsCast(_Tokens, _Structure, umint(iBefore)))
				return ECodeSpacing::mc_None;
		}

		constexpr ch8 const *c_pBinaryOperators[] =
			{
				"==", "!=", "<=", ">=", "<=>", "||", "&&", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
				, "+", "-", "/", "%", "|", "^", "?", ":"
			}
		;
		for (auto pOperator : c_pBinaryOperators)
		{
			if (_Tokens.f_IsText(Left, pOperator) || _Tokens.f_IsText(Right, pOperator))
				return ECodeSpacing::mc_Space;
		}

		return ECodeSpacing::mc_Preserve;
	}
}
