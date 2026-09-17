// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormatting.h"
#include "Malterlib_Develop_CodeFormattingLexer.h"
#include "Malterlib_Develop_CodeFormattingStructure.h"

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
		static ch8 const *const gsc_pMoved[] =
			{
				"const", "volatile", "static", "constexpr"
			}
		;
		CCodeTokenStream Tokens(_Source);
		CStr Text;
		umint nMoved[4] = {};
		for (auto const &Token : Tokens.f_GetTokens())
		{
			if (!fg_IsCodeToken(Token))
				continue;

			bool bMoved = false;
			for (umint i = 0; i < 4 && !bMoved; ++i)
			{
				bMoved = Tokens.f_IsText(Token, gsc_pMoved[i]);
				nMoved[i] += bMoved;
			}

			if (!bMoved)
				Text += Tokens.f_GetText(Token);
		}

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

	// Normalizes a source into comparable token spellings. Closing a nested template
	// argument list regroups '>' '>' into the single token '>>', the same ambiguity the
	// language resolves by context, so both spellings normalize alike.
	static TCVector<CStr> fg_NormalizeCodeTokens(CStr const &_Source)
	{
		CCodeTokenStream Stream(_Source);
		TCVector<CStr> Texts;
		for (auto const &Token : Stream.f_GetTokens())
		{
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineSplice)
				continue;

			auto Text = Stream.f_GetText(Token);
			// Trailing space inside a line comment is layout, not comment text.
			if (Kind == ECodeTokenKind::mc_LineComment)
				Text = fg_TrimCommentTrailingSpace(Text);

			auto fInsert = [&](CStr const &_Text)
				{
					Texts.f_Insert("{}:{}"_f << umint(Kind) << _Text);
				}
			;
			if (Kind == ECodeTokenKind::mc_Punctuator && (Text == ">>" || Text == ">>="))
			{
				fInsert(">");
				fInsert(Text == ">>" ? ">" : ">=");

				continue;
			}

			fInsert(Text);
		}

		return Texts;
	}

	CStr fg_DescribeCodeTokenDifference(CStr const &_First, CStr const &_Second)
	{
		auto First = fg_NormalizeCodeTokens(_First);
		auto Second = fg_NormalizeCodeTokens(_Second);
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

	bool fg_HasEquivalentCodeTokens(CStr const &_First, CStr const &_Second)
	{
		auto First = fg_NormalizeCodeTokens(_First);
		auto Second = fg_NormalizeCodeTokens(_Second);
		if (First.f_GetLen() != Second.f_GetLen())
			return false;

		for (umint i = 0; i < First.f_GetLen(); ++i)
		{
			if (First[i] != Second[i])
				return false;
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
	struct CFormattingAnalyzer
	{
		explicit CFormattingAnalyzer(CCodeFormattingRequest const &_Request, bool _bAllowConversions = true, bool _bAllowQualifiers = true)
			: m_Request(_Request)
			, m_Tokens(_Request.m_Source)
			, m_Structure(m_Tokens)
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
		bool fp_IsLastOnLine(umint _iToken) const;

		void fp_RuleIndentation();
		void fp_RuleTrailingWhitespace();
		void fp_RuleLineEndings();
		void fp_RuleFinalNewline();
		void fp_RuleTokenSpacing();
		bool fp_HasOperand(umint _iToken, bool _bBefore) const;
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
		bool fp_IsLambdaIntroducer(umint _iToken) const;
		bool fp_FollowsScope(umint _iToken) const;
		bool fp_IsFunctionQualifier(umint _iToken) const;
		bool fp_IsTrailingReturnArrow(umint _iToken) const;
		umint fp_SkipFunctionQualifiers(umint _iToken) const;
		bool fp_ClosesLambdaIntroducer(umint _iToken) const;
		umint fp_SkipTemplateHeader(umint _iToken) const;
		bool fp_IsCastGroup(umint _iNode) const;
		bool fp_LayoutScopes(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndent, bool _bMustSplit = false);
		bool fp_LayoutMembers(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation);
		bool fp_LayoutHead(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation);
		void fp_FindLooseOperators(umint _iFirst, umint _iLast, NContainer::TCVector<umint> &o_Operators) const;
		void fp_PrepareTokenDepth();
		bool fp_ConvertTrailingReturn(umint _iNode, umint _iDeclFirst, umint _iIndent);
		void fp_ConvertQualifiers();
		void fp_ConvertSpecifiers();
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
		void fp_PlaceBody(umint _iNode, umint _iBlock, umint _iIndent, bool _bDeclarator);
		bool fp_PlaceBlock(umint _iBlock, umint _iIndent, umint _nReference);
		bool fp_CanPlaceBlock(umint _iBlock) const;
		void fp_ShiftBlock(umint _iBlock, aint _nDelta);
		void fp_PrepareBlockEnds();
		void fp_PrepareDirectives();
		NStr::CStr fp_GetDirectiveKeyword(umint _iToken) const;
		bool fp_IsDirectiveParallel(umint _iDirective, umint _iOpen, umint _iClose) const;
		bool fp_HasOpaqueDirective(umint _iFirst, umint _iLast) const;
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
		bool fp_TryJoin(umint _iFirstToken, umint _iLastToken, umint _iStartColumn);
		bool fp_MeasureJoinedWidth(umint _iFirstToken, umint _iLastToken, umint &o_nColumns) const;
		umint fp_GetTokenColumns(CCodeToken const &_Token) const;
		void fp_DiagnoseLineLength(NContainer::TCVector<CCodeFormattingEdit> const &_Edits);
		void fp_EnsureSingleSpace(umint _iToken, bool _bBefore, CStr const &_Rule, CStr const &_Explanation);
		void fp_RemoveSpaceBefore(umint _iToken, CStr const &_Rule, CStr const &_Explanation);
		void fp_RemoveFollowingBlankLines(umint _iToken, CStr const &_Rule, CStr const &_Explanation);

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
		NContainer::TCVector<uint8> m_bInConditional;			// Indexed by token: the token stands inside a conditional group's branches.
		NContainer::TCVector<CCodeFormattingRange> m_Conditionals;	// Source spans of the '#if' groups, for saying which one a structure was cut by.
		NContainer::TCVector<uint8> m_GapState;					// Indexed by token: what the gap in front of it becomes.
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

			auto Spacing = fg_GetCanonicalSpacing(m_Tokens, m_Structure, umint(iPrevious), i);
			if (Spacing == ECodeSpacing::mc_Space)
				fp_EnsureSingleSpace(i, true, "token-space", "these tokens are written with one space between them");
			else if (Spacing == ECodeSpacing::mc_None)
				fp_RemoveSpaceBefore(i, "token-space", "these tokens are written without a space between them");
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
				if (m_Tokens.f_IsText(Before, "{"))
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
			auto iSource = m_Lines.f_FindLine(fg_MapOffsetToOriginal(_Edits, Lines.f_GetLineStart(iLine)));
			if (iSource == iReported || !fp_IsLineSelected(iSource))
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
		fp_PrepareTokenDepth();
		fp_PrepareDirectives();
		CStr Explanation;
		if (!fp_CollectDisabledRegions(Explanation))
			return fFailed(Explanation);

		if (!fp_ResolveRanges(Explanation))
			return fFailed(Explanation);

		// Moving a return type behind its parameter list, and dropping the braces around a
		// single guarded statement, change tokens, which no other rule does. Those
		// conversions are decided first, and the layout is then made on the converted
		// source, so the lines it settles on are the lines a later pass sees.
		// A qualifier in front of its type moves behind it before anything else is decided.
		// The other conversions rewrite text a qualifier can stand in, a return type above
		// all, so they are made on the source this one leaves, in a stage of their own.
		m_Baseline = m_Request.m_Source;
		if (m_bAllowQualifiers)
		{
			fp_ConvertQualifiers();
			fp_ConvertSpecifiers();
		}

		bool bQualifierStage = !m_Structural.f_IsEmpty();
		if (!bQualifierStage && m_bAllowConversions)
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
			fp_RuleBlankLines();
			fp_RuleLineBreaks();
			fp_RuleTokenSpacing();
			fp_EmitLayout();
			// The layout writes the indentation of every line it places, so what this rule
			// is left to spell is only the lines it did not.
			fp_RuleIndentation();

			fp_LimitJoinedLines();

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

		// Every rule but the conversion leaves the token stream alone, so the result has to
		// match the converted source token for token.
		auto Formatted = fg_ApplyCodeFormattingEdits(m_Request.m_Source, Result.m_Edits);
		if (!fg_HasEquivalentCodeTokens(m_Baseline, Formatted))
			return fFailed("Formatting would change the token stream ({}); no edits were produced"_f << fg_DescribeCodeTokenDifference(m_Baseline, Formatted));

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

				// Two closers of nested template argument lists are written as one '>>'
				// whatever the source has between them.
				bool bClosers = m_Structure.f_IsAngleBracket(iPrevious) && m_Structure.f_IsAngleBracket(i)
					&& m_Tokens.f_IsText(Tokens[iPrevious], ">") && m_Tokens.f_IsText(Token, ">")
				;
				if (bClosers || bComment)
					nColumns += 0;
				else if (!bNewline)
					nColumns += nGap;
				else
				{
					auto Spacing = fg_GetCanonicalSpacing(m_Tokens, m_Structure, iPrevious, i);
					if (Spacing == ECodeSpacing::mc_Preserve)
						return false;

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
					auto Spacing = fg_GetCanonicalSpacing(m_Tokens, m_Structure, iPrevious, i);
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

					iStart += nKeep;
					nLength -= nKeep;

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

				auto Spacing = fg_GetCanonicalSpacing(m_Tokens, m_Structure, umint(iPrevious), i);
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
		static ch8 const *const gsc_pSpecifiers[] =
			{
				"inline", "constinit", "extern", "virtual", "friend", "thread_local", "mutable", "explicit", "inline_always", "inline_never"
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

				bool bSpecifier = false;
				for (auto pSpecifier : gsc_pSpecifiers)
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

		static ch8 const *const gsc_pSpecifiers[] =
			{
				"static", "inline", "constexpr", "consteval", "constinit", "extern", "virtual", "friend", "thread_local", "typedef"
				, "mutable", "explicit", "register"
			}
		;
		static ch8 const *const gsc_pOpeners[] =
			{
				"(", ",", "<", ";", "{", "}", "->", "=", "]", ":", "operator", "new"
			}
		;
		static ch8 const *const gsc_pFundamental[] =
			{
				"signed", "unsigned", "short", "long", "int", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "bool", "float", "double", "void"
			}
		;
		static ch8 const *const gsc_pElaborated[] =
			{
				"struct", "class", "union", "enum"
			}
		;
		// What a function's qualifier is followed by, and what an expression starts with:
		// names a type is never spelled with.
		static ch8 const *const gsc_pNoType[] =
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
				if (m_Tokens.f_IsText(Tokens[j], "typename") || fIsAny(j, gsc_pElaborated))
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
				else if (fIsAny(j, gsc_pFundamental))
				{
					iEnd = j;
					for (auto iNext = fp_NextCode(iEnd); iNext >= 0 && fIsAny(umint(iNext), gsc_pFundamental); iNext = fp_NextCode(iEnd))
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
						bool bName = Tokens[j].m_Kind == ECodeTokenKind::mc_Identifier && !fIsQualifier(j) && !fIsAny(j, gsc_pNoType) && !fIsAny(j, gsc_pSpecifiers);
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
					|| (Token.m_Kind == ECodeTokenKind::mc_Identifier && !fIsAny(umint(_iToken), gsc_pNoType))
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

			bool bLeads = iBefore < 0 || fIsAny(umint(iBefore), gsc_pOpeners) || fIsAny(umint(iBefore), gsc_pSpecifiers);
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
			while (iType >= 0 && fIsAny(umint(iType), gsc_pSpecifiers))
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
			return false;

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
		static ch8 const *const gsc_pSpecifiers[] =
			{
				"static", "virtual", "inline", "constexpr", "consteval", "constinit", "explicit", "friend", "extern"
				, "mutable", "thread_local", "inline_always", "inline_never", "inline_small", "inline_medium"
				, "inline_large", "inline_extralarge", "mark_nodebug"
			}
		;
		umint iReturn = _iDeclFirst;
		while (iReturn < iDeclarator)
		{
			auto const &Token = Tokens[iReturn];
			bool bSkip = false;
			for (auto pSpecifier : gsc_pSpecifiers)
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
		static ch8 const *const gsc_pStatementKeywords[] =
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
		static ch8 const *const gsc_pTypeWords[] =
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
				for (auto pKeyword : gsc_pStatementKeywords)
				{
					if (m_Tokens.f_IsText(Token, pKeyword))
						return false;
				}

				bool bTypeWord = false;
				for (auto pWord : gsc_pTypeWords)
					bTypeWord |= m_Tokens.f_IsText(Token, pWord);

				if (bAfterName && !bTypeWord)
					return false;

				bAfterName = true;

				continue;
			}

			bAfterName = false;

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
		auto nAuto = _iIndent + CStr("auto ").f_GetLen();
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

		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Block)
				fp_LayoutNode(iChild, fp_GetStatementIndent(Nodes[iChild].m_iFirstToken));
			else
				fp_JoinNode(iChild);
		}
	}

	// A member initializer list is its own line structure: one entry per line, each split
	// only when that entry does not fit. It is never folded onto the signature.
	umint CFormattingAnalyzer::fp_FindInitializerList(umint _iNode, umint _iFirstParen, umint _iLast) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (!_iFirstParen)
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
		}
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
			static ch8 const *const gsc_pQualifiers[] =
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
				for (auto pQualifier : gsc_pQualifiers)
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
		umint iDeclFirst = Node.m_iFirstToken;
		while (Node.m_bTemplateHeader)
		{
			auto iNext = fp_SkipTemplateHeader(iDeclFirst);
			if (iNext == iDeclFirst || iNext > iSignatureLast)
				break;

			// The header itself still comes back to one line where it fits.
			auto iHeaderLast = fp_PreviousCode(iNext);
			if (!m_bProbing && iHeaderLast >= 0 && umint(iHeaderLast) > iDeclFirst && fp_FitsInline(iDeclFirst, umint(iHeaderLast), _iIndent))
				fp_MarkInline(iDeclFirst, umint(iHeaderLast));

			iDeclFirst = iNext;
		}

		bool bJoinable = fp_IsRangeJoinable(_iNode, iDeclFirst, iSignatureLast)
			&& !Node.m_bFixedLineBreaks
			&& Node.m_Kind != ECodeNodeKind::mc_Unsupported
			&& fp_IsFirstOnLine(iDeclFirst)
		;
		if (iInitializerList != TCLimitsInt<umint>::mc_Max)
			iHeadLast = iSignatureLast;

		bool bFits = bJoinable && fp_FitsInline(iDeclFirst, iHeadLast, _iIndent);

		// The first phase only decides which return types move. A declaration that fits as
		// it stands has no reason to; one that does not is converted where that lets its
		// name fit, and the converted source is what gets laid out.
		if (m_bProbing)
		{
			if (bJoinable && !bFits)
				fp_ConvertTrailingReturn(_iNode, iDeclFirst, _iIndent);

			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, _iIndent, !bLambdaBody);
				fp_LayoutNode(iBlock, _iIndent);
			}

			fp_ProbeBodies(_iNode, _iIndent);

			return;
		}

		if (bFits)
		{
			fp_MarkInline(iDeclFirst, iHeadLast);
			if (iInitializerList != TCLimitsInt<umint>::mc_Max)
				fp_LayoutInitializerList(_iNode, iInitializerList, iHeadLastWithInitializers, _iIndent + nTab);

			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, _iIndent, !bLambdaBody);
				fp_LayoutNode(iBlock, _iIndent);

				// A lambda's terminator stands on a line of its own, at the statement's
				// indentation, where a declaration's stays behind its closing brace: '};'.
				// A body that could not be placed keeps its terminator as written too.
				auto iLast = Node.m_iLastToken;
				bool bLambdaTerminator = bLambdaBody
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
				bMustSplit = fp_FitsInline(iDeclFirst, iRangeLast, _iIndent);
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
			bool bSplit = fp_LayoutRange(_iNode, iDeclFirst, iSignatureEnd, _iIndent, bClause, true, bMustSplit);
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
			bool bBodyTerminator = iBlock != TCLimitsInt<umint>::mc_Max && !bLambdaBody;
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
			// The head cannot be relaid out, but its inner constructs still can.
			for (auto iChild : Node.m_Children)
			{
				if (Nodes[iChild].m_Kind != ECodeNodeKind::mc_Block)
					fp_JoinNode(iChild);
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
				fp_PlaceBody(_iNode, iBlock, _iIndent, !bLambdaBody);
			else if (bJoinable && !m_bOperatorSplit)
				fp_BreakBefore(iBrace, bLambdaBody ? _iIndent + nTab : _iIndent);

			fp_LayoutNode(iBlock, _iIndent);
		}
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
		auto nGuardIndent = fp_GetStatementIndent(Guard.m_iFirstToken);
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
		auto nGuardIndent = fp_GetStatementIndent(Guard.m_iFirstToken);

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
		if (bBraced)
		{
			if (!fp_IsFirstOnLine(Node.m_iLastToken))
				fp_OwnLineBefore(Node.m_iLastToken, _iIndent);
			else if (bDepths && !m_bInConditional[Node.m_iLastToken] && fp_GetStatementIndent(Node.m_iLastToken) != _iIndent)
				fp_IndentBefore(Node.m_iLastToken, _iIndent);
		}

		static ch8 const *const gsc_pStatementKeywords[] =
			{
				"else", "while", "catch", "if", "for", "do", "switch", "try", "return", "co_return", "break", "continue", "goto", "case", "default", "{"
			}
		;
		umint nLevel = bBraced ? _iIndent + nTab : _iIndent;
		umint nPlaced = nLevel;
		// The clause whose statement is still to come, and the depth that clause was
		// written at: what it guards stands one level in from there.
		umint iGuard = TCLimitsInt<umint>::mc_Max;
		umint nGuard = 0;
		umint iPrevious = TCLimitsInt<umint>::mc_Max;
		bool bOnLabelLine = false;
		for (auto iChild : Node.m_Children)
		{
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

			// A case written on its label's line stays there whole: 'case 1: a = 1; break;'.
			bOnLabelLine = !bFirstOnLine && (bLabelled || bOnLabelLine);
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

			bool bStays = Child.m_Kind == ECodeNodeKind::mc_Unsupported || bOnLabelLine || bElseIf || bAttribute || m_Tokens.f_IsText(First, ";");
			// Behind a closing brace only a keyword starts a statement of its own, since a
			// name there declares a variable of the type just defined.
			if (!bStays && !bFirstOnLine && bAfterBlock)
			{
				bool bKeyword = false;
				for (auto pKeyword : gsc_pStatementKeywords)
					bKeyword |= m_Tokens.f_IsText(First, pKeyword);

				bStays = !bKeyword;
			}

			umint nWritten = bFirstOnLine ? fp_GetStatementIndent(iFirst) : nPlaced;
			if (!bStays && !bFirstOnLine)
			{
				fp_OwnLineBefore(iFirst, nPlace);
				nWritten = nPlace;
			}
			else if (!bStays && bDepths && !m_bInConditional[iFirst] && nWritten != nPlace && !fp_KeepsOwnLines(iChild))
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

		if (Node.m_Kind == ECodeNodeKind::mc_Statement)
			fp_LayoutStatement(_iNode, fp_GetStatementIndent(Node.m_iFirstToken));
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

		// Which tokens a conditional holds. The depth the sources give the lines inside one
		// varies with the file, so the standard does not settle it and they keep theirs.
		m_bInConditional.f_SetLen(Tokens.f_GetLen());
		umint nDepth = 0;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
			{
				auto Keyword = fp_GetDirectiveKeyword(i);
				if (Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef")
					++nDepth;
				else if (Keyword == "endif" && nDepth)
					--nDepth;
			}

			m_bInConditional[i] = nDepth != 0;
		}
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
		static CPrecedence const gsc_Operators[] =
			{
				{"*", 5}, {"/", 5}, {"%", 5}, {"+", 6}, {"-", 6}, {"<<", 7}, {">>", 7}, {"<=>", 8}
				, {"<", 9}, {">", 9}, {"<=", 9}, {">=", 9}, {"==", 10}, {"!=", 10}
				, {"&", 11}, {"^", 12}, {"|", 13}, {"&&", 14}, {"||", 15}
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
				for (auto const &Operator : gsc_Operators)
				{
					if (m_Tokens.f_IsText(Tokens[i], Operator.m_pText))
						nPrecedence = Operator.m_Level;
				}

				// Without operands on both sides the token is a declarator or a unary form.
				if (!nPrecedence || i == _iFirst || !fp_HasOperand(i, true) || !fp_HasOperand(i, false))
					continue;

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
				// body, a pure specifier, a requires clause, or another qualifier.
				if (m_Tokens.f_IsText(Tokens[i], "&") || m_Tokens.f_IsText(Tokens[i], "&&"))
				{
					auto iNext = fp_NextCode(i);
					bool bQualifier = iNext >= 0
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
	}

	// A lambda follows the operator at _iToken when the next thing is a capture list with
	// a parameter list or a body behind it.
	bool CFormattingAnalyzer::fp_IsLambdaIntroducer(umint _iToken) const
	{
		auto iNext = fp_NextCode(_iToken);
		if (iNext < 0 || !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iNext)], "["))
			return false;

		auto const &Nodes = m_Structure.f_GetNodes();
		for (auto const &Node : Nodes)
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_iFirstToken != umint(iNext))
				continue;

			auto iAfter = fp_NextCode(Node.m_iLastToken);
			if (iAfter < 0)
				return false;

			// The capture list is followed by the parameter list, by the body, or by the
			// lambda's own template parameter list.
			auto const &After = m_Tokens.f_GetTokens()[umint(iAfter)];

			return m_Tokens.f_IsText(After, "(") || m_Tokens.f_IsText(After, "{") || m_Tokens.f_IsText(After, "<");
		}

		return false;
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

	// Steps over the run of qualifiers a declarator's parameter list may carry, including
	// the argument one of them can take.
	umint CFormattingAnalyzer::fp_SkipFunctionQualifiers(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		aint i = aint(_iToken);
		while (i >= 0)
		{
			// A pure specifier or a defaulted or deleted definition ends the declarator the
			// same way and stands behind the parenthesis with the qualifiers: ') const = 0'.
			if (m_Tokens.f_IsText(Tokens[umint(i)], "="))
			{
				auto iValue = fp_NextCode(umint(i));
				if (iValue < 0)
					break;

				auto const &Value = Tokens[umint(iValue)];
				if (!m_Tokens.f_IsText(Value, "0") && !m_Tokens.f_IsText(Value, "default") && !m_Tokens.f_IsText(Value, "delete"))
					break;

				i = fp_NextCode(umint(iValue));

				break;
			}

			if (!fp_IsFunctionQualifier(umint(i)))
				break;

			auto iNext = fp_NextCode(umint(i));
			if (iNext >= 0 && m_Tokens.f_IsText(Tokens[umint(iNext)], "("))
			{
				for (auto const &Node : m_Structure.f_GetNodes())
				{
					if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_iFirstToken != umint(iNext))
						continue;

					iNext = fp_NextCode(Node.m_iLastToken);

					break;
				}
			}

			i = iNext;
		}

		return i < 0 ? _iToken : umint(i);
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

		auto iAfter = fp_NextCode(_iToken);

		return iAfter >= 0
			&& (m_Tokens.f_IsText(Tokens[umint(iAfter)], "(") || m_Tokens.f_IsText(Tokens[umint(iAfter)], "{") || m_Tokens.f_IsText(Tokens[umint(iAfter)], "<"))
		;
	}

	// The words that may stand between a parameter list and a trailing return type.
	bool CFormattingAnalyzer::fp_IsFunctionQualifier(umint _iToken) const
	{
		static ch8 const *const gsc_pQualifiers[] =
			{
				"const", "volatile", "noexcept", "mutable", "override", "final", "&", "&&"
			}
		;
		auto const &Token = m_Tokens.f_GetTokens()[_iToken];
		for (auto pQualifier : gsc_pQualifiers)
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
			return true;

		// A lambda's template parameter list is a scope of its own rather than part of a
		// name, which is what a template argument list behind an identifier is.
		if (!m_Structure.f_IsAngleBracket(_iToken))
			return false;

		for (auto const &Node : m_Structure.f_GetNodes())
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group || Node.m_Bracket != ECodeBracket::mc_Angle || Node.m_iLastToken != _iToken)
				continue;

			auto iBefore = fp_PreviousCode(Node.m_iFirstToken);

			return iBefore >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBefore)], "]");
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

	// The last resort for a line nothing else can shorten: every member access at the
	// line's own level takes a line of its own, and each part is then laid out on its own.
	bool CFormattingAnalyzer::fp_LayoutMembers(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		TCVector<umint> Members;
		for (umint i = _iFirst + 1; i <= _iLast; ++i)
		{
			if (m_TokenDepth[i] != nLevel || (!m_Tokens.f_IsText(Tokens[i], ".") && !m_Tokens.f_IsText(Tokens[i], "->")))
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

	// A name too long for its line even with its parameter list opened can only give at its
	// own scopes: the template argument list it carries, and after that its member accesses.
	bool CFormattingAnalyzer::fp_LayoutHead(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation)
	{
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
				// rather than being a scope of their own.
				if (fp_IsCastGroup(iChild))
					continue;

				bool bBeforeName = bStatement && Child.m_iLastToken < m_iSplitFirstParen;
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
			bool bIntroducer = m_Tokens.f_IsText(Before, "]") || m_Structure.f_IsAngleBracket(umint(iBefore));
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
		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			if (m_TokenDepth[i] != nLevel || !m_Tokens.f_IsText(Tokens[i], "->"))
				continue;

			auto iBefore = fp_PreviousCode(i);
			if (iBefore < 0 || umint(iBefore) < _iFirst)
				continue;

			if (fp_IsTrailingReturnArrow(i))
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
		bool bMustSplit = _bMustSplit;
		while (true)
		{
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

			bMustSplit = false;
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
			// is pushed off the line it was on opens at the continuation level.
			auto nMarkerIndent = bStartsLine ? nLineIndent : nContinuation;
			++iScope;
			if (!fp_LayoutGroup(Scopes[iScope - 1], nMarkerIndent, !bStartsLine))
				continue;

			bSplit = true;
			nLineIndent = nMarkerIndent;
			auto iNext = fp_NextCode(Scope.m_iLastToken);
			if (iNext < 0 || umint(iNext) > _iLast)
				break;

			// A function's qualifiers belong behind the closing parenthesis, on its line,
			// wherever there is room for them, and a class's 'final' behind its template
			// argument list the same way.
			auto iResume = umint(iNext);
			if (Scope.m_Bracket == ECodeBracket::mc_Paren)
				iResume = fp_SkipFunctionQualifiers(umint(iNext));
			else if (Scope.m_Bracket == ECodeBracket::mc_Angle && m_Tokens.f_IsText(Tokens[umint(iNext)], "final"))
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

			// What follows the scope resumes under its closing marker.
			iLineFirst = umint(iNext);
			fp_BreakBefore(iLineFirst, nLineIndent);

			// A member chain that resumes there and does not fit is broken at every member,
			// each under the marker. Taken a scope at a time it would be broken only as far
			// as it had to be, and end in a line of as many calls as happened to fit.
			bool bMember = m_Tokens.f_IsText(Tokens[iLineFirst], ".") || m_Tokens.f_IsText(Tokens[iLineFirst], "->");
			if (bMember && !fp_FitsInline(iLineFirst, _iLast, nLineIndent) && fp_LayoutMembers(_iNode, iLineFirst, _iLast, nLineIndent, nLineIndent))
				break;
		}

		return bSplit;
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
		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			// Only a directive standing at the range's own level cuts it. One inside a
			// construct within it belongs to that construct, and a closing marker behind a
			// directive stays where the marker's own scope puts it.
			// A line comment ends its line the same way, and what stands behind it starts
			// the next: 'class CFoo // Comment' above its base clause.
			bool bEndsLine = (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor && !m_bOpaqueDirective[i]) || Tokens[i].m_Kind == ECodeTokenKind::mc_LineComment;
			if (!bEndsLine || m_TokenDepth[i] != nLevel)
				continue;

			auto iNext = fp_NextCode(i);
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
					// One that opens the segment already stands on a line of its own.
					bSplitSegment |= bCutAtOperator && iOperator > iStart && iOperator <= iEnd;
				}

				// A chain written broken at one member access is broken at each of them.
				bool bSplitMembers = false;
				for (umint iMember = iStart + 1; bCutAtMember && iMember <= iEnd; ++iMember)
					bSplitMembers |= m_TokenDepth[iMember] == nLevel && (m_Tokens.f_IsText(Tokens[iMember], ".") || m_Tokens.f_IsText(Tokens[iMember], "->"));

				auto nSegmentIndent = iCut ? nContinuation : _iIndent;
				if (iCut)
					fp_BreakBefore(iStart, nSegmentIndent);

				if (bSplitMembers && !bSplitSegment && fp_LayoutMembers(_iNode, iStart, iEnd, nSegmentIndent, nContinuation))
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

		// A statement's continuation is indented past its own start; an element of a group
		// already sits at the group's content indentation and its continuation aligns there.
		auto nContinuation = _bIndentContinuations ? _iIndent + nTab : _iIndent;
		m_bOperatorSplit |= _bIndentContinuations;
		// A lambda is written behind the operator that takes it, so that operator stays on
		// the line its left hand side ends: 'g_Dispatch /' with the lambda below it. Only
		// the first operator qualifies, and only while the line in front of it is whole.
		bool bOperatorTrails = fp_IsLambdaIntroducer(Operators[0]) && fp_FitsInline(_iFirst, Operators[0], _iIndent);
		if (bOperatorTrails)
			fp_BreakAfter(Operators[0], nContinuation);

		for (umint i = bOperatorTrails ? 1 : 0; i < Operators.f_GetLen(); ++i)
			fp_BreakBefore(Operators[i], nContinuation);

		for (umint iSegment = 0; iSegment <= Operators.f_GetLen(); ++iSegment)
		{
			auto iStart = iSegment ? Operators[iSegment - 1] : _iFirst;
			auto iEnd = iSegment < Operators.f_GetLen() ? umint(fp_PreviousCode(Operators[iSegment])) : _iLast;
			// An operator left on the previous line belongs to neither segment's own line.
			if (iSegment == 1 && bOperatorTrails)
			{
				auto iNext = fp_NextCode(iStart);
				if (iNext < 0 || umint(iNext) > iEnd)
					continue;

				iStart = umint(iNext);
			}
			else if (!iSegment && bOperatorTrails)
				iEnd = Operators[0];

			if (iEnd < iStart)
				continue;

			auto nSegmentIndent = iSegment ? nContinuation : _iIndent;
			if (fp_FitsInline(iStart, iEnd, nSegmentIndent))
				fp_MarkInline(iStart, iEnd);
			else
				fp_LayoutScopes(_iNode, iStart, iEnd, nSegmentIndent, _bClause && !iSegment, _bIndentContinuations && !iSegment);
		}

		return true;
	}
}
