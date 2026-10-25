// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Develop_CodeFormattingStructure.h"
#include "Malterlib_Develop_TextLayout.h"

#include <Mib/Storage/Optional>

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
}

namespace NMib::NDevelop::NPrivate
{
	// Keywords that stand in front of a name or a parenthesis without declaring anything.
	inline constexpr ch8 const *gc_pExpressionKeywords[] =
		{
			"if"
			, "for"
			, "while"
			, "switch"
			, "return"
			, "co_return"
			, "co_await"
			, "co_yield"
			, "throw"
			, "new"
			, "delete"
			, "sizeof"
			, "alignof"
			, "decltype"
			, "typeid"
			, "static_assert"
			, "noexcept"
			, "alignas"
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

	template <umint t_nTexts>
	bool fg_IsAnyText(CCodeTokenStream const &_Tokens, CCodeToken const &_Token, ch8 const * const (&_pTexts)[t_nTexts]);
	bool fg_IsSignificant(ECodeTokenKind _Kind);
	bool fg_IsAsmSectionSeparator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken);
	aint fg_PreviousCode(CCodeTokenStream const &_Tokens, umint _iToken);
	aint fg_NextCode(CCodeTokenStream const &_Tokens, umint _iToken);
	bool fg_IsDeclaratorText(CCodeTokenStream const &_Tokens, CCodeToken const &_Token);
	bool fg_NamesOperator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken);
	bool fg_IsRefQualifier(CCodeTokenStream const &_Tokens, umint _iToken);
	aint fg_FindGroupClosingAt(CCodeStructure const &_Structure, umint _iClose, ECodeBracket _Bracket);
	umint fg_FindEnclosingNode(CCodeStructure const &_Structure, umint _iToken);
	bool fg_NamesFunction(CCodeTokenStream const &_Tokens, CCodeToken const &_Token);
	bool fg_NamesType(CCodeTokenStream const &_Tokens, CCodeToken const &_Token);
	bool fg_NamesCall(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iName);
	bool fg_DeclaresPack(CCodeTokenStream const &_Tokens, umint _iEllipsis);
	bool fg_IsFunctionPointerDeclarator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup);
	bool fg_IsParameterList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup);
	bool fg_IsConditionalColon(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iColon);
	bool fg_IsBitFieldColon(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iColon);
	bool fg_IsDSLMarker(CCodeTokenStream const &_Tokens, CCodeToken const &_Token);
	bool fg_ClosesExpressionBody(CCodeStructure const &_Structure, umint _iBrace);
	bool fg_IsCast(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose);
	bool fg_IsPostfixStep(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken);
	bool fg_IsInfixOperator(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken);
}

#include "Malterlib_Develop_CodeFormattingStructurePrivate.hpp"
