// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Vector>
#include <Mib/String/String>

namespace NMib::NDevelop
{
	enum class ECodeLanguage
	{
		mc_Unknown
		, mc_Cpp
		, mc_BuildSystem		// The registry syntax of the build system's '.M*' files.
	};

	enum class ECodeTokenKind
	{
		mc_ByteOrderMark		// A leading UTF-8 signature.
		, mc_Whitespace			// Spaces and tabs, never crossing a line boundary.
		, mc_Newline
		, mc_LineSplice			// A backslash that joins this source line to the next.
		, mc_LineComment
		, mc_BlockComment
		, mc_Preprocessor		// A whole directive, including continuations and embedded comments.
		, mc_Identifier
		, mc_Number
		, mc_CharLiteral
		, mc_StringLiteral		// Ordinary, encoded, and raw string literals.
		, mc_Punctuator
		, mc_Unknown			// A byte no C++ token can start with.
	};

	// What a project's naming says an identifier is. The roles come from its
	// '.malterlib-format', so a token lexed without one has none.
	enum class ECodeNameRole : uint8
	{
		mc_None = 0
		, mc_Function = DMibBit(0)
		, mc_Type = DMibBit(1)
		, mc_Macro = DMibBit(2)
		, mc_SpecifierMacro = DMibBit(3)
		, mc_DSLMarker = DMibBit(4)
	};

	struct CCodeFormattingNaming;

	struct CCodeToken
	{
		ECodeTokenKind m_Kind = ECodeTokenKind::mc_Unknown;
		umint m_iOffset = 0;
		umint m_nLength = 0;
		bool m_bMultiLine = false;			// Contains a line terminator, so its interior layout is protected.
		bool m_bUnterminated = false;		// The source ends inside the token.
		ECodeNameRole m_Roles = ECodeNameRole::mc_None;

		umint f_GetEnd() const;
	};

	// Lexes C and C++ source, or the build system's syntax, losslessly: concatenating every
	// token reproduces the input. The lexer needs no compilation database and does not
	// evaluate conditional branches.
	struct CCodeTokenStream
	{
		CCodeTokenStream() = default;
		explicit CCodeTokenStream
			(
				NStr::CStr const &_Source
				, CCodeFormattingNaming const *_pNaming = nullptr
				, ECodeLanguage _Language = ECodeLanguage::mc_Cpp
			)
		;

		NContainer::TCVector<CCodeToken> const &f_GetTokens() const;

		// False when the source ends inside a comment or literal, which makes the
		// structure uncertain and the file unsupported for automatic edits.
		bool f_IsComplete() const;

		// Index of the token containing the byte offset, or the token count at the end.
		umint f_FindToken(umint _iOffset) const;

		bool f_IsText(CCodeToken const &_Token, NStr::CStr const &_Text) const;
		bool f_IsText(CCodeToken const &_Token, ch8 const *_pText) const;
		bool f_HasRole(CCodeToken const &_Token, ECodeNameRole _Role) const;
		bool f_StartsWith(CCodeToken const &_Token, ch8 const *_pPrefix) const;
		bool f_HasSameText(CCodeToken const &_Left, CCodeToken const &_Right) const;
		ch8 const *f_GetTextPointer(CCodeToken const &_Token) const;
		NStr::CStr f_GetText(CCodeToken const &_Token) const;
		NStr::CStr const &f_GetSource() const;

		// Splits a punctuator into two adjacent tokens of the same kind. C++ reads a '>>' that
		// ends a template argument list as two '>' tokens, and the structure builder spells it
		// that way so that each list has a closing marker of its own.
		void f_SplitToken(umint _iToken, umint _nFirstLength);

	private:
		void fp_Lex();
		void fp_LexBuildSystem();

		NStr::CStr mp_Source;
		NContainer::TCVector<CCodeToken> mp_Tokens;
		bool mp_bComplete = true;
	};
}

#include "Malterlib_Develop_CodeFormattingLexer.hpp"
