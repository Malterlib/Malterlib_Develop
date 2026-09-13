// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Vector>
#include <Mib/String/String>

namespace NMib::NDevelop
{
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

	struct CCodeToken
	{
		ECodeTokenKind m_Kind = ECodeTokenKind::mc_Unknown;
		umint m_iOffset = 0;
		umint m_nLength = 0;
		bool m_bMultiLine = false;			// Contains a line terminator, so its interior layout is protected.
		bool m_bUnterminated = false;		// The source ends inside the token.

		umint f_GetEnd() const;
	};

	// Lexes C and C++ source losslessly: concatenating every token reproduces the input.
	// The lexer needs no compilation database and does not evaluate conditional branches.
	struct CCodeTokenStream
	{
		CCodeTokenStream() = default;
		explicit CCodeTokenStream(NStr::CStr const &_Source);

		NContainer::TCVector<CCodeToken> const &f_GetTokens() const;

		// False when the source ends inside a comment or literal, which makes the
		// structure uncertain and the file unsupported for automatic edits.
		bool f_IsComplete() const;

		// Index of the token containing the byte offset, or the token count at the end.
		umint f_FindToken(umint _iOffset) const;

		bool f_IsText(CCodeToken const &_Token, NStr::CStr const &_Text) const;
		NStr::CStr f_GetText(CCodeToken const &_Token) const;
		NStr::CStr const &f_GetSource() const;

	private:
		void fp_Lex();

		NStr::CStr mp_Source;
		NContainer::TCVector<CCodeToken> mp_Tokens;
		bool mp_bComplete = true;
	};
}
