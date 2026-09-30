// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormatting.h"
#include "Malterlib_Develop_CodeFormattingLexer.h"
#include "Malterlib_Develop_CodeFormattingStructure.h"

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
	using namespace NStorage;
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

	// Wider than any line: what a block measures as, so that no range holding one fits.
	constexpr umint gc_nBlockWidth = umint(1) << 24;

	bool fg_IsCodeToken(CCodeToken const &_Token)
	{
		switch (_Token.m_Kind)
		{
			case ECodeTokenKind::mc_Identifier:
			case ECodeTokenKind::mc_Number:
			case ECodeTokenKind::mc_CharLiteral:
			case ECodeTokenKind::mc_StringLiteral:
			case ECodeTokenKind::mc_Punctuator:
				return true;
			default: return false;
		}
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

	// Maps an offset in the original source to the source with the conversions applied. An
	// offset inside a converted span lands at the start of its replacement, or at the end of
	// it when the offset ends a range.
	umint fg_MapOffsetToConverted(TCVector<CCodeFormattingEdit> const &_Conversions, umint _iOffset, bool _bEnd)
	{
		aint nDelta = 0;
		for (auto const &Edit : _Conversions)
		{
			if (Edit.m_iOffset >= _iOffset && !(_bEnd && Edit.m_iOffset == _iOffset && !Edit.m_nLength))
				break;

			if (_iOffset < Edit.f_GetEnd())
				return umint(aint(Edit.m_iOffset) + nDelta) + (_bEnd ? Edit.m_Replacement.f_GetLen() : 0);

			nDelta += aint(Edit.m_Replacement.f_GetLen()) - aint(Edit.m_nLength);
		}

		return umint(aint(_iOffset) + nDelta);
	}

	// What the conversions that reorder words leave as it was: the code with the words they
	// move left out, and how many of each there are. The tokens are joined since a '>' that
	// a qualifier lands between two of is lexed as one token with its neighbour before the
	// move and as two after it.
	CStr fg_GetReorderInvariant(CStr const &_Source)
	{
		constexpr ch8 const *c_pMoved[] =
			{
				"const", "volatile", "static", "constexpr"
			}
		;
		// A run of statement terminators reads as one, since the stage takes out the ones
		// that end nothing: 'f_Call();;' reads as 'f_Call();'. A comma in front of a closing
		// brace goes the same way: 'EA, }' reads as 'EA }'.
		CCodeTokenStream Tokens(_Source);
		CStr Text;
		umint nMoved[4] = {};
		bool bTerminated = false;
		bool bComma = false;
		for (auto const &Token : Tokens.f_GetTokens())
		{
			if (!fg_IsCodeToken(Token))
				continue;

			if (bComma && !Tokens.f_IsText(Token, "}"))
				Text += ",";

			bComma = Tokens.f_IsText(Token, ",");
			if (bComma)
			{
				bTerminated = false;

				continue;
			}

			bool bMoved = false;
			for (umint i = 0; i < 4 && !bMoved; ++i)
			{
				bMoved = Tokens.f_IsText(Token, c_pMoved[i]);
				nMoved[i] += bMoved;
			}

			if (bMoved)
				continue;

			bool bTerminator = Tokens.f_IsText(Token, ";");
			if (!bTerminator || !bTerminated)
				Text += Tokens.f_GetText(Token);

			bTerminated = bTerminator;
		}

		if (bComma)
			Text += ",";

		for (auto nWords : nMoved)
			Text += " {}"_f << nWords;

		return Text;
	}

	// Maps an offset in the converted source back to the original. An offset inside a
	// replacement lands where the converted span started.
	umint fg_MapOffsetToOriginal(TCVector<CCodeFormattingEdit> const &_Conversions, umint _iOffset)
	{
		aint nDelta = 0;
		for (auto const &Edit : _Conversions)
		{
			auto iStart = umint(aint(Edit.m_iOffset) + nDelta);
			if (_iOffset < iStart)
				break;

			if (_iOffset < iStart + Edit.m_Replacement.f_GetLen())
				return Edit.m_iOffset;

			nDelta += aint(Edit.m_Replacement.f_GetLen()) - aint(Edit.m_nLength);
		}

		return umint(aint(_iOffset) - nDelta);
	}

	// Expresses edits made on the converted source as edits on the original. An edit that
	// touches no conversion is shifted back to where it stood. One that reaches into the
	// text a conversion wrote is merged with that conversion, and with every other one it
	// reaches, into a single edit over everything they cover in the original, whose text
	// is what the converted source has there with the edits made.
	bool fg_ComposeEdits
		(
			TCVector<CCodeFormattingEdit> const &_Conversions
			, CStr const &_Converted
			, TCVector<CCodeFormattingEdit> const &_Edits
			, TCVector<CCodeFormattingEdit> &o_Edits
			, CStr &o_Explanation
		)
	{
		// Where each conversion's text stands in the converted source.
		TCVector<umint> Starts;
		aint nShift = 0;
		for (auto const &Conversion : _Conversions)
		{
			Starts.f_Insert(umint(aint(Conversion.m_iOffset) + nShift));
			nShift += aint(Conversion.m_Replacement.f_GetLen()) - aint(Conversion.m_nLength);
		}

		auto fTouches = [](umint _iStart, umint _iEnd, umint _iOtherStart, umint _iOtherEnd)
			{
				// An empty range is touched by whatever stands at it; two that hold text
				// have to share some of it.
				if (_iStart == _iEnd || _iOtherStart == _iOtherEnd)
					return _iStart <= _iOtherEnd && _iOtherStart <= _iEnd;

				return _iStart < _iOtherEnd && _iOtherStart < _iEnd;
			}
		;

		umint iConversion = 0;
		umint iEdit = 0;
		aint nDelta = 0;
		while (iConversion < _Conversions.f_GetLen() || iEdit < _Edits.f_GetLen())
		{
			// What stands first opens a cluster, which then takes in whatever touches it.
			bool bConversionFirst = iConversion < _Conversions.f_GetLen() && (iEdit >= _Edits.f_GetLen() || Starts[iConversion] <= _Edits[iEdit].m_iOffset);
			umint iStart = bConversionFirst ? Starts[iConversion] : _Edits[iEdit].m_iOffset;
			umint iEnd = bConversionFirst ? iStart : _Edits[iEdit].f_GetEnd();
			umint iSourceStart = bConversionFirst ? _Conversions[iConversion].m_iOffset : umint(aint(iStart) - nDelta);
			umint iFirstEdit = iEdit;
			umint nConversions = 0;
			umint iConvertedEnd = 0;
			umint iConvertedSourceEnd = 0;
			CStr Rule;
			if (!bConversionFirst)
				++iEdit;

			while (true)
			{
				if (iConversion < _Conversions.f_GetLen())
				{
					auto const &Conversion = _Conversions[iConversion];
					auto iConversionEnd = Starts[iConversion] + Conversion.m_Replacement.f_GetLen();
					if ((!nConversions && bConversionFirst) || fTouches(Starts[iConversion], iConversionEnd, iStart, iEnd))
					{
						iEnd = fg_Max(iEnd, iConversionEnd);
						iConvertedEnd = iConversionEnd;
						iConvertedSourceEnd = Conversion.f_GetEnd();
						nDelta += aint(Conversion.m_Replacement.f_GetLen()) - aint(Conversion.m_nLength);
						if (!Rule)
							Rule = Conversion.m_Rule;

						++iConversion;
						++nConversions;

						continue;
					}
				}

				if (iEdit >= _Edits.f_GetLen() || !fTouches(_Edits[iEdit].m_iOffset, _Edits[iEdit].f_GetEnd(), iStart, iEnd))
					break;

				iEnd = fg_Max(iEnd, _Edits[iEdit].f_GetEnd());
				++iEdit;
			}

			// An end inside what the last conversion wrote is that conversion's end in the
			// original; one behind it stands in text the original has too.
			umint iSourceEnd = nConversions && iEnd <= iConvertedEnd ? iConvertedSourceEnd : umint(aint(iEnd) - nDelta);

			if (!nConversions)
			{
				auto Edit = _Edits[iFirstEdit];
				Edit.m_iOffset = iSourceStart;
				o_Edits.f_Insert(Edit);

				continue;
			}

			if (iEnd > _Converted.f_GetLen() || iSourceEnd < iSourceStart)
			{
				o_Explanation = "{} could not be expressed as an edit of the original source"_f << Rule;

				return false;
			}

			CStr Text;
			umint iCopied = iStart;
			for (umint i = iFirstEdit; i < iEdit; ++i)
			{
				Text += CStr(_Converted.f_GetStr() + iCopied, _Edits[i].m_iOffset - iCopied);
				Text += _Edits[i].m_Replacement;
				iCopied = _Edits[i].f_GetEnd();
			}

			Text += CStr(_Converted.f_GetStr() + iCopied, iEnd - iCopied);
			auto &Merged = o_Edits.f_Insert();
			Merged.m_iOffset = iSourceStart;
			Merged.m_nLength = iSourceEnd - iSourceStart;
			Merged.m_Replacement = Text;
			Merged.m_Rule = Rule;
		}

		return true;
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
			else if (Value == "malterlib-buildsystem")
				m_Profile = ECodeFormattingProfile::mc_MalterlibBuildSystem;
			else if (Value != "off")
				DMibError("Invalid malterlib_format: '{}' (expected malterlib, malterlib-buildsystem, off, or unset)"_f << *pValue);
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

	// The profile the .editorconfig opts a file in with says what the file is written in; its name says nothing.
	ECodeLanguage CCodeFormattingSettings::f_GetLanguage() const
	{
		switch (m_Profile)
		{
		case ECodeFormattingProfile::mc_Disabled: return ECodeLanguage::mc_Unknown;
		case ECodeFormattingProfile::mc_Malterlib: return ECodeLanguage::mc_Cpp;
		case ECodeFormattingProfile::mc_MalterlibBuildSystem: return ECodeLanguage::mc_BuildSystem;
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

	// Normalizes a source into comparable token spellings. Closing a nested template
	// argument list regroups '>' '>' into the single token '>>', the same ambiguity the
	// language resolves by context, so both spellings normalize alike.
	static TCVector<CStr> fg_NormalizeCodeTokens(CStr const &_Source, ECodeLanguage _Language)
	{
		CCodeTokenStream Stream(_Source, nullptr, _Language);
		bool bCpp = _Language != ECodeLanguage::mc_BuildSystem;
		TCVector<CStr> Texts;
		for (auto const &Token : Stream.f_GetTokens())
		{
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || (bCpp && Kind == ECodeTokenKind::mc_LineSplice))
				continue;

			auto Text = Stream.f_GetText(Token);
			// Trailing space inside a line comment or behind a directive is layout, not text.
			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_Preprocessor)
				Text = fg_TrimCommentTrailingSpace(Text);

			auto fInsert = [&](CStr const &_Text)
				{
					Texts.f_Insert("{}:{}"_f << umint(Kind) << _Text);
				}
			;
			if (bCpp && Kind == ECodeTokenKind::mc_Punctuator && (Text == ">>" || Text == ">>="))
			{
				fInsert(">");
				fInsert(Text == ">>" ? ">" : ">=");

				continue;
			}

			fInsert(Text);
		}

		return Texts;
	}

	CStr fg_DescribeCodeTokenDifference(CStr const &_First, CStr const &_Second, ECodeLanguage _Language)
	{
		auto First = fg_NormalizeCodeTokens(_First, _Language);
		auto Second = fg_NormalizeCodeTokens(_Second, _Language);
		for (umint i = 0; i < fg_Min(First.f_GetLen(), Second.f_GetLen()); ++i)
		{
			if (First[i] == Second[i])
				continue;

			return "token {} became '{}' instead of '{}'"_f << i << Second[i] << First[i];
		}

		if (First.f_GetLen() == Second.f_GetLen())
			return {};

		return "the token count changed from {} to {}"_f << First.f_GetLen() << Second.f_GetLen();
	}

	// A token's comparable text, as fg_NormalizeCodeTokens spells it, without building a
	// string for it: a closer of two nested template argument lists counts as two, and the
	// trailing space of a line comment or a directive as none.
	struct CCodeTokenView
	{
		ECodeTokenKind m_Kind = ECodeTokenKind::mc_Unknown;
		ch8 const *m_pText = nullptr;
		umint m_nLength = 0;
	};

	// A splice is layout in C++, but in the build system's syntax it is what continues a value.
	static void fg_CollectCodeTokenViews(CCodeTokenStream const &_Stream, ECodeLanguage _Language, TCVector<CCodeTokenView> &o_Views)
	{
		bool bCpp = _Language != ECodeLanguage::mc_BuildSystem;
		for (auto const &Token : _Stream.f_GetTokens())
		{
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || (bCpp && Kind == ECodeTokenKind::mc_LineSplice))
				continue;

			auto pText = _Stream.f_GetTextPointer(Token);
			auto nLength = Token.m_nLength;
			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_Preprocessor)
			{
				while (nLength && (pText[nLength - 1] == ' ' || pText[nLength - 1] == '\t'))
					--nLength;
			}

			if
			(
				bCpp
				&& Kind == ECodeTokenKind::mc_Punctuator
				&& nLength >= 2
				&& pText[0] == '>'
				&& pText[1] == '>'
				&& (nLength == 2 || (nLength == 3 && pText[2] == '='))
			)
			{
				o_Views.f_Insert(CCodeTokenView{Kind, pText, 1});
				o_Views.f_Insert(CCodeTokenView{Kind, pText + 1, nLength - 1});

				continue;
			}

			o_Views.f_Insert(CCodeTokenView{Kind, pText, nLength});
		}
	}

	bool fg_HasEquivalentCodeTokens(CStr const &_First, CStr const &_Second, ECodeLanguage _Language)
	{
		CCodeTokenStream FirstStream(_First, nullptr, _Language);
		CCodeTokenStream SecondStream(_Second, nullptr, _Language);
		TCVector<CCodeTokenView> First;
		TCVector<CCodeTokenView> Second;
		fg_CollectCodeTokenViews(FirstStream, _Language, First);
		fg_CollectCodeTokenViews(SecondStream, _Language, Second);
		if (First.f_GetLen() != Second.f_GetLen())
			return false;

		for (umint i = 0; i < First.f_GetLen(); ++i)
		{
			auto const &Left = First[i];
			auto const &Right = Second[i];
			if (Left.m_Kind != Right.m_Kind || Left.m_nLength != Right.m_nLength)
				return false;

			for (umint j = 0; j < Left.m_nLength; ++j)
			{
				if (Left.m_pText[j] != Right.m_pText[j])
					return false;
			}
		}

		return true;
	}
}

namespace
{
	// Lays a file out in two phases. Every statement is first taken as if it were written on
	// one line, and then split, outermost break first, only where that line is too long. The
	// decisions are kept per gap between tokens and written out once, so no decision can
	// depend on an edit already made, or on where the source happened to break its lines.
	// The build system's syntax has no C++ structure; its rules read brackets and lines alone.
	CCodeStructure fg_BuildStructure(CCodeTokenStream &_Tokens, ECodeLanguage _Language)
	{
		if (_Language == ECodeLanguage::mc_BuildSystem)
			return {};

		return CCodeStructure(_Tokens);
	}

	struct CFormattingAnalyzer
	{
		explicit CFormattingAnalyzer(CCodeFormattingRequest const &_Request, bool _bAllowConversions = true, bool _bAllowQualifiers = true)
			: m_Request(_Request)
			, m_Tokens(_Request.m_Source, _Request.m_pNaming.f_Get(), _Request.m_Settings.f_GetLanguage())
			, m_Structure(fg_BuildStructure(m_Tokens, _Request.m_Settings.f_GetLanguage()))
			, m_Lines(_Request.m_Source)
			, m_bAllowConversions(_bAllowConversions)
			, m_bAllowQualifiers(_bAllowQualifiers)
		{
		}

		CCodeFormattingResult f_Analyze(bool _bVerify);

	private:
		// What a gap in front of a token becomes: what the source has, the inline spelling,
		// or a line break at a given indentation.
		enum class EGap : uint8
		{
			mc_Keep
			, mc_Inline
			, mc_Break				// A split construct starts a line here.
			, mc_OwnLine			// A block's brace or a statement takes a line of its own here.
			, mc_Indent				// The line keeps its break and takes the indentation its body moved to.
		};

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
		aint fp_LeadingElse(umint _iToken) const;
		bool fp_IsLastOnLine(umint _iToken) const;

		void fp_RuleIndentation();
		void fp_RuleTrailingWhitespace();
		void fp_RuleLineEndings();
		void fp_RuleFinalNewline();
		void fp_RuleTokenSpacing();
		bool fp_HasOperand(umint _iToken, bool _bBefore) const;
		bool fp_EndsOperand(aint _iToken) const;
		aint fp_ContinuesLiteral(umint _iToken) const;
		bool fp_ContinuesValue(umint _iContinued) const;
		void fp_RuleBlankLines();
		void fp_RuleLineBreaks();
		void fp_LayoutNode(umint _iNode, umint _iIndent);
		void fp_LayoutStatement(umint _iNode, umint _iIndent);
		void fp_ProbeBodies(umint _iNode, umint _iIndent);
		void fp_LayoutInitializerList(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent);
		umint fp_FindInitializerList(umint _iNode, umint _iFirstParen, umint _iLast) const;
		bool fp_LayoutGroup(umint _iNode, umint _iIndent, bool _bBreakBefore = true);
		void fp_LayoutElements(umint _iNode, umint _iIndent);
		bool fp_LayoutRange(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndentContinuations, bool _bMustSplit = false);
		bool fp_BreakAtAssign(umint _iFirst, umint _iOperator, umint _iIndent, umint _nContinuation);
		bool fp_IsLambdaIntroducer(umint _iToken) const;
		bool fp_FollowsScope(umint _iToken) const;
		bool fp_IsFunctionQualifier(umint _iToken) const;
		bool fp_IsTrailingReturnArrow(umint _iToken) const;
		bool fp_ClosesLambdaIntroducer(umint _iToken) const;
		umint fp_SkipTemplateHeader(umint _iToken) const;
		bool fp_IsCastGroup(umint _iNode) const;
		bool fp_IsNamedCast(umint _iOpen) const;
		bool fp_LayoutScopes(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndent, bool _bMustSplit = false);
		bool fp_IsYieldedScope(umint _iOpen) const;
		ECodeSpacing fp_GetInlineSpacing(umint _iLeft, umint _iRight) const;
		ECodeSpacing fp_DecideInlineSpacing(umint _iLeft, umint _iRight) const;
		ECodeSpacing fp_GetCanonicalSpacing(umint _iLeft, umint _iRight) const;
		bool fp_IsInFunctionBody(umint _iStatement) const;
		bool fp_LayoutMembers(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation);
		bool fp_BreakAtQualification(umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation);
		bool fp_LayoutHead(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation);
		void fp_FindLooseOperators(umint _iFirst, umint _iLast, NContainer::TCVector<umint> &o_Operators) const;
		void fp_PrepareTokenDepth();
		bool fp_ConvertTrailingReturn(umint _iNode, umint _iDeclFirst, umint _iIndent);
		void fp_ConvertQualifiers();
		void fp_ConvertSpecifiers();
		void fp_ConvertEmptyStatements();
		void fp_ConvertEnumCommas();
		void fp_ConvertEnumBodyCommas(CCodeNode const &_Body);
		bool fp_DropBraces(umint _iStatement, umint _iGuard);
		bool fp_AddBraces(umint _iStatement, umint _iGuard);
		bool fp_IsBraceGuard(umint _iGuard, bool &o_bClauseFits) const;
		bool fp_IsRangeOneLine(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent) const;
		void fp_BreakBefore(umint _iToken, umint _iIndent);
		void fp_BreakAfter(umint _iToken, umint _iIndent);
		void fp_OwnLineBefore(umint _iToken, umint _iIndent);
		void fp_IndentBefore(umint _iToken, umint _iIndent);
		bool fp_IsBreakGap(umint _iToken) const;
		bool fp_IsGuard(umint _iNode) const;
		void fp_LayoutBlockLines(umint _iNode, umint _iIndent);
		bool fp_KeepsOwnLines(umint _iNode) const;
		bool fp_IsRequirementsBody(umint _iNode, umint _iBrace) const;
		void fp_PlaceBody(umint _iNode, umint _iBlock, umint _iIndent, bool _bDeclarator);
		bool fp_PlaceBlock(umint _iBlock, umint _iIndent, umint _nReference);
		bool fp_CanPlaceBlock(umint _iBlock) const;
		void fp_ShiftBlock(umint _iBlock, aint _nDelta);
		void fp_PrepareBlockEnds();
		void fp_PrepareDirectives();
		NStr::CStr fp_GetDirectiveKeyword(umint _iToken) const;
		bool fp_IsDirectiveParallel(umint _iDirective, umint _iOpen, umint _iClose) const;
		bool fp_HasOpaqueDirective(umint _iFirst, umint _iLast) const;
		bool fp_IsInConditionalWithin(umint _iToken, umint _iScopeFirst) const;
		bool fp_SpansLines(umint _iFirst, umint _iLast) const;
		umint fp_GetSourceLineIndent(umint _iToken) const;
		void fp_MarkInline(umint _iFirst, umint _iLast);
		void fp_EmitLayout();
		bool fp_IsRangeJoinable(umint _iNode, umint _iFirst, umint _iLast) const;
		bool fp_FitsInline(umint _iFirst, umint _iLast, umint _iIndent) const;
		umint fp_GetStatementIndent(umint _iToken) const;
		NStr::CStr fp_MakeIndent(umint _nColumns) const;
		void fp_LimitJoinedLines();
		void fp_BuildPlan(NContainer::TCVector<CCodeFormattingEdit> &o_Edits, NContainer::TCVector<umint> &o_Sources) const;
		void fp_JoinNode(umint _iNode);
		void fp_LayoutLambdaHead(umint _iNode, umint _iBody);
		bool fp_TryJoin(umint _iFirstToken, umint _iLastToken, umint _iStartColumn);
		bool fp_MeasureJoinedWidth(umint _iFirstToken, umint _iLastToken, umint &o_nColumns) const;
		umint fp_GetTokenColumns(CCodeToken const &_Token) const;
		void fp_DiagnoseLineLength(NContainer::TCVector<CCodeFormattingEdit> const &_Edits);
		void fp_EnsureSingleSpace(umint _iToken, bool _bBefore, ch8 const *_pRule, ch8 const *_pExplanation);
		void fp_RemoveSpaceBefore(umint _iToken, ch8 const *_pRule, ch8 const *_pExplanation);
		void fp_RemoveFollowingBlankLines(umint _iToken, ch8 const *_pRule, ch8 const *_pExplanation);
		void fp_AlignContinuations();
		bool fp_PrepareBuildSystemLines(CStr &o_Explanation);
		void fp_RuleBuildSystemIndentation();
		void fp_RuleBuildSystemBlankLines();

		CCodeFormattingRequest const &m_Request;
		CCodeTokenStream m_Tokens;
		CCodeStructure m_Structure;
		CTextLineMap m_Lines;
		bool m_bAllowConversions = true;						// Only the analysis of the original source converts return types.
		bool m_bAllowQualifiers = true;							// Qualifiers move first, in a stage of their own, so no other conversion has to know of them.
		CStr m_Baseline;										// The source with every conversion made, which the result matches token for token.
		bool m_bProbing = false;								// The statement walk only decides conversions, and lays nothing out.
		bool m_bOperatorSplit = false;							// The statement broke at operators, so a block belongs to a continuation.
		umint m_iSplitFirstParen = 0;							// A declaration is never split before its name.
		umint m_iSplitTrailingReturn = TCLimitsInt<umint>::mc_Max;
		NContainer::TCVector<umint> m_TokenDepth;				// Bracket nesting of each token, for finding a range's own level.
		NContainer::TCVector<umint> m_iBlockEnd;				// Indexed by token: the closing brace of the block the token opens, or the token count.
		NContainer::TCVector<uint8> m_bOpaqueDirective;			// Indexed by token: a directive whose branches cut a construct, so nothing is read across it.
		NContainer::TCVector<umint> m_nOpaqueBefore;			// Indexed by token: how many opaque directives stand in front of it.
		NContainer::TCVector<umint> m_iConditionalOpen;		// Indexed by token: the directive opening the innermost conditional around it, or the maximum.
		NContainer::TCVector<CCodeFormattingRange> m_Conditionals;	// Source spans of the '#if' groups, for saying which one a structure was cut by.
		NContainer::TCVector<uint8> m_GapState;					// Indexed by token: what the gap in front of it becomes.
		mutable NContainer::TCVector<uint8> m_CanonicalSpacing;	// Indexed by token: the standard's spelling of the gap in front of it, plus one, or zero when not yet asked.
		mutable NContainer::TCVector<uint8> m_InlineSpacing;		// Indexed by token: the spacing rules' spelling of the gap in front of it, plus one, or zero when not yet asked.
		mutable NContainer::TCVector<umint> m_TokenColumns;		// Indexed by token: its width in columns, plus one, or zero when not yet measured.
		NContainer::TCVector<umint> m_GapIndent;				// The indentation a break in front of the token takes.
		NContainer::TCVector<uint8> m_bCommentMoved;			// Indexed by token: a comment on a line of its own that moves with the block around it.
		NContainer::TCVector<uint8> m_bBlankBefore;			// A blank line stands in front of the token, which the layout writes where it writes the gap.
		NContainer::TCVector<umint> m_CommentIndent;			// The indentation such a comment's line takes.
		NContainer::TCVector<CCodeFormattingEdit> m_Structural;	// Token changes: return types moved behind their parameter lists, braces dropped.
		NContainer::TCVector<uint8> m_bProtectedStart;
		NContainer::TCVector<uint8> m_bProtectedEnd;
		NContainer::TCVector<CCodeFormattingRange> m_Disabled;
		NContainer::TCVector<CCodeFormattingRange> m_Effective;
		struct CJoinCandidate
		{
			umint m_iOffset = 0;								// Where the joined construct starts in the source.
			NContainer::TCVector<umint> m_Edits;
			bool m_bDropped = false;
		};

		NContainer::TCVector<CCodeFormattingEdit> m_Edits;
		NContainer::TCVector<NStr::CStr> m_EditExplanations;
		NContainer::TCVector<uint8> m_bEditDropped;
		NContainer::TCVector<CJoinCandidate> m_JoinCandidates;
		NContainer::TCVector<CCodeFormattingDiagnostic> m_Diagnostics;
		// Per line of a build system source: where its first and last significant tokens are,
		// the tab levels its brackets settle, and the column a continued value aligns to.
		struct CBuildSystemLine
		{
			aint m_iFirst = -1;									// Token index; -1 for a blank line.
			aint m_iLast = -1;
			umint m_Level = 0;
			umint m_nContinuationColumns = 0;					// Set on a line a backslash continues.
			bool m_bContinued = false;
		};
		NContainer::TCVector<CBuildSystemLine> m_BuildSystemLines;
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
					<< gc_FormatOnDirective
					<< m_Lines.f_FindLine(Token.m_iOffset) + 1
					<< gc_FormatOffDirective
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
				<< gc_FormatOffDirective
				<< m_Lines.f_FindLine(iDirective) + 1
				<< gc_FormatOnDirective
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
					return _iOffset && _iOffset < nSource && m_Request.m_Source.f_GetStr()[_iOffset - 1] == '\r' && m_Request.m_Source.f_GetStr()[_iOffset] == '\n';
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
		auto pSource = m_Request.m_Source.f_GetStr();
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		// Nearly every question is about the token that starts its line, whose column is
		// the line's indentation: blanks alone, counted without decoding the text, and
		// remembered per line since the layout asks for every statement's more than once.
		umint nIndent = 0;
		auto i = iStart;
		for (; i < _iOffset; ++i)
		{
			if (pSource[i] == ' ')
				++nIndent;
			else if (pSource[i] == '\t')
				nIndent = nTab ? (nIndent / nTab + 1) * nTab : nIndent;
			else
				break;
		}

		if (i == _iOffset)
			return nIndent + 1;

		umint nColumns = 0;
		if (!fg_MeasureTextColumns(pSource + iStart, _iOffset - iStart, nTab, nColumns))
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
				fp_AddDiagnostic("range-boundary", _iOffset, _nLength, "{} requires an edit outside the requested range; no edit was applied to this unit"_f << _Rule, false);
			}

