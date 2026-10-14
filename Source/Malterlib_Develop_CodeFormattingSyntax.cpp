// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingStructurePrivate.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

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

			auto const &Token = Tokens[i];
			if (_Tokens.f_IsText(Token, "struct") || _Tokens.f_IsText(Token, "class") || _Tokens.f_IsText(Token, "union"))
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
		bool bClause = false;
		for (auto i = iFirst; i <= _iLast; ++i)
		{
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
				continue;

			// A requires clause's constraint joins its parts with logical operators, and parenthesizes them.
			bClause |= _Tokens.f_IsText(Token, "requires");
			if (bClause && (_Tokens.f_IsText(Token, "||") || _Tokens.f_IsText(Token, "!")))
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
						auto const &HeaderToken = Tokens[i];
						if (_Tokens.f_IsText(HeaderToken, "<"))
							++nDepth;
						else if (_Tokens.f_IsText(HeaderToken, ">"))
							--nDepth;
						else if (_Tokens.f_IsText(HeaderToken, ">>"))
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
					auto const *pBeforeChild = Child.m_Bracket == ECodeBracket::mc_Paren && iBeforeChild >= 0 ? &Tokens[umint(iBeforeChild)] : nullptr;
					bool bRequires
						= pBeforeChild
						&&
						(
							_Tokens.f_IsText(*pBeforeChild, "requires")
							||
							(
								bClause
								&&
								(
									_Tokens.f_IsText(*pBeforeChild, "&&")
									|| _Tokens.f_IsText(*pBeforeChild, "||")
									|| _Tokens.f_IsText(*pBeforeChild, "!")
								)
							)
						)
					;

					// A macro the naming lists expands to the type a declarator follows, as 'decltype' does.
					bool bDecltype = pBeforeChild && (_Tokens.f_IsText(*pBeforeChild, "decltype") || _Tokens.f_HasRole(*pBeforeChild, ECodeNameRole::mc_Macro));
					// An attribute qualifies the type in front of it: 'umint __attribute__((address_space(256))) *pAddress'.
					bool bAttribute
						= pBeforeChild
						&& (_Tokens.f_IsText(*pBeforeChild, "__attribute__") || _Tokens.f_IsText(*pBeforeChild, "__attribute") || _Tokens.f_IsText(*pBeforeChild, "__declspec"))
					;
					bool bTypePart = Child.m_Bracket == ECodeBracket::mc_Angle || Child.m_Bracket == ECodeBracket::mc_Square || bRequires || bDecltype || bAttribute;
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
		bool bBehindType
			= (Type.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Type, gc_pExpressionKeywords))
			|| (_Structure.f_IsAngleBracket(umint(iType)) && _Tokens.f_IsText(Type, ">"))
			|| fg_IsDeclaratorText(_Tokens, Type)
		;
		if (!bBehindType)
			return false;

		auto iInner = fg_NextCode(_Tokens, Node.m_iFirstToken);
		auto const *pInner = iInner >= 0 ? &Tokens[umint(iInner)] : nullptr;
		if (pInner && pInner->m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, *pInner, gc_pExpressionKeywords))
		{
			iInner = fg_NextCode(_Tokens, umint(iInner));
			pInner = iInner >= 0 ? &Tokens[umint(iInner)] : nullptr;
		}

		if (!pInner || (!_Tokens.f_IsText(*pInner, "*") && !_Tokens.f_IsText(*pInner, "&")))
			return false;

		auto iBehind = fg_NextCode(_Tokens, _iClose);

		return iBehind >= 0 && _Tokens.f_IsText(Tokens[umint(iBehind)], "(");
	}

	// A parenthesis that holds nothing but fundamental type words, and stands where no call
	// or subscript could have produced it, is a cast: '(smint)-1', '(unsigned int)x'. The
	// same parenthesis behind 'sizeof' is the operator's operand, '(sizeof(int) - 1)'.
	bool fg_ClosesFundamentalCast(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		constexpr ch8 const *c_pFundamental[] =
			{
				"void"
				, "bool"
				, "char"
				, "short"
				, "int"
				, "long"
				, "unsigned"
				, "signed"
				, "float"
				, "double"
				, "const"
				, "volatile"
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
			bool bCalled
				= (Before.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords))
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
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
				continue;

			if (!fg_IsAnyText(_Tokens, Token, c_pFundamental) && !_Tokens.f_HasRole(Token, ECodeNameRole::mc_Type))
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
			bool bOperand
				= BeforeOpen.m_Kind == ECodeTokenKind::mc_Identifier
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
}

namespace NMib::NDevelop::NPrivate
{
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

		// Nor does the opening of a conversion type's argument list: 'operator TCFoo<(c_A > 1)>'.
		if (_Tokens.f_IsText(Tokens[umint(i)], "<") && _Structure.f_IsAngleBracket(umint(i)))
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

		// A conversion type's argument list is stepped over whole, whatever it holds.
		auto fSkipArguments = [&](aint _iToken) -> aint
			{
				if (!_Tokens.f_IsText(Tokens[umint(_iToken)], ">") || !_Structure.f_IsAngleBracket(umint(_iToken)))
					return _iToken;

				auto iList = fg_FindGroupClosingAt(_Structure, umint(_iToken), ECodeBracket::mc_Angle);

				return iList >= 0 ? aint(_Structure.f_GetNodes()[umint(iList)].m_iFirstToken) : _iToken;
			}
		;

