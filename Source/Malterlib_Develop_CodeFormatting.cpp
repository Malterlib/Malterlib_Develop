// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormatting.h"
#include "Malterlib_Develop_CodeFormattingLexer.h"

#include <Mib/File/File>

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
	using namespace NStorage;
	using namespace NFile;
}

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;

	CStr const gc_FormatOffDirective = "// malterlib-format off";
	CStr const gc_FormatOnDirective = "// malterlib-format on";

	umint fg_ParsePositiveProperty(CStr const &_Value, CStr const &_Name)
	{
		umint Value = 0;
		for (auto pParse = _Value.f_GetStr(); *pParse; ++pParse)
		{
			auto Char = *pParse;
			if (Char < '0' || Char > '9' || Value > (TCLimitsInt<umint>::mc_Max - umint(Char - '0')) / 10)
				DMibError("Invalid {}: '{}'"_f << _Name << _Value);

			Value = Value * 10 + umint(Char - '0');
		}

		if (!Value)
			DMibError("Invalid {}: '{}' (expected a positive integer)"_f << _Name << _Value);

		return Value;
	}

	CStr const *fg_FindProperty(CEditorConfigProperties const &_Properties, CStr const &_Name)
	{
		auto pValue = _Properties.f_FindEqual(_Name);
		if (pValue && *pValue == "unset")
			return nullptr;

		return pValue;
	}

	bool fg_ParseBooleanProperty(CStr const &_Value, CStr const &_Name)
	{
		if (_Value == "true")
			return true;
		if (_Value == "false")
			return false;

		DMibError("Invalid {}: '{}' (expected true or false)"_f << _Name << _Value);
	}

	bool fg_IsSpaceOrTab(ch8 _Char)
	{
		return _Char == ' ' || _Char == '\t';
	}

	bool fg_IsValidUtf8(CStr const &_Text)
	{
		if (_Text.f_IsEmpty())
			return true;

		CStrIteratorUTF8 End(_Text.f_GetStr() + _Text.f_GetLen(), 0);
		for (CStrIteratorUTF8 Iterator(_Text); ; ++Iterator)
		{
			if (Iterator.f_IsBroken() || !Iterator.f_IsWholeCodePoint())
				return false;

			if (Iterator - End >= 0)
				break;
		}

		return true;
	}

	// A UTF-8 continuation byte can never start a code point, so a range boundary there
	// would split an encoded character.
	bool fg_IsCodePointBoundary(CStr const &_Text, umint _iOffset)
	{
		if (_iOffset >= _Text.f_GetLen())
			return true;

		return (uch8(_Text.f_GetStr()[_iOffset]) & 0xC0) != 0x80;
	}

	CStr fg_TrimCommentTrailingSpace(CStr const &_Text)
	{
		auto nLength = _Text.f_GetLen();
		while (nLength && fg_IsSpaceOrTab(_Text.f_GetStr()[nLength - 1]))
			--nLength;

		return CStr(_Text.f_GetStr(), nLength);
	}
}

namespace NMib::NDevelop
{
	CCodeFormattingSettings::CCodeFormattingSettings(CEditorConfigProperties const &_Properties)
	{
		if (auto pValue = fg_FindProperty(_Properties, "malterlib_format"))
		{
			auto Value = pValue->f_LowerCase();
			if (Value == "malterlib")
				m_Profile = ECodeFormattingProfile::mc_Malterlib;
			else if (Value != "off")
				DMibError("Invalid malterlib_format: '{}' (expected malterlib, off, or unset)"_f << *pValue);
		}

		if (auto pValue = fg_FindProperty(_Properties, "indent_style"))
		{
			if (*pValue == "tab")
				m_bIndentWithTabs = true;
			else if (*pValue == "space")
				m_bIndentWithTabs = false;
			else
				DMibError("Invalid indent_style: '{}' (expected tab or space)"_f << *pValue);
		}

		if (auto pValue = fg_FindProperty(_Properties, "indent_size"))
		{
			if (*pValue != "tab")
				m_nIndentSize = fg_ParsePositiveProperty(*pValue, "indent_size");
		}

		auto pWidth = fg_FindProperty(_Properties, "tab_width");
		if (!pWidth)
			pWidth = fg_FindProperty(_Properties, "indent_size");

		if (pWidth && *pWidth != "tab")
			m_nTabWidth = fg_ParsePositiveProperty(*pWidth, "tab_width");

		if (m_Profile != ECodeFormattingProfile::mc_Disabled)
			m_nMaxColumns = 190;

		if (auto pValue = fg_FindProperty(_Properties, "max_line_length"))
		{
			m_bHasExplicitMaxColumns = true;
			m_nMaxColumns = *pValue == "off" ? 0 : fg_ParsePositiveProperty(*pValue, "max_line_length");
		}

		if (auto pValue = fg_FindProperty(_Properties, "trim_trailing_whitespace"))
			m_bTrimTrailingWhitespace = fg_ParseBooleanProperty(*pValue, "trim_trailing_whitespace");

		if (auto pValue = fg_FindProperty(_Properties, "insert_final_newline"))
			m_bInsertFinalNewline = fg_ParseBooleanProperty(*pValue, "insert_final_newline");

		if (auto pValue = fg_FindProperty(_Properties, "end_of_line"))
		{
			if (*pValue == "lf")
				m_EndOfLine = ETextLineEnding::mc_LF;
			else if (*pValue == "crlf")
				m_EndOfLine = ETextLineEnding::mc_CRLF;
			else if (*pValue == "cr")
				m_EndOfLine = ETextLineEnding::mc_CR;
			else
				DMibError("Invalid end_of_line: '{}' (expected lf, crlf, or cr)"_f << *pValue);
		}

		if (auto pValue = fg_FindProperty(_Properties, "charset"))
			m_Charset = pValue->f_LowerCase();
	}

