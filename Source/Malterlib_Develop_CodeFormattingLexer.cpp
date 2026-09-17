// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingLexer.h"

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
}

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;

	bool fg_IsHorizontalSpace(ch8 _Char)
	{
		return _Char == ' ' || _Char == '\t' || _Char == '\v' || _Char == '\f';
	}

	bool fg_IsLineBreak(ch8 _Char)
	{
		return _Char == '\n' || _Char == '\r';
	}

	bool fg_IsDigit(ch8 _Char)
	{
		return _Char >= '0' && _Char <= '9';
	}

	// Bytes at or above 0x80 are treated as identifier characters: the lexer only needs
	// token boundaries, and no punctuator or literal delimiter is encoded that way in UTF-8.
	bool fg_IsIdentifierStart(ch8 _Char)
	{
		return (_Char >= 'A' && _Char <= 'Z') || (_Char >= 'a' && _Char <= 'z') || _Char == '_' || _Char == '$' || uch8(_Char) >= 0x80;
	}

	bool fg_IsIdentifierChar(ch8 _Char)
	{
		return fg_IsIdentifierStart(_Char) || fg_IsDigit(_Char);
	}

	struct CLexerCursor
	{
		ch8 const *m_pStart = nullptr;
		umint m_nLength = 0;
		umint m_iOffset = 0;

		bool f_AtEnd() const
		{
			return m_iOffset >= m_nLength;
		}

		ch8 f_Peek(umint _nAhead = 0) const
		{
			return m_iOffset + _nAhead < m_nLength ? m_pStart[m_iOffset + _nAhead] : ch8(0);
		}

		// Consumes a line terminator, treating CRLF as one unit. Returns false at other bytes.
		bool f_SkipLineBreak()
		{
			if (f_Peek() == '\r')
			{
				m_iOffset += f_Peek(1) == '\n' ? 2 : 1;

				return true;
			}

			if (f_Peek() == '\n')
			{
				++m_iOffset;

				return true;
			}

			return false;
		}

		// A backslash before a line terminator splices the next source line onto this one.
		// Trailing horizontal space between them is a common extension and stays protected.
		bool f_SkipSplice()
		{
			if (f_Peek() != '\\')
				return false;

			umint iAhead = 1;
			while (fg_IsHorizontalSpace(f_Peek(iAhead)))
				++iAhead;

			if (!fg_IsLineBreak(f_Peek(iAhead)))
				return false;

			m_iOffset += iAhead;
			f_SkipLineBreak();

			return true;
		}
	};

	bool fg_IsSameText(ch8 const *_pText, umint _nLength, ch8 const *_pOther)
	{
		for (umint i = 0; i < _nLength; ++i)
		{
			if (_pText[i] != _pOther[i] || !_pOther[i])
				return false;
		}

		return !_pOther[_nLength];
	}

	bool fg_IsOneOf(ch8 const *_pText, umint _nLength, std::initializer_list<ch8 const *> _Candidates)
	{
		for (auto pCandidate : _Candidates)
		{
			if (fg_IsSameText(_pText, _nLength, pCandidate))
				return true;
		}

		return false;
	}

	// Returns true when the source ends before the closing quote.
	bool fg_LexQuoted(CLexerCursor &_Cursor, ch8 _Quote)
	{
		++_Cursor.m_iOffset;
		while (!_Cursor.f_AtEnd())
		{
			if (_Cursor.f_SkipSplice())
				continue;

			auto Char = _Cursor.f_Peek();
			if (fg_IsLineBreak(Char))
				return true;

			++_Cursor.m_iOffset;
			if (Char == _Quote)
				return false;

			if (Char == '\\' && !_Cursor.f_AtEnd() && !_Cursor.f_SkipSplice())
				++_Cursor.m_iOffset;
		}

		return true;
	}

	bool fg_LexRawString(CLexerCursor &_Cursor)
	{
		++_Cursor.m_iOffset;
		auto iDelimiter = _Cursor.m_iOffset;
		while (!_Cursor.f_AtEnd() && _Cursor.f_Peek() != '(' && !fg_IsLineBreak(_Cursor.f_Peek()))
			++_Cursor.m_iOffset;

		if (_Cursor.f_AtEnd() || _Cursor.f_Peek() != '(')
			return true;

		auto nDelimiter = _Cursor.m_iOffset - iDelimiter;
		++_Cursor.m_iOffset;
		while (!_Cursor.f_AtEnd())
		{
			if (_Cursor.f_Peek() != ')')
			{
				++_Cursor.m_iOffset;

				continue;
			}

			umint i = 0;
			while (i < nDelimiter && _Cursor.f_Peek(1 + i) == _Cursor.m_pStart[iDelimiter + i])
				++i;

			if (i == nDelimiter && _Cursor.f_Peek(1 + nDelimiter) == '"')
			{
				_Cursor.m_iOffset += nDelimiter + 2;

				return false;
			}

			++_Cursor.m_iOffset;
		}

		return true;
	}

	void fg_LexNumber(CLexerCursor &_Cursor)
	{
		++_Cursor.m_iOffset;
		while (!_Cursor.f_AtEnd())
		{
			auto Char = _Cursor.f_Peek();
			auto Next = _Cursor.f_Peek(1);
			if ((Char == 'e' || Char == 'E' || Char == 'p' || Char == 'P') && (Next == '+' || Next == '-'))
			{
				_Cursor.m_iOffset += 2;

				continue;
			}

			if (Char == '\'' && fg_IsIdentifierChar(Next))
			{
				_Cursor.m_iOffset += 2;

				continue;
			}

			if (!fg_IsIdentifierChar(Char) && Char != '.')
				break;

			++_Cursor.m_iOffset;
		}
	}

	void fg_LexLineComment(CLexerCursor &_Cursor)
	{
		_Cursor.m_iOffset += 2;
		while (!_Cursor.f_AtEnd() && !fg_IsLineBreak(_Cursor.f_Peek()))
		{
			if (!_Cursor.f_SkipSplice())
				++_Cursor.m_iOffset;
		}
	}

	bool fg_LexBlockComment(CLexerCursor &_Cursor)
	{
		_Cursor.m_iOffset += 2;
		while (!_Cursor.f_AtEnd())
		{
			if (_Cursor.f_Peek() == '*' && _Cursor.f_Peek(1) == '/')
			{
				_Cursor.m_iOffset += 2;

				return false;
			}

			++_Cursor.m_iOffset;
		}

		return true;
	}

	// A directive runs to the first line terminator that is neither spliced nor inside a
	// block comment. Conditional branches are not evaluated; every branch is lexed as text.
	bool fg_LexPreprocessor(CLexerCursor &_Cursor)
	{
		bool bUnterminated = false;
		++_Cursor.m_iOffset;
		while (!_Cursor.f_AtEnd())
		{
			if (_Cursor.f_SkipSplice())
				continue;

			auto Char = _Cursor.f_Peek();
			if (fg_IsLineBreak(Char))
				break;

			if (Char == '/' && _Cursor.f_Peek(1) == '*')
			{
				bUnterminated |= fg_LexBlockComment(_Cursor);

				continue;
			}

			if (Char == '/' && _Cursor.f_Peek(1) == '/')
			{
				fg_LexLineComment(_Cursor);

				continue;
			}

			if (Char == '"' || Char == '\'')
			{
				// An apostrophe in directive text, such as in a comment word, is not a literal.
				auto iRestore = _Cursor.m_iOffset;
				if (fg_LexQuoted(_Cursor, Char))
					_Cursor.m_iOffset = iRestore + 1;

				continue;
			}

			++_Cursor.m_iOffset;
		}

		return bUnterminated;
	}

	bool fg_LexPunctuator(CLexerCursor &_Cursor)
	{
		constexpr ch8 const *c_pPunctuators[] =
			{
				"<<=", ">>=", "...", "<=>", "->*"
				, "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||"
				, "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", ".*"
			}
		;
		for (auto pPunctuator : c_pPunctuators)
		{
			umint nLength = 0;
			while (pPunctuator[nLength])
				++nLength;

			umint i = 0;
			while (i < nLength && _Cursor.f_Peek(i) == pPunctuator[i])
				++i;

			if (i == nLength)
			{
				_Cursor.m_iOffset += nLength;

				return true;
			}
		}

		constexpr ch8 c_Single[] = "{}[]()<>:;.?*+-/%^&|~!=,#\\@`";
		for (auto pSingle = c_Single; *pSingle; ++pSingle)
		{
			if (_Cursor.f_Peek() == *pSingle)
			{
				++_Cursor.m_iOffset;

				return true;
			}
		}

		return false;
	}
}

