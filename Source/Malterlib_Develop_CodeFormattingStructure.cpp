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

	CCodeStructure::CCodeStructure(CCodeTokenStream const &_Tokens)
		: mp_pTokens(&_Tokens)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
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

		auto const &Previous = Tokens[mp_Significant[_iToken - 1]];
		if (Previous.m_Kind != ECodeTokenKind::mc_Identifier)
			return 0;

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
		if (!fIsTightOrBroken(Previous.f_GetEnd(), Open.m_iOffset))
			return 0;

		if (_iToken + 1 < mp_Significant.f_GetLen() && !fIsTightOrBroken(Open.f_GetEnd(), Tokens[mp_Significant[_iToken + 1]].m_iOffset))
			return 0;

		umint nDepth = 0;
		umint nBrackets = 0;
		for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				continue;

			// A template argument can be a function type or an array bound, so a balanced
			// bracket group inside the list is skipped rather than ending the search.
			if (mp_pTokens->f_IsText(Token, "(") || mp_pTokens->f_IsText(Token, "["))
			{
				++nBrackets;

				continue;
			}

			if (nBrackets)
			{
				if (mp_pTokens->f_IsText(Token, ")") || mp_pTokens->f_IsText(Token, "]"))
					--nBrackets;

				continue;
			}

			if (mp_pTokens->f_IsText(Token, "<"))
				++nDepth;
			else if (mp_pTokens->f_IsText(Token, "<<"))
				nDepth += 2;
			else if (mp_pTokens->f_IsText(Token, ">"))
			{
				if (!--nDepth)
					return i;
			}
			else if (mp_pTokens->f_IsText(Token, ">>"))
			{
				// From the inner list's view a '>>' closes this level and leaves the other
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
	// lambda body. A braced initializer never has a statement terminator at its own level.
	bool CCodeStructure::fp_IsBlockBrace(umint _iToken) const
	{
		auto const &Tokens = mp_pTokens->f_GetTokens();
		umint nDepth = 0;
		for (auto i = _iToken; i < mp_Significant.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[mp_Significant[i]];
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

			// Closing two nested template argument lists is spelled as one '>>' token, so
			// the inner list stops on it and leaves the outer one to close there as well.
			if (_Bracket == ECodeBracket::mc_Angle && mp_pTokens->f_IsText(Token, ">>"))
			{
				mp_bAngleBracket[mp_Significant[i]] = 1;
				mp_nPendingAngleClose = 1;
				fp_Finish(iNode, mp_Significant[i]);

				return i;
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
				if (mp_nPendingAngleClose && _Bracket == ECodeBracket::mc_Angle)
				{
					--mp_nPendingAngleClose;
					fp_Finish(iNode, mp_Significant[i]);

					return i + 1;
				}

				continue;
			}

			if (fg_IsClosingBracket(*mp_pTokens, Token))
			{
				// The brackets do not nest as written, so this construct keeps its layout.
				mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
				fp_Finish(iNode, mp_Significant[i]);

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
		bool bConditional = false;
		auto const &First = Tokens[mp_Significant[_iToken]];
		bool bLabel = mp_pTokens->f_IsText(First, "case");
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

		// A template header and a requires clause each occupy their own line, so the
		// statement around them keeps its line structure even when it would fit on one.
		if (mp_pTokens->f_IsText(Tokens[mp_Significant[_iToken]], "template"))
			mp_Nodes[iNode].m_bFixedLineBreaks = true;

		while (i < mp_Significant.f_GetLen())
		{
			fp_Note(iNode, i);
			auto const &Token = Tokens[mp_Significant[i]];
			if (mp_pTokens->f_IsText(Token, "requires"))
				mp_Nodes[iNode].m_bFixedLineBreaks = true;

			if (mp_pTokens->f_IsText(Token, "?"))
				bConditional = true;

			// A label ends its statement: the body after it belongs on its own line.
			if (mp_pTokens->f_IsText(Token, ":") && !bConditional && (bLabel || i == _iToken + 1))
			{
				if (bLabel || Tokens[mp_Significant[_iToken]].m_Kind == ECodeTokenKind::mc_Identifier)
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
				auto const &First = Tokens[mp_Significant[_iToken]];
				bool bDefinition = mp_pTokens->f_IsText(First, "struct")
					|| mp_pTokens->f_IsText(First, "class")
					|| mp_pTokens->f_IsText(First, "union")
					|| mp_pTokens->f_IsText(First, "enum")
					|| mp_pTokens->f_IsText(First, "namespace")
				;
				bool bBlock = bAfterCloseParen || i == _iToken || bDefinition;
				if (!bBlock)
				{
					// A statement terminator at the brace's own level settles it wherever the
					// brace appears, including a body that follows a braced member initializer.
					auto const &Previous = Tokens[mp_Significant[i - 1]];
					bBlock = mp_pTokens->f_IsText(Previous, "else")
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
				if (i < mp_Significant.f_GetLen())
				{
					auto const &Next = Tokens[mp_Significant[i]];
					if (mp_pTokens->f_IsText(Next, ";"))
					{
						fp_Note(iNode, i);
						fp_Finish(iNode, mp_Significant[i]);

						return i + 1;
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
				// A '>>' that closed a nested list has no outer list here to close.
				mp_nPendingAngleClose = 0;
				bAfterCloseParen = false;

				continue;
			}

			if (fg_IsClosingBracket(*mp_pTokens, Token))
			{
				mp_Nodes[iNode].m_Kind = ECodeNodeKind::mc_Unsupported;
				fp_Finish(iNode, mp_Significant[i]);

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

namespace NMib::NDevelop
{
	// Decides the inline separator between two adjacent significant tokens. Only spellings
	// the standard settles are decided; everything else keeps whatever the source has, which
	// is what stops a rewrite from guessing at an ambiguous construct.
	ECodeSpacing fg_GetCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight)
	{
		auto const &Left = _Tokens.f_GetTokens()[_iLeft];
		auto const &Right = _Tokens.f_GetTokens()[_iRight];
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

		// A resolved template bracket hugs its arguments and separates the list from the
		// declarator after it. An unresolved '<' or '>' is an ordinary binary operator.
		if (_Structure.f_IsAngleBracket(_iRight))
			return ECodeSpacing::mc_None;

		if (_Structure.f_IsAngleBracket(_iLeft))
		{
			if (fLeft("<"))
				return ECodeSpacing::mc_None;

			if (Right.m_Kind == ECodeTokenKind::mc_Identifier)
				return ECodeSpacing::mc_Space;

			// A parameter list after a template argument's type spells a function type,
			// which the standard separates: TCActorFunctor<TCFuture<void> (CStr _Host)>.
			if (fRight("("))
				return ECodeSpacing::mc_Space;

			if (fRight("::") || fRight(",") || fRight(";") || fRight(")") || fRight("[") || fRight("]"))
				return ECodeSpacing::mc_None;

			return ECodeSpacing::mc_Preserve;
		}

		if (fLeft("<") || fRight(">") || fLeft(">"))
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

		// A keyword is separated from a parenthesis that follows it; a call name is not.
		// Operators spelled like a call, such as sizeof and decltype, stay tight.
		if (fRight("("))
		{
			static ch8 const *const gsc_pSpacedKeywords[] =
				{
					"if", "for", "while", "switch", "catch", "return", "co_return", "co_await", "co_yield"
					, "throw", "new", "delete", "case"
				}
			;
			for (auto pKeyword : gsc_pSpacedKeywords)
			{
				if (_Tokens.f_IsText(Left, pKeyword))
					return ECodeSpacing::mc_Space;
			}

			if (Left.m_Kind == ECodeTokenKind::mc_Identifier || fLeft(")") || fLeft("]"))
				return ECodeSpacing::mc_None;

			return ECodeSpacing::mc_Preserve;
		}

		// Member access and qualification never take spaces.
		if (fLeft(".") || fLeft("->") || fLeft("::") || fRight(".") || fRight("::"))
			return ECodeSpacing::mc_None;

		// '->' is a trailing return type as well as member access. After a parameter list
		// or a function's qualifiers it can only be the former, which takes a space.
		if (fRight("->"))
		{
			if (fLeft(")") || fLeft("const") || fLeft("volatile") || fLeft("noexcept") || fLeft("override") || fLeft("final"))
				return ECodeSpacing::mc_Space;

			return ECodeSpacing::mc_Preserve;
		}

		static ch8 const *const gsc_pBinaryOperators[] =
			{
				"==", "!=", "<=", ">=", "<=>", "||", "&&", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
				, "+", "/", "%", "|", "^", "?", ":"
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