	bool CCodeFormattingSettings::f_IsFormattingEnabled() const
	{
		return m_Profile != ECodeFormattingProfile::mc_Disabled;
	}

	ECodeLanguage fg_DetectCodeLanguage(CStr const &_Path)
	{
		auto Name = CFile::fs_GetFile(_Path);
		auto Extension = CFile::fs_GetExtension(Name).f_LowerCase();
		if (!Extension)
		{
			// Public include wrappers have no extension; they are ordinary C++ headers.
			return Name ? ECodeLanguage::mc_Cpp : ECodeLanguage::mc_Unknown;
		}

		for (auto pCandidate : {"c", "cc", "cpp", "cxx", "c++", "h", "hh", "hpp", "hxx", "h++", "inl", "ipp"})
		{
			if (Extension == pCandidate)
				return ECodeLanguage::mc_Cpp;
		}

		return ECodeLanguage::mc_Unknown;
	}

	umint CCodeFormattingRange::f_GetEnd() const
	{
		return m_iOffset + m_nLength;
	}

	umint CCodeFormattingEdit::f_GetEnd() const
	{
		return m_iOffset + m_nLength;
	}

	bool CCodeFormattingResult::f_HasEdits() const
	{
		return !m_Edits.f_IsEmpty();
	}

	bool CCodeFormattingResult::f_HasUnfixableDiagnostics() const
	{
		for (auto const &Diagnostic : m_Diagnostics)
		{
			if (!Diagnostic.m_bHasAutomaticFix)
				return true;
		}

		return false;
	}

	CStr fg_ApplyCodeFormattingEdits(CStr const &_Source, TCVector<CCodeFormattingEdit> const &_Edits)
	{
		CStr Result;
		umint iCopied = 0;
		for (auto const &Edit : _Edits)
		{
			if (Edit.m_iOffset < iCopied || Edit.f_GetEnd() > _Source.f_GetLen())
				DMibError("Formatting edits must be ordered, non-overlapping, and inside the source");

			Result += CStr(_Source.f_GetStr() + iCopied, Edit.m_iOffset - iCopied);
			Result += Edit.m_Replacement;
			iCopied = Edit.f_GetEnd();
		}

		Result += CStr(_Source.f_GetStr() + iCopied, _Source.f_GetLen() - iCopied);

		return Result;
	}

	bool fg_HasEquivalentCodeTokens(CStr const &_First, CStr const &_Second)
	{
		CCodeTokenStream FirstStream(_First);
		CCodeTokenStream SecondStream(_Second);
		auto fSignificant = [](TCVector<CCodeToken> const &_Tokens)
			{
				TCVector<umint> Indices;
				for (umint i = 0; i < _Tokens.f_GetLen(); ++i)
				{
					auto Kind = _Tokens[i].m_Kind;
					if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline && Kind != ECodeTokenKind::mc_LineSplice)
						Indices.f_Insert(i);
				}

				return Indices;
			}
		;

		auto FirstIndices = fSignificant(FirstStream.f_GetTokens());
		auto SecondIndices = fSignificant(SecondStream.f_GetTokens());
		if (FirstIndices.f_GetLen() != SecondIndices.f_GetLen())
			return false;

		for (umint i = 0; i < FirstIndices.f_GetLen(); ++i)
		{
			auto const &FirstToken = FirstStream.f_GetTokens()[FirstIndices[i]];
			auto const &SecondToken = SecondStream.f_GetTokens()[SecondIndices[i]];
			if (FirstToken.m_Kind != SecondToken.m_Kind)
				return false;

			auto FirstText = FirstStream.f_GetText(FirstToken);
			auto SecondText = SecondStream.f_GetText(SecondToken);
			// Trailing space inside a line comment is layout, not comment text.
			if (FirstToken.m_Kind == ECodeTokenKind::mc_LineComment)
			{
				FirstText = fg_TrimCommentTrailingSpace(FirstText);
				SecondText = fg_TrimCommentTrailingSpace(SecondText);
			}

			if (FirstText != SecondText)
				return false;
		}

		return true;
	}
}

namespace
{
	struct CFormattingAnalyzer
	{
		explicit CFormattingAnalyzer(CCodeFormattingRequest const &_Request)
			: m_Request(_Request)
			, m_Tokens(_Request.m_Source)
			, m_Lines(_Request.m_Source)
		{
		}

		CCodeFormattingResult f_Analyze(bool _bVerify);

	private:
		void fp_PrepareLineProtection();
		bool fp_CollectDisabledRegions(CStr &o_Explanation);
		bool fp_ResolveRanges(CStr &o_Explanation);
		void fp_AddEdit(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Replacement, CStr const &_Explanation);
		void fp_AddDiagnostic(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Explanation, bool _bHasAutomaticFix);

		bool fp_IsDisabled(umint _iOffset, umint _nLength) const;
		bool fp_IsSelected(umint _iOffset, umint _nLength) const;
		bool fp_IsLineSelected(umint _iLine) const;
		umint fp_GetColumn(umint _iOffset) const;
		ETextLineEnding fp_GetDefaultLineEnding() const;

		aint fp_PreviousSignificant(umint _iToken) const;
		aint fp_NextSignificant(umint _iToken) const;
		aint fp_PreviousCode(umint _iToken) const;
		aint fp_NextCode(umint _iToken) const;
		bool fp_IsFirstOnLine(umint _iToken) const;
		bool fp_IsLastOnLine(umint _iToken) const;

