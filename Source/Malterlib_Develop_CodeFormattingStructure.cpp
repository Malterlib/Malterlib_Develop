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

			return;
		}

		mp_Nodes[iRoot].m_iFirstToken = mp_Significant.f_GetFirst();
		auto iNext = fp_BuildBlock(iRoot, 0, false);
		mp_Nodes[iRoot].m_iLastToken = mp_Significant[fg_Min(iNext, mp_Significant.f_GetLen() - 1)];
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

		// A braced initializer is written one element per line on purpose, so a construct
		// around one keeps its lines instead of collapsing the list into an expression.
		if (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Brace)
		{
			auto const &Source = mp_pTokens->f_GetSource();
			for (auto i = Node.m_iFirstToken; i <= _iLastToken && !Node.m_bHasMultiLineBrace; ++i)
			{
				auto const &Token = mp_pTokens->f_GetTokens()[i];
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
	// A '<' opens a template argument list only when it is written tight against the name
	// before it and a matching '>' exists before the enclosing construct ends. Malterlib
	// spells comparisons with spaces, so the tight spelling is a reliable discriminator.
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
		auto const &Previous = Tokens[mp_Significant[_iToken - 1]];
		if (Previous.m_Kind != ECodeTokenKind::mc_Identifier && !mp_pTokens->f_IsText(Previous, "]"))
			return 0;

		// A template header's list can only be one, however it is spaced.
		bool bHeader = mp_pTokens->f_IsText(Previous, "template");

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

	// A brace that holds statements is a block, even inside an argument list, where it is a
	// lambda body. A braced initializer holds expressions: it never has a statement
	// terminator at its own level, and no element of one can start with a keyword only a
	// statement begins with, which is what a body whose every statement is compound has
	// instead of a terminator: 'mutable { for (auto &Entry : Entries) { ... } }'.
	bool CCodeStructure::fp_IsBlockBrace(umint _iToken) const
	{
		static ch8 const *const gsc_pStatementKeywords[] =
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

				for (auto pKeyword : gsc_pStatementKeywords)
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
			if (Token.m_Kind == ECodeTokenKind::mc_Punctuator && (mp_pTokens->f_IsText(Token, ",") || mp_pTokens->f_IsText(Token, ";")))
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

				auto const &First = Tokens[mp_Significant[iHead]];
				bool bDefinition = mp_pTokens->f_IsText(First, "struct")
					|| mp_pTokens->f_IsText(First, "class")
					|| mp_pTokens->f_IsText(First, "union")
					|| mp_pTokens->f_IsText(First, "enum")
					|| mp_pTokens->f_IsText(First, "namespace")
				;
				bool bBlock = bAfterCloseParen || bAfterTrailingReturn || i == _iToken || bDefinition;
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

					if (Next.m_Kind == ECodeTokenKind::mc_Punctuator && !mp_pTokens->f_IsText(Next, "{") && !mp_pTokens->f_IsText(Next, "}"))
						continue;
				}

				fp_Finish(iNode, mp_Significant[iClose]);

				return i;
			}

			auto Bracket = fg_GetOpeningBracket(*mp_pTokens, Token);
			if (Bracket != ECodeBracket::mc_None)
			{
				mp_Nodes[iNode].m_SplitPoints.f_Insert(mp_Significant[i]);
				i = fp_BuildGroup(iNode, i, Bracket);
				bAfterCloseParen = Bracket == ECodeBracket::mc_Paren;
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

	// The node that most closely encloses the token, or the node count when none does.
	umint fg_FindEnclosingNode(CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto iFound = Nodes.f_GetLen();
		for (umint iNode = 0; iNode < Nodes.f_GetLen(); ++iNode)
		{
			auto const &Node = Nodes[iNode];
			if (Node.m_iFirstToken >= _iToken || Node.m_iLastToken <= _iToken)
				continue;

			if (iFound == Nodes.f_GetLen() || Node.m_iFirstToken >= Nodes[iFound].m_iFirstToken)
				iFound = iNode;
		}

		return iFound;
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
		bool bSpelled = false;
		for (auto i = Statement.m_iFirstToken; i <= _iLast; ++i)
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
					bool bDecltype = bParen && _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "decltype");
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

		// 'if constexpr (...)' holds a condition, whatever follows it.
		if (_Tokens.f_IsText(Tokens[umint(iName)], "constexpr"))
			return false;

		// Directly inside a template argument list a parenthesis behind a type spells a
		// function type, whose parameters it declares: 'TCFunction<void (CFoo &&_Value)>'.
		// A type ends in a template argument list of its own, or in a name written apart
		// from the parenthesis, which a call in a value argument never is.
		if (Group.m_iParent < Nodes.f_GetLen() && Nodes[Group.m_iParent].m_Kind == ECodeNodeKind::mc_Group && Nodes[Group.m_iParent].m_Bracket == ECodeBracket::mc_Angle)
		{
			auto const &Name = Tokens[umint(iName)];
			if (_Structure.f_IsAngleBracket(umint(iName)) && _Tokens.f_IsText(Name, ">"))
				return true;

			if (Name.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Name, gc_pExpressionKeywords) && Name.f_GetEnd() != Tokens[Group.m_iFirstToken].m_iOffset)
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
			for (auto const &Node : Nodes)
			{
				if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Angle || Node.m_iLastToken != umint(iIntroducer))
					continue;

				auto iCapture = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
				bool bCaptures = iCapture >= 0 && _Tokens.f_IsText(Tokens[umint(iCapture)], "]") && fg_NameSubscriptOperator(_Tokens, umint(iCapture)) < 0;
				if (bCaptures && fg_IsCaptureList(_Tokens, _Structure, umint(iCapture)))
					return true;

				break;
			}
		}

		// A function's parameter list is the first parenthesis of its statement; the ones
		// after it belong to a constructor's initializers or to expressions.
		auto const &Parent = Nodes[Group.m_iParent];
		if (Parent.m_Kind != ECodeNodeKind::mc_Statement)
			return false;

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
					|| _Tokens.f_IsText(Tokens[umint(iBeforeChild)], "requires"))
			;
			if (iChild != _iGroup && bOperand)
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
			auto iClose = umint(iName);
			iName = -1;
			for (auto const &Node : Nodes)
			{
				if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Angle || Node.m_iLastToken != iClose)
					continue;

				iName = fg_PreviousCode(_Tokens, Node.m_iFirstToken);

				break;
			}

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
		// is made of names, qualification, template argument lists, and declarators.
		auto iBefore = fg_PreviousCode(_Tokens, umint(iName));
		if (iBefore >= 0 && iBefore < aint(Parent.m_iFirstToken))
			iBefore = -1;

		if (iBefore >= 0 && fg_SpellsType(_Tokens, _Structure, Group.m_iParent, umint(iBefore)))
			return true;

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
			if (bOpens && iTarget >= 0 && _Tokens.f_GetText(Tokens[umint(iTarget)]) == _Tokens.f_GetText(Tokens[umint(iName)]))
				return true;
		}

		auto const &After = Tokens[umint(iAfter)];
		static ch8 const *const gsc_pFunctionTails[] =
			{
				"{", "const", "volatile", "noexcept", "override", "final", "mutable", "requires"
			}
		;
		if (fg_IsAnyText(_Tokens, After, gsc_pFunctionTails))
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
		static ch8 const *const gsc_pOther[] =
			{
				"case", "default", "public", "private", "protected", "struct", "class", "union", "enum"
				, "namespace", "template", "using", "friend", "operator"
			}
		;
		if (fg_IsAnyText(_Tokens, First, gsc_pOther) || fg_IsAnyText(_Tokens, First, gc_pExpressionKeywords))
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

	// A name spelled as an underscore with nothing but lower case behind it, '_o', '_j' or
	// '_' itself, is one of Malterlib's DSL markers rather than something declared: a
	// parameter's underscore is followed by a capital, and nothing else takes one at all.
	bool fg_IsDSLMarker(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		auto Text = _Tokens.f_GetText(_Token);
		if (Text.f_IsEmpty() || Text.f_GetStr()[0] != '_')
			return false;

		for (umint i = 1; i < Text.f_GetLen(); ++i)
		{
			if (Text.f_GetStr()[i] < 'a' || Text.f_GetStr()[i] > 'z')
				return false;
		}

		return true;
	}

	// An operator's spelling says what it does only where an operand stands on both sides
	// of it. Without one in front it is the unary form, '-1' and '*pValue'; without one
	// behind it names something else, a cast's '(CFoo *)' or a pack's '&&...'. '*', '&'
	// and '&&' are ambiguous even in that position, since a name in front of one can be a
	// type as easily as an operand, and behind a parameter list the same token qualifies
	// the function: 'f_Get() const &'.
	bool fg_IsInfixOperator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Token = Tokens[_iToken];
		if (Token.m_Kind != ECodeTokenKind::mc_Punctuator || _Structure.f_IsAngleBracket(_iToken))
			return false;

		static ch8 const *const gsc_pOperators[] =
			{
				"*", "/", "%", "+", "-", "<<", ">>", "<", ">", "<=", ">=", "<=>", "==", "!=", "&", "^", "|", "&&", "||"
			}
		;
		if (!fg_IsAnyText(_Tokens, Token, gsc_pOperators))
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
		;
		if (!bOperand)
			return false;

		auto const &After = Tokens[umint(iAfter)];
		static ch8 const *const gsc_pCloses[] =
			{
				")", "]", "}", ",", ";", "..."
			}
		;
		if (fg_IsAnyText(_Tokens, After, gsc_pCloses))
			return false;

		if (!fg_IsDeclaratorText(_Tokens, Token))
			return true;

		if (fg_IsDeclaratorToken(_Tokens, _Structure, _iToken))
			return false;

		// What follows a ref-qualifier is the rest of the declaration: the trailing return
		// type, the body, a pure specifier, a requires clause or another qualifier.
		static ch8 const *const gsc_pTails[] =
			{
				"->", "{", "=", "requires", "const", "volatile", "noexcept", "override", "final", "&", "&&"
			}
		;
		if (fg_IsAnyText(_Tokens, After, gsc_pTails))
			return false;

		// What is left is the spelling C++ itself cannot tell apart, 'C(CStr &_A)' against
		// 'C(a & b)', so the reading is settled only where a declaration cannot stand: behind
		// a literal, behind the '=' that ends the declarator part of what the token stands
		// in, or in a condition, which declares nothing without an '=' of its own.
		if (Before.m_Kind == ECodeTokenKind::mc_Number || Before.m_Kind == ECodeTokenKind::mc_StringLiteral || Before.m_Kind == ECodeTokenKind::mc_CharLiteral)
			return true;

		auto const &Nodes = _Structure.f_GetNodes();

		// A ')' closes a cast as well as a call, and '(CFoo)*pValue' spells the tokens of a
		// multiplication, so only a call or a subscript in front of the token settles it. An
		// operator spelled like a call yields a value the same way, apart from 'decltype',
		// which names a type: 'decltype(m_Value) *pValue'.
		if (_Tokens.f_IsText(Before, ")") || _Tokens.f_IsText(Before, "]"))
		{
			static ch8 const *const gsc_pValueOperators[] =
				{
					"sizeof", "alignof", "typeid", "noexcept"
				}
			;
			for (auto const &Group : Nodes)
			{
				if (Group.m_Kind != ECodeNodeKind::mc_Group || Group.m_iLastToken != umint(iBefore))
					continue;

				auto iName = fg_PreviousCode(_Tokens, Group.m_iFirstToken);
				if (iName < 0)
					return false;

				auto const &Name = Tokens[umint(iName)];
				if (Name.m_Kind == ECodeTokenKind::mc_Identifier)
					return !fg_IsAnyText(_Tokens, Name, gc_pExpressionKeywords) || fg_IsAnyText(_Tokens, Name, gsc_pValueOperators);

				return _Tokens.f_IsText(Name, ")") || _Tokens.f_IsText(Name, "]");
			}

			return false;
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
	// A capture list stands where an operand cannot: a subscript follows a name, a call, a
	// template argument list, another subscript, or a literal.
	bool fg_IsCaptureList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		for (auto const &Node : _Structure.f_GetNodes())
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Square || Node.m_iLastToken != _iClose)
				continue;

			auto iBefore = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
			if (iBefore < 0)
				return true;

			auto const &Before = Tokens[umint(iBefore)];
			if (Before.m_Kind == ECodeTokenKind::mc_Identifier)
				return fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords);

			if (Before.m_Kind != ECodeTokenKind::mc_Punctuator)
				return false;

			return !_Tokens.f_IsText(Before, ")") && !_Tokens.f_IsText(Before, "]") && !_Structure.f_IsAngleBracket(umint(iBefore));
		}

		return false;
	}

	// An arrow introduces a trailing return type when a parameter list, or a function's
	// qualifiers behind one, stands in front of it; behind a call's arguments it is a
	// member access: 'fg_Get()->f_Call()'.
	bool fg_IsTrailingReturnArrow(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iArrow)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!_Tokens.f_IsText(Tokens[_iArrow], "->"))
			return false;

		static ch8 const *const gsc_pQualifiers[] =
			{
				"const", "volatile", "noexcept", "override", "final", "mutable", "&", "&&"
			}
		;
		auto iBefore = fg_PreviousCode(_Tokens, _iArrow);
		while (iBefore >= 0 && fg_IsAnyText(_Tokens, Tokens[umint(iBefore)], gsc_pQualifiers))
			iBefore = fg_PreviousCode(_Tokens, umint(iBefore));

		if (iBefore < 0 || !_Tokens.f_IsText(Tokens[umint(iBefore)], ")"))
			return false;

		return fg_ClosesParameterList(_Tokens, _Structure, umint(iBefore));
	}

	bool fg_ClosesParameterList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		for (umint iNode = 0; iNode < Nodes.f_GetLen(); ++iNode)
		{
			auto const &Node = Nodes[iNode];
			if (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Paren && Node.m_iLastToken == _iClose)
				return fg_IsParameterList(_Tokens, _Structure, iNode);
		}

		return false;
	}

	// A '*', '&' or '&&' declares a pointer or reference where only a type can stand in
	// front of it: behind 'const', 'volatile', or another declarator; behind a name or a
	// template argument list when nothing that could be an operand follows it, or when
	// it stands in a parameter list, outside a default argument. Elsewhere the same
	// token is an operator, or has no settled reading: 'TCFoo<T> &&_Other' and
	// 'cFoo<T> && cBar<T>' spell the same tokens.
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
			for (auto const &Node : _Structure.f_GetNodes())
			{
				if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_iLastToken != umint(iPrevious))
					continue;

				auto iKeyword = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
				bBehindTemplate = iKeyword >= 0 && _Tokens.f_IsText(Tokens[umint(iKeyword)], "decltype");

				break;
			}
		}

		if (!bBehindTemplate && Previous.m_Kind != ECodeTokenKind::mc_Identifier)
			return false;

		auto iNext = fg_NextCode(_Tokens, _iToken);
		if (iNext >= 0)
		{
			auto const &Next = Tokens[umint(iNext)];
			bool bUnnamed = _Tokens.f_IsText(Next, ",") || _Tokens.f_IsText(Next, ")") || _Tokens.f_IsText(Next, "...") || _Tokens.f_IsText(Next, "=")
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

		for (auto i = iStart + 1; i < _iToken; ++i)
		{
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

			if (!bNested && _Tokens.f_IsText(Tokens[i], "="))
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
			// Anywhere else the same spelling is a call or a construction, and stays tight.
			if (fRight("("))
			{
				auto const &Nodes = _Structure.f_GetNodes();
				for (auto const &Node : Nodes)
				{
					if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Angle || Node.m_iLastToken != _iLeft)
						continue;

					auto const &Parent = Nodes[Node.m_iParent];

					return Parent.m_Bracket == ECodeBracket::mc_Angle ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;
				}

				return ECodeSpacing::mc_None;
			}

			if (fRight("::") || fRight(",") || fRight(";") || fRight(")") || fRight("[") || fRight("]"))
				return ECodeSpacing::mc_None;

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

		// Plain '=' assigns and initializes, and is written apart from both sides. Behind
		// one of Malterlib's DSL markers it is instead the tail of a spelling that hugs the
		// key it follows, '"Names"_o= _o[...]', and a marker is told from every other name
		// by its shape: an underscore with nothing but lower case behind it, which no
		// declared name has. A capture default is settled by the markers around it, and an
		// operator function's name keeps whatever spelling it has.
		if (fLeft("=") || fRight("="))
		{
			if (fRight("=") && Left.m_Kind == ECodeTokenKind::mc_Identifier && fg_IsDSLMarker(_Tokens, Left))
				return ECodeSpacing::mc_Preserve;

			return ECodeSpacing::mc_Space;
		}

		// An operator in an infix position is written apart from both of its operands,
		// whatever they are spelled with: 'nFlags & mc_Mask', '5 * 5', 'a * (b + c)'. The
		// rules below read a parenthesis, a name or a declarator beside the operator as
		// part of some other construct, so this stands in front of them.
		if (fg_IsInfixOperator(_Tokens, _Structure, _iLeft) || fg_IsInfixOperator(_Tokens, _Structure, _iRight))
			return ECodeSpacing::mc_Space;

		// A keyword is separated from a parenthesis that follows it; a call name is not.
		// Operators spelled like a call, such as sizeof and decltype, stay tight.
		if (fRight("("))
		{
			static ch8 const *const gsc_pSpacedKeywords[] =
				{
					"if", "for", "while", "switch", "catch", "return", "co_return", "co_await", "co_yield"
					, "throw", "delete", "case", "requires", "constexpr"
				}
			;
			for (auto pKeyword : gsc_pSpacedKeywords)
			{
				if (_Tokens.f_IsText(Left, pKeyword))
					return ECodeSpacing::mc_Space;
			}

			// A bare name behind a capture list, such as an attribute macro, stands apart from
			// the parameter list behind it: '[&] mark_nodebug (int _Value)'.
			if (Left.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				auto iBeforeName = fg_PreviousCode(_Tokens, _iLeft);
				if (iBeforeName >= 0 && _Structure.f_IsAngleBracket(umint(iBeforeName)) && _Tokens.f_IsText(Tokens[umint(iBeforeName)], ">"))
				{
					for (auto const &Node : _Structure.f_GetNodes())
					{
						if (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Angle && Node.m_iLastToken == umint(iBeforeName))
						{
							iBeforeName = fg_PreviousCode(_Tokens, Node.m_iFirstToken);

							break;
						}
					}
				}

				if (iBeforeName >= 0 && _Tokens.f_IsText(Tokens[umint(iBeforeName)], "]") && fg_IsCaptureList(_Tokens, _Structure, umint(iBeforeName)))
					return ECodeSpacing::mc_Space;
			}

			// Directly inside a template argument list a name in front of a parameter list
			// can spell a function type, 'TCFunction<FCallback (int)>', which is written
			// with a space, as well as a call in a value argument, which is not. An alias
			// spells function types the same way, 'using FCall = void (int)', and so does a
			// pointer to function anywhere: 'void (*)(int)'.
			if (Left.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				auto const &Nodes = _Structure.f_GetNodes();
				for (auto const &Node : Nodes)
				{
					if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_iFirstToken != _iRight)
						continue;

					if (Nodes[Node.m_iParent].m_Bracket == ECodeBracket::mc_Angle)
						return ECodeSpacing::mc_Preserve;

					// A pointer to function's declarator is followed by its parameter list and
					// holds no separator, which is what tells 'void (*pCall)(int)' from a call
					// whose first argument takes an address: 'f_Call(&CFoo::f_Get, _Value)'.
					auto iInner = fg_NextCode(_Tokens, _iRight);
					auto iBehind = fg_NextCode(_Tokens, Node.m_iLastToken);
					bool bDeclarator = iInner >= 0
						&& (_Tokens.f_IsText(Tokens[umint(iInner)], "*") || _Tokens.f_IsText(Tokens[umint(iInner)], "&"))
						&& iBehind >= 0
						&& _Tokens.f_IsText(Tokens[umint(iBehind)], "(")
						&& Node.m_SplitPoints.f_IsEmpty()
					;
					if (bDeclarator)
						return ECodeSpacing::mc_Preserve;

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

		// Member access and qualification never take spaces.
		if (fLeft(".") || fLeft("::") || fRight(".") || fRight("::"))
			return ECodeSpacing::mc_None;

		// A declarator is separated from the type it modifies and hugs what it declares:
		// 'CStr const &_Name', 'TCVector<int> *&_pList', 'ch8 const *const'. Another
		// declarator hugs it.
		if (fg_IsDeclaratorText(_Tokens, Right) && fg_IsDeclaratorToken(_Tokens, _Structure, _iRight))
			return fg_IsDeclaratorText(_Tokens, Left) ? ECodeSpacing::mc_None : ECodeSpacing::mc_Space;

		if (fg_IsDeclaratorText(_Tokens, Left) && fg_IsDeclaratorToken(_Tokens, _Structure, _iLeft))
		{
			// A pack's ellipsis stands apart from the declarator in front of it, as it does
			// from a type: 'tfp_CParams && ...p_Params', and '&& ...' where the pack has no
			// name. Only the one that expands into a template argument list is written
			// tight, like any other expansion there: 'tp_CParams &&...>'.
			if (fRight("..."))
			{
				auto iBehind = fg_NextCode(_Tokens, _iRight);
				bool bExpands = iBehind >= 0 && _Structure.f_IsAngleBracket(umint(iBehind)) && _Tokens.f_IsText(Tokens[umint(iBehind)], ">");

				return bExpands ? ECodeSpacing::mc_None : ECodeSpacing::mc_Space;
			}

			if (Right.m_Kind == ECodeTokenKind::mc_Identifier)
				return ECodeSpacing::mc_None;
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
		static ch8 const *const gsc_pQualifiers[] =
			{
				"const", "volatile", "noexcept", "override", "final", "mutable", "requires", "&", "&&"
			}
		;
		// The placement arguments of a 'new' stand apart from the type it constructs:
		// 'new (_pMemory) CFoo(1)'.
		if (fLeft(")") && (Right.m_Kind == ECodeTokenKind::mc_Identifier || fRight("::")))
		{
			for (auto const &Node : _Structure.f_GetNodes())
			{
				if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_iLastToken != _iLeft)
					continue;

				auto iKeyword = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
				if (iKeyword >= 0 && _Tokens.f_IsText(Tokens[umint(iKeyword)], "new"))
					return ECodeSpacing::mc_Space;

				break;
			}
		}

		// A parenthesis whose last word is a declarator or a qualifier spells a type and
		// nothing else, so it is a cast, and what it converts hugs it like any operand of a
		// unary operator: '(ch8 const *)&Value'.
		if (fLeft(")"))
		{
			auto iInner = fg_PreviousCode(_Tokens, _iLeft);
			bool bCast = iInner >= 0
				&& (fg_IsDeclaratorText(_Tokens, Tokens[umint(iInner)]) || _Tokens.f_IsText(Tokens[umint(iInner)], "const") || _Tokens.f_IsText(Tokens[umint(iInner)], "volatile"))
			;
			bool bOperand = (Right.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Right, gsc_pQualifiers))
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
		for (auto pQualifier : gsc_pQualifiers)
			bAfterDeclarator |= fLeft(pQualifier) && !fLeft("&") && !fLeft("&&");

		if (bAfterDeclarator)
		{
			for (auto pQualifier : gsc_pQualifiers)
			{
				if (fRight(pQualifier))
					return ECodeSpacing::mc_Space;
			}
		}

		// A keyword and the name beside it are separated by one space: 'auto' and the name
		// it declares, 'template' and an instantiated name, 'return' and its operand. Two
		// plain names are not settled: a macro written on a line of its own inside a list
		// stands next to a name too, and keeps that line.
		if (Left.m_Kind == ECodeTokenKind::mc_Identifier && Right.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			static ch8 const *const gsc_pKeywords[] =
				{
					"auto", "template", "extern", "static", "inline", "constexpr", "consteval", "constinit", "virtual", "explicit"
					, "friend", "typename", "const", "volatile", "mutable", "struct", "class", "union", "enum", "namespace", "using"
					, "return", "co_return", "co_await", "co_yield", "throw", "new", "delete", "case", "goto", "sizeof", "alignof"
					, "void", "bool", "int", "char", "short", "long", "unsigned", "signed", "float", "double", "operator", "requires"
				}
			;
			for (auto pKeyword : gsc_pKeywords)
			{
				if (_Tokens.f_IsText(Left, pKeyword) || _Tokens.f_IsText(Right, pKeyword))
					return ECodeSpacing::mc_Space;
			}

			return ECodeSpacing::mc_Preserve;
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

			for (auto const &Node : _Structure.f_GetNodes())
			{
				if (Node.m_Kind == ECodeNodeKind::mc_Statement && Node.m_iLastToken == _iRight && Node.m_iFirstToken != _iRight)
					return ECodeSpacing::mc_None;
			}
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
					|| _Tokens.f_IsText(Before, ")")
					|| _Tokens.f_IsText(Before, "]")
					|| (_Structure.f_IsAngleBracket(umint(iBefore)) && _Tokens.f_IsText(Before, ">"))
				;
			}

			if (!bOperand)
				return ECodeSpacing::mc_None;
		}

		static ch8 const *const gsc_pBinaryOperators[] =
			{
				"==", "!=", "<=", ">=", "<=>", "||", "&&", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
				, "+", "-", "/", "%", "|", "^", "?", ":"
			}
		;
		for (auto pOperator : gsc_pBinaryOperators)
		{
			if (_Tokens.f_IsText(Left, pOperator) || _Tokens.f_IsText(Right, pOperator))
				return ECodeSpacing::mc_Space;
		}

		return ECodeSpacing::mc_Preserve;
	}
}