namespace NMib::NDevelop
{
	umint CCodeToken::f_GetEnd() const
	{
		return m_iOffset + m_nLength;
	}

	CCodeTokenStream::CCodeTokenStream(CStr const &_Source)
		: mp_Source(_Source)
	{
		fp_Lex();
	}

	TCVector<CCodeToken> const &CCodeTokenStream::f_GetTokens() const
	{
		return mp_Tokens;
	}

	bool CCodeTokenStream::f_IsComplete() const
	{
		return mp_bComplete;
	}

	umint CCodeTokenStream::f_FindToken(umint _iOffset) const
	{
		umint iLow = 0;
		umint iHigh = mp_Tokens.f_GetLen();
		while (iLow < iHigh)
		{
			auto iMiddle = iLow + (iHigh - iLow) / 2;
			if (mp_Tokens[iMiddle].f_GetEnd() <= _iOffset)
				iLow = iMiddle + 1;
			else
				iHigh = iMiddle;
		}

		return iLow;
	}

	bool CCodeTokenStream::f_IsText(CCodeToken const &_Token, CStr const &_Text) const
	{
		if (_Token.m_nLength != _Text.f_GetLen())
			return false;

		auto pToken = mp_Source.f_GetStr() + _Token.m_iOffset;
		for (umint i = 0; i < _Token.m_nLength; ++i)
		{
			if (pToken[i] != _Text.f_GetStr()[i])
				return false;
		}

		return true;
	}

