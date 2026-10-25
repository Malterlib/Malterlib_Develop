// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NDevelop::NPrivate;

	// Whether the token is one of the operators 'operator-space' writes apart from both
	// operands. Plain assignment is deliberately absent: a lone '=' is also a lambda
	// capture default and the trailing token of Malterlib's '_o=' and '_j=' DSL spellings.
	bool fg_IsSpacedOperator(CCodeTokenStream const &_Tokens, CCodeToken const &_Token)
	{
		if (_Token.m_Kind != ECodeTokenKind::mc_Punctuator || _Token.m_nLength < 2)
			return false;

		constexpr ch8 const *c_pSpacedOperators[] =
			{
				"=="
				, "!="
				, "<="
				, ">="
				, "<=>"
				, "||"
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

		for (auto pOperator : c_pSpacedOperators)
		{
			if (_Tokens.f_IsText(_Token, pOperator))
				return true;
		}

		return false;
	}
}

namespace NMib::NDevelop::NPrivate
{
	void CFormattingAnalyzer::fp_RuleIndentation()
	{
		auto const &Settings = m_Request.m_Settings;
		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			if (m_ProtectedStart[iLine] || !fp_IsLineSelected(iLine))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
			auto iIndent = iStart;
			while (iIndent < iEnd && fg_IsSpaceOrTab(m_pSource[iIndent]))
				++iIndent;

			// An all-whitespace line is normalized by the trailing whitespace rule.
			if (iIndent == iEnd)
				continue;

			// A line the layout placed takes its indentation from that decision, already
			// spelled the way this rule would spell it, and a directive's is its own rule's.
			// So does one the code behind its comments starts. A line behind a line splice is no line the layout writes.
			auto const &Tokens = m_Tokens.f_GetTokens();
			auto iToken = m_Tokens.f_FindToken(iIndent);
			auto iBreak = iToken;
			while (iBreak && Tokens[iBreak - 1].m_Kind == ECodeTokenKind::mc_Whitespace)
				--iBreak;

			bool bSpliced = iBreak && Tokens[iBreak - 1].m_Kind == ECodeTokenKind::mc_LineSplice;
			if ((fp_IsBreakGap(iToken) && !bSpliced) || Tokens[iToken].m_Kind == ECodeTokenKind::mc_Preprocessor)
				continue;

			// A comment the layout moves is written where it moves to.
			if (m_CommentMoved[iToken] && fp_CanShiftCommentLines(iToken, aint(m_CommentIndent[iToken]) - aint(fp_GetSourceLineIndent(iToken))))
				continue;

			auto fIsBeforeCode = [&](umint _iToken)
				{
					auto const &Token = Tokens[_iToken];

					return Token.m_Kind == ECodeTokenKind::mc_Whitespace || (Token.m_Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine);
				}
			;

			auto iCode = iToken;
			while (iCode < Tokens.f_GetLen() && fIsBeforeCode(iCode))
				++iCode;

			if (iCode != iToken && iCode < Tokens.f_GetLen() && fg_IsCodeToken(Tokens[iCode]) && fp_IsBreakGap(iCode))
				continue;

			umint nColumns = 0;
			if (!fg_MeasureTextColumns(m_pSource + iStart, iIndent - iStart, Settings.m_nTabWidth, nColumns))
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

			if (fg_IsSameText(Canonical, m_pSource + iStart, iIndent - iStart))
				continue;

			fp_AddEdit("indentation", iStart, iIndent - iStart, Canonical, Settings.m_bIndentWithTabs ? "indentation must use tabs" : "indentation must use spaces");
		}

