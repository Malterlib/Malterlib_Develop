// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingLexer.h"
#include "Malterlib_Develop_CodeFormattingStyle.h"

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

	// Bytes at or above 0x80 are treated as identifier characters: the lexer only needs
	// token boundaries, and no punctuator or literal delimiter is encoded that way in UTF-8.
	bool fg_IsIdentifierStart(ch8 _Char)
	{
		return fg_CharIsAnsiAlphabetical(_Char) || _Char == '_' || _Char == '$' || uch8(_Char) >= 0x80;
	}

	bool fg_IsIdentifierChar(ch8 _Char)
	{
		return fg_IsIdentifierStart(_Char) || fg_CharIsNumber(_Char);
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
			auto Char = _pOther[i];
			if (_pText[i] != Char || !Char)
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
	// The build system's strings run over lines, and an escape takes the character behind it, a line break included.
	bool fg_LexMultiLineQuoted(CLexerCursor &_Cursor, ch8 _Quote)
	{
		++_Cursor.m_iOffset;
		while (!_Cursor.f_AtEnd())
		{
			auto Char = _Cursor.f_Peek();
			++_Cursor.m_iOffset;
			if (Char == _Quote)
				return false;

			if (Char == '\\' && !_Cursor.f_AtEnd())
				++_Cursor.m_iOffset;
		}

		return true;
	}

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

			// A splice between an escape and the character it escapes is taken out first: '\\' over a splice escapes what follows.
			if (Char == '\\')
			{
				while (_Cursor.f_SkipSplice())
				{
				}

				if (!_Cursor.f_AtEnd() && !fg_IsLineBreak(_Cursor.f_Peek()))
					++_Cursor.m_iOffset;
			}
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

	// A template string, '`a @(b) c`', holds expressions between '@(' and the matching ')',
	// and an expression can hold strings and template strings of its own. Returns true when
	// the source ends inside it.
	bool fg_LexTemplateString(CLexerCursor &_Cursor)
	{
		// Zero is a template string; an expression is the depth of its parentheses.
		TCVector<umint> Open;
		Open.f_Insert(umint(0));
		++_Cursor.m_iOffset;

		while (!_Cursor.f_AtEnd())
		{
			auto Char = _Cursor.f_Peek();
			auto &Top = Open.f_GetLast();
			if (!Top)
			{
				if (Char == '`')
				{
					++_Cursor.m_iOffset;
					Open.f_Remove(Open.f_GetLen() - 1);
					if (Open.f_IsEmpty())
						return false;

					continue;
				}

				// '@@' is an '@' of the text, and blanks can stand between an '@' and its expression: '@ (a)'.
				if (Char == '\\' && _Cursor.f_Peek(1))
					_Cursor.m_iOffset += 2;
				else if (Char == '@' && _Cursor.f_Peek(1) == '@')
					_Cursor.m_iOffset += 2;
				else if (Char == '@')
				{
					++_Cursor.m_iOffset;
					umint iExpression = 0;
					while (fg_IsHorizontalSpace(_Cursor.f_Peek(iExpression)) || fg_IsLineBreak(_Cursor.f_Peek(iExpression)))
						++iExpression;

					if (_Cursor.f_Peek(iExpression) == '(')
					{
						_Cursor.m_iOffset += iExpression + 1;
						Open.f_Insert(umint(1));
					}
				}
				else
					++_Cursor.m_iOffset;

				continue;
			}

			if (Char == '"' || Char == '\'')
			{
				if (fg_LexMultiLineQuoted(_Cursor, Char))
					return true;

				continue;
			}

			// A comment in an expression holds no bracket of it: '@(/*)*/ x)'.
			if (Char == '/' && _Cursor.f_Peek(1) == '*')
			{
				if (fg_LexBlockComment(_Cursor))
					return true;

				continue;
			}

			if (Char == '/' && _Cursor.f_Peek(1) == '/')
			{
				fg_LexLineComment(_Cursor);

				continue;
			}

			++_Cursor.m_iOffset;
			if (Char == '`')
				Open.f_Insert(umint(0));
			else if (Char == '(')
				++Top;
			else if (Char == ')' && !--Top)
				Open.f_Remove(Open.f_GetLen() - 1);
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

			// A raw string's line breaks are its text, and do not end the directive: '#define DCode R"(' over 'a' over ')"'.
			if (fg_IsIdentifierStart(Char) && (!_Cursor.m_iOffset || !fg_IsIdentifierChar(_Cursor.m_pStart[_Cursor.m_iOffset - 1])))
			{
				// Splices go before tokens are read, so they can stand inside a raw string's prefix and between it and its quote:
				// 'u\' over '8R\' over '"(...)"'. The prefix is read across them.
				auto pText = _Cursor.m_pStart;
				auto nText = _Cursor.m_nLength;
				auto fSkipSplices = [&](umint _iFrom)
					{
						auto iAt = _iFrom;
						while (iAt < nText && pText[iAt] == '\\')
						{
							auto iBreak = iAt + 1;
							while (iBreak < nText && (pText[iBreak] == ' ' || pText[iBreak] == '\t'))
								++iBreak;

							if (iBreak >= nText || (pText[iBreak] != '\n' && pText[iBreak] != '\r'))
								break;

							iAt = iBreak + (pText[iBreak] == '\r' && iBreak + 1 < nText && pText[iBreak + 1] == '\n' ? 2 : 1);
						}

						return iAt;
					}
				;

				ch8 Prefix[3];
				umint nPrefix = 0;
				auto iQuote = _Cursor.m_iOffset;
				while (iQuote < nText && fg_IsIdentifierChar(pText[iQuote]))
				{
					if (nPrefix < sizeof(Prefix))
						Prefix[nPrefix] = pText[iQuote];

					++nPrefix;
					iQuote = fSkipSplices(iQuote + 1);
				}

				while (fg_IsIdentifierChar(_Cursor.f_Peek()))
					++_Cursor.m_iOffset;

				if (nPrefix > sizeof(Prefix) || !fg_IsOneOf(Prefix, nPrefix, {"R", "u8R", "LR", "uR", "UR"}))
					continue;

				if (iQuote < _Cursor.m_nLength && _Cursor.m_pStart[iQuote] == '"')
				{
					_Cursor.m_iOffset = iQuote;
					bUnterminated |= fg_LexRawString(_Cursor);
				}

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
				"<<="
				, ">>="
				, "..."
				, "<=>"
				, "->*"
				, "::"
				, "->"
				, "++"
				, "--"
				, "<<"
				, ">>"
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
				, "##"
				, ".*"
			}
		;

		// A digraph is one punctuator, which the analysis refuses to format. '<::' is '<' and '::' unless a ':' or '>'
		// follows it.
		auto First = _Cursor.f_Peek();
		auto Second = _Cursor.f_Peek(1);
		bool bLessColon = First == '<' && Second == ':' && (_Cursor.f_Peek(2) != ':' || _Cursor.f_Peek(3) == ':' || _Cursor.f_Peek(3) == '>');
		if (First == '%' && Second == ':' && _Cursor.f_Peek(2) == '%' && _Cursor.f_Peek(3) == ':')
		{
			_Cursor.m_iOffset += 4;

			return true;
		}

		if ((First == '<' && Second == '%') || (First == '%' && Second == '>') || (First == '%' && Second == ':') || (First == ':' && Second == '>') || bLessColon)
		{
			_Cursor.m_iOffset += 2;

			return true;
		}

		for (auto pPunctuator : c_pPunctuators)
		{
			if (pPunctuator[0] != First)
				continue;

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

	CCodeTokenStream::CCodeTokenStream(CStr const &_Source, CCodeFormattingStyle const *_pStyle, ECodeLanguage _Language)
		: mp_Source(_Source)
		, mp_Language(_Language)
	{
		if (_Language == ECodeLanguage::mc_BuildSystem)
		{
			fp_LexBuildSystem();

			return;
		}

		fp_Lex();

		// C reserves none of the words C++ adds, so they name what the code declares: 'struct CNode *new;'. C's character
		// types are typedefs, which a declaration can name a variable as: 'const int wchar_t = 1;'.
		if (_Language == ECodeLanguage::mc_C)
		{
			fp_MarkCppBranches();

			constexpr ch8 const *c_pCppKeywords[] =
				{
					"catch"
					, "char8_t"
					, "char16_t"
					, "char32_t"
					, "wchar_t"
					, "class"
					, "concept"
					, "consteval"
					, "constinit"
					, "const_cast"
					, "co_await"
					, "co_return"
					, "co_yield"
					, "decltype"
					, "delete"
					, "dynamic_cast"
					, "explicit"
					, "export"
					, "final"
					, "friend"
					, "import"
					, "module"
					, "mutable"
					, "namespace"
					, "new"
					, "noexcept"
					, "operator"
					, "override"
					, "private"
					, "protected"
					, "public"
					, "reinterpret_cast"
					, "requires"
					, "static_cast"
					, "template"
					, "this"
					, "throw"
					, "try"
					, "typeid"
					, "typename"
					, "using"
					, "virtual"
				}
			;

			for (auto &Token : mp_Tokens)
			{
				if (Token.m_Kind != ECodeTokenKind::mc_Identifier || Token.m_bCpp)
					continue;

				for (auto pKeyword : c_pCppKeywords)
					Token.m_bPlainName |= f_IsText(Token, pKeyword);
			}
		}

		if (!_pStyle)
			return;

		// Each identifier is looked up once, so the rules test a bit rather than a list.
		auto pSource = mp_Source.f_GetStr();
		for (auto &Token : mp_Tokens)
		{
			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
				Token.m_Roles = _pStyle->f_GetRoles(pSource + Token.m_iOffset, Token.m_nLength);
		}
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

	// A literal is compared in place: nearly every rule asks this of every token, and a
	// string built for each question was most of the engine's time.
	bool CCodeTokenStream::f_StartsWith(CCodeToken const &_Token, ch8 const *_pPrefix) const
	{
		auto pToken = mp_Source.f_GetStr() + _Token.m_iOffset;
		for (umint i = 0; _pPrefix[i]; ++i)
		{
			if (i >= _Token.m_nLength || pToken[i] != _pPrefix[i])
				return false;
		}

		return true;
	}

	bool CCodeTokenStream::f_HasSameText(CCodeToken const &_Left, CCodeToken const &_Right) const
	{
		if (_Left.m_nLength != _Right.m_nLength)
			return false;

		auto pSource = mp_Source.f_GetStr();
		for (umint i = 0; i < _Left.m_nLength; ++i)
		{
			if (pSource[_Left.m_iOffset + i] != pSource[_Right.m_iOffset + i])
				return false;
		}

		return true;
	}

	ch8 const *CCodeTokenStream::f_GetTextPointer(CCodeToken const &_Token) const
	{
		return mp_Source.f_GetStr() + _Token.m_iOffset;
	}

	bool CCodeTokenStream::f_IsText(CCodeToken const &_Token, CStr const &_Text) const
	{
		if (_Token.m_nLength != _Text.f_GetLen() || _Token.m_bPlainName)
			return false;

		auto pToken = mp_Source.f_GetStr() + _Token.m_iOffset;
		auto pText = _Text.f_GetStr();
		for (umint i = 0; i < _Token.m_nLength; ++i)
		{
			if (pToken[i] != pText[i])
				return false;
		}

		return true;
	}

	CStr CCodeTokenStream::f_GetText(CCodeToken const &_Token) const
	{
		return mp_Source.f_Extract(_Token.m_iOffset, _Token.m_nLength);
	}

	CStr const &CCodeTokenStream::f_GetSource() const
	{
		return mp_Source;
	}

	ECodeLanguage CCodeTokenStream::f_GetLanguage() const
	{
		return mp_Language;
	}

	bool CCodeTokenStream::f_IsC(CCodeToken const &_Token) const
	{
		return mp_Language == ECodeLanguage::mc_C && !_Token.m_bCpp;
	}
}

namespace
{
	using namespace NMib;

	enum class ECppCondition
	{
		mc_Unknown
		, mc_Cpp    // Only a C++ compiler meets the condition.
		, mc_NotCpp // Only a compiler that is not C++ meets it.
	};

	// Reads a conditional's condition for the forms that prove which language meets it: '__cplusplus', 'defined(__cplusplus)'
	// or 'defined __cplusplus', alone, compared as greater than a version, or in front of a '&&' that no '||' follows, and
	// their negation alone. '__cplusplus' is 0 where C++ is not, so any other use proves nothing.
	ECppCondition fg_ClassifyCppCondition(ch8 const *_pText, umint _nLength, bool _bDefinedOnly)
	{
		// Blanks go, but one between two names keeps them two: 'defined __cplusplus' is no 'defined__cplusplus'.
		NStr::CStr Compact;
		bool bSeparated = false;
		auto fIsNameChar = [](ch8 _Char)
			{
				return fg_CharIsAnsiAlphabetical(_Char) || fg_CharIsNumber(_Char) || _Char == '_';
			}
		;
		for (umint i = 0; i < _nLength; ++i)
		{
			auto Char = _pText[i];

			// A literal's text is no comment: '__has_include("a//b.h")'.
			if (Char == '"' || Char == '\'')
			{
				Compact.f_AddChar(Char);
				for (++i; i < _nLength && _pText[i] != Char; ++i)
				{
					if (_pText[i] == '\\' && i + 1 < _nLength)
						Compact.f_AddChar(_pText[i++]);

					Compact.f_AddChar(_pText[i]);
				}

				if (i < _nLength)
					Compact.f_AddChar(Char);

				bSeparated = false;

				continue;
			}

			if (Char == '/' && i + 1 < _nLength && _pText[i + 1] == '/')
				break;

			if (Char == '/' && i + 1 < _nLength && _pText[i + 1] == '*')
			{
				i += 2;
				while (i + 1 < _nLength && (_pText[i] != '*' || _pText[i + 1] != '/'))
					++i;

				++i;
				bSeparated = true;

				continue;
			}

			// A splice joins the text around it, which it does not separate: 'defined\\' over '__cplusplus' is one name.
			if (Char == '\\')
			{
				auto iAfter = i + 1;
				while (iAfter < _nLength && (_pText[iAfter] == ' ' || _pText[iAfter] == '\t'))
					++iAfter;

				if (iAfter < _nLength && (_pText[iAfter] == '\r' || _pText[iAfter] == '\n'))
				{
					i = iAfter + (_pText[iAfter] == '\r' && iAfter + 1 < _nLength && _pText[iAfter + 1] == '\n');

					continue;
				}

				bSeparated = true;

				continue;
			}

			if (Char == ' ' || Char == '\t' || Char == '\r' || Char == '\n')
			{
				bSeparated = true;

				continue;
			}

			if (bSeparated && fIsNameChar(Char) && !Compact.f_IsEmpty() && fIsNameChar(Compact[Compact.f_GetLen() - 1]))
				Compact.f_AddChar(' ');

			bSeparated = false;
			Compact.f_AddChar(Char);
		}

		if (_bDefinedOnly)
			return Compact == "__cplusplus" ? ECppCondition::mc_Cpp : ECppCondition::mc_Unknown;

		// Parentheses around the whole condition change nothing: '(defined(__cplusplus))', '!(__cplusplus)'.
		auto fStripParentheses = [](NStr::CStr _Condition)
			{
				while (_Condition.f_StartsWith("(") && _Condition.f_EndsWith(")"))
				{
					aint nDepth = 0;
					bool bWraps = true;
					for (umint i = 0; i < _Condition.f_GetLen() && bWraps; ++i)
					{
						nDepth += _Condition[i] == '(' ? 1 : _Condition[i] == ')' ? -1 : 0;
						bWraps = nDepth > 0 || i + 1 == _Condition.f_GetLen();
					}

					if (!bWraps)
						break;

					_Condition = _Condition.f_Extract(1, _Condition.f_GetLen() - 2);
				}

				return _Condition;
			}
		;

		Compact = fStripParentheses(fg_Move(Compact));
		bool bNegated = Compact.f_StartsWith("!");
		auto Rest = bNegated ? fStripParentheses(Compact.f_Extract(1)) : Compact;

		umint nAtom = 0;
		for (auto pAtom : {"defined(__cplusplus)", "defined __cplusplus", "__cplusplus"})
		{
			if (Rest.f_StartsWith(pAtom))
			{
				nAtom = NStr::fg_StrLen(pAtom);

				break;
			}
		}

		if (!nAtom)
			return ECppCondition::mc_Unknown;

		auto pRest = Rest.f_GetStr() + nAtom;
		auto pEnd = Rest.f_GetStr() + Rest.f_GetLen();
		if (bNegated)
			return pRest == pEnd ? ECppCondition::mc_NotCpp : ECppCondition::mc_Unknown;

		if (nAtom == 11 && pRest != pEnd && *pRest == '>')
		{
			++pRest;
			bool bOrEqual = pRest != pEnd && *pRest == '=';
			if (bOrEqual)
				++pRest;

			// Only a comparison 0 fails proves C++: '__cplusplus >= 0' holds in C.
			auto pDigits = pRest;
			bool bAboveZero = false;
			while (pRest != pEnd && fg_CharIsNumber(*pRest))
				bAboveZero |= *pRest++ != '0';

			if (pRest == pDigits || (bOrEqual && !bAboveZero))
				return ECppCondition::mc_Unknown;

			while (pRest != pEnd && (*pRest == 'L' || *pRest == 'l' || *pRest == 'U' || *pRest == 'u'))
				++pRest;
		}

		if (pRest == pEnd)
			return ECppCondition::mc_Cpp;

		// A conjunction holds only where its first term does, unless an alternative or a choice further on lets C in, spelled
		// '||' or as <iso646.h> spells it. Out of scope by decision: a name further on is read as the value it names, which a
		// macro expanding to an alternative would make a choice of its own.
		if (pEnd - pRest >= 2 && pRest[0] == '&' && pRest[1] == '&')
		{
			for (auto pScan = pRest; pScan != pEnd; ++pScan)
			{
				if (*pScan == '?' || (*pScan == '|' && pScan + 1 != pEnd && pScan[1] == '|'))
					return ECppCondition::mc_Unknown;

				bool bWordStart = (pScan == Rest.f_GetStr() || !fIsNameChar(pScan[-1])) && fIsNameChar(*pScan);
				if (!bWordStart)
					continue;

				auto pWordEnd = pScan;
				while (pWordEnd != pEnd && fIsNameChar(*pWordEnd))
					++pWordEnd;

				NStr::CStr Word(pScan, pWordEnd - pScan);
				if (Word == "or" || Word == "bitor")
					return ECppCondition::mc_Unknown;
			}

			return ECppCondition::mc_Cpp;
		}

		return ECppCondition::mc_Unknown;
	}
}

namespace NMib::NDevelop
{
	// The keyword of the directive the text starts with, its '#' first, read past the comments and line splices that can stand
	// around it: '#/**/else', '#en\' over 'dif'. The first 16 letters go to o_Keyword, the length returned counts them all,
	// and o_iAfter is where the text behind the keyword starts.
	umint fg_ReadDirectiveKeyword(ch8 const *_pText, umint _nText, ch8 (&o_Keyword)[16], umint &o_iAfter)
	{
		auto fSplice = [&](umint _iChar) -> umint
			{
				if (_iChar >= _nText || _pText[_iChar] != '\\')
					return 0;

				umint iEnd = _iChar + 1;
				while (iEnd < _nText && (_pText[iEnd] == ' ' || _pText[iEnd] == '\t'))
					++iEnd;

				if (iEnd >= _nText || (_pText[iEnd] != '\n' && _pText[iEnd] != '\r'))
					return 0;

				iEnd += _pText[iEnd] == '\r' && iEnd + 1 < _nText && _pText[iEnd + 1] == '\n' ? 2 : 1;

				return iEnd - _iChar;
			}
		;

		umint iChar = 0;
		while (iChar < _nText && _pText[iChar] != '#')
			++iChar;

		++iChar;
		while (iChar < _nText)
		{
			auto Char = _pText[iChar];
			if (Char == ' ' || Char == '\t')
				++iChar;
			else if (auto nSplice = fSplice(iChar))
				iChar += nSplice;
			else if (Char == '/' && iChar + 1 < _nText && _pText[iChar + 1] == '*')
			{
				auto iClose = NStr::fg_StrFindLengthAware<false>(_pText + iChar + 2, _nText - iChar - 2, "*/");
				iChar = iClose < 0 ? _nText : iChar + 2 + umint(iClose) + 2;
			}
			else
				break;
		}

		umint nKeyword = 0;
		while (iChar < _nText)
		{
			if (fg_CharIsLowerCaseAnsiAlphabetical(_pText[iChar]))
			{
				if (nKeyword < sizeof(o_Keyword))
					o_Keyword[nKeyword] = _pText[iChar];

				++nKeyword;
				++iChar;
			}
			else if (auto nSplice = fSplice(iChar))
				iChar += nSplice;
			else
				break;
		}

		o_iAfter = iChar;

		return nKeyword;
	}

	// A C header carries its C++ interface in conditionals only a C++ compiler takes, which hold C++ however the file is
	// read: '#ifdef __cplusplus', '#if defined(__cplusplus) && ...', '#if __cplusplus >= ...' and the '#else' of
	// '#ifndef __cplusplus'. A condition another compiler can meet, '... || _MSC_VER', holds no C++ of its own.
	void CCodeTokenStream::fp_MarkCppBranches()
	{
		struct CBranch
		{
			bool m_bOuterCpp = false;
			bool m_bElseCpp = false;
			bool m_bCpp = false;
		};

		TCVector<CBranch> Branches;

		auto fCpp = [&]
			{
				return !Branches.f_IsEmpty() && (Branches.f_GetLast().m_bCpp || Branches.f_GetLast().m_bOuterCpp);
			}
		;

		for (auto &Token : mp_Tokens)
		{
			if (Token.m_Kind != ECodeTokenKind::mc_Preprocessor)
			{
				Token.m_bCpp = fCpp();

				continue;
			}

			// Read in place: every directive of the file passes here.
			auto pText = f_GetTextPointer(Token);
			auto nText = Token.m_nLength;
			ch8 Keyword[16];
			umint iChar = 0;
			auto nKeyword = fg_ReadDirectiveKeyword(pText, nText, Keyword, iChar);

			auto fIs = [&](ch8 const *_pKeyword)
				{
					auto nLength = umint(fg_StrLen(_pKeyword));
					return nLength == nKeyword && nKeyword <= sizeof(Keyword) && NMemory::fg_MemCmp((uint8 const *)Keyword, (uint8 const *)_pKeyword, nLength) == 0;
				}
			;

			auto Condition = fg_ClassifyCppCondition(pText + iChar, nText - iChar, fIs("ifdef") || fIs("ifndef"));
			bool bNot = fIs("ifndef") || fIs("elifndef");
			bool bCpp = Condition == ECppCondition::mc_Cpp && !bNot;
			bool bElseCpp = (Condition == ECppCondition::mc_NotCpp && !bNot) || (Condition == ECppCondition::mc_Cpp && bNot);

			if (fIs("if") || fIs("ifdef") || fIs("ifndef"))
			{
				CBranch Branch;
				Branch.m_bOuterCpp = fCpp();
				Branch.m_bCpp = bCpp;
				Branch.m_bElseCpp = bElseCpp;
				Branches.f_Insert(Branch);
			}
			else if ((fIs("elif") || fIs("elifdef") || fIs("elifndef")) && !Branches.f_IsEmpty())
			{
				// A branch behind one compiled only without C++ is compiled only with it, and so is every branch behind it.
				auto &Branch = Branches.f_GetLast();
				Branch.m_bCpp = Branch.m_bElseCpp || bCpp;
				Branch.m_bElseCpp = Branch.m_bElseCpp || bElseCpp;
			}
			else if (fIs("else") && !Branches.f_IsEmpty())
			{
				auto &Branch = Branches.f_GetLast();
				Branch.m_bCpp = Branch.m_bElseCpp;
			}
			else if (fIs("endif") && !Branches.f_IsEmpty())
				Branches.f_Remove(Branches.f_GetLen() - 1);

			Token.m_bCpp = fCpp();
		}
	}

	// The tokens are ascending and distinct. The stream is rebuilt once: a file holding many of them would otherwise move
	// every token behind each one.
	void CCodeTokenStream::f_SplitTokens(NContainer::TCVector<umint> const &_Tokens, umint _nFirstLength)
	{
		NContainer::TCVector<CCodeToken> Tokens;
		Tokens.f_Reserve(mp_Tokens.f_GetLen() + _Tokens.f_GetLen());
		umint iSplit = 0;
		for (umint i = 0; i < mp_Tokens.f_GetLen(); ++i)
		{
			auto &First = Tokens.f_Insert(mp_Tokens[i]);
			if (iSplit >= _Tokens.f_GetLen() || _Tokens[iSplit] != i)
				continue;

			++iSplit;
			auto Second = First;
			Second.m_iOffset += _nFirstLength;
			Second.m_nLength -= _nFirstLength;
			First.m_nLength = _nFirstLength;
			Tokens.f_Insert(fg_Move(Second));
		}

		mp_Tokens = fg_Move(Tokens);
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

			// A block comment is one space to the preprocessor, so a directive may still follow it. One spanning lines continues
			// the logical line it started on, as the compilers read it: 'int a; /* c\n*/ #define F (x)' holds no directive.
			if (Char == '/' && Cursor.f_Peek(1) == '*')
			{
				auto bUnterminated = fg_LexBlockComment(Cursor);
				fEmit(ECodeTokenKind::mc_BlockComment, iStart, bUnterminated);

				continue;
			}

			// A splice joins its lines before directives are recognized, so a line it starts still starts a logical line.
			if (Cursor.f_SkipSplice())
			{
				fEmit(ECodeTokenKind::mc_LineSplice, iStart, false);

				continue;
			}

			bLineStart = false;

			if (Char == '/' && Cursor.f_Peek(1) == '/')
			{
				fg_LexLineComment(Cursor);
				fEmit(ECodeTokenKind::mc_LineComment, iStart, false);

				continue;
			}

			if (fg_CharIsNumber(Char) || (Char == '.' && fg_CharIsNumber(Cursor.f_Peek(1))))
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

			// Objective-C spells its keywords '@end', its strings '@"..."' and its object literals '@[...]', '@{...}' and
			// '@(...)'.
			if (Char == '@')
			{
				++Cursor.m_iOffset;
				auto Next = Cursor.f_Peek();
				if (Next == '"')
				{
					auto bUnterminated = fg_LexQuoted(Cursor, Next);
					fEmit(ECodeTokenKind::mc_StringLiteral, iStart, bUnterminated);
				}
				else if (fg_IsIdentifierStart(Next))
				{
					while (fg_IsIdentifierChar(Cursor.f_Peek()))
						++Cursor.m_iOffset;

					fEmit(ECodeTokenKind::mc_Identifier, iStart, false);
				}
				else
					fEmit(ECodeTokenKind::mc_Punctuator, iStart, false);

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

namespace NMib::NDevelop
{
	// A backslash that ends a line continues the value on the next line, as it does in the
	// registry parser; only the backslash is the splice, so the space around it stays layout.
	void CCodeTokenStream::fp_LexBuildSystem()
	{
		CLexerCursor Cursor{mp_Source.f_GetStr(), umint(mp_Source.f_GetLen()), 0};

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

		if (Cursor.f_Peek() == ch8(0xEF) && Cursor.f_Peek(1) == ch8(0xBB) && Cursor.f_Peek(2) == ch8(0xBF))
		{
			Cursor.m_iOffset += 3;
			fEmit(ECodeTokenKind::mc_ByteOrderMark, 0, false);
		}

		while (!Cursor.f_AtEnd())
		{
			auto iStart = Cursor.m_iOffset;
			auto Char = Cursor.f_Peek();
			if (fg_IsLineBreak(Char))
			{
				Cursor.f_SkipLineBreak();
				fEmit(ECodeTokenKind::mc_Newline, iStart, false);

				continue;
			}

			if (fg_IsHorizontalSpace(Char))
			{
				while (fg_IsHorizontalSpace(Cursor.f_Peek()))
					++Cursor.m_iOffset;

				fEmit(ECodeTokenKind::mc_Whitespace, iStart, false);

				continue;
			}

			if (Char == '/' && Cursor.f_Peek(1) == '/')
			{
				Cursor.m_iOffset += 2;
				while (!Cursor.f_AtEnd() && !fg_IsLineBreak(Cursor.f_Peek()))
					++Cursor.m_iOffset;

				fEmit(ECodeTokenKind::mc_LineComment, iStart, false);

				continue;
			}

			if (Char == '/' && Cursor.f_Peek(1) == '*')
			{
				auto bUnterminated = fg_LexBlockComment(Cursor);
				fEmit(ECodeTokenKind::mc_BlockComment, iStart, bUnterminated);

				continue;
			}

			if (Char == '"' || Char == '\'')
			{
				auto bUnterminated = fg_LexMultiLineQuoted(Cursor, Char);
				fEmit(ECodeTokenKind::mc_StringLiteral, iStart, bUnterminated);

				continue;
			}

			if (Char == '`')
			{
				auto bUnterminated = fg_LexTemplateString(Cursor);
				fEmit(ECodeTokenKind::mc_StringLiteral, iStart, bUnterminated);

				continue;
			}

			// In a name a backslash escapes the character behind it, a blank as well, which is part of the name: 'Fast\ Compile',
			// 'C\+\+', 'Value\ '. Starting a name it escapes all but a blank.
			auto fIsEscape = [&](umint _iAhead, bool _bInName)
				{
					auto Escaped = Cursor.f_Peek(_iAhead + 1);
					return Cursor.f_Peek(_iAhead) == '\\' && Escaped && !fg_IsLineBreak(Escaped) && (_bInName || !fg_IsHorizontalSpace(Escaped));
				}
			;

			if (Char == '\\' && !fIsEscape(0, false))
			{
				umint iAhead = 1;
				while (fg_IsHorizontalSpace(Cursor.f_Peek(iAhead)))
					++iAhead;

				++Cursor.m_iOffset;
				auto Next = Cursor.f_Peek(iAhead - 1);
				fEmit(!Next || fg_IsLineBreak(Next) ? ECodeTokenKind::mc_LineSplice : ECodeTokenKind::mc_Punctuator, iStart, false);

				continue;
			}

			if (fg_IsIdentifierChar(Char) || Char == '.' || Char == '\\')
			{
				while (true)
				{
					auto Peek = Cursor.f_Peek();
					if (fIsEscape(0, Cursor.m_iOffset > iStart))
						Cursor.m_iOffset += 2;
					else if (Peek != '\\' && (fg_IsIdentifierChar(Peek) || Peek == '.'))
						++Cursor.m_iOffset;
					else
						break;
				}

				fEmit(ECodeTokenKind::mc_Identifier, iStart, false);

				continue;
			}

			++Cursor.m_iOffset;
			fEmit(uch8(Char) < 0x20 || Char == 0x7F ? ECodeTokenKind::mc_Unknown : ECodeTokenKind::mc_Punctuator, iStart, false);
		}
	}
}