	CStr CCodeTokenStream::f_GetText(CCodeToken const &_Token) const
	{
		return CStr(mp_Source.f_GetStr() + _Token.m_iOffset, _Token.m_nLength);
	}

	CStr const &CCodeTokenStream::f_GetSource() const
	{
		return mp_Source;
	}

	void CCodeTokenStream::f_SplitToken(umint _iToken, umint _nFirstLength)
	{
		auto Second = mp_Tokens[_iToken];
		Second.m_iOffset += _nFirstLength;
		Second.m_nLength -= _nFirstLength;
		mp_Tokens[_iToken].m_nLength = _nFirstLength;
		mp_Tokens.f_InsertBefore(_iToken + 1, Second);
	}

	void CCodeTokenStream::fp_Lex()
	{
		CLexerCursor Cursor{mp_Source.f_GetStr(), umint(mp_Source.f_GetLen()), 0};
		bool bLineStart = true;
		bool bFileStart = true;
		auto fEmit = [&](ECodeTokenKind _Kind, umint _iStart, bool _bUnterminated)
			{
				auto &Token = mp_Tokens.f_Insert();
				Token.m_Kind = _Kind;
				Token.m_iOffset = _iStart;
				Token.m_nLength = Cursor.m_iOffset - _iStart;
				Token.m_bUnterminated = _bUnterminated;
				for (umint i = _iStart; i < Cursor.m_iOffset; ++i)
				{
					if (fg_IsLineBreak(Cursor.m_pStart[i]))
					{
						Token.m_bMultiLine = true;

						break;
					}
				}

				mp_bComplete &= !_bUnterminated;
			}
		;

		while (!Cursor.f_AtEnd())
		{
			auto iStart = Cursor.m_iOffset;
			auto Char = Cursor.f_Peek();
			if (bFileStart)
			{
				bFileStart = false;
				if (Char == ch8(0xEF) && Cursor.f_Peek(1) == ch8(0xBB) && Cursor.f_Peek(2) == ch8(0xBF))
				{
					Cursor.m_iOffset += 3;
					fEmit(ECodeTokenKind::mc_ByteOrderMark, iStart, false);

					continue;
				}
			}

			if (fg_IsLineBreak(Char))
			{
				Cursor.f_SkipLineBreak();
				fEmit(ECodeTokenKind::mc_Newline, iStart, false);
				bLineStart = true;

				continue;
			}

			if (fg_IsHorizontalSpace(Char))
			{
				while (fg_IsHorizontalSpace(Cursor.f_Peek()))
					++Cursor.m_iOffset;

				fEmit(ECodeTokenKind::mc_Whitespace, iStart, false);

				continue;
			}

			if (Char == '#' && bLineStart)
			{
				auto bUnterminated = fg_LexPreprocessor(Cursor);
				fEmit(ECodeTokenKind::mc_Preprocessor, iStart, bUnterminated);

				continue;
			}

			bLineStart = false;
			if (Char == '/' && Cursor.f_Peek(1) == '/')
			{
				fg_LexLineComment(Cursor);
				fEmit(ECodeTokenKind::mc_LineComment, iStart, false);

				continue;
			}

			if (Char == '/' && Cursor.f_Peek(1) == '*')
			{
				auto bUnterminated = fg_LexBlockComment(Cursor);
				fEmit(ECodeTokenKind::mc_BlockComment, iStart, bUnterminated);

				continue;
			}

			if (fg_IsDigit(Char) || (Char == '.' && fg_IsDigit(Cursor.f_Peek(1))))
			{
				fg_LexNumber(Cursor);
				fEmit(ECodeTokenKind::mc_Number, iStart, false);

				continue;
			}

			if (fg_IsIdentifierStart(Char))
			{
				while (fg_IsIdentifierChar(Cursor.f_Peek()))
					++Cursor.m_iOffset;

				auto pPrefix = Cursor.m_pStart + iStart;
				auto nPrefix = Cursor.m_iOffset - iStart;
				auto Quote = Cursor.f_Peek();
				if (Quote == '"' && fg_IsOneOf(pPrefix, nPrefix, {"R", "u8R", "LR", "uR", "UR"}))
				{
					auto bUnterminated = fg_LexRawString(Cursor);
					fEmit(ECodeTokenKind::mc_StringLiteral, iStart, bUnterminated);

					continue;
				}

				if ((Quote == '"' || Quote == '\'') && fg_IsOneOf(pPrefix, nPrefix, {"u8", "L", "u", "U"}))
				{
					auto bUnterminated = fg_LexQuoted(Cursor, Quote);
					fEmit(Quote == '"' ? ECodeTokenKind::mc_StringLiteral : ECodeTokenKind::mc_CharLiteral, iStart, bUnterminated);

					continue;
				}

				fEmit(ECodeTokenKind::mc_Identifier, iStart, false);

				continue;
			}

			if (Char == '"' || Char == '\'')
			{
				auto bUnterminated = fg_LexQuoted(Cursor, Char);
				fEmit(Char == '"' ? ECodeTokenKind::mc_StringLiteral : ECodeTokenKind::mc_CharLiteral, iStart, bUnterminated);

				continue;
			}

			if (Cursor.f_SkipSplice())
			{
				fEmit(ECodeTokenKind::mc_LineSplice, iStart, false);

				continue;
			}

			if (fg_LexPunctuator(Cursor))
			{
				fEmit(ECodeTokenKind::mc_Punctuator, iStart, false);

				continue;
			}

			++Cursor.m_iOffset;
			fEmit(ECodeTokenKind::mc_Unknown, iStart, false);
		}
	}
}