		void fp_RuleIndentation();
		void fp_RuleTrailingWhitespace();
		void fp_RuleLineEndings();
		void fp_RuleFinalNewline();
		void fp_RuleTokenSpacing();
		bool fp_HasOperand(umint _iToken, bool _bBefore) const;
		void fp_RuleBlankLines();
		void fp_DiagnoseLineLength();
		void fp_EnsureSingleSpace(umint _iToken, bool _bBefore, CStr const &_Rule, CStr const &_Explanation);
		void fp_RemoveSpaceBefore(umint _iToken, CStr const &_Rule, CStr const &_Explanation);
		void fp_RemoveFollowingBlankLines(umint _iToken, CStr const &_Rule, CStr const &_Explanation);

		CCodeFormattingRequest const &m_Request;
		CCodeTokenStream m_Tokens;
		CTextLineMap m_Lines;
		NContainer::TCVector<uint8> m_bProtectedStart;
		NContainer::TCVector<uint8> m_bProtectedEnd;
		NContainer::TCVector<CCodeFormattingRange> m_Disabled;
		NContainer::TCVector<CCodeFormattingRange> m_Effective;
		NContainer::TCVector<CCodeFormattingEdit> m_Edits;
		NContainer::TCVector<NStr::CStr> m_EditExplanations;
		NContainer::TCVector<CCodeFormattingDiagnostic> m_Diagnostics;
		bool m_bWholeFile = false;
	};

	void CFormattingAnalyzer::fp_PrepareLineProtection()
	{
		m_bProtectedStart.f_SetLen(m_Lines.f_GetLineCount());
		m_bProtectedEnd.f_SetLen(m_Lines.f_GetLineCount());
		for (umint i = 0; i < m_Lines.f_GetLineCount(); ++i)
		{
			m_bProtectedStart[i] = 0;
			m_bProtectedEnd[i] = 0;
		}

		for (auto const &Token : m_Tokens.f_GetTokens())
		{
			if (!Token.m_bMultiLine || Token.m_Kind == ECodeTokenKind::mc_Newline)
				continue;

			auto iFirst = m_Lines.f_FindLine(Token.m_iOffset);
			auto iLast = m_Lines.f_FindLine(Token.f_GetEnd() ? Token.f_GetEnd() - 1 : 0);
			for (umint i = iFirst; i < iLast; ++i)
				m_bProtectedEnd[i] = 1;

			// A splice only protects the end of the line carrying the backslash; the
			// spliced line's own indentation stays ordinary layout.
			if (Token.m_Kind == ECodeTokenKind::mc_LineSplice)
				continue;

			for (umint i = iFirst + 1; i <= iLast; ++i)
				m_bProtectedStart[i] = 1;
		}
	}

	bool CFormattingAnalyzer::fp_CollectDisabledRegions(CStr &o_Explanation)
	{
		bool bDisabled = false;
		umint iDisabledStart = 0;
		umint iDirective = 0;
		for (auto const &Token : m_Tokens.f_GetTokens())
		{
			if (Token.m_Kind != ECodeTokenKind::mc_LineComment)
				continue;

			auto Text = fg_TrimCommentTrailingSpace(m_Tokens.f_GetText(Token));
			if (Text == gc_FormatOffDirective)
			{
				if (bDisabled)
				{
					o_Explanation = "Nested '{}' directive at line {}"_f << gc_FormatOffDirective << m_Lines.f_FindLine(Token.m_iOffset) + 1;

					return false;
				}

				bDisabled = true;
				iDisabledStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Token.m_iOffset));
				iDirective = Token.m_iOffset;

				continue;
			}

			if (Text != gc_FormatOnDirective)
				continue;

			if (!bDisabled)
			{
				o_Explanation = "'{}' at line {} has no matching '{}'"_f
					<< gc_FormatOnDirective << m_Lines.f_FindLine(Token.m_iOffset) + 1 << gc_FormatOffDirective
				;

				return false;
			}