		i = fSkipArguments(i);
		for (umint nSteps = 0; nSteps < 16; ++nSteps)
		{
			auto iPrevious = fg_PreviousCode(_Tokens, umint(i));
			if (iPrevious < 0)
				return false;

			iPrevious = fSkipArguments(iPrevious);
			auto const &Previous = Tokens[umint(iPrevious)];
			if (_Tokens.f_IsText(Previous, "operator"))
				return true;

			// Only what a name is made of stands between the keyword and the list.
			bool bName
				= Previous.m_Kind == ECodeTokenKind::mc_Identifier
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

		auto fIsCvQualifier = [&](aint _iToken)
			{
				auto const &Token = Tokens[umint(_iToken)];

				return _Tokens.f_IsText(Token, "const") || _Tokens.f_IsText(Token, "volatile");
			}
		;

		auto iBefore = fg_PreviousCode(_Tokens, _iToken);
		while (iBefore >= 0 && fIsCvQualifier(iBefore))
			iBefore = fg_PreviousCode(_Tokens, umint(iBefore));

		return iBefore >= 0 && _Tokens.f_IsText(Tokens[umint(iBefore)], ")");
	}

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

		// The standard library's aliases of fundamental types are spelled by the language as well.
		constexpr ch8 const *c_pFundamental[] =
			{
				"bool"
				, "char"
				, "short"
				, "int"
				, "long"
				, "unsigned"
				, "signed"
				, "float"
				, "double"
				, "void"
				, "wchar_t"
				, "char8_t"
				, "char16_t"
				, "char32_t"
				, "size_t"
				, "ssize_t"
				, "ptrdiff_t"
				, "intptr_t"
				, "uintptr_t"
				, "intmax_t"
				, "uintmax_t"
				, "nullptr_t"
				, "max_align_t"
				, "int8_t"
				, "int16_t"
				, "int32_t"
				, "int64_t"
				, "uint8_t"
				, "uint16_t"
				, "uint32_t"
				, "uint64_t"
			}
		;

		// C's character types are typedefs a C file reads as plain names, which a cast still names as types: '(wchar_t)-1'.
		if (_Token.m_bPlainName)
		{
			auto Text = _Tokens.f_GetText(_Token);
			return Text == "wchar_t" || Text == "char8_t" || Text == "char16_t" || Text == "char32_t";
		}

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

	// Whether the parenthesis is a pointer to function's declarator: a calling convention, keyword or
	// macro, or none, declarators, and the name or none, with the parameter list or the bound behind it:
	// '(DMibCrossmoduleAPI *m_fAlloc)(umint _Size)', '(*)(int)', '(&_Array)[4]'.
	bool fg_IsFunctionPointerDeclarator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Group = Nodes[_iGroup];
		if (Group.m_Kind != ECodeNodeKind::mc_Group || Group.m_Bracket != ECodeBracket::mc_Paren || !Group.m_SplitPoints.f_IsEmpty())
			return false;

		auto iBehind = fg_NextCode(_Tokens, Group.m_iLastToken);
		auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
		if (!pBehind || (!_Tokens.f_IsText(*pBehind, "(") && !_Tokens.f_IsText(*pBehind, "[")))
			return false;

		constexpr ch8 const *c_pConventions[] =
			{
				"__stdcall"
				, "__cdecl"
				, "__fastcall"
				, "__thiscall"
				, "__vectorcall"
			}
		;

		auto fIsConvention = [&](aint _iToken)
			{
				auto const &Token = Tokens[umint(_iToken)];

				return Token.m_Kind == ECodeTokenKind::mc_Identifier && (_Tokens.f_HasRole(Token, ECodeNameRole::mc_Macro) || fg_IsAnyText(_Tokens, Token, c_pConventions));
			}
		;

		auto i = fg_NextCode(_Tokens, Group.m_iFirstToken);
		while (i >= 0 && umint(i) < Group.m_iLastToken && fIsConvention(i))
			i = fg_NextCode(_Tokens, umint(i));

		// A block pointer, Clang's extension, is declared with '^' where a pointer to function has '*':
		// 'void (^fReport)(void *_pMemory)'.
		auto fIsDeclarator = [&](aint _iToken)
			{
				auto const &Token = Tokens[umint(_iToken)];

				return fg_IsDeclaratorText(_Tokens, Token) || _Tokens.f_IsText(Token, "^");
			}
		;