		// A block comment's lines behind its first keep their columns, which are their text's, spelled the way indentation
		// is. One a move writes is written where it moves to.
		auto const &Tokens = m_Tokens.f_GetTokens();
		NContainer::TCVector<umint> EditStarts;
		bool bEditStarts = false;
		for (auto const &Token : Tokens)
		{
			if (Token.m_Kind != ECodeTokenKind::mc_BlockComment || !Token.m_bMultiLine)
				continue;

			if (!bEditStarts)
			{
				for (auto const &Edit : m_Edits)
					EditStarts.f_Insert(Edit.m_iOffset);

				EditStarts.f_Sort();
				bEditStarts = true;
			}

			for (auto iLine = m_Lines.f_FindLine(Token.m_iOffset) + 1; iLine < m_Lines.f_GetLineCount() && m_Lines.f_GetLineStart(iLine) < Token.f_GetEnd(); ++iLine)
			{
				auto iStart = m_Lines.f_GetLineStart(iLine);
				auto iContent = iStart;
				while (iContent < Token.f_GetEnd() && fg_IsSpaceOrTab(m_pSource[iContent]))
					++iContent;

				auto const *pContent = iContent < Token.f_GetEnd() ? m_pSource + iContent : nullptr;
				bool bBlank = !pContent || *pContent == '\n' || *pContent == '\r';
				if (bBlank || iContent == iStart || !fp_IsLineSelected(iLine) || fp_IsDisabled(iStart, iContent - iStart))
					continue;

				if (EditStarts.f_BinarySearch(iStart) >= 0)
					continue;

				umint nColumns = 0;
				if (!fg_MeasureTextColumns(m_pSource + iStart, iContent - iStart, Settings.m_nTabWidth, nColumns))
					continue;

				auto Canonical = fp_MakeIndent(nColumns);
				if (!fg_IsSameText(Canonical, m_pSource + iStart, iContent - iStart))
					fp_AddEdit("indentation", iStart, iContent - iStart, Canonical, Settings.m_bIndentWithTabs ? "indentation must use tabs" : "indentation must use spaces");
			}
		}
	}

	// A directive starts its line with its '#', indented behind it by a tab for each conditional it stands in, apart
	// from the code around it: a conditional's '#elif', '#else' and '#endif' stand where its '#if' does. A conditional
	// without alternatives around all of a file's code, as '#ifdef __cplusplus' around a header's, adds no depth. A
	// file whose conditionals do not pair up keeps its directives as they are.
	void CFormattingAnalyzer::fp_RuleDirectiveIndentation()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();

		struct CDirective
		{
			umint m_iToken;
			umint m_iGroup; // The conditional the directive opens, continues or stands in.
			bool m_bOwn;    // It opens or continues that conditional rather than standing in it.
		};

		struct CGroup
		{
			umint m_iParent;
			umint m_iOpen;
			umint m_iClose = 0;
			bool m_bAlternatives = false;
		};

		constexpr umint c_iNoGroup = TCLimitsInt<umint>::mc_Max;
		TCVector<CDirective> Directives;
		TCVector<CGroup> Groups;
		umint iGroup = c_iNoGroup;
		aint iFirstCode = -1;
		aint iLastCode = -1;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Preprocessor)
			{
				if (fg_IsCodeToken(Tokens[i]))
				{
					if (iFirstCode < 0)
						iFirstCode = aint(i);

					iLastCode = aint(i);
				}

				continue;
			}

			auto Keyword = fp_GetDirectiveKeyword(i);
			bool bOpens = Keyword == "if" || Keyword == "ifdef" || Keyword == "ifndef";
			bool bAlternative = Keyword == "elif" || Keyword == "elifdef" || Keyword == "elifndef" || Keyword == "else";
			if ((bAlternative || Keyword == "endif") && iGroup == c_iNoGroup)
				return;

			if (bOpens)
			{
				Groups.f_Insert(CGroup{.m_iParent = iGroup, .m_iOpen = i});
				iGroup = Groups.f_GetLen() - 1;
			}
			else if (bAlternative)
				Groups[iGroup].m_bAlternatives = true;

			if (!Keyword.f_IsEmpty())
				Directives.f_Insert(CDirective{i, iGroup, bOpens || bAlternative || Keyword == "endif"});

			if (Keyword == "endif")
			{
				Groups[iGroup].m_iClose = i;
				iGroup = Groups[iGroup].m_iParent;
			}
		}

		if (iGroup != c_iNoGroup)
			return;

		auto fIsWrapper = [&](CGroup const &_Group)
			{
				return _Group.m_iParent == c_iNoGroup && !_Group.m_bAlternatives && iFirstCode >= 0 && _Group.m_iOpen < umint(iFirstCode) && _Group.m_iClose > umint(iLastCode);
			}
		;

		for (auto const &Directive : Directives)
		{
			auto const &Token = Tokens[Directive.m_iToken];
			auto iLine = m_Lines.f_FindLine(Token.m_iOffset);
			auto iStart = m_Lines.f_GetLineStart(iLine);
			bool bFirst = true;
			for (auto iByte = iStart; iByte < Token.m_iOffset && bFirst; ++iByte)
				bFirst = fg_IsSpaceOrTab(m_pSource[iByte]);

			if (!bFirst || m_ProtectedStart[iLine] || !fp_IsLineSelected(iLine))
				continue;

			auto pText = m_pSource + Token.m_iOffset;
			umint iName = 1;
			while (iName < Token.m_nLength && fg_IsSpaceOrTab(pText[iName]))
				++iName;

			umint nDepth = 0;
			for (auto iEnclosing = Directive.m_bOwn ? Groups[Directive.m_iGroup].m_iParent : Directive.m_iGroup; iEnclosing != c_iNoGroup; iEnclosing = Groups[iEnclosing].m_iParent)
				nDepth += !fIsWrapper(Groups[iEnclosing]);

			// Indented with spaces the name stands where a tab per level would put it: '#   include' for '#\tinclude'.
			CStr Canonical = "#";
			if (m_Request.m_Settings.m_bIndentWithTabs)
			{
				for (umint iTab = 0; iTab < nDepth; ++iTab)
					Canonical += "\t";
			}
			else
			{
				auto nColumns = nDepth * m_Request.m_Settings.m_nTabWidth;
				auto nSpaces = nColumns > nDepth ? nColumns - 1 : nDepth;
				for (umint iSpace = 0; iSpace < nSpaces; ++iSpace)
					Canonical += " ";
			}

			auto iEnd = Token.m_iOffset + iName;
			if (!fg_IsSameText(Canonical, m_pSource + iStart, iEnd - iStart) && !fp_IsFormatOff(iStart, iEnd - iStart))
			{
				fp_AddEdit
					(
						"directive-indentation"
						, iStart
						, iEnd - iStart
						, Canonical
						, "a directive stands at the start of its line, indented behind its '#' a level for each conditional around it"
					)
				;
			}

			// The lines a directive is continued on move together, the least indented one level in from the directive. A raw
			// string's lines are its text, as are those a splice continues a literal onto, and keep the whole directive's lines
			// where they are, as does a splice joining two lines' text into one token. So does an include's, whose header name
			// keeps its blanks: '#include <dir/' over ' header.h>', and a '__has_include' one's, as an embedded resource's name does.
			// The keyword is read past the comments and splices that can stand in front of it: '#/**/include'.
			auto Keyword = fp_GetDirectiveKeyword(Directive.m_iToken);
			bool bInclude
				= Keyword == "include"
				|| Keyword == "import"
				|| Keyword == "embed"
				|| NStr::fg_StrFindLengthAware<false>(pText, Token.m_nLength, "__has_include") >= 0
				|| NStr::fg_StrFindLengthAware<false>(pText, Token.m_nLength, "__has_embed") >= 0
			;
			// A macro's name ending at a splice is function-like only where its parameter list follows on the next line
			// without a blank in front, which a blank the indentation adds would make object-like: '#define F\' over '(x) x'.
			bool bNameAtSplice = false;
			if (Keyword == "define")
			{
				ch8 DefineKeyword[16];
				umint iName = 0;
				fg_ReadDirectiveKeyword(pText, Token.m_nLength, DefineKeyword, iName);
				while (iName < Token.m_nLength && fg_IsSpaceOrTab(pText[iName]))
					++iName;

				auto iNameEnd = iName;
				while (iNameEnd < Token.m_nLength && (fg_CharIsAnsiAlphabetical(pText[iNameEnd]) || fg_CharIsNumber(pText[iNameEnd]) || pText[iNameEnd] == '_'))
					++iNameEnd;

				bNameAtSplice = iNameEnd > iName && iNameEnd < Token.m_nLength && pText[iNameEnd] == '\\';
			}

			bool bKeepLines
				= bInclude
				|| bNameAtSplice
				|| fg_HoldsRawStringLines(pText, Token.m_nLength)
				|| fg_HoldsSplicedLiteral(pText, Token.m_nLength)
				|| fg_JoinsAcrossSplice(pText, Token.m_nLength)
			;
			if (!Token.m_bMultiLine || bKeepLines)
				continue;

			struct CContinuation
			{
				umint m_iStart;
				umint m_iContent;
				umint m_nColumns;
				bool m_bSelected; // The line is selected; the lines are indented together or not at all.
			};

			TCVector<CContinuation> Continuations;
			umint nLeast = TCLimitsInt<umint>::mc_Max;
			bool bMeasured = true;
			for (auto iLine2 = iLine + 1; bMeasured && iLine2 < m_Lines.f_GetLineCount() && m_Lines.f_GetLineStart(iLine2) < Token.f_GetEnd(); ++iLine2)
			{
				auto iLineStart = m_Lines.f_GetLineStart(iLine2);
				auto iContent = iLineStart;
				while (iContent < Token.f_GetEnd() && fg_IsSpaceOrTab(m_pSource[iContent]))
					++iContent;

				// A line holding nothing but its splice, or nothing at all, has no indentation to keep.
				auto const *pContent = iContent < Token.f_GetEnd() ? m_pSource + iContent : nullptr;
				bool bEmpty = !pContent || *pContent == '\\' || *pContent == '\n' || *pContent == '\r';
				if (bEmpty)
					continue;

				umint nColumns = 0;
				bMeasured = fg_MeasureTextColumns(m_pSource + iLineStart, iContent - iLineStart, m_Request.m_Settings.m_nTabWidth, nColumns);
				Continuations.f_Insert(CContinuation{iLineStart, iContent, nColumns, fp_IsLineSelected(iLine2)});
				nLeast = fg_Min(nLeast, nColumns);
			}

			if (!bMeasured || Continuations.f_IsEmpty())
				continue;

			// The lines keep their depths against the least indented of them, which a selection holding only some of them would
			// move away from the others on every pass.
			umint nSelected = 0;
			for (auto const &Continuation : Continuations)
				nSelected += Continuation.m_bSelected;

			if (nSelected && nSelected != Continuations.f_GetLen())
				continue;

			auto nTab = m_Request.m_Settings.m_nTabWidth;
			auto nBase = (nDepth + 1) * nTab;

			auto fIndent = [&](umint _nColumns)
				{
					CStr Indent;
					umint nSpaces = _nColumns;
					if (m_Request.m_Settings.m_bIndentWithTabs)
					{
						for (umint iTab = 0; iTab < _nColumns / nTab; ++iTab)
							Indent += "\t";

						nSpaces = _nColumns % nTab;
					}

					for (umint iSpace = 0; iSpace < nSpaces; ++iSpace)
						Indent += " ";

					return Indent;
				}
			;

			// The lines of the directive with the indentation each is written at, to align the splices behind them.
			struct CSpliceLine
			{
				CStr m_Prefix; // The line as written, up to the blanks in front of its splice.
				umint m_iPadding = 0;
				umint m_iSplice = 0;
			};

			TCVector<CSpliceLine> SpliceLines;

			auto fSpliceLine = [&](umint _iContent, CStr const &_Indent, umint _iLineEnd)
				{
					// Only a backslash with nothing but blanks behind it splices; one in the text, a comment's 'a\\b', does not.
					auto iSplice = _iLineEnd;
					while (iSplice > _iContent && (m_pSource[iSplice - 1] == ' ' || m_pSource[iSplice - 1] == '\t' || m_pSource[iSplice - 1] == '\v' || m_pSource[iSplice - 1] == '\f'))
						--iSplice;

					if (iSplice <= _iContent || m_pSource[iSplice - 1] != '\\')
						return;

					--iSplice;
					auto iPadding = iSplice;
					while (iPadding > _iContent && fg_IsSpaceOrTab(m_pSource[iPadding - 1]))
						--iPadding;

					CStr Text = _Indent;
					Text.f_AddStr(m_pSource + _iContent, iPadding - _iContent);
					SpliceLines.f_Insert(CSpliceLine{fg_Move(Text), iPadding, iSplice});
				}
			;

			fSpliceLine(iEnd, Canonical, m_Lines.f_GetLineContentEnd(iLine));
			for (auto const &Continuation : Continuations)
			{
				auto Indent = fIndent(Continuation.m_nColumns - nLeast + nBase);
				auto iContinuationLine = m_Lines.f_FindLine(Continuation.m_iStart);
				if (m_Lines.f_GetLineContentEnd(iContinuationLine) < Token.f_GetEnd())
					fSpliceLine(Continuation.m_iContent, Indent, m_Lines.f_GetLineContentEnd(iContinuationLine));

				auto nLength = Continuation.m_iContent - Continuation.m_iStart;
				if (!Continuation.m_bSelected || fg_IsSameText(Indent, m_pSource + Continuation.m_iStart, nLength) || fp_IsFormatOff(Continuation.m_iStart, nLength))
					continue;

				fp_AddEdit("directive-indentation", Continuation.m_iStart, nLength, Indent, "a directive's continuation lines stand one level in from it");
			}

			// Splices aligned at one column, as the source writes them or behind the lines as they are indented here, are
			// aligned again behind the longest line, at the next tab stop where tabs pad them.
			if (SpliceLines.f_GetLen() < 2)
				continue;

			umint nAligned = 0;
			umint nWrittenAligned = 0;
			bool bSplicesMeasured = true;
			bool bSourceAligned = true;
			bool bWrittenAligned = true;
			bool bTabs = false;
			umint nLongest = 0;

			for (auto const &Line : SpliceLines)
			{
				auto iLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Line.m_iSplice));
				umint nSplice = 0;
				umint nPrefix = 0;
				bSplicesMeasured
					= bSplicesMeasured && fg_MeasureTextColumns(m_pSource + iLineStart, Line.m_iSplice - iLineStart, nTab, nSplice) && fg_MeasureTextColumns(Line.m_Prefix, nTab, nPrefix)
				;

				umint nWritten = nPrefix;
				for (auto iPad = Line.m_iPadding; iPad < Line.m_iSplice; ++iPad)
					nWritten = m_pSource[iPad] == '\t' ? (nWritten / nTab + 1) * nTab : nWritten + 1;

				bSourceAligned = bSourceAligned && (&Line == &SpliceLines[0] || nSplice == nAligned);
				bWrittenAligned = bWrittenAligned && (&Line == &SpliceLines[0] || nWritten == nWrittenAligned);
				nAligned = nSplice;
				nWrittenAligned = nWritten;
				nLongest = fg_Max(nLongest, nPrefix);
				for (auto iPad = Line.m_iPadding; iPad < Line.m_iSplice; ++iPad)
					bTabs |= m_pSource[iPad] == '\t';
			}

			if (!bSplicesMeasured || (!bSourceAligned && !bWrittenAligned))
				continue;

			auto nTarget = bTabs ? (nLongest / nTab + 1) * nTab : nLongest + 1;
			for (auto const &Line : SpliceLines)
			{
				umint nPrefix = 0;
				fg_MeasureTextColumns(Line.m_Prefix, nTab, nPrefix);
				CStr Padding;
				if (bTabs)
				{
					for (auto nColumn = nPrefix; nColumn < nTarget; nColumn = (nColumn / nTab + 1) * nTab)
						Padding += "\t";
				}
				else
				{
					for (auto nColumn = nPrefix; nColumn < nTarget; ++nColumn)
						Padding += " ";
				}

				auto nLength = Line.m_iSplice - Line.m_iPadding;
				if (fg_IsSameText(Padding, m_pSource + Line.m_iPadding, nLength) || fp_IsFormatOff(Line.m_iPadding, nLength))
					continue;

				fp_AddEdit("directive-indentation", Line.m_iPadding, nLength, Padding, "the splices of a directive's lines stay aligned");
			}
		}
	}

	void CFormattingAnalyzer::fp_RuleTrailingWhitespace()
	{
		if (!m_Request.m_Settings.m_bTrimTrailingWhitespace)
			return;

		for (umint iLine = 0; iLine < m_Lines.f_GetLineCount(); ++iLine)
		{
			if (m_ProtectedEnd[iLine] || !fp_IsLineSelected(iLine))
				continue;

			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
			auto iTrim = iEnd;
			while (iTrim > iStart && fg_IsSpaceOrTab(m_pSource[iTrim - 1]))
				--iTrim;

			// A build system file's name holds the blank it escapes: 'Value\ '.
			auto const &Tokens = m_Tokens.f_GetTokens();
			auto iToken = m_Tokens.f_FindToken(iTrim);
			if (iTrim < iEnd && iToken < Tokens.f_GetLen() && Tokens[iToken].m_iOffset < iTrim && Tokens[iToken].m_Kind == ECodeTokenKind::mc_Identifier)
				iTrim = fg_Min(Tokens[iToken].f_GetEnd(), iEnd);

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

			if (m_ProtectedEnd[iLine])
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

		// A newline behind a trailing backslash makes it a line splice, which joins what stands before it to what would
		// follow, so the file is told about rather than given one. Blanks behind it splice as well, and the trailing
		// whitespace rule takes them out anyway.
		auto const &Source = m_Request.m_Source;
		auto iTail = Source.f_GetLen();
		while (iTail > 0 && (m_pSource[iTail - 1] == ' ' || m_pSource[iTail - 1] == '\t' || m_pSource[iTail - 1] == '\v' || m_pSource[iTail - 1] == '\f'))
			--iTail;

		if (iTail > 0 && m_pSource[iTail - 1] == '\\')
		{
			fp_AddDiagnostic("final-newline", iTail - 1, 1, "the file ends in a backslash, which a final newline would make a line splice", false);

			return;
		}

		fp_AddEdit("final-newline", Source.f_GetLen(), 0, fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding()), "file must end with a newline");
	}

	void CFormattingAnalyzer::fp_EnsureSingleSpace(umint _iToken, bool _bBefore, ch8 const *_pRule, ch8 const *_pExplanation)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Token = Tokens[_iToken];

		// A gap the layout breaks is the layout's: a space written into it would land on
		// the offset of the line break, and the plan keeps only one edit there.
		// A comment behind the token on its line ends the gap there, whatever the layout does with the line below.
		auto iRight = _bBefore ? aint(_iToken) : fp_NextCode(_iToken);
		auto iNextSignificant = _bBefore ? aint(-1) : fp_NextSignificant(_iToken);
		auto const *pNextSignificant = iNextSignificant >= 0 ? &Tokens[umint(iNextSignificant)] : nullptr;
		bool bCommentBehind = pNextSignificant && (pNextSignificant->m_Kind == ECodeTokenKind::mc_BlockComment || pNextSignificant->m_Kind == ECodeTokenKind::mc_LineComment);
		if (iRight >= 0 && fp_IsBreakGap(umint(iRight)) && !bCommentBehind)
			return;

		if (_bBefore)
		{
			if (fp_IsFirstOnLine(_iToken))
				return;

			auto const &Previous = Tokens[_iToken - 1];
			if (Previous.m_Kind == ECodeTokenKind::mc_Whitespace)
			{
				if (Previous.m_nLength != 1 || m_pSource[Previous.m_iOffset] != ' ')
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
			if (Next.m_nLength != 1 || m_pSource[Next.m_iOffset] != ' ')
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

	// What ends the element a comma opens with none in it, and takes no space from the comma: a closer, a template
	// argument list's included, or a terminator.
	bool CFormattingAnalyzer::fp_ClosesBehindComma(umint _iToken) const
	{
		auto const &Token = m_Tokens.f_GetTokens()[_iToken];

		return m_Tokens.f_IsText(Token, ")")
			|| m_Tokens.f_IsText(Token, "]")
			|| m_Tokens.f_IsText(Token, "}")
			|| m_Tokens.f_IsText(Token, ";")
			|| (m_Tokens.f_IsText(Token, ">") && m_Structure.f_IsAngleBracket(_iToken))
		;
	}

	ECodeSpacing CFormattingAnalyzer::fp_DecideInlineSpacing(umint _iLeft, umint _iRight) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Left = Tokens[_iLeft];
		auto const &Right = Tokens[_iRight];
		bool bClause
			= m_Tokens.f_IsText(Left, "if") || m_Tokens.f_IsText(Left, "for") || m_Tokens.f_IsText(Left, "while") || m_Tokens.f_IsText(Left, "switch") || m_Tokens.f_IsText(Left, "catch")
		;
		if (bClause && m_Tokens.f_IsText(Right, "("))
			return ECodeSpacing::mc_Space;

		if (m_Structure.f_IsAngleBracket(_iLeft) && m_Structure.f_IsAngleBracket(_iRight) && m_Tokens.f_IsText(Left, ">") && m_Tokens.f_IsText(Right, ">"))
			return ECodeSpacing::mc_None;

		if (m_Tokens.f_IsText(Right, ","))
			return ECodeSpacing::mc_None;

		if (m_Tokens.f_IsText(Left, ","))
		{
			// The comma rule leaves the gap in front of a closer to the standard's spelling.
			if (fp_ClosesBehindComma(_iRight))
				return fp_GetCanonicalSpacing(_iLeft, _iRight);

			return Right.m_bMultiLine ? ECodeSpacing::mc_Preserve : ECodeSpacing::mc_Space;
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
				bool bClause
					= m_Tokens.f_IsText(Token, "if")
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

			// 'operator ,' names a function, and is spaced as every operator's name is.
			auto iBeforeComma = m_Tokens.f_IsText(Token, ",") ? fp_PreviousCode(i) : aint(-1);
			bool bOperatorName = iBeforeComma >= 0 && m_Tokens.f_IsText(Tokens[umint(iBeforeComma)], "operator");
			if (m_Tokens.f_IsText(Token, ",") && !bOperatorName)
			{
				fp_RemoveSpaceBefore(i, "comma-space", "the comma operator has no space before it");

				auto iNext = fp_NextSignificant(i);
				auto const *pNext = iNext >= 0 ? &Tokens[umint(iNext)] : nullptr;
				if (pNext && !pNext->m_bMultiLine && pNext->m_Kind != ECodeTokenKind::mc_Newline)
				{
					// An empty element between two commas leaves the second its rule, no space before it: 'f(a,,b)'.
					bool bClosing = fp_ClosesBehindComma(umint(iNext));
					if (!bClosing && !m_Tokens.f_IsText(*pNext, ","))
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

			// A '/' kept tight between two names is tight on one side only once the layout breaks the other, where the
			// next pass spaces it.
			if (Spacing == ECodeSpacing::mc_Preserve && m_Tokens.f_IsText(Tokens[umint(iPrevious)], "/") && fp_IsBreakGap(umint(iPrevious)))
				Spacing = ECodeSpacing::mc_Space;
			else if (Spacing == ECodeSpacing::mc_Preserve && m_Tokens.f_IsText(Tokens[i], "/"))
			{
				auto iNext = fp_NextCode(i);
				if (iNext >= 0 && fp_IsBreakGap(umint(iNext)))
					Spacing = ECodeSpacing::mc_Space;
			}

			if (Spacing == ECodeSpacing::mc_Space)
				fp_EnsureSingleSpace(i, true, "token-space", "these tokens are written with one space between them");
			else if (Spacing == ECodeSpacing::mc_None)
				fp_RemoveSpaceBefore(i, "token-space", "these tokens are written without a space between them");
		}

		// A comment trailing code stands one space behind it; columns aligned with tabs drift
		// apart as soon as the code in front of them is respaced. A block comment on one line
		// trails its code where it ends the line, or stands in front of the separator behind it.
		for (umint i = 1; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			bool bTrailing = Token.m_Kind == ECodeTokenKind::mc_LineComment;
			if (Token.m_Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine)
			{
				auto iAfter = i + 1;
				while (iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Whitespace)
					++iAfter;

				auto iBefore = fp_PreviousSignificant(i);
				bool bPreviousCode = iBefore >= 0 && fg_IsCodeToken(Tokens[umint(iBefore)]);
				bTrailing = bPreviousCode && (iAfter >= Tokens.f_GetLen() || Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Newline) && !fp_IsNameComment(i);

				// So does one behind an operand in front of its separator, which ends its line once the separator moves.
				bTrailing = bTrailing || fp_IsTrailingBlockComment(i);
			}

			// A byte order mark in front is no code the comment trails: it leads the file's first line.
			auto iLeading = fp_PreviousSignificant(i);
			bool bBehindMark = iLeading >= 0 && Tokens[umint(iLeading)].m_Kind == ECodeTokenKind::mc_ByteOrderMark;
			if (bTrailing && !bBehindMark && !fp_IsFirstOnLine(i))
				fp_EnsureSingleSpace(i, true, "comment-space", "a trailing comment stands one space behind its code");
		}
	}

	// With the project's AlignEnumValues, the '=' of a run of enumerators stands at one column, and with its
	// AlignTrailingComments, so do the comments behind a run of enumerators or members: one space past the longest
	// line of the run. A run is the entries of one body
	// on consecutive lines of the plan, each written on one line; a blank line, a comment line or an entry of another
	// kind ends it. A line the alignment makes too long is left for the line length check to report.
	void CFormattingAnalyzer::fp_AlignListColumns()
	{
		auto const *pStyle = m_Request.m_pStyle.f_Get();
		if (!pStyle || (!pStyle->m_bAlignTrailingComments && !pStyle->m_bAlignEnumValues) || !m_Structure.f_IsComplete())
			return;

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Source = m_Request.m_Source;
		auto nTab = m_Request.m_Settings.m_nTabWidth;

		TCVector<CCodeFormattingEdit> Plan;
		TCVector<umint> Sources;
		fp_BuildPlan(Plan, Sources);
		CStr Formatted = fg_ApplyCodeFormattingEdits(Source, Plan);
		CTextLineMap FormattedLines(Formatted);
		auto pFormatted = Formatted.f_GetStr();

		// The plan is ordered and its edits do not overlap, so an offset is mapped by a search over the change each edit in
		// front of it makes, which a run of a large list asks for at every entry.
		TCVector<aint> Deltas;
		Deltas.f_SetLen(Plan.f_GetLen() + 1);
		Deltas[0] = 0;
		for (umint iEdit = 0; iEdit < Plan.f_GetLen(); ++iEdit)
			Deltas[iEdit + 1] = Deltas[iEdit] + aint(Plan[iEdit].m_Replacement.f_GetLen()) - aint(Plan[iEdit].m_nLength);

		auto fFirstAt = [&](umint _iOffset)
			{
				umint iLow = 0;
				umint iHigh = Plan.f_GetLen();
				while (iLow < iHigh)
				{
					auto iMiddle = iLow + (iHigh - iLow) / 2;
					if (Plan[iMiddle].m_iOffset < _iOffset)
						iLow = iMiddle + 1;
					else
						iHigh = iMiddle;
				}

				return iLow;
			}
		;

		// A token starts behind what the plan inserts at its offset and ends in front of it: the line break the layout
		// puts between 'a = 1' and an enumerator's leading comma belongs to neither. The mapping is the one
		// fg_MapOffsetToConverted makes.
		auto fMap = [&](umint _iOffset, bool _bEnd) -> umint
			{
				auto iEdit = fFirstAt(_iOffset);
				bool bInsertion = false;
				for (auto iAt = iEdit; iAt < Plan.f_GetLen() && Plan[iAt].m_iOffset == _iOffset; ++iAt)
					bInsertion |= !Plan[iAt].m_nLength;

				bool bAfterInsertions = bInsertion ? !_bEnd : _bEnd;
				while (bAfterInsertions && iEdit < Plan.f_GetLen() && Plan[iEdit].m_iOffset == _iOffset && !Plan[iEdit].m_nLength)
					++iEdit;

				if (iEdit && _iOffset < Plan[iEdit - 1].f_GetEnd())
				{
					auto const &Edit = Plan[iEdit - 1];
					return umint(aint(Edit.m_iOffset) + Deltas[iEdit - 1]) + (bAfterInsertions ? Edit.m_Replacement.f_GetLen() : 0);
				}

				return umint(aint(_iOffset) + Deltas[iEdit]);
			}
		;

		auto fLine = [&](umint _iOffset, bool _bEnd)
			{
				return FormattedLines.f_FindLine(fMap(_iOffset, _bEnd));
			}
		;

		auto fColumn = [&](umint _iOffset, bool _bEnd, umint &o_nColumns)
			{
				auto iMapped = fMap(_iOffset, _bEnd);
				auto iStart = FormattedLines.f_GetLineStart(FormattedLines.f_FindLine(iMapped));

				return fg_MeasureTextColumns(pFormatted + iStart, iMapped - iStart, nTab, o_nColumns);
			}
		;

		// Alignment is spelled with spaces, which keep the column at any tab width.
		auto fTarget = [&](umint _nWidest)
			{
				return _nWidest + 1;
			}
		;

		auto fPadding = [&](umint _nFrom, umint _nTo)
			{
				CStr Padding;
				for (auto nColumn = _nFrom; nColumn < _nTo; ++nColumn)
					Padding += " ";

				return Padding;
			}
		;

		// The edits the other rules made, by offset. The alignment's own edits come later, each in a gap of its own.
		TCVector<umint> EditsByOffset;
		for (umint iEdit = 0; iEdit < m_Edits.f_GetLen(); ++iEdit)
			EditsByOffset.f_Insert(iEdit);

		EditsByOffset.f_Sort
			(
				[&](umint _iLeft, umint _iRight)
				{
					return m_Edits[_iLeft].m_iOffset <=> m_Edits[_iRight].m_iOffset;
				}
			)
		;

		auto fReplaceGap = [&](umint _iStart, umint _iEnd, CStr const &_Padding, ch8 const *_pRule, ch8 const *_pExplanation)
			{
				if (fp_IsDisabled(_iStart, _iEnd - _iStart) || !fp_IsSelected(_iStart, _iEnd - _iStart))
					return;

				// What the spacing rules wrote into the gap gives way to the alignment.
				umint iLow = 0;
				umint iHigh = EditsByOffset.f_GetLen();
				while (iLow < iHigh)
				{
					auto iMiddle = iLow + (iHigh - iLow) / 2;
					if (m_Edits[EditsByOffset[iMiddle]].m_iOffset < _iStart)
						iLow = iMiddle + 1;
					else
						iHigh = iMiddle;
				}

				for (auto iIndex = iLow; iIndex < EditsByOffset.f_GetLen() && m_Edits[EditsByOffset[iIndex]].m_iOffset <= _iEnd; ++iIndex)
				{
					auto iEdit = EditsByOffset[iIndex];
					if (m_Edits[iEdit].f_GetEnd() <= _iEnd)
						m_EditDropped[iEdit] = 1;
				}

				if (!fg_IsSameText(_Padding, m_pSource + _iStart, _iEnd - _iStart))
					fp_AddEdit(_pRule, _iStart, _iEnd - _iStart, _Padding, _pExplanation);
			}
		;

		struct CEntry
		{
			umint m_iFirst = 0;
			umint m_iLast = 0;
			umint m_iAssign = 0;            // The enumerator's '=', or zero.
			umint m_iComment = 0;           // The comment behind the entry, or zero.
			umint m_iLine = 0;              // The plan's line the entry stands on.
			bool m_bAligns = false;         // Written on one line.
			TCVector<umint> m_Continuation; // The source lines below that continue the comment.
		};

		auto fAlignRun = [&](TCVector<CEntry> const &_Run, bool _bEnum)
			{
				TCVector<aint> Shift;
				Shift.f_SetLen(_Run.f_GetLen());
				for (auto &nShift : Shift)
					nShift = 0;

				if (_bEnum && pStyle->m_bAlignEnumValues)
				{
					// The padding replaces the gap in front of the '=', so a comment in it keeps the entry out of the run:
					// 'B /* note */ = 2'.
					auto fAligns = [&](CEntry const &_Entry)
						{
							if (!_Entry.m_iAssign)
								return false;

							for (auto i = umint(fp_PreviousCode(_Entry.m_iAssign)) + 1; i < _Entry.m_iAssign; ++i)
							{
								if (Tokens[i].m_Kind != ECodeTokenKind::mc_Whitespace)
									return false;
							}

							return true;
						}
					;

					umint nWidest = 0;
					umint nValues = 0;
					for (auto const &Entry : _Run)
					{
						umint nColumns = 0;
						if (!fAligns(Entry) || !fColumn(Tokens[umint(fp_PreviousCode(Entry.m_iAssign))].f_GetEnd(), true, nColumns))
							continue;

						nWidest = fg_Max(nWidest, nColumns);
						++nValues;
					}

					// A single value has nothing to align with.
					bool bAny = nValues >= 2;

					auto nTarget = fTarget(nWidest);
					for (umint iEntry = 0; bAny && iEntry < _Run.f_GetLen(); ++iEntry)
					{
						auto const &Entry = _Run[iEntry];
						umint nName = 0;
						umint nAssign = 0;
						if (!fAligns(Entry))
							continue;

						auto iName = umint(fp_PreviousCode(Entry.m_iAssign));
						if (!fColumn(Tokens[iName].f_GetEnd(), true, nName) || !fColumn(Tokens[Entry.m_iAssign].m_iOffset, false, nAssign))
							continue;

						Shift[iEntry] = aint(nTarget) - aint(nAssign);
						auto Padding = fPadding(nName, nTarget);
						fReplaceGap(Tokens[iName].f_GetEnd(), Tokens[Entry.m_iAssign].m_iOffset, Padding, "enum-value-align", "the '=' of a run of enumerators stands at one column");
					}
				}

				// A comment's continuation lines stand where the comment does: at the entry's indentation, and spaces from there.
				auto fPlaceContinuation = [&](CEntry const &_Entry, umint _nColumn)
					{
						auto iMapped = fMap(Tokens[_Entry.m_iFirst].m_iOffset, false);
						auto iLineStart = FormattedLines.f_GetLineStart(FormattedLines.f_FindLine(iMapped));
						CStr Indent(pFormatted + iLineStart, iMapped - iLineStart);
						umint nIndent = 0;
						if (!fg_MeasureTextColumns(Indent, nTab, nIndent) || nIndent > _nColumn)
							return;

						auto Prefix = Indent + fPadding(nIndent, _nColumn);
						for (auto iLine : _Entry.m_Continuation)
						{
							auto iStart = m_Lines.f_GetLineStart(iLine);
							auto iIndent = iStart;
							while (fg_IsSpaceOrTab(m_pSource[iIndent]))
								++iIndent;

							fReplaceGap(iStart, iIndent, Prefix, "comment-align", "a comment continued below the entry it trails aligns with it");
						}
					}
				;

				auto fShiftContinuations = [&]
					{
						for (umint iEntry = 0; iEntry < _Run.f_GetLen(); ++iEntry)
						{
							auto const &Entry = _Run[iEntry];
							umint nComment = 0;
							if (Entry.m_iComment && Shift[iEntry] && !Entry.m_Continuation.f_IsEmpty() && fColumn(Tokens[Entry.m_iComment].m_iOffset, false, nComment))
								fPlaceContinuation(Entry, umint(aint(nComment) + Shift[iEntry]));
						}
					}
				;

				if (!pStyle->m_bAlignTrailingComments)
				{
					fShiftContinuations();

					return;
				}

				// An entry whose comment the maximum column cannot take keeps it one space behind its code, and the
				// others are aligned without it.
				auto nMaximum = pStyle->m_nAlignTrailingCommentsMaximumColumn;
				umint nWidest = 0;
				umint nComments = 0;
				TCVector<umint> Widths;
				TCVector<uint8> AlignedEntries;
				Widths.f_SetLen(_Run.f_GetLen());
				AlignedEntries.f_SetLen(_Run.f_GetLen());

				for (umint iEntry = 0; iEntry < _Run.f_GetLen(); ++iEntry)
				{
					auto const &Entry = _Run[iEntry];
					auto &bAligned = AlignedEntries[iEntry];
					bAligned = 0;
					umint nColumns = 0;
					if (!Entry.m_iComment || !fColumn(Tokens[Entry.m_iLast].f_GetEnd(), true, nColumns))
						continue;

					auto &nWidth = Widths[iEntry];
					nWidth = umint(aint(nColumns) + Shift[iEntry]);
					if (nMaximum && fTarget(nWidth) + 1 > nMaximum)
						continue;

					bAligned = 1;
					nWidest = fg_Max(nWidest, nWidth);
					++nComments;
				}

				if (nComments < 2)
				{
					fShiftContinuations();

					return;
				}

				auto nTarget = fTarget(nWidest);

				for (umint iEntry = 0; iEntry < _Run.f_GetLen(); ++iEntry)
				{
					auto const &Entry = _Run[iEntry];
					if (!Entry.m_iComment)
						continue;

					if (!AlignedEntries[iEntry])
					{
						umint nComment = 0;
						if (Shift[iEntry] && !Entry.m_Continuation.f_IsEmpty() && fColumn(Tokens[Entry.m_iComment].m_iOffset, false, nComment))
							fPlaceContinuation(Entry, umint(aint(nComment) + Shift[iEntry]));

						continue;
					}

					auto iGap = Tokens[Entry.m_iLast].f_GetEnd();
					fReplaceGap(iGap, Tokens[Entry.m_iComment].m_iOffset, fPadding(Widths[iEntry], nTarget), "comment-align", "the comments behind a run of entries stand at one column");
					fPlaceContinuation(Entry, nTarget);
				}
			}
		;

		auto fComment = [&](CEntry &o_Entry)
			{
				auto iNext = o_Entry.m_iLast + 1;
				while (iNext < Tokens.f_GetLen() && Tokens[iNext].m_Kind == ECodeTokenKind::mc_Whitespace)
					++iNext;

				auto const *pNext = iNext < Tokens.f_GetLen() ? &Tokens[iNext] : nullptr;
				if (!pNext || pNext->m_Kind != ECodeTokenKind::mc_LineComment || fLine(pNext->m_iOffset, false) != o_Entry.m_iLine)
					return;

				o_Entry.m_iComment = iNext;

				// The comment lines below indented past the entry's own line continue its comment, as
				// fp_AlignContinuations reads them.
				auto nCodeIndent = fp_GetSourceLineIndent(iNext);
				for (auto iLine = m_Lines.f_FindLine(Tokens[iNext].m_iOffset) + 1; iLine < m_Lines.f_GetLineCount(); ++iLine)
				{
					auto iStart = m_Lines.f_GetLineStart(iLine);
					auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
					auto iIndent = iStart;
					while (iIndent < iEnd && fg_IsSpaceOrTab(m_pSource[iIndent]))
						++iIndent;

					if (iIndent == iEnd || m_ProtectedStart[iLine])
						break;

					auto const &Comment = Tokens[m_Tokens.f_FindToken(iIndent)];
					umint nColumns = 0;
					if (Comment.m_Kind != ECodeTokenKind::mc_LineComment || Comment.m_iOffset != iIndent)
						break;

					if (!fg_MeasureTextColumns(m_pSource + iStart, iIndent - iStart, nTab, nColumns) || nColumns <= nCodeIndent)
						break;

					o_Entry.m_Continuation.f_Insert(iLine);
				}
			}
		;

		for (umint iNode = 0; iNode < Nodes.f_GetLen(); ++iNode)
		{
			auto const &Body = Nodes[iNode];
			if (Body.m_Kind != ECodeNodeKind::mc_Block || Body.m_iParent >= Nodes.f_GetLen() || Body.m_iLastToken <= Body.m_iFirstToken)
				continue;

			auto const &Owner = Nodes[Body.m_iParent];
			if (Owner.m_Kind != ECodeNodeKind::mc_Statement)
				continue;

			// A typedef names the record its class key defines: 'typedef struct { ... } CFoo;'.
			auto iKey = fp_SkipTemplateHeader(Owner.m_iFirstToken);
			if (m_Tokens.f_IsText(Tokens[iKey], "typedef"))
			{
				auto iDefined = fp_NextCode(iKey);
				iKey = iDefined >= 0 ? umint(iDefined) : iKey;
			}

			auto const &Key = Tokens[iKey];
			bool bEnum = m_Structure.f_IsEnumBody(iNode);
			bool bRecord = m_Tokens.f_IsText(Key, "struct") || m_Tokens.f_IsText(Key, "class") || m_Tokens.f_IsText(Key, "union") || Owner.m_bObjectiveCHeader;
			if (!bEnum && !bRecord)
				continue;

			TCVector<CEntry> Entries;

			auto fAdd = [&](umint _iFirst, umint _iLast, umint _iAssign, bool _bQualifies)
				{
					auto &Entry = Entries.f_Insert();
					Entry.m_iFirst = _iFirst;
					Entry.m_iLast = _iLast;
					Entry.m_iAssign = _iAssign;
					Entry.m_iLine = fLine(Tokens[_iFirst].m_iOffset, false);
					Entry.m_bAligns = _bQualifies && fLine(Tokens[_iLast].f_GetEnd(), true) == Entry.m_iLine;
					if (Entry.m_bAligns)
						fComment(Entry);
				}
			;

			if (bEnum)
			{
				auto iFirst = fp_NextCode(Body.m_iFirstToken);
				if (iFirst < 0 || umint(iFirst) >= Body.m_iLastToken)
					continue;

				auto nLevel = m_TokenDepth[umint(iFirst)];
				auto iStart = umint(iFirst);
				umint iAssign = 0;
				for (auto i = umint(iFirst); i <= Body.m_iLastToken; ++i)
				{
					auto const &Token = Tokens[i];
					if (!fg_IsCodeToken(Token))
						continue;

					bool bEnd = i == Body.m_iLastToken || (i > iStart && m_TokenDepth[i] == nLevel && m_Tokens.f_IsText(Token, ","));
					if (bEnd)
					{
						auto iLast = fp_PreviousCode(i);
						if (iLast >= 0 && umint(iLast) >= iStart)
							fAdd(iStart, umint(iLast), iAssign, true);

						iStart = i;
						iAssign = 0;

						continue;
					}

					if (!iAssign && m_TokenDepth[i] == nLevel && m_Tokens.f_IsText(Token, "="))
						iAssign = i;
				}
			}
			else
			{
				for (auto iChild : Body.m_Children)
				{
					auto const &Member = Nodes[iChild];
					if (Member.m_Kind != ECodeNodeKind::mc_Statement)
						continue;

					// A field is declared by one statement ending in its terminator with no parameter list in front of
					// an initializer; a function, a type or an alias is an entry of another kind.
					bool bField = m_Tokens.f_IsText(Tokens[Member.m_iLastToken], ";");
					for (auto pKeyword : {"using", "typedef", "template", "friend", "static_assert"})
						bField &= !m_Tokens.f_IsText(Tokens[Member.m_iFirstToken], pKeyword);

					// A class key declares a field where a name follows the type it names and no body defines that type:
					// 'struct ares_addr addr;', where 'struct CFoo;' and 'struct CFoo { ... };' declare a type.
					auto nLevel = m_TokenDepth[Member.m_iFirstToken];
					bool bClassKey = false;
					for (auto pKeyword : {"struct", "class", "union", "enum"})
						bClassKey |= m_Tokens.f_IsText(Tokens[Member.m_iFirstToken], pKeyword);

					if (bField && bClassKey)
					{
						aint iName = -1;
						for (auto i = Member.m_iFirstToken + 1; i <= Member.m_iLastToken; ++i)
						{
							auto const &Token = Tokens[i];
							if (m_Tokens.f_IsText(Token, "{"))
							{
								iName = -1;

								break;
							}

							bool bEnds
								= m_TokenDepth[i] == nLevel
								&& (m_Tokens.f_IsText(Token, ";") || m_Tokens.f_IsText(Token, "[") || m_Tokens.f_IsText(Token, "=") || m_Tokens.f_IsText(Token, ":"))
							;
							if (bEnds && iName < 0)
								iName = fp_PreviousCode(i);
						}

						auto iBeforeName = iName >= 0 ? fp_PreviousCode(umint(iName)) : aint(-1);
						bField
							= iName >= 0
							&& Tokens[umint(iName)].m_Kind == ECodeTokenKind::mc_Identifier
							&& iBeforeName > aint(Member.m_iFirstToken)
							&& !m_Tokens.f_IsText(Tokens[umint(iBeforeName)], "::")
						;
					}

					for (auto i = Member.m_iFirstToken; bField && i <= Member.m_iLastToken; ++i)
					{
						if (m_TokenDepth[i] != nLevel)
							continue;

						if (m_Tokens.f_IsText(Tokens[i], "="))
							break;

						bField = !m_Tokens.f_IsText(Tokens[i], "(");
					}

					fAdd(Member.m_iFirstToken, Member.m_iLastToken, 0, bField);
				}
			}

			TCVector<CEntry> Run;
			for (auto const &Entry : Entries)
			{
				if (!Run.f_IsEmpty() && (!Entry.m_bAligns || Entry.m_iLine != Run.f_GetLast().m_iLine + 1 + Run.f_GetLast().m_Continuation.f_GetLen()))
				{
					fAlignRun(Run, bEnum);
					Run.f_Clear();
				}

				if (Entry.m_bAligns)
					Run.f_Insert(Entry);
			}

			if (!Run.f_IsEmpty())
				fAlignRun(Run, bEnum);
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
		m_BlankBefore.f_SetLen(Tokens.f_GetLen());
		for (auto &bBlank : m_BlankBefore)
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
				auto const &Next = Tokens[iNext];
				if (Next.m_Kind == ECodeTokenKind::mc_Whitespace)
					continue;

				if (Next.m_Kind != ECodeTokenKind::mc_Newline)
					break;

				++nNewlines;
				iRunEnd = Next.f_GetEnd();
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
			// template header or 'typedef', and holds the body: 'struct CFoo : CBase { ... };'. A braced
			// initializer behind an '=' is a variable's, 'struct timespec Time = {...};', and so is a
			// definition that declares variables behind its body: '} m_Data;'.
			bool bTypeDefinition = false;
			if (!bBody)
			{
				auto iKey = fp_SkipTemplateHeader(Node.m_iFirstToken);
				bool bTypedef = false;
				if (auto iBehind = fp_NextCode(iKey); iBehind >= 0 && m_Tokens.f_IsText(Tokens[iKey], "typedef"))
				{
					iKey = umint(iBehind);
					bTypedef = true;
				}

				auto const &Key = Tokens[iKey];
				bool bClassKey = m_Tokens.f_IsText(Key, "struct") || m_Tokens.f_IsText(Key, "class") || m_Tokens.f_IsText(Key, "union") || m_Tokens.f_IsText(Key, "enum");
				for (auto iChild : Node.m_Children)
				{
					auto const &Child = Nodes[iChild];
					if (!bClassKey || Child.m_Kind != ECodeNodeKind::mc_Block || Child.m_iFirstToken <= iKey)
						continue;

					auto iBehindBody = fp_NextCode(Child.m_iLastToken);
					bool bInitializer = !bTypedef && iBehindBody >= 0 && umint(iBehindBody) < Node.m_iLastToken;
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
			umint iTrailing = TCLimitsInt<umint>::mc_Max;
			for (; iNext < Tokens.f_GetLen(); ++iNext)
			{
				auto const &Next = Tokens[iNext];
				auto Kind = Next.m_Kind;
				if (Kind == ECodeTokenKind::mc_Newline)
				{
					if (!nNewlines)
						iNewline = iNext;

					++nNewlines;
				}
				else if (Kind == ECodeTokenKind::mc_LineComment && !nNewlines)
					iTrailing = iNext;
				else if (Kind == ECodeTokenKind::mc_LineComment && nNewlines == 1 && iTrailing != TCLimitsInt<umint>::mc_Max)
				{
					// The comment lines continuing the one trailing the body below it, written further in than its code, go with it.
					umint nColumns = 0;
					auto iStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Next.m_iOffset));
					if (!fg_MeasureTextColumns(m_pSource + iStart, Next.m_iOffset - iStart, m_Request.m_Settings.m_nTabWidth, nColumns))
						break;

					if (nColumns <= fp_GetTrailedCodeIndent(iTrailing) || fp_StartsBodyBehind(iTrailing, nColumns))
						break;

					nNewlines = 0;
				}
				else if (Kind != ECodeTokenKind::mc_Whitespace)
					break;
			}

			// A brace behind the block says the block was no body: a requires expression's
			// requirements end in one, with the function's body below them.
			auto const *pNext = iNext < Tokens.f_GetLen() ? &Tokens[iNext] : nullptr;
			bool bEndsScope = pNext && (m_Tokens.f_IsText(*pNext, "}") || m_Tokens.f_IsText(*pNext, "{"));

			// A conditional's directive belongs to lines of its own and a pragma to the code it
			// brackets; any other, a '#define' behind a type included, is set off like code.
			bool bAttachedDirective = false;
			if (pNext && pNext->m_Kind == ECodeTokenKind::mc_Preprocessor)
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
				bool bMacro = m_Tokens.f_HasRole(Tokens[iNext], ECodeNameRole::mc_Macro) && iOpen >= 0 && m_Tokens.f_IsText(Tokens[umint(iOpen)], "(");
				if (bMacro)
					continue;
			}

			// The layout takes what stands behind the definition on its line onto a line of its own, behind a block comment
			// that stays trailing the definition as well: '}; /* note */ void f();'.
			if (!nNewlines)
			{
				auto iCode = iNext;
				while (iCode < Tokens.f_GetLen() && (Tokens[iCode].m_Kind == ECodeTokenKind::mc_BlockComment || Tokens[iCode].m_Kind == ECodeTokenKind::mc_Whitespace))
					++iCode;

				if (iCode < Tokens.f_GetLen() && fg_IsCodeToken(Tokens[iCode]))
					m_BlankBefore[iCode] = 1;

				continue;
			}

			// A blank line would part a NOLINTNEXTLINE from the line it suppresses.
			auto const &Suppressed = fp_GetSuppressedLines();
			auto iLineAfter = m_Lines.f_FindLine(Tokens[iNewline].f_GetEnd());
			if (iLineAfter < Suppressed.f_GetLen() && Suppressed[iLineAfter])
				continue;

			// The newline is written in the line ending the configuration asks for, which takes the line-ending rule's edit of it
			// over rather than leaving the two to compete.
			auto const &NewlineToken = Tokens[iNewline];
			CStr Newline = m_Request.m_Settings.m_EndOfLine ? CStr(fg_GetTextLineEndingBytes(*m_Request.m_Settings.m_EndOfLine)) : m_Tokens.f_GetText(NewlineToken);
			CStr Rule = bFunction ? "function-blank-line" : "type-blank-line";
			CStr Explanation = bFunction ? "a blank line follows a function's body" : "a blank line follows a type's definition";
			bool bTaken = false;
			for (umint iEdit = 0; iEdit < m_Edits.f_GetLen() && !bTaken; ++iEdit)
			{
				auto &Edit = m_Edits[iEdit];
				if (Edit.m_Rule != "line-ending" || Edit.m_iOffset != NewlineToken.m_iOffset || Edit.m_nLength != NewlineToken.m_nLength)
					continue;

				Edit.m_Replacement = Newline + Newline;
				Edit.m_Rule = Rule;
				m_EditExplanations[iEdit] = Explanation;
				bTaken = true;
			}

			if (!bTaken)
				fp_AddEdit(Rule, NewlineToken.m_iOffset, NewlineToken.m_nLength, Newline + Newline, Explanation);
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
				auto const *pLabel = iLabel >= 0 ? &Tokens[umint(iLabel)] : nullptr;
				bool bAfterLabel = pLabel && (m_Tokens.f_IsText(*pLabel, "public") || m_Tokens.f_IsText(*pLabel, "private") || m_Tokens.f_IsText(*pLabel, "protected"));
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
					m_BlankBefore[i] = 1;

					continue;
				}

				auto const &NewlineToken = Tokens[iNewline];
				auto Newline = m_Tokens.f_GetText(NewlineToken);
				fp_AddEdit("access-blank-line", NewlineToken.m_iOffset, NewlineToken.m_nLength, Newline + Newline, "a blank line stands in front of an access specifier");

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
	// against the source line the result's line came from. In the build system's files a string
	// ending a line counts for no width, since its value is what makes the line long and no break
	// can shorten it; what stands in front of it counts.
	void CFormattingAnalyzer::fp_DiagnoseLineLength(NContainer::TCVector<CCodeFormattingEdit> const &_Edits)
	{
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		if (!nMaxColumns)
			return;

		auto Formatted = fg_ApplyCodeFormattingEdits(m_Request.m_Source, _Edits);
		CTextLineMap Lines(Formatted);
		auto pFormatted = Formatted.f_GetStr();
		auto Language = m_Request.m_Settings.f_GetLanguage();
		TCUniquePointer<CCodeTokenStream> pStringTokens;
		if (Language == ECodeLanguage::mc_BuildSystem)
			pStringTokens = fg_Construct(Formatted, nullptr, Language);

		// The line's text in front of the string that ends it, which only a splice may follow, or the whole line.
		auto fWithoutEndingString = [&](umint _iStart, umint _nLength)
			{
				auto const &Tokens = pStringTokens->f_GetTokens();
				auto iEnd = _iStart + _nLength;
				aint iEnding = -1;
				for (auto iToken = pStringTokens->f_FindToken(_iStart); iToken < Tokens.f_GetLen() && Tokens[iToken].m_iOffset < iEnd; ++iToken)
				{
					auto Kind = Tokens[iToken].m_Kind;
					if (Kind == ECodeTokenKind::mc_StringLiteral)
						iEnding = aint(iToken);
					else if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline && Kind != ECodeTokenKind::mc_LineSplice)
						iEnding = -1;
				}

				if (iEnding < 0)
					return Formatted.f_Extract(_iStart, _nLength);

				auto iStringStart = fg_Max(Tokens[umint(iEnding)].m_iOffset, _iStart);

				return Formatted.f_Extract(_iStart, iStringStart - _iStart);
			}
		;

		auto iReported = TCLimitsInt<umint>::mc_Max;
		COffsetToOriginal ToOriginal{_Edits};
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
			bool bMeasured = fg_MeasureTextColumns(pFormatted + iStart, nLength, m_Request.m_Settings.m_nTabWidth, nColumns);
			if (bMeasured && nColumns <= nMaxColumns)
				continue;

			umint nCodeColumns = 0;
			if (pStringTokens && fg_MeasureTextColumns(fWithoutEndingString(iStart, nLength), m_Request.m_Settings.m_nTabWidth, nCodeColumns) && nCodeColumns <= nMaxColumns)
				continue;

			// Several lines of the result can come from one line of the source, which is
			// then named once.
			// A region the formatter is turned off for keeps its lines as written, long ones too.
			auto iSource = m_Lines.f_FindLine(fg_MapOffsetToOriginal(ToOriginal, Lines.f_GetLineStart(iLine)));
			if (iSource == iReported || !fp_IsLineSelected(iSource) || fp_IsFormatOff(m_Lines.f_GetLineStart(iSource), m_Lines.f_GetLine(iSource).m_nLength))
				continue;

			iReported = iSource;

			CStr Explanation
				= bMeasured
				? "line length {} exceeds max_line_length = {}"_f << nColumns << nMaxColumns
				: "line length overflows the column counter and exceeds max_line_length = {}"_f << nMaxColumns
			;
			fp_AddDiagnostic("line-length", m_Lines.f_GetLineStart(iSource), m_Lines.f_GetLine(iSource).m_nLength, Explanation, false);
		}
	}
}