			auto &Range = m_Disabled.f_Insert();
			Range.m_iOffset = iDisabledStart;
			Range.m_nLength = m_Lines.f_GetLineEnd(m_Lines.f_FindLine(Token.m_iOffset)) - iDisabledStart;
			bDisabled = false;
		}

		if (bDisabled)
		{
			o_Explanation = "'{}' at line {} has no matching '{}'"_f
				<< gc_FormatOffDirective << m_Lines.f_FindLine(iDirective) + 1 << gc_FormatOnDirective
			;

			return false;
		}

		return true;
	}

	bool CFormattingAnalyzer::fp_ResolveRanges(CStr &o_Explanation)
	{
		auto nSource = m_Request.m_Source.f_GetLen();
		m_bWholeFile = m_Request.m_Ranges.f_IsEmpty();
		if (m_bWholeFile)
		{
			auto &Range = m_Effective.f_Insert();
			Range.m_nLength = nSource;

			return true;
		}

		NContainer::TCVector<CCodeFormattingRange> Requested;
		for (auto const &Range : m_Request.m_Ranges)
		{
			if (Range.m_nLength > nSource || Range.m_iOffset > nSource - Range.m_nLength)
			{
				o_Explanation = "Range {}+{} is outside the {} byte source"_f << Range.m_iOffset << Range.m_nLength << nSource;

				return false;
			}

			if (!fg_IsCodePointBoundary(m_Request.m_Source, Range.m_iOffset) || !fg_IsCodePointBoundary(m_Request.m_Source, Range.f_GetEnd()))
			{
				o_Explanation = "Range {}+{} starts or ends inside an encoded code point"_f << Range.m_iOffset << Range.m_nLength;

				return false;
			}

			auto fSplitsLineEnding = [&](umint _iOffset)
				{
					return _iOffset && _iOffset < nSource
						&& m_Request.m_Source.f_GetStr()[_iOffset - 1] == '\r' && m_Request.m_Source.f_GetStr()[_iOffset] == '\n'
					;
				}
			;
			if (fSplitsLineEnding(Range.m_iOffset) || fSplitsLineEnding(Range.f_GetEnd()))
			{
				o_Explanation = "Range {}+{} starts or ends inside a CRLF pair"_f << Range.m_iOffset << Range.m_nLength;

				return false;
			}

			auto Resolved = Range;
			if (m_Request.m_RangePolicy == ECodeRangePolicy::mc_Expand)
			{
				// A zero-length range is a cursor: it selects the line it sits on, and at
				// end of file the preceding line. An empty file has an empty selection.
				auto iFirst = m_Lines.f_FindLine(Range.m_iOffset);
				auto iLast = m_Lines.f_FindLine(Range.f_GetEnd() ? Range.f_GetEnd() - 1 : Range.m_iOffset);
				Resolved.m_iOffset = m_Lines.f_GetLineStart(iFirst);
				Resolved.m_nLength = m_Lines.f_GetLineEnd(iLast) - Resolved.m_iOffset;
			}

			Requested.f_Insert(Resolved);
		}

		Requested.f_Sort
			(
				[](CCodeFormattingRange const &_Left, CCodeFormattingRange const &_Right)
				{
					return _Left.m_iOffset <=> _Right.m_iOffset;
				}
			)
		;
		for (auto const &Range : Requested)
		{
			if (!m_Effective.f_IsEmpty() && Range.m_iOffset <= m_Effective.f_GetLast().f_GetEnd())
			{
				auto &Last = m_Effective.f_GetLast();
				Last.m_nLength = fg_Max(Last.f_GetEnd(), Range.f_GetEnd()) - Last.m_iOffset;

				continue;
			}

			m_Effective.f_Insert(Range);
		}

		return true;
	}

	bool CFormattingAnalyzer::fp_IsDisabled(umint _iOffset, umint _nLength) const
	{
		for (auto const &Range : m_Disabled)
		{
			if (_iOffset < Range.f_GetEnd() && Range.m_iOffset < _iOffset + fg_Max(_nLength, umint(1)))
				return true;
		}

		return false;
	}

	bool CFormattingAnalyzer::fp_IsSelected(umint _iOffset, umint _nLength) const
	{
		for (auto const &Range : m_Effective)
		{
			if (_iOffset >= Range.m_iOffset && _iOffset + _nLength <= Range.f_GetEnd())
				return true;
		}

		return false;
	}

	bool CFormattingAnalyzer::fp_IsLineSelected(umint _iLine) const
	{
		auto iStart = m_Lines.f_GetLineStart(_iLine);
		auto iEnd = m_Lines.f_GetLineEnd(_iLine);
		for (auto const &Range : m_Effective)
		{
			if (iStart < Range.f_GetEnd() && Range.m_iOffset < fg_Max(iEnd, iStart + 1))
				return true;
		}

		return false;
	}

	umint CFormattingAnalyzer::fp_GetColumn(umint _iOffset) const
	{
		auto iLine = m_Lines.f_FindLine(_iOffset);
		auto iStart = m_Lines.f_GetLineStart(iLine);
		umint nColumns = 0;
		if (!fg_MeasureTextColumns(m_Request.m_Source.f_GetStr() + iStart, _iOffset - iStart, m_Request.m_Settings.m_nTabWidth, nColumns))
			return 1;

		return nColumns + 1;
	}

	ETextLineEnding CFormattingAnalyzer::fp_GetDefaultLineEnding() const
	{
		if (m_Request.m_Settings.m_EndOfLine)
			return *m_Request.m_Settings.m_EndOfLine;

		for (umint i = 0; i < m_Lines.f_GetLineCount(); ++i)
		{
			if (m_Lines.f_GetLine(i).m_Ending != ETextLineEnding::mc_None)
				return m_Lines.f_GetLine(i).m_Ending;
		}

		return ETextLineEnding::mc_LF;
	}

	void CFormattingAnalyzer::fp_AddDiagnostic(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Explanation, bool _bHasAutomaticFix)
	{
		auto &Diagnostic = m_Diagnostics.f_Insert();
		Diagnostic.m_Rule = _Rule;
		Diagnostic.m_Severity = _bHasAutomaticFix ? ECodeFormattingSeverity::mc_Warning : ECodeFormattingSeverity::mc_Error;
		Diagnostic.m_iOffset = _iOffset;
		Diagnostic.m_nLength = _nLength;
		Diagnostic.m_iLine = m_Lines.f_FindLine(_iOffset) + 1;
		Diagnostic.m_iColumn = fp_GetColumn(_iOffset);
		Diagnostic.m_Explanation = _Explanation;
		Diagnostic.m_bHasAutomaticFix = _bHasAutomaticFix;
	}

	void CFormattingAnalyzer::fp_AddEdit(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Replacement, CStr const &_Explanation)
	{
		if (fp_IsDisabled(_iOffset, _nLength))
			return;

		if (!fp_IsSelected(_iOffset, _nLength))
		{
			if (m_Request.m_RangePolicy == ECodeRangePolicy::mc_Strict)
			{
				fp_AddDiagnostic
					(
						"range-boundary", _iOffset, _nLength
						, "{} requires an edit outside the requested range; no edit was applied to this unit"_f << _Rule
						, false
					)
				;
			}

			return;
		}

		auto &Edit = m_Edits.f_Insert();
		Edit.m_iOffset = _iOffset;
		Edit.m_nLength = _nLength;
		Edit.m_Replacement = _Replacement;
		Edit.m_Rule = _Rule;
		m_EditExplanations.f_Insert(_Explanation);
	}
}