		bool bDeclarator = false;
		while (i >= 0 && umint(i) < Group.m_iLastToken && fIsDeclarator(i))
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
					"="
					, "("
					, ","
					, "||"
					, "&&"
					, "!"
					, "?"
					, ":"
					, "return"
					, "requires"
				}
			;

			auto iLead = fg_PreviousCode(_Tokens, umint(iName));
			auto iBehind = fg_NextCode(_Tokens, Group.m_iLastToken);

			return iLead >= 0 && fg_IsAnyText(_Tokens, Tokens[umint(iLead)], c_pOperandLeads) && iBehind >= 0 && _Tokens.f_IsText(Tokens[umint(iBehind)], "{");
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
		auto const *pParent = Group.m_iParent < Nodes.f_GetLen() ? &Nodes[Group.m_iParent] : nullptr;
		if (pParent && pParent->m_Kind == ECodeNodeKind::mc_Group && pParent->m_Bracket == ECodeBracket::mc_Angle)
		{
			auto const &Name = Tokens[umint(iName)];

			// A fundamental type's keyword is written apart from the parenthesis whatever it spells, so it is read as
			// the function type it is written as: 'TCFoo<int(int) &>' as 'TCFoo<int (int) &>'.
			constexpr ch8 const *c_pFundamental[] =
				{
					"bool"
					, "char"
					, "short"
					, "int"
					, "long"
					, "unsigned"
					, "signed"
					, "float"
					, "double"
					, "void"
					, "wchar_t"
					, "char8_t"
					, "char16_t"
					, "char32_t"
				}
			;

			bool bApart = Name.f_GetEnd() != Tokens[Group.m_iFirstToken].m_iOffset || fg_IsAnyText(_Tokens, Name, c_pFundamental);

			// Two names in a row in it declare a parameter, which no argument of a call does however the layout breaks the
			// parenthesis from its name: 'function_ref<CResult(CPoint _Point, CValue &_Value)>'.
			bool bDeclares = false;
			for (auto i = Group.m_iFirstToken + 1; i < Group.m_iLastToken && !bDeclares; ++i)
			{
				auto const &Token = Tokens[i];
				if (Token.m_Kind != ECodeTokenKind::mc_Identifier || _Structure.f_FindEnclosingNode(i) != _iGroup || fg_IsAnyText(_Tokens, Token, gc_pExpressionKeywords))
					continue;

				auto iNext = fg_NextCode(_Tokens, i);
				auto const *pNext = iNext >= 0 && umint(iNext) < Group.m_iLastToken ? &Tokens[umint(iNext)] : nullptr;
				bDeclares = pNext && pNext->m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, *pNext, gc_pExpressionKeywords);
			}

			bool bType = (bApart || bDeclares) && !fg_NamesCall(_Tokens, _Structure, umint(iName));
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
		auto const &Name = Tokens[umint(iName)];
		if (Name.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Name, gc_pExpressionKeywords))
		{
			auto iBehind = fg_PreviousCode(_Tokens, umint(iName));
			auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
			bool bBehindList = pBehind && (_Tokens.f_IsText(*pBehind, "]") || (_Structure.f_IsAngleBracket(umint(iBehind)) && _Tokens.f_IsText(*pBehind, ">")));
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
			auto const *pBeforeChild = iBeforeChild >= 0 ? &Tokens[umint(iBeforeChild)] : nullptr;
			bool bOperand
				= pBeforeChild
				&&
				(
					fg_IsAnyText(_Tokens, *pBeforeChild, gc_pExpressionKeywords)
					|| _Tokens.f_IsText(*pBeforeChild, "operator")
					|| _Tokens.f_IsText(*pBeforeChild, "requires")
					|| _Tokens.f_IsText(*pBeforeChild, "__attribute__")
					|| _Tokens.f_IsText(*pBeforeChild, "__attribute")
					|| _Tokens.f_IsText(*pBeforeChild, "__declspec")
				)
			;
			// So does one behind a constraint's operator in a requires clause: 'requires cA<t_C> && (cB<t_C>)'.
			if (!bOperand && pBeforeChild && (_Tokens.f_IsText(*pBeforeChild, "&&") || _Tokens.f_IsText(*pBeforeChild, "||")))
			{
				for (auto i = Parent.m_iFirstToken; i < umint(iBeforeChild) && !bOperand; ++i)
					bOperand = _Tokens.f_IsText(Tokens[i], "requires") && _Structure.f_FindEnclosingNode(i) == Group.m_iParent;
			}

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
		// is spelled with, and such a function has one wherever it is declared. One named through a member access is
		// called: 'this->operator -> ()'.
		if (fg_NamesOperator(_Tokens, _Structure, umint(iName)))
		{
			auto iOperator = iName;
			for (umint iStep = 0; iOperator >= 0 && iStep < 4 && !_Tokens.f_IsText(Tokens[umint(iOperator)], "operator"); ++iStep)
				iOperator = fg_PreviousCode(_Tokens, umint(iOperator));

			auto iAccess = iOperator >= 0 && _Tokens.f_IsText(Tokens[umint(iOperator)], "operator") ? fg_PreviousCode(_Tokens, umint(iOperator)) : aint(-1);

			auto const *pAccess = iAccess >= 0 ? &Tokens[umint(iAccess)] : nullptr;

			return !pAccess || (!_Tokens.f_IsText(*pAccess, ".") && !_Tokens.f_IsText(*pAccess, "->"));
		}

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
				"{"
				, "const"
				, "volatile"
				, "noexcept"
				, "override"
				, "final"
				, "mutable"
				, "requires"
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

		// The children stand in the order of their tokens, so one cursor passes them along with the tokens.
		auto const &Node = Nodes[iNode];
		umint iChild = 0;
		for (auto i = Node.m_iFirstToken; i < _iColon; ++i)
		{
			while (iChild < Node.m_Children.f_GetLen() && Nodes[Node.m_Children[iChild]].m_iLastToken < i)
				++iChild;

			if (iChild < Node.m_Children.f_GetLen() && Nodes[Node.m_Children[iChild]].m_iFirstToken <= i)
			{
				i = Nodes[Node.m_Children[iChild]].m_iLastToken;

				continue;
			}

			if (_Tokens.f_IsText(Tokens[i], "?"))
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
				"case"
				, "default"
				, "public"
				, "private"
				, "protected"
				, "struct"
				, "class"
				, "union"
				, "enum"
				, "namespace"
				, "template"
				, "using"
				, "friend"
				, "operator"
			}
		;

		if (fg_IsAnyText(_Tokens, First, c_pOther) || fg_IsAnyText(_Tokens, First, gc_pExpressionKeywords))
			return false;

		auto iName = fg_PreviousCode(_Tokens, _iColon);
		if (iName < 0 || Tokens[umint(iName)].m_Kind != ECodeTokenKind::mc_Identifier)
			return false;

		// A class head's colon follows the name behind its key, whatever specifiers lead the statement:
		// 'const struct S final : CBase'. A bit-field's follows the member's name behind its type: 'enum E e : 3'.
		auto iKey = iName;
		while (iKey >= 0 && (_Tokens.f_IsText(Tokens[umint(iKey)], "final") || _Tokens.f_IsText(Tokens[umint(iKey)], "sealed")))
			iKey = fg_PreviousCode(_Tokens, umint(iKey));

		iKey = iKey >= 0 ? fg_PreviousCode(_Tokens, umint(iKey)) : aint(-1);
		constexpr ch8 const *c_pClassKeys[] = {"struct", "class", "union", "enum"};
		if (iKey >= 0 && fg_IsAnyText(_Tokens, Tokens[umint(iKey)], c_pClassKeys))
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

	// Whether the parenthesis closing at the token is a cast by either reading.
	bool fg_IsCast(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		return fg_ClosesFundamentalCast(_Tokens, _Structure, _iClose) || fg_IsCastParenthesis(_Tokens, _Structure, _iClose);
	}

	// A postfix '++' or '--' ends the operand it stands behind: '*pParse++ - '0''.
	bool fg_IsPostfixStep(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Token = Tokens[_iToken];
		if (!_Tokens.f_IsText(Token, "++") && !_Tokens.f_IsText(Token, "--"))
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
		if (Token.m_Kind != ECodeTokenKind::mc_Punctuator || _Structure.f_IsAngleBracket(_iToken) || fg_IsBlockLiteralCaret(_Tokens, _Structure, _iToken))
			return false;

		constexpr ch8 const *c_pOperators[] =
			{
				"*"
				, "/"
				, "%"
				, "+"
				, "-"
				, "<<"
				, ">>"
				, "<"
				, ">"
				, "<="
				, ">="
				, "<=>"
				, "=="
				, "!="
				, "&"
				, "^"
				, "|"
				, "&&"
				, "||"
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

		bool bOperand
			= Before.m_Kind == ECodeTokenKind::mc_Number
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
				")"
				, "]"
				, "}"
				, ","
				, ";"
				, "..."
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
				bool bAlias
					= _Tokens.f_IsText(Tokens[iUsing], "using")
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
				"sizeof"
				, "alignof"
				, "this"
				, "nullptr"
				, "true"
				, "false"
			}
		;

		// Behind a closing parenthesis the rules below tell a call from a condition or a cast.
		bool bValueOperand
			= !_Tokens.f_IsText(Before, ")")
			&& !_Tokens.f_IsText(Before, "]")
			&&
			(
				After.m_Kind == ECodeTokenKind::mc_Number
				|| After.m_Kind == ECodeTokenKind::mc_StringLiteral
				|| After.m_Kind == ECodeTokenKind::mc_CharLiteral
				|| fg_IsAnyText(_Tokens, After, c_pValueOperands)
			)
		;
		if (bValueOperand)
			return true;

		if (fg_IsDeclaratorToken(_Tokens, _Structure, _iToken))
			return false;

		// What follows a ref-qualifier is the rest of the declaration: the trailing return
		// type, the body, a pure specifier, a requires clause or another qualifier.
		constexpr ch8 const *c_pTails[] =
			{
				"->"
				, "{"
				, "="
				, "requires"
				, "const"
				, "volatile"
				, "noexcept"
				, "override"
				, "final"
				, "&"
				, "&&"
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
		// 'Value += a & b'. The left operand's qualified name is stepped over to find what stands in front, and a
		// template-id's arguments or a parenthesis with it. A requires clause's constraints are no declaration either:
		// 'requires cA<T>' over '&& cB<T>'.
		auto iOperand = iBefore;
		if (_Structure.f_IsAngleBracket(umint(iBefore)) && _Tokens.f_IsText(Before, ">"))
		{
			auto iArguments = fg_FindGroupClosingAt(_Structure, umint(iBefore), ECodeBracket::mc_Angle);
			auto iName = iArguments >= 0 ? fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iArguments)].m_iFirstToken) : aint(-1);
			iOperand = iName >= 0 && Tokens[umint(iName)].m_Kind == ECodeTokenKind::mc_Identifier ? iName : aint(-1);
		}
		else if (_Tokens.f_IsText(Before, ")"))
		{
			auto iGroup = fg_FindGroupClosingAt(_Structure, umint(iBefore), ECodeBracket::mc_Paren);
			auto iLead = iGroup >= 0 ? fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iGroup)].m_iFirstToken) : aint(-1);
			auto const *pLead = iLead >= 0 ? &Tokens[umint(iLead)] : nullptr;
			bool bConstraint
				= pLead && (_Tokens.f_IsText(*pLead, "requires") || _Tokens.f_IsText(*pLead, "&&") || _Tokens.f_IsText(*pLead, "||")) && !_Structure.f_IsAngleBracket(umint(iLead))
			;
			if (bConstraint && (_Tokens.f_IsText(Token, "&&") || _Tokens.f_IsText(Token, "||")))
				return true;

			iOperand = -1;
		}
		else if (Before.m_Kind != ECodeTokenKind::mc_Identifier)
			iOperand = -1;

		if (iOperand >= 0)
		{
			auto iLead = fg_PreviousCode(_Tokens, umint(iOperand));
			while (iLead >= 0 && _Tokens.f_IsText(Tokens[umint(iLead)], "::"))
			{
				auto iScope = fg_PreviousCode(_Tokens, umint(iLead));
				if (iScope < 0 || Tokens[umint(iScope)].m_Kind != ECodeTokenKind::mc_Identifier)
					break;

				iLead = fg_PreviousCode(_Tokens, umint(iScope));
			}

			constexpr ch8 const *c_pOperators[] =
				{
					"+"
					, "-"
					, "/"
					, "%"
					, "|"
					, "^"
					, "<<"
					, ">>"
					, "=="
					, "!="
					, "<="
					, ">="
					, "<=>"
					, "&&"
					, "||"
					, "?"
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

			auto const *pLead = iLead >= 0 ? &Tokens[umint(iLead)] : nullptr;
			if (pLead && !_Structure.f_IsAngleBracket(umint(iLead)) && fg_IsAnyText(_Tokens, *pLead, c_pOperators))
				return true;

			bool bConstraint = pLead && _Tokens.f_IsText(*pLead, "requires") && (_Tokens.f_IsText(Token, "&&") || _Tokens.f_IsText(Token, "||"));
			if (bConstraint)
				return true;

			// A member access names a value, never a type: 'Style.Penalty *' over '(a + b)'.
			if (pLead && iLead == fg_PreviousCode(_Tokens, umint(iOperand)) && (_Tokens.f_IsText(*pLead, ".") || _Tokens.f_IsText(*pLead, "->")))
				return true;
		}

		// Nor does a declaration stand in front of what only an operand starts with: 'Value & ~Mask'.
		bool bOperandAfter = _Tokens.f_IsText(After, "~") || _Tokens.f_IsText(After, "!") || After.m_Kind == ECodeTokenKind::mc_Number || After.m_Kind == ECodeTokenKind::mc_CharLiteral;
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
					"sizeof"
					, "alignof"
					, "typeid"
					, "noexcept"
				}
			;

			auto iGroup = fg_FindGroupClosingAt(_Structure, umint(iBefore), ECodeBracket::mc_None);
			if (iGroup < 0)
				return false;

			auto iName = fg_PreviousCode(_Tokens, Nodes[umint(iGroup)].m_iFirstToken);
			if (iName < 0)
				return false;

			// A call's callee can be a template-id, whose name stands in front of its arguments: 'isa<CFoo>(Value) &&'.
			if (_Tokens.f_IsText(Tokens[umint(iName)], ">") && _Structure.f_IsAngleBracket(umint(iName)))
			{
				auto iArguments = fg_FindGroupClosingAt(_Structure, umint(iName), ECodeBracket::mc_Angle);
				auto iTemplate = iArguments >= 0 ? fg_PreviousCode(_Tokens, Nodes[umint(iArguments)].m_iFirstToken) : aint(-1);
				if (iTemplate >= 0 && Tokens[umint(iTemplate)].m_Kind == ECodeTokenKind::mc_Identifier)
					iName = iTemplate;
			}

			auto const &Name = Tokens[umint(iName)];
			if (Name.m_Kind == ECodeTokenKind::mc_Identifier)
				return !fg_IsAnyText(_Tokens, Name, gc_pExpressionKeywords) || fg_IsAnyText(_Tokens, Name, c_pValueOperators);

			// A parenthesis opening behind another, an '=' or an operator groups an expression, a cast
			// having been told apart above: '((a + b) & ~Mask)'.
			constexpr ch8 const *c_pExpressionLeads[] =
				{
					"("
					, "="
					, ","
					, "+"
					, "-"
					, "*"
					, "/"
					, "%"
					, "|"
					, "^"
					, "&&"
					, "||"
					, "?"
					, ":"
					, "<<"
					, ">>"
					, "=="
					, "!="
					, "<"
					, ">"
					, "<="
					, ">="
				}
			;

			// One holding a name and nothing more converts as readily as it groups: '(task_info_t)&Info'.
			if (_Tokens.f_IsText(Before, ")") && fg_IsAnyText(_Tokens, Name, c_pExpressionLeads) && !_Structure.f_IsAngleBracket(umint(iName)))
			{
				bool bExpression = false;
				for (auto i = Nodes[umint(iGroup)].m_iFirstToken + 1; i < umint(iBefore) && !bExpression; ++i)
				{
					auto const &Inner = Tokens[i];
					bExpression
						= Inner.m_Kind == ECodeTokenKind::mc_Number || (Inner.m_Kind == ECodeTokenKind::mc_Punctuator && !_Tokens.f_IsText(Inner, "::") && !_Structure.f_IsAngleBracket(i))
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
			auto const *pClause = iClause >= 0 ? &Tokens[umint(iClause)] : nullptr;

			// A requires clause's parenthesis holds a constraint, which declares nothing.
			if (pClause && _Tokens.f_IsText(*pClause, "requires"))
				return !fg_IsParameterList(_Tokens, _Structure, iNode);

			if (pClause && _Tokens.f_IsText(*pClause, "constexpr"))
			{
				iClause = fg_PreviousCode(_Tokens, umint(iClause));
				pClause = iClause >= 0 ? &Tokens[umint(iClause)] : nullptr;
			}

			bCondition = pClause && (_Tokens.f_IsText(*pClause, "if") || _Tokens.f_IsText(*pClause, "while") || _Tokens.f_IsText(*pClause, "switch"));
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

			auto const &Inner = Tokens[i];

			// A separator starts the next declarator: 'int *pA = f(), *pB;'.
			if (!bCondition && (_Tokens.f_IsText(Inner, ",") || _Tokens.f_IsText(Inner, ";")))
				bAssigned = false;
			else if (_Tokens.f_IsText(Inner, "="))
				bAssigned = true;
		}

		return bCondition ? !bAssigned : bAssigned;
	}
}