			return;
		}

		auto &Edit = m_Edits.f_Insert();
		Edit.m_iOffset = _iOffset;
		Edit.m_nLength = _nLength;
		Edit.m_Replacement = _Replacement;
		Edit.m_Rule = _Rule;
		m_EditExplanations.f_Insert(_Explanation);
		m_bEditDropped.f_Insert(uint8(0));
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
		if (fp_IsBreakGap(_iToken))
			return true;

		auto iPrevious = fp_PreviousSignificant(_iToken);

		return iPrevious < 0 || m_Tokens.f_GetTokens()[umint(iPrevious)].m_bMultiLine;
	}

	// The 'else' that starts the line an 'else if' is written on, or -1. The 'if' has no
	// line of its own, so its statement is laid out against the one the 'else' starts.
	aint CFormattingAnalyzer::fp_LeadingElse(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		if (!m_Tokens.f_IsText(Tokens[_iToken], "if") || fp_IsFirstOnLine(_iToken))
			return -1;

		auto iElse = fp_PreviousCode(_iToken);
		if (iElse < 0 || !m_Tokens.f_IsText(Tokens[umint(iElse)], "else") || !fp_IsFirstOnLine(umint(iElse)))
			return -1;

		return iElse;
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

			return !m_Tokens.f_IsText(Next, ")") && !m_Tokens.f_IsText(Next, "]") && !m_Tokens.f_IsText(Next, "}") && !m_Tokens.f_IsText(Next, ",") && !m_Tokens.f_IsText(Next, ";");
		}

		auto const &Previous = m_Tokens.f_GetTokens()[_iToken];

		return !m_Tokens.f_IsText(Previous, "(")
			&& !m_Tokens.f_IsText(Previous, "[")
			&& !m_Tokens.f_IsText(Previous, "{")
			&& !m_Tokens.f_IsText(Previous, ",")
			&& !m_Tokens.f_IsText(Previous, ";")
		;
	}

	// The string literal in front of the token when the token continues it on a line of its
	// own, which the source asks for and the layout keeps; -1 otherwise.
	aint CFormattingAnalyzer::fp_ContinuesLiteral(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		if (Tokens[_iToken].m_Kind != ECodeTokenKind::mc_StringLiteral || Tokens[_iToken].m_bMultiLine || !fp_IsFirstOnLine(_iToken))
			return -1;

		auto iPrevious = fp_PreviousSignificant(_iToken);
		while (iPrevious >= 0 && Tokens[umint(iPrevious)].m_Kind == ECodeTokenKind::mc_Newline)
			iPrevious = fp_PreviousSignificant(umint(iPrevious));

		bool bContinues = iPrevious >= 0
			&& Tokens[umint(iPrevious)].m_Kind == ECodeTokenKind::mc_StringLiteral
			&& !Tokens[umint(iPrevious)].m_bMultiLine
			&& m_TokenDepth[umint(iPrevious)] == m_TokenDepth[_iToken]
		;

		return bContinues ? iPrevious : aint(-1);
	}

	// Whether the string the token continues is a value given behind an '=', a DSL key's
	// included, or a macro's name on its line: '"Key"_o= "a"' over '"b"', 'DPrefix " a"'
	// over '"b"'. Such a continuation stands one level in from that line.
	bool CFormattingAnalyzer::fp_ContinuesValue(umint _iContinued) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto iPrevious = fp_ContinuesLiteral(_iContinued);
		if (iPrevious < 0)
			return false;

		auto iRun = umint(iPrevious);
		while (true)
		{
			// Back over the literals the run holds on its line, then to the line it continues.
			auto iBefore = fp_PreviousCode(iRun);
			if (iBefore >= 0 && Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_StringLiteral && !fp_SpansLines(umint(iBefore), iRun))
			{
				iRun = umint(iBefore);

				continue;
			}

			auto iEarlier = fp_ContinuesLiteral(iRun);
			if (iEarlier < 0)
				break;

			iRun = umint(iEarlier);
		}

		auto iLead = fp_PreviousCode(iRun);
		if (iLead < 0 || fp_SpansLines(umint(iLead), iRun))
			return false;

		auto const &Lead = Tokens[umint(iLead)];

		return m_Tokens.f_IsText(Lead, "=") || Lead.m_Kind == ECodeTokenKind::mc_Identifier;
	}

	bool CFormattingAnalyzer::fp_EndsOperand(aint _iToken) const
	{
		if (_iToken < 0)
			return false;

		auto const &Token = m_Tokens.f_GetTokens()[umint(_iToken)];
		switch (Token.m_Kind)
		{
		case ECodeTokenKind::mc_Number:
		case ECodeTokenKind::mc_StringLiteral:
		case ECodeTokenKind::mc_CharLiteral:
			return true;
		case ECodeTokenKind::mc_Identifier:
			{
				constexpr ch8 const *c_pLeadsOperand[] =
					{
						"return", "co_return", "co_await", "co_yield", "throw", "case", "new", "delete", "else", "do", "sizeof", "alignof"
					}
				;
				for (auto pKeyword : c_pLeadsOperand)
				{
					if (m_Tokens.f_IsText(Token, pKeyword))
						return false;
				}

				return true;
			}
		case ECodeTokenKind::mc_Punctuator:
			return m_Tokens.f_IsText(Token, ")")
				|| m_Tokens.f_IsText(Token, "]")
				|| m_Tokens.f_IsText(Token, "}")
				|| m_Tokens.f_IsText(Token, "++")
				|| m_Tokens.f_IsText(Token, "--")
				|| (m_Structure.f_IsAngleBracket(umint(_iToken)) && m_Tokens.f_IsText(Token, ">"))
			;
		default:
			return false;
		}
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

			// A line the layout placed takes its indentation from that decision, already
			// spelled the way this rule would spell it.
			auto iToken = m_Tokens.f_FindToken(iIndent);
			if (fp_IsBreakGap(iToken))
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

			fp_AddEdit("indentation", iStart, iIndent - iStart, Canonical, Settings.m_bIndentWithTabs ? "indentation must use tabs" : "indentation must use spaces");
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

			fp_AddEdit("line-ending", m_Lines.f_GetLineContentEnd(iLine), m_Lines.f_GetTerminatorLength(iLine), Bytes, "line ending must match end_of_line");
		}
	}

	void CFormattingAnalyzer::fp_RuleFinalNewline()
	{
		if (!m_bWholeFile || !m_Request.m_Settings.m_bInsertFinalNewline || m_Request.m_Source.f_IsEmpty())
			return;

		auto iLast = m_Lines.f_GetLineCount() - 1;
		if (!m_Lines.f_GetLine(iLast).m_nLength && m_Lines.f_GetLine(iLast).m_Ending == ETextLineEnding::mc_None)
			return;

		fp_AddEdit("final-newline", m_Request.m_Source.f_GetLen(), 0, fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding()), "file must end with a newline");
	}

	void CFormattingAnalyzer::fp_EnsureSingleSpace(umint _iToken, bool _bBefore, ch8 const *_pRule, ch8 const *_pExplanation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Token = Tokens[_iToken];
		// A gap the layout breaks is the layout's: a space written into it would land on
		// the offset of the line break, and the plan keeps only one edit there.
		auto iRight = _bBefore ? aint(_iToken) : fp_NextCode(_iToken);
		if (iRight >= 0 && fp_IsBreakGap(umint(iRight)))
			return;

		if (_bBefore)
		{
			if (fp_IsFirstOnLine(_iToken))
				return;

			auto const &Previous = Tokens[_iToken - 1];
			if (Previous.m_Kind == ECodeTokenKind::mc_Whitespace)
			{
				if (Previous.m_nLength != 1 || m_Request.m_Source.f_GetStr()[Previous.m_iOffset] != ' ')
					fp_AddEdit(_pRule, Previous.m_iOffset, Previous.m_nLength, " ", _pExplanation);

				return;
			}

			fp_AddEdit(_pRule, Token.m_iOffset, 0, " ", _pExplanation);

			return;
		}

		if (fp_IsLastOnLine(_iToken))
			return;

		auto const &Next = Tokens[_iToken + 1];
		if (Next.m_Kind == ECodeTokenKind::mc_Whitespace)
		{
			if (Next.m_nLength != 1 || m_Request.m_Source.f_GetStr()[Next.m_iOffset] != ' ')
				fp_AddEdit(_pRule, Next.m_iOffset, Next.m_nLength, " ", _pExplanation);

			return;
		}

		fp_AddEdit(_pRule, Token.f_GetEnd(), 0, " ", _pExplanation);
	}

	void CFormattingAnalyzer::fp_RemoveSpaceBefore(umint _iToken, ch8 const *_pRule, ch8 const *_pExplanation)
	{
		if (fp_IsFirstOnLine(_iToken) || fp_IsBreakGap(_iToken))
			return;

		auto const &Previous = m_Tokens.f_GetTokens()[_iToken - 1];
		if (Previous.m_Kind != ECodeTokenKind::mc_Whitespace)
			return;

		fp_AddEdit(_pRule, Previous.m_iOffset, Previous.m_nLength, {}, _pExplanation);
	}

	// Whether the token is one of the operators 'operator-space' writes apart from both
	// operands. Plain assignment is deliberately absent: a lone '=' is also a lambda
	// capture default and the trailing token of Malterlib's '_o=' and '_j=' DSL spellings.
	bool fg_IsSpacedOperator(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Punctuator || _Token.m_nLength < 2)
			return false;

		constexpr ch8 const *c_pSpacedOperators[] =
			{
				"==", "!=", "<=", ">=", "<=>", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
			}
		;
		for (auto pOperator : c_pSpacedOperators)
		{
			if (_Tokens.f_IsText(_Token, pOperator))
				return true;
		}

		return false;
	}

	// The standard's spelling of the gap in front of a code token, decided once: the layout
	// asks for it every time it measures a line the gap stands in.
	ECodeSpacing CFormattingAnalyzer::fp_GetCanonicalSpacing(umint _iLeft, umint _iRight) const
	{
		auto nTokens = m_Tokens.f_GetTokens().f_GetLen();
		if (m_CanonicalSpacing.f_GetLen() != nTokens)
		{
			m_CanonicalSpacing.f_SetLen(nTokens);
			for (auto &Value : m_CanonicalSpacing)
				Value = 0;
		}

		if (_iRight >= nTokens || fp_PreviousCode(_iRight) != aint(_iLeft))
			return fg_GetCanonicalSpacing(m_Tokens, m_Structure, _iLeft, _iRight);

		if (!m_CanonicalSpacing[_iRight])
			m_CanonicalSpacing[_iRight] = uint8(fg_GetCanonicalSpacing(m_Tokens, m_Structure, _iLeft, _iRight)) + 1;

		return ECodeSpacing(m_CanonicalSpacing[_iRight] - 1);
	}

	// The separator the spacing rules write between two tokens on one line, in the order
	// the rules are applied: the clause, angle, comma and operator rules first, and the
	// standard's spelling of every other pair after them. The width a line is measured at
	// has to be the width those rules leave it with, or the first pass lays out a line the
	// second measures differently.
	ECodeSpacing CFormattingAnalyzer::fp_GetInlineSpacing(umint _iLeft, umint _iRight) const
	{
		auto nTokens = m_Tokens.f_GetTokens().f_GetLen();
		if (m_InlineSpacing.f_GetLen() != nTokens)
		{
			m_InlineSpacing.f_SetLen(nTokens);
			for (auto &Value : m_InlineSpacing)
				Value = 0;
		}

		if (_iRight >= nTokens || fp_PreviousCode(_iRight) != aint(_iLeft))
			return fp_DecideInlineSpacing(_iLeft, _iRight);

		if (!m_InlineSpacing[_iRight])
			m_InlineSpacing[_iRight] = uint8(fp_DecideInlineSpacing(_iLeft, _iRight)) + 1;

		return ECodeSpacing(m_InlineSpacing[_iRight] - 1);
	}

	ECodeSpacing CFormattingAnalyzer::fp_DecideInlineSpacing(umint _iLeft, umint _iRight) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Left = Tokens[_iLeft];
		auto const &Right = Tokens[_iRight];
		bool bClause = m_Tokens.f_IsText(Left, "if")
			|| m_Tokens.f_IsText(Left, "for")
			|| m_Tokens.f_IsText(Left, "while")
			|| m_Tokens.f_IsText(Left, "switch")
			|| m_Tokens.f_IsText(Left, "catch")
		;
		if (bClause && m_Tokens.f_IsText(Right, "("))
			return ECodeSpacing::mc_Space;

		if (m_Structure.f_IsAngleBracket(_iLeft) && m_Structure.f_IsAngleBracket(_iRight) && m_Tokens.f_IsText(Left, ">") && m_Tokens.f_IsText(Right, ">"))
			return ECodeSpacing::mc_None;

		if (m_Tokens.f_IsText(Right, ","))
			return ECodeSpacing::mc_None;

		if (m_Tokens.f_IsText(Left, ","))
		{
			bool bClosing = m_Tokens.f_IsText(Right, ")") || m_Tokens.f_IsText(Right, "]") || m_Tokens.f_IsText(Right, "}");

			return bClosing || Right.m_bMultiLine ? ECodeSpacing::mc_Preserve : ECodeSpacing::mc_Space;
		}

		auto fSpacedOperator = [&](umint _iOperator)
			{
				if (!fg_IsSpacedOperator(m_Tokens, Tokens[_iOperator]))
					return false;

				auto iPrevious = fp_PreviousCode(_iOperator);
				if (iPrevious >= 0 && m_Tokens.f_IsText(Tokens[umint(iPrevious)], "operator"))
					return false;

				return iPrevious >= 0 && fp_HasOperand(umint(iPrevious), true) && fp_HasOperand(_iOperator, false);
			}
		;
		if (fSpacedOperator(_iRight) || fSpacedOperator(_iLeft))
			return ECodeSpacing::mc_Space;

		return fp_GetCanonicalSpacing(_iLeft, _iRight);
	}

	// Whether the statement stands in a function's body, a lambda's or a control clause's,
	// where a name behind a type followed by a parenthesis defines a variable rather than
	// declaring a function: 'TCUniquePointer<CFoo> pFoo(fg_Construct())'.
	bool CFormattingAnalyzer::fp_IsInFunctionBody(umint _iStatement) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto iBlock = Nodes[_iStatement].m_iParent;

		return iBlock < Nodes.f_GetLen() && fg_IsFunctionBody(m_Tokens, m_Structure, iBlock);
	}

	void CFormattingAnalyzer::fp_RuleTokenSpacing()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				bool bClause = m_Tokens.f_IsText(Token, "if")
					|| m_Tokens.f_IsText(Token, "for")
					|| m_Tokens.f_IsText(Token, "while")
					|| m_Tokens.f_IsText(Token, "switch")
					|| m_Tokens.f_IsText(Token, "catch")
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

			// Two nested template argument lists close as one '>>', unless the layout puts
			// the second closer on a line of its own.
			if (m_Structure.f_IsAngleBracket(i) && m_Tokens.f_IsText(Token, ">"))
			{
				auto iPrevious = fp_PreviousCode(i);
				bool bBreak = fp_IsBreakGap(i);
				if (!bBreak && iPrevious >= 0 && m_Structure.f_IsAngleBracket(umint(iPrevious)) && m_Tokens.f_IsText(Tokens[umint(iPrevious)], ">"))
					fp_RemoveSpaceBefore(i, "angle-space", "nested template argument lists close as one '>>'");

				continue;
			}

			if (m_Tokens.f_IsText(Token, ","))
			{
				fp_RemoveSpaceBefore(i, "comma-space", "the comma operator has no space before it");
				auto iNext = fp_NextSignificant(i);
				if (iNext >= 0 && !Tokens[umint(iNext)].m_bMultiLine && Tokens[umint(iNext)].m_Kind != ECodeTokenKind::mc_Newline)
				{
					bool bClosing = m_Tokens.f_IsText(Tokens[umint(iNext)], ")") || m_Tokens.f_IsText(Tokens[umint(iNext)], "]") || m_Tokens.f_IsText(Tokens[umint(iNext)], "}");
					if (!bClosing)
						fp_EnsureSingleSpace(i, false, "comma-space", "the comma operator has one space after it");
				}

				continue;
			}

			if (!fg_IsSpacedOperator(m_Tokens, Token))
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

		// Every other pair of tokens on one line takes the spelling the standard settles
		// for it, where it settles one: a member access and a scope marker hug their
		// operands, a keyword stands apart from its parenthesis. A gap the layout breaks
		// is its own, and one holding anything but spaces is not on one line.
		for (umint i = 1; i < Tokens.f_GetLen(); ++i)
		{
			if (!fg_IsCodeToken(Tokens[i]))
				continue;

			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious < 0 || fp_IsBreakGap(i))
				continue;

			bool bOneLine = true;
			for (auto iGap = umint(iPrevious) + 1; iGap < i; ++iGap)
				bOneLine &= Tokens[iGap].m_Kind == ECodeTokenKind::mc_Whitespace;

			if (!bOneLine)
				continue;

			auto Spacing = fp_GetCanonicalSpacing(umint(iPrevious), i);
			if (Spacing == ECodeSpacing::mc_Space)
				fp_EnsureSingleSpace(i, true, "token-space", "these tokens are written with one space between them");
			else if (Spacing == ECodeSpacing::mc_None)
				fp_RemoveSpaceBefore(i, "token-space", "these tokens are written without a space between them");
		}

		// A comment trailing code stands one space behind it; columns aligned with tabs drift
		// apart as soon as the code in front of them is respaced. A block comment on one line
		// trails its code where it ends the line.
		for (umint i = 1; i < Tokens.f_GetLen(); ++i)
		{
			bool bTrailing = Tokens[i].m_Kind == ECodeTokenKind::mc_LineComment;
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_BlockComment && !Tokens[i].m_bMultiLine)
			{
				auto iAfter = i + 1;
				while (iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Whitespace)
					++iAfter;

				auto iBefore = fp_PreviousSignificant(i);
				bool bPreviousCode = iBefore >= 0 && fg_IsCodeToken(Tokens[umint(iBefore)]);
				bTrailing = bPreviousCode && (iAfter >= Tokens.f_GetLen() || Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Newline);
			}

			if (bTrailing && !fp_IsFirstOnLine(i))
				fp_EnsureSingleSpace(i, true, "comment-space", "a trailing comment stands one space behind its code");
		}
	}

	// Lines that continue what a line above them started are placed against that line as the
	// plan lays it out, which only the plan says.
	//
	// A comment continued on the lines below the code it trails is aligned with it. A comment
	// line at its code's own indentation, or at less, is a comment of its own rather than a
	// continuation.
	//
	// A string literal continued on the next line, 'Text = "a "' over '"b"', stands one level
	// in when the line it continues starts its statement, and at that line's level when the
	// line is an element of a list or an operand the statement was already split at.
	void CFormattingAnalyzer::fp_AlignContinuations()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Source = m_Request.m_Source;
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		TCVector<CCodeFormattingEdit> Plan;
		CStr Formatted;
		bool bPlanned = false;
		auto fFormattedColumn = [&](umint _iOffset, umint &o_nColumns)
			{
				if (!bPlanned)
				{
					TCVector<umint> Sources;
					fp_BuildPlan(Plan, Sources);
					Formatted = fg_ApplyCodeFormattingEdits(Source, Plan);
					bPlanned = true;
				}

				auto iMapped = fg_MapOffsetToConverted(Plan, _iOffset, false);
				auto iFormattedStart = iMapped;
				while (iFormattedStart && Formatted.f_GetStr()[iFormattedStart - 1] != '\n' && Formatted.f_GetStr()[iFormattedStart - 1] != '\r')
					--iFormattedStart;

				return fg_MeasureTextColumns(Formatted.f_GetStr() + iFormattedStart, iMapped - iFormattedStart, nTab, o_nColumns);
			}
		;
		auto fPlaceLine = [&](umint _iStart, umint _iIndent, umint _nColumns, ch8 const *_pRule, ch8 const *_pExplanation)
			{
				// The layout's own placement of the line gives way to the alignment.
				for (umint iEdit = 0; iEdit < m_Edits.f_GetLen(); ++iEdit)
				{
					auto const &Edit = m_Edits[iEdit];
					if (Edit.m_iOffset >= _iStart && Edit.f_GetEnd() <= _iIndent)
						m_bEditDropped[iEdit] = 1;
				}

				auto Replacement = fp_MakeIndent(_nColumns);
				if (Replacement != CStr(Source.f_GetStr() + _iStart, _iIndent - _iStart))
					fp_AddEdit(_pRule, _iStart, _iIndent - _iStart, Replacement, _pExplanation);
			}
		;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_LineComment || fp_IsFirstOnLine(i))
				continue;

			auto nCodeIndent = fp_GetSourceLineIndent(i);
			umint nTarget = TCLimitsInt<umint>::mc_Max;
			for (auto iLine = m_Lines.f_FindLine(Tokens[i].m_iOffset) + 1; iLine < m_Lines.f_GetLineCount(); ++iLine)
			{
				auto iStart = m_Lines.f_GetLineStart(iLine);
				auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
				auto iIndent = iStart;
				while (iIndent < iEnd && fg_IsSpaceOrTab(Source.f_GetStr()[iIndent]))
					++iIndent;

				if (iIndent == iEnd || m_bProtectedStart[iLine])
					break;

				auto const &Comment = Tokens[m_Tokens.f_FindToken(iIndent)];
				if (Comment.m_Kind != ECodeTokenKind::mc_LineComment || Comment.m_iOffset != iIndent)
					break;

				umint nColumns = 0;
				if (!fg_MeasureTextColumns(Source.f_GetStr() + iStart, iIndent - iStart, nTab, nColumns) || nColumns <= nCodeIndent)
					break;

				if (nTarget == TCLimitsInt<umint>::mc_Max && !fFormattedColumn(Tokens[i].m_iOffset, nTarget))
					break;

				fPlaceLine(iStart, iIndent, nTarget, "comment-space", "a comment continued below the code it trails aligns with it");
			}
		}

		auto const &Nodes = m_Structure.f_GetNodes();
		// An opened DSL array's bracket stands on a line of its own, at the element's level, or
		// one level in when the line starts its statement, and what it holds moves with it:
		// '"Names"_o= _o' over '[' over the elements over ']'. The shift of an array around
		// another is added to the inner one's, which is laid out after it.
		struct CShift
		{
			umint m_iFirstLine = 0;
			umint m_iLastLine = 0;
			aint m_nColumns = 0;
		};
		TCVector<CShift> Shifts;
		auto fShiftAt = [&](umint _iLine)
			{
				aint nShift = 0;
				for (auto const &Shift : Shifts)
				{
					if (_iLine >= Shift.m_iFirstLine && _iLine <= Shift.m_iLastLine)
						nShift += Shift.m_nColumns;
				}

				return nShift;
			}
		;
		auto fLineIndentOffset = [&](umint _iLine)
			{
				auto iIndent = m_Lines.f_GetLineStart(_iLine);
				auto iEnd = m_Lines.f_GetLineContentEnd(_iLine);
				while (iIndent < iEnd && fg_IsSpaceOrTab(Source.f_GetStr()[iIndent]))
					++iIndent;

				return iIndent;
			}
		;
		auto const &LineEnding = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());
		auto fBreakBefore = [&](umint _iToken, umint _nColumns, ch8 const *_pExplanation)
			{
				auto iGap = Tokens[umint(fp_PreviousSignificant(_iToken))].f_GetEnd();
				for (umint iEdit = 0; iEdit < m_Edits.f_GetLen(); ++iEdit)
				{
					if (m_Edits[iEdit].m_iOffset >= iGap && m_Edits[iEdit].f_GetEnd() <= Tokens[_iToken].m_iOffset)
						m_bEditDropped[iEdit] = 1;
				}

				fp_AddEdit("line-break", iGap, Tokens[_iToken].m_iOffset - iGap, LineEnding + fp_MakeIndent(_nColumns), _pExplanation);
			}
		;
		for (umint iNode = 0; m_Structure.f_IsComplete() && iNode < Nodes.f_GetLen(); ++iNode)
		{
			auto const &Node = Nodes[iNode];
			if (Node.m_Kind != ECodeNodeKind::mc_Group)
				continue;

			auto iOpen = Node.m_iFirstToken;
			auto iClose = Node.m_iLastToken;
			if (iClose >= Tokens.f_GetLen())
				continue;

			auto iMarker = fp_PreviousCode(iOpen);
			auto iFirstElement = fp_NextCode(iOpen);
			bool bArray = Node.m_Bracket == ECodeBracket::mc_Square && iMarker >= 0 && m_Tokens.f_HasRole(Tokens[umint(iMarker)], ECodeNameRole::mc_DSLMarker);
			// An object is told by its first key, which the DSL's marker makes one: '{"Key"_o= 5'.
			auto iKeyMarker = iFirstElement >= 0 ? fp_NextCode(umint(iFirstElement)) : aint(-1);
			bool bObject = Node.m_Bracket == ECodeBracket::mc_Brace
				&& iFirstElement >= 0
				&& Tokens[umint(iFirstElement)].m_Kind == ECodeTokenKind::mc_StringLiteral
				&& iKeyMarker >= 0
				&& m_Tokens.f_HasRole(Tokens[umint(iKeyMarker)], ECodeNameRole::mc_DSLMarker)
			;
			// A directive's lines are the conditional's, and keep where they are.
			if ((!bArray && !bObject) || Node.m_bHasDirective)
				continue;

			auto iOpenLine = m_Lines.f_FindLine(Tokens[iOpen].m_iOffset);
			auto iCloseLine = m_Lines.f_FindLine(Tokens[iClose].m_iOffset);
			auto iAfterOpen = fp_NextSignificant(iOpen);
			bool bOpened = iAfterOpen >= 0 && Tokens[umint(iAfterOpen)].m_Kind == ECodeTokenKind::mc_Newline && iCloseLine > iOpenLine;
			if (!bOpened)
				continue;

			// Where the bracket stands: its own line, or the one it is moved to.
			bool bMoveOpener = bArray && !fp_IsFirstOnLine(iOpen);
			aint nTarget = 0;
			{
				auto iHeadFirst = m_Tokens.f_FindToken(fLineIndentOffset(iOpenLine));
				umint nHead = 0;
				if (!fFormattedColumn(Tokens[iHeadFirst].m_iOffset, nHead))
					continue;

				nTarget = aint(nHead) + fShiftAt(iOpenLine);
				if (bMoveOpener)
				{
					auto iStatement = m_Structure.f_FindEnclosingNode(iOpen);
					while (iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement)
						iStatement = Nodes[iStatement].m_iParent;

					if (iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_iFirstToken == iHeadFirst)
						nTarget += aint(nTab);
				}
			}

			// The elements stand one level in from the bracket, the first one's line saying how
			// far the lines it holds move.
			aint nDelta = 0;
			{
				auto iFirstLine = iOpenLine + 1;
				while (iFirstLine < iCloseLine && fLineIndentOffset(iFirstLine) == m_Lines.f_GetLineContentEnd(iFirstLine))
					++iFirstLine;

				umint nFirst = 0;
				if (!fFormattedColumn(fLineIndentOffset(iFirstLine), nFirst))
					continue;

				nDelta = nTarget + aint(nTab) - (aint(nFirst) + fShiftAt(iFirstLine));
			}

			if (bMoveOpener)
				fBreakBefore(iOpen, umint(fg_Max(nTarget, aint(0))), "an opened list's bracket stands on a line of its own");

			bool bCloserOwnLine = fp_IsFirstOnLine(iClose);
			for (auto iLine = iOpenLine + 1; iLine <= iCloseLine; ++iLine)
			{
				auto iIndent = fLineIndentOffset(iLine);
				if (m_bProtectedStart[iLine])
					continue;

				// A blank line sets nothing apart among the elements of data.
				if (iIndent == m_Lines.f_GetLineContentEnd(iLine))
				{
					if (iLine < iCloseLine)
					{
						auto iBlank = m_Lines.f_GetLineStart(iLine);
						fp_AddEdit("blank-line", iBlank, m_Lines.f_GetLineStart(iLine + 1) - iBlank, {}, "no blank line stands between the elements of a list");
					}

					continue;
				}

				if (!nDelta)
					continue;

				umint nIndent = 0;
				if (!fFormattedColumn(iIndent, nIndent))
					continue;

				auto nPlaced = aint(nIndent) + fShiftAt(iLine) + nDelta;
				fPlaceLine(m_Lines.f_GetLineStart(iLine), iIndent, umint(fg_Max(nPlaced, aint(0))), "line-break", "an opened list's lines move with its bracket");
			}

			if (!bCloserOwnLine)
				fBreakBefore(iClose, umint(fg_Max(nTarget, aint(0))), "an opened list's closing bracket stands on a line of its own");

			if (nDelta)
				Shifts.f_Insert(CShift{iOpenLine + 1, iCloseLine, nDelta});
		}

		for (umint i = 0; m_Structure.f_IsComplete() && i < Tokens.f_GetLen(); ++i)
		{
			auto iPrevious = fp_ContinuesLiteral(i);
			if (iPrevious < 0)
				continue;

			auto iLine = m_Lines.f_FindLine(Tokens[i].m_iOffset);
			if (m_bProtectedStart[iLine])
				continue;

			// The chain is placed against the line its first literal stands on.
			auto iHead = umint(iPrevious);
			for (auto iBefore = fp_ContinuesLiteral(iHead); iBefore >= 0; iBefore = fp_ContinuesLiteral(iHead))
				iHead = umint(iBefore);

			auto iHeadLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[iHead].m_iOffset));
			auto iHeadFirst = m_Tokens.f_FindToken(iHeadLineStart);
			while (iHeadFirst < iHead && (Tokens[iHeadFirst].m_Kind == ECodeTokenKind::mc_Whitespace || Tokens[iHeadFirst].m_Kind == ECodeTokenKind::mc_ByteOrderMark))
				++iHeadFirst;

			auto iStatement = m_Structure.f_FindEnclosingNode(iHead);
			while (iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement)
				iStatement = Nodes[iStatement].m_iParent;

			bool bStartsStatement = iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_iFirstToken == iHeadFirst;
			bStartsStatement |= fp_ContinuesValue(i);
			umint nColumns = 0;
			if (!fFormattedColumn(Tokens[iHeadFirst].m_iOffset, nColumns))
				continue;

			fPlaceLine
				(
					m_Lines.f_GetLineStart(iLine)
					, Tokens[i].m_iOffset
					, umint(fg_Max(aint(nColumns) + fShiftAt(m_Lines.f_FindLine(Tokens[iHead].m_iOffset)), aint(0))) + (bStartsStatement ? nTab : 0)
					, "string-continuation"
					, bStartsStatement ? "a string continued on the next line stands one level in" : "a string continued on the next line stands at its element's level"
				)
			;
		}
	}

	void CFormattingAnalyzer::fp_RemoveFollowingBlankLines(umint _iToken, ch8 const *_pRule, ch8 const *_pExplanation)
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

		fp_AddEdit(_pRule, iBlankStart, iBlankEnd - iBlankStart, {}, _pExplanation);
	}

	void CFormattingAnalyzer::fp_RuleBlankLines()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		m_bBlankBefore.f_SetLen(Tokens.f_GetLen());
		for (auto &bBlank : m_bBlankBefore)
			bBlank = 0;

		// One blank line separates what it separates; a second adds nothing. The first of a
		// run stays, and the rules below take that one too where none belongs.
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Newline)
				continue;

			umint nNewlines = 1;
			umint iKeepEnd = 0;
			umint iRunEnd = 0;
			umint iNext = i + 1;
			for (; iNext < Tokens.f_GetLen(); ++iNext)
			{
				if (Tokens[iNext].m_Kind == ECodeTokenKind::mc_Whitespace)
					continue;

				if (Tokens[iNext].m_Kind != ECodeTokenKind::mc_Newline)
					break;

				++nNewlines;
				iRunEnd = Tokens[iNext].f_GetEnd();
				if (nNewlines == 2)
					iKeepEnd = iRunEnd;
			}

			if (nNewlines > 2)
				fp_AddEdit("blank-line", iKeepEnd, iRunEnd - iKeepEnd, {}, "one blank line separates, and a second adds nothing");

			i = iNext - 1;
		}

		// A function's body, and a type's definition, is set off from what follows it by a
		// blank line, unless that is the end of the scope it stands in. A directive behind
		// the body belongs to a conditional whose lines are its own.
		auto const &Nodes = m_Structure.f_GetNodes();
		for (umint iNode = 0; m_Structure.f_IsComplete() && iNode < Nodes.f_GetLen(); ++iNode)
		{
			auto const &Node = Nodes[iNode];
			if (Node.m_Kind != ECodeNodeKind::mc_Statement || Node.m_iParent >= Nodes.f_GetLen())
				continue;

			auto ParentKind = Nodes[Node.m_iParent].m_Kind;
			if (ParentKind != ECodeNodeKind::mc_Block && ParentKind != ECodeNodeKind::mc_File)
				continue;

			umint iParameters = TCLimitsInt<umint>::mc_Max;
			bool bBody = false;
			for (auto iChild : Node.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_Bracket == ECodeBracket::mc_Paren && iParameters == TCLimitsInt<umint>::mc_Max)
					iParameters = Child.m_iLastToken;

				bBody |= Child.m_Kind == ECodeNodeKind::mc_Block && Child.m_iLastToken == Node.m_iLastToken;
			}

			// A type is defined by a declaration that starts with its class key, behind any
			// template header, and holds the body: 'struct CFoo : CBase { ... };'. A braced
			// initializer behind an '=' is a variable's, 'struct timespec Time = {...};'.
			bool bTypeDefinition = false;
			if (!bBody)
			{
				auto iKey = fp_SkipTemplateHeader(Node.m_iFirstToken);
				auto const &Key = Tokens[iKey];
				bool bClassKey = m_Tokens.f_IsText(Key, "struct") || m_Tokens.f_IsText(Key, "class") || m_Tokens.f_IsText(Key, "union") || m_Tokens.f_IsText(Key, "enum");
				for (auto iChild : Node.m_Children)
				{
					auto const &Child = Nodes[iChild];
					if (!bClassKey || Child.m_Kind != ECodeNodeKind::mc_Block || Child.m_iFirstToken <= iKey)
						continue;

					bool bInitializer = false;
					for (auto i = iKey; i < Child.m_iFirstToken && !bInitializer; ++i)
						bInitializer = m_TokenDepth[i] == m_TokenDepth[iKey] && m_Tokens.f_IsText(Tokens[i], "=");

					bTypeDefinition |= !bInitializer;
				}
			}

			bool bFunction = bBody && iParameters != TCLimitsInt<umint>::mc_Max && fg_ClosesParameterList(m_Tokens, m_Structure, iParameters);
			if (!bFunction && !bTypeDefinition)
				continue;

			umint iNewline = TCLimitsInt<umint>::mc_Max;
			umint nNewlines = 0;
			umint iNext = Node.m_iLastToken + 1;
			for (; iNext < Tokens.f_GetLen(); ++iNext)
			{
				auto Kind = Tokens[iNext].m_Kind;
				if (Kind == ECodeTokenKind::mc_Newline)
				{
					if (!nNewlines)
						iNewline = iNext;

					++nNewlines;
				}
				else if (Kind != ECodeTokenKind::mc_Whitespace && !(Kind == ECodeTokenKind::mc_LineComment && !nNewlines))
					break;
			}

			// A brace behind the block says the block was no body: a requires expression's
			// requirements end in one, with the function's body below them.
			bool bEndsScope = iNext < Tokens.f_GetLen() && (m_Tokens.f_IsText(Tokens[iNext], "}") || m_Tokens.f_IsText(Tokens[iNext], "{"));
			// A conditional's directive belongs to lines of its own and a pragma to the code it
			// brackets; any other, a '#define' behind a type included, is set off like code.
			bool bAttachedDirective = false;
			if (iNext < Tokens.f_GetLen() && Tokens[iNext].m_Kind == ECodeTokenKind::mc_Preprocessor)
			{
				auto Keyword = fp_GetDirectiveKeyword(iNext);
				for (auto pAttached : {"if", "ifdef", "ifndef", "elif", "elifdef", "elifndef", "else", "endif", "pragma"})
					bAttachedDirective |= Keyword == pAttached;
			}

			if (iNext >= Tokens.f_GetLen() || nNewlines > 1 || bAttachedDirective || bEndsScope)
				continue;

			// A macro invoked directly under a body belongs to the function, as the one
			// that implements the streaming of the type it was written for does.
			if (Tokens[iNext].m_Kind == ECodeTokenKind::mc_Identifier)
			{
				auto iOpen = fp_NextCode(iNext);
				bool bMacro = m_Tokens.f_HasRole(Tokens[iNext], ECodeNameRole::mc_Macro)
					&& iOpen >= 0
					&& m_Tokens.f_IsText(Tokens[umint(iOpen)], "(")
				;
				if (bMacro)
					continue;
			}

			if (!nNewlines)
			{
				if (fg_IsCodeToken(Tokens[iNext]))
					m_bBlankBefore[iNext] = 1;

				continue;
			}

			auto Newline = m_Tokens.f_GetText(Tokens[iNewline]);
			if (bFunction)
				fp_AddEdit("function-blank-line", Tokens[iNewline].m_iOffset, Tokens[iNewline].m_nLength, Newline + Newline, "a blank line follows a function's body");
			else
				fp_AddEdit("type-blank-line", Tokens[iNewline].m_iOffset, Tokens[iNewline].m_nLength, Newline + Newline, "a blank line follows a type's definition");
		}

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind == ECodeTokenKind::mc_Punctuator && m_Tokens.f_IsText(Token, "{"))
			{
				fp_RemoveFollowingBlankLines(i, "block-blank-line", "no blank line follows an opening brace");

				continue;
			}

			// Nor does one stand in front of the closing brace, whatever ends the line above it.
			if (Token.m_Kind == ECodeTokenKind::mc_Punctuator && m_Tokens.f_IsText(Token, "}"))
			{
				umint iFirstNewline = TCLimitsInt<umint>::mc_Max;
				umint iLastNewline = TCLimitsInt<umint>::mc_Max;
				for (umint iGap = i; iGap; --iGap)
				{
					auto const &Gap = Tokens[iGap - 1];
					if (Gap.m_Kind == ECodeTokenKind::mc_Newline)
					{
						iFirstNewline = iGap - 1;
						if (iLastNewline == TCLimitsInt<umint>::mc_Max)
							iLastNewline = iGap - 1;
					}
					else if (Gap.m_Kind != ECodeTokenKind::mc_Whitespace)
						break;
				}

				if (iFirstNewline != iLastNewline)
				{
					auto iBlankStart = Tokens[iFirstNewline].f_GetEnd();
					fp_AddEdit("block-blank-line", iBlankStart, Tokens[iLastNewline].f_GetEnd() - iBlankStart, {}, "no blank line stands in front of a closing brace");
				}

				continue;
			}

			if (Token.m_Kind != ECodeTokenKind::mc_Identifier)
				continue;

			// An access specifier opens a section of its class: a blank line sets it off from
			// the section in front of it, and its members follow it at once. The first in a
			// class stands under the opening brace, and one with a comment or a directive
			// above it keeps the lines around that.
			if (m_Tokens.f_IsText(Token, "public") || m_Tokens.f_IsText(Token, "private") || m_Tokens.f_IsText(Token, "protected"))
			{
				auto iColon = fp_NextCode(i);
				auto iBefore = fp_PreviousCode(i);
				if (iColon < 0 || iBefore < 0 || !m_Tokens.f_IsText(Tokens[umint(iColon)], ":"))
					continue;

				auto const &Before = Tokens[umint(iBefore)];
				bool bLabel = m_Tokens.f_IsText(Before, "{") || m_Tokens.f_IsText(Before, "}") || m_Tokens.f_IsText(Before, ";") || m_Tokens.f_IsText(Before, ":");
				if (!bLabel)
					continue;

				fp_RemoveFollowingBlankLines(umint(iColon), "access-blank-line", "no blank line follows an access specifier");
				// One standing right behind another opens a section that holds nothing, and
				// follows the label above it the way a member does.
				auto iLabel = m_Tokens.f_IsText(Before, ":") ? fp_PreviousCode(umint(iBefore)) : aint(-1);
				bool bAfterLabel = iLabel >= 0
					&& (m_Tokens.f_IsText(Tokens[umint(iLabel)], "public") || m_Tokens.f_IsText(Tokens[umint(iLabel)], "private") || m_Tokens.f_IsText(Tokens[umint(iLabel)], "protected"))
				;
				if (m_Tokens.f_IsText(Before, "{") || bAfterLabel)
					continue;

				umint nNewlines = 0;
				umint iNewline = 0;
				bool bPlain = true;
				for (umint iGap = umint(iBefore) + 1; iGap < i; ++iGap)
				{
					if (Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline)
					{
						++nNewlines;
						iNewline = iGap;
					}
					else if (Tokens[iGap].m_Kind != ECodeTokenKind::mc_Whitespace)
						bPlain = false;
				}

				if (!bPlain || nNewlines > 1)
					continue;

				// A specifier still on the line of what is in front of it gets its line from
				// the layout, which writes the blank one with it.
				if (!nNewlines)
				{
					m_bBlankBefore[i] = 1;

					continue;
				}

				auto Newline = m_Tokens.f_GetText(Tokens[iNewline]);
				fp_AddEdit("access-blank-line", Tokens[iNewline].m_iOffset, Tokens[iNewline].m_nLength, Newline + Newline, "a blank line stands in front of an access specifier");

				continue;
			}

			bool bCase = m_Tokens.f_IsText(Token, "case");
			if (!bCase && !m_Tokens.f_IsText(Token, "default"))
				continue;

			// A label follows the end of the previous statement or the start of the switch body.
			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious < 0)
				continue;

			auto const &Previous = Tokens[umint(iPrevious)];
			bool bLabelContext = m_Tokens.f_IsText(Previous, "{") || m_Tokens.f_IsText(Previous, "}") || m_Tokens.f_IsText(Previous, ";") || m_Tokens.f_IsText(Previous, ":");
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

	// The limit applies to the lines the rules produce, not to the ones the source has: a
	// line they break up is no violation, and only what they leave too long is reported,
	// against the source line the result's line came from.
	void CFormattingAnalyzer::fp_DiagnoseLineLength(NContainer::TCVector<CCodeFormattingEdit> const &_Edits)
	{
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		if (!nMaxColumns)
			return;

		auto Formatted = fg_ApplyCodeFormattingEdits(m_Request.m_Source, _Edits);
		CTextLineMap Lines(Formatted);
		auto iReported = TCLimitsInt<umint>::mc_Max;
		for (umint iLine = 0; iLine < Lines.f_GetLineCount(); ++iLine)
		{
			auto iStart = Lines.f_GetLineStart(iLine);
			auto nLength = Lines.f_GetLine(iLine).m_nLength;
			// The file-leading byte-order mark occupies no column.
			if (!iLine)
			{
				auto nBom = fg_GetTextBomLength(Formatted);
				iStart += nBom;
				nLength -= fg_Min(nBom, nLength);
			}

			umint nColumns = 0;
			bool bMeasured = fg_MeasureTextColumns(Formatted.f_GetStr() + iStart, nLength, m_Request.m_Settings.m_nTabWidth, nColumns);
			if (bMeasured && nColumns <= nMaxColumns)
				continue;

			// Several lines of the result can come from one line of the source, which is
			// then named once.
			// A region the formatter is turned off for keeps its lines as written, long ones too.
			auto iSource = m_Lines.f_FindLine(fg_MapOffsetToOriginal(_Edits, Lines.f_GetLineStart(iLine)));
			if (iSource == iReported || !fp_IsLineSelected(iSource) || fp_IsDisabled(m_Lines.f_GetLineStart(iSource), m_Lines.f_GetLine(iSource).m_nLength))
				continue;

			iReported = iSource;
			CStr Explanation = bMeasured
				? "line length {} exceeds max_line_length = {}"_f << nColumns << nMaxColumns
				: "line length overflows the column counter and exceeds max_line_length = {}"_f << nMaxColumns
			;
			fp_AddDiagnostic("line-length", m_Lines.f_GetLineStart(iSource), m_Lines.f_GetLine(iSource).m_nLength, Explanation, false);
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

		auto Language = Settings.f_GetLanguage();
		bool bBuildSystem = Language == ECodeLanguage::mc_BuildSystem;

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
		if (bBuildSystem)
		{
			if (!fp_PrepareBuildSystemLines(Explanation))
				return fUnsupported(Explanation);
		}
		else
		{
			fp_PrepareTokenDepth();
			fp_PrepareDirectives();
		}

		if (!fp_CollectDisabledRegions(Explanation))
			return fFailed(Explanation);

		if (!fp_ResolveRanges(Explanation))
			return fFailed(Explanation);

		// Moving a return type behind its parameter list, and dropping the braces around a
		// single guarded statement, change tokens, which no other rule does. Those
		// conversions are decided first, and the layout is then made on the converted
		// source, so the lines it settles on are the lines a later pass sees.
		// A qualifier in front of its type moves behind it before anything else is decided,
		// and a terminator that ends nothing goes. The other conversions rewrite text a
		// qualifier can stand in, a return type above all, and count the statements a
		// block holds, so they are made on the source this stage leaves, in one of their own.
		m_Baseline = m_Request.m_Source;
		if (m_bAllowQualifiers && !bBuildSystem)
		{
			fp_ConvertQualifiers();
			fp_ConvertSpecifiers();
			fp_ConvertEmptyStatements();
			fp_ConvertEnumCommas();
		}

		bool bQualifierStage = !m_Structural.f_IsEmpty();
		if (!bQualifierStage && m_bAllowConversions && !bBuildSystem)
		{
			m_bProbing = true;
			fp_RuleLineBreaks();
			m_bProbing = false;
		}

		if (!m_Structural.f_IsEmpty())
		{
			m_Structural.f_Sort
				(
					[](CCodeFormattingEdit const &_Left, CCodeFormattingEdit const &_Right)
					{
						return _Left.m_iOffset <=> _Right.m_iOffset;
					}
				)
			;
			auto ConvertedSource = fg_ApplyCodeFormattingEdits(m_Request.m_Source, m_Structural);
			// Moving a qualifier changes nothing but where it stands, which is checked here
			// since the result is only ever compared against the converted source.
			if (bQualifierStage && fg_GetReorderInvariant(m_Request.m_Source) != fg_GetReorderInvariant(ConvertedSource))
				return fFailed("Reordering qualifiers and specifiers would change more than where they stand; no edits were produced");

			CCodeFormattingRequest Nested = m_Request;
			Nested.m_Source = ConvertedSource;
			for (auto &Range : Nested.m_Ranges)
			{
				auto iStart = fg_MapOffsetToConverted(m_Structural, Range.m_iOffset, false);
				auto iEnd = fg_MapOffsetToConverted(m_Structural, Range.f_GetEnd(), true);
				Range.m_iOffset = iStart;
				Range.m_nLength = iEnd - iStart;
			}

			CFormattingAnalyzer Inner(Nested, bQualifierStage && m_bAllowConversions, false);
			auto Converted = Inner.f_Analyze(false);
			if (Converted.m_Status != ECodeFormattingStatus::mc_Complete)
				return fFailed("Laying out the converted source failed: {}"_f << Converted.m_Explanation);

			m_Baseline = Inner.m_Baseline;

			if (!fg_ComposeEdits(m_Structural, ConvertedSource, Converted.m_Edits, Result.m_Edits, Explanation))
				return fFailed(Explanation);

			for (auto const &Edit : m_Structural)
			{
				CStr Explanation = "the return type moves behind the parameter list";
				if (Edit.m_Rule == "braces")
					Explanation = "a guarded statement on one line stands without braces, one across lines within them";
				else if (Edit.m_Rule == "east-qualifier")
					Explanation = "a qualifier stands behind the type it qualifies";
				else if (Edit.m_Rule == "specifier-order")
					Explanation = "'static' stands in front of 'constexpr'";
				else if (Edit.m_Rule == "empty-statement")
					Explanation = "a terminator that ends nothing is taken out";
				else if (Edit.m_Rule == "enum-comma")
					Explanation = "an enumerator's comma stands in front of it, and the last one has none";
				else if (Edit.m_Rule == "list-comma")
					Explanation = "a comment describes the element in front of it, which the comma behind it goes past";

				fp_AddDiagnostic(Edit.m_Rule, Edit.m_iOffset, Edit.m_nLength, Explanation, true);
			}

			for (auto Diagnostic : Converted.m_Diagnostics)
			{
				Diagnostic.m_iOffset = fg_MapOffsetToOriginal(m_Structural, Diagnostic.m_iOffset);
				Diagnostic.m_iLine = m_Lines.f_FindLine(Diagnostic.m_iOffset) + 1;
				Diagnostic.m_iColumn = fp_GetColumn(Diagnostic.m_iOffset);
				m_Diagnostics.f_Insert(Diagnostic);
			}
		}
		else
		{
			fp_RuleTrailingWhitespace();
			fp_RuleLineEndings();
			fp_RuleFinalNewline();
			if (bBuildSystem)
			{
				fp_RuleBuildSystemBlankLines();
				fp_RuleBuildSystemIndentation();
			}
			else
			{
				fp_RuleBlankLines();
				fp_RuleLineBreaks();
				fp_RuleTokenSpacing();
				fp_EmitLayout();
				// The layout writes the indentation of every line it places, so what this rule
				// is left to spell is only the lines it did not.
				fp_RuleIndentation();

				fp_LimitJoinedLines();
				fp_AlignContinuations();
			}

			NContainer::TCVector<umint> Sources;
			fp_BuildPlan(Result.m_Edits, Sources);
			for (auto iEdit : Sources)
			{
				auto const &Edit = m_Edits[iEdit];
				auto &Diagnostic = m_Diagnostics.f_Insert();
				Diagnostic.m_Rule = Edit.m_Rule;
				Diagnostic.m_Severity = ECodeFormattingSeverity::mc_Warning;
				Diagnostic.m_iOffset = Edit.m_iOffset;
				Diagnostic.m_nLength = Edit.m_nLength;
				Diagnostic.m_iLine = m_Lines.f_FindLine(Edit.m_iOffset) + 1;
				Diagnostic.m_iColumn = fp_GetColumn(Edit.m_iOffset);
				Diagnostic.m_Explanation = m_EditExplanations[iEdit];
				Diagnostic.m_bHasAutomaticFix = true;
			}

			fp_DiagnoseLineLength(Result.m_Edits);
		}

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

		// A pass that changes nothing is its own proof: the source it would be analyzed
		// against again is the very source it was analyzed on, and the analysis is a
		// function of that source alone.
		if (!Result.f_HasEdits() && m_Structural.f_IsEmpty())
			return Result;

		// Every rule but the conversion leaves the token stream alone, so the result has to
		// match the converted source token for token.
		auto Formatted = fg_ApplyCodeFormattingEdits(m_Request.m_Source, Result.m_Edits);
		if (!fg_HasEquivalentCodeTokens(m_Baseline, Formatted, Language))
		{
			return fFailed
				(
					"Formatting would change the token stream ({}); no edits were produced"_f
					<< fg_DescribeCodeTokenDifference(m_Baseline, Formatted, Language)
				)
			;
		}

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
				auto iLine = FormattedLines.f_FindLine(Edit.m_iOffset);
				auto iStart = FormattedLines.f_GetLineStart(iLine);
				CStr Line(Formatted.f_GetStr() + iStart, FormattedLines.f_GetLineContentEnd(iLine) - iStart);

				// The line itself is what says which construct disagreed with the first pass.
				return fFailed
					(
						"Formatting did not reach a stable result: {} would still change formatted line {}: {}"_f
						<< Edit.m_Rule
						<< iLine + 1
						<< Line
					)
				;
			}
		}

		return Result;
	}
}