namespace
{
	aint CFormattingAnalyzer::fp_PreviousSignificant(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		aint i = aint(_iToken) - 1;
		while (i >= 0 && Tokens[umint(i)].m_Kind == ECodeTokenKind::mc_Whitespace)
			--i;

		return i;
	}

	aint CFormattingAnalyzer::fp_NextSignificant(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		umint i = _iToken + 1;
		while (i < Tokens.f_GetLen() && Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace)
			++i;

		return i < Tokens.f_GetLen() ? aint(i) : -1;
	}

	// The previous token that carries meaning, skipping layout and comments.
	aint CFormattingAnalyzer::fp_PreviousCode(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		aint i = aint(_iToken) - 1;
		while (i >= 0)
		{
			switch (Tokens[umint(i)].m_Kind)
			{
				case ECodeTokenKind::mc_ByteOrderMark:
				case ECodeTokenKind::mc_Whitespace:
				case ECodeTokenKind::mc_Newline:
				case ECodeTokenKind::mc_LineSplice:
				case ECodeTokenKind::mc_LineComment:
				case ECodeTokenKind::mc_BlockComment:
				case ECodeTokenKind::mc_Preprocessor:
					--i;

					continue;
				default: return i;
			}
		}

		return -1;
	}

	// The next token that carries meaning, skipping layout and comments.
	aint CFormattingAnalyzer::fp_NextCode(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		umint i = _iToken + 1;
		while (i < Tokens.f_GetLen())
		{
			switch (Tokens[i].m_Kind)
			{
				case ECodeTokenKind::mc_ByteOrderMark:
				case ECodeTokenKind::mc_Whitespace:
				case ECodeTokenKind::mc_Newline:
				case ECodeTokenKind::mc_LineSplice:
				case ECodeTokenKind::mc_LineComment:
				case ECodeTokenKind::mc_BlockComment:
				case ECodeTokenKind::mc_Preprocessor:
					++i;

					continue;
				default: return aint(i);
			}
		}

		return -1;
	}

	bool CFormattingAnalyzer::fp_IsFirstOnLine(umint _iToken) const
	{
		auto iPrevious = fp_PreviousSignificant(_iToken);

		return iPrevious < 0 || m_Tokens.f_GetTokens()[umint(iPrevious)].m_bMultiLine;
	}

	bool CFormattingAnalyzer::fp_IsLastOnLine(umint _iToken) const
	{
		auto iNext = fp_NextSignificant(_iToken);

		return iNext < 0 || m_Tokens.f_GetTokens()[umint(iNext)].m_bMultiLine;
	}

	// Decides whether an operator token has an operand on the given side. An adjacent
	// opening delimiter or separator means there is none.
	bool CFormattingAnalyzer::fp_HasOperand(umint _iToken, bool _bBefore) const
	{
		if (!_bBefore)
		{
			auto iNext = fp_NextCode(_iToken);
			if (iNext < 0)
				return false;

			_iToken = umint(iNext);
			auto const &Next = m_Tokens.f_GetTokens()[_iToken];

			return !m_Tokens.f_IsText(Next, ")") && !m_Tokens.f_IsText(Next, "]") && !m_Tokens.f_IsText(Next, "}")
				&& !m_Tokens.f_IsText(Next, ",") && !m_Tokens.f_IsText(Next, ";")
			;
		}

		auto const &Previous = m_Tokens.f_GetTokens()[_iToken];

		return !m_Tokens.f_IsText(Previous, "(") && !m_Tokens.f_IsText(Previous, "[") && !m_Tokens.f_IsText(Previous, "{")
			&& !m_Tokens.f_IsText(Previous, ",") && !m_Tokens.f_IsText(Previous, ";")
		;
	}