namespace NMib::NDevelop
{
	using namespace NPrivate;

	// Whether the block holds a function's statements rather than a class's, a
	// namespace's or an enumeration's members: it belongs to a function, a lambda, a
	// control clause or a bare block. There a name behind a type in front of a parenthesis
	// defines a variable, since no function is ever declared inside another.
	bool fg_IsFunctionBody(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iBlock)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Tokens = _Tokens.f_GetTokens();
		auto const &Block = Nodes[_iBlock];
		if (Block.m_Kind != ECodeNodeKind::mc_Block || Block.m_iParent >= Nodes.f_GetLen())
			return false;

		auto iOwner = Block.m_iParent;
		auto const &Owner = Nodes[iOwner];

		// A block in a bracket belongs to an expression, a lambda's body or a requirement's: 'f([] { ... })',
		// 'static_assert(requires { ... })'.
		if (Owner.m_Kind == ECodeNodeKind::mc_Group)
			return true;

		if (Owner.m_Kind != ECodeNodeKind::mc_Statement || fg_IsClassHead(_Tokens, _Structure, iOwner) || _Structure.f_IsEnumBody(_iBlock))
			return false;

		auto const &First = Tokens[Owner.m_iFirstToken];
		if (_Tokens.f_IsText(First, "namespace"))
			return false;