namespace
{
	bool fg_IsBuildSystemOpener(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Token.m_Kind == ECodeTokenKind::mc_Punctuator && (_Tokens.f_IsText(_Token, "{") || _Tokens.f_IsText(_Token, "[") || _Tokens.f_IsText(_Token, "("));
	}

	bool fg_IsBuildSystemCloser(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Token.m_Kind == ECodeTokenKind::mc_Punctuator && (_Tokens.f_IsText(_Token, "}") || _Tokens.f_IsText(_Token, "]") || _Tokens.f_IsText(_Token, ")"));
	}

	// A build system file is indented by its brackets alone, the scopes of the registry and
	// the objects, arrays and calls of a value alike: a line stands at the level of the
	// innermost bracket open at its start, and a line that starts by closing it one level
	// out. The brackets a line leaves open take one level more than the line, together,
	// unless the outer one holds lines of its own behind the inner one's, as the objects of
	// '[{ ... }, { ... }]' do, where each takes a level. A line a backslash continues aligns
	// with the value it continues, at the tab stop at or in front of it when tabs indent.
	bool CFormattingAnalyzer::fp_PrepareBuildSystemLines(CStr &o_Explanation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Settings = m_Request.m_Settings;
		auto nTab = Settings.m_nTabWidth;
		auto nLines = m_Lines.f_GetLineCount();
		m_BuildSystemLines.f_SetLen(nLines);

		// The line each opener's bracket closes on, indexed by token.
		TCVector<umint> CloseLine;
		CloseLine.f_SetLen(Tokens.f_GetLen());
		TCVector<umint> Pending;
		for (umint iToken = 0; iToken < Tokens.f_GetLen(); ++iToken)
		{
			auto const &Token = Tokens[iToken];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_ByteOrderMark)
				continue;

			auto iLine = m_Lines.f_FindLine(Token.m_iOffset);
			auto &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0)
				Line.m_iFirst = iToken;

			Line.m_iLast = iToken;
			if (fg_IsBuildSystemOpener(m_Tokens, Token))
				Pending.f_Insert(iToken);
			else if (fg_IsBuildSystemCloser(m_Tokens, Token))
			{
				if (Pending.f_IsEmpty())
				{
					o_Explanation = "'{}' at line {} closes no bracket"_f << m_Tokens.f_GetText(Token) << iLine + 1;

					return false;
				}

				CloseLine[Pending.f_GetLast()] = iLine;
				Pending.f_Remove(Pending.f_GetLen() - 1);
			}
		}

		if (!Pending.f_IsEmpty())
		{
			o_Explanation = "The bracket opened at line {} is never closed"_f << m_Lines.f_FindLine(Tokens[Pending.f_GetLast()].m_iOffset) + 1;

			return false;
		}

		struct COpen
		{
			umint m_Level = 0;									// The level of the lines inside the bracket.
			umint m_iCloseLine = 0;
		};

		TCVector<COpen> Open;
		bool bContinued = false;
		umint nChainColumns = 0;
		for (umint iLine = 0; iLine < nLines; ++iLine)
		{
			auto &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0)
				continue;

			bool bCloses = fg_IsBuildSystemCloser(m_Tokens, Tokens[umint(Line.m_iFirst)]);
			Line.m_Level = Open.f_IsEmpty() ? 0 : Open.f_GetLast().m_Level - (bCloses ? 1 : 0);
			Line.m_bContinued = bContinued;
			Line.m_nContinuationColumns = nChainColumns;

			auto nOpenAtStart = Open.f_GetLen();
			for (auto iToken = umint(Line.m_iFirst); iToken <= umint(Line.m_iLast); ++iToken)
			{
				if (fg_IsBuildSystemOpener(m_Tokens, Tokens[iToken]))
					Open.f_Insert(COpen{0, CloseLine[iToken]});
				else if (fg_IsBuildSystemCloser(m_Tokens, Tokens[iToken]))
				{
					Open.f_Remove(Open.f_GetLen() - 1);
					nOpenAtStart = fg_Min(nOpenAtStart, Open.f_GetLen());
				}
			}

			for (auto iOpen = nOpenAtStart; iOpen < Open.f_GetLen(); ++iOpen)
			{
				auto &Opened = Open[iOpen];
				if (iOpen == nOpenAtStart)
					Opened.m_Level = Line.m_Level + 1;
				else
				{
					auto const &Outer = Open[iOpen - 1];
					bool bOuterHoldsMore = false;
					for (auto iBetween = Opened.m_iCloseLine + 1; iBetween < Outer.m_iCloseLine && !bOuterHoldsMore; ++iBetween)
						bOuterHoldsMore = m_BuildSystemLines[iBetween].m_iFirst >= 0;

					Opened.m_Level = Outer.m_Level + (bOuterHoldsMore ? 1 : 0);
				}
			}

			bool bSplices = Tokens[umint(Line.m_iLast)].m_Kind == ECodeTokenKind::mc_LineSplice;
			if (bSplices && !bContinued)
			{
				nChainColumns = (Line.m_Level + 1) * nTab;
				auto iFirstOffset = Tokens[umint(Line.m_iFirst)].m_iOffset;
				for (auto iToken = umint(Line.m_iFirst); iToken <= umint(Line.m_iLast); ++iToken)
				{
					if (Tokens[iToken].m_Kind != ECodeTokenKind::mc_StringLiteral)
						continue;

					// The line is measured as it will be indented, which starts on a tab stop.
					umint nColumns = 0;
					if (fg_MeasureTextColumns(m_Request.m_Source.f_GetStr() + iFirstOffset, Tokens[iToken].m_iOffset - iFirstOffset, nTab, nColumns))
						nChainColumns = Line.m_Level * nTab + (Settings.m_bIndentWithTabs ? nColumns - nColumns % nTab : nColumns);

					break;
				}
			}

			bContinued = bSplices;
		}

		return true;
	}

	void CFormattingAnalyzer::fp_RuleBuildSystemIndentation()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Source = m_Request.m_Source;
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			auto const &Line = m_BuildSystemLines[iLine];
			if (Line.m_iFirst < 0 || m_bProtectedStart[iLine] || !fp_IsLineSelected(iLine))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			if (!iLine)
				iStart += fg_GetTextBomLength(Source);

			auto const &First = Tokens[umint(Line.m_iFirst)];
			// A comment at the very start of a line comments out what it stands in front of.
			bool bComment = First.m_Kind == ECodeTokenKind::mc_LineComment || First.m_Kind == ECodeTokenKind::mc_BlockComment;
			if (bComment && First.m_iOffset == iStart)
				continue;

			auto Canonical = fp_MakeIndent(Line.m_bContinued ? Line.m_nContinuationColumns : Line.m_Level * nTab);
			if (Canonical == CStr(Source.f_GetStr() + iStart, First.m_iOffset - iStart))
				continue;

			fp_AddEdit
				(
					"indentation"
					, iStart
					, First.m_iOffset - iStart
					, Canonical
					, Line.m_bContinued ? "a continued value aligns with the value it continues" : "a line stands one level deeper than the bracket open around it"
				)
			;
		}
	}

	// A blank line separates what it separates; a second adds nothing, and none belongs at
	// the start or end of the file or right inside a bracket.
	void CFormattingAnalyzer::fp_RuleBuildSystemBlankLines()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLines = m_Lines.f_GetLineCount();
		// The empty line behind the final line terminator is where the file ends, not a line.
		auto nContentLines = nLines;
		if (nLines && !m_Lines.f_GetLine(nLines - 1).m_nLength && m_Lines.f_GetLine(nLines - 1).m_Ending == ETextLineEnding::mc_None)
			--nContentLines;

		auto fIsBlank = [&](umint _iLine)
			{
				return m_BuildSystemLines[_iLine].m_iFirst < 0 && !m_bProtectedStart[_iLine];
			}
		;

		umint iLine = 0;
		while (iLine < nContentLines)
		{
			if (!fIsBlank(iLine))
			{
				++iLine;

				continue;
			}

			auto iRunEnd = iLine;
			while (iRunEnd < nContentLines && fIsBlank(iRunEnd))
				++iRunEnd;

			bool bAfterOpen = iLine && m_BuildSystemLines[iLine - 1].m_iLast >= 0
				&& fg_IsBuildSystemOpener(m_Tokens, Tokens[umint(m_BuildSystemLines[iLine - 1].m_iLast)])
			;
			bool bBeforeClose = iRunEnd < nContentLines && m_BuildSystemLines[iRunEnd].m_iFirst >= 0
				&& fg_IsBuildSystemCloser(m_Tokens, Tokens[umint(m_BuildSystemLines[iRunEnd].m_iFirst)])
			;
			bool bKeep = iLine && iRunEnd < nContentLines && !bAfterOpen && !bBeforeClose;
			auto iRemove = iLine + (bKeep ? 1 : 0);
			if (iRemove < iRunEnd)
			{
				auto iRemoveStart = m_Lines.f_GetLineStart(iRemove);
				auto iRemoveEnd = m_Request.m_Source.f_GetLen();
				if (iRunEnd < nLines)
					iRemoveEnd = m_Lines.f_GetLineStart(iRunEnd);
				else if (iLine)
				{
					// The file ends without a terminator on a blank line, so the terminator in front
					// of the run goes with it, and the final newline rule writes the file's last one.
					iRemoveStart = m_Lines.f_GetLineContentEnd(iLine - 1);
				}

				CStr Explanation = bKeep ? "one blank line separates, and a second adds nothing"
					: bAfterOpen || bBeforeClose ? "no blank line stands right inside a bracket"
					: "no blank line starts or ends the file"
				;
				fp_AddEdit("blank-line", iRemoveStart, iRemoveEnd - iRemoveStart, {}, Explanation);
			}

			iLine = iRunEnd;
		}
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

namespace
{
	umint CFormattingAnalyzer::fp_GetTokenColumns(CCodeToken const &_Token) const
	{
		// A token of this source is measured once; the width of its text never changes.
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto pFirst = Tokens.f_GetArray();
		if (&_Token >= pFirst && &_Token < pFirst + Tokens.f_GetLen())
		{
			umint iToken = umint(&_Token - pFirst);
			if (m_TokenColumns.f_GetLen() != Tokens.f_GetLen())
			{
				m_TokenColumns.f_SetLen(Tokens.f_GetLen());
				for (auto &Value : m_TokenColumns)
					Value = 0;
			}

			if (!m_TokenColumns[iToken])
			{
				umint nColumns = 0;
				bool bMeasured = fg_MeasureTextColumns(m_Request.m_Source.f_GetStr() + _Token.m_iOffset, _Token.m_nLength, m_Request.m_Settings.m_nTabWidth, nColumns);
				m_TokenColumns[iToken] = bMeasured && nColumns < TCLimitsInt<umint>::mc_Max ? nColumns + 1 : TCLimitsInt<umint>::mc_Max;
			}

			return m_TokenColumns[iToken] == TCLimitsInt<umint>::mc_Max ? TCLimitsInt<umint>::mc_Max : m_TokenColumns[iToken] - 1;
		}

		umint nColumns = 0;
		if (!fg_MeasureTextColumns(m_Request.m_Source.f_GetStr() + _Token.m_iOffset, _Token.m_nLength, m_Request.m_Settings.m_nTabWidth, nColumns))
			return TCLimitsInt<umint>::mc_Max;

		return nColumns;
	}

	// Measures the construct as a single line, gap by gap: a gap the source writes on one
	// line keeps its width, since only a gap holding a line break is ever rewritten, and one
	// holding a line break is measured at the width joining it writes. Returns false when a
	// gap has no inline spelling: a line break the standard does not settle, a comment, or
	// a directive.
	bool CFormattingAnalyzer::fp_MeasureJoinedWidth(umint _iFirstToken, umint _iLastToken, umint &o_nColumns) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		umint nColumns = 0;
		umint iPrevious = _iFirstToken;
		for (umint i = _iFirstToken; i <= _iLastToken; ++i)
		{
			// A block never fits on a line: it counts as wider than any line, its interior
			// is not measured, and what follows resumes behind its closing brace.
			if (i < m_iBlockEnd.f_GetLen() && m_iBlockEnd[i] <= _iLastToken)
			{
				nColumns += gc_nBlockWidth;
				i = m_iBlockEnd[i];
				iPrevious = i;

				continue;
			}

			auto const &Token = Tokens[i];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineSplice || Kind == ECodeTokenKind::mc_Whitespace)
				continue;

			// A line comment and a directive each end the line they stand on, so a construct
			// holding one is wider than any line rather than unmeasurable.
			if (Kind == ECodeTokenKind::mc_LineComment || (Kind == ECodeTokenKind::mc_Preprocessor && !m_bOpaqueDirective[i]))
			{
				nColumns += gc_nBlockWidth;

				continue;
			}

			if (Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor)
				return false;

			if (Token.m_bMultiLine)
				return false;

			if (i != _iFirstToken)
			{
				bool bNewline = false;
				bool bComment = false;
				umint nGap = 0;
				for (umint iGap = iPrevious + 1; iGap < i; ++iGap)
				{
					auto GapKind = Tokens[iGap].m_Kind;
					if (GapKind == ECodeTokenKind::mc_Newline || GapKind == ECodeTokenKind::mc_LineSplice)
						bNewline = true;
					else if (GapKind == ECodeTokenKind::mc_Whitespace)
						nGap += fp_GetTokenColumns(Tokens[iGap]);
					else if (GapKind == ECodeTokenKind::mc_LineComment || (GapKind == ECodeTokenKind::mc_Preprocessor && !m_bOpaqueDirective[iGap]))
						bComment = true;
					else
						return false;
				}

				// A string continued on a line of its own inside a bracket keeps that line, as a
				// line comment ends the line in front of it, so the bracket around it opens.
				if (bNewline && !bComment && m_TokenDepth[i] > m_TokenDepth[_iFirstToken] && fp_ContinuesLiteral(i) >= 0)
				{
					nColumns += gc_nBlockWidth + fp_GetTokenColumns(Token);
					iPrevious = i;

					continue;
				}

				// Two closers of nested template argument lists are written as one '>>'
				// whatever the source has between them.
				bool bClosers = m_Structure.f_IsAngleBracket(iPrevious) && m_Structure.f_IsAngleBracket(i)
					&& m_Tokens.f_IsText(Tokens[iPrevious], ">") && m_Tokens.f_IsText(Token, ">")
				;
				// A gap the standard settles is measured as the spacing rule writes it, so a
				// line measured to fit still fits once its gaps are respaced.
				if (bClosers || bComment)
					nColumns += 0;
				else
				{
					// A gap on one line is measured at the width the spacing rules leave it,
					// which is its own only where the standard settles nothing.
					auto Spacing = fp_GetInlineSpacing(iPrevious, i);
					if (Spacing == ECodeSpacing::mc_Preserve)
					{
						if (bNewline)
							return false;

						nColumns += nGap;
					}
					else
						nColumns += Spacing == ECodeSpacing::mc_Space;
				}
			}

			nColumns += fp_GetTokenColumns(Token);
			iPrevious = i;
		}

		o_nColumns = nColumns;

		return true;
	}

	// Replaces every gap inside the construct that holds a line break with its canonical
	// inline separator. Gaps already on one line keep the spacing the other rules govern.
	bool CFormattingAnalyzer::fp_TryJoin(umint _iFirstToken, umint _iLastToken, umint _iStartColumn)
	{
		umint nColumns = 0;
		if (!fp_MeasureJoinedWidth(_iFirstToken, _iLastToken, nColumns) || nColumns >= gc_nBlockWidth)
			return false;

		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		if (nMaxColumns && _iStartColumn + nColumns > nMaxColumns)
			return false;

		// Only the edits this join emits belong to its candidate. Splitting shares the rule
		// name, and dropping a split for an overlong line would undo the very fix for it.
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto iFirstEdit = m_Edits.f_GetLen();
		umint iPrevious = TCLimitsInt<umint>::mc_Max;
		for (umint i = _iFirstToken; i <= _iLastToken; ++i)
		{
			auto Kind = Tokens[i].m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineSplice)
				continue;

			if (iPrevious != TCLimitsInt<umint>::mc_Max && iPrevious + 1 != i)
			{
				bool bNewline = false;
				for (umint iGap = iPrevious + 1; iGap < i; ++iGap)
					bNewline |= Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline || Tokens[iGap].m_Kind == ECodeTokenKind::mc_LineSplice;

				if (bNewline)
				{
					// Written the way it was measured, the spacing rules' spelling included.
					auto Spacing = fp_GetInlineSpacing(iPrevious, i);
					auto iStart = Tokens[iPrevious].f_GetEnd();
					fp_AddEdit("line-break", iStart, Tokens[i].m_iOffset - iStart, Spacing == ECodeSpacing::mc_Space ? " " : CStr(), "the construct fits on one line");
				}
			}

			iPrevious = i;
		}

		auto &Candidate = m_JoinCandidates.f_Insert();
		Candidate.m_iOffset = Tokens[_iFirstToken].m_iOffset;
		for (auto i = iFirstEdit; i < m_Edits.f_GetLen(); ++i)
			Candidate.m_Edits.f_Insert(i);

		return true;
	}

}

namespace
{
	// Orders the collected edits and resolves the overlaps independent rules can produce,
	// for example a blank line inside a removed run that also carries trailing whitespace.
	void CFormattingAnalyzer::fp_BuildPlan(TCVector<CCodeFormattingEdit> &o_Edits, TCVector<umint> &o_Sources) const
	{
		o_Edits.f_Clear();
		o_Sources.f_Clear();

		TCVector<umint> Order;
		for (umint i = 0; i < m_Edits.f_GetLen(); ++i)
		{
			if (!m_bEditDropped[i])
				Order.f_Insert(i);
		}

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

			o_Edits.f_Insert(Edit);
			o_Sources.f_Insert(iEdit);
			if (!Edit.m_nLength)
			{
				iLastInsertion = Edit.m_iOffset;
				bHasInsertion = true;
			}

			iCovered = Edit.f_GetEnd();
		}
	}

	// Joining is decided per construct, so two constructs that end up on the same line can
	// each fit and still overflow together. The plan is applied and remeasured, and every
	// join landing on an overlong line is dropped, until no join makes a line too long.
	void CFormattingAnalyzer::fp_LimitJoinedLines()
	{
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		if (!nMaxColumns || m_JoinCandidates.f_IsEmpty())
			return;

		for (umint iPass = 0; iPass < 8; ++iPass)
		{
			TCVector<CCodeFormattingEdit> Plan;
			TCVector<umint> Sources;
			fp_BuildPlan(Plan, Sources);
			auto Formatted = fg_ApplyCodeFormattingEdits(m_Request.m_Source, Plan);
			CTextLineMap FormattedLines(Formatted);

			bool bDropped = false;
			umint iPlan = 0;
			aint nDelta = 0;
			for (auto &Candidate : m_JoinCandidates)
			{
				if (Candidate.m_bDropped || Candidate.m_Edits.f_IsEmpty())
					continue;

				while (iPlan < Plan.f_GetLen() && Plan[iPlan].f_GetEnd() <= Candidate.m_iOffset)
				{
					nDelta += aint(Plan[iPlan].m_Replacement.f_GetLen()) - aint(Plan[iPlan].m_nLength);
					++iPlan;
				}

				auto iLine = FormattedLines.f_FindLine(umint(aint(Candidate.m_iOffset) + nDelta));
				umint nColumns = 0;
				auto iStart = FormattedLines.f_GetLineStart(iLine);
				bool bMeasured = fg_MeasureTextColumns(Formatted.f_GetStr() + iStart, FormattedLines.f_GetLine(iLine).m_nLength, m_Request.m_Settings.m_nTabWidth, nColumns);
				if (bMeasured && nColumns <= nMaxColumns)
					continue;

				Candidate.m_bDropped = true;
				bDropped = true;
				for (auto iEdit : Candidate.m_Edits)
					m_bEditDropped[iEdit] = 1;
			}

			if (!bDropped)
				return;
		}

		// The passes did not settle, so no construct is joined rather than risking a long line.
		for (auto &Candidate : m_JoinCandidates)
		{
			for (auto iEdit : Candidate.m_Edits)
				m_bEditDropped[iEdit] = 1;
		}
	}
}

namespace
{
	CStr CFormattingAnalyzer::fp_MakeIndent(umint _nColumns) const
	{
		auto const &Settings = m_Request.m_Settings;
		CStr Indent;
		if (!Settings.m_bIndentWithTabs)
		{
			for (umint i = 0; i < _nColumns; ++i)
				Indent += " ";

			return Indent;
		}

		for (umint i = 0; i < _nColumns / Settings.m_nTabWidth; ++i)
			Indent += "\t";

		for (umint i = 0; i < _nColumns % Settings.m_nTabWidth; ++i)
			Indent += " ";

		return Indent;
	}

	// A statement's own indentation is where its first token already sits. Splitting places
	// new lines relative to that, so no separate model of scope depth is needed.
	// The indentation a statement starting at the token is laid out against: the one the
	// layout gave it, or else the column the source wrote it at.
	umint CFormattingAnalyzer::fp_GetStatementIndent(umint _iToken) const
	{
		if (fp_IsBreakGap(_iToken))
			return m_GapIndent[_iToken];

		return fp_GetColumn(m_Tokens.f_GetTokens()[_iToken].m_iOffset) - 1;
	}

	// True when the range holds nothing that fixes its own line structure.
	bool CFormattingAnalyzer::fp_IsRangeJoinable(umint _iNode, umint _iFirst, umint _iLast) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			auto Kind = Tokens[i].m_Kind;
			if (Kind == ECodeTokenKind::mc_BlockComment || (Kind == ECodeTokenKind::mc_Preprocessor && m_bOpaqueDirective[i]))
				return false;

			// A line break is layout; only a token whose own text spans lines is fixed. A
			// line comment ends its line and so forbids joining across it, which measuring
			// it as wider than any line takes care of, while the lines around it are laid
			// out as usual. A transparent directive ends its line the same way.
			bool bLayout = Kind == ECodeTokenKind::mc_Newline
				|| Kind == ECodeTokenKind::mc_LineSplice
				|| Kind == ECodeTokenKind::mc_Whitespace
				|| Kind == ECodeTokenKind::mc_LineComment
				|| Kind == ECodeTokenKind::mc_Preprocessor
			;
			if (bLayout)
				continue;

