// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

	umint fg_ParsePositiveProperty(CStr const &_Value, CStr const &_Name)
	{
		umint Value = 0;
		for (auto pParse = _Value.f_GetStr(); *pParse; ++pParse)
		{
			auto Char = *pParse;
			if (!fg_CharIsNumber(Char) || Value > (TCLimitsInt<umint>::mc_Max - umint(Char - '0')) / 10)
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

	// A source with some of its lines left out: each deletion starts at an offset of the variant and of the original and
	// takes a length of the original.
	struct CSourceVariant
	{
		CStr m_Source;
		TCVector<CCodeFormattingRange> m_Deleted; // Original coordinates, ascending.
		TCVector<umint> m_DeletedAt;              // Variant coordinates of each deletion.
	};

	CSourceVariant fg_BuildSourceVariant(CStr const &_Source, TCVector<CCodeFormattingRange> &&_Deleted)
	{
		_Deleted.f_Sort
			(
				[](CCodeFormattingRange const &_Left, CCodeFormattingRange const &_Right)
				{
					return _Left.m_iOffset <=> _Right.m_iOffset;
				}
			)
		;

		CSourceVariant Variant;
		{
			auto pSource = _Source.f_GetStr();
			CStr::CAppender Appender(Variant.m_Source);
			umint iCopied = 0;

			for (auto const &Deleted : _Deleted)
			{
				if (!Variant.m_Deleted.f_IsEmpty() && Variant.m_Deleted.f_GetLast().f_GetEnd() == Deleted.m_iOffset)
				{
					Variant.m_Deleted.f_GetLast().m_nLength += Deleted.m_nLength;
					iCopied = Deleted.f_GetEnd();

					continue;
				}

				Appender.f_AddString(pSource + iCopied, Deleted.m_iOffset - iCopied);
				Variant.m_DeletedAt.f_Insert(Appender.f_GetStrLen());
				Variant.m_Deleted.f_Insert(Deleted);
				iCopied = Deleted.f_GetEnd();
			}

			Appender.f_AddString(pSource + iCopied, _Source.f_GetLen() - iCopied);
		}

		return Variant;
	}

	// An offset of the variant in the original; one where lines were left out is in front of them, or behind them.
	umint fg_ToOriginal(CSourceVariant const &_Variant, umint _iOffset, bool _bBehind)
	{
		umint nDeleted = 0;
		for (umint i = 0; i < _Variant.m_DeletedAt.f_GetLen(); ++i)
		{
			auto iDeletedAt = _Variant.m_DeletedAt[i];
			if (iDeletedAt < _iOffset || (iDeletedAt == _iOffset && _bBehind))
				nDeleted += _Variant.m_Deleted[i].m_nLength;
		}

		return _iOffset + nDeleted;
	}

	// An offset of the original, outside the lines left out, in the variant.
	umint fg_ToVariant(CSourceVariant const &_Variant, umint _iOffset)
	{
		umint nDeleted = 0;
		for (auto const &Deleted : _Variant.m_Deleted)
		{
			if (Deleted.f_GetEnd() <= _iOffset)
				nDeleted += Deleted.m_nLength;
		}

		return _iOffset - nDeleted;
	}

	// Splits a whitespace edit at the positions inside it. The piece in front of a position takes as much of the replacement
	// as holds the newlines in front of the position, so a gap's line breaks stay with the lines they end. False for an edit
	// cut through anything but whitespace, or whose replacement joins the lines a position stands between.
	bool fg_SplitEdit(CStr const &_Source, CCodeFormattingEdit const &_Edit, TCVector<umint> const &_Cuts, TCVector<CCodeFormattingEdit> &o_Pieces)
	{
		auto iStart = _Edit.m_iOffset;
		auto iEnd = _Edit.f_GetEnd();

		bool bCut = false;
		for (auto iCut : _Cuts)
			bCut = bCut || (iCut > iStart && iCut < iEnd);

		if (!bCut)
		{
			o_Pieces.f_Insert(_Edit);

			return true;
		}

		auto pSource = _Source.f_GetStr();
		for (auto i = iStart; i < iEnd; ++i)
		{
			auto Char = pSource[i];
			if (Char != ' ' && Char != '\t' && Char != '\r' && Char != '\n')
				return false;
		}

		CStr Rest = _Edit.m_Replacement;
		for (auto iCut : _Cuts)
		{
			if (iCut <= iStart || iCut >= iEnd)
				continue;

			umint nNewlines = 0;
			for (auto i = iStart; i < iCut; ++i)
				nNewlines += pSource[i] == '\n';

			umint iSplit = 0;
			umint nSeen = 0;
			auto pRest = Rest.f_GetStr();

			for (umint i = 0; i < Rest.f_GetLen() && nSeen < nNewlines; ++i)
			{
				if (pRest[i] == '\n')
				{
					++nSeen;
					iSplit = i + 1;
				}
			}

			if (nNewlines && !nSeen)
				return false;

			auto &Piece = o_Pieces.f_Insert();
			Piece.m_iOffset = iStart;
			Piece.m_nLength = iCut - iStart;
			Piece.m_Replacement = Rest.f_Left(iSplit);
			Piece.m_Rule = _Edit.m_Rule;

			Rest = Rest.f_Extract(iSplit);
			iStart = iCut;
		}

		auto &Piece = o_Pieces.f_Insert();
		Piece.m_iOffset = iStart;
		Piece.m_nLength = iEnd - iStart;
		Piece.m_Replacement = Rest;
		Piece.m_Rule = _Edit.m_Rule;

		return true;
	}

	CCodeFormattingResult fg_AnalyzeCodeFormattingVerified(CCodeFormattingRequest const &_Request, bool _bVerifyAlternatives);

	// A file the conditionals' alternative heads leave without a structure is formatted once for each alternative, the
	// others left out with every directive of those conditionals: each branch is laid out in the variant that holds it, the
	// code around them in the first, and every directive line as the plain analysis, which sees them all, writes it. Any
	// edit that cannot be told apart that way, or a result the next pass would change, leaves the plain analysis's result.
	// The variants make no conversion, so the file gets none.
	CCodeFormattingResult fg_AnalyzeAlternatives(CCodeFormattingRequest const &_Request, CFormattingAnalyzer const &_Analyzer, CCodeFormattingResult &&_Plain, bool _bVerify)
	{
		bool bCut = false;
		for (auto const &Diagnostic : _Plain.m_Diagnostics)
			bCut = bCut || Diagnostic.m_Rule == "structure";

		TCVector<CAlternativeHeads> Groups;
		bool bSupported = bCut && _Plain.m_Status == ECodeFormattingStatus::mc_Complete && _Request.m_Ranges.f_IsEmpty() && _Analyzer.f_FindAlternativeHeads(Groups) && !Groups.f_IsEmpty();
		if (!bSupported)
			return fg_Move(_Plain);

		// Where the alternatives cannot be laid out, the plain plan stands, which the analysis it was verified by saw
		// without them: the whole analysis, whose alternatives can lay out what it left, runs on its result until that is
		// stable, its edits composed with the plan.
		auto fFallback = [&]() -> CCodeFormattingResult
			{
				if (!_bVerify || _Plain.m_Edits.f_IsEmpty())
					return fg_Move(_Plain);

				auto fFailed = [&](CStr const &_Explanation)
					{
						CCodeFormattingResult Failed;
						Failed.m_Status = ECodeFormattingStatus::mc_Failed;
						Failed.m_Explanation = _Explanation;
						Failed.m_EffectiveRanges = _Plain.m_EffectiveRanges;

						return Failed;
					}
				;

				auto Formatted = fg_ApplyCodeFormattingEdits(_Request.m_Source, _Plain.m_Edits);
				for (umint iPass = 0;; ++iPass)
				{
					auto CheckRequest = _Request;
					CheckRequest.m_Source = Formatted;
					auto Check = fg_AnalyzeCodeFormattingVerified(CheckRequest, false);
					if (Check.m_Status != ECodeFormattingStatus::mc_Complete)
						return fFailed("Formatting did not reach a stable result: {}"_f << Check.m_Explanation);

					if (Check.m_Edits.f_IsEmpty())
					{
						if (iPass)
							_Analyzer.f_ReplaceResidualDiagnostics(_Plain.m_Edits, Check.m_Diagnostics, _Plain.m_Diagnostics);

						return fg_Move(_Plain);
					}

					if (iPass + 1 == 4)
						return fFailed("Formatting did not reach a stable result: the conditional's alternative heads would lay out the formatted file again");

					TCVector<CCodeFormattingEdit> Composed;
					CStr Explanation;
					if (!fg_ComposeEdits(_Plain.m_Edits, Formatted, Check.m_Edits, Composed, Explanation))
						return fFailed(Explanation);

					auto Further = fg_ApplyCodeFormattingEdits(Formatted, Check.m_Edits);
					if (fg_ApplyCodeFormattingEdits(_Request.m_Source, Composed) != Further)
						return fFailed("Formatting did not reach a stable result: the passes' edits do not compose");

					_Analyzer.f_AddPassDiagnostics(_Plain.m_Edits, Check.m_Diagnostics, true, _Plain.m_Diagnostics);
					_Plain.m_Edits = fg_Move(Composed);
					Formatted = fg_Move(Further);
				}
			}
		;

		auto const &Source = _Request.m_Source;
		auto pSource = Source.f_GetStr();
		auto Directives = _Analyzer.f_GetDirectiveSpans();

		umint nVariants = 0;
		for (auto const &Group : Groups)
			nVariants = fg_Max(nVariants, Group.m_Branches.f_GetLen());

		// The alternative a byte of the original is laid out in, or -1 for a directive line.
		auto fOwner = [&](umint _iOffset) -> aint
			{
				for (auto const &Directive : Directives)
				{
					if (_iOffset >= Directive.m_iOffset && _iOffset < Directive.f_GetEnd())
						return -1;
				}

				for (auto const &Group : Groups)
				{
					for (umint iBranch = 0; iBranch < Group.m_Branches.f_GetLen(); ++iBranch)
					{
						auto const &Branch = Group.m_Branches[iBranch];
						if (_iOffset >= Branch.m_iOffset && _iOffset < Branch.f_GetEnd())
							return aint(iBranch);
					}
				}

				return 0;
			}
		;

		TCVector<CCodeFormattingEdit> Edits;
		TCVector<CCodeFormattingDiagnostic> Diagnostics;

		auto fKeep = [&](CCodeFormattingEdit &&_Edit)
			{
				if (!fg_IsSameText(_Edit.m_Replacement, pSource + _Edit.m_iOffset, _Edit.m_nLength))
					Edits.f_Insert(fg_Move(_Edit));
			}
		;

		// The directive lines as the plain analysis writes them.
		{
			TCVector<umint> Cuts;
			for (auto const &Directive : Directives)
			{
				Cuts.f_Insert(Directive.m_iOffset);
				Cuts.f_Insert(Directive.f_GetEnd());
			}

			for (auto const &Edit : _Plain.m_Edits)
			{
				TCVector<CCodeFormattingEdit> Pieces;
				if (!fg_SplitEdit(Source, Edit, Cuts, Pieces))
					return fFallback();

				for (auto &Piece : Pieces)
				{
					if (fOwner(Piece.m_iOffset) < 0)
						fKeep(fg_Move(Piece));
				}
			}

			for (auto const &Diagnostic : _Plain.m_Diagnostics)
			{
				if (Diagnostic.m_Rule != "structure" && fOwner(Diagnostic.m_iOffset) < 0)
					Diagnostics.f_Insert(Diagnostic);
			}
		}

		CTextLineMap Lines(Source);

		for (umint iVariant = 0; iVariant < nVariants; ++iVariant)
		{
			TCVector<CCodeFormattingRange> Deleted;
			for (auto const &Group : Groups)
			{
				for (auto const &Directive : Group.m_Directives)
					Deleted.f_Insert(Directive);

				auto iKept = fg_Min(iVariant, Group.m_Branches.f_GetLen() - 1);
				for (umint iBranch = 0; iBranch < Group.m_Branches.f_GetLen(); ++iBranch)
				{
					if (iBranch != iKept && Group.m_Branches[iBranch].m_nLength)
						Deleted.f_Insert(Group.m_Branches[iBranch]);
				}
			}

			auto Variant = fg_BuildSourceVariant(Source, fg_Move(Deleted));
			auto VariantRequest = _Request;
			VariantRequest.m_Source = Variant.m_Source;

			// A conversion can take tokens out of one branch and leave their counterparts in the others: the braces dropped
			// around a single statement lose the '{' every head ends with. The variants change whitespace only, which keeps
			// the tokens, and whether they converge is checked on the merged result.
			CFormattingAnalyzer VariantAnalyzer(VariantRequest, false, false);
			auto Result = VariantAnalyzer.f_Analyze(false);
			if (Result.m_Status != ECodeFormattingStatus::mc_Complete)
				return fFallback();

			for (auto const &Diagnostic : Result.m_Diagnostics)
			{
				if (Diagnostic.m_Rule == "structure")
					return fFallback();
			}

			TCVector<umint> Cuts = Variant.m_DeletedAt;
			for (auto const &Directive : Directives)
			{
				if (fOwner(Directive.m_iOffset) >= 0)
					continue;

				bool bDeleted = false;
				for (auto const &Range : Variant.m_Deleted)
					bDeleted = bDeleted || (Directive.m_iOffset >= Range.m_iOffset && Directive.m_iOffset < Range.f_GetEnd());

				if (bDeleted)
					continue;

				Cuts.f_Insert(fg_ToVariant(Variant, Directive.m_iOffset));
				Cuts.f_Insert(fg_ToVariant(Variant, Directive.f_GetEnd()));
			}

			Cuts.f_Sort();

			for (auto const &Edit : Result.m_Edits)
			{
				TCVector<CCodeFormattingEdit> Pieces;
				if (!fg_SplitEdit(Variant.m_Source, Edit, Cuts, Pieces))
					return fFallback();

				for (auto &Piece : Pieces)
				{
					auto iStart = fg_ToOriginal(Variant, Piece.m_iOffset, true);
					auto iEnd = Piece.m_nLength ? fg_ToOriginal(Variant, Piece.f_GetEnd(), false) : iStart;
					if (iEnd < iStart || iEnd - iStart != Piece.m_nLength || fOwner(iStart) != aint(iVariant))
						continue;

					// A gap ending where lines were left out ends the line in front of their directive, which a join in the
					// variant would pull onto that line: 'f(' over '#else'. A blank line taken out leaves the line's own end.
					bool bEndsLine = iEnd > iStart && (pSource[iEnd - 1] == '\n' || pSource[iEnd - 1] == '\r');
					bool bJoins = Piece.m_Replacement.f_Find("\n") < 0 && Piece.m_Replacement.f_Find("\r") < 0;
					if (bJoins && Piece.m_Replacement.f_IsEmpty() && iStart > 0 && (pSource[iStart - 1] == '\n' || pSource[iStart - 1] == '\r'))
						bJoins = false;

					// One that breaks it ends in the indentation of the variant's next line, which would stand in front of the
					// directive instead: 'f' over '(' over '\t#else'.
					auto const &Replacement = Piece.m_Replacement;
					bool bIndents = !bJoins && !Replacement.f_IsEmpty() && Replacement[Replacement.f_GetLen() - 1] != '\n' && Replacement[Replacement.f_GetLen() - 1] != '\r';
					if (bEndsLine && (bJoins || bIndents))
					{
						for (auto const &Range : Variant.m_Deleted)
						{
							if (Range.m_iOffset == iEnd)
								return fFallback();
						}
					}

					Piece.m_iOffset = iStart;
					fKeep(fg_Move(Piece));
				}
			}

			for (auto Diagnostic : Result.m_Diagnostics)
			{
				auto iOffset = fg_ToOriginal(Variant, Diagnostic.m_iOffset, true);
				if (fOwner(iOffset) != aint(iVariant))
					continue;

				auto iLine = Lines.f_FindLine(iOffset);
				umint nColumns = 0;
				fg_MeasureTextColumns(pSource + Lines.f_GetLineStart(iLine), iOffset - Lines.f_GetLineStart(iLine), _Request.m_Settings.m_nTabWidth, nColumns);

				Diagnostic.m_iOffset = iOffset;
				Diagnostic.m_iLine = iLine + 1;
				Diagnostic.m_iColumn = nColumns + 1;
				Diagnostics.f_Insert(fg_Move(Diagnostic));
			}
		}

		Edits.f_Sort
			(
				[](CCodeFormattingEdit const &_Left, CCodeFormattingEdit const &_Right)
				{
					return _Left.m_iOffset <=> _Right.m_iOffset;
				}
			)
		;

		for (umint i = 1; i < Edits.f_GetLen(); ++i)
		{
			auto const &Previous = Edits[i - 1];
			auto const &Edit = Edits[i];
			if (Previous.f_GetEnd() > Edit.m_iOffset || (!Edit.m_nLength && Previous.m_iOffset == Edit.m_iOffset))
				return fFallback();
		}

		Diagnostics.f_Sort
			(
				[](CCodeFormattingDiagnostic const &_Left, CCodeFormattingDiagnostic const &_Right)
				{
					if (_Left.m_iOffset != _Right.m_iOffset)
						return _Left.m_iOffset <=> _Right.m_iOffset;

					return _Left.m_Rule <=> _Right.m_Rule;
				}
			)
		;

		if (_bVerify)
		{
			auto CheckRequest = _Request;
			CheckRequest.m_Source = fg_ApplyCodeFormattingEdits(Source, Edits);
			auto Check = fg_AnalyzeCodeFormattingVerified(CheckRequest, false);
			if (Check.m_Status != ECodeFormattingStatus::mc_Complete || !Check.m_Edits.f_IsEmpty())
				return fFallback();
		}

		CCodeFormattingResult Result;
		Result.m_Status = ECodeFormattingStatus::mc_Complete;
		Result.m_Edits = fg_Move(Edits);
		Result.m_Diagnostics = fg_Move(Diagnostics);
		Result.m_EffectiveRanges = fg_Move(_Plain.m_EffectiveRanges);

		return Result;
	}

	CCodeFormattingResult fg_AnalyzeCodeFormattingVerified(CCodeFormattingRequest const &_Request, bool _bVerifyAlternatives)
	{
		CFormattingAnalyzer Analyzer(_Request);

		return fg_AnalyzeAlternatives(_Request, Analyzer, Analyzer.f_Analyze(true), _bVerifyAlternatives);
	}
}

namespace NMib::NDevelop
{
	using namespace NPrivate;

	CCodeFormattingSettings::CCodeFormattingSettings(CEditorConfigProperties const &_Properties)
	{
		if (auto pValue = fg_FindProperty(_Properties, "malterlib_format"))
		{
			auto Value = pValue->f_LowerCase();
			if (Value == "malterlib")
				m_Profile = ECodeFormattingProfile::mc_Malterlib;
			else if (Value == "malterlib-c")
				m_Profile = ECodeFormattingProfile::mc_MalterlibC;
			else if (Value == "malterlib-buildsystem")
				m_Profile = ECodeFormattingProfile::mc_MalterlibBuildSystem;
			else if (Value != "off")
				DMibError("Invalid malterlib_format: '{}' (expected malterlib, malterlib-c, malterlib-buildsystem, off, or unset)"_f << *pValue);
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

		bool bIndentSize = false;
		if (auto pValue = fg_FindProperty(_Properties, "indent_size"))
		{
			if (*pValue != "tab")
			{
				m_nIndentSize = fg_ParsePositiveProperty(*pValue, "indent_size");
				bIndentSize = true;
			}
		}

		// 'tab' stands for a width only in indent_size, which tab_width falls back to.
		if (auto pWidth = fg_FindProperty(_Properties, "tab_width"))
			m_nTabWidth = fg_ParsePositiveProperty(*pWidth, "tab_width");
		else if (auto pIndent = fg_FindProperty(_Properties, "indent_size"); pIndent && *pIndent != "tab")
			m_nTabWidth = fg_ParsePositiveProperty(*pIndent, "tab_width");

		// 'indent_size = tab', or none, indents a level by a tab's width.
		if (!bIndentSize)
			m_nIndentSize = m_nTabWidth;

		if (m_Profile != ECodeFormattingProfile::mc_Disabled)
			m_nMaxColumns = 190;

		if (auto pValue = fg_FindProperty(_Properties, "max_line_length"))
		{
			m_bHasExplicitMaxColumns = true;
			m_nMaxColumns = pValue->f_LowerCase() == "off" ? 0 : fg_ParsePositiveProperty(*pValue, "max_line_length");
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
		case ECodeFormattingProfile::mc_MalterlibC: return ECodeLanguage::mc_C;
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
		{
			auto pSource = _Source.f_GetStr();
			CStr::CAppender Appender(Result);
			umint iCopied = 0;

			for (auto const &Edit : _Edits)
			{
				umint nSource = _Source.f_GetLen();
				if (Edit.m_iOffset < iCopied || Edit.m_iOffset > nSource || Edit.m_nLength > nSource - Edit.m_iOffset)
					DMibError("Formatting edits must be ordered, non-overlapping, and inside the source");

				Appender.f_AddString(pSource + iCopied, Edit.m_iOffset - iCopied);
				Appender += Edit.m_Replacement;
				iCopied = Edit.f_GetEnd();
			}

			Appender.f_AddString(pSource + iCopied, _Source.f_GetLen() - iCopied);
		}

		return Result;
	}

	static CStr fg_NormalizeDirectiveLines(ch8 const *_pText, umint _nLength);

	// Normalizes a source into comparable token spellings. A '>>' closing two template argument lists is split as the
	// structure splits it, so it and '> >' normalize alike, while a shift stays one token.
	static TCVector<CStr> fg_NormalizeCodeTokens(CStr const &_Source, ECodeLanguage _Language)
	{
		CCodeTokenStream Stream(_Source, nullptr, _Language);
		bool bCpp = _Language != ECodeLanguage::mc_BuildSystem;
		if (bCpp)
			CCodeStructure::fs_SplitSharedAngleClosers(Stream);

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

			if (Kind == ECodeTokenKind::mc_Preprocessor && Text.f_StartsWith("#"))
			{
				Text = "#" + Text.f_Extract(1).f_TrimLeft();
				Text = fg_NormalizeDirectiveLines(Text.f_GetStr(), Text.f_GetLen());
			}

			// The indentation of a block comment's lines behind its first moves with the comment.
			if (Kind == ECodeTokenKind::mc_BlockComment && Token.m_bMultiLine)
				Text = fg_NormalizeDirectiveLines(Text.f_GetStr(), Text.f_GetLen());

			auto fInsert = [&](CStr const &_Text)
				{
					Texts.f_Insert("{}:{}"_f << umint(Kind) << _Text);
				}
			;

			// A comma in front of a closing bracket separates nothing in the build system's syntax.
			if (!bCpp && Kind == ECodeTokenKind::mc_Punctuator && (Text == "]" || Text == "}" || Text == ")"))
			{
				if (!Texts.f_IsEmpty() && Texts.f_GetLast() == ("{}:,"_f << umint(ECodeTokenKind::mc_Punctuator)).f_GetStr())
					Texts.f_Remove(Texts.f_GetLen() - 1);
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
		CStr m_Normalized; // A directive continued over lines, without the indentation of its lines.
	};

	static CStr fg_NormalizeDirectiveLines(ch8 const *_pText, umint _nLength)
	{
		CStr Result;
		umint iLine = 0;

		// The blank starting the line behind '#define NAME\' decides whether the macro takes parameters: '(x) x' is a
		// parameter list, ' (x) x' a replacement. That line keeps whether it has one.
		// The text can start behind the directive's '#', which the keyword is read from.
		bool bNameAtSplice = false;
		{
			umint nFirstLine = 0;
			while (nFirstLine < _nLength && _pText[nFirstLine] != '\n' && _pText[nFirstLine] != '\r')
				++nFirstLine;

			CStr FirstLine = _nLength && _pText[0] == '#' ? CStr(_pText, nFirstLine) : "#" + CStr(_pText, nFirstLine);
			auto pLine = FirstLine.f_GetStr();
			umint nLine = FirstLine.f_GetLen();

			ch8 Keyword[16];
			umint iName = 0;
			auto nKeyword = fg_ReadDirectiveKeyword(pLine, nLine, Keyword, iName);
			if (nKeyword == 6 && NMemory::fg_MemCmp((uint8 const *)Keyword, (uint8 const *)"define", 6) == 0)
			{
				while (iName < nLine && fg_IsSpaceOrTab(pLine[iName]))
					++iName;

				auto iNameEnd = iName;
				while (iNameEnd < nLine && (fg_CharIsAnsiAlphabetical(pLine[iNameEnd]) || fg_CharIsNumber(pLine[iNameEnd]) || pLine[iNameEnd] == '_'))
					++iNameEnd;

				bNameAtSplice = iNameEnd > iName && iNameEnd < nLine && pLine[iNameEnd] == '\\';
			}
		}

		// Lines end in LF, CRLF or a lone CR alike.
		while (iLine <= _nLength)
		{
			auto iNewline = iLine;
			while (iNewline < _nLength && _pText[iNewline] != '\n' && _pText[iNewline] != '\r')
				++iNewline;

			umint nTerminator = 0;
			if (iNewline < _nLength)
				nTerminator = _pText[iNewline] == '\r' && iNewline + 1 < _nLength && _pText[iNewline + 1] == '\n' ? 2 : 1;

			auto iContent = iLine;
			if (iLine)
			{
				while (iContent < iNewline && fg_IsSpaceOrTab(_pText[iContent]))
					++iContent;

				if (bNameAtSplice && iContent > iLine)
					Result += " ";

				bNameAtSplice = false;
			}

			auto iEnd = iNewline;
			bool bSplice = iNewline < _nLength && iEnd > iContent && _pText[iEnd - 1] == '\\';
			auto iBlanks = bSplice ? iEnd - 1 : iEnd;
			while (bSplice && iBlanks > iContent && fg_IsSpaceOrTab(_pText[iBlanks - 1]))
				--iBlanks;

			Result += CStr(_pText + iContent, iBlanks - iContent);
			if (bSplice)
				Result += CStr(_pText + iEnd - 1, iNewline + nTerminator - (iEnd - 1));
			else if (iNewline < _nLength)
				Result += CStr(_pText + iBlanks, iNewline + nTerminator - iBlanks);
			else
				Result += CStr(_pText + iBlanks, _nLength - iBlanks);

			iLine = iNewline + nTerminator;
			if (!nTerminator)
				break;
		}

		return Result;
	}

	// A splice is layout in C++, but in the build system's syntax it is what continues a value.
	static void fg_CollectCodeTokenViews(CCodeTokenStream const &_Stream, ECodeLanguage _Language, TCVector<CCodeTokenView> &o_Views)
	{
		bool bCpp = _Language != ECodeLanguage::mc_BuildSystem;
		CCodeToken const *pPrevious = nullptr;
		for (auto const &Token : _Stream.f_GetTokens())
		{
			auto Kind = Token.m_Kind;
			auto const *pBefore = pPrevious;
			pPrevious = &Token;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || (bCpp && Kind == ECodeTokenKind::mc_LineSplice))
				continue;

			// A user-defined literal's suffix is part of the literal, which the lexer reads apart: '"a"_s' is no '"a" _s'.
			bool bSuffix
				= bCpp
				&& Kind == ECodeTokenKind::mc_Identifier
				&& pBefore
				&& (pBefore->m_Kind == ECodeTokenKind::mc_StringLiteral || pBefore->m_Kind == ECodeTokenKind::mc_CharLiteral || pBefore->m_Kind == ECodeTokenKind::mc_Number)
				&& pBefore->f_GetEnd() == Token.m_iOffset
			;
			if (bSuffix)
				o_Views.f_Insert(CCodeTokenView{ECodeTokenKind::mc_Punctuator, nullptr, 0, CStr("literal suffix")});

			auto pText = _Stream.f_GetTextPointer(Token);
			auto nLength = Token.m_nLength;
			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_Preprocessor)
			{
				while (nLength && fg_IsSpaceOrTab(pText[nLength - 1]))
					--nLength;
			}

			// The blanks between a directive's '#' and its name are its indentation.
			if (Kind == ECodeTokenKind::mc_Preprocessor && nLength && pText[0] == '#')
			{
				umint nSkip = 1;
				while (nSkip < nLength && fg_IsSpaceOrTab(pText[nSkip]))
					++nSkip;

				pText += nSkip;
				nLength -= nSkip;

				// A literal's lines are its text, which no layout of the directive's lines may change.
				if (Token.m_bMultiLine && !fg_HoldsRawStringLines(pText, nLength) && !fg_HoldsSplicedLiteral(pText, nLength))
				{
					CCodeTokenView View{Kind, nullptr, 0, fg_NormalizeDirectiveLines(pText, nLength)};
					o_Views.f_Insert(fg_Move(View));

					continue;
				}
			}

			// The indentation of a block comment's lines behind its first moves with the comment.
			if (Kind == ECodeTokenKind::mc_BlockComment && Token.m_bMultiLine)
			{
				CCodeTokenView View{Kind, nullptr, 0, fg_NormalizeDirectiveLines(pText, nLength)};
				o_Views.f_Insert(fg_Move(View));

				continue;
			}

			// A comma in front of a closing bracket separates nothing in the build system's syntax.
			if (!bCpp && Kind == ECodeTokenKind::mc_Punctuator && nLength == 1)
			{
				auto Char = pText[0];
				bool bClosing = Char == ']' || Char == '}' || Char == ')';
				auto const *pLast = o_Views.f_IsEmpty() ? nullptr : &o_Views.f_GetLast();
				if (bClosing && pLast && pLast->m_Kind == ECodeTokenKind::mc_Punctuator && pLast->m_nLength == 1 && pLast->m_pText && pLast->m_pText[0] == ',')
					o_Views.f_Remove(o_Views.f_GetLen() - 1);
			}

			o_Views.f_Insert(CCodeTokenView{Kind, pText, nLength});
		}
	}

	bool fg_HasEquivalentCodeTokens(CStr const &_First, CStr const &_Second, ECodeLanguage _Language)
	{
		CCodeTokenStream FirstStream(_First, nullptr, _Language);
		CCodeTokenStream SecondStream(_Second, nullptr, _Language);

		// A '>>' closing two template argument lists is the same two tokens as '> >', which the layout may write either way;
		// any other '>>' is a shift, whose halves never stand apart.
		if (_Language != ECodeLanguage::mc_BuildSystem)
		{
			CCodeStructure::fs_SplitSharedAngleClosers(FirstStream);
			CCodeStructure::fs_SplitSharedAngleClosers(SecondStream);
		}
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
			auto pLeft = Left.m_pText ? Left.m_pText : Left.m_Normalized.f_GetStr();
			auto pRight = Right.m_pText ? Right.m_pText : Right.m_Normalized.f_GetStr();
			auto nLeft = Left.m_pText ? Left.m_nLength : Left.m_Normalized.f_GetLen();
			auto nRight = Right.m_pText ? Right.m_nLength : Right.m_Normalized.f_GetLen();
			if (Left.m_Kind != Right.m_Kind || nLeft != nRight)
				return false;

			for (umint j = 0; j < nLeft; ++j)
			{
				if (pLeft[j] != pRight[j])
					return false;
			}
		}

		return true;
	}

	CCodeFormattingResult fg_AnalyzeCodeFormatting(CCodeFormattingRequest const &_Request)
	{
		return fg_AnalyzeCodeFormattingVerified(_Request, true);
	}
}

namespace NMib::NDevelop::NPrivate
{
	CStr const gc_FormatOffDirective = "// malterlib-format off";
	CStr const gc_FormatOnDirective = "// malterlib-format on";

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

	// Overlong encodings, surrogates and values past U+10FFFF are invalid as well.
	bool fg_IsValidUtf8(CStr const &_Text)
	{
		return NStr::fg_IsValidUTF8(_Text.f_GetStr(), _Text.f_GetLen());
	}

	// A UTF-8 continuation byte can never start a code point, so a range boundary there
	// would split an encoded character.
	bool fg_IsCodePointBoundary(CStr const &_Text, umint _iOffset)
	{
		if (_iOffset >= _Text.f_GetLen())
			return true;

		return (uch8(_Text.f_GetStr()[_iOffset]) & 0xC0) != 0x80;
	}

	// Objective-C's visibility keywords open a section of a class's instance variables as an access specifier does.
	bool fg_IsObjectiveCVisibility(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		return _Tokens.f_IsText(_Token, "@public") || _Tokens.f_IsText(_Token, "@private") || _Tokens.f_IsText(_Token, "@protected") || _Tokens.f_IsText(_Token, "@package");
	}

	// Whether the text is the bytes given, compared in place rather than through a string made of them.
	bool fg_IsSameText(CStr const &_Text, ch8 const *_pOther, umint _nOther)
	{
		return _Text.f_GetLen() == _nOther && NMemory::fg_MemCmp((uint8 const *)_Text.f_GetStr(), (uint8 const *)_pOther, _nOther) == 0;
	}

	CStr fg_TrimCommentTrailingSpace(CStr const &_Text)
	{
		auto pText = _Text.f_GetStr();
		auto nLength = _Text.f_GetLen();
		while (nLength && fg_IsSpaceOrTab(pText[nLength - 1]))
			--nLength;

		return _Text.f_Left(nLength);
	}

	// The blanks behind the '//' of a line comment.
	umint fg_CommentBlanks(CStr const &_Text)
	{
		auto pText = _Text.f_GetStr();
		umint nBlanks = 0;
		while (2 + nBlanks < _Text.f_GetLen() && fg_IsSpaceOrTab(pText[2 + nBlanks]))
			++nBlanks;

		return nBlanks;
	}

	// The blanks every line with text of a run of line comments starts with behind its '//'. False for a run without text
	// and for a documenting one, '///' or '//!', whose marker is the text's own.
	bool fg_SharedCommentBlanks(TCVector<CStr> const &_Texts, umint &o_nShared)
	{
		ch8 const *pShared = nullptr;
		o_nShared = 0;

		for (auto const &Text : _Texts)
		{
			auto pText = Text.f_GetStr();
			if (Text.f_GetLen() > 2)
			{
				auto Marker = pText[2];
				if (Marker == '/' || Marker == '!')
					return false;
			}

			auto nBlanks = fg_CommentBlanks(Text);
			if (2 + nBlanks >= Text.f_GetLen())
				continue;

			if (!pShared)
			{
				o_nShared = nBlanks;
				pShared = pText;

				continue;
			}

			umint nCommon = 0;
			while (nCommon < o_nShared && nCommon < nBlanks && pShared[2 + nCommon] == pText[2 + nCommon])
				++nCommon;

			o_nShared = nCommon;
		}

		return pShared;
	}

	// Whether the run's shared blanks are written as one space. What the lines keep behind them places their text relative
	// to each other only at the columns the source has them where it starts with a tab on one line and a space on another,
	// so those keep their blanks.
	bool fg_ConvertsCommentBlanks(TCVector<CStr> const &_Texts, umint _nShared)
	{
		if (!_nShared)
			return false;

		bool bFirst = true;
		bool bOneSpace = false;
		bool bTabsKept = false;
		bool bSpacesKept = false;

		for (auto const &Text : _Texts)
		{
			auto nBlanks = fg_CommentBlanks(Text);
			if (2 + nBlanks >= Text.f_GetLen())
				continue;

			auto pText = Text.f_GetStr();
			if (bFirst)
			{
				bOneSpace = _nShared == 1 && pText[2] == ' ';
				bFirst = false;
			}

			if (_nShared < nBlanks)
			{
				bTabsKept = bTabsKept || pText[2 + _nShared] == '\t';
				bSpacesKept = bSpacesKept || pText[2 + _nShared] == ' ';
			}
		}

		return !bOneSpace && !(bTabsKept && bSpacesKept);
	}

	// GCC's -Wimplicit-fallthrough reads a comment as the annotation only behind the statement that falls through, so it
	// never moves above it: '/* falls through */', '// FALLTHRU', '// fall-through'.
	bool fg_IsFallthroughComment(CStr const &_Text)
	{
		auto Text = _Text.f_LowerCase();
		for (aint iFall = Text.f_Find("fall"); iFall >= 0; iFall = Text.f_Find(iFall + 1, "fall"))
		{
			auto iThrough = umint(iFall) + 4;
			if (iThrough < Text.f_GetLen() && Text[iThrough] == 's')
				++iThrough;

			if (iThrough < Text.f_GetLen() && (Text[iThrough] == ' ' || Text[iThrough] == '-'))
				++iThrough;

			if (Text.f_Extract(iThrough, 3) == "thr")
				return true;
		}

		return false;
	}

	// The line comment a block comment on one line is written as behind code, or an empty string where it keeps its
	// spelling: one with no text, one ending in a line splice or its trigraph, which a line comment would continue onto the next line,
	// a 'malterlib-format' marker, and a clang-tidy NOLINT comment, which suppresses diagnostics on its own line only.
	// A documenting marker is kept: '/*!< A */' becomes '//!< A' and '/** A */' '/// A'; one opening with more stars stays.
	CStr fg_TrailingLineComment(CStr const &_Text)
	{
		if (_Text.f_Find("malterlib-format") >= 0 || _Text.f_Find("NOLINT") >= 0)
			return {};

		CStr Marker = "//";
		umint nOpen = 2;
		if (_Text.f_StartsWith("/**<") || _Text.f_StartsWith("/*!<"))
		{
			Marker = _Text.f_StartsWith("/**<") ? "///<" : "//!<";
			nOpen = 4;
		}
		else if (_Text.f_StartsWith("/***"))
			return {};
		else if ((_Text.f_StartsWith("/**") || _Text.f_StartsWith("/*!")) && !_Text.f_StartsWith("/**/"))
		{
			Marker = _Text.f_StartsWith("/**") ? "///" : "//!";
			nOpen = 3;
		}

		if (_Text.f_GetLen() < nOpen + 2)
			return {};

		auto Inner = _Text.f_Extract(nOpen, _Text.f_GetLen() - nOpen - 2).f_Trim();
		if (Inner.f_IsEmpty() || Inner.f_EndsWith("\\") || Inner.f_EndsWith("?" "?/"))
			return {};

		return Marker + " " + Inner;
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
				"const"
				, "volatile"
				, "static"
				, "constexpr"
			}
		;

		// A run of statement terminators reads as one, since the stage takes out the ones
		// that end nothing: 'f_Call();;' reads as 'f_Call();'. A comma in front of a closing
		// brace goes the same way: 'EA, }' reads as 'EA }'. So does one beside a directive,
		// which the stage moves across it into every branch.
		CCodeTokenStream Tokens(_Source);
		auto pSource = _Source.f_GetStr();
		CStr Text;
		{
			CStr::CAppender Appender(Text);
			umint MovedCounts[4] = {};
			bool bTerminated = false;
			bool bComma = false;
			bool bAfterDirective = false;

			for (auto const &Token : Tokens.f_GetTokens())
			{
				if (Token.m_Kind == ECodeTokenKind::mc_Preprocessor)
				{
					bComma = false;
					bAfterDirective = true;

					continue;
				}

				if (!fg_IsCodeToken(Token))
					continue;

				if (bComma && !Tokens.f_IsText(Token, "}"))
					Appender += ",";

				bComma = Tokens.f_IsText(Token, ",") && !bAfterDirective;
				bAfterDirective = false;

				if (Tokens.f_IsText(Token, ","))
				{
					bTerminated = false;

					continue;
				}

				bool bMoved = false;
				for (umint i = 0; i < 4 && !bMoved; ++i)
				{
					bMoved = Tokens.f_IsText(Token, c_pMoved[i]);
					MovedCounts[i] += bMoved;
				}

				if (bMoved)
					continue;

				bool bTerminator = Tokens.f_IsText(Token, ";");
				if (!bTerminator || !bTerminated)
					Appender.f_AddString(pSource + Token.m_iOffset, Token.m_nLength);

				bTerminated = bTerminator;
			}

			if (bComma)
				Appender += ",";

			for (auto nWords : MovedCounts)
			{
				Appender += " ";
				Appender += CStr::fs_ToStr(nWords);
			}
		}

		return Text;
	}

	// An offset in text an edit wrote maps to the start of what the edit replaced, or with _bEnd, to its end, so a span
	// over written text covers what that text replaced.
	umint fg_MapOffsetToOriginal(COffsetToOriginal &_Mapper, umint _iOffset, bool _bEnd)
	{
		if (_iOffset < _Mapper.m_iPrevious)
		{
			_Mapper.m_iEdit = 0;
			_Mapper.m_nDelta = 0;
		}

		_Mapper.m_iPrevious = _iOffset;

		for (; _Mapper.m_iEdit < _Mapper.m_Edits.f_GetLen(); ++_Mapper.m_iEdit)
		{
			auto const &Edit = _Mapper.m_Edits[_Mapper.m_iEdit];
			auto iStart = umint(aint(Edit.m_iOffset) + _Mapper.m_nDelta);
			if (_iOffset < iStart || (_bEnd && _iOffset == iStart))
				break;

			if (_iOffset < iStart + Edit.m_Replacement.f_GetLen())
				return _bEnd ? Edit.m_iOffset + Edit.m_nLength : Edit.m_iOffset;

			if (_bEnd && _iOffset == iStart + Edit.m_Replacement.f_GetLen())
				return Edit.m_iOffset + Edit.m_nLength;

			_Mapper.m_nDelta += aint(Edit.m_Replacement.f_GetLen()) - aint(Edit.m_nLength);
		}

		return umint(aint(_iOffset) - _Mapper.m_nDelta);
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

				auto const *pEdit = iEdit < _Edits.f_GetLen() ? &_Edits[iEdit] : nullptr;
				if (!pEdit || !fTouches(pEdit->m_iOffset, pEdit->f_GetEnd(), iStart, iEnd))
					break;

				iEnd = fg_Max(iEnd, pEdit->f_GetEnd());
				++iEdit;
			}

			// An end inside what the last conversion wrote is that conversion's end in the
			// original; one behind it stands in text the original has too.
			umint iSourceEnd = nConversions && iEnd <= iConvertedEnd ? iConvertedSourceEnd : umint(aint(iEnd) - nDelta);

			// Edits that only touch each other, as insertions at one offset do, are each shifted back on their own.
			if (!nConversions)
			{
				for (umint i = iFirstEdit; i < iEdit; ++i)
				{
					auto Edit = _Edits[i];
					Edit.m_iOffset = umint(aint(Edit.m_iOffset) - nDelta);
					o_Edits.f_Insert(Edit);
				}

				continue;
			}

			if (iEnd > _Converted.f_GetLen() || iSourceEnd < iSourceStart)
			{
				o_Explanation = "{} could not be expressed as an edit of the original source"_f << Rule;

				return false;
			}

			CStr Text;
			{
				auto pConverted = _Converted.f_GetStr();
				CStr::CAppender Appender(Text);
				umint iCopied = iStart;

				for (umint i = iFirstEdit; i < iEdit; ++i)
				{
					auto const &Edit = _Edits[i];
					Appender.f_AddString(pConverted + iCopied, Edit.m_iOffset - iCopied);
					Appender += Edit.m_Replacement;
					iCopied = Edit.f_GetEnd();
				}

				Appender.f_AddString(pConverted + iCopied, iEnd - iCopied);
			}

			auto &Merged = o_Edits.f_Insert();
			Merged.m_iOffset = iSourceStart;
			Merged.m_nLength = iSourceEnd - iSourceStart;
			Merged.m_Replacement = Text;
			Merged.m_Rule = Rule;
		}

		return true;
	}

	// The indentation of a directive's continuation lines and the blanks in front of its splices are layout.
	// Whether a directive's text holds a raw string literal spanning lines.
	bool fg_HoldsRawStringLines(ch8 const *_pText, umint _nLength)
	{
		for (umint i = 0; i + 1 < _nLength; ++i)
		{
			if (_pText[i] != 'R' || _pText[i + 1] != '"')
				continue;

			auto iWord = i;

			auto fWordChar = [](ch8 _Char)
				{
					return fg_CharIsAnsiAlphabetical(_Char) || fg_CharIsNumber(_Char) || _Char == '_';
				}
			;

			while (iWord && fWordChar(_pText[iWord - 1]))
				--iWord;

			CStr Prefix(_pText + iWord, i + 1 - iWord);
			if (Prefix != "R" && Prefix != "u8R" && Prefix != "LR" && Prefix != "uR" && Prefix != "UR")
				continue;

			auto iOpen = i + 2;
			while (iOpen < _nLength)
			{
				auto Char = _pText[iOpen];
				if (Char == '(' || Char == '\n' || Char == '\r')
					break;

				++iOpen;
			}

			if (iOpen >= _nLength || _pText[iOpen] != '(')
				continue;

			CStr Close = ")" + CStr(_pText + i + 2, iOpen - i - 2) + "\"";
			for (auto iEnd = iOpen + 1; iEnd < _nLength; ++iEnd)
			{
				if (_pText[iEnd] == '\n' || _pText[iEnd] == '\r')
					return true;

				if (iEnd + Close.f_GetLen() <= _nLength && CStr(_pText + iEnd, Close.f_GetLen()) == Close)
				{
					i = iEnd;

					break;
				}
			}
		}

		return false;
	}

	// Whether a directive's text continues a string or character literal onto its next line, whose blanks are then the
	// literal's. A digit separator read as a quote only keeps lines that could have moved.
	bool fg_HoldsSplicedLiteral(ch8 const *_pText, umint _nLength)
	{
		ch8 Quote = 0;
		for (umint i = 0; i < _nLength; ++i)
		{
			auto Char = _pText[i];
			if (Quote)
			{
				if (Char == '\\' && i + 1 < _nLength)
				{
					// A splice may have blanks between its backslash and the line's end, as the lexer reads it. Splices are taken
					// out before escapes are read, so the backslash an escape would take can splice as well: '\\\\' over a line end.
					auto fSplices = [&](umint _iBackslash)
						{
							auto iAfter = _iBackslash + 1;
							while (iAfter < _nLength && (_pText[iAfter] == ' ' || _pText[iAfter] == '\t' || _pText[iAfter] == '\v' || _pText[iAfter] == '\f'))
								++iAfter;

							return iAfter < _nLength && (_pText[iAfter] == '\n' || _pText[iAfter] == '\r');
						}
					;

					if (fSplices(i) || (_pText[i + 1] == '\\' && fSplices(i + 1)))
						return true;

					++i;
				}
				else if (Char == Quote || Char == '\n')
					Quote = 0;

				continue;
			}

			if (Char == '/' && i + 1 < _nLength && _pText[i + 1] == '/')
				return false;

			if (Char == '/' && i + 1 < _nLength && _pText[i + 1] == '*')
			{
				i += 2;
				while (i + 1 < _nLength && (_pText[i] != '*' || _pText[i + 1] != '/'))
					++i;

				++i;
			}
			else if (Char == '"' || Char == '\'')
				Quote = Char;
		}

		return false;
	}

	// Whether a splice in a directive's text joins what ends one line to what starts the next, with no blank on either
	// side: '#define DAdd\' over '(x) x'. Blanks added at the splice would part them.
	bool fg_JoinsAcrossSplice(ch8 const *_pText, umint _nLength)
	{
		for (umint i = 1; i + 1 < _nLength; ++i)
		{
			if (_pText[i] != '\\')
				continue;

			auto iNext = i + 1;
			while (iNext + 1 < _nLength && (_pText[iNext] == ' ' || _pText[iNext] == '\t' || _pText[iNext] == '\v' || _pText[iNext] == '\f'))
				++iNext;

			if (_pText[iNext] == '\r' && iNext + 1 < _nLength && _pText[iNext + 1] == '\n')
				++iNext;

			if (_pText[iNext] != '\n' && _pText[iNext] != '\r')
				continue;

			auto Before = _pText[i - 1];
			auto After = iNext + 1 < _nLength ? _pText[iNext + 1] : ch8(' ');
			bool bBlankBefore = Before == ' ' || Before == '\t';
			bool bBlankAfter = After == ' ' || After == '\t' || After == '\n' || After == '\r' || After == '\\';
			if (!bBlankBefore && !bBlankAfter)
				return true;
		}

		return false;
	}

	// True when the two sources hold the same directives in the same order, whatever their indentation.
	bool fg_HasEquivalentDirectives(CStr const &_First, CStr const &_Second, ECodeLanguage _Language)
	{
		CCodeTokenStream FirstStream(_First, nullptr, _Language);
		CCodeTokenStream SecondStream(_Second, nullptr, _Language);
		TCVector<CCodeTokenView> First;
		TCVector<CCodeTokenView> Second;

		fg_CollectCodeTokenViews(FirstStream, _Language, First);
		fg_CollectCodeTokenViews(SecondStream, _Language, Second);

		umint iSecond = 0;
		for (auto const &Left : First)
		{
			if (Left.m_Kind != ECodeTokenKind::mc_Preprocessor)
				continue;

			while (iSecond < Second.f_GetLen() && Second[iSecond].m_Kind != ECodeTokenKind::mc_Preprocessor)
				++iSecond;

			if (iSecond == Second.f_GetLen())
				return false;

			auto const &Right = Second[iSecond++];
			auto LeftText = Left.m_pText ? CStr(Left.m_pText, Left.m_nLength) : Left.m_Normalized;
			auto RightText = Right.m_pText ? CStr(Right.m_pText, Right.m_nLength) : Right.m_Normalized;
			if (LeftText != RightText)
				return false;
		}

		while (iSecond < Second.f_GetLen() && Second[iSecond].m_Kind != ECodeTokenKind::mc_Preprocessor)
			++iSecond;

		return iSecond == Second.f_GetLen();
	}
}