		// A linkage block's brace stands behind its language's name, where a function an 'extern' declares has its
		// declarator: 'extern "C" {' and 'extern "C" void f() {'.
		if (_Tokens.f_IsText(First, "extern"))
		{
			umint iBefore = 0;
			for (auto iToken = Owner.m_iFirstToken; iToken < Block.m_iFirstToken; ++iToken)
			{
				auto Kind = Tokens[iToken].m_Kind;
				bool bCode
					= Kind == ECodeTokenKind::mc_Identifier
					|| Kind == ECodeTokenKind::mc_Number
					|| Kind == ECodeTokenKind::mc_CharLiteral
					|| Kind == ECodeTokenKind::mc_StringLiteral
					|| Kind == ECodeTokenKind::mc_Punctuator
				;
				if (bCode)
					iBefore = iToken;
			}

			return Tokens[iBefore].m_Kind != ECodeTokenKind::mc_StringLiteral;
		}

		return true;
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
				"this"
				, "nullptr"
				, "true"
				, "false"
			}
		;
		constexpr ch8 const *c_pUnary[] =
			{
				"&"
				, "*"
				, "!"
				, "-"
				, "+"
				, "~"
				, "{"
				, "new"
				, "delete"
				, "sizeof"
				, "alignof"
				, "typeid"
				, "throw"
				, "co_await"
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
				bool bArgument
					= Token.m_Kind == ECodeTokenKind::mc_Number
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

	// Every parameter in the parenthesis is proven one, where an argument list holding values reads the same: '(int)',
	// '(CStr const &_Value)', '(TCVector<int> _Values)', '(void)', '()'. A parameter is proven by a word only a type
	// holds, by two names written one behind the other, or by a first name the naming lists as a type's.
	bool fg_HoldsParameters(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup)
	{
		auto const &Nodes = _Structure.f_GetNodes();
		auto const &Group = Nodes[_iGroup];
		auto const &Tokens = _Tokens.f_GetTokens();
		constexpr ch8 const *c_pTypeWords[] =
			{
				"void"
				, "bool"
				, "char"
				, "char8_t"
				, "char16_t"
				, "char32_t"
				, "wchar_t"
				, "short"
				, "int"
				, "long"
				, "signed"
				, "unsigned"
				, "float"
				, "double"
				, "auto"
				, "const"
				, "volatile"
				, "struct"
				, "class"
				, "union"
				, "enum"
				, "typename"
				, "decltype"
				, "..."
			}
		;

		bool bProven = true;
		bool bEmpty = true;
		bool bParameterProven = false;
		bool bDefault = false;
		bool bFirstName = true;
		bool bNameBehind = false;
		bool bTypeByRole = false; // The last name proved the parameter by the naming's type role alone.
		bool bDisproven = false;
		for (auto i = Group.m_iFirstToken + 1; i < Group.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (!fg_IsSignificant(Token.m_Kind))
				continue;

			bEmpty = false;

			bool bNested = false;
			for (auto iChild : Group.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken <= i && i <= Child.m_iLastToken)
				{
					// A brace or a parenthesis behind a name constructs a value, 'int{42}', 'int(42)', unless the parenthesis
					// opens a declarator: 'int (*_pFunction)(int)'.
					auto const &Open = Tokens[Child.m_iFirstToken];
					if (!bDefault && _Tokens.f_IsText(Open, "{"))
						bDisproven = true;
					else if (!bDefault && bNameBehind && _Tokens.f_IsText(Open, "("))
					{
						auto iInside = fg_NextCode(_Tokens, Child.m_iFirstToken);
						bool bDeclarator
							= iInside >= 0
							&& umint(iInside) < Child.m_iLastToken
							&&
							(
								_Tokens.f_IsText(Tokens[umint(iInside)], "*")
								|| _Tokens.f_IsText(Tokens[umint(iInside)], "&")
								|| _Tokens.f_IsText(Tokens[umint(iInside)], "&&")
								|| _Tokens.f_IsText(Tokens[umint(iInside)], "^")
							)
						;
						bDisproven = bDisproven || !bDeclarator;
					}

					// A parenthesis ends a name: the parameter list behind a declarator, '(*_pFunction)(int)', is none.
					if (_Tokens.f_IsText(Open, "("))
						bNameBehind = false;

					i = Child.m_iLastToken;
					bNested = true;

					break;
				}
			}

			if (bNested)
				continue;

			if (_Tokens.f_IsText(Token, ","))
			{
				bProven = bProven && bParameterProven;
				bParameterProven = false;
				bDefault = false;
				bFirstName = true;
				bNameBehind = false;
				bTypeByRole = false;

				continue;
			}

			if (bDefault)
				continue;

			if (_Tokens.f_IsText(Token, "="))
			{
				bDefault = true;

				continue;
			}

			// An ellipsis behind a name expands a pack as readily as it declares one, 'CResult<Ns...> Values(Ns...)', and the
			// name behind it is what proves a declaration: 'Ts ...p_Values'.
			if (_Tokens.f_IsText(Token, "...") && !bFirstName)
				continue;

			if (fg_IsAnyText(_Tokens, Token, c_pTypeWords))
			{
				bParameterProven = true;
				bFirstName = false;
				bNameBehind = !_Tokens.f_IsText(Token, "...");
				bTypeByRole = false;

				continue;
			}
			else if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				// The alternative spelling of an operator is one: 'a not_eq b' is a value, and so is GNU's part of a complex number,
				// '__real__ z', its alignment, '__alignof__ x', and an expression GNU's '__extension__' leads. A macro standing for an operator, 'a AND b' with
				// '#define AND &&', is out of scope by decision: a macro is only what the naming lists.
				constexpr ch8 const *c_pOperators[] =
					{
						"and"
						, "and_eq"
						, "bitand"
						, "bitor"
						, "compl"
						, "not"
						, "not_eq"
						, "or"
						, "or_eq"
						, "xor"
						, "xor_eq"
						, "__real__"
						, "__imag__"
						, "__real"
						, "__imag"
						, "__alignof__"
						, "__alignof"
						, "__extension__"
					}
				;

				for (auto pOperator : c_pOperators)
				{
					if (_Tokens.f_IsText(Token, pOperator))
						bDisproven = true;
				}

				bTypeByRole = !bNameBehind && bFirstName && _Tokens.f_HasRole(Token, ECodeNameRole::mc_Type);
				if (bNameBehind || bTypeByRole)
					bParameterProven = true;

				bFirstName = false;
				bNameBehind = true;

				continue;
			}

			// A type the naming lists only qualifies the name behind it, which is the one that has to be a type:
			// 'COptions::mc_Value' is a value, 'COptions::CValue' a type.
			if (_Tokens.f_IsText(Token, "::"))
			{
				if (bTypeByRole)
					bParameterProven = false;

				bTypeByRole = false;
				bFirstName = true;
				bNameBehind = false;

				continue;
			}

			// Outside a default argument a declaration holds no operator but its declarators' and no literal: 'T::m_A + T::m_B'
			// is a value.
			bool bDeclarationPunctuator = false;
			for (auto pPunctuator : {"::", "*", "&", "&&", "^", "<", ">"})
				bDeclarationPunctuator = bDeclarationPunctuator || _Tokens.f_IsText(Token, pPunctuator);

			bDisproven = bDisproven || !bDeclarationPunctuator;
			bNameBehind = false;
		}

		return bEmpty || (bProven && bParameterProven && !bDisproven);
	}

	// An Objective-C block literal's '^' introduces it, '^{ ... }' or '^(int _A) { ... }', and is no operator.
	bool fg_IsBlockLiteralCaret(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken)
	{
		auto const &Tokens = _Tokens.f_GetTokens();
		if (!_Tokens.f_IsText(Tokens[_iToken], "^"))
			return false;

		auto iNext = fg_NextCode(_Tokens, _iToken);
		if (iNext < 0)
			return false;

		if (_Tokens.f_IsText(Tokens[umint(iNext)], "("))
		{
			auto iGroup = _Structure.f_FindNodeOpeningAt(umint(iNext));
			iNext = iGroup < _Structure.f_GetNodes().f_GetLen() ? fg_NextCode(_Tokens, _Structure.f_GetNodes()[iGroup].m_iLastToken) : aint(-1);
		}

		return iNext >= 0 && _Tokens.f_IsText(Tokens[umint(iNext)], "{");
	}

	bool fg_IsCaptureList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose)
	{
		// C has no lambdas, so its brackets subscript and designate: '[1] = 2'.
		if (_Tokens.f_IsC(_Tokens.f_GetTokens()[_iClose]))
			return false;

		auto const &Tokens = _Tokens.f_GetTokens();
		auto iNode = fg_FindGroupClosingAt(_Structure, _iClose, ECodeBracket::mc_Square);
		if (iNode < 0)
			return false;

		auto const &Node = _Structure.f_GetNodes()[umint(iNode)];
		if (Node.m_bMessageSend)
			return false;

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
				"const"
				, "volatile"
				, "noexcept"
				, "override"
				, "final"
				, "mutable"
				, "&"
				, "&&"
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
			return fg_FindGroupClosingAt(_Structure, umint(iRequirement), ECodeBracket::mc_Brace) >= 0;

		auto iBefore = fSkipNoexceptCondition(fg_PreviousCode(_Tokens, _iArrow));
		while (iBefore >= 0 && fg_IsAnyText(_Tokens, Tokens[umint(iBefore)], c_pQualifiers))
			iBefore = fSkipNoexceptCondition(fg_PreviousCode(_Tokens, umint(iBefore)));

		if (iBefore < 0)
			return false;

		// So does one a requirement's 'noexcept' stands between: '{ t() } noexcept -> cFoo'.
		if (iBefore != iRequirement && _Tokens.f_IsText(Tokens[umint(iBefore)], "}"))
			return fg_FindGroupClosingAt(_Structure, umint(iBefore), ECodeBracket::mc_Brace) >= 0;

		// A lambda that takes nothing may leave its parameter list out, and then the arrow
		// stands behind the capture list, or behind the attribute macro that follows one:
		// '[pState] -> TCFuture<void>'.
		auto iCapture = iBefore;
		auto const &Before = Tokens[umint(iBefore)];
		if (Before.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Before, gc_pExpressionKeywords))
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

			bool bTypePart
				= (Type.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Type, gc_pExpressionKeywords)) || _Tokens.f_IsText(Type, "::") || fg_IsDeclaratorText(_Tokens, Type)
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

		auto const *pAfter = iAfter >= 0 ? &Tokens[umint(iAfter)] : nullptr;

		return pAfter && (_Tokens.f_IsText(*pAfter, ":") || _Tokens.f_IsText(*pAfter, "="));
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
				// So does a macro the naming lists, whose expansion is a type where a declarator follows it, and
				// an attribute of the type: 'umint __attribute__((address_space(256))) *pAddress'.
				auto iKeyword = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iNode)].m_iFirstToken);
				auto const *pKeyword = iKeyword >= 0 ? &Tokens[umint(iKeyword)] : nullptr;
				bBehindTemplate
					= pKeyword
					&&
					(
						_Tokens.f_IsText(*pKeyword, "decltype")
						|| _Tokens.f_IsText(*pKeyword, "__attribute__")
						|| _Tokens.f_IsText(*pKeyword, "__attribute")
						|| _Tokens.f_IsText(*pKeyword, "__declspec")
						|| _Tokens.f_HasRole(*pKeyword, ECodeNameRole::mc_Macro)
					)
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

				bool bTypePart
					= (Back.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Back, gc_pExpressionKeywords))
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
			bool bUnnamed
				= _Tokens.f_IsText(Next, ",")
				|| _Tokens.f_IsText(Next, ")")
				|| _Tokens.f_IsText(Next, "...")
				|| _Tokens.f_IsText(Next, "=")
				|| _Tokens.f_IsText(Next, "const")
				|| _Tokens.f_IsText(Next, "volatile")
				|| (_Structure.f_IsAngleBracket(umint(iNext)) && _Tokens.f_IsText(Next, ">"))
			;
			if (bUnnamed)
				return true;
		}

		// A name standing behind 'requires' or a constraint's operator is a constraint, which declares nothing:
		// 'requires cA<t_C>' over '&& cB<t_C>'. The name is one, however qualified, so two names in a row end it.
		{
			auto iBack = iPrevious;
			bool bBehindName = false;
			while (iBack >= 0)
			{
				auto const &Back = Tokens[umint(iBack)];
				if (_Structure.f_IsAngleBracket(umint(iBack)) && _Tokens.f_IsText(Back, ">") && !bBehindName)
				{
					auto iArguments = fg_FindGroupClosingAt(_Structure, umint(iBack), ECodeBracket::mc_Angle);
					if (iArguments < 0)
						break;

					iBack = fg_PreviousCode(_Tokens, _Structure.f_GetNodes()[umint(iArguments)].m_iFirstToken);

					continue;
				}

				bool bName = Back.m_Kind == ECodeTokenKind::mc_Identifier && !fg_IsAnyText(_Tokens, Back, gc_pExpressionKeywords) && !_Tokens.f_IsText(Back, "requires");
				if ((!bName || bBehindName) && !_Tokens.f_IsText(Back, "::"))
					break;

				bBehindName = bName;
				iBack = fg_PreviousCode(_Tokens, umint(iBack));
			}

			auto const *pBack = iBack >= 0 ? &Tokens[umint(iBack)] : nullptr;
			bool bConstraint
				= pBack
				&& umint(iBack) != umint(iPrevious)
				&& (_Tokens.f_IsText(*pBack, "requires") || _Tokens.f_IsText(*pBack, "&&") || _Tokens.f_IsText(*pBack, "||"))
				&& !_Structure.f_IsAngleBracket(umint(iBack))
			;
			if (bConstraint)
				return false;
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