	void CFormattingAnalyzer::fp_RuleIndentation()
	{
		auto const &Source = m_Request.m_Source;
		auto const &Settings = m_Request.m_Settings;
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			if (m_bProtectedStart[iLine] || !fp_IsLineSelected(iLine))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
			auto iIndent = iStart;
			while (iIndent < iEnd && fg_IsSpaceOrTab(Source.f_GetStr()[iIndent]))
				++iIndent;

			// An all-whitespace line is normalized by the trailing whitespace rule.
			if (iIndent == iEnd)
				continue;

			umint nColumns = 0;
			if (!fg_MeasureTextColumns(Source.f_GetStr() + iStart, iIndent - iStart, Settings.m_nTabWidth, nColumns))
				continue;

			CStr Canonical;
			if (Settings.m_bIndentWithTabs)
			{
				for (umint i = 0; i < nColumns / Settings.m_nTabWidth; ++i)
					Canonical += "\t";

				for (umint i = 0; i < nColumns % Settings.m_nTabWidth; ++i)
					Canonical += " ";
			}
			else
			{
				for (umint i = 0; i < nColumns; ++i)
					Canonical += " ";
			}

			if (Canonical == CStr(Source.f_GetStr() + iStart, iIndent - iStart))
				continue;

			fp_AddEdit
				(
					"indentation", iStart, iIndent - iStart, Canonical
					, Settings.m_bIndentWithTabs ? "indentation must use tabs" : "indentation must use spaces"
				)
			;
		}
	}

	void CFormattingAnalyzer::fp_RuleTrailingWhitespace()
	{
		if (!m_Request.m_Settings.m_bTrimTrailingWhitespace)
			return;

		auto const &Source = m_Request.m_Source;
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			if (m_bProtectedEnd[iLine] || !fp_IsLineSelected(iLine))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
			auto iTrim = iEnd;
			while (iTrim > iStart && fg_IsSpaceOrTab(Source.f_GetStr()[iTrim - 1]))
				--iTrim;

			if (iTrim == iEnd)
				continue;

			fp_AddEdit("trailing-whitespace", iTrim, iEnd - iTrim, {}, "trailing whitespace");
		}
	}

	void CFormattingAnalyzer::fp_RuleLineEndings()
	{
		// A line-ending change rewrites bytes on every line, so it needs a whole-file request.
		if (!m_bWholeFile || !m_Request.m_Settings.m_EndOfLine)
			return;

		auto Ending = *m_Request.m_Settings.m_EndOfLine;
		auto Bytes = fg_GetTextLineEndingBytes(Ending);
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			auto const &Line = m_Lines.f_GetLine(iLine);
			if (Line.m_Ending == ETextLineEnding::mc_None || Line.m_Ending == Ending)
				continue;

			if (m_bProtectedEnd[iLine])
				continue;

			fp_AddEdit
				(
					"line-ending", m_Lines.f_GetLineContentEnd(iLine), m_Lines.f_GetTerminatorLength(iLine), Bytes
					, "line ending must match end_of_line"
				)
			;
		}
	}

	void CFormattingAnalyzer::fp_RuleFinalNewline()
	{
		if (!m_bWholeFile || !m_Request.m_Settings.m_bInsertFinalNewline || m_Request.m_Source.f_IsEmpty())
			return;

		auto iLast = m_Lines.f_GetLineCount() - 1;
		if (!m_Lines.f_GetLine(iLast).m_nLength && m_Lines.f_GetLine(iLast).m_Ending == ETextLineEnding::mc_None)
			return;

		fp_AddEdit
			(
				"final-newline", m_Request.m_Source.f_GetLen(), 0, fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding())
				, "file must end with a newline"
			)
		;
	}

	void CFormattingAnalyzer::fp_EnsureSingleSpace(umint _iToken, bool _bBefore, CStr const &_Rule, CStr const &_Explanation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Token = Tokens[_iToken];
		if (_bBefore)
		{
			if (fp_IsFirstOnLine(_iToken))
				return;

			auto const &Previous = Tokens[_iToken - 1];
			if (Previous.m_Kind == ECodeTokenKind::mc_Whitespace)
			{
				if (Previous.m_nLength != 1 || m_Request.m_Source.f_GetStr()[Previous.m_iOffset] != ' ')
					fp_AddEdit(_Rule, Previous.m_iOffset, Previous.m_nLength, " ", _Explanation);

				return;
			}

			fp_AddEdit(_Rule, Token.m_iOffset, 0, " ", _Explanation);

			return;
		}

		if (fp_IsLastOnLine(_iToken))
			return;

		auto const &Next = Tokens[_iToken + 1];
		if (Next.m_Kind == ECodeTokenKind::mc_Whitespace)
		{
			if (Next.m_nLength != 1 || m_Request.m_Source.f_GetStr()[Next.m_iOffset] != ' ')
				fp_AddEdit(_Rule, Next.m_iOffset, Next.m_nLength, " ", _Explanation);

			return;
		}

		fp_AddEdit(_Rule, Token.f_GetEnd(), 0, " ", _Explanation);
	}

	void CFormattingAnalyzer::fp_RemoveSpaceBefore(umint _iToken, CStr const &_Rule, CStr const &_Explanation)
	{
		if (fp_IsFirstOnLine(_iToken))
			return;

		auto const &Previous = m_Tokens.f_GetTokens()[_iToken - 1];
		if (Previous.m_Kind != ECodeTokenKind::mc_Whitespace)
			return;

		fp_AddEdit(_Rule, Previous.m_iOffset, Previous.m_nLength, {}, _Explanation);
	}

	void CFormattingAnalyzer::fp_RuleTokenSpacing()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		// Plain assignment is deliberately absent: a lone '=' is also a lambda capture
		// default and the trailing token of Malterlib's '_o=' and '_j=' DSL spellings.
		static ch8 const *const gsc_pSpacedOperators[] =
			{
				"==", "!=", "<=", ">=", "<=>", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
			}
		;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				bool bClause = m_Tokens.f_IsText(Token, "if") || m_Tokens.f_IsText(Token, "for") || m_Tokens.f_IsText(Token, "while")
					|| m_Tokens.f_IsText(Token, "switch") || m_Tokens.f_IsText(Token, "catch")
				;
				if (!bClause)
					continue;

				auto iNext = fp_NextSignificant(i);
				if (iNext < 0 || !m_Tokens.f_IsText(Tokens[umint(iNext)], "("))
					continue;

				fp_EnsureSingleSpace(i, false, "clause-space", "a clause keyword and its parenthesis are separated by one space");

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				continue;

			if (m_Tokens.f_IsText(Token, ","))
			{
				fp_RemoveSpaceBefore(i, "comma-space", "the comma operator has no space before it");
				auto iNext = fp_NextSignificant(i);
				if (iNext >= 0 && !Tokens[umint(iNext)].m_bMultiLine && Tokens[umint(iNext)].m_Kind != ECodeTokenKind::mc_Newline)
				{
					bool bClosing = m_Tokens.f_IsText(Tokens[umint(iNext)], ")") || m_Tokens.f_IsText(Tokens[umint(iNext)], "]")
						|| m_Tokens.f_IsText(Tokens[umint(iNext)], "}")
					;
					if (!bClosing)
						fp_EnsureSingleSpace(i, false, "comma-space", "the comma operator has one space after it");
				}

				continue;
			}

			bool bSpaced = false;
			for (auto pOperator : gsc_pSpacedOperators)
				bSpaced |= m_Tokens.f_IsText(Token, pOperator);

			if (!bSpaced)
				continue;

			// An operator name is part of a declarator, not an expression operator.
			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious >= 0 && m_Tokens.f_IsText(Tokens[umint(iPrevious)], "operator"))
				continue;

			// A macro argument such as DMibExpect(Value, ==, 2) passes the operator as a
			// bare token. Without both operands it is not in an infix position.
			if (iPrevious < 0 || !fp_HasOperand(umint(iPrevious), true) || !fp_HasOperand(i, false))
				continue;

			fp_EnsureSingleSpace(i, true, "operator-space", "binary operators have one space before them");
			fp_EnsureSingleSpace(i, false, "operator-space", "binary operators have one space after them");
		}
	}

	void CFormattingAnalyzer::fp_RemoveFollowingBlankLines(umint _iToken, CStr const &_Rule, CStr const &_Explanation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		umint i = _iToken + 1;
		while (i < Tokens.f_GetLen() && Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace)
			++i;

		if (i >= Tokens.f_GetLen() || Tokens[i].m_Kind != ECodeTokenKind::mc_Newline)
			return;

		auto iBlankStart = Tokens[i].f_GetEnd();
		auto iBlankEnd = iBlankStart;
		++i;
		while (i < Tokens.f_GetLen())
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace)
			{
				++i;

				continue;
			}

			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Newline)
				break;

			iBlankEnd = Tokens[i].f_GetEnd();
			++i;
		}

		if (iBlankEnd == iBlankStart)
			return;

		fp_AddEdit(_Rule, iBlankStart, iBlankEnd - iBlankStart, {}, _Explanation);
	}

	void CFormattingAnalyzer::fp_RuleBlankLines()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind == ECodeTokenKind::mc_Punctuator && m_Tokens.f_IsText(Token, "{"))
			{
				fp_RemoveFollowingBlankLines(i, "block-blank-line", "no blank line follows an opening brace");

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_Identifier)
				continue;

			bool bCase = m_Tokens.f_IsText(Token, "case");
			if (!bCase && !m_Tokens.f_IsText(Token, "default"))
				continue;

			// A label follows the end of the previous statement or the start of the switch body.
			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious < 0)
				continue;

			auto const &Previous = Tokens[umint(iPrevious)];
			bool bLabelContext = m_Tokens.f_IsText(Previous, "{") || m_Tokens.f_IsText(Previous, "}")
				|| m_Tokens.f_IsText(Previous, ";") || m_Tokens.f_IsText(Previous, ":")
			;
			if (!bLabelContext)
				continue;

			// Scan to the label's colon. A conditional operator makes the colon ambiguous.
			umint iColon = i + 1;
			umint nDepth = 0;
			bool bFound = false;
			while (iColon < Tokens.f_GetLen())
			{
				auto const &Candidate = Tokens[iColon];
				if (Candidate.m_Kind == ECodeTokenKind::mc_Punctuator)
				{
					if (m_Tokens.f_IsText(Candidate, "(") || m_Tokens.f_IsText(Candidate, "[") || m_Tokens.f_IsText(Candidate, "{"))
						++nDepth;
					else if (m_Tokens.f_IsText(Candidate, ")") || m_Tokens.f_IsText(Candidate, "]") || m_Tokens.f_IsText(Candidate, "}"))
					{
						if (!nDepth)
							break;

						--nDepth;
					}
					else if (!nDepth && (m_Tokens.f_IsText(Candidate, "?") || m_Tokens.f_IsText(Candidate, ";")))
						break;
					else if (!nDepth && m_Tokens.f_IsText(Candidate, ":"))
					{
						bFound = true;

						break;
					}
				}

				++iColon;
			}

			if (bFound)
				fp_RemoveFollowingBlankLines(iColon, "case-blank-line", "no blank line follows a case label");
		}
	}

	void CFormattingAnalyzer::fp_DiagnoseLineLength()
	{
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		if (!nMaxColumns)
			return;

		auto const &Source = m_Request.m_Source;
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			if (!fp_IsLineSelected(iLine))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto nLength = m_Lines.f_GetLine(iLine).m_nLength;
			// The file-leading byte-order mark occupies no column.
			if (!iLine)
			{
				auto nBom = fg_GetTextBomLength(Source);
				iStart += nBom;
				nLength -= fg_Min(nBom, nLength);
			}

			umint nColumns = 0;
			if (!fg_MeasureTextColumns(Source.f_GetStr() + iStart, nLength, m_Request.m_Settings.m_nTabWidth, nColumns))
			{
				fp_AddDiagnostic
					(
						"line-length", m_Lines.f_GetLineStart(iLine), m_Lines.f_GetLine(iLine).m_nLength
						, "line length overflows the column counter and exceeds max_line_length = {}"_f << nMaxColumns
						, false
					)
				;

				continue;
			}

			if (nColumns <= nMaxColumns)
				continue;

			fp_AddDiagnostic
				(
					"line-length", m_Lines.f_GetLineStart(iLine), m_Lines.f_GetLine(iLine).m_nLength
					, "line length {} exceeds max_line_length = {}"_f << nColumns << nMaxColumns
					, false
				)
			;
		}
	}
}

