// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingStructurePrivate.h"

namespace NMib::NDevelop
{
	using namespace NPrivate;

	// Decides the inline separator between two adjacent significant tokens. Only spellings
	// the standard settles are decided; everything else keeps whatever the source has, which
	// is what stops a rewrite from guessing at an ambiguous construct.
	namespace
	{
		bool fg_IsSelectorPart(CCodeNode const &_Send, umint _iToken)
		{
			for (auto iPart : _Send.m_SplitPoints)
			{
				if (iPart == _iToken)
					return true;
			}

			return false;
		}

		// A message send writes its receiver and each selector part apart, a keyword against its ':' and the argument
		// behind it apart from the ':': '[pMenu addItem: pItem atIndex: 0]'. An object literal's '@' hugs its bracket,
		// and the selector '@selector' names is written as one word: '@selector(setObject:forKey:)'.
		NStorage::TCOptional<ECodeSpacing> fg_GetObjectiveCSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight)
		{
			auto const &Tokens = _Tokens.f_GetTokens();
			auto const &Left = Tokens[_iLeft];
			auto const &Right = Tokens[_iRight];
			auto const &Nodes = _Structure.f_GetNodes();

			if (_Tokens.f_IsText(Left, "@") && Left.m_Kind == ECodeTokenKind::mc_Punctuator)
				return ECodeSpacing::mc_None;

			if (_Tokens.f_IsText(Right, "(") && _Tokens.f_IsText(Left, "@selector"))
				return ECodeSpacing::mc_None;

			// The keywords spelled like a clause stand apart from their parenthesis: '@catch (NSException *_pException)'.
			if (_Tokens.f_IsText(Right, "(") && (_Tokens.f_IsText(Left, "@catch") || _Tokens.f_IsText(Left, "@synchronized") || _Tokens.f_IsText(Left, "@property")))
				return ECodeSpacing::mc_Space;

			// The index places a node's first and last tokens in the node around it, and a section's line or a
			// declaration ends at the token on the right.
			auto iNode = _Structure.f_FindStatementEndingAt(_iRight);
			auto const *pStatement = iNode < Nodes.f_GetLen() ? &Nodes[iNode] : nullptr;
			if (!pStatement || !(pStatement->m_bObjectiveCHeader || pStatement->m_bObjectiveCMethod))
				iNode = _Structure.f_FindEnclosingNode(_iRight);

			if (iNode >= Nodes.f_GetLen())
				return {};

			auto const &Node = Nodes[iNode];
			bool bTopLevel = _iLeft == Node.m_iFirstToken || _Structure.f_FindEnclosingNode(_iLeft) == iNode;

			// A section's line writes its base, category and protocols apart: '@interface CFoo (CBar) : NSObject <CDelegate>'.
			if (Node.m_bObjectiveCHeader && bTopLevel)
			{
				if (_Tokens.f_IsText(Left, ":") || _Tokens.f_IsText(Right, ":") || _Tokens.f_IsText(Right, "(") || _Tokens.f_IsText(Right, "<"))
					return ECodeSpacing::mc_Space;

				return {};
			}

			// A method writes its sign apart, a type in parentheses against the name behind it, and each keyword against its
			// ':' with the parameter apart from it: '- (void)f_Set: (int)_Value with: (int)_Other'.
			if (Node.m_bObjectiveCMethod && bTopLevel)
			{
				if (_iLeft == Node.m_iFirstToken)
					return ECodeSpacing::mc_Space;

				if (_Tokens.f_IsText(Left, ")") && Right.m_Kind == ECodeTokenKind::mc_Identifier)
					return ECodeSpacing::mc_None;

				if (_Tokens.f_IsText(Right, ":"))
					return ECodeSpacing::mc_None;

				if (_Tokens.f_IsText(Left, ":"))
					return ECodeSpacing::mc_Space;

				// A parameter's name and the keyword behind it.
				if (Left.m_Kind == ECodeTokenKind::mc_Identifier && Right.m_Kind == ECodeTokenKind::mc_Identifier)
					return ECodeSpacing::mc_Space;

				return {};
			}

			if (Node.m_Bracket == ECodeBracket::mc_Paren)
			{
				auto iBefore = fg_PreviousCode(_Tokens, Node.m_iFirstToken);
				if (iBefore >= 0 && _Tokens.f_IsText(Tokens[umint(iBefore)], "@selector"))
					return ECodeSpacing::mc_None;

				return {};
			}

			if (!Node.m_bMessageSend || !bTopLevel)
				return {};

			if (_Tokens.f_IsText(Right, ":") && fg_IsSelectorPart(Node, _iLeft))
				return ECodeSpacing::mc_None;

			if (_Tokens.f_IsText(Left, ":"))
			{
				auto iKeyword = fg_PreviousCode(_Tokens, _iLeft);
				if (iKeyword >= 0 && fg_IsSelectorPart(Node, umint(iKeyword)))
					return ECodeSpacing::mc_Space;
			}

			if (fg_IsSelectorPart(Node, _iRight) && _iLeft != Node.m_iFirstToken)
				return ECodeSpacing::mc_Space;

			return {};
		}
	}

	static ECodeSpacing fsg_DecideCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight);

	namespace
	{
		// Two punctuators written together read as one where they spell a longer one, ': :' as '::' and '+ +' as
		// '++', which is the token stream changing rather than its spacing. So does a lone '@' with what follows it, '@ x'
		// as the word '@x', and a number with a '.' or a name behind it, '0 .x' as '0.' and 'x'.
		bool fg_MergesTokens(CCodeTokenStream const &_Tokens, CCodeToken const &_Left, CCodeToken const &_Right)
		{
			// The '@' of an object literal hugs its bracket, which reads the same written apart: '@[', '@{', '@('.
			if (_Tokens.f_IsText(_Left, "@"))
				return _Right.m_Kind == ECodeTokenKind::mc_Identifier || _Right.m_Kind == ECodeTokenKind::mc_StringLiteral || _Right.m_Kind == ECodeTokenKind::mc_Number;

			if (_Left.m_Kind == ECodeTokenKind::mc_Number)
			{
				auto Next = _Tokens.f_GetTextPointer(_Right)[0];

				return Next == '.' || Next == '_' || fg_CharIsNumber(Next) || fg_CharIsAnsiAlphabetical(Next);
			}

			if (_Left.m_Kind != ECodeTokenKind::mc_Punctuator || _Right.m_Kind != ECodeTokenKind::mc_Punctuator)
				return false;

			constexpr ch8 const *c_pPunctuators[] =
				{
					"::"
					, "->*"
					, "->"
					, "++"
					, "--"
					, "<<="
					, ">>="
					, "<<"
					, ">>"
					, "<=>"
					, "<="
					, ">="
					, "=="
					, "!="
					, "&&"
					, "||"
					, "+="
					, "-="
					, "*="
					, "/="
					, "%="
					, "&="
					, "|="
					, "^="
					, ".*"
					, "..."
					, "##"
					, "//"
					, "/*"
				}
			;

			auto pLeft = _Tokens.f_GetTextPointer(_Left);
			auto pRight = _Tokens.f_GetTextPointer(_Right);

			for (auto pPunctuator : c_pPunctuators)
			{
				umint nPunctuator = 0;
				while (pPunctuator[nPunctuator])
					++nPunctuator;

				if (nPunctuator <= _Left.m_nLength || nPunctuator - _Left.m_nLength > _Right.m_nLength)
					continue;

				bool bMerges = true;
				for (umint i = 0; i < nPunctuator && bMerges; ++i)
					bMerges = pPunctuator[i] == (i < _Left.m_nLength ? pLeft[i] : pRight[i - _Left.m_nLength]);

				if (bMerges)
					return true;
			}

			return false;
		}
	}

	bool fg_CodeTokensMerge(CCodeTokenStream const &_Tokens, CCodeToken const &_Left, CCodeToken const &_Right)
	{
		return fg_MergesTokens(_Tokens, _Left, _Right);
	}

	ECodeSpacing fg_GetCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight)
	{
		auto Spacing = fsg_DecideCanonicalSpacing(_Tokens, _Structure, _iLeft, _iRight);
		if (Spacing != ECodeSpacing::mc_None)
			return Spacing;

		// The closers of nested template argument lists are written as one '>>' on purpose: the structure reads them apart.
		auto const &Tokens = _Tokens.f_GetTokens();
		if (_Structure.f_IsAngleBracket(_iLeft) && _Structure.f_IsAngleBracket(_iRight))
			return Spacing;

		return fg_MergesTokens(_Tokens, Tokens[_iLeft], Tokens[_iRight]) ? ECodeSpacing::mc_Space : Spacing;
	}

	static ECodeSpacing fsg_DecideCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight)
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

		// What an 'else' guards stands one space behind it: 'else if', 'else g();'.
		if (fLeft("else") && Left.m_Kind == ECodeTokenKind::mc_Identifier && (Right.m_Kind == ECodeTokenKind::mc_Identifier || fRight("{")))
			return ECodeSpacing::mc_Space;

		if (fRight("(") && fg_NamesOperator(_Tokens, _Structure, _iLeft))
			return ECodeSpacing::mc_Space;

		if (auto Spacing = fg_GetObjectiveCSpacing(_Tokens, _Structure, _iLeft, _iRight))
			return *Spacing;

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
			// A template header is the exception: 'template <typename t_CType>'. So is an operator's
			// name, which stands apart from its arguments as from anything: 'operator = <EJsonType>'.
			auto iOperator = fg_PreviousCode(_Tokens, _iLeft);
			if (fLeft("template") || (Left.m_Kind == ECodeTokenKind::mc_Punctuator && iOperator >= 0 && _Tokens.f_IsText(Tokens[umint(iOperator)], "operator")))
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
					"?"
					, "="
					, "=="
					, "!="
					, "<="
					, ">="
					, "<=>"
					, "||"
					, "|"
					, "^"
					, "+"
					, "-"
					, "/"
					, "%"
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

				return Parent.m_Kind == ECodeNodeKind::mc_Statement && Parent.m_iFirstToken == Node.m_iFirstToken && !Node.m_bList;
			}
		;

		if ((fLeft("{") && !fRight("}") && fIsRequirement(_iLeft)) || (fRight("}") && !fLeft("{") && fIsRequirement(_iRight)))
			return ECodeSpacing::mc_Space;

		// A block's braces are the layout's, which breaks them; where a construct keeps its lines they keep their spacing
		// too, as a list's spelling is no block's: 'template for (auto x : T) { f(x); }'.
		auto fIsBlockBrace = [&](umint _iBrace)
			{
				auto const &Nodes = _Structure.f_GetNodes();
				auto iNode = _Structure.f_FindNodeOpeningAt(_iBrace);
				if (iNode >= Nodes.f_GetLen())
					iNode = _Structure.f_FindNodeClosingAt(_iBrace);

				return iNode < Nodes.f_GetLen() && Nodes[iNode].m_Kind == ECodeNodeKind::mc_Block && Nodes[iNode].m_Bracket == ECodeBracket::mc_Brace;
			}
		;

		if (!(fLeft("{") && fRight("}")) && ((fLeft("{") && fIsBlockBrace(_iLeft)) || (fRight("}") && fIsBlockBrace(_iRight))))
			return ECodeSpacing::mc_Preserve;

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
			auto const *pBefore = iBefore >= 0 ? &Tokens[umint(iBefore)] : nullptr;
			auto const *pAfter = iAfter >= 0 ? &Tokens[umint(iAfter)] : nullptr;
			auto const &Slash = Tokens[iSlash];
			bool bTight
				= pBefore
				&& pAfter
				&& pBefore->m_Kind == ECodeTokenKind::mc_Identifier
				&& pAfter->m_Kind == ECodeTokenKind::mc_Identifier
				&& pBefore->f_GetEnd() == Slash.m_iOffset
				&& Slash.f_GetEnd() == pAfter->m_iOffset
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
			auto const *pMarker = iMarker >= 0 ? &Tokens[umint(iMarker)] : nullptr;
			auto const *pKey = iKey >= 0 ? &Tokens[umint(iKey)] : nullptr;
			bool bMarker = pMarker && pMarker->m_Kind == ECodeTokenKind::mc_Identifier && fg_IsDSLMarker(_Tokens, *pMarker);
			bool bKeyed = pKey && pKey->m_Kind == ECodeTokenKind::mc_StringLiteral && pKey->f_GetEnd() == pMarker->m_iOffset;
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
			// Behind a parameter list or a function's qualifier 'throw' is an exception specification,
			// spelled like 'noexcept': 'f(size_t _Size) throw()'.
			if (fLeft("throw"))
			{
				auto iSpecified = fg_PreviousCode(_Tokens, _iLeft);
				auto const *pSpecified = iSpecified >= 0 ? &Tokens[umint(iSpecified)] : nullptr;
				bool bQualifier
					= pSpecified
					&& (_Tokens.f_IsText(*pSpecified, "const") || _Tokens.f_IsText(*pSpecified, "volatile") || _Tokens.f_IsText(*pSpecified, "&") || _Tokens.f_IsText(*pSpecified, "&&"))
				;
				if (bQualifier || (iSpecified >= 0 && fg_ClosesParameterList(_Tokens, _Structure, umint(iSpecified))))
					return ECodeSpacing::mc_None;
			}

			constexpr ch8 const *c_pSpacedKeywords[] =
				{
					"if"
					, "for"
					, "while"
					, "switch"
					, "catch"
					, "return"
					, "co_return"
					, "co_await"
					, "co_yield"
					, "throw"
					, "delete"
					, "case"
					, "requires"
					, "constexpr"
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
				bool bBlock
					= iCaret >= 0
					&& _Tokens.f_IsText(Tokens[umint(iCaret)], "^")
					&& iDeclarator < _Structure.f_GetNodes().f_GetLen()
					&& fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator)
				;
				if (bBlock)
					return ECodeSpacing::mc_Space;

				// So does a pointer to function's in a 'typedef', where the name in front can only be the type it returns:
				// 'typedef LONG (NTAPI *PHandler)(ULONG64 _Frame)'.
				auto iStatement = fg_FindEnclosingNode(_Structure, _iLeft);
				auto const &Nodes = _Structure.f_GetNodes();
				bool bTypedef
					= iStatement < Nodes.f_GetLen()
					&& Nodes[iStatement].m_Kind == ECodeNodeKind::mc_Statement
					&& _Tokens.f_IsText(Tokens[Nodes[iStatement].m_iFirstToken], "typedef")
				;
				if (bTypedef && iDeclarator < Nodes.f_GetLen() && fg_IsFunctionPointerDeclarator(_Tokens, _Structure, iDeclarator))
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
				auto fIsDeclaratorPart = [&](aint _iToken)
					{
						auto const &Token = Tokens[umint(_iToken)];

						return fg_IsDeclaratorText(_Tokens, Token) || _Tokens.f_IsText(Token, "const");
					}
				;

				auto iType = fg_PreviousCode(_Tokens, _iLeft);
				while (iType >= 0 && fIsDeclaratorPart(iType))
					iType = fg_PreviousCode(_Tokens, umint(iType));

				bool bType = iType >= 0 && fg_NamesType(_Tokens, Tokens[umint(iType)]);
				bool bFunctionTypePlace = false;
				if (bType)
				{
					auto iGroup = fg_FindEnclosingNode(_Structure, _iLeft);
					auto const &Nodes = _Structure.f_GetNodes();
					auto const *pGroup = iGroup < Nodes.f_GetLen() ? &Nodes[iGroup] : nullptr;
					if (pGroup && pGroup->m_Kind == ECodeNodeKind::mc_Group && pGroup->m_Bracket == ECodeBracket::mc_Angle)
						bFunctionTypePlace = true;
					else if (pGroup && pGroup->m_Kind == ECodeNodeKind::mc_Statement)
					{
						auto iUsing = pGroup->m_iFirstToken;
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
						auto const *pInner = iInner >= 0 ? &Tokens[umint(iInner)] : nullptr;
						auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
						bool bDeclarator
							= pInner
							&& (_Tokens.f_IsText(*pInner, "*") || _Tokens.f_IsText(*pInner, "&"))
							&& pBehind
							&& (_Tokens.f_IsText(*pBehind, "(") || _Tokens.f_IsText(*pBehind, "["))
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
					auto const *pInner = iInner >= 0 ? &Tokens[umint(iInner)] : nullptr;

					// A calling convention only stands in a declarator: 'NTSTATUS (__stdcall *m_pLoad)(PWCHAR _pPath)'.
					bool bConvention = false;
					if (pInner && pInner->m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, *pInner, gc_pExpressionKeywords))
					{
						auto iStar = fg_NextCode(_Tokens, umint(iInner));
						constexpr ch8 const *c_pConventions[] =
							{
								"__stdcall"
								, "__cdecl"
								, "__fastcall"
								, "__thiscall"
								, "__vectorcall"
							}
						;

						bConvention = fg_IsAnyText(_Tokens, *pInner, c_pConventions);
						auto const *pStar = iStar >= 0 ? &Tokens[umint(iStar)] : nullptr;
						if (pStar && (_Tokens.f_IsText(*pStar, "*") || _Tokens.f_IsText(*pStar, "&")))
							pInner = pStar;
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
								auto const *pAfter = iAfter >= 0 ? &Tokens[umint(iAfter)] : nullptr;
								if (pAfter && _Tokens.f_IsText(*pAfter, "*"))
								{
									pInner = pAfter;

									break;
								}

								if (!pAfter || pAfter->m_Kind != ECodeTokenKind::mc_Identifier)
									break;

								iScope = umint(iAfter);
							}
						}
					}

					// A pointer or a reference to an array is followed by the bound instead: 'ch8
					// (&_Dest)[t_Size]'. Behind a name the naming lists as a type's, which cannot be
					// called, the declarator stands apart from it.
					auto iBehind = fg_NextCode(_Tokens, Node.m_iLastToken);
					auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
					bool bDeclarator
						= pInner
						&& (_Tokens.f_IsText(*pInner, "*") || _Tokens.f_IsText(*pInner, "&"))
						&& pBehind
						&& (_Tokens.f_IsText(*pBehind, "(") || _Tokens.f_IsText(*pBehind, "["))
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

					// A function's name, or a member's, is called with the address: 'Actor.f_CallActor(&CFoo::f_Get)()'. A
					// trailing return type's arrow accesses no member: '-> void (*)(int)'.
					auto iAccess = fg_PreviousCode(_Tokens, _iLeft);
					auto const *pAccess = iAccess >= 0 ? &Tokens[umint(iAccess)] : nullptr;
					bool bCalled
						= fg_NamesFunction(_Tokens, Left)
						||
						(
							pAccess
							&&
							(
								_Tokens.f_IsText(*pAccess, ".")
								|| (_Tokens.f_IsText(*pAccess, "->") && !fg_IsTrailingReturnArrow(_Tokens, _Structure, umint(iAccess)))
							)
						)
					;
					if (bDeclarator && bCalled)
						return ECodeSpacing::mc_None;

					if (bDeclarator && bConvention)
						return ECodeSpacing::mc_Space;

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
		// hugs its operands. Behind 'operator' it is a function's name, spaced as the others are: 'operator -> == p'.
		auto iBeforeArrow = fLeft("->") ? fg_PreviousCode(_Tokens, _iLeft) : aint(-1);
		if (fLeft("->") && (iBeforeArrow < 0 || !_Tokens.f_IsText(Tokens[umint(iBeforeArrow)], "operator")))
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
			auto const *pClass = iClass >= 0 ? &Tokens[umint(iClass)] : nullptr;
			bool bClass = pClass && (pClass->m_Kind == ECodeTokenKind::mc_Identifier || (_Structure.f_IsAngleBracket(umint(iClass)) && _Tokens.f_IsText(*pClass, ">")));

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
		// 'using ::size_t', 'typedef ::CFoo CBar', 'return ::fg_Get()', 'class CFoo : public ::NA::CBase'.
		if (fRight("::") && Left.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			constexpr ch8 const *c_pDeclarationKeywords[] =
				{
					"using"
					, "typedef"
					, "typename"
					, "namespace"
					, "const"
					, "volatile"
					, "static"
					, "constexpr"
					, "consteval"
					, "constinit"
					, "inline"
					, "virtual"
					, "friend"
					, "extern"
					, "struct"
					, "class"
					, "enum"
					, "union"
					, "mutable"
					, "thread_local"
					, "public"
					, "private"
					, "protected"
				}
			;

			if (fg_IsAnyText(_Tokens, Left, gc_pExpressionKeywords) || fg_IsAnyText(_Tokens, Left, c_pDeclarationKeywords))
				return ECodeSpacing::mc_Space;
		}

		// So does one behind an operator that only stands between operands: 'Value += ::fg_Get()'.
		if (fRight("::"))
		{
			constexpr ch8 const *c_pBinaryOperators[] =
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
					, "=="
					, "!="
					, "<="
					, ">="
					, "<=>"
					, "&&"
					, "||"
					, "?"
				}
			;

			if (fg_IsAnyText(_Tokens, Left, c_pBinaryOperators))
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
						"this"
						, "auto"
						, "const"
						, "volatile"
						, "typename"
						, "struct"
						, "class"
						, "enum"
						, "static"
						, "constexpr"
						, "inline"
					}
				;

				auto iBehind = fg_NextCode(_Tokens, _iRight);
				bool bDirective = false;
				for (auto i = _iRight + 1; iBehind >= 0 && i < umint(iBehind); ++i)
					bDirective |= Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor;

				auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
				bool bName
					= pBehind
					&& !bDirective
					&& pBehind->m_Kind == ECodeTokenKind::mc_Identifier
					&& !fg_IsAnyText(_Tokens, *pBehind, gc_pExpressionKeywords)
					&& !fg_IsAnyText(_Tokens, *pBehind, c_pNoName)
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
			return fg_DeclaresPack(_Tokens, _iRight) ? ECodeSpacing::mc_Space : ECodeSpacing::mc_None;

		if (fLeft("...") && Right.m_Kind == ECodeTokenKind::mc_Identifier)
			return ECodeSpacing::mc_None;

		// What follows a parameter list is the function's qualifiers and specifiers, and
		// they are separated from it and from each other. Without a spelling for these the
		// declaration cannot be measured as one line, and so could never be joined.
		constexpr ch8 const *c_pQualifiers[] =
			{
				"const"
				, "volatile"
				, "noexcept"
				, "override"
				, "final"
				, "mutable"
				, "requires"
				, "&"
				, "&&"
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
						"new"
						, "alignas"
						, "decltype"
						, "__attribute__"
						, "__attribute"
						, "__declspec"
					}
				;

				// A macro's argument list with a name behind it spells a type or a prefix for
				// what follows: 'DMibFloatConstexprCache(TCFloat) Cache'.
				auto iKeyword = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);
				auto const *pKeyword = iKeyword >= 0 ? &Tokens[umint(iKeyword)] : nullptr;
				bool bApart
					= pKeyword && (fg_IsAnyText(_Tokens, *pKeyword, c_pApart) || (Right.m_Kind == ECodeTokenKind::mc_Identifier && _Tokens.f_HasRole(*pKeyword, ECodeNameRole::mc_Macro)))
				;
				if (bApart && !fg_IsAnyText(_Tokens, Right, c_pQualifiers))
					return ECodeSpacing::mc_Space;
			}

			// An attribute behind a parameter list stands apart from it: '(void *) __attribute__((weak))'.
			if ((fRight("__attribute__") || fRight("__attribute") || fRight("__declspec")) && fg_ClosesParameterList(_Tokens, _Structure, _iLeft))
				return ECodeSpacing::mc_Space;

			// An attribute macro behind a parameter list, in front of the trailing return type,
			// stands apart from both: '(int _A) DMibSuppressUndefinedSanitizer -> void *'.
			auto iArrow = fg_NextCode(_Tokens, _iRight);
			bool bAttribute
				= Right.m_Kind == ECodeTokenKind::mc_Identifier
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
				bCast
					= iOpen >= 0
					&& _Tokens.f_IsText(Tokens[umint(iOpen)], "(")
					&& fg_NamesType(_Tokens, Tokens[umint(iInner)])
					&& !(iBeforeOpen >= 0 && Tokens[umint(iBeforeOpen)].m_Kind == ECodeTokenKind::mc_Identifier)
				;
			}

			bool bOperand
				= (Right.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Right, c_pQualifiers))
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
		// plain names are settled on one line, where one space separates them however the
		// source aligned them, and at a statement's own level, where they spell a declaration
		// written across lines: 'CResult' over 'fg_Run(...)'. A macro written on a line of its
		// own inside a list or an enum stands next to a name too, and keeps that line.
		if (Left.m_Kind == ECodeTokenKind::mc_Identifier && Right.m_Kind == ECodeTokenKind::mc_Identifier)
		{
			constexpr ch8 const *c_pKeywords[] =
				{
					"auto"
					, "template"
					, "extern"
					, "static"
					, "inline"
					, "constexpr"
					, "consteval"
					, "constinit"
					, "virtual"
					, "explicit"
					, "friend"
					, "typename"
					, "const"
					, "volatile"
					, "mutable"
					, "struct"
					, "class"
					, "union"
					, "enum"
					, "namespace"
					, "using"
					, "return"
					, "co_return"
					, "co_await"
					, "co_yield"
					, "throw"
					, "new"
					, "delete"
					, "case"
					, "goto"
					, "sizeof"
					, "alignof"
					, "void"
					, "bool"
					, "int"
					, "char"
					, "short"
					, "long"
					, "unsigned"
					, "signed"
					, "float"
					, "double"
					, "operator"
					, "requires"
				}
			;

			for (auto pKeyword : c_pKeywords)
			{
				if (_Tokens.f_IsText(Left, pKeyword) || _Tokens.f_IsText(Right, pKeyword))
					return ECodeSpacing::mc_Space;
			}

			bool bOneLine = true;
			for (auto i = _iLeft + 1; i < _iRight && bOneLine; ++i)
				bOneLine = Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace;

			if (bOneLine)
				return ECodeSpacing::mc_Space;

			auto const &Nodes = _Structure.f_GetNodes();
			auto iStatement = _Structure.f_FindEnclosingNode(_iRight);
			auto const *pStatement = iStatement < Nodes.f_GetLen() ? &Nodes[iStatement] : nullptr;
			if (!pStatement || pStatement->m_Kind != ECodeNodeKind::mc_Statement || pStatement->m_iFirstToken > _iLeft)
				return ECodeSpacing::mc_Preserve;

			// The name below is a declarator's, followed by its parameter list or qualified, and the one above no macro
			// the naming lists, nor one in front of a statement in a function's body, where calls stand too.
			auto iBehind = fg_NextCode(_Tokens, _iRight);
			auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
			bool bDeclarator = pBehind && (_Tokens.f_IsText(*pBehind, "(") || _Tokens.f_IsText(*pBehind, "::"));
			auto iBody = pStatement->m_iParent;
			bool bEnumBody = _Structure.f_IsEnumBody(iBody);
			bool bSettled
				= bDeclarator
				&& !bEnumBody
				&& !_Tokens.f_HasRole(Left, ECodeNameRole::mc_Macro)
				&& !(Nodes[iBody].m_Kind == ECodeNodeKind::mc_Block && fg_IsFunctionBody(_Tokens, _Structure, iBody))
			;

			return bSettled ? ECodeSpacing::mc_Space : ECodeSpacing::mc_Preserve;
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
				bOperand
					= Before.m_Kind == ECodeTokenKind::mc_Number
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

		// A cast hugs the block literal it converts, as it does any operand: '(void)^(int _A) { ... }'.
		if (fLeft(")") && fg_IsBlockLiteralCaret(_Tokens, _Structure, _iRight) && fg_IsCast(_Tokens, _Structure, _iLeft))
			return ECodeSpacing::mc_None;

		// Behind a cast a '*' or '&' is unary and hugs its operand: '(void *)*pJumpBuffer'.
		if (fLeft("*") || fLeft("&"))
		{
			auto iBefore = fg_PreviousCode(_Tokens, _iLeft);
			if (iBefore >= 0 && _Tokens.f_IsText(Tokens[umint(iBefore)], ")") && fg_IsCast(_Tokens, _Structure, umint(iBefore)))
				return ECodeSpacing::mc_None;
		}

		// A type's definition is followed by the declarators it declares: '} CFoo, *pFoo;'.
		if (fLeft("}") && (Right.m_Kind == ECodeTokenKind::mc_Identifier || fRight("*") || fRight("&")))
		{
			auto const &Nodes = _Structure.f_GetNodes();
			auto iBlock = _Structure.f_FindNodeClosingAt(_iLeft);
			auto const *pBlock = iBlock < Nodes.f_GetLen() ? &Nodes[iBlock] : nullptr;
			auto iOwner = pBlock && pBlock->m_Kind == ECodeNodeKind::mc_Block ? pBlock->m_iParent : Nodes.f_GetLen();
			auto const *pOwner = iOwner < Nodes.f_GetLen() ? &Nodes[iOwner] : nullptr;
			if (pOwner && pOwner->m_Kind == ECodeNodeKind::mc_Statement && pOwner->m_iLastToken > _iLeft)
			{
				auto iKey = aint(pOwner->m_iFirstToken);
				if (_Tokens.f_IsText(Tokens[umint(iKey)], "typedef"))
					iKey = fg_NextCode(_Tokens, umint(iKey));

				if (iKey >= 0 && fg_IsAnyText(_Tokens, Tokens[umint(iKey)], {"struct", "class", "union", "enum"}))
					return ECodeSpacing::mc_Space;
			}
		}

		constexpr ch8 const *c_pBinaryOperators[] =
			{
				"=="
				, "!="
				, "<="
				, ">="
				, "<=>"
				, "||"
				, "&&"
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
				, "+"
				, "-"
				, "/"
				, "%"
				, "|"
				, "^"
				, "?"
				, ":"
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