			if (Tokens[i].m_bMultiLine)
				return false;
		}

		// A braced initializer written across lines, or anything else that fixes its own
		// lines, makes the construct around it unable to render inline. The node flags are
		// propagated from descendants, so the direct children answer for all. A block is
		// the exception: a lambda body inside a group opens on a line of its own, and the
		// construct around it is laid out as a split one.
		for (auto iChild : m_Structure.f_GetNodes()[_iNode].m_Children)
		{
			auto const &Child = m_Structure.f_GetNodes()[iChild];
			if (Child.m_iFirstToken < _iFirst || Child.m_iLastToken > _iLast)
				continue;

			bool bFixed = Child.m_Kind == ECodeNodeKind::mc_Block
				|| Child.m_Kind == ECodeNodeKind::mc_Unsupported
				|| fp_HasOpaqueDirective(Child.m_iFirstToken, Child.m_iLastToken)
				|| Child.m_bHasMultiLineToken
				|| Child.m_bHasMultiLineBrace
			;
			if (bFixed)
				return false;
		}

		return true;
	}

	bool CFormattingAnalyzer::fp_FitsInline(umint _iFirst, umint _iLast, umint _iIndent) const
	{
		umint nColumns = 0;
		if (!fp_MeasureJoinedWidth(_iFirst, _iLast, nColumns) || nColumns >= gc_nBlockWidth)
			return false;

		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;

		return !nMaxColumns || _iIndent + nColumns <= nMaxColumns;
	}

	// Starts a new line before the token, at the given indentation.
	void CFormattingAnalyzer::fp_BreakBefore(umint _iToken, umint _iIndent)
	{
		m_GapState[_iToken] = uint8(EGap::mc_Break);
		m_GapIndent[_iToken] = _iIndent;
	}

	void CFormattingAnalyzer::fp_BreakAfter(umint _iToken, umint _iIndent)
	{
		auto iNext = fp_NextCode(_iToken);
		if (iNext >= 0)
			fp_BreakBefore(umint(iNext), _iIndent);
	}

	void CFormattingAnalyzer::fp_OwnLineBefore(umint _iToken, umint _iIndent)
	{
		m_GapState[_iToken] = uint8(EGap::mc_OwnLine);
		m_GapIndent[_iToken] = _iIndent;
	}

	void CFormattingAnalyzer::fp_IndentBefore(umint _iToken, umint _iIndent)
	{
		m_GapState[_iToken] = uint8(EGap::mc_Indent);
		m_GapIndent[_iToken] = _iIndent;
	}

	// True when the layout has decided that the token starts a line.
	bool CFormattingAnalyzer::fp_IsBreakGap(umint _iToken) const
	{
		if (_iToken >= m_GapState.f_GetLen())
			return false;

		auto State = EGap(m_GapState[_iToken]);

		return State == EGap::mc_Break || State == EGap::mc_OwnLine || State == EGap::mc_Indent;
	}

	// The range is written on one line: every gap inside it takes its inline spelling.
	void CFormattingAnalyzer::fp_MarkInline(umint _iFirst, umint _iLast)
	{
		for (umint i = _iFirst + 1; i <= _iLast; ++i)
			m_GapState[i] = uint8(EGap::mc_Inline);
	}

	// Writes the layout decisions out as edits. A gap that keeps what the source has, one an
	// opaque directive stands in, and an inline gap the source already writes on one line
	// are left to the other rules. A gap holding a comment or a transparent directive keeps
	// its lines, and those lines their own text, so only the indentation the token starts
	// its line at is written.
	void CFormattingAnalyzer::fp_EmitLayout()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			// Only a code token owns the gap in front of it; a range's marks also land on
			// the trivia inside it.
			switch (Tokens[i].m_Kind)
			{
				case ECodeTokenKind::mc_ByteOrderMark:
				case ECodeTokenKind::mc_Whitespace:
				case ECodeTokenKind::mc_Newline:
				case ECodeTokenKind::mc_LineSplice:
				case ECodeTokenKind::mc_LineComment:
				case ECodeTokenKind::mc_BlockComment:
				case ECodeTokenKind::mc_Preprocessor:
					continue;
				default: break;
			}

			auto State = EGap(m_GapState[i]);
			if (State == EGap::mc_Keep)
				continue;

			auto iPrevious = fp_PreviousCode(i);
			if (iPrevious < 0)
				continue;

			bool bNewline = false;
			bool bKeepLines = false;
			bool bOwned = false;
			for (umint iGap = umint(iPrevious) + 1; iGap < i; ++iGap)
			{
				auto Kind = Tokens[iGap].m_Kind;
				if (Kind == ECodeTokenKind::mc_Newline)
					bNewline = true;
				else if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
					bKeepLines = true;
				else if (Kind == ECodeTokenKind::mc_Preprocessor)
				{
					if (m_bOpaqueDirective[iGap])
						bOwned = true;
					else
						bKeepLines = true;
				}
				else if (Kind != ECodeTokenKind::mc_Whitespace)
					bOwned = true;
			}

			if (bOwned)
				continue;

			auto iStart = Tokens[umint(iPrevious)].f_GetEnd();
			auto nLength = Tokens[i].m_iOffset - iStart;
			// A gap holding a comment or a directive keeps its lines, and one keeping its
			// lines keeps any blank lines too; only the indentation of the line the token
			// starts moves, so the edit covers that indentation alone.
			auto fKeepLines = [&]
				{
					auto pGap = m_Request.m_Source.f_GetStr() + iStart;
					umint nKeep = nLength;
					while (nKeep && pGap[nKeep - 1] != '\n' && pGap[nKeep - 1] != '\r')
						--nKeep;

					// A comment on the token's own line stays in front of it, so only the
					// blanks between the line break and that comment are the indentation.
					umint nIndent = 0;
					while (nKeep + nIndent < nLength && (pGap[nKeep + nIndent] == ' ' || pGap[nKeep + nIndent] == '\t'))
						++nIndent;

					iStart += nKeep;
					nLength = nIndent;

					return fp_MakeIndent(m_GapIndent[i]);
				}
			;
			CStr Replacement;
			CStr Explanation;
			if (State == EGap::mc_Break || State == EGap::mc_OwnLine)
			{
				if (bKeepLines && !bNewline)
					continue;

				Replacement = bKeepLines ? fKeepLines() : (m_bBlankBefore[i] ? Ending + Ending : Ending) + fp_MakeIndent(m_GapIndent[i]);
				Explanation = State == EGap::mc_Break ? "a split construct puts this on its own line" : "a block's braces and each of its statements take a line of their own";
			}
			else if (State == EGap::mc_Indent)
			{
				if (!bNewline)
					continue;

				Replacement = fKeepLines();
				Explanation = "the body's lines move with its brace";
			}
			else
			{
				if (!bNewline || bKeepLines)
					continue;

				// Joined the way it was measured: the operator a spacing rule writes apart,
				// 'cA<t_C> || cB<t_C>', is apart here too.
				auto Spacing = fp_GetInlineSpacing(umint(iPrevious), i);
				if (Spacing == ECodeSpacing::mc_Preserve)
					continue;

				Replacement = Spacing == ECodeSpacing::mc_Space ? " " : "";
				Explanation = "the construct fits on one line";
			}

			if (Replacement == CStr(m_Request.m_Source.f_GetStr() + iStart, nLength))
				continue;

			fp_AddEdit("line-break", iStart, nLength, Replacement, Explanation);
		}

		// A comment standing on a line of its own belongs to the block around it, and its
		// line is written at the depth that block moved to.
		auto const &Source = m_Request.m_Source;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (!m_bCommentMoved[i])
				continue;

			auto iLine = m_Lines.f_FindLine(Tokens[i].m_iOffset);
			if (m_bProtectedStart[iLine])
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto nLength = Tokens[i].m_iOffset - iStart;
			CStr Replacement = fp_MakeIndent(m_CommentIndent[i]);
			if (Replacement == CStr(Source.f_GetStr() + iStart, nLength))
				continue;

			fp_AddEdit("line-break", iStart, nLength, Replacement, "the body's lines move with its brace");
		}
	}

	bool CFormattingAnalyzer::fp_LayoutGroup(umint _iNode, umint _iIndent, bool _bBreakBefore)
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_Kind != ECodeNodeKind::mc_Group)
			return false;

		// An empty group has nothing to put on its own line. Reporting that keeps the
		// statement from being treated as split, which would strand its terminator on a
		// line of its own without making anything fit.
		auto iInner = fp_NextCode(Node.m_iFirstToken);
		if (iInner < 0 || umint(iInner) == Node.m_iLastToken)
			return false;

		if (_bBreakBefore)
			fp_BreakBefore(Node.m_iFirstToken, _iIndent);

		fp_BreakAfter(Node.m_iFirstToken, _iIndent + m_Request.m_Settings.m_nTabWidth);
		for (auto iSplit : Node.m_SplitPoints)
			fp_BreakBefore(iSplit, _iIndent + m_Request.m_Settings.m_nTabWidth);

		fp_BreakBefore(Node.m_iLastToken, _iIndent);
		fp_LayoutElements(_iNode, _iIndent + m_Request.m_Settings.m_nTabWidth);

		return true;
	}

	// Each element of a split group that still does not fit has its own groups split in turn.
	void CFormattingAnalyzer::fp_LayoutElements(umint _iNode, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		umint iElement = Node.m_iFirstToken + 1;
		umint iSplit = 0;
		while (iElement <= Node.m_iLastToken)
		{
			auto iEnd = iSplit < Node.m_SplitPoints.f_GetLen() ? Node.m_SplitPoints[iSplit] : Node.m_iLastToken;
			if (iEnd > iElement)
			{
				// A range ends on a code token: a comment trailing the element belongs to
				// the gap in front of the separator, and is no part of the element's width.
				auto iLast = fp_PreviousCode(iEnd);
				auto iStart = fp_NextCode(iElement - 1);
				if (iStart >= 0 && iLast >= 0 && umint(iStart) <= umint(iLast))
					fp_LayoutRange(_iNode, umint(iStart), umint(iLast), _iIndent, false, false);
			}

			if (iSplit >= Node.m_SplitPoints.f_GetLen())
				break;

			iElement = Node.m_SplitPoints[iSplit];
			++iSplit;
		}
	}

	// Writes a declaration's specifiers in one order where the sources have two: 'static'
	// in front of 'constexpr'. Only what a run of specifiers is made of may stand between
	// the two, with nothing but spaces and line breaks around it, so what moves is a
	// specifier among its like and never a word of something else.
	void CFormattingAnalyzer::fp_ConvertSpecifiers()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		constexpr ch8 const *c_pSpecifiers[] =
			{
				"inline", "constinit", "extern", "virtual", "friend", "thread_local", "mutable", "explicit"
			}
		;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Identifier || !m_Tokens.f_IsText(Tokens[i], "constexpr"))
				continue;

			umint iStatic = TCLimitsInt<umint>::mc_Max;
			for (auto iNext = fp_NextCode(i); iNext >= 0; iNext = fp_NextCode(umint(iNext)))
			{
				if (m_Tokens.f_IsText(Tokens[umint(iNext)], "static"))
				{
					iStatic = umint(iNext);

					break;
				}

				bool bSpecifier = m_Tokens.f_HasRole(Tokens[umint(iNext)], ECodeNameRole::mc_SpecifierMacro);
				for (auto pSpecifier : c_pSpecifiers)
					bSpecifier |= m_Tokens.f_IsText(Tokens[umint(iNext)], pSpecifier);

				if (!bSpecifier)
					break;
			}

			if (iStatic == TCLimitsInt<umint>::mc_Max)
				continue;

			auto iBehind = fp_NextCode(iStatic);
			if (iBehind < 0)
				continue;

			bool bPlain = true;
			for (umint iGap = i; iGap < umint(iBehind) && bPlain; ++iGap)
				bPlain = fg_IsCodeToken(Tokens[iGap]) || Tokens[iGap].m_Kind == ECodeTokenKind::mc_Whitespace || Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

			if (!bPlain)
				continue;

			auto iRemove = Tokens[iStatic].m_iOffset;
			auto nRemove = Tokens[umint(iBehind)].m_iOffset - iRemove;
			auto iInsert = Tokens[i].m_iOffset;
			auto nInsert = Tokens[i].m_nLength;
			if (fp_IsDisabled(iInsert, nInsert) || !fp_IsSelected(iInsert, nInsert) || fp_IsDisabled(iRemove, nRemove) || !fp_IsSelected(iRemove, nRemove))
				continue;

			auto &Insert = m_Structural.f_Insert();
			Insert.m_iOffset = iInsert;
			Insert.m_nLength = nInsert;
			Insert.m_Replacement = "static constexpr";
			Insert.m_Rule = "specifier-order";
			auto &Remove = m_Structural.f_Insert();
			Remove.m_iOffset = iRemove;
			Remove.m_nLength = nRemove;
			Remove.m_Rule = "specifier-order";
		}
	}

	// Takes out a statement terminator that ends nothing: the second ';' of 'f_Call();;'
	// is an empty statement of its own. Only one standing alone as a statement goes, never
	// a separator of 'for (;;)', and only where nothing but spaces and line breaks stand
	// between it and the terminator in front of it, so no comment or directive moves.
	void CFormattingAnalyzer::fp_ConvertEmptyStatements()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto const &Node : m_Structure.f_GetNodes())
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Statement || Node.m_iFirstToken != Node.m_iLastToken || !m_Tokens.f_IsText(Tokens[Node.m_iFirstToken], ";"))
				continue;

			auto iEmpty = Node.m_iFirstToken;
			auto iBefore = fp_PreviousCode(iEmpty);
			if (iBefore < 0 || !m_Tokens.f_IsText(Tokens[umint(iBefore)], ";"))
				continue;

			bool bPlain = true;
			for (auto iGap = umint(iBefore) + 1; iGap < iEmpty && bPlain; ++iGap)
				bPlain = Tokens[iGap].m_Kind == ECodeTokenKind::mc_Whitespace || Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

			if (!bPlain)
				continue;

			auto iRemove = Tokens[umint(iBefore)].f_GetEnd();
			auto nRemove = Tokens[iEmpty].f_GetEnd() - iRemove;
			if (fp_IsDisabled(iRemove, nRemove) || !fp_IsSelected(iRemove, nRemove))
				continue;

			auto &Remove = m_Structural.f_Insert();
			Remove.m_iOffset = iRemove;
			Remove.m_nLength = nRemove;
			Remove.m_Rule = "empty-statement";
		}
	}

	// Gives each enum's enumerators their commas in front of them wherever the layout could
	// not move one there by itself.
	void CFormattingAnalyzer::fp_ConvertEnumCommas()
	{
		if (!m_Structure.f_IsComplete())
			return;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		for (auto const &Node : Nodes)
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Block || Node.m_iParent >= Nodes.f_GetLen() || Nodes[Node.m_iParent].m_Kind != ECodeNodeKind::mc_Statement)
				continue;

			if (m_Tokens.f_IsText(Tokens[fp_SkipTemplateHeader(Nodes[Node.m_iParent].m_iFirstToken)], "enum"))
				fp_ConvertEnumBodyCommas(Node);
		}

		// A comment behind a list's comma describes the element in front of it, so the comma
		// moves in front of the element behind it instead, as the layout puts it: 'a, // A'
		// over 'b' is 'a // A' over ', b'.
		// A braced list written with its commas behind its elements, or holding a directive,
		// keeps them there.
		auto fKeepsCommas = [&](CCodeNode const &_List)
			{
				if (_List.m_Bracket != ECodeBracket::mc_Brace)
					return false;

				for (auto i = _List.m_iFirstToken; i < _List.m_iLastToken; ++i)
				{
					if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
						return true;
				}

				for (auto iComma : _List.m_SplitPoints)
				{
					auto iNext = fp_NextCode(iComma);
					if (!m_Tokens.f_IsText(Tokens[iComma], ",") || iNext < 0 || umint(iNext) >= _List.m_iLastToken || !fp_IsLastOnLine(iComma))
						continue;

					bool bComment = false;
					for (auto i = iComma + 1; i < umint(iNext) && !bComment; ++i)
						bComment = Tokens[i].m_Kind == ECodeTokenKind::mc_LineComment;

					if (!bComment)
						return true;
				}

				return false;
			}
		;
		for (auto const &Node : Nodes)
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group || fKeepsCommas(Node))
				continue;

			for (auto iComma : Node.m_SplitPoints)
			{
				if (!m_Tokens.f_IsText(Tokens[iComma], ","))
					continue;

				auto iComment = iComma + 1;
				while (iComment < Tokens.f_GetLen() && Tokens[iComment].m_Kind == ECodeTokenKind::mc_Whitespace)
					++iComment;

				if (iComment >= Tokens.f_GetLen() || Tokens[iComment].m_Kind != ECodeTokenKind::mc_LineComment)
					continue;

				auto iNext = fp_NextCode(iComma);
				if (iNext < 0 || umint(iNext) >= Node.m_iLastToken)
					continue;

				bool bPlain = true;
				for (auto i = iComment + 1; i < umint(iNext) && bPlain; ++i)
				{
					auto Kind = Tokens[i].m_Kind;
					bPlain = Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineComment;
				}

				auto iBefore = fp_PreviousCode(iComma);
				if (!bPlain || iBefore < 0)
					continue;

				auto iRemove = Tokens[iComma].m_iOffset;
				bool bTight = true;
				for (auto i = umint(iBefore) + 1; i < iComma && bTight; ++i)
					bTight = Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace;

				if (bTight)
					iRemove = Tokens[umint(iBefore)].f_GetEnd();

				auto nRemove = Tokens[iComma].f_GetEnd() - iRemove;
				auto iInsert = Tokens[umint(iNext)].m_iOffset;
				if (fp_IsDisabled(iRemove, nRemove) || !fp_IsSelected(iRemove, nRemove) || fp_IsDisabled(iInsert, 0) || !fp_IsSelected(iInsert, 0))
					continue;

				auto &Remove = m_Structural.f_Insert();
				Remove.m_iOffset = iRemove;
				Remove.m_nLength = nRemove;
				Remove.m_Rule = "list-comma";

				auto &Insert = m_Structural.f_Insert();
				Insert.m_iOffset = iInsert;
				Insert.m_nLength = 0;
				Insert.m_Replacement = ", ";
				Insert.m_Rule = "list-comma";
			}
		}
	}

	// A comma separated from the enumerator behind it by a comment or a conditional's directive,
	// and the one behind the last enumerator, goes, and one is put in front of each enumerator
	// that has another in front of it in every configuration it is compiled in. Every
	// configuration then spells the enumerators it did, and the layout moves the commas that
	// only blanks separate from their enumerators. An enum holding another directive, or an
	// enumerator that is the first in some configurations only, keeps its commas where they are.
	void CFormattingAnalyzer::fp_ConvertEnumBodyCommas(CCodeNode const &_Body)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		struct CBranch
		{
			umint m_iGroup;
			umint m_iBranch;
		};
		struct CEnumerator
		{
			umint m_iFirst;
			TCVector<CBranch> m_Path;
		};
		TCVector<CBranch> Path;
		TCVector<CEnumerator> Enumerators;
		TCVector<umint> Commas;
		umint nGroups = 0;
		bool bOpen = false;
		bool bEndedByDirective = false;
		aint iFirstCode = fp_NextCode(_Body.m_iFirstToken);
		if (iFirstCode < 0 || umint(iFirstCode) >= _Body.m_iLastToken)
			return;

		// An enumerator in front of another is always compiled with it where every branch it
		// stands in is one the other stands in too, and never where the two stand in different
		// branches of one conditional.
		auto fRelation = [](TCVector<CBranch> const &_Before, TCVector<CBranch> const &_After) -> int
			{
				for (umint iLevel = 0; iLevel < _Before.f_GetLen(); ++iLevel)
				{
					if (iLevel >= _After.f_GetLen() || _Before[iLevel].m_iGroup != _After[iLevel].m_iGroup)
						return 0;

					if (_Before[iLevel].m_iBranch != _After[iLevel].m_iBranch)
						return -1;
				}

				return 1;
			}
		;
		auto nLevel = m_TokenDepth[umint(iFirstCode)];
		for (auto i = _Body.m_iFirstToken + 1; i < _Body.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind == ECodeTokenKind::mc_Preprocessor)
			{
				auto Keyword = fp_GetDirectiveKeyword(i);
				if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
					Path.f_Insert(CBranch{nGroups++, 0});
				else if (Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef" || Keyword == "else")
				{
					if (Path.f_IsEmpty())
						return;

					++Path.f_GetLast().m_iBranch;
				}
				else if (Keyword == "endif")
				{
					if (Path.f_IsEmpty())
						return;

					Path.f_Remove(Path.f_GetLen() - 1);
				}
				else
					return;

				bEndedByDirective |= bOpen;
				bOpen = false;

				continue;
			}

			if (!fg_IsCodeToken(Token))
				continue;

			if (m_TokenDepth[i] == nLevel && m_Tokens.f_IsText(Token, ","))
			{
				if (!bOpen && !bEndedByDirective)
					return;

				Commas.f_Insert(i);
				bOpen = false;
				bEndedByDirective = false;

				continue;
			}

			if (bOpen)
				continue;

			if (m_TokenDepth[i] != nLevel || Token.m_Kind != ECodeTokenKind::mc_Identifier)
				return;

			// An enumerator a directive ended without a comma is complete only where it is the
			// last of its branch, with the next one in another branch of the same conditional.
			if (bEndedByDirective && fRelation(Enumerators.f_GetLast().m_Path, Path) != -1)
				return;

			bEndedByDirective = false;

			Enumerators.f_Insert(CEnumerator{i, Path});
			bOpen = true;
		}

		if (!Path.f_IsEmpty() || Enumerators.f_IsEmpty())
			return;

		auto fPlain = [&](umint _iFirst, umint _iEnd)
			{
				for (auto i = _iFirst; i < _iEnd; ++i)
				{
					if (Tokens[i].m_Kind != ECodeTokenKind::mc_Whitespace && Tokens[i].m_Kind != ECodeTokenKind::mc_Newline)
						return false;
				}

				return true;
			}
		;
		TCVector<CCodeFormattingEdit> Edits;
		for (umint iEnumerator = 0; iEnumerator < Enumerators.f_GetLen(); ++iEnumerator)
		{
			auto const &Enumerator = Enumerators[iEnumerator];
			bool bAlways = false;
			bool bNever = true;
			for (umint iBefore = 0; iBefore < iEnumerator; ++iBefore)
			{
				auto Relation = fRelation(Enumerators[iBefore].m_Path, Enumerator.m_Path);
				bAlways |= Relation == 1;
				bNever &= Relation == -1;
			}

			if (!bAlways && !bNever)
				return;

			auto iPrevious = fp_PreviousCode(Enumerator.m_iFirst);
			bool bCommaInFront = iPrevious >= 0
				&& m_Tokens.f_IsText(Tokens[umint(iPrevious)], ",")
				&& fPlain(umint(iPrevious) + 1, Enumerator.m_iFirst)
			;
			if (bCommaInFront && !bAlways)
				return;

			if (bAlways && !bCommaInFront)
			{
				auto &Insert = Edits.f_Insert();
				Insert.m_iOffset = Tokens[Enumerator.m_iFirst].m_iOffset;
				Insert.m_nLength = 0;
				Insert.m_Replacement = ", ";
				Insert.m_Rule = "enum-comma";
			}
		}

		for (auto iComma : Commas)
		{
			auto iNext = fp_NextCode(iComma);
			if (iNext >= 0 && umint(iNext) < _Body.m_iLastToken && fPlain(iComma + 1, umint(iNext)))
				continue;

			// The blanks in front of the comma go with it where it trails its enumerator's line.
			auto iRemove = Tokens[iComma].m_iOffset;
			auto iBefore = fp_PreviousCode(iComma);
			if (iBefore >= 0)
			{
				bool bPlain = true;
				for (auto iGap = umint(iBefore) + 1; iGap < iComma && bPlain; ++iGap)
					bPlain = Tokens[iGap].m_Kind == ECodeTokenKind::mc_Whitespace;

				if (bPlain)
					iRemove = Tokens[umint(iBefore)].f_GetEnd();
			}

			auto &Remove = Edits.f_Insert();
			Remove.m_iOffset = iRemove;
			Remove.m_nLength = Tokens[iComma].f_GetEnd() - iRemove;
			Remove.m_Rule = "enum-comma";
		}

		for (auto const &Edit : Edits)
		{
			if (fp_IsDisabled(Edit.m_iOffset, Edit.m_nLength) || !fp_IsSelected(Edit.m_iOffset, Edit.m_nLength))
				return;
		}

		for (auto &Edit : Edits)
			m_Structural.f_Insert(fg_Move(Edit));
	}

	// Moves a qualifier written in front of its type behind it: 'const int &_Value' becomes
	// 'int const &_Value'. Where the qualifier stands is told by what is in front of it: a
	// separator, an opening marker or a specifier has no type for it to follow, so the type
	// is what comes next. Behind a name, a template argument list or a parenthesis it can
	// as well be the qualifier of what it follows, 'CFoo const _Value' and 'f_Get() const',
	// and is only moved where a type and then a declarator or a name follow it, which
	// neither of those readings has. The type has to be spelled out by tokens this can
	// follow to its end, and with nothing but spaces and line breaks between them, which
	// the layout would take out before the next pass; anything else stays.
	void CFormattingAnalyzer::fp_ConvertQualifiers()
	{
		if (!m_Structure.f_IsComplete())
			return;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		TCVector<umint> GroupEnd;
		GroupEnd.f_SetLen(Tokens.f_GetLen());
		for (auto &iEnd : GroupEnd)
			iEnd = TCLimitsInt<umint>::mc_Max;

		for (auto const &Node : Nodes)
		{
			if (Node.m_Kind == ECodeNodeKind::mc_Group)
				GroupEnd[Node.m_iFirstToken] = Node.m_iLastToken;
		}

		constexpr ch8 const *c_pSpecifiers[] =
			{
				"static", "inline", "constexpr", "consteval", "constinit", "extern", "virtual", "friend", "thread_local", "typedef"
				, "mutable", "explicit", "register"
			}
		;
		constexpr ch8 const *c_pOpeners[] =
			{
				"(", ",", "<", ";", "{", "}", "->", "=", "]", ":", "operator", "new"
			}
		;
		constexpr ch8 const *c_pFundamental[] =
			{
				"signed", "unsigned", "short", "long", "int", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "bool", "float", "double", "void"
			}
		;
		constexpr ch8 const *c_pElaborated[] =
			{
				"struct", "class", "union", "enum"
			}
		;
		// What a function's qualifier is followed by, and what an expression starts with:
		// names a type is never spelled with.
		constexpr ch8 const *c_pNoType[] =
			{
				"override", "final", "noexcept", "requires", "try", "throw", "operator", "new", "delete", "return", "co_return", "co_await"
				, "co_yield", "sizeof", "alignof", "template", "using", "namespace", "if", "for", "while", "switch", "do", "else", "case"
				, "default", "this", "nullptr", "true", "false"
			}
		;
		auto fIsAny = [&](umint _iToken, auto const &_pTexts)
			{
				for (auto pText : _pTexts)
				{
					if (m_Tokens.f_IsText(Tokens[_iToken], pText))
						return true;
				}

				return false;
			}
		;
		auto fIsQualifier = [&](umint _iToken)
			{
				return m_Tokens.f_IsText(Tokens[_iToken], "const") || m_Tokens.f_IsText(Tokens[_iToken], "volatile");
			}
		;

		// Follows a type to its last token: a name, qualified and with template arguments
		// where it has them, a fundamental type's words, 'auto', or 'decltype' and its operand,
		// behind 'typename' or the keyword of an elaborated name where one stands.
		constexpr umint c_NoType = TCLimitsInt<umint>::mc_Max;
		auto fFollowType = [&](umint _iFirst) -> umint
			{
				umint iEnd = c_NoType;
				auto j = _iFirst;
				if (m_Tokens.f_IsText(Tokens[j], "typename") || fIsAny(j, c_pElaborated))
				{
					auto iNext = fp_NextCode(j);
					if (iNext < 0)
						return c_NoType;

					j = umint(iNext);
				}

				if (m_Tokens.f_IsText(Tokens[j], "auto"))
					iEnd = j;
				else if (m_Tokens.f_IsText(Tokens[j], "decltype"))
				{
					auto iOpen = fp_NextCode(j);
					if (iOpen < 0 || !m_Tokens.f_IsText(Tokens[umint(iOpen)], "(") || GroupEnd[umint(iOpen)] == TCLimitsInt<umint>::mc_Max)
						return c_NoType;

					iEnd = GroupEnd[umint(iOpen)];
				}
				else if (fIsAny(j, c_pFundamental))
				{
					iEnd = j;
					for (auto iNext = fp_NextCode(iEnd); iNext >= 0 && fIsAny(umint(iNext), c_pFundamental); iNext = fp_NextCode(iEnd))
						iEnd = umint(iNext);
				}
				else
				{
					// A name, qualified and with template arguments where it has them.
					if (m_Tokens.f_IsText(Tokens[j], "::"))
					{
						auto iNext = fp_NextCode(j);
						if (iNext < 0)
							return c_NoType;

						j = umint(iNext);
					}

					bool bNamed = false;
					while (true)
					{
						bool bName = Tokens[j].m_Kind == ECodeTokenKind::mc_Identifier && !fIsQualifier(j) && !fIsAny(j, c_pNoType) && !fIsAny(j, c_pSpecifiers);
						if (!bName)
						{
							bNamed = false;

							break;
						}

						bNamed = true;
						iEnd = j;
						auto iNext = fp_NextCode(iEnd);
						if (iNext >= 0 && m_Tokens.f_IsText(Tokens[umint(iNext)], "<"))
						{
							// A '<' the structure did not resolve is as much a comparison.
							if (!m_Structure.f_IsAngleBracket(umint(iNext)) || GroupEnd[umint(iNext)] == TCLimitsInt<umint>::mc_Max)
							{
								bNamed = false;

								break;
							}

							iEnd = GroupEnd[umint(iNext)];
							iNext = fp_NextCode(iEnd);
						}

						if (iNext < 0 || !m_Tokens.f_IsText(Tokens[umint(iNext)], "::"))
							break;

						iNext = fp_NextCode(umint(iNext));
						if (iNext >= 0 && m_Tokens.f_IsText(Tokens[umint(iNext)], "template"))
							iNext = fp_NextCode(umint(iNext));

						if (iNext < 0)
						{
							bNamed = false;

							break;
						}

						j = umint(iNext);
					}

					if (!bNamed)
						return c_NoType;
				}

				return iEnd;
			}
		;

		// What a type is followed by where it declares something: a declarator, or a name.
		auto fDeclares = [&](aint _iToken)
			{
				if (_iToken < 0)
					return false;

				auto const &Token = Tokens[umint(_iToken)];

				return m_Tokens.f_IsText(Token, "*")
					|| m_Tokens.f_IsText(Token, "&")
					|| m_Tokens.f_IsText(Token, "&&")
					|| (Token.m_Kind == ECodeTokenKind::mc_Identifier && !fIsAny(umint(_iToken), c_pNoType))
				;
			}
		;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Identifier || !fIsQualifier(i))
				continue;

			// A run of qualifiers moves as one, from its first.
			auto iBefore = fp_PreviousCode(i);
			if (iBefore >= 0 && fIsQualifier(umint(iBefore)))
				continue;

			umint iRunLast = i;
			for (auto iNext = fp_NextCode(iRunLast); iNext >= 0 && fIsQualifier(umint(iNext)); iNext = fp_NextCode(iRunLast))
				iRunLast = umint(iNext);

			bool bLeads = iBefore < 0 || fIsAny(umint(iBefore), c_pOpeners) || fIsAny(umint(iBefore), c_pSpecifiers);
			if (!bLeads)
			{
				auto const &Before = Tokens[umint(iBefore)];
				bool bAmbiguous = Before.m_Kind == ECodeTokenKind::mc_Identifier
					|| m_Tokens.f_IsText(Before, ")")
					|| (m_Structure.f_IsAngleBracket(umint(iBefore)) && m_Tokens.f_IsText(Before, ">"))
				;
				if (!bAmbiguous)
					continue;
			}

			// Specifiers written behind the qualifier stay where they are.
			auto iType = fp_NextCode(iRunLast);
			while (iType >= 0 && fIsAny(umint(iType), c_pSpecifiers))
				iType = fp_NextCode(umint(iType));

			if (iType < 0)
				continue;

			auto iFirstBehind = umint(fp_NextCode(iRunLast));
			auto iEnd = fFollowType(umint(iType));
			if (iEnd == c_NoType)
				continue;

			auto iAfter = fp_NextCode(iEnd);
			if (iAfter >= 0 && fIsQualifier(umint(iAfter)))
				continue;

			// Only a declarator or a name behind the type says the qualifier led it.
			if (!bLeads && !fDeclares(iAfter))
				continue;

			// Behind its type the qualifier has a name in front of it, which is the place it
			// is least sure to have led from. Where what then follows would read as a type
			// led by it once more, a macro between the type and its declarator as in
			// 'const CFoo DFar *', the next pass would move it again, so it stays.
			if (iAfter >= 0)
			{
				auto iAgain = fFollowType(umint(iAfter));
				if (iAgain != c_NoType && fDeclares(fp_NextCode(iAgain)))
					continue;
			}

			bool bPlain = true;
			for (umint iGap = i; iGap <= iEnd && bPlain; ++iGap)
				bPlain = fg_IsCodeToken(Tokens[iGap]) || Tokens[iGap].m_Kind == ECodeTokenKind::mc_Whitespace || Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline;

			if (!bPlain)
				continue;

			auto iRunStart = Tokens[i].m_iOffset;
			auto nRun = Tokens[iFirstBehind].m_iOffset - iRunStart;
			auto iLastStart = Tokens[iEnd].m_iOffset;
			auto nLast = Tokens[iEnd].m_nLength;
			if (fp_IsDisabled(iRunStart, nRun) || !fp_IsSelected(iRunStart, nRun) || fp_IsDisabled(iLastStart, nLast) || !fp_IsSelected(iLastStart, nLast))
				continue;

			CStr Moved = m_Tokens.f_GetText(Tokens[iEnd]);
			for (auto iRun = aint(i); iRun >= 0 && umint(iRun) <= iRunLast; iRun = fp_NextCode(umint(iRun)))
			{
				Moved += " ";
				Moved += m_Tokens.f_GetText(Tokens[umint(iRun)]);
			}

			auto &Remove = m_Structural.f_Insert();
			Remove.m_iOffset = iRunStart;
			Remove.m_nLength = nRun;
			Remove.m_Rule = "east-qualifier";
			auto &Insert = m_Structural.f_Insert();
			Insert.m_iOffset = iLastStart;
			Insert.m_nLength = nLast;
			Insert.m_Replacement = Moved;
			Insert.m_Rule = "east-qualifier";
		}
	}

	// Moves a declaration's return type behind its parameter list when the name would not
	// otherwise fit. Converting alone is always eight columns longer, so it is only ever
	// worth doing together with putting the trailing type on its own line. The conversion is
	// recorded as text; the layout of the converted declaration is made on the result.
	bool CFormattingAnalyzer::fp_ConvertTrailingReturn(umint _iNode, umint _iDeclFirst, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		auto const &Tokens = m_Tokens.f_GetTokens();
		umint iParen = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Group && Nodes[iChild].m_Bracket == ECodeBracket::mc_Paren)
			{
				iParen = iChild;

				break;
			}
		}

		if (iParen == TCLimitsInt<umint>::mc_Max)
			return false;

		auto iOpen = Nodes[iParen].m_iFirstToken;
		auto iClose = Nodes[iParen].m_iLastToken;
		if (iOpen == Node.m_iFirstToken)
			return false;

		// A variable constructed with arguments has the shape of a declaration too; what the
		// parenthesis holds is what tells them apart.
		if (fg_HoldsArguments(m_Tokens, m_Structure, iParen))
			return false;

		auto iBeforeOpen = fp_PreviousCode(iOpen);
		if (iBeforeOpen < 0)
			return false;

		// Whether the name already fits in front of the parameter list decides, further
		// down, whether converting is worth anything when the trailing type on its own
		// line does not make the signature fit.
		bool bNameFits = fp_FitsInline(_iDeclFirst, umint(iBeforeOpen), _iIndent);

		// The declarator-id is the last name before the parameter list, extended backwards
		// only through '::'. A return type in front of it looks the same, so stopping at
		// the first name that is not qualified is what separates the two.
		auto fSkipAngleBackwards = [&](aint _iToken)
			{
				umint nDepth = 0;
				for (auto i = _iToken; i >= 0; i = fp_PreviousCode(umint(i)))
				{
					if (!m_Structure.f_IsAngleBracket(umint(i)))
						continue;

					if (m_Tokens.f_IsText(Tokens[umint(i)], ">"))
					{
						++nDepth;

						continue;
					}

					if (!--nDepth)
						return i;
				}

				return aint(-1);
			}
		;

		// The name may carry its own template argument list, as an explicit instantiation
		// or a specialization does: 'f_Create<...>(...)'.
		umint iDeclarator = umint(iBeforeOpen);
		if (m_Structure.f_IsAngleBracket(iDeclarator) && m_Tokens.f_IsText(Tokens[iDeclarator], ">"))
		{
			auto iOpenAngle = fSkipAngleBackwards(aint(iDeclarator));
			if (iOpenAngle < 0)
				return false;

			auto iName = fp_PreviousCode(umint(iOpenAngle));
			if (iName < 0 || Tokens[umint(iName)].m_Kind != ECodeTokenKind::mc_Identifier)
				return false;

			iDeclarator = umint(iName);
		}
		else if (Tokens[iDeclarator].m_Kind != ECodeTokenKind::mc_Identifier)
		{
			// An operator function is named by the keyword and its symbol, or by the call
			// operator's own parentheses: 'operator ->* (', 'operator () ('.
			auto iOperator = fp_PreviousCode(iDeclarator);
			if (iOperator >= 0 && m_Tokens.f_IsText(Tokens[iDeclarator], ")") && m_Tokens.f_IsText(Tokens[umint(iOperator)], "("))
				iOperator = fp_PreviousCode(umint(iOperator));

			if (iOperator < 0 || !m_Tokens.f_IsText(Tokens[umint(iOperator)], "operator"))
				return false;

			iDeclarator = umint(iOperator);
		}

		auto iWalk = fp_PreviousCode(iDeclarator);
		if (iWalk >= 0 && m_Tokens.f_IsText(Tokens[umint(iWalk)], "~"))
		{
			iDeclarator = umint(iWalk);
			iWalk = fp_PreviousCode(iDeclarator);
		}

		while (iWalk >= 0 && m_Tokens.f_IsText(Tokens[umint(iWalk)], "::"))
		{
			iDeclarator = umint(iWalk);
			iWalk = fp_PreviousCode(iDeclarator);
			if (iWalk >= 0 && m_Structure.f_IsAngleBracket(umint(iWalk)) && m_Tokens.f_IsText(Tokens[umint(iWalk)], ">"))
			{
				auto iOpenAngle = fSkipAngleBackwards(iWalk);
				if (iOpenAngle < 0)
					return false;

				iDeclarator = umint(iOpenAngle);
				iWalk = fp_PreviousCode(iDeclarator);
			}

			if (iWalk < 0 || Tokens[umint(iWalk)].m_Kind != ECodeTokenKind::mc_Identifier)
				break;

			iDeclarator = umint(iWalk);
			iWalk = fp_PreviousCode(iDeclarator);
		}

		// Skip a template header and the declaration specifiers before the return type.
		constexpr ch8 const *c_pSpecifiers[] =
			{
				"static", "virtual", "inline", "constexpr", "consteval", "constinit", "explicit", "friend", "extern", "mutable", "thread_local"
			}
		;
		umint iReturn = _iDeclFirst;
		umint iSpecifiersFirst = _iDeclFirst;
		while (iReturn < iDeclarator)
		{
			auto const &Token = Tokens[iReturn];
			bool bSkip = m_Tokens.f_HasRole(Token, ECodeNameRole::mc_SpecifierMacro);
			for (auto pSpecifier : c_pSpecifiers)
				bSkip |= m_Tokens.f_IsText(Token, pSpecifier);

			if (m_Tokens.f_IsText(Token, "template"))
			{
				// 'template <...>' introduces a declaration and is stepped over. 'template
				// Type Name(...)' is an explicit instantiation, whose 'template' is a
				// specifier like any other: the return type behind it moves the same way.
				auto iAfter = fp_SkipTemplateHeader(iReturn);
				if (iAfter == iReturn)
				{
					auto iNext = fp_NextCode(iReturn);
					if (iNext < 0)
						return false;

					iAfter = umint(iNext);
				}

				if (iAfter >= iDeclarator)
					return false;

				iReturn = iAfter;
				iSpecifiersFirst = iAfter;

				continue;
			}

			if (m_Tokens.f_IsText(Token, "["))
			{
				// The attribute is a child group; step past it whole. Without one the shape
				// is not the expected declaration, so no conversion is attempted.
				umint iAfter = TCLimitsInt<umint>::mc_Max;
				for (auto iChild : Node.m_Children)
				{
					auto const &Child = Nodes[iChild];
					if (Child.m_iFirstToken < iReturn || Child.m_iLastToken >= iDeclarator)
						continue;

					auto iNext = fp_NextCode(Child.m_iLastToken);
					if (iNext >= 0)
						iAfter = umint(iNext);

					break;
				}

				if (iAfter == TCLimitsInt<umint>::mc_Max || iAfter <= iReturn)
					return false;

				iReturn = iAfter;

				continue;
			}

			if (!bSkip)
				break;

			auto iNext = fp_NextCode(iReturn);
			if (iNext < 0)
				return false;

			iReturn = umint(iNext);
		}

		// A constructor, a destructor, and a conversion operator have no return type.
		if (iReturn >= iDeclarator)
			return false;

		auto iReturnLast = fp_PreviousCode(iDeclarator);
		if (iReturnLast < 0 || umint(iReturnLast) < iReturn)
			return false;

		// A macro, by the project's naming, can expand to specifiers as readily as to a type,
		// and a specifier moved behind the parameter list is no type.
		for (auto i = iReturn; i <= umint(iReturnLast); ++i)
		{
			if (m_Tokens.f_HasRole(Tokens[i], ECodeNameRole::mc_Macro))
				return false;
		}

		// A return type that is already 'auto' has nowhere to go: moving it behind the
		// parameter list would only spell the same deduction as 'auto ... -> auto'.
		if (umint(iReturnLast) == iReturn && m_Tokens.f_IsText(Tokens[iReturn], "auto"))
			return false;

		umint nReturnWidth = 0;
		if (!fp_MeasureJoinedWidth(iReturn, umint(iReturnLast), nReturnWidth))
			return false;

		// Everything before the name has to read as a type. An expression statement also
		// ends in a call, and rewriting one of those as a declaration would destroy it.
		auto iBeforeDeclarator = fp_PreviousCode(iDeclarator);
		if (iBeforeDeclarator >= 0 && (m_Tokens.f_IsText(Tokens[umint(iBeforeDeclarator)], ".") || m_Tokens.f_IsText(Tokens[umint(iBeforeDeclarator)], "->")))
			return false;

		// A keyword is spelled like an identifier, so a statement that opens with one reads
		// as a type unless it is named here. 'return g_Dispatch(x) / [] {}' is an expression
		// whose first word would otherwise pass for its return type.
		constexpr ch8 const *c_pStatementKeywords[] =
			{
				"return", "co_return", "co_await", "co_yield", "throw", "new", "delete", "this", "sizeof", "alignof"
				, "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "goto"
				, "try", "catch", "using", "namespace", "nullptr", "true", "false", "operator"
				, "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast"
			}
		;
		// A name that follows a complete type at the type's own level is not part of it:
		// an attribute macro stands there, and moving it along would misplace it. Only the
		// words a type is spelled with may follow another name.
		constexpr ch8 const *c_pTypeWords[] =
			{
				"const", "volatile", "typename", "struct", "class", "union", "enum", "unsigned", "signed"
				, "short", "long", "int", "char", "double", "float", "bool", "void"
			}
		;
		umint nAngle = 0;
		bool bAfterName = false;
		for (umint i = iReturn; i <= umint(iReturnLast); ++i)
		{
			auto const &Token = Tokens[i];
			// Whatever a template argument list holds, a function type's parameter list
			// included, is part of the type around it.
			if (m_Structure.f_IsAngleBracket(i))
			{
				nAngle += m_Tokens.f_IsText(Token, "<") ? 1 : 0;
				nAngle -= m_Tokens.f_IsText(Token, ">") ? 1 : 0;
				bAfterName = !nAngle;

				continue;
			}

			if (nAngle || Token.m_Kind == ECodeTokenKind::mc_Whitespace)
				continue;

			if (Token.m_Kind == ECodeTokenKind::mc_Identifier)
			{
				for (auto pKeyword : c_pStatementKeywords)
				{
					if (m_Tokens.f_IsText(Token, pKeyword))
						return false;
				}

				bool bTypeWord = false;
				for (auto pWord : c_pTypeWords)
					bTypeWord |= m_Tokens.f_IsText(Token, pWord);

				if (bAfterName && !bTypeWord)
					return false;

				bAfterName = true;

				continue;
			}

			// A declarator completes the type in front of it, so a name behind 'void *' is
			// as much a bare name as one behind 'void': 'void * DMibCrossmoduleAPI fs_Alloc'.
			bool bDeclarator = m_Tokens.f_IsText(Token, "*") || m_Tokens.f_IsText(Token, "&") || m_Tokens.f_IsText(Token, "&&");
			bAfterName = bAfterName && bDeclarator;

			if (Token.m_Kind == ECodeTokenKind::mc_Number)
				continue;

			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				return false;

			bool bTypePunctuation = m_Tokens.f_IsText(Token, "::")
				|| m_Tokens.f_IsText(Token, "*")
				|| m_Tokens.f_IsText(Token, "&")
				|| m_Tokens.f_IsText(Token, "&&")
				|| m_Tokens.f_IsText(Token, ",")
				|| m_Tokens.f_IsText(Token, "[")
				|| m_Tokens.f_IsText(Token, "]")
				|| m_Tokens.f_IsText(Token, "...")
			;
			if (!bTypePunctuation)
				return false;
		}

		// Find where the trailing type goes: after the qualifiers, before a definition,
		// a pure specifier, or the terminator. An existing arrow means there is nothing to do.
		umint iInsert = TCLimitsInt<umint>::mc_Max;
		for (auto i = fp_NextCode(iClose); i >= 0 && umint(i) <= Node.m_iLastToken; i = fp_NextCode(umint(i)))
		{
			auto const &Token = Tokens[umint(i)];
			if (m_Tokens.f_IsText(Token, "->"))
				return false;

			// The trailing type follows the qualifiers but comes in front of 'override' and
			// 'final', which are written after the declarator, and in front of a pure
			// specifier, a body, a requires clause and the terminator.
			bool bInsert = m_Tokens.f_IsText(Token, "=")
				|| m_Tokens.f_IsText(Token, "{")
				|| m_Tokens.f_IsText(Token, ";")
				|| m_Tokens.f_IsText(Token, "requires")
				|| m_Tokens.f_IsText(Token, "override")
				|| m_Tokens.f_IsText(Token, "final")
			;
			if (bInsert)
			{
				iInsert = umint(i);

				break;
			}
		}

		if (iInsert == TCLimitsInt<umint>::mc_Max)
			return false;

		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());
		auto iReturnStart = Tokens[iReturn].m_iOffset;
		CStr ReturnType(m_Request.m_Source.f_GetStr() + iReturnStart, Tokens[umint(iReturnLast)].f_GetEnd() - iReturnStart);
		auto iPrevious = fp_PreviousCode(iInsert);
		if (iPrevious < 0)
			return false;

		// The gap before the insertion point is written whole, so no other rule may claim it.
		auto iGap = Tokens[umint(iPrevious)].f_GetEnd();
		auto nGap = Tokens[iInsert].m_iOffset - iGap;
		auto bBody = m_Tokens.f_IsText(Tokens[iInsert], "{") || m_Tokens.f_IsText(Tokens[iInsert], ";");
		// 'override', 'final' and a pure specifier are written behind the trailing type, on
		// its line.
		bool bVirtSpecifier = m_Tokens.f_IsText(Tokens[iInsert], "override") || m_Tokens.f_IsText(Tokens[iInsert], "final") || m_Tokens.f_IsText(Tokens[iInsert], "=");
		CStr Replacement = Ending + fp_MakeIndent(_iIndent + nTab) + "-> " + ReturnType;
		Replacement += bVirtSpecifier ? CStr(" ") : Ending + fp_MakeIndent(bBody ? _iIndent : _iIndent + nTab);
		// With the trailing type on a line of its own, 'auto' and everything up to it may
		// already fit on one. The parameter list is then left whole: opening it is the
		// step after this one, not a part of it.
		umint nSignature = 0;
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		// The specifiers in front of the return type stay in front of 'auto', and take their
		// columns on the line with it: 'inline_small auto f_Get(...)'. A template header has
		// a line of its own.
		auto nAuto = _iIndent + CStr("auto ").f_GetLen();
		auto iSpecifiersLast = fp_PreviousCode(iReturn);
		umint nSpecifiers = 0;
		if (iReturn > iSpecifiersFirst && iSpecifiersLast >= 0 && umint(iSpecifiersLast) >= iSpecifiersFirst && fp_MeasureJoinedWidth(iSpecifiersFirst, umint(iSpecifiersLast), nSpecifiers))
			nAuto += nSpecifiers + 1;
		bool bFits = fp_MeasureJoinedWidth(iDeclarator, umint(iPrevious), nSignature) && (!nMaxColumns || nAuto + nSignature <= nMaxColumns);

		// Converting costs eight columns of its own. It pays for itself when it makes the
		// signature fit, and otherwise only when the name would not fit in front of the
		// parameter list at all and the return type is wider than the 'auto' that replaces
		// it: moving 'void' frees no room on the line.
		if (!bFits && (bNameFits || nReturnWidth <= CStr("auto").f_GetLen()))
			return false;

		auto nReturn = Tokens[umint(iReturnLast)].f_GetEnd() - iReturnStart;
		if (fp_IsDisabled(iReturnStart, nReturn) || !fp_IsSelected(iReturnStart, nReturn) || fp_IsDisabled(iGap, nGap) || !fp_IsSelected(iGap, nGap))
			return false;

		auto &First = m_Structural.f_Insert();
		First.m_iOffset = iReturnStart;
		First.m_nLength = nReturn;
		// A declarator hugs the name, so nothing stands between a type that ends in one and
		// the name: 'CFoo &f_Get()'. The keyword that takes its place would run into it.
		First.m_Replacement = iReturnStart + nReturn == Tokens[iDeclarator].m_iOffset ? "auto " : "auto";
		First.m_Rule = "trailing-return";
		auto &Second = m_Structural.f_Insert();
		Second.m_iOffset = iGap;
		Second.m_nLength = nGap;
		Second.m_Replacement = Replacement;
		Second.m_Rule = "trailing-return";

		return true;
	}

	// Fallback for a construct whose own lines are fixed: its inner constructs can still be
	// brought back to one line where they fit.
	void CFormattingAnalyzer::fp_JoinNode(umint _iNode)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (Node.m_Kind == ECodeNodeKind::mc_Unsupported)
			return;

		bool bContainer = Node.m_Kind == ECodeNodeKind::mc_File
			|| Node.m_Kind == ECodeNodeKind::mc_Block
			|| (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Brace)
		;
		if (!bContainer && Node.f_IsJoinable() && !Node.m_bFixedLineBreaks && !Node.m_bTemplateHeader)
		{
			auto iFirst = Node.m_iFirstToken;
			if (Node.m_Kind == ECodeNodeKind::mc_Group)
			{
				umint iStatement = _iNode;
				while (Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement && iStatement)
					iStatement = Nodes[iStatement].m_iParent;

				auto iBoundary = Nodes[iStatement].m_Kind == ECodeNodeKind::mc_Statement ? Nodes[iStatement].m_iFirstToken : iFirst;
				auto iOwner = fp_PreviousCode(iFirst);
				if (iOwner >= 0 && umint(iOwner) >= iBoundary)
				{
					auto const &Owner = m_Tokens.f_GetTokens()[umint(iOwner)];
					bool bOwns = Owner.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Owner, ")") || m_Tokens.f_IsText(Owner, "]") || m_Tokens.f_IsText(Owner, ">");
					// Behind a list written across lines the call starts a line of its own, as the
					// layout puts it: '>' over '().promise()'.
					if (bOwns && Owner.m_Kind != ECodeTokenKind::mc_Identifier)
					{
						auto iOwnerList = m_Structure.f_FindNodeClosingAt(umint(iOwner));
						if (iOwnerList < Nodes.f_GetLen() && fp_SpansLines(Nodes[iOwnerList].m_iFirstToken, umint(iOwner)))
							bOwns = false;
					}

					// A lambda's introducer parts are units of their own, so one never pulls
					// the next onto its line the way a name pulls its argument list.
					if (bOwns && !fp_ClosesLambdaIntroducer(umint(iOwner)))
						iFirst = umint(iOwner);
				}

				// A unary '!' or '~' in front belongs to its operand: '!(a && b)', '!!x'.
				for (auto iUnary = fp_PreviousCode(iFirst); iUnary >= 0 && umint(iUnary) >= iBoundary; iUnary = fp_PreviousCode(iFirst))
				{
					auto const &Unary = m_Tokens.f_GetTokens()[umint(iUnary)];
					if (!m_Tokens.f_IsText(Unary, "!") && !m_Tokens.f_IsText(Unary, "~"))
						break;

					iFirst = umint(iUnary);
				}
			}

			if (fp_TryJoin(iFirst, Node.m_iLastToken, fp_GetStatementIndent(iFirst)))
				return;
		}

		// A list keeping its lines still lays out an element written on one line too long for
		// it, as an opened list lays out each of its elements.
		TCVector<umint> LaidOut;
		if (Node.m_Kind == ECodeNodeKind::mc_Group && (Node.m_Bracket == ECodeBracket::mc_Brace || Node.m_Bracket == ECodeBracket::mc_Paren))
		{
			umint iElement = Node.m_iFirstToken + 1;
			for (umint iSplit = 0; iElement <= Node.m_iLastToken; ++iSplit)
			{
				auto iEnd = iSplit < Node.m_SplitPoints.f_GetLen() ? Node.m_SplitPoints[iSplit] : Node.m_iLastToken;
				auto iLast = fp_PreviousCode(iEnd);
				auto iStart = fp_NextCode(iElement - 1);
				bool bElement = iEnd > iElement && iStart >= 0 && iLast >= 0 && umint(iStart) <= umint(iLast) && fp_IsFirstOnLine(umint(iStart));
				for (auto i = bElement ? umint(iStart) + 1 : umint(0); bElement && i < umint(iLast); ++i)
					bElement = m_Tokens.f_GetTokens()[i].m_Kind != ECodeTokenKind::mc_Newline;

				if (bElement && fp_IsLastOnLine(umint(iLast)))
				{
					auto nIndent = fp_GetStatementIndent(umint(iStart));
					if (!fp_FitsInline(umint(iStart), umint(iLast), nIndent) && fp_IsRangeJoinable(_iNode, umint(iStart), umint(iLast)))
					{
						fp_LayoutRange(_iNode, umint(iStart), umint(iLast), nIndent, false, false);
						LaidOut.f_Insert(umint(iStart));
						LaidOut.f_Insert(umint(iLast));
					}
				}

				if (iSplit >= Node.m_SplitPoints.f_GetLen())
					break;

				iElement = Node.m_SplitPoints[iSplit];
			}
		}

		for (auto iChild : Node.m_Children)
		{
			bool bLaidOut = false;
			for (umint iRange = 0; iRange + 1 < LaidOut.f_GetLen() && !bLaidOut; iRange += 2)
				bLaidOut = Nodes[iChild].m_iFirstToken >= LaidOut[iRange] && Nodes[iChild].m_iLastToken <= LaidOut[iRange + 1];

			if (bLaidOut)
				continue;

			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Block)
			{
				fp_LayoutLambdaHead(_iNode, iChild);
				fp_LayoutNode(iChild, fp_GetStatementIndent(Nodes[iChild].m_iFirstToken));
			}
			else
				fp_JoinNode(iChild);
		}
	}

	// A lambda standing in a construct whose own lines are kept still has a head of its
	// own: one that does not fit on its line is split as it would be in any statement.
	void CFormattingAnalyzer::fp_LayoutLambdaHead(umint _iNode, umint _iBody)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto iBrace = Nodes[_iBody].m_iFirstToken;
		auto iHeadLast = fp_PreviousCode(iBrace);
		if (iHeadLast < 0)
			return;

		umint iHeadFirst = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Nodes[_iNode].m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (Child.m_iLastToken >= iBrace)
				break;

			if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_Bracket == ECodeBracket::mc_Square && fp_ClosesLambdaIntroducer(Child.m_iLastToken))
				iHeadFirst = Child.m_iFirstToken;
		}

		if (iHeadFirst == TCLimitsInt<umint>::mc_Max)
			return;

		// The expression the lambda is an operand of starts the line, as in
		// 'g_ActorSubscription(Actor) / [...]', and is laid out with it.
		auto iLineFirst = iHeadFirst;
		while (!fp_IsFirstOnLine(iLineFirst))
		{
			auto iPrevious = fp_PreviousCode(iLineFirst);
			if (iPrevious < 0 || umint(iPrevious) <= Nodes[_iNode].m_iFirstToken)
				return;

			// A group in front is stepped over whole: 'g_ActorSubscription(Actor) / [...]'.
			auto iGroup = m_Structure.f_FindNodeClosingAt(umint(iPrevious));
			if (iGroup < Nodes.f_GetLen() && Nodes[iGroup].m_Kind == ECodeNodeKind::mc_Group)
				iPrevious = aint(Nodes[iGroup].m_iFirstToken);

			if (umint(iPrevious) <= Nodes[_iNode].m_iFirstToken || m_TokenDepth[umint(iPrevious)] != m_TokenDepth[iHeadFirst])
				return;

			iLineFirst = umint(iPrevious);
		}

		auto nIndent = fp_GetStatementIndent(iLineFirst);
		if (!fp_IsRangeJoinable(_iNode, iLineFirst, umint(iHeadLast)) || fp_FitsInline(iLineFirst, umint(iHeadLast), nIndent))
			return;

		// The capture list stays on the operator's line where it fits there, and the parts
		// behind it follow at that line's level. One too long for it goes below the operator
		// as the statement's continuation.
		umint iCaptureLast = Nodes[m_Structure.f_FindNodeOpeningAt(iHeadFirst)].m_iLastToken;
		bool bCaptureBelow = iLineFirst != iHeadFirst && !fp_FitsInline(iLineFirst, iCaptureLast, nIndent);
		fp_LayoutRange(_iNode, iLineFirst, umint(iHeadLast), nIndent, false, bCaptureBelow);
	}

	// A member initializer list is its own line structure: one entry per line, each split
	// only when that entry does not fit. It is never folded onto the signature.
	umint CFormattingAnalyzer::fp_FindInitializerList(umint _iNode, umint _iFirstParen, umint _iLast) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (!_iFirstParen)
			return TCLimitsInt<umint>::mc_Max;

		// A class's colon starts its base clause, whatever parenthesis stands in front of it,
		// and a case's ends its label: 'struct alignas(32) CFoo : public CBar', 'case DFoo(1):'.
		auto const &Key = m_Tokens.f_GetTokens()[fp_SkipTemplateHeader(Node.m_iFirstToken)];
		if (m_Tokens.f_IsText(Key, "struct") || m_Tokens.f_IsText(Key, "class") || m_Tokens.f_IsText(Key, "union") || m_Tokens.f_IsText(Key, "case"))
			return TCLimitsInt<umint>::mc_Max;

		// A conditional operator also puts a colon at the statement's own level, and its
		// '?' can stand in front of the parameter list the initializer list follows.
		for (umint i = Node.m_iFirstToken; i <= _iLast; ++i)
		{
			bool bInside = false;
			for (auto iChild : Node.m_Children)
				bInside |= i >= Nodes[iChild].m_iFirstToken && i <= Nodes[iChild].m_iLastToken;

			if (bInside)
				continue;

			if (m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], "?"))
				return TCLimitsInt<umint>::mc_Max;

			if (i >= _iFirstParen && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], ":"))
				return i;
		}

		return TCLimitsInt<umint>::mc_Max;
	}

	void CFormattingAnalyzer::fp_LayoutInitializerList(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		NContainer::TCVector<umint> Entries;
		Entries.f_Insert(_iFirst);
		for (umint i = _iFirst + 1; i <= _iLast; ++i)
		{
			bool bInside = false;
			for (auto iChild : Node.m_Children)
				bInside |= i >= Nodes[iChild].m_iFirstToken && i <= Nodes[iChild].m_iLastToken;

			if (!bInside && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], ","))
				Entries.f_Insert(i);
		}

		for (umint iEntry = 0; iEntry < Entries.f_GetLen(); ++iEntry)
		{
			auto iEnd = iEntry + 1 < Entries.f_GetLen() ? umint(fp_PreviousCode(Entries[iEntry + 1])) : _iLast;
			// A directive or a comment inside an entry fixes its lines. Measuring such an
			// entry as one line makes it look far too wide and splits what already fits.
			if (!fp_IsRangeJoinable(_iNode, Entries[iEntry], iEnd))
				continue;

			// An entry is a unit of its own: its scope markers align with it, as a call's
			// do inside a split expression, and a lambda body in it opens under it.
			fp_BreakBefore(Entries[iEntry], _iIndent);
			fp_LayoutRange(_iNode, Entries[iEntry], iEnd, _iIndent, false, false);
		}
	}

	// The conversions are decided by a walk over the statements and the blocks they own. A
	// lambda's body belongs to the group it is written in rather than to the statement
	// around it, so the walk reaches one through the groups of that statement.
	void CFormattingAnalyzer::fp_ProbeBodies(umint _iNode, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		for (auto iChild : Nodes[_iNode].m_Children)
		{
			if (Nodes[iChild].m_Kind != ECodeNodeKind::mc_Group)
				continue;

			for (auto iInner : Nodes[iChild].m_Children)
			{
				if (Nodes[iInner].m_Kind == ECodeNodeKind::mc_Block)
					fp_LayoutNode(iInner, _iIndent);
			}

			fp_ProbeBodies(iChild, _iIndent);
		}
	}

	void CFormattingAnalyzer::fp_LayoutStatement(umint _iNode, umint _iIndent)
	{
		// Whether a statement broke at its operators is its own: one left over from the
		// statement before would keep this one's body from being placed.
		m_bOperatorSplit = false;
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto const &Tokens = m_Tokens.f_GetTokens();

		bool bOpenedAtParen = false;
		// The parenthesis a statement opens with, where it has one: '(a + b).f_Call();'.
		umint iLeadingClose = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Group && Nodes[iChild].m_Bracket == ECodeBracket::mc_Paren && Nodes[iChild].m_iFirstToken == Node.m_iFirstToken)
				iLeadingClose = Nodes[iChild].m_iLastToken;
		}
		// A block always occupies its own lines, so only the head decides the statement's shape.
		umint iHeadLast = Node.m_iLastToken;
		umint iHeadLastWithInitializers = Node.m_iLastToken;
		umint iBlock = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind != ECodeNodeKind::mc_Block)
				continue;

			iBlock = iChild;
			auto iFirst = Nodes[iChild].m_iFirstToken;
			auto iPrevious = fp_PreviousCode(iFirst);
			iHeadLast = iPrevious >= 0 ? umint(iPrevious)
				: iFirst;
			iHeadLastWithInitializers = iHeadLast;

			break;
		}

		// A statement that does not start its own line, such as one behind an attribute on
		// a clause's line, has no indentation of its own to lay anything out against.
		umint iFirstParenGroup = 0;
		umint iFirstParenGroupStart = 0;
		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Group && Nodes[iChild].m_Bracket == ECodeBracket::mc_Paren)
			{
				iFirstParenGroup = Nodes[iChild].m_iLastToken;
				iFirstParenGroupStart = Nodes[iChild].m_iFirstToken;

				break;
			}
		}

		// The first parenthesis is a declarator's parameter list when a name stands in front
		// of it. Behind a capture list it opens a lambda, and the statement around it is an
		// expression that has no declaration to protect or return type to move.
		auto iBeforeParen = iFirstParenGroupStart ? fp_PreviousCode(iFirstParenGroupStart) : aint(-1);
		bool bDeclarator = iBeforeParen >= 0 && Tokens[umint(iBeforeParen)].m_Kind == ECodeTokenKind::mc_Identifier;
		// An operator's name ends in the symbol it overloads rather than in an identifier,
		// and a lambda's parameter list stands behind its capture list or its own template
		// parameter list. What is left is a declaration, whose body opens at the
		// statement's own indentation.
		if (!bDeclarator && iBeforeParen >= 0)
		{
			bool bIntroducer = fg_IsCaptureList(m_Tokens, m_Structure, umint(iBeforeParen))
				|| (m_Structure.f_IsAngleBracket(umint(iBeforeParen)) && fp_ClosesLambdaIntroducer(umint(iBeforeParen)))
			;
			if (!bIntroducer)
				bDeclarator = fg_ClosesParameterList(m_Tokens, m_Structure, iFirstParenGroup);
		}
		// Whose body the statement's block is decides where it opens and what its terminator
		// does. A lambda is introduced by a capture list standing in front of the brace; a
		// declaration never has one there, whatever the first parenthesis of the statement
		// belongs to: 'Promise.f_Future() > [Promise](CResult &&_Result) { ... }'.
		bool bLambdaBody = false;
		if (iBlock != TCLimitsInt<umint>::mc_Max)
		{
			auto iBrace = Nodes[iBlock].m_iFirstToken;
			for (auto iChild : Node.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_Bracket != ECodeBracket::mc_Square || Child.m_iLastToken >= iBrace)
					continue;

				bLambdaBody |= fp_ClosesLambdaIntroducer(Child.m_iLastToken);
			}

			bLambdaBody |= fp_IsRequirementsBody(_iNode, iBrace);
		}

		// A lambda's body stands one level in, under the expression the lambda is written
		// in. A statement that opens with the lambda itself has no such expression and no
		// continuation level: its body stands where the statement does, like the lines of
		// one that opens with a parenthesis, and what it is called with under the brace.
		// An attribute opens with two brackets and is no capture list: '[[maybe_unused]] auto
		// fA = [&] { ... };' has its lambda in an expression like any other.
		auto iSecond = fp_NextCode(Node.m_iFirstToken);
		bool bOpensWithLambda = m_Tokens.f_IsText(Tokens[Node.m_iFirstToken], "[") && !(iSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSecond)], "["));
		bool bBodyIn = bLambdaBody && !bOpensWithLambda;
		// A name can end in a template argument list, and nothing before it is ever broken.
		// A lambda's own template parameter list ends the same way but names nothing.
		bool bNamed = bDeclarator
			|| (iBeforeParen >= 0 && m_Structure.f_IsAngleBracket(umint(iBeforeParen)) && !fp_FollowsScope(umint(iBeforeParen)))
		;

		// A trailing return type is one logical unit on its own line; its own scope markers
		// are only split when it does not fit there. A lambda writes one of its own and a
		// member access is spelled the same way, so the arrow only counts when nothing but
		// the function's qualifiers stands between it and the parameter list.
		umint iTrailingReturn = TCLimitsInt<umint>::mc_Max;
		if (bDeclarator && iFirstParenGroupStart && fg_ClosesParameterList(m_Tokens, m_Structure, iFirstParenGroup))
		{
			constexpr ch8 const *c_pQualifiers[] =
				{
					"const", "volatile", "noexcept", "override", "final", "&", "&&"
				}
			;
			bool bAfterQualifier = false;
			for (auto i = fp_NextCode(iFirstParenGroup); i >= 0 && umint(i) <= Node.m_iLastToken; )
			{
				if (m_Tokens.f_IsText(Tokens[umint(i)], "->"))
				{
					iTrailingReturn = umint(i);

					break;
				}

				// A qualifier can carry an argument, as 'noexcept(...)' does, and that is
				// stepped over whole rather than mistaken for the end of the qualifiers.
				if (bAfterQualifier && m_Tokens.f_IsText(Tokens[umint(i)], "("))
				{
					umint iEnd = TCLimitsInt<umint>::mc_Max;
					for (auto iChild : Node.m_Children)
					{
						if (Nodes[iChild].m_iFirstToken == umint(i))
						{
							iEnd = Nodes[iChild].m_iLastToken;

							break;
						}
					}

					if (iEnd == TCLimitsInt<umint>::mc_Max)
						break;

					bAfterQualifier = false;
					i = fp_NextCode(iEnd);

					continue;
				}

				bAfterQualifier = false;
				for (auto pQualifier : c_pQualifiers)
					bAfterQualifier |= m_Tokens.f_IsText(Tokens[umint(i)], pQualifier);

				if (!bAfterQualifier)
					break;

				i = fp_NextCode(umint(i));
			}
		}

		auto iInitializerList = fp_FindInitializerList(_iNode, iFirstParenGroup, iHeadLast);
		auto iSignatureLast = iHeadLast;
		if (iInitializerList != TCLimitsInt<umint>::mc_Max)
		{
			auto iPrevious = fp_PreviousCode(iInitializerList);
			if (iPrevious < 0)
				iInitializerList = TCLimitsInt<umint>::mc_Max;
			else
				iSignatureLast = umint(iPrevious);
		}

		// A template header holds the line it is on. The declaration behind it starts a
		// line of its own and is laid out there, so the header is stepped over first.
		// A template header and a requires clause behind one each keep the line they are on,
		// in whatever order and number they stand, and the declaration behind them starts a
		// line of its own that is laid out as usual. Only a clause elsewhere in the
		// declaration keeps the whole statement's lines.
		umint iDeclFirst = Node.m_iFirstToken;
		bool bFixedLineBreaks = Node.m_bFixedLineBreaks;
		bool bSteppedOverClause = false;
		while (Node.m_bTemplateHeader)
		{
			if (m_Tokens.f_IsText(Tokens[iDeclFirst], "template"))
			{
				auto iNext = fp_SkipTemplateHeader(iDeclFirst);
				if (iNext == iDeclFirst || iNext > iSignatureLast)
					break;

				// The header itself still comes back to one line where it fits. One that does not
				// fit opens its parameter list, with 'template' alone on its line and the markers
				// at the header's level, since nothing extends the header behind its '>':
				// 'template' / '<' / 'typename t_C' / '>'.
				auto iHeaderLast = fp_PreviousCode(iNext);
				if (!m_bProbing && iHeaderLast >= 0 && umint(iHeaderLast) > iDeclFirst)
				{
					if (fp_FitsInline(iDeclFirst, umint(iHeaderLast), _iIndent))
						fp_MarkInline(iDeclFirst, umint(iHeaderLast));
					else
					{
						auto iOpen = fp_NextCode(iDeclFirst);
						umint nJoined = 0;
						for (auto iChild : Node.m_Children)
						{
							auto const &Child = Nodes[iChild];
							if (iOpen < 0 || Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_iFirstToken != umint(iOpen) || Child.m_iLastToken != umint(iHeaderLast))
								continue;

							if (fp_MeasureJoinedWidth(iDeclFirst, umint(iHeaderLast), nJoined))
								fp_LayoutGroup(iChild, _iIndent, true);

							break;
						}
					}
				}

				iDeclFirst = iNext;

				continue;
			}

			if (!m_Tokens.f_IsText(Tokens[iDeclFirst], "requires"))
				break;

			auto nLevel = m_TokenDepth[iDeclFirst];
			aint iClauseEnd = -1;
			for (auto iNext = fp_NextCode(iDeclFirst); iNext >= 0 && umint(iNext) <= iSignatureLast; iNext = fp_NextCode(umint(iNext)))
			{
				if (m_TokenDepth[umint(iNext)] == nLevel && fp_IsFirstOnLine(umint(iNext)))
				{
					iClauseEnd = iNext;

					break;
				}
			}

			if (iClauseEnd < 0)
				break;

			iDeclFirst = umint(iClauseEnd);
			bSteppedOverClause = true;
		}

		if (bSteppedOverClause)
		{
			bFixedLineBreaks = false;
			for (auto iRest = iDeclFirst; iRest <= Node.m_iLastToken; ++iRest)
			{
				if (m_Tokens.f_IsText(Tokens[iRest], "requires"))
					bFixedLineBreaks = true;
			}
		}

		// A requires clause behind the declaration keeps the line it starts, and the
		// declaration in front of it is laid out as any other that ends there.
		if (bFixedLineBreaks)
		{
			aint iClause = -1;
			bool bElsewhere = false;
			for (auto i = fp_NextCode(iDeclFirst); i >= 0 && umint(i) <= iHeadLast; i = fp_NextCode(umint(i)))
			{
				if (!m_Tokens.f_IsText(Tokens[umint(i)], "requires"))
					continue;

				if (m_TokenDepth[umint(i)] == m_TokenDepth[iDeclFirst] && fp_IsFirstOnLine(umint(i)))
				{
					iClause = i;

					break;
				}

				bElsewhere = true;
			}

			// Only a clause written on one line is left alone whole; one holding a requires
			// expression, whose braces the head can end in, keeps the statement's lines.
			// The terminator of a declaration ends the statement, not the clause, and an
			// initializer list behind the clause is laid out on lines of its own.
			auto iClauseLast = iInitializerList != TCLimitsInt<umint>::mc_Max ? fp_PreviousCode(iInitializerList) : aint(iHeadLast);
			if (iClauseLast >= 0 && umint(iClauseLast) == Node.m_iLastToken && m_Tokens.f_IsText(Tokens[umint(iClauseLast)], ";"))
				iClauseLast = fp_PreviousCode(umint(iClauseLast));

			bool bOneLineClause = iClause >= 0 && iClauseLast >= 0 && !m_Tokens.f_IsText(Tokens[umint(iClauseLast)], "requires");
			for (auto i = iClause >= 0 ? fp_NextCode(umint(iClause)) : aint(-1); bOneLineClause && i >= 0 && i <= iClauseLast; i = fp_NextCode(umint(i)))
				bOneLineClause = !fp_IsFirstOnLine(umint(i));

			auto iBeforeClause = iClause >= 0 ? fp_PreviousCode(umint(iClause)) : aint(-1);
			if (bOneLineClause && iBeforeClause >= 0 && umint(iBeforeClause) >= iDeclFirst && !bElsewhere)
			{
				iSignatureLast = umint(iBeforeClause);
				iHeadLast = iSignatureLast;
				bFixedLineBreaks = false;
			}
		}

		auto iElse = fp_LeadingElse(iDeclFirst);
		umint iLineFirst = iElse >= 0 ? umint(iElse) : iDeclFirst;
		bool bJoinable = fp_IsRangeJoinable(_iNode, iDeclFirst, iSignatureLast)
			&& !bFixedLineBreaks
			&& Node.m_Kind != ECodeNodeKind::mc_Unsupported
			&& fp_IsFirstOnLine(iLineFirst)
		;
		if (iInitializerList != TCLimitsInt<umint>::mc_Max)
			iHeadLast = iSignatureLast;

		bool bFits = bJoinable && fp_FitsInline(iLineFirst, iHeadLast, _iIndent);

		// The first phase only decides which return types move. A declaration that fits as
		// it stands has no reason to; one that does not is converted where that lets its
		// name fit, and the converted source is what gets laid out. Anything else is laid
		// out as the second phase will, so the bodies inside it are measured at the depth
		// they are moved to rather than the one the source gave them.
		if (m_bProbing && bJoinable && !bFits && !fp_IsInFunctionBody(_iNode) && fp_ConvertTrailingReturn(_iNode, iDeclFirst, _iIndent))
		{
			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, _iIndent, !bBodyIn);
				fp_LayoutNode(iBlock, _iIndent);
			}

			fp_ProbeBodies(_iNode, _iIndent);

			return;
		}

		// What an expression goes on with behind a lambda's body resumes under the body's
		// closing brace: an operator or a member access and what it takes, or the arguments
		// the lambda is called with at once. It is laid out
		// there like any other line, broken at its operators where it does not fit, and the
		// terminator of a statement that has such a tail takes a line of its own.
		auto fLayoutTail = [&]
			{
				if (!bLambdaBody || iBlock == TCLimitsInt<umint>::mc_Max || !fp_IsFirstOnLine(Nodes[iBlock].m_iFirstToken))
					return;

				auto iTail = fp_NextCode(Nodes[iBlock].m_iLastToken);
				if (!bBodyIn)
				{
					// Under the brace of a statement that opens with its lambda, what it is
					// called with stands where the statement does, as written otherwise.
					bool bMoves = iTail >= 0
						&& umint(iTail) < Node.m_iLastToken
						&& fp_IsFirstOnLine(umint(iTail))
						&& fp_GetStatementIndent(umint(iTail)) != _iIndent
					;
					if (bMoves)
						fp_IndentBefore(umint(iTail), _iIndent);

					// With no continuation level the terminator would stand at the level of the
					// line it ends, so it ends that line instead, as the one of a statement
					// split at its leading parenthesis does: '() > fg_TempCopy(Promise);'.
					bool bOwnTail = iTail >= 0 && umint(iTail) < Node.m_iLastToken && fp_IsFirstOnLine(umint(iTail));
					if (bOwnTail && m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";") && fp_FitsInline(umint(iTail), Node.m_iLastToken, _iIndent))
						fp_MarkInline(umint(iTail), Node.m_iLastToken);

					// A call that hugs the brace, '}()', ends the statement there as well.
					bool bHugsBrace = iTail >= 0 && umint(iTail) < Node.m_iLastToken && !fp_IsFirstOnLine(umint(iTail));
					auto iBrace = Nodes[iBlock].m_iLastToken;
					if (bHugsBrace && m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";") && fp_FitsInline(iBrace, Node.m_iLastToken, _iIndent))
						fp_MarkInline(iBrace, Node.m_iLastToken);

					return;
				}

				if (iTail < 0 || umint(iTail) >= Node.m_iLastToken || Tokens[umint(iTail)].m_Kind != ECodeTokenKind::mc_Punctuator)
					return;

				auto const &Tail = Tokens[umint(iTail)];
				if (m_Tokens.f_IsText(Tail, ")") || m_Tokens.f_IsText(Tail, ",") || m_Tokens.f_IsText(Tail, ";") || m_Tokens.f_IsText(Tail, "]"))
					return;


				bool bTerminated = m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";");
				auto iTailLast = bTerminated ? fp_PreviousCode(Node.m_iLastToken) : aint(Node.m_iLastToken);
				if (iTailLast < iTail || !fp_IsRangeJoinable(_iNode, umint(iTail), umint(iTailLast)))
					return;

				fp_BreakBefore(umint(iTail), _iIndent + nTab);
				fp_LayoutRange(_iNode, umint(iTail), umint(iTailLast), _iIndent + nTab, false, false);
				if (bTerminated)
					fp_BreakBefore(Node.m_iLastToken, _iIndent);
			}
		;

		if (bFits)
		{
			fp_MarkInline(iDeclFirst, iHeadLast);
			if (iInitializerList != TCLimitsInt<umint>::mc_Max)
				fp_LayoutInitializerList(_iNode, iInitializerList, iHeadLastWithInitializers, _iIndent + nTab);

			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, _iIndent, !bBodyIn);
				fp_LayoutNode(iBlock, _iIndent);
				fLayoutTail();

				// A lambda's terminator stands on a line of its own, at the statement's
				// indentation, where a declaration's stays behind its closing brace: '};'.
				// A body that could not be placed keeps its terminator as written too.
				auto iLast = Node.m_iLastToken;
				bool bLambdaTerminator = bBodyIn
					&& fp_IsFirstOnLine(Nodes[iBlock].m_iFirstToken)
					&& m_Tokens.f_IsText(Tokens[iLast], ";")
					&& fp_PreviousCode(iLast) == aint(Nodes[iBlock].m_iLastToken)
				;
				if (bLambdaTerminator && !fp_IsFirstOnLine(iLast))
					fp_OwnLineBefore(iLast, _iIndent);
				else if (bLambdaTerminator && fp_GetStatementIndent(iLast) != _iIndent)
					fp_IndentBefore(iLast, _iIndent);
			}

			return;
		}

		if (bJoinable)
		{
			bool bClause = m_Tokens.f_IsText(Tokens[iDeclFirst], "if")
				|| m_Tokens.f_IsText(Tokens[iDeclFirst], "for")
				|| m_Tokens.f_IsText(Tokens[iDeclFirst], "while")
				|| m_Tokens.f_IsText(Tokens[iDeclFirst], "switch")
				|| m_Tokens.f_IsText(Tokens[iDeclFirst], "catch")
			;
			bool bHasTerminator = m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";") && Node.m_iLastToken > Node.m_iFirstToken;
			auto iRangeLast = iHeadLast;
			bool bMustSplit = false;
			if (bHasTerminator && iRangeLast == Node.m_iLastToken)
			{
				auto iPrevious = fp_PreviousCode(Node.m_iLastToken);
				if (iPrevious >= 0)
					iRangeLast = umint(iPrevious);

				// The terminator ends the expression's line and counts towards it. An
				// expression that only fits without it is still one that has to be split,
				// since the terminator takes a line of its own only from a split statement.
				bMustSplit = fp_FitsInline(iLineFirst, iRangeLast, _iIndent);
			}

			// A trailing return type is one unit on a line of its own, so the signature in
			// front of it is laid out, and measured, without it.
			umint iSignatureEnd = iRangeLast;
			bool bTrailing = iTrailingReturn != TCLimitsInt<umint>::mc_Max && iTrailingReturn <= iRangeLast;
			if (bTrailing)
			{
				auto iBefore = fp_PreviousCode(iTrailingReturn);
				bTrailing = iBefore >= 0 && umint(iBefore) >= iDeclFirst;
				if (bTrailing)
				{
					iSignatureEnd = umint(iBefore);
					bMustSplit = false;
				}
			}

			m_iSplitFirstParen = bNamed ? iFirstParenGroupStart : 0;
			m_iSplitTrailingReturn = iTrailingReturn;
			m_bOperatorSplit = false;
			// A statement with no scope marker to split keeps its shape; only a statement
			// that was actually relaid out puts its terminator on a line of its own.
			bool bSplit = fp_LayoutRange(_iNode, iLineFirst, iSignatureEnd, _iIndent, bClause, true, bMustSplit);
			if (bTrailing)
			{
				fp_BreakBefore(iTrailingReturn, _iIndent + nTab);
				if (fp_FitsInline(iTrailingReturn, iRangeLast, _iIndent + nTab))
					fp_MarkInline(iTrailingReturn, iRangeLast);

				bSplit = true;
			}

			m_iSplitFirstParen = 0;
			m_iSplitTrailingReturn = TCLimitsInt<umint>::mc_Max;
			// A split statement's terminator takes a line of its own, except behind a
			// declaration's body, where it stays on the closing brace's line: '};'.
			bool bBodyTerminator = iBlock != TCLimitsInt<umint>::mc_Max && !bBodyIn;
			bOpenedAtParen = iLeadingClose != TCLimitsInt<umint>::mc_Max && fp_IsFirstOnLine(iLeadingClose);
			if (bSplit && bHasTerminator && !bBodyTerminator && !bOpenedAtParen)
				fp_BreakBefore(Node.m_iLastToken, _iIndent);
		}

		bOpenedAtParen = iLeadingClose != TCLimitsInt<umint>::mc_Max && fp_IsFirstOnLine(iLeadingClose);
		// A statement split at the parenthesis it opens with has no continuation level: its
		// lines stand where it does, and a terminator on a line of its own would stand among
		// them as one more of them. It ends the statement's last line instead.
		if (bOpenedAtParen && m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";"))
		{
			auto iBeforeTerminator = fp_PreviousCode(Node.m_iLastToken);
			if (iBeforeTerminator >= 0 && umint(iBeforeTerminator) > Node.m_iFirstToken)
				fp_MarkInline(umint(iBeforeTerminator), Node.m_iLastToken);
		}

		if (!bJoinable)
		{
			// The head cannot be relaid out, but its inner constructs still can, and a requires
			// expression's body still stands one level in wherever the source put its brace.
			for (auto iChild : Node.m_Children)
			{
				if (Nodes[iChild].m_Kind != ECodeNodeKind::mc_Block)
					fp_JoinNode(iChild);
				else if (fp_IsFirstOnLine(Nodes[iChild].m_iFirstToken) && fp_IsRequirementsBody(_iNode, Nodes[iChild].m_iFirstToken))
					fp_PlaceBody(_iNode, iChild, _iIndent, false);
			}
		}

		if (bJoinable && iInitializerList != TCLimitsInt<umint>::mc_Max)
			fp_LayoutInitializerList(_iNode, iInitializerList, iHeadLastWithInitializers, _iIndent + nTab);

		if (iBlock != TCLimitsInt<umint>::mc_Max)
		{
			// A declaration's body opens at the statement's own indentation. A lambda's does
			// not: it belongs to an expression and sits one level in, under the lambda.
			// After an operator split the brace is already on a continuation line.
			auto iBrace = Nodes[iBlock].m_iFirstToken;
			if (!fp_IsFirstOnLine(iBrace))
				fp_PlaceBody(_iNode, iBlock, _iIndent, !bBodyIn);
			else if (bJoinable && !m_bOperatorSplit)
				fp_BreakBefore(iBrace, bBodyIn ? _iIndent + nTab : _iIndent);

			fp_LayoutNode(iBlock, _iIndent);
			fLayoutTail();
		}
	}

	// A requires expression's body is an expression's like a lambda's, and stands where one
	// does: 'concept cFoo = requires (t_C _Value)' with the body one level in. A requires
	// clause stands behind a declarator and constrains the body that follows it, while a
	// requires expression stands where an operand does.
	bool CFormattingAnalyzer::fp_IsRequirementsBody(umint _iNode, umint _iBrace) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		auto iBeforeBrace = fp_PreviousCode(_iBrace);
		if (iBeforeBrace >= 0 && m_Tokens.f_IsText(Tokens[umint(iBeforeBrace)], ")"))
		{
			for (auto iChild : Nodes[_iNode].m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_iLastToken == umint(iBeforeBrace))
					iBeforeBrace = fp_PreviousCode(Child.m_iFirstToken);
			}
		}

		if (iBeforeBrace < 0 || !m_Tokens.f_IsText(Tokens[umint(iBeforeBrace)], "requires"))
			return false;

		auto iLead = fp_PreviousCode(umint(iBeforeBrace));
		constexpr ch8 const *c_pOperandLeads[] =
			{
				"=", "(", ",", "||", "!", "?", ":", "return", "requires"
			}
		;
		for (auto pLead : c_pOperandLeads)
		{
			if (iLead >= 0 && m_Tokens.f_IsText(Tokens[umint(iLead)], pLead))
				return true;
		}

		return false;
	}

	// A body that shares a line with its head opens on a line of its own, and takes its
	// lines along so that their depth still follows the brace: a declaration's body at the
	// statement's indentation, a lambda's one level in. A comment or a directive inside
	// could not follow, so a body that would have to move stays where it is. A block that
	// is the statement is placed by the block around it, and after an operator split the
	// brace already stands on a continuation line.
	void CFormattingAnalyzer::fp_PlaceBody(umint _iNode, umint _iBlock, umint _iIndent, bool _bDeclarator)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto iBrace = Nodes[_iBlock].m_iFirstToken;
		if (m_bOperatorSplit || iBrace == Nodes[_iNode].m_iFirstToken)
			return;

		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto nPlace = _bDeclarator ? _iIndent : _iIndent + nTab;
		if (!fp_IsFirstOnLine(iBrace))
		{
			fp_PlaceBlock(_iBlock, nPlace, _iIndent);

			return;
		}

		// A body that already has a line of its own still takes the depth its head gives
		// it, and its lines move with it, so that a lambda written one level too far out
		// is brought back under the expression it belongs to.
		auto nReference = fp_GetStatementIndent(iBrace);
		if (nReference == nPlace || !fp_CanPlaceBlock(_iBlock))
			return;

		fp_IndentBefore(iBrace, nPlace);
		fp_ShiftBlock(_iBlock, aint(nPlace) - aint(nReference));
	}

	// Opens the block on a line of its own at the indentation, moving the lines inside it
	// by the distance from the indentation they were written against. A comment or a
	// directive inside could not follow such a move, so the block then stays where it is.
	bool CFormattingAnalyzer::fp_PlaceBlock(umint _iBlock, umint _iIndent, umint _nReference)
	{
		auto const &Block = m_Structure.f_GetNodes()[_iBlock];
		if (Block.m_Kind != ECodeNodeKind::mc_Block)
			return false;

		auto nDelta = aint(_iIndent) - aint(_nReference);
		if (nDelta && !fp_CanPlaceBlock(_iBlock))
			return false;

		fp_OwnLineBefore(Block.m_iFirstToken, _iIndent);
		if (nDelta)
			fp_ShiftBlock(_iBlock, nDelta);

		return true;
	}

	// Whether the block's lines can follow a move. A multiline token spells its own lines
	// and an opaque directive fixes the ones around it, so neither can be brought to a new
	// depth. A directive keeps the column its own convention gives it either way.
	bool CFormattingAnalyzer::fp_CanPlaceBlock(umint _iBlock) const
	{
		auto const &Block = m_Structure.f_GetNodes()[_iBlock];

		return !Block.m_bHasMultiLineToken && !fp_HasOpaqueDirective(Block.m_iFirstToken, Block.m_iLastToken);
	}

	// Moves every line the block's tokens start by the given number of columns.
	void CFormattingAnalyzer::fp_ShiftBlock(umint _iBlock, aint _nDelta)
	{
		auto const &Block = m_Structure.f_GetNodes()[_iBlock];
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto i = Block.m_iFirstToken + 1; i <= Block.m_iLastToken; ++i)
		{
			// A comment on a line of its own is one of the block's lines and moves with it.
			// One trailing code keeps its place behind that code, and a directive the column
			// its own convention gives it.
			auto Kind = Tokens[i].m_Kind;
			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
			{
				if (!fp_IsFirstOnLine(i))
					continue;

				auto nIndent = aint(fp_GetSourceLineIndent(i)) + _nDelta;
				if (nIndent < 0)
					continue;

				m_bCommentMoved[i] = 1;
				m_CommentIndent[i] = umint(nIndent);

				continue;
			}

			switch (Kind)
			{
				case ECodeTokenKind::mc_ByteOrderMark:
				case ECodeTokenKind::mc_Whitespace:
				case ECodeTokenKind::mc_Newline:
				case ECodeTokenKind::mc_LineSplice:
				case ECodeTokenKind::mc_Preprocessor:
					continue;
				default: break;
			}

			if (fp_IsBreakGap(i) || !fp_IsFirstOnLine(i))
				continue;

			auto nIndent = aint(fp_GetStatementIndent(i)) + _nDelta;
			if (nIndent >= 0)
				fp_IndentBefore(i, umint(nIndent));
		}
	}

	// 'if', 'else', 'for' and 'while' guard a statement whose braces the standard decides:
	// none around a statement on one line, braces around one written across lines or
	// behind a clause split across lines.
	bool CFormattingAnalyzer::fp_IsBraceGuard(umint _iGuard, bool &o_bClauseFits) const
	{
		auto const &Guard = m_Structure.f_GetNodes()[_iGuard];
		auto const &GuardFirst = m_Tokens.f_GetTokens()[Guard.m_iFirstToken];
		bool bClause = m_Tokens.f_IsText(GuardFirst, "if") || m_Tokens.f_IsText(GuardFirst, "for") || m_Tokens.f_IsText(GuardFirst, "while");
		if (!bClause && !m_Tokens.f_IsText(GuardFirst, "else"))
			return false;

		o_bClauseFits = !bClause || fp_FitsInline(Guard.m_iFirstToken, Guard.m_iLastToken, fp_GetStatementIndent(Guard.m_iFirstToken));

		return true;
	}

	// Whether the range is laid out as one line: brought onto one where it can be, and
	// otherwise written on one, since lines it fixes itself stay as they are.
	bool CFormattingAnalyzer::fp_IsRangeOneLine(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		if (fp_IsRangeJoinable(_iNode, _iFirst, _iLast))
			return fp_FitsInline(_iFirst, _iLast, _iIndent);

		// A block takes lines of its own wherever the source wrote it, so a range that
		// holds one, a lambda's body above all, is laid out across lines whether or not
		// the source has it on one.
		for (auto iChild : m_Structure.f_GetNodes()[_iNode].m_Children)
		{
			auto const &Child = m_Structure.f_GetNodes()[iChild];
			if (Child.m_iFirstToken < _iFirst || Child.m_iLastToken > _iLast)
				continue;

			if (Child.m_Kind == ECodeNodeKind::mc_Block || Child.m_bHasBlock)
				return false;
		}

		for (auto i = _iFirst; i <= _iLast; ++i)
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_Newline)
				return false;
		}

		return true;
	}

	// The braces around a single statement guarded by 'if', 'else', 'for' or 'while' are
	// dropped when the statement is laid out as one line, since the standard writes such
	// a statement without them. Only a block that holds exactly one statement ending in
	// ';' qualifies, and nothing but whitespace may stand between the braces and it: a
	// directive, a macro without a terminator, an empty statement, or a block inside
	// would each change what the source says or where it says it. A comment trailing the
	// statement on its line follows it out of the block. A nested 'if' is never one
	// statement to the builder, which keeps a dangling 'else' where it is.
	bool CFormattingAnalyzer::fp_DropBraces(umint _iStatement, umint _iGuard)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Guard = Nodes[_iGuard];
		bool bClauseFits = false;
		if (!fp_IsBraceGuard(_iGuard, bClauseFits) || !bClauseFits)
			return false;

		auto const &Statement = Nodes[_iStatement];
		if (Statement.m_Kind != ECodeNodeKind::mc_Statement || Statement.m_Children.f_GetLen() != 1)
			return false;

		auto const &Block = Nodes[Statement.m_Children[0]];
		if (Block.m_Kind != ECodeNodeKind::mc_Block || Block.m_iFirstToken != Statement.m_iFirstToken || Block.m_iLastToken != Statement.m_iLastToken)
			return false;

		if (Block.m_bHasDirective || Block.m_bHasMultiLineToken || Block.m_Children.f_GetLen() != 1)
			return false;

		auto const &Inner = Nodes[Block.m_Children[0]];
		if (Inner.m_Kind != ECodeNodeKind::mc_Statement || Inner.m_iFirstToken == Inner.m_iLastToken)
			return false;

		if (m_Tokens.f_IsText(Tokens[Inner.m_iFirstToken], "{") || !m_Tokens.f_IsText(Tokens[Inner.m_iLastToken], ";"))
			return false;

		auto fOnlyWhitespace = [&](umint _iFrom, umint _iTo)
			{
				for (auto i = _iFrom; i < _iTo; ++i)
				{
					auto Kind = Tokens[i].m_Kind;
					if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
						return false;
				}

				return true;
			}
		;
		if (!fOnlyWhitespace(Guard.m_iLastToken + 1, Block.m_iFirstToken) || !fOnlyWhitespace(Block.m_iFirstToken + 1, Inner.m_iFirstToken))
			return false;

		auto nTab = m_Request.m_Settings.m_nTabWidth;
		// The 'if' of an 'else if' stands behind the 'else', whose line the braces go under.
		auto iElse = fp_LeadingElse(Guard.m_iFirstToken);
		auto nGuardIndent = fp_GetStatementIndent(iElse >= 0 ? umint(iElse) : Guard.m_iFirstToken);
		if (!fp_IsRangeOneLine(Block.m_Children[0], Inner.m_iFirstToken, Inner.m_iLastToken, nGuardIndent + nTab))
			return false;

		auto iCloseStart = Tokens[Inner.m_iLastToken].f_GetEnd();
		bool bTrailingComment = false;
		for (auto i = Inner.m_iLastToken + 1; i < Block.m_iLastToken; ++i)
		{
			auto Kind = Tokens[i].m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline)
				continue;

			bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
			if (!bComment || bTrailingComment || Tokens[i].m_bMultiLine)
				return false;

			bool bOnStatementLine = true;
			for (auto iGap = Inner.m_iLastToken + 1; iGap < i; ++iGap)
				bOnStatementLine &= Tokens[iGap].m_Kind != ECodeTokenKind::mc_Newline;

			if (!bOnStatementLine)
				return false;

			bTrailingComment = true;
			iCloseStart = Tokens[i].f_GetEnd();
		}

		// A comment behind the closing brace would land on the statement's line, so the
		// brace must end its line, or be followed by the 'else' the layout moves down; that
		// 'else' then starts a line of its own here, so a trailing comment never swallows it.
		auto iAfter = fp_NextCode(Block.m_iLastToken);
		bool bLastOnLine = fp_IsLastOnLine(Block.m_iLastToken);
		bool bElseFollows = iAfter >= 0 && m_Tokens.f_IsText(Tokens[umint(iAfter)], "else");
		if (!bLastOnLine && !bElseFollows)
			return false;

		auto iOpenStart = Tokens[Guard.m_iLastToken].f_GetEnd();
		auto nOpen = Tokens[Inner.m_iFirstToken].m_iOffset - iOpenStart;
		auto iCloseEnd = bLastOnLine ? Tokens[Block.m_iLastToken].f_GetEnd() : Tokens[umint(iAfter)].m_iOffset;
		auto nClose = iCloseEnd - iCloseStart;
		if (fp_IsDisabled(iOpenStart, nOpen) || !fp_IsSelected(iOpenStart, nOpen) || fp_IsDisabled(iCloseStart, nClose) || !fp_IsSelected(iCloseStart, nClose))
			return false;

		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());
		auto &Open = m_Structural.f_Insert();
		Open.m_iOffset = iOpenStart;
		Open.m_nLength = nOpen;
		Open.m_Replacement = Ending + fp_MakeIndent(nGuardIndent + nTab);
		Open.m_Rule = "braces";
		auto &Close = m_Structural.f_Insert();
		Close.m_iOffset = iCloseStart;
		Close.m_nLength = nClose;
		if (!bLastOnLine)
			Close.m_Replacement = Ending + fp_MakeIndent(nGuardIndent);

		Close.m_Rule = "braces";

		return true;
	}

	// Braces are put around a single guarded statement that is laid out across lines, or
	// that a clause split across lines guards, as the standard requires. The statement
	// must end in ';', so a macro without a terminator is left alone, and only whitespace
	// may stand between the guard and it. An attribute in front of the statement stays
	// on the clause's line, and the brace opens behind it. A comment trailing the
	// statement's last line stays there, in front of the closing brace.
	bool CFormattingAnalyzer::fp_AddBraces(umint _iStatement, umint _iGuard)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Guard = Nodes[_iGuard];
		bool bClauseFits = false;
		if (!fp_IsBraceGuard(_iGuard, bClauseFits))
			return false;

		auto const &Statement = Nodes[_iStatement];
		if (Statement.m_Kind != ECodeNodeKind::mc_Statement || Statement.m_iFirstToken == Statement.m_iLastToken)
			return false;

		if (!m_Tokens.f_IsText(Tokens[Statement.m_iLastToken], ";") || m_Tokens.f_IsText(Tokens[Statement.m_iFirstToken], "{"))
			return false;

		auto nTab = m_Request.m_Settings.m_nTabWidth;
		// The 'if' of an 'else if' stands behind the 'else', whose line the braces go under.
		auto iElse = fp_LeadingElse(Guard.m_iFirstToken);
		auto nGuardIndent = fp_GetStatementIndent(iElse >= 0 ? umint(iElse) : Guard.m_iFirstToken);

		// The brace opens behind an attribute on the clause's line.
		auto iOpenAfter = Guard.m_iLastToken;
		auto iFirst = Statement.m_iFirstToken;
		auto iSecond = fp_NextCode(iFirst);
		if (m_Tokens.f_IsText(Tokens[iFirst], "[") && iSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSecond)], "["))
		{
			for (auto iChild : Statement.m_Children)
			{
				if (Nodes[iChild].m_iFirstToken != iFirst)
					continue;

				iOpenAfter = Nodes[iChild].m_iLastToken;
				auto iNext = fp_NextCode(iOpenAfter);
				if (iNext < 0 || umint(iNext) > Statement.m_iLastToken)
					return false;

				iFirst = umint(iNext);

				break;
			}

			if (iOpenAfter == Guard.m_iLastToken)
				return false;
		}

		// Behind the attribute the statement can be a block already: 'if (a) [[unlikely]] {'.
		// The builder then reads the 'else' behind that block as more of the statement, and
		// braces around the two would take the 'else' away from its clause. No statement
		// that holds one at its own level is ever wrapped.
		if (m_Tokens.f_IsText(Tokens[iFirst], "{"))
			return false;

		for (auto i = iFirst; i <= Statement.m_iLastToken; ++i)
		{
			if (m_TokenDepth[i] == m_TokenDepth[iFirst] && m_Tokens.f_IsText(Tokens[i], "else"))
				return false;
		}

		if (bClauseFits && fp_IsRangeOneLine(_iStatement, iFirst, Statement.m_iLastToken, nGuardIndent + nTab))
			return false;

		for (auto i = iOpenAfter + 1; i < iFirst; ++i)
		{
			auto Kind = Tokens[i].m_Kind;
			if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
				return false;
		}

		auto iCloseAt = Tokens[Statement.m_iLastToken].f_GetEnd();
		for (auto i = Statement.m_iLastToken + 1; i < Tokens.f_GetLen(); ++i)
		{
			auto Kind = Tokens[i].m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace)
				continue;

			if ((Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment) && !Tokens[i].m_bMultiLine)
			{
				iCloseAt = Tokens[i].f_GetEnd();

				continue;
			}

			break;
		}

		auto iOpenStart = Tokens[iOpenAfter].f_GetEnd();
		auto nOpen = Tokens[iFirst].m_iOffset - iOpenStart;
		if (fp_IsDisabled(iOpenStart, nOpen) || !fp_IsSelected(iOpenStart, nOpen) || fp_IsDisabled(iCloseAt, 0) || !fp_IsSelected(iCloseAt, 0))
			return false;

		auto Ending = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());
		auto &Open = m_Structural.f_Insert();
		Open.m_iOffset = iOpenStart;
		Open.m_nLength = nOpen;
		Open.m_Replacement = Ending + fp_MakeIndent(nGuardIndent) + "{" + Ending + fp_MakeIndent(nGuardIndent + nTab);
		Open.m_Rule = "braces";
		auto &Close = m_Structural.f_Insert();
		Close.m_iOffset = iCloseAt;
		Close.m_nLength = 0;
		Close.m_Replacement = Ending + fp_MakeIndent(nGuardIndent) + "}";
		Close.m_Rule = "braces";

		return true;
	}

	// A clause that ends at its condition, and a keyword that only introduces the
	// statement after it, guard that statement.
	bool CFormattingAnalyzer::fp_IsGuard(umint _iNode) const
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_Kind != ECodeNodeKind::mc_Statement)
			return false;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &First = Tokens[Node.m_iFirstToken];
		if (Node.m_iFirstToken == Node.m_iLastToken)
			return m_Tokens.f_IsText(First, "else") || m_Tokens.f_IsText(First, "do") || m_Tokens.f_IsText(First, "try");

		bool bClause = m_Tokens.f_IsText(First, "if")
			|| m_Tokens.f_IsText(First, "for")
			|| m_Tokens.f_IsText(First, "while")
			|| m_Tokens.f_IsText(First, "switch")
			|| m_Tokens.f_IsText(First, "catch")
		;

		return bClause && m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ")");
	}

	// Every brace of a block and every statement in it takes a line of its own: the
	// statements at the block's level, what a clause guards one level in, and a guarded
	// block at the clause's own level. Only what shares a line is moved; the depth of a
	// line the source already starts is not the layout's to decide, so the level a moved
	// statement takes is read from the lines around it. A case that shares its label's
	// line is left there, since 'case 1: return 1;' is written that way on purpose, and so
	// is the 'if' of an 'else if', and an attribute on a clause's line. Behind a closing
	// brace only a keyword starts a statement of its own; a name there declares a variable
	// of the type just defined.
	// True when the statement writes its own lines, so its first one cannot be moved
	// without leaving the rest of them where they are. A block takes its lines along, so
	// what stands inside one says nothing about the statement around it.
	bool CFormattingAnalyzer::fp_KeepsOwnLines(umint _iNode) const
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_bFixedLineBreaks || Node.m_bHasMultiLineToken || Node.m_bHasMultiLineBrace)
			return true;

		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto i = Node.m_iFirstToken; i <= Node.m_iLastToken; ++i)
		{
			if (i < m_iBlockEnd.f_GetLen() && m_iBlockEnd[i] <= Node.m_iLastToken)
			{
				i = m_iBlockEnd[i];

				continue;
			}

			if (Tokens[i].m_Kind == ECodeTokenKind::mc_BlockComment)
				return true;
		}

		return false;
	}

	void CFormattingAnalyzer::fp_LayoutBlockLines(umint _iNode, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		bool bBraced = Node.m_Kind == ECodeNodeKind::mc_Block;
		// A conditional whose branches cut a construct leaves the statements around it no
		// depth of their own, since the token stream runs through every branch at once. Such
		// a block keeps the depths the source gave its lines.
		bool bDepths = !fp_HasOpaqueDirective(Node.m_iFirstToken, Node.m_iLastToken);
		// The file's scope starts before anything in it, the directives in front of its first
		// statement included.
		umint iScopeFirst = bBraced ? Node.m_iFirstToken : 0;
		if (bBraced)
		{
			if (!fp_IsFirstOnLine(Node.m_iLastToken))
				fp_OwnLineBefore(Node.m_iLastToken, _iIndent);
			else if (bDepths && !fp_IsInConditionalWithin(Node.m_iLastToken, iScopeFirst) && fp_GetStatementIndent(Node.m_iLastToken) != _iIndent)
				fp_IndentBefore(Node.m_iLastToken, _iIndent);
		}

		constexpr ch8 const *c_pStatementKeywords[] =
			{
				"else", "while", "catch", "if", "for", "do", "switch", "try", "return", "co_return", "break", "continue", "goto", "case", "default", "{"
			}
		;
		umint nLevel = bBraced ? _iIndent + nTab : _iIndent;
		umint nPlaced = nLevel;
		bool bEnumBody = false;
		if (bBraced && Node.m_iParent < Nodes.f_GetLen() && Nodes[Node.m_iParent].m_Kind == ECodeNodeKind::mc_Statement)
			bEnumBody = m_Tokens.f_IsText(Tokens[fp_SkipTemplateHeader(Nodes[Node.m_iParent].m_iFirstToken)], "enum");

		// A comma a directive stands behind is one the conversion could not move in front of
		// the enumerator after it, and the enum keeps all of its commas behind its enumerators.
		bool bEnumTrailingCommas = false;
		for (auto i = Node.m_iFirstToken + 1; bEnumBody && i < Node.m_iLastToken && !bEnumTrailingCommas; ++i)
		{
			if (!m_Tokens.f_IsText(Tokens[i], ","))
				continue;

			auto iNext = fp_NextCode(i);
			for (auto iGap = i + 1; iNext >= 0 && iGap < umint(iNext) && !bEnumTrailingCommas; ++iGap)
				bEnumTrailingCommas = Tokens[iGap].m_Kind == ECodeTokenKind::mc_Preprocessor;
		}
		// The clause whose statement is still to come, and the depth that clause was
		// written at: what it guards stands one level in from there.
		umint iGuard = TCLimitsInt<umint>::mc_Max;
		umint nGuard = 0;
		umint iPrevious = TCLimitsInt<umint>::mc_Max;
		bool bOnLabelLine = false;
		auto fIsCaseLabel = [&](umint _iIndex)
			{
				if (_iIndex >= Node.m_Children.f_GetLen())
					return false;

				auto const &Label = Nodes[Node.m_Children[_iIndex]];

				return m_Tokens.f_IsText(Tokens[Label.m_iLastToken], ":")
					&& (m_Tokens.f_IsText(Tokens[Label.m_iFirstToken], "case") || m_Tokens.f_IsText(Tokens[Label.m_iFirstToken], "default"))
				;
			}
		;
		// A case whose last statement does not jump falls through to the label behind it. A
		// macro the naming lists as never returning jumps as 'throw' does.
		auto fFallsThrough = [&](umint _iLast)
			{
				if (!fIsCaseLabel(_iLast + 1))
					return false;

				constexpr ch8 const *c_pJumps[] =
					{
						"break", "return", "co_return", "continue", "throw", "goto"
					}
				;
				auto const &Last = Tokens[Nodes[Node.m_Children[_iLast]].m_iFirstToken];
				if (m_Tokens.f_HasRole(Last, ECodeNameRole::mc_NoReturnMacro))
					return false;

				for (auto pJump : c_pJumps)
				{
					if (m_Tokens.f_IsText(Last, pJump))
						return false;
				}

				return true;
			}
		;
		// The last statement a label took onto its line, and the label itself.
		umint iJoinedLabel = TCLimitsInt<umint>::mc_Max;
		umint iJoinedUntil = TCLimitsInt<umint>::mc_Max;
		umint iChildIndex = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			++iChildIndex;
			auto const &Child = Nodes[iChild];
			auto iFirst = Child.m_iFirstToken;
			auto const &First = Tokens[iFirst];
			bool bFirstOnLine = fp_IsFirstOnLine(iFirst);
			bool bGuarded = iGuard != TCLimitsInt<umint>::mc_Max;
			bool bLabelled = false;
			bool bAfterBlock = false;
			if (iPrevious != TCLimitsInt<umint>::mc_Max)
			{
				auto const &Previous = Nodes[iPrevious];
				bLabelled = m_Tokens.f_IsText(Tokens[Previous.m_iLastToken], ":");
				bAfterBlock = m_Tokens.f_IsText(Tokens[Previous.m_iLastToken], "}");
			}

			// A case written on its label's line stays there where it is one statement and no
			// block, a 'break' behind it included: 'case 1: a = 1; break;'. A block, or more
			// statements than that, takes lines of its own under the label like any other body.
			bool bSharedLabel = bLabelled && iChildIndex >= 2 && fIsCaseLabel(iChildIndex - 2);

			if (bLabelled && !bFirstOnLine && bSharedLabel)
				bOnLabelLine = false;
			else if (bLabelled && !bFirstOnLine)
			{
				umint nOnLine = 0;
				bool bBlockOnLine = false;
				umint iLastOnLine = iChildIndex;
				for (auto iOther = iChildIndex; iOther < Node.m_Children.f_GetLen(); ++iOther)
				{
					auto const &Other = Nodes[Node.m_Children[iOther]];
					if (iOther > iChildIndex && fp_IsFirstOnLine(Other.m_iFirstToken))
						break;

					bool bBreak = iOther > iChildIndex && m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "break");
					nOnLine += !bBreak;
					bBlockOnLine |= Other.m_Kind == ECodeNodeKind::mc_Block || m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "{");
					iLastOnLine = iOther;
				}

				bOnLabelLine = nOnLine == 1 && !bBlockOnLine && !fFallsThrough(iLastOnLine);
			}
			else
				bOnLabelLine = !bFirstOnLine && bOnLabelLine;
			// The clause the statement stands directly behind, which is the one whose braces
			// the standard decides; an attribute on its line stands between the two.
			bool bDirectlyGuarded = bGuarded && iPrevious == iGuard;
			bool bElseIf = bDirectlyGuarded && m_Tokens.f_IsText(Tokens[Nodes[iPrevious].m_iFirstToken], "else") && m_Tokens.f_IsText(First, "if");
			if (m_bProbing && m_bAllowConversions && bDirectlyGuarded && !bElseIf)
			{
				if (m_Tokens.f_IsText(First, "{"))
					fp_DropBraces(iChild, iPrevious);
				else
					fp_AddBraces(iChild, iPrevious);
			}

			// An attribute is written on the clause's line: 'if (x) [[unlikely]]'.
			auto iSecond = fp_NextCode(iFirst);
			bool bAttribute = m_Tokens.f_IsText(First, "[") && iSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSecond)], "[");
			// A label stands one level out from the statements written under it, whether it
			// is a case, an access specifier or a target to jump to.
			bool bLabel = m_Tokens.f_IsText(Tokens[Child.m_iLastToken], ":");
			umint nPlace = nLevel;
			if (bGuarded)
				nPlace = m_Tokens.f_IsText(First, "{") ? nGuard : nGuard + nTab;
			else if (bLabel && nLevel >= nTab)
				nPlace = nLevel - nTab;

			// An enumerator list is no statement, but takes the line under the enum's brace as one
			// does, where it is one line to move.
			bool bEnumerators = false;
			if (bEnumBody && !bFirstOnLine && iPrevious == TCLimitsInt<umint>::mc_Max)
			{
				auto iChildLast = fp_PreviousCode(fg_Min(Child.m_iLastToken, Node.m_iLastToken - 1) + 1);
				bEnumerators = iChildLast >= aint(iFirst) && !fp_SpansLines(iFirst, umint(iChildLast));
			}

			// A case whose body is one statement, a 'break' behind it included, takes it onto
			// its label's line where the whole fits there: 'case 1: a = 1; break;'. A block, a
			// clause and a comment keep the body under the label.
			bool bJoinedToLabel = iJoinedLabel != TCLimitsInt<umint>::mc_Max && iChildIndex > iJoinedLabel && iChildIndex <= iJoinedUntil;
			// A body that labels in front of this one fall through to stays under all of them.
			bool bCaseLabel = bLabel && (m_Tokens.f_IsText(First, "case") || m_Tokens.f_IsText(First, "default"));
			if (bCaseLabel && !bLabelled && !bGuarded && Child.m_Kind == ECodeNodeKind::mc_Statement)
			{
				umint iBodyLast = iChildIndex;
				umint nStatements = 0;
				bool bSimple = true;
				for (auto iOther = iChildIndex + 1; iOther < Node.m_Children.f_GetLen() && bSimple; ++iOther)
				{
					auto const &Other = Nodes[Node.m_Children[iOther]];
					auto const &OtherFirst = Tokens[Other.m_iFirstToken];
					if (m_Tokens.f_IsText(Tokens[Other.m_iLastToken], ":") || m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "}"))
						break;

					bool bBreak = m_Tokens.f_IsText(OtherFirst, "break");
					if (bBreak && nStatements <= 1)
					{
						iBodyLast = iOther;

						break;
					}

					constexpr ch8 const *c_pCompound[] =
						{
							"if", "for", "while", "switch", "do", "else", "try", "{", "case", "default"
						}
					;
					bool bCompound = false;
					for (auto pKeyword : c_pCompound)
						bCompound |= m_Tokens.f_IsText(OtherFirst, pKeyword);

					bSimple = Other.m_Kind == ECodeNodeKind::mc_Statement && !bCompound && ++nStatements == 1;
					iBodyLast = iOther;
				}

				auto iLast = Nodes[Node.m_Children[iBodyLast]].m_iLastToken;
				// A blank line between the body's statements is one its author put there; one
				// behind the label goes anyway.
				umint nNewlines = 0;
				bool bBlank = false;
				auto iBodyFirst = iBodyLast > iChildIndex ? Nodes[Node.m_Children[iChildIndex + 1]].m_iFirstToken : iLast;
				for (auto i = iBodyFirst + 1; i < iLast && !bBlank; ++i)
				{
					if (Tokens[i].m_Kind == ECodeTokenKind::mc_Newline)
						bBlank = ++nNewlines > 1;
					else if (Tokens[i].m_Kind != ECodeTokenKind::mc_Whitespace)
						nNewlines = 0;
				}

				bool bJoins = bSimple
					&& !bBlank
					&& !fFallsThrough(iBodyLast)
					&& iBodyLast > iChildIndex
					&& nStatements <= 1
					&& fp_IsFirstOnLine(Nodes[Node.m_Children[iChildIndex + 1]].m_iFirstToken)
					&& fp_IsRangeJoinable(_iNode, Child.m_iFirstToken, iLast)
					&& fp_FitsInline(Child.m_iFirstToken, iLast, nPlace)
				;
				if (bJoins)
				{
					fp_MarkInline(Child.m_iLastToken, iLast);
					iJoinedLabel = iChildIndex;
					iJoinedUntil = iBodyLast;
				}
			}

			bool bStays = (Child.m_Kind == ECodeNodeKind::mc_Unsupported && !bEnumerators) || bOnLabelLine || bElseIf || bAttribute || bJoinedToLabel || m_Tokens.f_IsText(First, ";");

			// Each enumerator takes a line of its own, the comma in front of it: 'EA' over ', EB',
			// or behind the one in front of it in an enum holding a directive. A comma with a
			// comment or a directive behind it keeps its place, and so does a trailing one,
			// which ends the list rather than starting an enumerator.
			if (bEnumBody && iPrevious == TCLimitsInt<umint>::mc_Max)
			{
				for (auto i = iFirst; i < Node.m_iLastToken && i <= Child.m_iLastToken; ++i)
				{
					if (m_TokenDepth[i] != m_TokenDepth[iFirst] || !m_Tokens.f_IsText(Tokens[i], ",") || fp_IsFirstOnLine(i))
						continue;

					auto iNextCode = fp_NextCode(i);
					bool bTrailing = iNextCode < 0 || umint(iNextCode) >= Node.m_iLastToken;
					bool bTrivia = false;
					for (auto iBetween = i + 1; !bTrailing && iBetween < umint(iNextCode) && !bTrivia; ++iBetween)
					{
						auto Kind = Tokens[iBetween].m_Kind;
						bTrivia = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor;
					}

					if (bTrivia || bTrailing)
						continue;

					if (bEnumTrailingCommas)
					{
						if (!fp_IsLastOnLine(i))
							fp_OwnLineBefore(umint(iNextCode), nPlace);

						continue;
					}

					// A comma ending its line moves to the front of the enumerator behind it, and a
					// blank line in front of that enumerator moves in front of the comma.
					fp_OwnLineBefore(i, nPlace);
					if (fp_IsLastOnLine(i))
					{
						fp_MarkInline(i, umint(iNextCode));
						umint nNewlines = 0;
						for (auto iBetween = i + 1; iBetween < umint(iNextCode); ++iBetween)
							nNewlines += Tokens[iBetween].m_Kind == ECodeTokenKind::mc_Newline;

						if (nNewlines > 1 && i < m_bBlankBefore.f_GetLen())
							m_bBlankBefore[i] = 1;
					}
				}
			}

			// Behind a closing brace only a keyword starts a statement of its own, since a
			// name there declares a variable of the type just defined.
			if (!bStays && !bFirstOnLine && bAfterBlock)
			{
				bool bKeyword = false;
				for (auto pKeyword : c_pStatementKeywords)
					bKeyword |= m_Tokens.f_IsText(First, pKeyword);

				bStays = !bKeyword;
			}

			umint nWritten = bFirstOnLine ? fp_GetStatementIndent(iFirst) : nPlaced;
			if (!bStays && !bFirstOnLine)
			{
				fp_OwnLineBefore(iFirst, nPlace);
				nWritten = nPlace;
			}
			else if (!bStays && bDepths && !fp_IsInConditionalWithin(iFirst, iScopeFirst) && nWritten != nPlace && !fp_KeepsOwnLines(iChild))
			{
				fp_IndentBefore(iFirst, nPlace);
				nWritten = nPlace;
			}

			if (fp_IsGuard(iChild))
			{
				iGuard = iChild;
				nGuard = nWritten;
			}
			else
				iGuard = TCLimitsInt<umint>::mc_Max;

			nPlaced = nWritten;
			iPrevious = iChild;
		}
	}

	void CFormattingAnalyzer::fp_LayoutNode(umint _iNode, umint _iIndent)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		switch (Node.m_Kind)
		{
			case ECodeNodeKind::mc_File:
			{
				fp_LayoutBlockLines(_iNode, 0);
				for (auto iChild : Node.m_Children)
					fp_LayoutNode(iChild, 0);

				return;
			}
			case ECodeNodeKind::mc_Block:
			{
				// A block whose brace still shares a line has no level to place its lines at;
				// the head that owns the brace decides where it goes first.
				if (fp_IsFirstOnLine(Node.m_iFirstToken))
					fp_LayoutBlockLines(_iNode, fp_GetStatementIndent(Node.m_iFirstToken));

				auto nTab = m_Request.m_Settings.m_nTabWidth;
				for (auto iChild : Node.m_Children)
					fp_LayoutNode(iChild, _iIndent + nTab);

				return;
			}
			case ECodeNodeKind::mc_Unsupported: return;
			default: break;
		}

		if (Node.m_Kind != ECodeNodeKind::mc_Statement)
			return;

		// A statement that opens with an attribute on its clause's line, 'if (x)
		// [[unlikely]]' with the block below, has no line of its own to be measured
		// from: it is laid out at the clause's indentation, where its block opens. The
		// 'if' of an 'else if' is laid out against the line the 'else' starts.
		auto iElse = fp_LeadingElse(Node.m_iFirstToken);
		auto nIndent = fp_GetStatementIndent(iElse >= 0 ? umint(iElse) : Node.m_iFirstToken);
		auto iSecond = fp_NextCode(Node.m_iFirstToken);
		bool bAttribute = m_Tokens.f_IsText(m_Tokens.f_GetTokens()[Node.m_iFirstToken], "[")
			&& iSecond >= 0
			&& m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iSecond)], "[")
		;
		if (bAttribute && !fp_IsFirstOnLine(Node.m_iFirstToken) && Node.m_iParent < Nodes.f_GetLen())
		{
			umint iPrevious = TCLimitsInt<umint>::mc_Max;
			for (auto iSibling : Nodes[Node.m_iParent].m_Children)
			{
				if (iSibling == _iNode)
					break;

				iPrevious = iSibling;
			}

			if (iPrevious != TCLimitsInt<umint>::mc_Max && fp_IsGuard(iPrevious))
				nIndent = fp_GetStatementIndent(Nodes[iPrevious].m_iFirstToken);
		}

		fp_LayoutStatement(_iNode, nIndent);
	}

	void CFormattingAnalyzer::fp_RuleLineBreaks()
	{
		// Every gap starts out keeping what the source has, so the rules that read the
		// decisions have them to read even where no layout is made.
		auto nTokens = m_Tokens.f_GetTokens().f_GetLen();
		m_GapState.f_SetLen(nTokens);
		m_GapIndent.f_SetLen(nTokens);
		m_bCommentMoved.f_SetLen(nTokens);
		m_CommentIndent.f_SetLen(nTokens);
		for (umint i = 0; i < nTokens; ++i)
		{
			m_GapState[i] = uint8(EGap::mc_Keep);
			m_GapIndent[i] = 0;
			m_bCommentMoved[i] = 0;
			m_CommentIndent[i] = 0;
		}

		// Brackets that do not nest as written make every line position a guess, so the
		// file keeps its layout. Say so rather than silently leaving it unformatted.
		if (!m_Structure.f_IsComplete())
		{
			// A conditional whose branches each spell a piece of one construct is the one
			// shape where brackets that do not nest is what the source means, so it is
			// named as itself rather than reported as the construct it left open.
			bool bConditional = !m_Conditionals.f_IsEmpty();
			if (!m_bProbing)
			{
				fp_AddDiagnostic
					(
						"structure"
						, bConditional ? m_Conditionals[0].m_iOffset : m_Structure.f_GetIncompleteOffset()
						, 0
						, bConditional
							? "the branches of this conditional each hold a piece of one construct, so the file's line structure was left alone"
							: "this construct's brackets do not nest as written, so the file's line structure was left alone"
						, false
					)
				;
			}

			return;
		}

		fp_PrepareBlockEnds();
		fp_LayoutNode(0, 0);

		// A braced list whose elements all stand on its opening brace's line was never
		// opened, so a closing brace left on a line of its own goes back behind the last
		// of them: '{1, 0' over '}' is '{1, 0}'. An opened list around it keeps its lines,
		// which is what would leave such a brace where it is.
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto const &Node : m_Structure.f_GetNodes())
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Brace || !fp_IsFirstOnLine(Node.m_iLastToken))
				continue;

			auto iLast = fp_PreviousCode(Node.m_iLastToken);
			if (iLast < 0 || umint(iLast) <= Node.m_iFirstToken)
				continue;

			// Only a list the layout left as the source wrote it, closing brace included.
			bool bOneLine = EGap(m_GapState[Node.m_iLastToken]) == EGap::mc_Keep;
			for (auto i = Node.m_iFirstToken + 1; i < Node.m_iLastToken && bOneLine; ++i)
			{
				if (fp_IsBreakGap(i))
				{
					bOneLine = false;

					break;
				}

				auto Kind = Tokens[i].m_Kind;
				if (i > umint(iLast))
					bOneLine = Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline;
				else
					bOneLine = Kind != ECodeTokenKind::mc_Newline && Kind != ECodeTokenKind::mc_LineComment && Kind != ECodeTokenKind::mc_Preprocessor;
			}

			if (bOneLine && !Tokens[umint(iLast)].m_bMultiLine)
				fp_MarkInline(umint(iLast), Node.m_iLastToken);
		}
	}

	void CFormattingAnalyzer::fp_PrepareBlockEnds()
	{
		auto nTokens = m_Tokens.f_GetTokens().f_GetLen();
		m_iBlockEnd.f_SetLen(nTokens);
		for (auto &iEnd : m_iBlockEnd)
			iEnd = nTokens;

		for (auto const &Node : m_Structure.f_GetNodes())
		{
			if (Node.m_Kind == ECodeNodeKind::mc_Block && Node.m_Bracket == ECodeBracket::mc_Brace)
				m_iBlockEnd[Node.m_iFirstToken] = Node.m_iLastToken;
		}
	}

	// The name the directive spells, such as 'if' or 'endif'.
	CStr CFormattingAnalyzer::fp_GetDirectiveKeyword(umint _iToken) const
	{
		auto const &Token = m_Tokens.f_GetTokens()[_iToken];
		auto pText = m_Request.m_Source.f_GetStr() + Token.m_iOffset;
		umint i = 0;
		while (i < Token.m_nLength && pText[i] != '#')
			++i;

		++i;
		while (i < Token.m_nLength && fg_IsSpaceOrTab(pText[i]))
			++i;

		umint iStart = i;
		while (i < Token.m_nLength && pText[i] >= 'a' && pText[i] <= 'z')
			++i;

		return CStr(pText + iStart, i - iStart);
	}

	// True when nothing the directive stands in the middle of is cut by it: every construct
	// spanning the directive spans the whole conditional group, so the branches are
	// alternatives inside one construct rather than pieces of one spread over several.
	bool CFormattingAnalyzer::fp_IsDirectiveParallel(umint _iDirective, umint _iOpen, umint _iClose) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		if (Nodes.f_IsEmpty())
			return false;

		umint iNode = 0;
		for (;;)
		{
			auto const &Node = Nodes[iNode];
			// A directive is trivia, so a node never starts or ends on one: a node spanning
			// the group has its first token in front of the opening directive and its last
			// behind the closing one.
			if (iNode && (Node.m_iFirstToken > _iOpen || Node.m_iLastToken < _iClose))
				return false;

			umint iNext = iNode;
			for (auto iChild : Node.m_Children)
			{
				if (Nodes[iChild].m_iFirstToken < _iDirective && Nodes[iChild].m_iLastToken > _iDirective)
				{
					iNext = iChild;

					break;
				}
			}

			if (iNext == iNode)
			{
				// A branch that ends on a clause leaves the statement it guards to the next
				// one, which is a construct cut in two that no node shows: the builder ends
				// a clause at its condition, so the pieces are separate children.
				umint iBefore = TCLimitsInt<umint>::mc_Max;
				for (auto iChild : Node.m_Children)
				{
					if (Nodes[iChild].m_iLastToken < _iDirective)
						iBefore = iChild;
				}

				return iBefore == TCLimitsInt<umint>::mc_Max || !fp_IsGuard(iBefore);
			}

			iNode = iNext;
		}
	}

	// A conditional group holds alternatives: the token stream reads its branches one after
	// another, which is the same shape as any single branch only where no construct is cut
	// by a branch boundary. Where one is, every directive of the group is opaque and the
	// construct around it keeps the lines the source gave it, as before. A directive that
	// is transparent only fixes the line it stands on, exactly as a line comment does.
	void CFormattingAnalyzer::fp_PrepareDirectives()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		m_bOpaqueDirective.f_SetLen(Tokens.f_GetLen());
		for (auto &bOpaque : m_bOpaqueDirective)
			bOpaque = 0;

		struct CConditional
		{
			TCVector<umint> m_Directives;
			bool m_bClosed = false;
		};
		TCVector<CConditional> Conditionals;
		TCVector<umint> Open;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			auto Keyword = fp_GetDirectiveKeyword(i);
			if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
			{
				Open.f_Insert(Conditionals.f_GetLen());
				Conditionals.f_Insert().m_Directives.f_Insert(i);

				continue;
			}

			bool bBranch = Keyword == "else" || Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef";
			if (!bBranch && Keyword != "endif")
				continue;

			// A branch or an end whose opening directive the file does not hold leaves the
			// group unknown, so nothing may be read across it.
			if (Open.f_IsEmpty())
			{
				m_bOpaqueDirective[i] = 1;

				continue;
			}

			auto &Conditional = Conditionals[Open.f_GetLast()];
			Conditional.m_Directives.f_Insert(i);
			if (!bBranch)
			{
				Conditional.m_bClosed = true;
				Open.f_Remove(Open.f_GetLen() - 1);
			}
		}

		for (auto const &Conditional : Conditionals)
		{
			auto const &Directives = Conditional.m_Directives;
			// A branch that opens a bracket it does not close, or closes one it did not
			// open, spells a piece of a construct rather than a whole alternative. The
			// token stream then holds no construct the file ever compiles, and the file is
			// reported as such rather than laid out against a shape nothing has.
			bool bBalanced = Conditional.m_bClosed;
			for (umint iBranch = 0; bBalanced && iBranch + 1 < Directives.f_GetLen(); ++iBranch)
			{
				aint nDepth = 0;
				aint nLowest = 0;
				for (auto i = Directives[iBranch] + 1; i < Directives[iBranch + 1]; ++i)
				{
					auto const &Token = Tokens[i];
					if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
						continue;

					if (m_Tokens.f_IsText(Token, "(") || m_Tokens.f_IsText(Token, "[") || m_Tokens.f_IsText(Token, "{"))
						++nDepth;
					else if (m_Tokens.f_IsText(Token, ")") || m_Tokens.f_IsText(Token, "]") || m_Tokens.f_IsText(Token, "}"))
					{
						--nDepth;
						nLowest = nDepth < nLowest ? nDepth : nLowest;
					}
				}

				bBalanced = !nDepth && !nLowest;
			}

			bool bParallel = bBalanced;
			for (umint i = 0; bParallel && i < Directives.f_GetLen(); ++i)
				bParallel = fp_IsDirectiveParallel(Directives[i], Directives[0], Directives.f_GetLast());

			if (bParallel)
				continue;

			if (!bBalanced)
			{
				auto &Span = m_Conditionals.f_Insert();
				Span.m_iOffset = Tokens[Directives[0]].m_iOffset;
				Span.m_nLength = Tokens[Directives.f_GetLast()].f_GetEnd() - Span.m_iOffset;
			}

			for (auto iDirective : Directives)
				m_bOpaqueDirective[iDirective] = 1;
		}

		m_nOpaqueBefore.f_SetLen(Tokens.f_GetLen() + 1);
		m_nOpaqueBefore[0] = 0;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
			m_nOpaqueBefore[i + 1] = m_nOpaqueBefore[i] + m_bOpaqueDirective[i];

		// The conditional each token stands in, by its opening directive. The depth the sources
		// give the lines directly inside one varies with the file, so the standard does not
		// settle it and they keep theirs.
		m_iConditionalOpen.f_SetLen(Tokens.f_GetLen());
		TCVector<umint> OpenConditionals;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
			{
				auto Keyword = fp_GetDirectiveKeyword(i);
				if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
					OpenConditionals.f_Insert(i);
				else if (Keyword == "endif" && !OpenConditionals.f_IsEmpty())
					OpenConditionals.f_Remove(OpenConditionals.f_GetLen() - 1);
			}

			m_iConditionalOpen[i] = OpenConditionals.f_IsEmpty() ? TCLimitsInt<umint>::mc_Max : OpenConditionals.f_GetLast();
		}
	}

	bool CFormattingAnalyzer::fp_SpansLines(umint _iFirst, umint _iLast) const
	{
		return m_Lines.f_FindLine(m_Tokens.f_GetTokens()[_iFirst].m_iOffset) != m_Lines.f_FindLine(m_Tokens.f_GetTokens()[_iLast].f_GetEnd() - 1);
	}

	// Whether the token stands in a conditional that opens inside the scope: one around the
	// whole scope leaves the depths inside it to the scope's own braces.
	bool CFormattingAnalyzer::fp_IsInConditionalWithin(umint _iToken, umint _iScopeFirst) const
	{
		if (_iToken >= m_iConditionalOpen.f_GetLen())
			return false;

		auto iOpen = m_iConditionalOpen[_iToken];

		return iOpen != TCLimitsInt<umint>::mc_Max && iOpen > _iScopeFirst;
	}

	// A node the builder could not close names a token past the end, so the range is taken
	// to where the source does end.
	bool CFormattingAnalyzer::fp_HasOpaqueDirective(umint _iFirst, umint _iLast) const
	{
		auto nTokens = m_Tokens.f_GetTokens().f_GetLen();
		if (_iFirst >= nTokens)
			return false;

		return m_nOpaqueBefore[(_iLast < nTokens ? _iLast : nTokens - 1) + 1] > m_nOpaqueBefore[_iFirst];
	}

	// The indentation of the line the token stands on in the source, whatever the layout
	// has decided since: the lines a body was written against.
	umint CFormattingAnalyzer::fp_GetSourceLineIndent(umint _iToken) const
	{
		auto const &Source = m_Request.m_Source;
		auto iLine = m_Lines.f_FindLine(m_Tokens.f_GetTokens()[_iToken].m_iOffset);
		auto iStart = m_Lines.f_GetLineStart(iLine);
		auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
		auto iIndent = iStart;
		while (iIndent < iEnd && fg_IsSpaceOrTab(Source.f_GetStr()[iIndent]))
			++iIndent;

		umint nColumns = 0;
		if (!fg_MeasureTextColumns(Source.f_GetStr() + iStart, iIndent - iStart, m_Request.m_Settings.m_nTabWidth, nColumns))
			return 0;

		return nColumns;
	}
}