namespace
{
	CCodeFormattingResult CFormattingAnalyzer::f_Analyze(bool _bVerify)
	{
		CCodeFormattingResult Result;
		auto const &Settings = m_Request.m_Settings;
		auto fUnsupported = [&](CStr const &_Explanation)
			{
				Result.m_Status = ECodeFormattingStatus::mc_Unsupported;
				Result.m_Explanation = _Explanation;

				return Result;
			}
		;
		auto fFailed = [&](CStr const &_Explanation)
			{
				Result.m_Status = ECodeFormattingStatus::mc_Failed;
				Result.m_Explanation = _Explanation;

				return Result;
			}
		;

		if (!Settings.f_IsFormattingEnabled())
			return fUnsupported("Malterlib formatting is not enabled for this file");

		if (m_Request.m_Language != ECodeLanguage::mc_Cpp)
			return fUnsupported("Only C and C++ sources have a formatting backend");

		if (Settings.m_Charset && Settings.m_Charset != "utf-8" && Settings.m_Charset != "utf-8-bom")
			return fUnsupported("charset = {} is not a supported formatting encoding"_f << Settings.m_Charset);

		if (!fg_IsValidUtf8(m_Request.m_Source))
			return fUnsupported("The source is not valid UTF-8 and is left untouched");

		if (!m_Tokens.f_IsComplete())
			return fUnsupported("The source ends inside a comment or literal");

		for (auto const &Token : m_Tokens.f_GetTokens())
		{
			if (Token.m_Kind == ECodeTokenKind::mc_Unknown)
				return fUnsupported("The source contains a byte that cannot start a token");
		}

		fp_PrepareLineProtection();
		CStr Explanation;
		if (!fp_CollectDisabledRegions(Explanation))
			return fFailed(Explanation);

		if (!fp_ResolveRanges(Explanation))
			return fFailed(Explanation);

		fp_RuleIndentation();
		fp_RuleTrailingWhitespace();
		fp_RuleLineEndings();
		fp_RuleFinalNewline();
		fp_RuleTokenSpacing();
		fp_RuleBlankLines();

		NContainer::TCVector<umint> Order;
		for (umint i = 0; i < m_Edits.f_GetLen(); ++i)
			Order.f_Insert(i);

		Order.f_Sort
			(
				[this](umint _Left, umint _Right)
				{
					auto const &Left = m_Edits[_Left];
					auto const &Right = m_Edits[_Right];
					if (Left.m_iOffset != Right.m_iOffset)
						return Left.m_iOffset <=> Right.m_iOffset;

					return Right.m_nLength <=> Left.m_nLength;
				}
			)
		;

		// Independent rules can describe the same bytes, for example when a blank line
		// inside a removed run also carries trailing whitespace. The first plan wins.
		umint iCovered = 0;
		umint iLastInsertion = 0;
		bool bHasInsertion = false;
		for (auto iEdit : Order)
		{
			auto const &Edit = m_Edits[iEdit];
			if (Edit.m_iOffset < iCovered)
				continue;

			if (!Edit.m_nLength && bHasInsertion && Edit.m_iOffset == iLastInsertion)
				continue;

			Result.m_Edits.f_Insert(Edit);
			auto &Diagnostic = Result.m_Diagnostics.f_Insert();
			Diagnostic.m_Rule = Edit.m_Rule;
			Diagnostic.m_Severity = ECodeFormattingSeverity::mc_Warning;
			Diagnostic.m_iOffset = Edit.m_iOffset;
			Diagnostic.m_nLength = Edit.m_nLength;
			Diagnostic.m_iLine = m_Lines.f_FindLine(Edit.m_iOffset) + 1;
			Diagnostic.m_iColumn = fp_GetColumn(Edit.m_iOffset);
			Diagnostic.m_Explanation = m_EditExplanations[iEdit];
			Diagnostic.m_bHasAutomaticFix = true;
			if (!Edit.m_nLength)
			{
				iLastInsertion = Edit.m_iOffset;
				bHasInsertion = true;
			}

			iCovered = Edit.f_GetEnd();
		}

		fp_DiagnoseLineLength();
		for (auto const &Diagnostic : m_Diagnostics)
			Result.m_Diagnostics.f_Insert(Diagnostic);

		Result.m_Diagnostics.f_Sort
			(
				[](CCodeFormattingDiagnostic const &_Left, CCodeFormattingDiagnostic const &_Right)
				{
					if (_Left.m_iOffset != _Right.m_iOffset)
						return _Left.m_iOffset <=> _Right.m_iOffset;

					return _Left.m_Rule <=> _Right.m_Rule;
				}
			)
		;
		Result.m_EffectiveRanges = m_Effective;
		Result.m_Status = ECodeFormattingStatus::mc_Complete;

		if (!_bVerify)
			return Result;

		auto Formatted = fg_ApplyCodeFormattingEdits(m_Request.m_Source, Result.m_Edits);
		if (!fg_HasEquivalentCodeTokens(m_Request.m_Source, Formatted))
			return fFailed("Formatting would change the token stream; no edits were produced");

		if (m_bWholeFile)
		{
			CCodeFormattingRequest Nested = m_Request;
			Nested.m_Source = Formatted;
			CFormattingAnalyzer Analyzer(Nested);
			auto Second = Analyzer.f_Analyze(false);
			if (Second.m_Status != ECodeFormattingStatus::mc_Complete)
				return fFailed("Formatting did not reach a stable result: {}"_f << Second.m_Explanation);

			if (Second.f_HasEdits())
			{
				CTextLineMap FormattedLines(Formatted);
				auto const &Edit = Second.m_Edits[0];

				return fFailed
					(
						"Formatting did not reach a stable result: {} would still change formatted line {}"_f
							<< Edit.m_Rule << FormattedLines.f_FindLine(Edit.m_iOffset) + 1
					)
				;
			}
		}

		return Result;
	}
}

namespace NMib::NDevelop
{
	CCodeFormattingResult fg_AnalyzeCodeFormatting(CCodeFormattingRequest const &_Request)
	{
		CFormattingAnalyzer Analyzer(_Request);

		return Analyzer.f_Analyze(true);
	}
}