namespace
{
	void CFormattingAnalyzer::fp_PrepareTokenDepth()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		m_TokenDepth.f_SetLen(Tokens.f_GetLen());
		umint nDepth = 0;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			bool bClosing = m_Tokens.f_IsText(Tokens[i], ")")
				|| m_Tokens.f_IsText(Tokens[i], "]")
				|| m_Tokens.f_IsText(Tokens[i], "}")
				|| (m_Structure.f_IsAngleBracket(i) && m_Tokens.f_IsText(Tokens[i], ">"))
			;
			if (bClosing && nDepth)
				--nDepth;

			m_TokenDepth[i] = nDepth;
			bool bOpening = m_Tokens.f_IsText(Tokens[i], "(")
				|| m_Tokens.f_IsText(Tokens[i], "[")
				|| m_Tokens.f_IsText(Tokens[i], "{")
				|| (m_Structure.f_IsAngleBracket(i) && m_Tokens.f_IsText(Tokens[i], "<"))
			;
			if (bOpening)
				++nDepth;
		}
	}

	// Finds the binary operators that bind loosest at the range's own bracket level. Those
	// are the outermost places the range can be broken, so they are split first and the
	// scopes inside them only if a resulting line is still too long.
	void CFormattingAnalyzer::fp_FindLooseOperators(umint _iFirst, umint _iLast, TCVector<umint> &o_Operators) const
	{
		struct CPrecedence
		{
			ch8 const *m_pText;
			umint m_Level;
		};
		// Assignment keeps its right hand side, and a comma is a separator a group owns.
		constexpr CPrecedence c_Operators[] =
			{
				{"*", 5}, {"/", 5}, {"%", 5}, {"+", 6}, {"-", 6}, {"<<", 7}, {">>", 7}, {"<=>", 8}
				, {"<", 9}, {">", 9}, {"<=", 9}, {">=", 9}, {"==", 10}, {"!=", 10}
				, {"&", 11}, {"^", 12}, {"|", 13}, {"&&", 14}, {"||", 15}, {"?", 16}, {":", 16}
			}
		;
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		// Assignment is not a split point, and what stands to its left is a declarator, not
		// an expression: '&' and '*' spell a reference or a pointer there, never an operator.
		for (umint i = _iLast; i > _iFirst; --i)
		{
			if (m_TokenDepth[i] == nLevel && m_Tokens.f_IsText(Tokens[i], "="))
			{
				_iFirst = i;

				break;
			}
		}

		umint nLoosest = 0;
		for (umint iPass = 0; iPass < 2; ++iPass)
		{
			for (umint i = _iFirst; i <= _iLast; ++i)
			{
				if (Tokens[i].m_Kind != ECodeTokenKind::mc_Punctuator || m_TokenDepth[i] != nLevel || m_Structure.f_IsAngleBracket(i))
					continue;

				umint nPrecedence = 0;
				for (auto const &Operator : c_Operators)
				{
					if (m_Tokens.f_IsText(Tokens[i], Operator.m_pText))
						nPrecedence = Operator.m_Level;
				}

				// Without operands on both sides the token is a declarator or a unary form: what
				// stands in front of an infix operator ends an operand, and an operator, an opener
				// or a keyword such as 'return' ends none, so '= &Value' takes an address.
				if (!nPrecedence || i == _iFirst || !fp_EndsOperand(fp_PreviousCode(i)) || !fp_HasOperand(i, false))
					continue;

				// A colon is the conditional's only behind the '?' it answers; any other one
				// ends a label, or introduces a base clause, an initializer list or a range.
				if (m_Tokens.f_IsText(Tokens[i], ":"))
				{
					bool bAnswers = false;
					for (umint iQuestion = _iFirst; iQuestion < i && !bAnswers; ++iQuestion)
						bAnswers = m_TokenDepth[iQuestion] == nLevel && m_Tokens.f_IsText(Tokens[iQuestion], "?");

					if (!bAnswers)
						continue;
				}

				// Behind 'operator' the token spells the function's name, not an operation.
				auto iName = fp_PreviousCode(i);
				if (iName >= 0 && m_Tokens.f_IsText(Tokens[umint(iName)], "operator"))
					continue;

				// A '*', '&' or '&&' that declares a pointer or reference is not an operation:
				// what stands in front of it is a type, and 'TCFoo<int> *' cannot multiply.
				if (fg_IsDeclaratorToken(m_Tokens, m_Structure, i))
					continue;

				// A '&' or '&&' is the function's ref-qualifier when nothing that could be an
				// operand follows it: what comes after one is the trailing return type, the
				// body, a pure specifier, a requires clause, or another qualifier. That is never
				// a second ref-qualifier, so a '&' behind it takes an address: 'a && &b == p'.
				if (m_Tokens.f_IsText(Tokens[i], "&") || m_Tokens.f_IsText(Tokens[i], "&&"))
				{
					auto iNext = fp_NextCode(i);
					bool bReference = iNext >= 0 && (m_Tokens.f_IsText(Tokens[umint(iNext)], "&") || m_Tokens.f_IsText(Tokens[umint(iNext)], "&&"));
					bool bQualifier = iNext >= 0
						&& !bReference
						&& (m_Tokens.f_IsText(Tokens[umint(iNext)], "->")
							|| m_Tokens.f_IsText(Tokens[umint(iNext)], "{")
							|| m_Tokens.f_IsText(Tokens[umint(iNext)], "=")
							|| m_Tokens.f_IsText(Tokens[umint(iNext)], "requires")
							|| fp_IsFunctionQualifier(umint(iNext)))
					;
					if (bQualifier)
						continue;
				}

				if (!iPass)
				{
					nLoosest = fg_Max(nLoosest, nPrecedence);

					continue;
				}

				if (nPrecedence == nLoosest)
					o_Operators.f_Insert(i);
			}

			if (!nLoosest)
				return;
		}

		// A conditional groups to the right, so only the first '?' and the ':' that answers it
		// are this range's: a conditional in either branch is one operand of the outer one.
		if (nLoosest != 16 || o_Operators.f_IsEmpty())
			return;

		umint nOpen = 0;
		for (umint iOperator = 0; iOperator < o_Operators.f_GetLen(); ++iOperator)
		{
			if (m_Tokens.f_IsText(Tokens[o_Operators[iOperator]], "?"))
				++nOpen;
			else if (!--nOpen)
			{
				TCVector<umint> Outer{o_Operators[0], o_Operators[iOperator]};
				o_Operators = fg_Move(Outer);

				return;
			}
		}
	}

	// A lambda follows the operator at _iToken when the next thing is a capture list with
	// a parameter list or a body behind it.
	bool CFormattingAnalyzer::fp_IsLambdaIntroducer(umint _iToken) const
	{
		auto iNext = fp_NextCode(_iToken);
		if (iNext < 0 || !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iNext)], "["))
			return false;

		auto const &Nodes = m_Structure.f_GetNodes();
		auto iNode = m_Structure.f_FindNodeOpeningAt(umint(iNext));
		if (iNode >= Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Group)
			return false;

		auto iAfter = fp_NextCode(Nodes[iNode].m_iLastToken);
		if (iAfter < 0)
			return false;

		// The capture list is followed by the parameter list, by the body, or by the
		// lambda's own template parameter list.
		auto const &After = m_Tokens.f_GetTokens()[umint(iAfter)];

		return m_Tokens.f_IsText(After, "(") || m_Tokens.f_IsText(After, "{") || m_Tokens.f_IsText(After, "<");
	}

	// A template header is spelled 'template <...>'. The space in front of the list is what
	// tells a template argument list from a comparison, so the header's own list is not a
	// resolved group and its end has to be found by hand.
	umint CFormattingAnalyzer::fp_SkipTemplateHeader(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		if (!m_Tokens.f_IsText(Tokens[_iToken], "template"))
			return _iToken;

		auto iOpen = fp_NextCode(_iToken);
		if (iOpen < 0 || !m_Tokens.f_IsText(Tokens[umint(iOpen)], "<"))
			return _iToken;

		umint nDepth = 0;
		for (auto i = iOpen; i >= 0; i = fp_NextCode(umint(i)))
		{
			auto const &Token = Tokens[umint(i)];
			if (m_Tokens.f_IsText(Token, "<"))
			{
				++nDepth;

				continue;
			}

			if (!m_Tokens.f_IsText(Token, ">"))
				continue;

			if (nDepth > 1)
			{
				--nDepth;

				continue;
			}

			auto iNext = fp_NextCode(umint(i));

			return iNext < 0 ? _iToken : umint(iNext);
		}

		return _iToken;
	}

	// A capture list, or a lambda's own template parameter list, ends one part of an
	// introducer. What follows stands on its own rather than belonging to what came before.
	// An arrow introduces a trailing return type when a parameter list, or a function's
	// qualifiers behind one, stands in front of it; behind a call's arguments it is a
	// member access: 'fg_Get()->f_Call()'.
	bool CFormattingAnalyzer::fp_IsTrailingReturnArrow(umint _iToken) const
	{
		return fg_IsTrailingReturnArrow(m_Tokens, m_Structure, _iToken);
	}

	bool CFormattingAnalyzer::fp_ClosesLambdaIntroducer(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		if (m_Structure.f_IsAngleBracket(_iToken))
			return fp_FollowsScope(_iToken);

		if (!m_Tokens.f_IsText(Tokens[_iToken], "]"))
			return false;

		// A subscript stands behind what it indexes; a capture list stands on its own, and
		// behind a keyword it is one however that keyword reads: 'return [&] { ... }'.
		if (!fg_IsCaptureList(m_Tokens, m_Structure, _iToken))
			return false;

		// A bare name behind the list, such as an attribute macro, stands between it and
		// the parameter list or the body: '[&] mark_no_coroutine_debug () mutable'.
		auto iAfter = fp_NextCode(_iToken);
		if (iAfter >= 0 && Tokens[umint(iAfter)].m_Kind == ECodeTokenKind::mc_Identifier && !fp_IsFunctionQualifier(umint(iAfter)))
			iAfter = fp_NextCode(umint(iAfter));

		// A lambda that takes nothing may leave its parameter list out, and then its
		// qualifiers and its return type stand behind the list: '[pState] mutable -> int'.
		while (iAfter >= 0 && fp_IsFunctionQualifier(umint(iAfter)))
			iAfter = fp_NextCode(umint(iAfter));

		if (iAfter < 0)
			return false;

		auto const &After = Tokens[umint(iAfter)];
		if (m_Tokens.f_IsText(After, "->"))
			return fp_IsTrailingReturnArrow(umint(iAfter));

		return m_Tokens.f_IsText(After, "(") || m_Tokens.f_IsText(After, "{") || m_Tokens.f_IsText(After, "<");
	}

	// The words that may stand between a parameter list and a trailing return type.
	bool CFormattingAnalyzer::fp_IsFunctionQualifier(umint _iToken) const
	{
		constexpr ch8 const *c_pQualifiers[] =
			{
				"const", "volatile", "noexcept", "mutable", "override", "final", "&", "&&"
			}
		;
		auto const &Token = m_Tokens.f_GetTokens()[_iToken];
		for (auto pQualifier : c_pQualifiers)
		{
			if (m_Tokens.f_IsText(Token, pQualifier))
				return true;
		}

		return false;
	}

	// A bracket's closing marker is where one scope ends and the next may start on a line
	// of its own. Anything else in front of a scope owns it: a name and its argument list
	// are one call, and the line cannot be broken between them. A template argument list
	// closes with '>' but is part of the name it follows, so it does not end a scope here.
	bool CFormattingAnalyzer::fp_FollowsScope(umint _iToken) const
	{
		auto const &Token = m_Tokens.f_GetTokens()[_iToken];
		if (m_Tokens.f_IsText(Token, ")") || m_Tokens.f_IsText(Token, "]"))
		{
			// The brackets behind 'operator' spell the function's name: 'operator ()', 'operator []'.
			auto iOpen = fp_PreviousCode(_iToken);
			if (iOpen >= 0 && (m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iOpen)], "(") || m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iOpen)], "[")))
			{
				auto iKeyword = fp_PreviousCode(umint(iOpen));
				if (iKeyword >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iKeyword)], "operator"))
					return false;
			}

			return true;
		}

		// A lambda's template parameter list is a scope of its own rather than part of a
		// name, which is what a template argument list behind an identifier is.
		if (!m_Structure.f_IsAngleBracket(_iToken))
			return false;

		auto const &Nodes = m_Structure.f_GetNodes();
		auto iNode = m_Structure.f_FindNodeClosingAt(_iToken);
		if (iNode >= Nodes.f_GetLen() || Nodes[iNode].m_Kind != ECodeNodeKind::mc_Group || Nodes[iNode].m_Bracket != ECodeBracket::mc_Angle)
			return false;

		auto iBefore = fp_PreviousCode(Nodes[iNode].m_iFirstToken);

		return iBefore >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBefore)], "]");
	}

	// Whether the parenthesis opening at the token holds the operand of a named cast:
	// 'static_cast<CFoo &>(_Value)'.
	bool CFormattingAnalyzer::fp_IsNamedCast(umint _iOpen) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto iClose = fp_PreviousCode(_iOpen);
		if (iClose < 0 || !m_Structure.f_IsAngleBracket(umint(iClose)) || !m_Tokens.f_IsText(Tokens[umint(iClose)], ">"))
			return false;

		auto const &Nodes = m_Structure.f_GetNodes();
		auto iNode = m_Structure.f_FindNodeClosingAt(umint(iClose));
		if (iNode < Nodes.f_GetLen() && Nodes[iNode].m_Kind == ECodeNodeKind::mc_Group && Nodes[iNode].m_Bracket == ECodeBracket::mc_Angle)
		{
			auto iKeyword = fp_PreviousCode(Nodes[iNode].m_iFirstToken);
			if (iKeyword < 0)
				return false;

			auto const &Keyword = Tokens[umint(iKeyword)];

			return m_Tokens.f_IsText(Keyword, "static_cast")
				|| m_Tokens.f_IsText(Keyword, "reinterpret_cast")
				|| m_Tokens.f_IsText(Keyword, "const_cast")
				|| m_Tokens.f_IsText(Keyword, "dynamic_cast")
			;
		}

		return false;
	}

	// A parenthesised type in front of an operand is a cast: nothing separates the closing
	// parenthesis from what follows, while a call or a clause has a name or a keyword in
	// front of the opening one.
	bool CFormattingAnalyzer::fp_IsCastGroup(umint _iNode) const
	{
		auto const &Node = m_Structure.f_GetNodes()[_iNode];
		if (Node.m_Bracket != ECodeBracket::mc_Paren)
			return false;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto iBefore = fp_PreviousCode(Node.m_iFirstToken);
		if (iBefore >= 0)
		{
			auto const &Before = Tokens[umint(iBefore)];
			// An argument list closes a name and a template parameter list closes a lambda's
			// introducer. What follows either is a call, never a cast.
			if (Before.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Before, ")") || m_Tokens.f_IsText(Before, "]") || m_Structure.f_IsAngleBracket(umint(iBefore)))
				return false;
		}

		auto iAfter = fp_NextCode(Node.m_iLastToken);
		if (iAfter < 0)
			return false;

		auto const &After = Tokens[umint(iAfter)];

		return After.m_Kind == ECodeTokenKind::mc_Identifier
			|| After.m_Kind == ECodeTokenKind::mc_Number
			|| After.m_Kind == ECodeTokenKind::mc_StringLiteral
			|| After.m_Kind == ECodeTokenKind::mc_CharLiteral
			|| m_Tokens.f_IsText(After, "(")
		;
	}

	// A call or a subscript on what a call or a subscript yields is a postfix operation
	// like a member access, and binds the same way: 'f_CallActor(&C::f_Fn)(_Params)[0]'.
	// A lambda's parameter list stands behind a closing marker too and is part of its
	// introducer instead, a cast's operand is what the cast converts, and an empty list
	// holds nothing to give a line to: '(*pFunctor)()'.
	bool CFormattingAnalyzer::fp_IsYieldedScope(umint _iOpen) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		if (!m_Tokens.f_IsText(Tokens[_iOpen], "(") && !m_Tokens.f_IsText(Tokens[_iOpen], "["))
			return false;

		auto iBefore = fp_PreviousCode(_iOpen);
		if (iBefore < 0 || (!m_Tokens.f_IsText(Tokens[umint(iBefore)], ")") && !m_Tokens.f_IsText(Tokens[umint(iBefore)], "]")))
			return false;

		auto iInner = fp_NextCode(_iOpen);
		if (iInner < 0 || m_Tokens.f_IsText(Tokens[umint(iInner)], ")") || m_Tokens.f_IsText(Tokens[umint(iInner)], "]"))
			return false;

		if (m_Tokens.f_IsText(Tokens[umint(iBefore)], "]") && fg_IsCaptureList(m_Tokens, m_Structure, umint(iBefore)))
			return false;

		auto const &Nodes = m_Structure.f_GetNodes();
		auto iGroup = m_Structure.f_FindNodeClosingAt(umint(iBefore));

		return iGroup >= Nodes.f_GetLen() || Nodes[iGroup].m_Kind != ECodeNodeKind::mc_Group || !fp_IsCastGroup(iGroup);
	}

	// The last resort for a line nothing else can shorten: every member access at the
	// line's own level takes a line of its own, and each part is then laid out on its own.
	// A call or a subscript on what the chain yields is one of its links, and gives with
	// the rest.
	bool CFormattingAnalyzer::fp_LayoutMembers(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		TCVector<umint> Members;
		for (umint i = _iFirst + 1; i <= _iLast; ++i)
		{
			if (m_TokenDepth[i] != nLevel)
				continue;

			if (fp_IsYieldedScope(i))
			{
				Members.f_Insert(i);

				continue;
			}

			if (!m_Tokens.f_IsText(Tokens[i], ".") && !m_Tokens.f_IsText(Tokens[i], "->"))
				continue;

			auto iBefore = fp_PreviousCode(i);
			if (iBefore < 0 || umint(iBefore) < _iFirst)
				continue;

			// An arrow behind a parameter list or a function's qualifiers introduces a
			// trailing return type, which is a break of its own and not a member access.
			auto const &Before = Tokens[umint(iBefore)];
			if (m_Tokens.f_IsText(Tokens[i], "->") && fp_IsTrailingReturnArrow(i))
				continue;

			bool bOperand = Before.m_Kind == ECodeTokenKind::mc_Identifier
				|| m_Tokens.f_IsText(Before, ")")
				|| m_Tokens.f_IsText(Before, "]")
				|| (m_Structure.f_IsAngleBracket(umint(iBefore)) && m_Tokens.f_IsText(Before, ">"))
			;
			if (bOperand)
				Members.f_Insert(i);
		}

		if (Members.f_IsEmpty())
			return false;

		for (auto iMember : Members)
			fp_BreakBefore(iMember, _nContinuation);

		for (umint iSegment = 0; iSegment <= Members.f_GetLen(); ++iSegment)
		{
			auto iStart = iSegment ? Members[iSegment - 1] : _iFirst;
			auto iEnd = iSegment < Members.f_GetLen() ? Members[iSegment] - 1 : _iLast;
			auto iLast = fp_PreviousCode(iEnd + 1);
			if (iLast < 0 || umint(iLast) < iStart)
				continue;

			auto nSegmentIndent = iSegment ? _nContinuation : _iIndent;
			if (fp_FitsInline(iStart, umint(iLast), nSegmentIndent))
				fp_MarkInline(iStart, umint(iLast));
			else
				fp_LayoutScopes(_iNode, iStart, umint(iLast), nSegmentIndent, false, false);
		}

		return true;
	}

	// A qualified name is written across lines at its qualification: the scope it names
	// stays whole and the line breaks in front of the '::' that follows it, which is where
	// the sources break one, 'TCFoo<CBar>' with '::f_Function' below it. One break is all
	// this is: a head that needs more of them has more in it than a name, and gives at its
	// scopes instead, as an explicit instantiation does whose arguments fill a line by
	// themselves. The break taken is the last one that leaves what stands in front of it on
	// the line, since the lines are filled from the range's start.
	bool CFormattingAnalyzer::fp_BreakAtQualification(umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		umint iBreak = 0;
		for (umint i = _iFirst + 1; i <= _iLast; ++i)
		{
			if (m_TokenDepth[i] != nLevel || !m_Tokens.f_IsText(Tokens[i], "::"))
				continue;

			// What the qualification names has to stand in front of it: a leading '::' names
			// the global scope and carries nothing to leave on the line.
			auto iBefore = fp_PreviousCode(i);
			if (iBefore < 0 || umint(iBefore) < _iFirst)
				continue;

			auto const &Before = Tokens[umint(iBefore)];
			bool bScope = Before.m_Kind == ECodeTokenKind::mc_Identifier
				|| (m_Structure.f_IsAngleBracket(umint(iBefore)) && m_Tokens.f_IsText(Before, ">"))
			;
			if (bScope && fp_FitsInline(_iFirst, umint(iBefore), _iIndent) && fp_FitsInline(i, _iLast, _nContinuation))
				iBreak = i;
		}

		if (!iBreak)
			return false;

		fp_MarkInline(_iFirst, umint(fp_PreviousCode(iBreak)));
		fp_BreakBefore(iBreak, _nContinuation);
		fp_MarkInline(iBreak, _iLast);

		return true;
	}

	// A name too long for its line even with its parameter list opened can only give at its
	// own scopes: the template argument list it carries, and after that its member accesses.
	// Its qualification gives first, since the scope it names stays whole that way.
	bool CFormattingAnalyzer::fp_LayoutHead(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation)
	{
		if (fp_BreakAtQualification(_iFirst, _iLast, _iIndent, _nContinuation))
			return true;

		auto iFirstParen = m_iSplitFirstParen;
		m_iSplitFirstParen = 0;
		bool bSplit = fp_LayoutScopes(_iNode, _iFirst, _iLast, _iIndent, false, _nContinuation != _iIndent);
		m_iSplitFirstParen = iFirstParen;
		if (bSplit)
			return true;

		return fp_LayoutMembers(_iNode, _iFirst, _iLast, _iIndent, _nContinuation);
	}

	// Fills lines greedily from the range's start: a line that does not fit is first broken
	// in front of a scope that follows another, or a trailing return type, and only when no
	// such break helps is the first scope on the line opened up. What follows an opened
	// scope starts under its closing marker and is filled the same way.
	bool CFormattingAnalyzer::fp_LayoutScopes(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndent, bool _bMustSplit)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		// A range that is already a continuation keeps its lines at one level. Only a range
		// standing at its own start puts what follows it one level in.
		auto nContinuation = _bClause || !_bIndent ? _iIndent : _iIndent + nTab;
		bool bStatement = Node.m_Kind == ECodeNodeKind::mc_Statement;
		bool bSplit = false;
		// A declaration is never split before its name while it has another way to fit:
		// its parameter list can be opened, or its return type moved behind that list. An
		// explicit instantiation has neither, and then its argument list is the only scope
		// it has, so that is where it breaks.
		bool bNamedScope = false;
		// A parameter's default argument is given to it the way a statement's value is.
		bool bElement = Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Paren && _iFirst > Node.m_iFirstToken;
		umint iAssign = 0;
		for (umint i = _iFirst; (bStatement || bElement) && i <= _iLast && !iAssign; ++i)
		{
			if (m_TokenDepth[i] == m_TokenDepth[_iFirst] && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], "="))
				iAssign = i;
		}

		// Behind a declaration's parameter list the '=' makes it pure, defaulted or deleted,
		// and gives nothing a value.
		if (bStatement && m_iSplitFirstParen && iAssign > m_iSplitFirstParen)
			iAssign = 0;

		TCVector<umint> Scopes;
		for (umint iPass = 0; iPass < 2; ++iPass)
		{
			for (auto iChild : Node.m_Children)
			{
				// A braced initializer is a scope like any other: it can be opened. Closing one
				// that is already open is what it never does, which its own multi-line flag
				// already refuses.
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind != ECodeNodeKind::mc_Group)
					continue;

				if (Child.m_iFirstToken < _iFirst || Child.m_iLastToken > _iLast)
					continue;

				// A trailing return type is one unit on its own line.
				if (bStatement && Child.m_iFirstToken > m_iSplitTrailingReturn)
					continue;

				// A cast converts what follows it, so its parentheses belong to that operand
				// rather than being a scope of their own, and so do the placement arguments
				// of a 'new', which stand in front of the type it constructs.
				// Behind 'operator' the 'new' names the function, whose parenthesis is its parameter list.
				auto iBeforeScope = fp_PreviousCode(Child.m_iFirstToken);
				auto iBeforeNew = iBeforeScope >= 0 ? fp_PreviousCode(umint(iBeforeScope)) : aint(-1);
				bool bPlacement = Child.m_Bracket == ECodeBracket::mc_Paren
					&& iBeforeScope >= 0
					&& m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBeforeScope)], "new")
					&& !(iBeforeNew >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBeforeNew)], "operator"))
				;
				if (bPlacement && fp_FitsInline(umint(iBeforeScope), Child.m_iLastToken, _iIndent))
					fp_MarkInline(umint(iBeforeScope), Child.m_iLastToken);

				if (fp_IsCastGroup(iChild) || bPlacement)
					continue;

				// What stands in front of a statement's '=' declares what the value is given
				// to, and is no more a place to break than what stands in front of a name.
				bool bBeforeName = (bStatement && Child.m_iLastToken < m_iSplitFirstParen) || Child.m_iLastToken < iAssign;
				// The template arguments of a class a name is qualified with stand in front of
				// that name too: 'TCFoo<CBar>::f_Function'.
				auto iQualifies = fp_NextCode(Child.m_iLastToken);
				bBeforeName |= Child.m_Bracket == ECodeBracket::mc_Angle
					&& iQualifies >= 0
					&& umint(iQualifies) <= _iLast
					&& m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iQualifies)], "::")
				;
				if (!iPass)
				{
					auto iInner = fp_NextCode(Child.m_iFirstToken);
					bNamedScope |= !bBeforeName && iInner >= 0 && umint(iInner) != Child.m_iLastToken;

					continue;
				}

				if (bBeforeName && bNamedScope)
					continue;

				// A name's template argument list stays whole while the argument list behind
				// it can be opened: 'TCFoo<T>(...)' opens its parentheses first, and the
				// name only gives when it still does not fit in front of them.
				if (Child.m_Bracket == ECodeBracket::mc_Angle)
				{
					auto iAfter = fp_NextCode(Child.m_iLastToken);
					bool bNamesCall = false;
					for (auto iOther : Node.m_Children)
					{
						auto const &Other = Nodes[iOther];
						if (Other.m_Kind != ECodeNodeKind::mc_Group || Other.m_Bracket != ECodeBracket::mc_Paren || Other.m_iLastToken > _iLast)
							continue;

						if (iAfter < 0 || Other.m_iFirstToken != umint(iAfter))
							continue;

						auto iInner = fp_NextCode(Other.m_iFirstToken);
						bNamesCall = iInner >= 0 && umint(iInner) != Other.m_iLastToken && !fp_FollowsScope(Child.m_iLastToken);
					}

					if (bNamesCall)
						continue;
				}

				Scopes.f_Insert(iChild);
			}
		}

		// A declaration's name qualified with a class's template arguments gives in front of
		// the '::' behind them before those arguments open: 'TCFoo<...>' over '::f_Name()'.
		// That is only where the name does not fit in front of its parameter list, which
		// otherwise opens instead; an empty one has nothing to open.
		bool bNameFits = !bStatement || !m_iSplitFirstParen || m_iSplitFirstParen > _iLast || fp_FitsInline(_iFirst, m_iSplitFirstParen, _iIndent);
		if (bNameFits && bStatement && m_iSplitFirstParen && m_iSplitFirstParen < _iLast)
		{
			auto iInner = fp_NextCode(m_iSplitFirstParen);
			bNameFits = iInner < 0 || !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iInner)], ")");
		}
		for (auto iChild : Node.m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (bNameFits || _bClause || Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_Bracket != ECodeBracket::mc_Angle)
				continue;

			if (Child.m_iFirstToken < _iFirst || Child.m_iLastToken >= m_iSplitFirstParen || Child.m_iLastToken >= _iLast)
				continue;

			auto iColons = fp_NextCode(Child.m_iLastToken);
			if (iColons < 0 || !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iColons)], "::"))
				continue;

			if (!fp_FitsInline(_iFirst, Child.m_iLastToken, _iIndent) || !fp_FitsInline(umint(iColons), _iLast, nContinuation))
				continue;

			fp_MarkInline(_iFirst, Child.m_iLastToken);
			fp_BreakBefore(umint(iColons), nContinuation);
			fp_MarkInline(umint(iColons), _iLast);

			return true;
		}

		// A value with nothing in it to open goes on a line of its own behind the '=' where
		// that makes the statement fit, rather than the type in front of the name giving.
		// Where the type and the name do not fit on one line to begin with, the name goes
		// down with its value, and the type stays whole on the line above them.
		if (iAssign && !_bClause && (_bIndent || bElement) && !fp_FitsInline(_iFirst, _iLast, _iIndent))
		{
			auto const &AllTokens = m_Tokens.f_GetTokens();
			auto iHeadEnd = fp_PreviousCode(iAssign);
			bool bSettled = iHeadEnd >= 0
				&& umint(iHeadEnd) >= _iFirst
				&& iAssign < _iLast
				&& fp_GetCanonicalSpacing(umint(iHeadEnd), iAssign) == ECodeSpacing::mc_Space
			;
			bool bHeadFits = bSettled && fp_FitsInline(_iFirst, umint(iHeadEnd), _iIndent);
			if (bHeadFits && (!bNamedScope || bElement) && fp_FitsInline(iAssign, _iLast, nContinuation))
			{
				fp_MarkInline(_iFirst, umint(iHeadEnd));
				fp_BreakBefore(iAssign, nContinuation);
				fp_MarkInline(iAssign, _iLast);

				return true;
			}

			if (bSettled && !bHeadFits && AllTokens[umint(iHeadEnd)].m_Kind == ECodeTokenKind::mc_Identifier)
			{
				// A declarator hugs the name, and goes where the name goes, and so does the
				// cv-qualifier the declarators stand behind: 'CFoo<...>' over 'const &_Name'.
				auto iNameFirst = umint(iHeadEnd);
				for (auto iBefore = fp_PreviousCode(iNameFirst); iBefore >= 0 && fg_IsDeclaratorToken(m_Tokens, m_Structure, umint(iBefore)); iBefore = fp_PreviousCode(iNameFirst))
					iNameFirst = umint(iBefore);

				auto iQualifier = fp_PreviousCode(iNameFirst);
				if (iNameFirst != umint(iHeadEnd) && iQualifier >= 0 && (AllTokens[umint(iQualifier)].m_Kind == ECodeTokenKind::mc_Identifier)
					&& (m_Tokens.f_IsText(AllTokens[umint(iQualifier)], "const") || m_Tokens.f_IsText(AllTokens[umint(iQualifier)], "volatile")))
				{
					iNameFirst = umint(iQualifier);
				}

				auto iTypeEnd = fp_PreviousCode(iNameFirst);
				bool bNameDown = iTypeEnd >= 0
					&& umint(iTypeEnd) >= _iFirst
					&& fp_FitsInline(_iFirst, umint(iTypeEnd), _iIndent)
					&& fp_FitsInline(iNameFirst, _iLast, nContinuation)
				;
				if (bNameDown)
				{
					fp_MarkInline(_iFirst, umint(iTypeEnd));
					fp_BreakBefore(iNameFirst, nContinuation);
					fp_MarkInline(iNameFirst, _iLast);

					return true;
				}
			}
		}

		// Where the line may break: in front of a scope that follows another scope's closing
		// marker, and in front of a trailing return type that follows a parameter list. They
		// are taken in the order they stand, so a lambda gives its parameter list a line of
		// its own before it gives one to its return type.
		TCVector<umint> Breaks;
		// A lambda's capture list, template parameter list and parameter list are the one
		// introducer: they stand together on a line or each takes one of its own.
		TCVector<bool> Introducer;
		for (umint i = 0; i < Scopes.f_GetLen(); ++i)
		{
			auto iBefore = fp_PreviousCode(Nodes[Scopes[i]].m_iFirstToken);
			if (iBefore < 0 || umint(iBefore) < _iFirst || !fp_FollowsScope(umint(iBefore)))
				continue;

			// A capture list, or a template parameter list that only a lambda can end, is
			// what one part of an introducer stands behind.
			auto const &Before = m_Tokens.f_GetTokens()[umint(iBefore)];
			bool bIntroducer = m_Tokens.f_IsText(Before, "]");
			if (m_Structure.f_IsAngleBracket(umint(iBefore)))
			{
				auto iList = m_Structure.f_FindNodeClosingAt(umint(iBefore));
				auto iCapture = iList < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iList].m_iFirstToken) : aint(-1);
				bIntroducer = iCapture >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iCapture)], "]");
			}

			// An empty scope is nothing to move down: only a part of an introducer takes a
			// line of its own while holding nothing, and '(*pFunctor)()' stays whole.
			auto iInner = fp_NextCode(Nodes[Scopes[i]].m_iFirstToken);
			if (!bIntroducer && iInner >= 0 && umint(iInner) == Nodes[Scopes[i]].m_iLastToken)
				continue;

			Breaks.f_Insert(Nodes[Scopes[i]].m_iFirstToken);
			Introducer.f_Insert(bIntroducer);
		}

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		// So may an exception specification behind a parameter list, which gives its line
		// before the list opens: 'fs_Call(t_C &&_This)' over 'noexcept(noexcept(...))'.
		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			bool bArrow = m_Tokens.f_IsText(Tokens[i], "->");
			if (m_TokenDepth[i] != nLevel || (!bArrow && !m_Tokens.f_IsText(Tokens[i], "noexcept")))
				continue;

			auto iBefore = fp_PreviousCode(i);
			if (iBefore < 0 || umint(iBefore) < _iFirst)
				continue;

			bool bSpecification = false;
			if (!bArrow)
			{
				auto iList = iBefore;
				while (iList >= 0 && umint(iList) > _iFirst && fp_IsFunctionQualifier(umint(iList)))
					iList = fp_PreviousCode(umint(iList));

				bSpecification = iList >= 0 && fg_ClosesParameterList(m_Tokens, m_Structure, umint(iList));
			}

			if (bArrow ? fp_IsTrailingReturnArrow(i) : bSpecification)
			{
				Breaks.f_Insert(i);
				Introducer.f_Insert(false);
			}
		}

		for (umint i = 1; i < Breaks.f_GetLen(); ++i)
		{
			for (umint j = i; j && Breaks[j - 1] > Breaks[j]; --j)
			{
				fg_Swap(Breaks[j - 1], Breaks[j]);
				fg_Swap(Introducer[j - 1], Introducer[j]);
			}
		}

		umint iLineFirst = _iFirst;
		umint nLineIndent = _iIndent;
		umint iScope = 0;
		// A line that has to be split stays one until a break starts the next, also where the
		// scope it would open first is passed over for a later one.
		bool bMustSplit = _bMustSplit;
		umint iMustSplitLine = iLineFirst;
		while (true)
		{
			bMustSplit &= iLineFirst == iMustSplitLine;
			// Deciding the rest fits is also deciding to write it that way.
			if (!bMustSplit && fp_FitsInline(iLineFirst, _iLast, nLineIndent))
			{
				fp_MarkInline(iLineFirst, _iLast);

				break;
			}

			// A line with a gap the standard does not settle has no single-line form to be
			// measured against, so it is left as it stands rather than taken for one that
			// is too wide.
			umint nLine = 0;
			if (!fp_MeasureJoinedWidth(iLineFirst, _iLast, nLine))
				break;

			umint iBreak = TCLimitsInt<umint>::mc_Max;
			for (umint i = 0; i < Breaks.f_GetLen(); ++i)
			{
				if (Breaks[i] <= iLineFirst)
					continue;

				auto iBefore = fp_PreviousCode(Breaks[i]);
				if (iBefore < 0 || umint(iBefore) < iLineFirst)
					continue;

				if (!fp_FitsInline(iLineFirst, umint(iBefore), nLineIndent))
					break;

				// A call on what another call yields spells one call expression with it,
				// 'f_CallActor(&C::f_Fn)(_Params)', and moves down only where the line has
				// nothing later to give. A scope holding a lambda body is such a thing: it
				// opens below whatever else is done, and what stands in front of it stays on
				// the line where it fits, the whole call expression included.
				bool bYielded = m_Tokens.f_IsText(Tokens[Breaks[i]], "(") && m_Tokens.f_IsText(Tokens[umint(iBefore)], ")");
				if (bYielded)
				{
					bool bBodyBelow = false;
					for (auto iOther : Scopes)
					{
						auto const &Other = Nodes[iOther];
						if (!Other.m_bHasBlock || Other.m_iFirstToken <= Breaks[i])
							continue;

						auto iBodyHead = fp_PreviousCode(Other.m_iFirstToken);
						bBodyBelow |= iBodyHead >= 0 && umint(iBodyHead) >= iLineFirst && fp_FitsInline(iLineFirst, umint(iBodyHead), nLineIndent);
					}

					if (bBodyBelow)
						continue;
				}

				iBreak = i;

				break;
			}

			if (iBreak != TCLimitsInt<umint>::mc_Max)
			{
				auto iBefore = fp_PreviousCode(Breaks[iBreak]);
				fp_MarkInline(iLineFirst, umint(iBefore));
				fp_BreakBefore(Breaks[iBreak], nContinuation);
				bSplit = true;
				iLineFirst = Breaks[iBreak];
				// A call or a subscript on what the chain yields is a link of it, and a chain
				// that gives at one link gives at all of them.
				if (fp_IsYieldedScope(iLineFirst) && fp_LayoutMembers(_iNode, iLineFirst, _iLast, nContinuation, nContinuation))
					break;

				// The rest of one introducer follows at once, so its parts never end up on
				// two lines where the source had three parts.
				while (Introducer[iBreak] && iBreak + 1 < Breaks.f_GetLen() && Introducer[iBreak + 1])
				{
					++iBreak;
					auto iPartLast = umint(fp_PreviousCode(Breaks[iBreak]));
					if (fp_FitsInline(iLineFirst, iPartLast, nContinuation))
						fp_MarkInline(iLineFirst, iPartLast);

					fp_BreakBefore(Breaks[iBreak], nContinuation);
					iLineFirst = Breaks[iBreak];
				}

				nLineIndent = nContinuation;
				while (iScope < Scopes.f_GetLen() && Nodes[Scopes[iScope]].m_iFirstToken < iLineFirst)
					++iScope;

				// An exception specification on a line of its own is a qualifier moved below the
				// parameter list, and a trailing return type behind it takes the next line.
				for (umint i = iBreak + 1; m_Tokens.f_IsText(Tokens[iLineFirst], "noexcept") && i < Breaks.f_GetLen(); ++i)
				{
					if (m_Tokens.f_IsText(Tokens[Breaks[i]], "->"))
					{
						bMustSplit = true;
						iMustSplitLine = iLineFirst;
					}
				}

				continue;
			}

			// A scope holding a lambda body is the one to open, since the body starts a line
			// of its own whatever else is done: what stands in front of that scope stays on
			// the line where it fits, earlier scopes included.
			for (umint i = iScope; i < Scopes.f_GetLen(); ++i)
			{
				if (!Nodes[Scopes[i]].m_bHasBlock)
					continue;

				auto iHead = fp_PreviousCode(Nodes[Scopes[i]].m_iFirstToken);
				if (i > iScope && iHead >= 0 && umint(iHead) >= iLineFirst && fp_FitsInline(iLineFirst, umint(iHead), nLineIndent))
					iScope = i;

				break;
			}

			// Nothing on the line can be moved down whole, so the next scope is opened. With
			// no scope left, the line's member accesses are the last thing that can give.
			if (iScope >= Scopes.f_GetLen())
			{
				bSplit |= fp_LayoutMembers(_iNode, iLineFirst, _iLast, nLineIndent, nContinuation);

				break;
			}

			// A named cast converts its operand and yields what the rest of the expression is
			// written on, so its parenthesis belongs to the head the way a C cast's does: it
			// stays closed while everything up to the next scope fits on the line, and the
			// line gives at the call behind it, 'static_cast<CFoo &>(_Value).f_Bind' with the
			// scopes of 'f_Bind' opened below.
			{
				auto const &Link = Nodes[Scopes[iScope]];
				auto iMember = fp_NextCode(Link.m_iLastToken);
				bool bChained = !Link.m_bHasBlock
					&& Link.m_Bracket == ECodeBracket::mc_Paren
					&& fp_IsNamedCast(Link.m_iFirstToken)
					&& iMember >= 0
					&& umint(iMember) <= _iLast
					&& (m_Tokens.f_IsText(Tokens[umint(iMember)], ".") || m_Tokens.f_IsText(Tokens[umint(iMember)], "->"))
				;
				umint iNextScope = TCLimitsInt<umint>::mc_Max;
				for (auto iChild : Node.m_Children)
				{
					auto const &Child = Nodes[iChild];
					if (!bChained || Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_iFirstToken <= Link.m_iLastToken || Child.m_iLastToken > _iLast)
						continue;

					auto iInner = fp_NextCode(Child.m_iFirstToken);
					if (iInner >= 0 && umint(iInner) != Child.m_iLastToken)
						iNextScope = fg_Min(iNextScope, Child.m_iFirstToken);
				}

				auto iLinkHead = iNextScope != TCLimitsInt<umint>::mc_Max ? fp_PreviousCode(iNextScope) : aint(-1);
				if (iLinkHead >= 0 && umint(iLinkHead) > Link.m_iLastToken && fp_FitsInline(iLineFirst, umint(iLinkHead), nLineIndent))
				{
					++iScope;

					continue;
				}
			}

			// A member access behind the scope about to be opened ends up on a line of its
			// own whatever the scope does, since what follows a closing marker resumes under
			// it. That line was all the line needed, so the scope stays whole and the access
			// takes the line below it: 'm_Subscription(&CFoo::f_GetActor)' with
			// '.f_Timeout(30.0).f_CallSync(m_pRunLoop)' under it. A scope holding a lambda
			// body is not one of these: its body takes lines whether or not it is opened.
			{
				auto const &Link = Nodes[Scopes[iScope]];
				auto iInner = fp_NextCode(Link.m_iFirstToken);
				auto iMember = fp_NextCode(Link.m_iLastToken);
				bool bOpens = !Link.m_bHasBlock && iInner >= 0 && umint(iInner) != Link.m_iLastToken && Link.m_iFirstToken >= iLineFirst;
				bool bMember = iMember >= 0
					&& umint(iMember) <= _iLast
					&& (m_Tokens.f_IsText(Tokens[umint(iMember)], ".") || m_Tokens.f_IsText(Tokens[umint(iMember)], "->"))
					&& !fp_IsTrailingReturnArrow(umint(iMember))
				;
				// The name a type declares resumes under its argument list the same way, and
				// takes the line below the type on its own: 'TCMap<CStr, CValue>' with
				// 'm_Values' under it.
				bool bDeclared = iMember >= 0
					&& umint(iMember) <= _iLast
					&& Link.m_Bracket == ECodeBracket::mc_Angle
					&& (Tokens[umint(iMember)].m_Kind == ECodeTokenKind::mc_Identifier || fg_IsDeclaratorToken(m_Tokens, m_Structure, umint(iMember)))
				;
				if (bDeclared)
				{
					auto iName = umint(iMember);
					while (iName < _iLast && fg_IsDeclaratorToken(m_Tokens, m_Structure, iName))
						iName = umint(fp_NextCode(iName));

					bDeclared = Tokens[iName].m_Kind == ECodeTokenKind::mc_Identifier
						&& !m_Tokens.f_IsText(Tokens[iName], "final")
						&& fp_FitsInline(umint(iMember), _iLast, nContinuation)
					;
				}

				// Moving the access down is what opening the call that has to break would do anyway,
				// since what follows a closing marker resumes under it; but only where that call has
				// an access behind it. The call that has to break is the one holding the most, the
				// scope in front of the access or the call behind it. Where it is the call behind,
				// the chain up to it stays on the line and the call opens: 'p->f_Get(i)->f_Report'
				// over '('. A chain of several accesses gives at each of them instead.
				if (bOpens && bMember && iScope + 1 < Scopes.f_GetLen())
				{
					auto const &Call = Nodes[Scopes[iScope + 1]];
					auto iCallHead = fp_PreviousCode(Call.m_iFirstToken);
					bool bOneAccess = Call.m_iLastToken <= _iLast && !Call.m_bHasBlock && iCallHead >= 0 && fp_PreviousCode(umint(iCallHead)) == iMember;
					auto iBehindCall = bOneAccess ? fp_NextCode(Call.m_iLastToken) : aint(-1);
					bool bAccessBehind = iBehindCall >= 0
						&& umint(iBehindCall) <= _iLast
						&& (m_Tokens.f_IsText(Tokens[umint(iBehindCall)], ".") || m_Tokens.f_IsText(Tokens[umint(iBehindCall)], "->"))
					;
					umint nLink = 0;
					umint nCall = 0;
					bool bCallHolds = bOneAccess
						&& fp_MeasureJoinedWidth(Link.m_iFirstToken, Link.m_iLastToken, nLink)
						&& fp_MeasureJoinedWidth(Call.m_iFirstToken, Call.m_iLastToken, nCall)
						&& nCall > nLink
					;
					if (bCallHolds && !bAccessBehind && fp_FitsInline(iLineFirst, umint(iCallHead), nLineIndent))
					{
						++iScope;

						continue;
					}
				}

				if (bOpens && (bMember || bDeclared) && fp_FitsInline(iLineFirst, Link.m_iLastToken, nLineIndent))
				{
					fp_MarkInline(iLineFirst, Link.m_iLastToken);
					fp_BreakBefore(umint(iMember), nContinuation);
					bSplit = true;
					iLineFirst = umint(iMember);
					nLineIndent = nContinuation;
					while (iScope < Scopes.f_GetLen() && Nodes[Scopes[iScope]].m_iFirstToken < iLineFirst)
						++iScope;

					// The chain is broken at every member, as one that resumes under a closing
					// marker is: a statement that gives at its member accesses gives at all of
					// them, rather than ending in a line of as many calls as happened to fit.
					if (bMember && fp_LayoutMembers(_iNode, iLineFirst, _iLast, nLineIndent, nLineIndent))
						break;

					continue;
				}
			}

			auto const &Scope = Nodes[Scopes[iScope]];
			bool bStartsLine = Scope.m_iFirstToken == iLineFirst;
			auto iHead = fp_PreviousCode(Scope.m_iFirstToken);
			if (!bStartsLine && iHead >= 0 && umint(iHead) >= iLineFirst)
			{
				if (fp_FitsInline(iLineFirst, umint(iHead), nLineIndent))
					fp_MarkInline(iLineFirst, umint(iHead));
				else if (fp_LayoutHead(_iNode, iLineFirst, umint(iHead), nLineIndent, nContinuation))
				{
					// The head now ends on a line of its own, and the scope is judged again
					// from where that line starts: behind a closing marker the line is over,
					// and the scope starts the next one.
					bSplit = true;
					umint iLine = iLineFirst;
					for (umint i = iLineFirst + 1; i <= umint(iHead); ++i)
					{
						if (fp_IsBreakGap(i))
							iLine = i;
					}

					if (iLine == iLineFirst)
						continue;

					nLineIndent = m_GapIndent[iLine];
					bool bCloser = false;
					for (auto iChild : Node.m_Children)
						bCloser |= Nodes[iChild].m_iLastToken == iLine;

					if (bCloser)
					{
						iLine = umint(fp_NextCode(iLine));
						fp_BreakBefore(iLine, nLineIndent);
					}

					iLineFirst = iLine;

					continue;
				}
			}

			// A scope that already starts its line owns that line's indentation; one that
			// is pushed off the line it was on opens at the continuation level. A class's
			// own argument list, behind the name its definition declares, is part of a head
			// nothing extends as an expression, and opens at the head's level like a template
			// header does: 'struct TCFoo' / '<' / 't_C' / '>'.
			bool bClassArguments = false;
			if (Scope.m_Bracket == ECodeBracket::mc_Angle && iLineFirst == _iFirst)
			{
				auto const &Keyword = Tokens[_iFirst];
				auto iName = fp_NextCode(_iFirst);
				bClassArguments = (m_Tokens.f_IsText(Keyword, "struct") || m_Tokens.f_IsText(Keyword, "class") || m_Tokens.f_IsText(Keyword, "union"))
					&& iName >= 0
					&& fp_NextCode(umint(iName)) == aint(Scope.m_iFirstToken)
				;
			}
			auto nMarkerIndent = bStartsLine || bClassArguments ? nLineIndent : nContinuation;
			++iScope;
			if (!fp_LayoutGroup(Scopes[iScope - 1], nMarkerIndent, !bStartsLine))
				continue;

			bSplit = true;
			// The base clause behind a class's list goes on the head as a continuation.
			nLineIndent = bClassArguments ? nContinuation : nMarkerIndent;
			auto iNext = fp_NextCode(Scope.m_iLastToken);
			if (iNext < 0 || umint(iNext) > _iLast)
				break;

			// A class's 'final' belongs behind its template argument list, on the closing
			// marker's line. A function's qualifiers and its pure specifier do not: behind an
			// opened parameter list they start the next line, under the marker, the way a
			// trailing return type does.
			auto iResume = umint(iNext);
			if (Scope.m_Bracket == ECodeBracket::mc_Angle && m_Tokens.f_IsText(Tokens[umint(iNext)], "final"))
			{
				auto iAfter = fp_NextCode(umint(iNext));
				iResume = iAfter < 0 ? _iLast + 1 : umint(iAfter);
			}
			else if (Scope.m_Bracket == ECodeBracket::mc_Square && Tokens[umint(iNext)].m_Kind == ECodeTokenKind::mc_Identifier)
			{
				// A bare name behind a capture list, such as an attribute macro, trails the
				// list on its line; the parameter list behind the name starts the next one.
				auto iAfter = fp_NextCode(umint(iNext));
				bool bTrails = iAfter >= 0 && m_Tokens.f_IsText(Tokens[umint(iAfter)], "(") && fg_IsCaptureList(m_Tokens, m_Structure, Scope.m_iLastToken);
				if (bTrails)
					iResume = umint(iAfter);
			}

			auto iLastQualifier = iResume != umint(iNext) ? fp_PreviousCode(iResume) : aint(-1);
			if (iLastQualifier >= 0 && fp_FitsInline(Scope.m_iLastToken, umint(iLastQualifier), nLineIndent))
			{
				fp_MarkInline(Scope.m_iLastToken, umint(iLastQualifier));
				if (iResume > _iLast)
					break;

				iNext = aint(iResume);
			}

			// A pack expansion stays on the closing marker's line: ')...'.
			bool bPastEnd = false;
			for (auto iMarker = Scope.m_iLastToken; iNext >= 0 && umint(iNext) <= _iLast && m_Tokens.f_IsText(Tokens[umint(iNext)], "..."); )
			{
				fp_MarkInline(iMarker, umint(iNext));
				iMarker = umint(iNext);
				iNext = fp_NextCode(iMarker);
				bPastEnd = iNext < 0 || umint(iNext) > _iLast;
			}

			if (bPastEnd)
				break;

			// What follows the scope resumes under its closing marker.
			iLineFirst = umint(iNext);
			fp_BreakBefore(iLineFirst, nLineIndent);

			// Qualifiers moved below an opened parameter list are a line of their own, and a
			// trailing return type behind them takes the next one, as it does behind the list.
			if (Scope.m_Bracket == ECodeBracket::mc_Paren && (fp_IsFunctionQualifier(iLineFirst) || m_Tokens.f_IsText(Tokens[iLineFirst], "=")))
			{
				for (auto iArrow = iLineFirst; iArrow <= _iLast; ++iArrow)
				{
					if (m_TokenDepth[iArrow] != m_TokenDepth[iLineFirst] || !m_Tokens.f_IsText(Tokens[iArrow], "->") || !fp_IsTrailingReturnArrow(iArrow))
						continue;

					auto iQualifiersLast = fp_PreviousCode(iArrow);
					if (iQualifiersLast >= 0 && fp_FitsInline(iLineFirst, umint(iQualifiersLast), nLineIndent))
					{
						fp_MarkInline(iLineFirst, umint(iQualifiersLast));
						fp_BreakBefore(iArrow, nLineIndent);
						iLineFirst = iArrow;
					}

					break;
				}
			}

			// A member chain that resumes there and does not fit is broken at every member,
			// each under the marker. Taken a scope at a time it would be broken only as far
			// as it had to be, and end in a line of as many calls as happened to fit.
			bool bMember = m_Tokens.f_IsText(Tokens[iLineFirst], ".") || m_Tokens.f_IsText(Tokens[iLineFirst], "->");
			if (bMember && !fp_FitsInline(iLineFirst, _iLast, nLineIndent) && fp_LayoutMembers(_iNode, iLineFirst, _iLast, nLineIndent, nLineIndent))
				break;
		}

		return bSplit;
	}

	// Starts the line of a statement's first operand with the '=' in front of it, where the
	// value is broken at the operator at _iOperator, and says whether it did. The head and
	// the operand then have their lines, and what is left is every operator behind them.
	bool CFormattingAnalyzer::fp_BreakAtAssign(umint _iFirst, umint _iOperator, umint _iIndent, umint _nContinuation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		umint iAssign = TCLimitsInt<umint>::mc_Max;
		for (umint i = _iOperator; i > _iFirst; --i)
		{
			if (m_TokenDepth[i] == m_TokenDepth[_iFirst] && m_Tokens.f_IsText(Tokens[i], "="))
			{
				iAssign = i;

				break;
			}
		}

		if (iAssign == TCLimitsInt<umint>::mc_Max)
			return false;

		auto iHeadEnd = fp_PreviousCode(iAssign);
		auto iOperandEnd = fp_PreviousCode(_iOperator);
		bool bMoves = iHeadEnd >= 0
			&& umint(iHeadEnd) >= _iFirst
			&& iOperandEnd > aint(iAssign)
			&& fp_GetCanonicalSpacing(umint(iHeadEnd), iAssign) == ECodeSpacing::mc_Space
			&& fp_FitsInline(_iFirst, umint(iHeadEnd), _iIndent)
			&& fp_FitsInline(iAssign, umint(iOperandEnd), _nContinuation)
		;
		if (!bMoves)
			return false;

		fp_MarkInline(_iFirst, umint(iHeadEnd));
		fp_BreakBefore(iAssign, _nContinuation);
		fp_MarkInline(iAssign, umint(iOperandEnd));

		return true;
	}

	// Lays a range out on as few levels as the column limit allows: the outermost breaks
	// first, and a resulting line is only broken further when it is still too long.
	bool CFormattingAnalyzer::fp_LayoutRange(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndentContinuations, bool _bMustSplit)
	{
		// A lambda body inside the range opens on a line of its own at the range's
		// indentation, its lines following it from where the source wrote them, and what
		// stands in front of it is a line that ends there. What follows the body keeps its
		// place behind the closing brace.
		auto const &Nodes = m_Structure.f_GetNodes();
		for (auto iChild : Nodes[_iNode].m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind != ECodeNodeKind::mc_Block || Child.m_iFirstToken < _iFirst || Child.m_iLastToken > _iLast)
				continue;

			auto iBrace = Child.m_iFirstToken;
			auto nReference = fp_GetSourceLineIndent(_iFirst);
			// A body whose lines cannot follow a move keeps the head it opens under where it
			// is too. Moving the head alone would leave the two at depths that disagree, and
			// the next pass would read that as a layout still to be made.
			if (nReference != _iIndent && !fp_CanPlaceBlock(iChild))
				return false;

			auto iHeadLast = fp_PreviousCode(iBrace);
			if (iHeadLast >= 0 && umint(iHeadLast) >= _iFirst)
				fp_LayoutRange(_iNode, _iFirst, umint(iHeadLast), _iIndent, _bClause, _bIndentContinuations);

			// A body already on a line of its own still moves with its element, so that
			// its depth follows the element's new indentation.
			if (!fp_IsFirstOnLine(iBrace) || fp_GetStatementIndent(iBrace) != _iIndent)
				fp_PlaceBlock(iChild, _iIndent, nReference);

			fp_LayoutNode(iChild, _iIndent);

			return true;
		}

		auto nTab = m_Request.m_Settings.m_nTabWidth;
		// A directive ends the line it stands on, so each stretch of the range between two
		// of them is a line of its own. Each is laid out as one, which is what keeps a
		// construct a conditional runs through from being opened up to make room that no
		// line of it needs.
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		TCVector<umint> Cuts;
		bool bContinuedString = false;
		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			// Only a directive standing at the range's own level cuts it. One inside a
			// construct within it belongs to that construct, and a closing marker behind a
			// directive stays where the marker's own scope puts it.
			// A line comment ends its line the same way, and what stands behind it starts
			// the next: 'class CFoo // Comment' above its base clause.
			// So does a string an element of a group continues on the line below it.
			bool bEndsLine = (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor && !m_bOpaqueDirective[i]) || Tokens[i].m_Kind == ECodeTokenKind::mc_LineComment;
			auto iNext = fp_NextCode(i);
			if (!_bIndentContinuations && Tokens[i].m_Kind == ECodeTokenKind::mc_StringLiteral && iNext >= 0 && fp_ContinuesLiteral(umint(iNext)) == aint(i))
			{
				bEndsLine = true;
				bContinuedString = true;
			}

			if (!bEndsLine || m_TokenDepth[i] != nLevel)
				continue;

			if (iNext < 0 || umint(iNext) > _iLast || umint(iNext) <= _iFirst || m_TokenDepth[umint(iNext)] != nLevel)
				continue;

			Cuts.f_Insert(umint(iNext));
		}

		if (!Cuts.f_IsEmpty())
		{
			// An operator standing at a cut is one the construct is written broken at, so
			// every operator that binds as loosely takes a line of its own even where its
			// segment would have fitted on one. Where no cut stands at an operator there is
			// no such operator to speak of: the branches are alternatives, not one
			// expression, and what any of them holds binds tighter than the cut between
			// them. A segment's scopes are opened only where its line is still too long.
			TCVector<umint> Operators;
			fp_FindLooseOperators(_iFirst, _iLast, Operators);
			bool bCutAtOperator = false;
			for (auto iOperator : Operators)
			{
				for (auto iCut : Cuts)
					bCutAtOperator |= iOperator == iCut;
			}

			bool bCutAtMember = false;
			for (auto iCut : Cuts)
				bCutAtMember |= m_Tokens.f_IsText(Tokens[iCut], ".") || m_Tokens.f_IsText(Tokens[iCut], "->");

			auto nContinuation = _bIndentContinuations ? _iIndent + nTab : _iIndent;
			for (umint iCut = 0; iCut <= Cuts.f_GetLen(); ++iCut)
			{
				auto iStart = iCut ? Cuts[iCut - 1] : _iFirst;
				auto iEnd = iCut < Cuts.f_GetLen() ? umint(fp_PreviousCode(Cuts[iCut])) : _iLast;
				if (iEnd < iStart)
					continue;

				bool bSplitSegment = false;
				for (auto iOperator : Operators)
				{
					// One that opens the segment already stands on a line of its own. A value
					// holding a string continued over lines spans lines, and so gives at each.
					bSplitSegment |= (bCutAtOperator || bContinuedString) && iOperator > iStart && iOperator <= iEnd;
				}

				// A chain written broken at one member access is broken at each of them.
				bool bSplitMembers = false;
				for (umint iMember = iStart + 1; bCutAtMember && iMember <= iEnd; ++iMember)
					bSplitMembers |= m_TokenDepth[iMember] == nLevel && (m_Tokens.f_IsText(Tokens[iMember], ".") || m_Tokens.f_IsText(Tokens[iMember], "->"));

				auto nSegmentIndent = iCut ? nContinuation : _iIndent;
				// A string continued as a value stands one level in: 'DPrefix " a"' over '"b"'.
				if (iCut && nContinuation == _iIndent && fp_ContinuesValue(iStart))
					nSegmentIndent = _iIndent + nTab;

				if (iCut)
					fp_BreakBefore(iStart, nSegmentIndent);

				if (bSplitMembers && !bSplitSegment && fp_LayoutMembers(_iNode, iStart, iEnd, nSegmentIndent, nContinuation))
					continue;

				// The first stretch of a value cut at its operators ends its first operand.
				if (!iCut && _bIndentContinuations && bCutAtOperator && !bSplitSegment && fp_BreakAtAssign(iStart, Cuts[0], _iIndent, nContinuation))
					continue;

				fp_LayoutRange(_iNode, iStart, iEnd, nSegmentIndent, _bClause && !iCut, _bIndentContinuations && !iCut, bSplitSegment);
			}

			return true;
		}

		// Fitting is not the same as being written that way: a range the layout keeps on
		// one line is put there, so the result does not depend on where the source broke.
		if (!_bMustSplit && fp_FitsInline(_iFirst, _iLast, _iIndent))
		{
			fp_MarkInline(_iFirst, _iLast);

			return false;
		}

		TCVector<umint> Operators;
		fp_FindLooseOperators(_iFirst, _iLast, Operators);
		if (Operators.f_IsEmpty())
			return fp_LayoutScopes(_iNode, _iFirst, _iLast, _iIndent, _bClause, _bIndentContinuations, _bMustSplit);

		// A range with a gap the standard does not settle has no single-line form to be
		// measured against, so it is left as it stands rather than taken for one that is
		// too wide, the way its scopes are.
		umint nJoined = 0;
		if (!_bMustSplit && !fp_MeasureJoinedWidth(_iFirst, _iLast, nJoined))
			return false;

		// A statement's continuation is indented past its own start; an element of a group
		// already sits at the group's content indentation and its continuation aligns there.
		// So does what follows a parenthesis the range opens with, once that parenthesis is
		// opened: its closing marker stands at the range's own level, and the operator
		// behind it under that marker.
		bool bLeadsOpened = false;
		if (_bIndentContinuations && m_Tokens.f_IsText(Tokens[_iFirst], "("))
		{
			for (auto iChild : Nodes[_iNode].m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_iFirstToken != _iFirst || Child.m_iLastToken >= Operators[0])
					continue;

				bLeadsOpened = Child.m_bHasBlock || !fp_FitsInline(_iFirst, Child.m_iLastToken, _iIndent);
			}
		}

		auto nContinuation = _bIndentContinuations && !bLeadsOpened ? _iIndent + nTab : _iIndent;
		m_bOperatorSplit |= _bIndentContinuations;
		// A lambda is written behind the operator that takes it, so that operator stays on
		// the line its left hand side ends, and so does the capture list where it fits
		// there: 'g_ActorFunctor / [this]' with the parameter list and the return type
		// below it. Only a capture list too long for that line goes below the operator,
		// 'g_Dispatch /' with the list opened under it. Only the first operator qualifies,
		// and only while the line in front of it is whole.
		bool bOperatorTrails = fp_IsLambdaIntroducer(Operators[0]) && fp_FitsInline(_iFirst, Operators[0], _iIndent);
		umint iHeadEnd = Operators[0];
		if (bOperatorTrails)
		{
			auto iCapture = fp_NextCode(Operators[0]);
			for (auto iChild : Nodes[_iNode].m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind != ECodeNodeKind::mc_Group || iCapture < 0 || Child.m_iFirstToken != umint(iCapture))
					continue;

				if (Child.m_iLastToken <= _iLast && fp_FitsInline(_iFirst, Child.m_iLastToken, _iIndent))
					iHeadEnd = Child.m_iLastToken;

				break;
			}

			fp_BreakAfter(iHeadEnd, nContinuation);
		}

		for (umint i = bOperatorTrails ? 1 : 0; i < Operators.f_GetLen(); ++i)
			fp_BreakBefore(Operators[i], nContinuation);

		// A statement's value broken at its operators starts a line of its own with the
		// '=', so that every operand stands under the one in front of it:
		//   Value
		//       = a
		//       + b
		// That holds where the first operand is a plain line. One that takes a lambda's body
		// or has to be opened keeps the '=' on the head, 'auto Value = fg_Function' with its
		// parenthesis below, and so does the key of a DSL spelling, which hugs its '='. A
		// parameter's default is given the same way, its '=' at the element's level.
		bool bAssignMoved = !bOperatorTrails && !bLeadsOpened && fp_BreakAtAssign(_iFirst, Operators[0], _iIndent, nContinuation);

		for (umint iSegment = 0; iSegment <= Operators.f_GetLen(); ++iSegment)
		{
			// The head and the first operand have their lines already.
			if (!iSegment && bAssignMoved)
				continue;

			auto iStart = iSegment ? Operators[iSegment - 1] : _iFirst;
			auto iEnd = iSegment < Operators.f_GetLen() ? umint(fp_PreviousCode(Operators[iSegment])) : _iLast;
			// An operator left on the previous line belongs to neither segment's own line,
			// nor does the capture list left there with it.
			if (iSegment == 1 && bOperatorTrails)
			{
				auto iNext = fp_NextCode(iHeadEnd);
				if (iNext < 0 || umint(iNext) > iEnd)
					continue;

				iStart = umint(iNext);
			}
			else if (!iSegment && bOperatorTrails)
				iEnd = iHeadEnd;

			if (iEnd < iStart)
				continue;

			auto nSegmentIndent = iSegment ? nContinuation : _iIndent;
			if (fp_FitsInline(iStart, iEnd, nSegmentIndent))
			{
				fp_MarkInline(iStart, iEnd);

				continue;
			}

			// An operand that holds operators binding tighter than the ones it was cut at
			// gives at those first, and they stand at the continuation level with the ones
			// it was cut at: 'a + b > c' with 'a', '+ b' and '> c' each on a line.
			TCVector<umint> Inner;
			auto iInnerFirst = iSegment ? umint(fp_NextCode(iStart)) : iStart;
			if (iInnerFirst <= iEnd)
				fp_FindLooseOperators(iInnerFirst, iEnd, Inner);

			// An operand that holds a lambda is laid out by its scopes, which is where its body
			// and the operator that takes it are placed: '> TestActor / [&]' stays whole.
			for (auto i = iStart; i <= iEnd && !Inner.f_IsEmpty(); ++i)
			{
				if (m_Tokens.f_IsText(Tokens[i], "{") || fp_IsLambdaIntroducer(i))
					Inner.f_Clear();
			}

			if (!Inner.f_IsEmpty())
			{
				umint nJoined = 0;
				if (fp_MeasureJoinedWidth(iStart, iEnd, nJoined))
				{
					// A conditional that is a branch of another binds no tighter than it, and is set
					// off one level deeper instead: ': b' with its '? c' and ': d' indented under it.
					bool bNestedConditional = m_Tokens.f_IsText(Tokens[Inner[0]], "?");
					auto nInnerIndent = bNestedConditional ? nContinuation + nTab : nContinuation;
					for (auto iInnerOperator : Inner)
						fp_BreakBefore(iInnerOperator, nInnerIndent);

					for (umint iPart = 0; iPart <= Inner.f_GetLen(); ++iPart)
					{
						auto iPartStart = iPart ? Inner[iPart - 1] : iStart;
						auto iPartEnd = iPart < Inner.f_GetLen() ? umint(fp_PreviousCode(Inner[iPart])) : iEnd;
						if (iPartEnd < iPartStart)
							continue;

						auto nPartIndent = iPart ? nInnerIndent : nSegmentIndent;
						if (fp_FitsInline(iPartStart, iPartEnd, nPartIndent))
							fp_MarkInline(iPartStart, iPartEnd);
						else
							fp_LayoutScopes(_iNode, iPartStart, iPartEnd, nPartIndent, _bClause && !iSegment && !iPart, false);
					}

					continue;
				}
			}

			fp_LayoutScopes(_iNode, iStart, iEnd, nSegmentIndent, _bClause && !iSegment, _bIndentContinuations && !iSegment);
		}

		return true;
	}
}
