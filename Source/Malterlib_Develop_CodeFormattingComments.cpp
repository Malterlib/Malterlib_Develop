// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace NMib::NDevelop::NPrivate
{
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

				auto pFormatted = Formatted.f_GetStr();
				auto iMapped = fg_MapOffsetToConverted(Plan, _iOffset, true);
				auto iFormattedStart = iMapped;
				while (iFormattedStart)
				{
					auto Char = pFormatted[iFormattedStart - 1];
					if (Char == '\n' || Char == '\r')
						break;

					--iFormattedStart;
				}

				return fg_MeasureTextColumns(pFormatted + iFormattedStart, iMapped - iFormattedStart, nTab, o_nColumns);
			}
		;

		auto fPlaceLine = [&](umint _iStart, umint _iIndent, umint _nColumns, ch8 const *_pRule, ch8 const *_pExplanation)
			{
				// The layout's own placement of the line gives way to the alignment. A break the layout writes over the
				// line's start, the newline included, keeps its line breaks and takes the alignment's indentation.
				for (umint iEdit = 0; iEdit < m_Edits.f_GetLen(); ++iEdit)
				{
					auto const &Edit = m_Edits[iEdit];
					auto &bDropped = m_EditDropped[iEdit];
					if (bDropped)
						continue;

					if (Edit.m_iOffset >= _iStart && Edit.f_GetEnd() <= _iIndent)
						bDropped = 1;
					else if (Edit.m_iOffset < _iStart && Edit.f_GetEnd() == _iIndent)
					{
						// A line ends in LF, CRLF or a lone CR.
						auto iBreak = fg_Max(Edit.m_Replacement.f_FindCharReverse('\n'), Edit.m_Replacement.f_FindCharReverse('\r'));
						if (iBreak < 0)
							continue;

						bDropped = 1;
						auto iOffset = Edit.m_iOffset;
						auto nLength = Edit.m_nLength;
						auto Replacement = Edit.m_Replacement.f_Left(iBreak + 1) + fp_MakeIndent(_nColumns);

						// One that writes what the source has is no edit, which the next pass would make again.
						if (!fg_IsSameText(Replacement, m_pSource + iOffset, nLength))
							fp_AddEdit(_pRule, iOffset, nLength, Replacement, _pExplanation);

						return;
					}
				}

				auto Replacement = fp_MakeIndent(_nColumns);
				if (!fg_IsSameText(Replacement, m_pSource + _iStart, _iIndent - _iStart))
					fp_AddEdit(_pRule, _iStart, _iIndent - _iStart, Replacement, _pExplanation);
			}
		;

		// The comment lines aligned as continuations, which no rule below places again.
		TCVector<bool> Continued;
		Continued.f_SetLen(Tokens.f_GetLen());
		for (auto &bContinued : Continued)
			bContinued = false;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind != ECodeTokenKind::mc_LineComment || fp_IsFirstOnLine(i))
				continue;

			// The comment trails the line the layout ends the code on, which starts where the layout starts it: 'g(a,' over
			// 'b); // x' joined trails 'g'.
			auto nCodeIndent = fp_GetTrailedCodeIndent(i);
			auto iLineStart = fp_PreviousCode(i);
			while
			(
				iLineStart > 0
				&& !fp_IsBreakGap(umint(iLineStart))
				&& (EGap(m_GapState[umint(iLineStart)]) == EGap::mc_Inline || (!fp_IsFirstOnLine(umint(iLineStart)) && !fp_StartsLineBehindComments(umint(iLineStart))))
			)
			{
				iLineStart = fp_PreviousCode(umint(iLineStart));
			}

			// A comment line continues the comment as the source spells it, which the blank line rule, running before the
			// layout, reads too. One the layout gives another start, joined or split, continues it where it would in the
			// layout as well, since the alignment makes the next pass read it so.
			auto iSourceFirst = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(m_Lines.f_FindLine(Token.m_iOffset)));
			while (iSourceFirst < i && !fg_IsCodeToken(Tokens[iSourceFirst]))
				++iSourceFirst;

			bool bReshaped = iLineStart >= 0 && umint(iLineStart) != iSourceFirst;
			umint nLayoutCodeIndent = nCodeIndent;
			if (bReshaped && fp_IsBreakGap(umint(iLineStart)))
				nLayoutCodeIndent = m_GapIndent[umint(iLineStart)];
			else if (bReshaped && m_Lines.f_FindLine(Tokens[umint(iLineStart)].m_iOffset) != m_Lines.f_FindLine(Token.m_iOffset))
				nLayoutCodeIndent = fp_GetSourceLineIndent(umint(iLineStart));

			if (bReshaped && fp_StartsSharedCaseLabel(umint(iLineStart), i))
				nLayoutCodeIndent += nTab;

			umint nTarget = TCLimitsInt<umint>::mc_Max;
			for (auto iLine = m_Lines.f_FindLine(Token.m_iOffset) + 1; iLine < m_Lines.f_GetLineCount(); ++iLine)
			{
				auto iStart = m_Lines.f_GetLineStart(iLine);
				auto iEnd = m_Lines.f_GetLineContentEnd(iLine);
				auto iIndent = iStart;
				while (iIndent < iEnd && fg_IsSpaceOrTab(m_pSource[iIndent]))
					++iIndent;

				if (iIndent == iEnd || m_ProtectedStart[iLine])
					break;

				auto iLineComment = m_Tokens.f_FindToken(iIndent);
				if (!fp_IsContinuationComment(iLineComment) || Tokens[iLineComment].m_iOffset != iIndent)
					break;

				umint nColumns = 0;
				if (!fg_MeasureTextColumns(m_pSource + iStart, iIndent - iStart, nTab, nColumns))
					break;

				// Where the layout leaves the code, a comment's line moved with the code around it is moved too.
				if (iLineComment < m_CommentMoved.f_GetLen() && m_CommentMoved[iLineComment] >= 2)
					break;

				// A line the layout placed is where it placed it.
				auto nLayoutColumns = nColumns;
				if (iLineComment < m_CommentMoved.f_GetLen() && m_CommentMoved[iLineComment])
					nLayoutColumns = m_CommentIndent[iLineComment];
				else if (fp_IsBreakGap(iLineComment))
					nLayoutColumns = m_GapIndent[iLineComment];

				if (nColumns <= nCodeIndent && !(bReshaped && nLayoutColumns > nLayoutCodeIndent))
					break;

				if (fp_StartsBodyBehind(i, nColumns))
					break;

				if (nTarget == TCLimitsInt<umint>::mc_Max && !fFormattedColumn(Token.m_iOffset, nTarget))
					break;

				fPlaceLine(iStart, iIndent, nTarget, "comment-space", "a comment continued below the code it trails aligns with it");
				Continued[iLineComment] = true;
			}
		}

		// The comment lines directly above a statement, written at its depth, describe it and move
		// with it. One at the very start of its line above code written further in comments out what
		// it stands in front of and keeps its place.
		for (umint i = 0; m_Structure.f_IsComplete() && i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			auto Kind = Token.m_Kind;
			bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
			if (!bComment || !fp_IsFirstOnLine(i))
				continue;

			// One the run rule placed where the code under it stands is placed.
			auto iLine = m_Lines.f_FindLine(Token.m_iOffset);
			if (m_ProtectedStart[iLine] || m_CommentMoved[i] >= 2 || Continued[i])
				continue;

			// A comment naming the code the layout joins onto the line in front goes with that code.
			if (Kind == ECodeTokenKind::mc_BlockComment && fp_IsAttachedComment(i))
			{
				auto iNamed = fp_NextCode(i);
				if (iNamed >= 0 && EGap(m_GapState[umint(iNamed)]) == EGap::mc_Inline)
					continue;
			}

			// A comment continuing the one that trails the line above, written further in than that line's code, is aligned
			// with that one. A blank line parts the two.
			auto fIsLineBreak = [&](aint _iToken)
				{
					if (_iToken < 0 || Tokens[umint(_iToken)].m_Kind != ECodeTokenKind::mc_Newline)
						return false;

					auto iBefore = fp_PreviousSignificant(umint(_iToken));
					return iBefore < 0 || Tokens[umint(iBefore)].m_Kind != ECodeTokenKind::mc_Newline;
				}
			;

			auto iAbove = fp_PreviousSignificant(i);
			while (iAbove >= 0 && (fIsLineBreak(iAbove) || (Tokens[umint(iAbove)].m_Kind == ECodeTokenKind::mc_LineComment && fp_IsFirstOnLine(umint(iAbove)))))
				iAbove = fp_PreviousSignificant(umint(iAbove));

			bool bContinues
				= iAbove >= 0
				&& Tokens[umint(iAbove)].m_Kind == ECodeTokenKind::mc_LineComment
				&& fp_GetSourceLineIndent(i) > fp_GetTrailedCodeIndent(umint(iAbove))
				&& !fp_StartsBodyBehind(umint(iAbove), fp_GetSourceLineIndent(i))
			;
			if (bContinues)
				continue;

			// The run ends at the first code token, every line up to it a comment line.
			umint iCode = i + 1;
			bool bAttached = true;
			bool bBlank = false;
			umint nNewlines = 0;
			for (; iCode < Tokens.f_GetLen(); ++iCode)
			{
				auto CodeKind = Tokens[iCode].m_Kind;
				if (CodeKind == ECodeTokenKind::mc_Newline)
				{
					bBlank |= ++nNewlines > 1;

					continue;
				}

				if (CodeKind == ECodeTokenKind::mc_Whitespace)
					continue;

				// A block comment naming the code behind it on its line starts that line as the code would.
				bool bNames = CodeKind == ECodeTokenKind::mc_BlockComment && fp_IsFirstOnLine(iCode) && iCode > i && !fp_IsLastOnLine(iCode);
				if ((CodeKind == ECodeTokenKind::mc_LineComment || CodeKind == ECodeTokenKind::mc_BlockComment) && !bNames)
				{
					bAttached &= fp_IsFirstOnLine(iCode);
					nNewlines = 0;

					continue;
				}

				break;
			}

			bool bEnds = iCode >= Tokens.f_GetLen();
			auto const *pCode = bEnds ? nullptr : &Tokens[iCode];
			bool bCloses = !pCode || pCode->m_Kind == ECodeTokenKind::mc_Preprocessor || m_Tokens.f_IsText(*pCode, "}");
			if (!bEnds && m_ProtectedStart[m_Lines.f_FindLine(Tokens[iCode].m_iOffset)])
				continue;

			if (!bAttached || (!bEnds && !nNewlines))
				continue;

			// The comment lines in front of a list's or a call's closing marker stand among its elements, one level in from
			// the marker.
			umint nAmong = 0;
			auto iGroup = bEnds ? m_Structure.f_GetNodes().f_GetLen() : m_Structure.f_FindNodeClosingAt(iCode);

			// A line comment continuing the one trailing the line above, further in than its code, is that comment's.
			auto iTrailing = fp_PreviousSignificant(i);
			if (iTrailing >= 0 && Tokens[umint(iTrailing)].m_Kind == ECodeTokenKind::mc_Newline)
				iTrailing = fp_PreviousSignificant(umint(iTrailing));

			bool bContinuesTrailing
				= Kind == ECodeTokenKind::mc_LineComment
				&& iTrailing >= 0
				&& Tokens[umint(iTrailing)].m_Kind == ECodeTokenKind::mc_LineComment
				&& !fp_IsFirstOnLine(umint(iTrailing))
				&& fp_GetSourceLineIndent(i) > fp_GetTrailedCodeIndent(umint(iTrailing))
			;
			bool bAmong
				= !bContinuesTrailing
				&& iGroup < m_Structure.f_GetNodes().f_GetLen()
				&& m_Structure.f_GetNodes()[iGroup].m_Kind == ECodeNodeKind::mc_Group
				&& iGroup < m_KeepsListLines.f_GetLen()
				&& !m_KeepsListLines[iGroup]
				&& !fp_HasOpaqueDirective(m_Structure.f_GetNodes()[iGroup].m_iFirstToken, iCode)
				&& fFormattedColumn(Tokens[iCode].m_iOffset, nAmong)
			;
			if (bAmong)
			{
				nAmong += m_Request.m_Settings.m_nTabWidth;
				auto nDelta = aint(nAmong) - aint(fp_GetSourceLineIndent(i));
				if (!fp_CanShiftCommentLines(i, nDelta))
					continue;

				fPlaceLine(m_Lines.f_GetLineStart(iLine), Token.m_iOffset, nAmong, "comment-indent", "a comment in front of a closing marker stands among the elements");
				fp_ShiftCommentLines(i, nDelta, "comment-indent", "a comment in front of a closing marker stands among the elements");

				continue;
			}

			// The comment lines closing a block, the file or a conditional's branch, or that a blank line parts from the
			// code below, stand at the depth of the statement above them, where they were written at it directly under it.
			// Parted from both, they stand at the depth of the code below.
			auto iReference = iCode;
			if (bCloses || bBlank)
			{
				auto const &Nodes = m_Structure.f_GetNodes();
				auto iLast = fp_PreviousCode(i);
				umint nAbove = 0;
				bool bUnder = iLast >= 0;
				for (auto iGap = umint(iLast) + 1; bUnder && iGap < i; ++iGap)
				{
					if (Tokens[iGap].m_Kind == ECodeTokenKind::mc_Newline)
						bUnder = ++nAbove < 2;
					else if (Tokens[iGap].m_Kind != ECodeTokenKind::mc_Whitespace)
						nAbove = 0;
				}

				auto iStatement = bUnder && iLast >= 0 ? m_Structure.f_FindStatementEndingAt(umint(iLast)) : Nodes.f_GetLen();
				if (iStatement < Nodes.f_GetLen() && fp_IsFirstOnLine(Nodes[iStatement].m_iFirstToken))
				{
					// The block comments naming the statement on its line start the line.
					iReference = Nodes[iStatement].m_iFirstToken;
					for (auto iName = iReference; iName > 0; --iName)
					{
						auto const &Name = Tokens[iName - 1];
						if (Name.m_Kind == ECodeTokenKind::mc_BlockComment && !Name.m_bMultiLine)
							iReference = iName - 1;
						else if (Name.m_Kind != ECodeTokenKind::mc_Whitespace)
							break;
					}
				}
				else if (bCloses)
					continue;
			}

			// A label has a depth of its own, and the comment above it stays with the body it follows, and moves with
			// that body where it was written at its depth.
			constexpr ch8 const *c_pLabels[] = {"public", "private", "protected", "case", "default", "@public", "@private", "@protected", "@package"};
			bool bLabel = false;
			for (auto pLabel : c_pLabels)
				bLabel |= m_Tokens.f_IsText(Tokens[iReference], pLabel);

			bool bAtBodyDepth = false;

			// One written at the label's own column describes the label, and moves with it.
			if (bLabel && fp_GetSourceLineIndent(i) == fp_GetSourceLineIndent(iReference))
				bLabel = false;

			if (bLabel)
			{
				auto const &Nodes = m_Structure.f_GetNodes();
				auto iLast = fp_PreviousCode(i);
				auto iBody = iLast >= 0 ? m_Structure.f_FindStatementEndingAt(umint(iLast)) : Nodes.f_GetLen();
				if (iBody >= Nodes.f_GetLen() || m_Tokens.f_IsText(Tokens[umint(iLast)], ":"))
					continue;

				// A body on its label's line has its depth one level in from the label.
				auto iBodyFirst = Nodes[iBody].m_iFirstToken;
				auto iBodyLabel = fp_FindSharedCaseLabel(iBodyFirst);
				umint nBodyDepth = fp_GetSourceLineIndent(iBodyFirst);
				if (iBodyLabel >= 0)
					nBodyDepth = fp_GetSourceLineIndent(umint(iBodyLabel)) + m_Request.m_Settings.m_nTabWidth;
				else if (!fp_IsFirstOnLine(iBodyFirst))
					continue;

				if (nBodyDepth != fp_GetSourceLineIndent(i))
					continue;

				iReference = iBodyFirst;
				bAtBodyDepth = true;
			}

			// Only a comment written where its statement was follows it to where it goes, or one that opens
			// a case's body behind its label, which stands where the body does wherever it was written.
			bool bOpensCase = false;
			if (auto iLabelEnd = fp_PreviousCode(i); iLabelEnd >= 0 && m_Tokens.f_IsText(Tokens[umint(iLabelEnd)], ":"))
			{
				auto iStatement = m_Structure.f_FindStatementEndingAt(umint(iLabelEnd));
				if (iStatement < m_Structure.f_GetNodes().f_GetLen())
				{
					auto const &LabelFirst = Tokens[m_Structure.f_GetNodes()[iStatement].m_iFirstToken];
					bOpensCase = m_Tokens.f_IsText(LabelFirst, "case") || m_Tokens.f_IsText(LabelFirst, "default");
				}
			}

			if (!bOpensCase && !bAtBodyDepth && fp_GetSourceLineIndent(i) != fp_GetSourceLineIndent(iReference))
				continue;

			umint nTarget = 0;
			auto iLabel = fp_FindSharedCaseLabel(iReference);
			if (!fFormattedColumn(Tokens[iLabel >= 0 ? umint(iLabel) : iReference].m_iOffset, nTarget))
				continue;

			if (iLabel >= 0)
				nTarget += m_Request.m_Settings.m_nTabWidth;

			auto nDelta = aint(nTarget) - aint(fp_GetSourceLineIndent(i));
			if (!fp_CanShiftCommentLines(i, nDelta))
				continue;

			fPlaceLine(m_Lines.f_GetLineStart(iLine), Token.m_iOffset, nTarget, "comment-indent", "a comment above a statement moves with it");
			fp_ShiftCommentLines(i, nDelta, "comment-indent", "a comment above a statement moves with it");
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
				while (iIndent < iEnd && fg_IsSpaceOrTab(m_pSource[iIndent]))
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
					auto const &Edit = m_Edits[iEdit];
					if (Edit.m_iOffset >= iGap && Edit.f_GetEnd() <= Tokens[_iToken].m_iOffset)
						m_EditDropped[iEdit] = 1;
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

			bool bArray = Node.m_Bracket == ECodeBracket::mc_Square;

			// A directive's lines are the conditional's, and keep where they are. One the layout opens is placed by it.
			auto iFirstElement = fp_NextCode(iOpen);
			auto FirstElementGap = iFirstElement >= 0 ? EGap(m_GapState[umint(iFirstElement)]) : EGap::mc_Inline;
			bool bLaidOut = FirstElementGap == EGap::mc_Break || FirstElementGap == EGap::mc_OwnLine;
			if (!fp_IsDSLData(iNode) || Node.m_bHasDirective || bLaidOut)
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
						iStatement = iStatement ? Nodes[iStatement].m_iParent : Nodes.f_GetLen();

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
				if (m_ProtectedStart[iLine])
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

		auto fIsLineLead = [&](umint _iToken)
			{
				auto Kind = Tokens[_iToken].m_Kind;

				return Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_ByteOrderMark;
			}
		;

		// The column each continued string's line is placed at here, which the plan the columns are read from predates.
		NContainer::TCMap<umint, umint> PlacedStringLines;
		for (umint i = 0; m_Structure.f_IsComplete() && i < Tokens.f_GetLen(); ++i)
		{
			auto iPrevious = fp_ContinuesLiteral(i);
			if (iPrevious < 0)
				continue;

			auto iLine = m_Lines.f_FindLine(Tokens[i].m_iOffset);
			if (m_ProtectedStart[iLine])
				continue;

			// The chain is placed against the line its first literal stands on.
			auto iHead = fp_FindStringRunStart(umint(iPrevious));

			auto iHeadLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[iHead].m_iOffset));
			auto iHeadFirst = m_Tokens.f_FindToken(iHeadLineStart);
			while (iHeadFirst < iHead && fIsLineLead(iHeadFirst))
				++iHeadFirst;

			// A comma the layout moves in front of the line's first token starts the line it is written on, at the
			// indentation its break gives it.
			// So does the line's first token where the layout joins the literal onto the line in front: '.Key =' over '"a"'.
			bool bMovedHead = false;
			while (true)
			{
				auto iBefore = fp_PreviousCode(iHeadFirst);
				if (iBefore < 0 || EGap(m_GapState[iHeadFirst]) != EGap::mc_Inline || !fp_SpansLines(umint(iBefore), iHeadFirst))
					break;

				iHeadFirst = umint(iBefore);
				bMovedHead = true;
				while (true)
				{
					auto iLineBefore = fp_PreviousCode(iHeadFirst);
					bool bJoined
						= iLineBefore >= 0
						&& !fp_IsBreakGap(iHeadFirst)
						&& (EGap(m_GapState[iHeadFirst]) == EGap::mc_Inline || !fp_IsFirstOnLine(iHeadFirst))
						&& !fp_SpansLines(umint(iLineBefore), iHeadFirst)
					;
					if (!bJoined)
						break;

					iHeadFirst = umint(iLineBefore);
				}
			}

			if (bMovedHead && !fp_IsBreakGap(iHeadFirst))
				continue;

			// A head line the layout breaks starts where its last break in front of the head is: '(' over 'Loc' over
			// ', "a "' over '"b"' has its head line start at the comma, and ', g' over '(' over '"a"' over '"b"' at '"a"'.
			for (auto iBreak = iHead; iBreak > iHeadFirst; --iBreak)
			{
				if (fg_IsCodeToken(Tokens[iBreak]) && fp_IsBreakGap(iBreak))
				{
					iHeadFirst = iBreak;

					break;
				}
			}

			auto iStatement = m_Structure.f_FindEnclosingNode(iHead);
			while (iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement)
				iStatement = iStatement ? Nodes[iStatement].m_iParent : Nodes.f_GetLen();

			bool bStartsStatement = iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_iFirstToken == iHeadFirst;
			bStartsStatement |= fp_ContinuesValue(i);

			umint nColumns = 0;
			if (bMovedHead)
				nColumns = m_GapIndent[iHeadFirst];
			else if (!fFormattedColumn(Tokens[iHeadFirst].m_iOffset, nColumns))
				continue;

			// A head line the layout breaks stands at that break's indentation; one it leaves moves with an opened list.
			aint nHeadColumn = aint(nColumns) + fShiftAt(m_Lines.f_FindLine(Tokens[iHead].m_iOffset));

			// A placed line is the one its first token starts in the source, not one the layout starts behind it: '"b"), "c"'
			// with the comma moved onto a line of its own.
			auto iHeadSourceLine = m_Lines.f_FindLine(Tokens[iHeadFirst].m_iOffset);
			auto iSourceFirst = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(iHeadSourceLine));
			while (iSourceFirst < iHeadFirst && !fg_IsCodeToken(Tokens[iSourceFirst]))
				++iSourceFirst;

			auto *pPlaced = fp_IsFirstOnLine(iHeadFirst) && iSourceFirst == iHeadFirst ? PlacedStringLines.f_FindEqual(iHeadSourceLine) : nullptr;
			if (pPlaced)
				nHeadColumn = aint(*pPlaced);
			else if (bMovedHead || fp_IsBreakGap(iHeadFirst))
				nHeadColumn = aint(m_GapIndent[iHeadFirst]);

			// A run on lines of its own behind the '=' ending a line stands one level in from that line, its first line with
			// the rest: 'CStr Message =' over '"a"' over '"b"'.
			auto iValueLead = fp_PreviousCode(iHead);
			auto HeadGap = EGap(m_GapState[iHead]);
			bool bValueLines
				= iValueLead >= 0
				&& m_Tokens.f_IsText(Tokens[umint(iValueLead)], "=")
				&& iHeadFirst == iHead
				&& fp_SpansLines(umint(iValueLead), iHead)
				&& (HeadGap == EGap::mc_Keep || HeadGap == EGap::mc_Indent)
			;
			if (bValueLines)
			{
				auto iLeadLine = m_Lines.f_FindLine(Tokens[umint(iValueLead)].m_iOffset);
				auto iLeadFirst = m_Tokens.f_FindToken(m_Lines.f_GetLineStart(iLeadLine));
				while (iLeadFirst < umint(iValueLead) && !fg_IsCodeToken(Tokens[iLeadFirst]))
					++iLeadFirst;

				umint nLead = 0;
				auto iHeadLine = m_Lines.f_FindLine(Tokens[iHead].m_iOffset);
				if (!m_ProtectedStart[iHeadLine] && fFormattedColumn(Tokens[iLeadFirst].m_iOffset, nLead))
				{
					nHeadColumn = aint(nLead + nTab);
					bStartsStatement = false;
					if (!PlacedStringLines.f_FindEqual(iHeadLine))
					{
						PlacedStringLines[iHeadLine] = umint(nHeadColumn);
						fPlaceLine
							(
								m_Lines.f_GetLineStart(iHeadLine)
								, Tokens[iHead].m_iOffset
								, umint(nHeadColumn)
								, "string-continuation"
								, "a string a value starts on a line of its own stands one level in"
							)
						;

						// The comment lines in front of the run stand with it.
						for (auto iCommentLine = iLeadLine + 1; iCommentLine < iHeadLine; ++iCommentLine)
						{
							auto iCommentIndent = fLineIndentOffset(iCommentLine);
							if (m_ProtectedStart[iCommentLine] || iCommentIndent == m_Lines.f_GetLineContentEnd(iCommentLine))
								continue;

							auto const &Comment = Tokens[m_Tokens.f_FindToken(iCommentIndent)];
							bool bCommentLine
								= (Comment.m_Kind == ECodeTokenKind::mc_LineComment || Comment.m_Kind == ECodeTokenKind::mc_BlockComment)
								&& Comment.m_iOffset == iCommentIndent
								&& !Comment.m_bMultiLine
							;
							if (bCommentLine)
							{
								fPlaceLine
									(
										m_Lines.f_GetLineStart(iCommentLine)
										, iCommentIndent
										, umint(nHeadColumn)
										, "string-continuation"
										, "a comment in front of a string a value starts on a line of its own stands with it"
									)
								;
							}
						}
					}
				}
			}

			auto nPlaced = umint(fg_Max(nHeadColumn, aint(0))) + (bStartsStatement ? nTab : 0);
			PlacedStringLines[iLine] = nPlaced;
			fPlaceLine
				(
					m_Lines.f_GetLineStart(iLine)
					, Tokens[i].m_iOffset
					, nPlaced
					, "string-continuation"
					, bStartsStatement ? "a string continued on the next line stands one level in" : "a string continued on the next line stands at its element's level"
				)
			;

			// The operators continuing a DSL key's value behind its continued strings stand at the
			// key's level with them: '"Key"_o= "a"' over '"b"' over '+ Warning'.
			auto iNext = fp_NextCode(i);
			if (iNext < 0 || fp_ContinuesLiteral(umint(iNext)) >= 0)
				continue;

			auto iLead = fp_FindContinuedStringLead(i);
			if (iLead < 0 || !m_Tokens.f_IsText(Tokens[umint(iLead)], "=") || !fp_IsKeyAssign(umint(iLead)))
				continue;

			auto Depth = m_TokenDepth[i];
			for (auto iOperand = iNext; iOperand >= 0; iOperand = fp_NextCode(umint(iOperand)))
			{
				auto const &Operand = Tokens[umint(iOperand)];
				auto OperandDepth = m_TokenDepth[umint(iOperand)];
				if (OperandDepth < Depth)
					break;

				if (OperandDepth == Depth && (m_Tokens.f_IsText(Operand, ",") || m_Tokens.f_IsText(Operand, ";")))
					break;

				if (OperandDepth != Depth || !fp_IsFirstOnLine(umint(iOperand)))
					continue;

				// One the layout breaks in front of rather than the source is placed by that break.
				auto iWritten = fp_PreviousSignificant(umint(iOperand));
				if (iWritten >= 0 && !Tokens[umint(iWritten)].m_bMultiLine)
					continue;

				auto iOperandLine = m_Lines.f_FindLine(Operand.m_iOffset);
				if (m_ProtectedStart[iOperandLine])
					continue;

				fPlaceLine
					(
						m_Lines.f_GetLineStart(iOperandLine)
						, Operand.m_iOffset
						, umint(fg_Max(nHeadColumn, aint(0)))
						, "string-continuation"
						, "an operator continuing a key's strings stands at the key's level"
					)
				;
			}
		}
	}

	// Whether each line of a block comment behind its first can move by the columns its first moves: the indentation
	// a line keeps relative to the comment is its text's.
	bool CFormattingAnalyzer::fp_CanShiftCommentLines(umint _iComment, aint _nDelta) const
	{
		auto const &Token = m_Tokens.f_GetTokens()[_iComment];
		if (Token.m_Kind != ECodeTokenKind::mc_BlockComment || !Token.m_bMultiLine || _nDelta >= 0)
			return true;

		for (auto iLine = m_Lines.f_FindLine(Token.m_iOffset) + 1; iLine < m_Lines.f_GetLineCount() && m_Lines.f_GetLineStart(iLine) < Token.f_GetEnd(); ++iLine)
		{
			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto iContent = iStart;
			while (iContent < Token.f_GetEnd() && fg_IsSpaceOrTab(m_pSource[iContent]))
				++iContent;

			auto const *pContent = iContent < Token.f_GetEnd() ? m_pSource + iContent : nullptr;
			if (!pContent || *pContent == '\n' || *pContent == '\r')
				continue;

			umint nColumns = 0;
			if (!fg_MeasureTextColumns(m_pSource + iStart, iContent - iStart, m_Request.m_Settings.m_nTabWidth, nColumns) || aint(nColumns) + _nDelta < 0)
				return false;
		}

		return true;
	}

	// Moves each line of a block comment behind its first by the columns its first moves.
	void CFormattingAnalyzer::fp_ShiftCommentLines(umint _iComment, aint _nDelta, ch8 const *_pRule, ch8 const *_pExplanation)
	{
		auto const &Token = m_Tokens.f_GetTokens()[_iComment];
		if (Token.m_Kind != ECodeTokenKind::mc_BlockComment || !Token.m_bMultiLine || !_nDelta)
			return;

		for (auto iLine = m_Lines.f_FindLine(Token.m_iOffset) + 1; iLine < m_Lines.f_GetLineCount() && m_Lines.f_GetLineStart(iLine) < Token.f_GetEnd(); ++iLine)
		{
			auto iStart = m_Lines.f_GetLineStart(iLine);
			auto iContent = iStart;
			while (iContent < Token.f_GetEnd() && fg_IsSpaceOrTab(m_pSource[iContent]))
				++iContent;

			auto const *pContent = iContent < Token.f_GetEnd() ? m_pSource + iContent : nullptr;
			if (!pContent || *pContent == '\n' || *pContent == '\r')
				continue;

			umint nColumns = 0;
			if (!fg_MeasureTextColumns(m_pSource + iStart, iContent - iStart, m_Request.m_Settings.m_nTabWidth, nColumns))
				continue;

			fp_AddEdit(_pRule, iStart, iContent - iStart, fp_MakeIndent(umint(fg_Max(aint(nColumns) + _nDelta, aint(0)))), _pExplanation);
		}
	}

	// A block comment over several lines trailing code becomes one line comment per line of its text, the
	// documenting marker kept: '/**< a' over 'b */' becomes '///< a' over '///< b', the continuation lines
	// aligned under the first by the layout.
	bool CFormattingAnalyzer::fp_MultiLineTrailingCommentLines(umint _iComment, CStr &o_Replacement) const
	{
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto const &LineEnding = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());
		auto const &Comment = Tokens[_iComment];
		if (Comment.m_Kind != ECodeTokenKind::mc_BlockComment || !Comment.m_bMultiLine || fp_IsFirstOnLine(_iComment))
			return false;

		// A comma a list's conversion takes out from behind it leaves it ending its line.
		auto iEnd = _iComment + 1;
		while (iEnd < Tokens.f_GetLen() && (Tokens[iEnd].m_Kind == ECodeTokenKind::mc_Whitespace || (iEnd < m_CommaGone.f_GetLen() && m_CommaGone[iEnd])))
			++iEnd;

		auto iBefore = fp_PreviousSignificant(_iComment);
		auto Text = m_Tokens.f_GetText(Comment);
		auto iCommentLine = m_Lines.f_FindLine(Comment.m_iOffset);
		bool bSameLine = iBefore >= 0;
		for (auto iGap = umint(iBefore) + 1; bSameLine && iGap < _iComment; ++iGap)
			bSameLine = Tokens[iGap].m_Kind != ECodeTokenKind::mc_Newline;

		bool bConvertible
			= (iEnd >= Tokens.f_GetLen() || Tokens[iEnd].m_Kind == ECodeTokenKind::mc_Newline)
			&& bSameLine
			&& Tokens[umint(iBefore)].m_Kind != ECodeTokenKind::mc_Preprocessor
			&& !m_ProtectedStart[iCommentLine]
			&& Text.f_Find("malterlib-format") < 0
			&& Text.f_Find("NOLINT") < 0
			&& !fp_IsDisabled(Comment.m_iOffset, Comment.m_nLength)
			&& fp_IsSelected(Comment.m_iOffset, Comment.m_nLength)
		;
		if (!bConvertible)
			return false;

		CStr Marker = "//";
		umint nOpen = 2;
		if (Text.f_StartsWith("/**<") || Text.f_StartsWith("/*!<"))
		{
			Marker = Text.f_StartsWith("/**<") ? "///<" : "//!<";
			nOpen = 4;
		}
		else if (Text.f_StartsWith("/***"))
			return false;
		else if (Text.f_StartsWith("/**") || Text.f_StartsWith("/*!"))
		{
			Marker = Text.f_StartsWith("/**") ? "///" : "//!";
			nOpen = 3;
		}

		auto Inner = Text.f_Extract(nOpen, Text.f_GetLen() - nOpen - 2);
		TCVector<CStr> Lines;
		bool bSplice = false;
		bool bStructured = false;
		bool bBlankAfterText = false;
		umint nContinuationDepth = TCLimitsInt<umint>::mc_Max;
		bool bOpeningLine = true;
		while (true)
		{
			// Lines end in LF, CRLF or a lone CR alike.
			aint iNewline = -1;
			auto pInner = Inner.f_GetStr();
			for (umint i = 0; i < Inner.f_GetLen() && iNewline < 0; ++i)
			{
				if (pInner[i] == '\n' || pInner[i] == '\r')
					iNewline = aint(i);
			}

			if (iNewline >= 0 && pInner[iNewline] == '\r' && umint(iNewline) + 1 < Inner.f_GetLen() && pInner[iNewline + 1] == '\n')
				Inner = Inner.f_Left(iNewline) + Inner.f_Extract(iNewline + 1);

			// A '*' starting a line after the first is the comment's margin; on the first it is text: '/* *p is the result'.
			auto Raw = iNewline >= 0 ? Inner.f_Left(iNewline) : Inner;
			auto Line = Raw.f_Trim();
			if (bOpeningLine)
				bOpeningLine = false;
			else
			{
				auto Content = Raw;
				if (Line.f_StartsWith("*"))
				{
					Content = Line.f_Extract(1);
					Line = Content.f_Trim();
				}

				// The lines after the first are written at one depth, behind a margin or without one: a list nested deeper or
				// an indented example has a structure line comments at one depth would lose.
				if (!Line.f_IsEmpty() && !Lines.f_IsEmpty())
				{
					auto nDepth = Content.f_GetLen() - Content.f_TrimLeft().f_GetLen();
					if (nContinuationDepth == TCLimitsInt<umint>::mc_Max)
						nContinuationDepth = nDepth;
					else if (nDepth != nContinuationDepth)
						bStructured = true;
				}
			}

			bSplice |= Line.f_EndsWith("\\") || Line.f_EndsWith("?" "?/");

			// A blank line between two parts the comment holds parts paragraphs.
			if (Line.f_IsEmpty() && !Lines.f_IsEmpty())
				bBlankAfterText = true;
			else if (!Line.f_IsEmpty())
			{
				bStructured = bStructured || bBlankAfterText;
				Lines.f_Insert(Line);
			}

			if (iNewline < 0)
				break;

			Inner = Inner.f_Extract(iNewline + 1);
		}

		if (Lines.f_IsEmpty() || bSplice || bStructured)
			return false;

		// The first line has to fit behind its code, which a line comment cannot leave.
		auto iLineStart = m_Lines.f_GetLineStart(iCommentLine);
		CTrailedCode Code;
		umint nCodeColumns = 0;
		if (fp_MeasureTrailedCode(_iComment, Code) && Code.m_bMeasured)
		{
			nCodeColumns = Code.m_nColumns;

			// Behind a block's opening brace ending the code the comment comes to trail the brace on its own line.
			umint nBraceLine = 0;
			if (fp_MeasureOnOpeningBraceLine(_iComment, Code, nBraceLine))
				nCodeColumns = nBraceLine;
			else if (nMaxColumns && nCodeColumns > nMaxColumns && m_Tokens.f_IsText(Tokens[umint(iBefore)], ";"))
			{
				// A statement too long for its line is split, its terminator taking a line of its own, which the comment
				// then trails.
				nCodeColumns = fp_GetSourceLineIndent(Code.m_iFirst) + 1;
			}
		}
		else if (!fg_MeasureTextColumns(m_pSource + iLineStart, Tokens[umint(iBefore)].f_GetEnd() - iLineStart, nTab, nCodeColumns))
			return false;

		if (nMaxColumns && nCodeColumns + 1 + Marker.f_GetLen() + 1 + Lines[0].f_GetLen() > nMaxColumns)
			return false;

		auto const &First = Tokens[m_Tokens.f_FindToken(iLineStart)];
		auto nIndent = First.m_Kind == ECodeTokenKind::mc_Whitespace ? First.m_nLength : 0;
		CStr Replacement;
		{
			CStr::CAppender Appender(Replacement);
			for (umint iText = 0; iText < Lines.f_GetLen(); ++iText)
			{
				if (iText)
				{
					Appender += LineEnding;
					Appender.f_AddString(m_pSource + iLineStart, nIndent);
					Appender += "\t";
				}

				Appender += Marker;
				Appender += " ";
				Appender += Lines[iText];
			}
		}

		o_Replacement = fg_Move(Replacement);

		return true;
	}

	// The columns up to a comment trailing a block's opening brace that ends the code on its line, as the brace's own
	// line the layout gives it writes them; false where the code does not end in such a brace.
	bool CFormattingAnalyzer::fp_MeasureOnOpeningBraceLine(umint _iComment, CTrailedCode const &_Code, umint &o_nColumns) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		auto iLastCode = fp_PreviousCode(_iComment);
		auto iBefore = fp_PreviousSignificant(_iComment);
		if (!m_Structure.f_IsComplete() || !_Code.m_bMeasured || iLastCode < 0 || iBefore < 0 || umint(iLastCode) <= _Code.m_iFirst)
			return false;

		auto iOpened = m_Tokens.f_IsText(Tokens[umint(iLastCode)], "{") ? m_Structure.f_FindNodeOpeningAt(umint(iLastCode)) : Nodes.f_GetLen();
		if (iOpened >= Nodes.f_GetLen() || Nodes[iOpened].m_Kind != ECodeNodeKind::mc_Block)
			return false;

		umint nLine = 0;
		umint nBrace = 0;
		if (!fp_MeasureJoinedWidth(_Code.m_iFirst, umint(iBefore), nLine, true) || !fp_MeasureJoinedWidth(umint(iLastCode), umint(iBefore), nBrace, true) || _Code.m_nColumns < nLine)
			return false;

		o_nColumns = _Code.m_nColumns - nLine + nBrace;

		return true;
	}

	// Measures the code in front of a comment trailing it, as the layout writes it: the line's first token, whether
	// that starts a statement, and its columns. Gives false for a line that starts with the comment or a directive.
	bool CFormattingAnalyzer::fp_MeasureTrailedCode(umint _iComment, CTrailedCode &o_Code) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nTab = m_Request.m_Settings.m_nTabWidth;

		auto fIsLineLead = [&](umint _iToken)
			{
				auto Kind = Tokens[_iToken].m_Kind;

				return Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_ByteOrderMark;
			}
		;

		auto iLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Tokens[_iComment].m_iOffset));
		auto iFirst = m_Tokens.f_FindToken(iLineStart);
		while (iFirst < _iComment && fIsLineLead(iFirst))
			++iFirst;

		auto iBefore = fp_PreviousSignificant(_iComment);
		if (iFirst >= _iComment || iBefore < 0 || Tokens[iFirst].m_Kind == ECodeTokenKind::mc_Preprocessor || Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_Preprocessor)
			return false;

		if (umint(iBefore) < m_CommaGone.f_GetLen() && m_CommaGone[umint(iBefore)] && fp_PreviousCode(umint(iBefore)) >= aint(iFirst))
			iBefore = fp_PreviousCode(umint(iBefore));

		// A statement behind a block's opening brace on its line takes a line of its own, and is measured there.
		o_Code.m_bBehindBrace = false;
		auto const &Nodes = m_Structure.f_GetNodes();
		// A brace behind a head on the line, 'namespace N { int f();', counts where its block goes on past the comment, as
		// the head and the statement both take lines of their own.
		auto iBrace = iFirst;
		auto iBlock = m_Tokens.f_IsText(Tokens[iFirst], "{") ? m_Structure.f_FindNodeOpeningAt(iFirst) : Nodes.f_GetLen();
		for (auto iToken = iBefore; iBlock >= Nodes.f_GetLen() && iToken > aint(iFirst); iToken = fp_PreviousCode(umint(iToken)))
		{
			if (!m_Tokens.f_IsText(Tokens[umint(iToken)], "{"))
				continue;

			// An initializer's brace is no block's, and the block's may stand in front of it: '{ int a[] = {1};'.
			auto iOpened = m_Structure.f_FindNodeOpeningAt(umint(iToken));
			if (iOpened >= Nodes.f_GetLen() || Nodes[iOpened].m_Kind != ECodeNodeKind::mc_Block)
				continue;

			if (Nodes[iOpened].m_iLastToken > umint(iBefore))
			{
				iBlock = iOpened;
				iBrace = umint(iToken);
			}

			break;
		}

		if (iBlock < Nodes.f_GetLen() && Nodes[iBlock].m_Kind == ECodeNodeKind::mc_Block)
		{
			auto iStatement = fp_NextCode(iBrace);
			bool bStatement = false;
			for (auto iChild : Nodes[iBlock].m_Children)
			{
				auto const &Child = Nodes[iChild];
				bStatement |= iStatement >= 0 && Child.m_Kind == ECodeNodeKind::mc_Statement && Child.m_iFirstToken == umint(iStatement);
			}

			if (bStatement && umint(iStatement) <= umint(iBefore))
			{
				o_Code.m_iBehindOf = iFirst;
				iFirst = umint(iStatement);
				o_Code.m_bBehindBrace = true;
			}
		}

		// So does one a clause or an 'else' guards on its line, one level in: 'else' over 'return;'.
		if (!o_Code.m_bBehindBrace)
		{
			aint iGuarded = -1;
			auto const &First = Tokens[iFirst];
			bool bElse = m_Tokens.f_IsText(First, "else");
			if (bElse)
				iGuarded = fp_NextCode(iFirst);
			else if (m_Tokens.f_IsText(First, "if") || m_Tokens.f_IsText(First, "for") || m_Tokens.f_IsText(First, "while"))
			{
				auto iOpen = fp_NextCode(iFirst);
				if (iOpen >= 0 && m_Tokens.f_IsText(Tokens[umint(iOpen)], "constexpr"))
					iOpen = fp_NextCode(umint(iOpen));

				auto iCondition = iOpen >= 0 && m_Tokens.f_IsText(Tokens[umint(iOpen)], "(") ? m_Structure.f_FindNodeOpeningAt(umint(iOpen)) : Nodes.f_GetLen();
				if (iCondition < Nodes.f_GetLen())
					iGuarded = fp_NextCode(Nodes[iCondition].m_iLastToken);
			}

			auto const *pGuarded = iGuarded >= 0 ? &Tokens[umint(iGuarded)] : nullptr;
			bool bSplits
				= pGuarded
				&& umint(iGuarded) <= umint(iBefore)
				&& !m_Tokens.f_IsText(*pGuarded, "{")
				&& !m_Tokens.f_IsText(*pGuarded, ";")
				&& !(bElse && m_Tokens.f_IsText(*pGuarded, "if"))
				&& !fp_SpansLines(iFirst, umint(iGuarded))
			;
			if (bSplits)
			{
				o_Code.m_iBehindOf = iFirst;
				iFirst = umint(iGuarded);
				o_Code.m_bBehindBrace = true;
			}
		}

		// A statement block's closing brace takes a line of its own, with the terminators behind it, and is measured there
		// at the block's depth. Without a complete structure nothing is laid out and the brace stays.
		umint iClosingBlock = Nodes.f_GetLen();
		for (auto iBrace = m_Structure.f_IsComplete() ? iBefore : aint(-1); iBrace > aint(iFirst) && iClosingBlock == Nodes.f_GetLen(); iBrace = fp_PreviousCode(umint(iBrace)))
		{
			if (!m_Tokens.f_IsText(Tokens[umint(iBrace)], "}"))
			{
				if (!m_Tokens.f_IsText(Tokens[umint(iBrace)], ";"))
					break;

				continue;
			}

			auto iBraceBlock = m_Structure.f_FindNodeClosingAt(umint(iBrace));
			auto const *pBraceBlock = iBraceBlock < Nodes.f_GetLen() ? &Nodes[iBraceBlock] : nullptr;
			if (!pBraceBlock || pBraceBlock->m_Kind != ECodeNodeKind::mc_Block || pBraceBlock->m_iFirstToken < iFirst)
				break;

			bool bStatementLevel = true;
			for (auto iNode = pBraceBlock->m_iParent; bStatementLevel && iNode && iNode < Nodes.f_GetLen(); iNode = Nodes[iNode].m_iParent)
			{
				if (Nodes[iNode].m_Kind == ECodeNodeKind::mc_Block)
					break;

				bStatementLevel = Nodes[iNode].m_Kind != ECodeNodeKind::mc_Group;
			}

			if (!bStatementLevel)
				break;

			iClosingBlock = iBraceBlock;
			iFirst = umint(iBrace);
		}

		// Only a line that starts a statement is one the comment can go above. A line the layout split off
		// one, a lone ';', a closing marker or an operand, is placed again in each pass, and moving its comment
		// would undo where the previous pass left it. A block's opening brace is no statement either: the comment
		// would part it from its head; nor is an 'else' or a 'catch', which goes on with the statement in front.
		// An Objective-C header is written on one line, unless a comment inside it keeps a list in it on its lines. One the
		// layout joins is measured from its start; in one kept on its lines no comment moves, which would join it.
		bool bHeaderKeepsLines = false;
		auto iInnermost = m_Structure.f_FindStatementEndingAt(umint(iBefore));
		if (iInnermost >= Nodes.f_GetLen())
			iInnermost = m_Structure.f_FindEnclosingNode(umint(iBefore));

		for (auto iNode = iInnermost; iNode && iNode < Nodes.f_GetLen(); iNode = Nodes[iNode].m_iParent)
		{
			auto const &Header = Nodes[iNode];

			// A method's signature is joined from its start up to a comment inside it: '-(void)m3:(int)a' over 'ATTR(1) // c'.
			if (Header.m_bObjectiveCMethod && Header.m_iFirstToken < iFirst && fp_SpansLines(Header.m_iFirstToken, iFirst))
			{
				bool bJoined = true;
				for (auto iToken = Header.m_iFirstToken; bJoined && iToken < iFirst; ++iToken)
				{
					auto Kind = Tokens[iToken].m_Kind;
					bJoined = Kind != ECodeTokenKind::mc_LineComment && Kind != ECodeTokenKind::mc_BlockComment && Kind != ECodeTokenKind::mc_Preprocessor;
				}

				if (bJoined)
					iFirst = Header.m_iFirstToken;

				break;
			}

			if (!Header.m_bObjectiveCHeader)
				continue;

			bool bLines = false;
			bool bComment = false;
			for (auto iToken = Header.m_iFirstToken; iToken < Header.m_iLastToken; ++iToken)
			{
				auto Kind = Tokens[iToken].m_Kind;
				bLines |= Kind == ECodeTokenKind::mc_Newline;
				bComment |= Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
			}

			if (bLines && bComment)
				bHeaderKeepsLines = true;
			else if (bLines)
				iFirst = Header.m_iFirstToken;

			break;
		}

		// A clause written over lines is joined onto its first line where nothing between them keeps them apart, so a comment
		// behind its body's brace or the empty statement it guards trails the clause from its start: 'if (a &&' over
		// 'b) { // Text'. A statement it guards on its last line takes a line of its own one level in instead.
		if (!bHeaderKeepsLines)
		{
			auto iStatement = m_Structure.f_FindEnclosingNode(iFirst);
			while (iStatement && iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement)
				iStatement = Nodes[iStatement].m_iParent;

			// Only one whose body's brace, or empty statement, the comment trails comes onto one line with it: one the comment
			// trails the end of the layout splits to make room for the comment.
			bool bValid = iStatement && iStatement < Nodes.f_GetLen();
			auto iStatementFirst = bValid ? Nodes[iStatement].m_iFirstToken : iFirst;
			auto iStatementLast = bValid ? aint(Nodes[iStatement].m_iLastToken) : aint(-1);
			auto const &Before = Tokens[umint(iBefore)];
			bool bEnds = bValid && (m_Tokens.f_IsText(Before, "{") || m_Tokens.f_IsText(Before, ";")) && fp_PreviousCode(umint(iBefore)) == iStatementLast && fp_IsGuard(iStatement);
			bool bContinues = bEnds && iStatementFirst < iFirst && fp_IsFirstOnLine(iStatementFirst) && fp_SpansLines(iStatementFirst, iFirst);
			for (auto iToken = iStatementFirst; bContinues && iToken < iFirst; ++iToken)
			{
				auto Kind = Tokens[iToken].m_Kind;
				bContinues = Kind != ECodeTokenKind::mc_LineComment && Kind != ECodeTokenKind::mc_BlockComment && Kind != ECodeTokenKind::mc_Preprocessor;
			}

			if (bContinues && fp_IsRangeJoinable(iStatement, iStatementFirst, umint(iBefore)))
				iFirst = iStatementFirst;

			auto iGuarded = m_Structure.f_FindStatementEndingAt(umint(iBefore));
			auto const *pGuarded = iGuarded < Nodes.f_GetLen() ? &Nodes[iGuarded] : nullptr;
			auto const *pGuardedFirst = pGuarded ? &Tokens[pGuarded->m_iFirstToken] : nullptr;
			bool bGuarded
				= bValid
				&& !bEnds
				&& pGuarded
				&& fp_IsGuard(iStatement)
				&& fp_PreviousCode(pGuarded->m_iFirstToken) == iStatementLast
				&& pGuarded->m_iFirstToken > iFirst
				&& !m_Tokens.f_IsText(*pGuardedFirst, "{")
				&& !m_Tokens.f_IsText(*pGuardedFirst, ";")
				&& iStatementFirst < iFirst
				&& fp_IsFirstOnLine(iStatementFirst)
			;
			for (auto iToken = iStatementFirst; bGuarded && iToken < iFirst; ++iToken)
			{
				auto Kind = Tokens[iToken].m_Kind;
				bGuarded = Kind != ECodeTokenKind::mc_LineComment && Kind != ECodeTokenKind::mc_BlockComment && Kind != ECodeTokenKind::mc_Preprocessor;
			}

			if (bGuarded && fp_IsRangeJoinable(iStatement, iStatementFirst, umint(iStatementLast)))
			{
				o_Code.m_iBehindOf = iStatementFirst;
				iFirst = pGuarded->m_iFirstToken;
				o_Code.m_bBehindBrace = true;
			}
		}

		// So does one alone behind a case label on its line that falls through to the next label, one level in.
		auto const &LineFirst = Tokens[iFirst];
		if (!o_Code.m_bBehindBrace && m_Structure.f_IsComplete() && (m_Tokens.f_IsText(LineFirst, "case") || m_Tokens.f_IsText(LineFirst, "default")))
		{
			auto iLabelBlock = m_Structure.f_FindEnclosingNode(iFirst);

			auto fIsLabel = [&](umint _iChild)
				{
					auto const &Child = Nodes[_iChild];
					auto const &ChildFirst = Tokens[Child.m_iFirstToken];

					return (m_Tokens.f_IsText(ChildFirst, "case") || m_Tokens.f_IsText(ChildFirst, "default")) && m_Tokens.f_IsText(Tokens[Child.m_iLastToken], ":");
				}
			;

			umint iLabelIndex = TCLimitsInt<umint>::mc_Max;
			auto const *pChildren = iLabelBlock < Nodes.f_GetLen() ? &Nodes[iLabelBlock].m_Children : nullptr;
			for (umint i = 0; pChildren && i < pChildren->f_GetLen() && iLabelIndex == TCLimitsInt<umint>::mc_Max; ++i)
			{
				if (Nodes[(*pChildren)[i]].m_iFirstToken == iFirst && fIsLabel((*pChildren)[i]))
					iLabelIndex = i;
			}

			if (iLabelIndex != TCLimitsInt<umint>::mc_Max && iLabelIndex + 1 < pChildren->f_GetLen())
			{
				auto const &Statement = Nodes[(*pChildren)[iLabelIndex + 1]];
				umint nOnLine = 0;
				bool bBlockOnLine = false;
				umint iLast = iLabelIndex + 1;
				for (auto iOther = iLabelIndex + 1; iOther < pChildren->f_GetLen(); ++iOther)
				{
					auto const &Other = Nodes[(*pChildren)[iOther]];
					if (fp_SpansLines(iFirst, Other.m_iFirstToken) || fIsLabel((*pChildren)[iOther]))
						break;

					nOnLine += !(iOther > iLabelIndex + 1 && m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "break"));
					bBlockOnLine |= Other.m_Kind == ECodeNodeKind::mc_Block || m_Tokens.f_IsText(Tokens[Other.m_iFirstToken], "{");
					iLast = iOther;
				}

				bool bFallsThrough = false;
				if (iLast + 1 < pChildren->f_GetLen() && fIsLabel((*pChildren)[iLast + 1]))
				{
					auto const &LastFirst = Tokens[Nodes[(*pChildren)[iLast]].m_iFirstToken];
					bFallsThrough = !m_Tokens.f_HasRole(LastFirst, ECodeNameRole::mc_NoReturnMacro);
					for (auto pJump : {"break", "return", "co_return", "continue", "throw", "goto"})
						bFallsThrough = bFallsThrough && !m_Tokens.f_IsText(LastFirst, pJump);
				}

				// Only a statement alone on the line, which a fall-through moves, is the one the comment trails there.
				bool bMoves = nOnLine == 1 && !bBlockOnLine && bFallsThrough;
				if (bMoves && Statement.m_iFirstToken <= umint(iBefore) && !fp_SpansLines(iFirst, Statement.m_iFirstToken))
				{
					o_Code.m_iBehindOf = iFirst;
					iFirst = Statement.m_iFirstToken;
					o_Code.m_bBehindBrace = true;
				}
			}
		}

		auto const &First = Tokens[iFirst];
		bool bGuarded = false;
		bool bStartsStatement
			= !m_Tokens.f_IsText(First, "}")
			&& !m_Tokens.f_IsText(First, ")")
			&& !m_Tokens.f_IsText(First, "]")
			&& !m_Tokens.f_IsText(First, "{")
			&& !m_Tokens.f_IsText(First, "else")
			&& !m_Tokens.f_IsText(First, "catch")
		;
		if (auto iPrevious = fp_PreviousCode(iFirst); iPrevious >= 0 && bStartsStatement)
		{
			auto const &Previous = Tokens[umint(iPrevious)];
			// A braced initializer's '}' ends no statement, nor does a lambda body's that an expression goes on behind:
			// the line behind it is one the layout splits off.
			auto const &Nodes = m_Structure.f_GetNodes();
			// The 'while' behind a 'do' block continues the statement.
			bool bClosesBlock = m_Tokens.f_IsText(Previous, "}") && m_Structure.f_FindStatementEndingAt(umint(iPrevious)) < Nodes.f_GetLen();
			if (auto iBlock = bClosesBlock ? m_Structure.f_FindNodeClosingAt(umint(iPrevious)) : Nodes.f_GetLen(); iBlock < Nodes.f_GetLen())
			{
				auto iHead = fp_PreviousCode(Nodes[iBlock].m_iFirstToken);
				bClosesBlock = iHead < 0 || !m_Tokens.f_IsText(Tokens[umint(iHead)], "do");
			}

			bGuarded = m_Tokens.f_IsText(Previous, "else") || m_Tokens.f_IsText(Previous, "do");
			if (m_Tokens.f_IsText(Previous, ")"))
			{
				auto iGroup = m_Structure.f_FindNodeClosingAt(umint(iPrevious));
				auto iClause = iGroup < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iGroup].m_iFirstToken) : aint(-1);
				for (auto pClause : {"if", "for", "while", "switch", "catch"})
					bGuarded |= iClause >= 0 && m_Tokens.f_IsText(Tokens[umint(iClause)], pClause);
			}

			bStartsStatement = m_Tokens.f_IsText(Previous, ";") || m_Tokens.f_IsText(Previous, "{") || bClosesBlock || m_Tokens.f_IsText(Previous, ":") || bGuarded;
		}

		// Code too long for the line by itself is split by the layout, which leaves the comment
		// room behind it. The line is measured as the spacing rules write it, the comment one space
		// behind the code, so that columns its '=' or comment are aligned at never move the comment.
		umint nCodeColumns = 0;
		umint nJoined = 0;
		bool bMeasured = fp_MeasureJoinedWidth(iFirst, umint(iBefore), nJoined);

		// Block comments naming the code behind them on the line are as wide as the source writes them, one space apart.
		if (!bMeasured && Tokens[iFirst].m_Kind == ECodeTokenKind::mc_BlockComment)
		{
			umint nNames = 0;
			auto iCode = iFirst;
			bool bNames = true;
			while (bNames && iCode <= umint(iBefore) && !fg_IsCodeToken(Tokens[iCode]))
			{
				auto const &Token = Tokens[iCode];
				if (Token.m_Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine)
					nNames += fp_GetTokenColumns(Token) + 1;
				else
					bNames = Token.m_Kind == ECodeTokenKind::mc_Whitespace;

				++iCode;
			}

			if (bNames && iCode <= umint(iBefore) && fp_MeasureJoinedWidth(iCode, umint(iBefore), nJoined))
			{
				nJoined += nNames;
				bMeasured = true;
			}
		}

		// The layout places a statement one level in per block around it, so it is measured at that depth, wherever
		// the source wrote it: the decision is then the same in every pass. A lambda's body stands in from the groups
		// it is an argument of, the outermost opened one level in from its statement, and a statement a clause guards
		// without braces one level in from the clause.
		umint nIndent = fp_GetSourceLineIndent(iFirst);
		if (bStartsStatement || iClosingBlock < Nodes.f_GetLen())
		{
			umint nLevels = bGuarded ? 1 : 0;
			umint nGroups = 0;

			// A statement a clause guards without braces stands a level in from the clause, each of a chain of clauses
			// one more, the 'if' of an 'else if' on the line of its 'else'. The guarded statement is the clause's sibling.
			auto fGuards = [&](umint _iBlock) -> umint
				{
					auto const &Children = Nodes[_iBlock].m_Children;
					umint iLow = 0;
					umint iHigh = Children.f_GetLen();
					while (iLow < iHigh)
					{
						auto iMiddle = (iLow + iHigh) / 2;
						if (Nodes[Children[iMiddle]].m_iFirstToken <= iFirst)
							iLow = iMiddle + 1;
						else
							iHigh = iMiddle;
					}

					if (!iLow || Nodes[Children[iLow - 1]].m_Kind != ECodeNodeKind::mc_Statement)
						return 0;

					auto fElseIf = [&](umint _iIf)
						{
							return _iIf >= 1
								&& m_Tokens.f_IsText(Tokens[Nodes[Children[_iIf]].m_iFirstToken], "if")
								&& m_Tokens.f_IsText(Tokens[Nodes[Children[_iIf - 1]].m_iFirstToken], "else")
							;
						}
					;

					// A braced body is a statement too, which stands at its clause's level, and the clauses in front of that
					// clause count from there.
					umint iCounted = iLow - 1;
					if (m_Tokens.f_IsText(Tokens[Nodes[Children[iCounted]].m_iFirstToken], "{"))
					{
						if (!iCounted || !fp_IsGuard(Children[iCounted - 1]))
							return 0;

						--iCounted;
						if (fElseIf(iCounted))
							--iCounted;
					}

					umint nGuards = 0;
					for (auto iSibling = iCounted; iSibling && fp_IsGuard(Children[iSibling - 1]); --iSibling)
					{
						++nGuards;
						if (fElseIf(iSibling - 1))
							--iSibling;
					}

					// The statement starting the line has its own guard counted already.
					if (nGuards && bGuarded && Nodes[Children[iLow - 1]].m_iFirstToken == iFirst)
						--nGuards;

					return nGuards;
				}
			;

			auto iInnermost = iClosingBlock < Nodes.f_GetLen() ? Nodes[iClosingBlock].m_iParent : m_Structure.f_FindEnclosingNode(iFirst);
			for (auto iNode = iInnermost; iNode && iNode < Nodes.f_GetLen(); iNode = Nodes[iNode].m_iParent)
			{
				auto Kind = Nodes[iNode].m_Kind;
				if (Kind == ECodeNodeKind::mc_Group)
					++nGroups;
				else if (Kind == ECodeNodeKind::mc_Block)
				{
					nLevels += 1 + (nGroups ? nGroups + 1 : 0) + fGuards(iNode);
					nGroups = 0;
				}
			}

			nLevels += nGroups ? nGroups + 1 : 0;

			// A case label stands at its switch block's braces, a level out from the statements in it.
			if (nLevels && (m_Tokens.f_IsText(First, "case") || m_Tokens.f_IsText(First, "default")))
				--nLevels;

			// Out of scope by decision: the continuation a lambda assigned to a variable sets its body in is not counted, so a
			// comment there that only fits above its line is left behind it and reported as too long.
			nIndent = nLevels * nTab;
		}

		// Code a block comment keeps from being joined is as wide as the spacing rules write it around the comment, and
		// code a line break the standard does not settle keeps from being joined as wide as the source writes it from its
		// first token, at the depth the layout gives the line. A guarded statement written on its guard's line is measured
		// with the guard, so one the layout moves to a line of its own exactly at the limit has its comment moved by the
		// next pass, which the repeated analysis composes into the plan.
		if (!bMeasured && fp_MeasureJoinedWidth(iFirst, umint(iBefore), nJoined, true))
			bMeasured = true;

		if (bMeasured)
			nCodeColumns = nIndent + nJoined;
		else
		{
			bMeasured = fg_MeasureTextColumns(m_pSource + First.m_iOffset, Tokens[umint(iBefore)].f_GetEnd() - First.m_iOffset, nTab, nCodeColumns);
			nCodeColumns += nIndent;
			for (auto i = iFirst; bMeasured && i < umint(iBefore); ++i)
				bMeasured = Tokens[i].m_Kind != ECodeTokenKind::mc_Newline;
		}

		o_Code.m_iFirst = iFirst;
		o_Code.m_nColumns = nCodeColumns;
		o_Code.m_bStartsStatement = bStartsStatement && !bHeaderKeepsLines;
		o_Code.m_bMeasured = bMeasured;

		return true;
	}

	// A block comment ending a line behind its code is written as a line comment: 'f_Call(); /* Text */'
	// becomes 'f_Call(); // Text'. A comment trailing a line it makes longer than the limit moves to a
	// line of its own above that line, at its indentation: '// Text' over 'f_Call();'. A comment
	// documenting what it trails, '///<', stays where it documents it.
	void CFormattingAnalyzer::fp_ConvertTrailingComments()
	{
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto const &LineEnding = fg_GetTextLineEndingBytes(fp_GetDefaultLineEnding());

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Comment = Tokens[i];
			bool bBlock = Comment.m_Kind == ECodeTokenKind::mc_BlockComment;

			if (bBlock && Comment.m_bMultiLine && !fp_IsFirstOnLine(i))
			{
				CStr Replacement;
				if (!fp_MultiLineTrailingCommentLines(i, Replacement))
					continue;

				auto &Replace = m_Structural.f_Insert();
				Replace.m_iOffset = Comment.m_iOffset;
				Replace.m_nLength = Comment.m_nLength;
				Replace.m_Replacement = Replacement;
				Replace.m_Rule = "trailing-comment";

				continue;
			}

			if ((Comment.m_Kind != ECodeTokenKind::mc_LineComment && !bBlock) || Comment.m_bMultiLine || fp_IsFirstOnLine(i))
				continue;

			// A comma behind the comment that a list's conversion takes out leaves the comment ending its line.
			auto iAfter = i + 1;
			while (iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Whitespace)
				++iAfter;

			bool bCommaGone = iAfter < m_CommaGone.f_GetLen() && m_CommaGone[iAfter];
			if (bCommaGone)
			{
				++iAfter;
				while (iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Whitespace)
					++iAfter;
			}

			if (iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind != ECodeTokenKind::mc_Newline)
				continue;

			// A block comment behind an element whose separator or closer starts the next line trails the element, as it does
			// where they share a line, which a split writes it as: 'a /* A */' over ', b'. One a comma the conversion moves
			// leaves ends its line in any layout. So does one standing for a declarator's name: 'int */*_A*/' over ', int _B'.
			if (bBlock && !bCommaGone && (fp_IsTrailingBlockComment(i) || fp_IsNameComment(i)))
				continue;

			auto Text = m_Tokens.f_GetText(Comment);

			// A NOLINT comment suppresses diagnostics on its own line only, and a fallthrough annotation is read only there.
			if (Text.f_Find("malterlib-format") >= 0 || Text.f_Find("NOLINT") >= 0 || fg_IsFallthroughComment(Text))
				continue;

			// A documenting comment describes what stands in front of it on its line, which it never leaves.
			bool bDocumenting = Text.f_StartsWith("///<") || Text.f_StartsWith("//!<") || Text.f_StartsWith("/**<") || Text.f_StartsWith("/*!<");

			// One without the '<' documents the declaration behind it, which moving it above its line would change: 'int a; /** b */'.
			bool bForward
				= (Text.f_StartsWith("///") && !Text.f_StartsWith("////"))
				|| Text.f_StartsWith("//!")
				|| (Text.f_StartsWith("/**") && !Text.f_StartsWith("/***") && Text != "/**/")
				|| Text.f_StartsWith("/*!")
			;

			auto iLine = m_Lines.f_FindLine(Comment.m_iOffset);
			if (m_ProtectedStart[iLine])
				continue;

			CTrailedCode Code;
			if (!fp_MeasureTrailedCode(i, Code))
				continue;

			auto iFirst = Code.m_iFirst;
			auto iBefore = fp_PreviousSignificant(i);

			CStr Converted = Text;
			if (bBlock)
			{
				Converted = fg_TrailingLineComment(Text);
				if (Converted.f_IsEmpty())
					continue;
			}

			// One inside a statement written across lines, its expression going on behind the comment, describes the part it
			// trails and stays behind it: 'bt1 && // CHECK: ...' over 'bf1'. A braced list's rows are data, which one moved
			// above its row still describes.
			bool bInsideStatement = false;
			{
				// The statement is the one the code behind the comment stands in, which a statement's first token is no part of
				// as the index reads it.
				auto const &Nodes = m_Structure.f_GetNodes();
				auto iNext = fp_NextCode(i);
				auto iBeforeNode = m_Structure.f_FindEnclosingNode(umint(iBefore));
				auto const *pBeforeNode = iBeforeNode < Nodes.f_GetLen() ? &Nodes[iBeforeNode] : nullptr;
				bool bListRow = pBeforeNode && pBeforeNode->m_Kind == ECodeNodeKind::mc_Group && pBeforeNode->m_Bracket == ECodeBracket::mc_Brace;
				auto iStatement = iNext >= 0 ? m_Structure.f_FindEnclosingNode(umint(iNext)) : Nodes.f_GetLen();
				while (iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind == ECodeNodeKind::mc_Group)
					iStatement = Nodes[iStatement].m_iParent;

				auto iNextNode = iNext >= 0 ? m_Structure.f_FindNodeOpeningAt(umint(iNext)) : Nodes.f_GetLen();
				bool bBlockFollows = iNextNode < Nodes.f_GetLen() && Nodes[iNextNode].m_Kind == ECodeNodeKind::mc_Block;
				auto const *pStatement = iStatement < Nodes.f_GetLen() ? &Nodes[iStatement] : nullptr;
				bInsideStatement
					= iNext >= 0
					&& pStatement
					&& pStatement->m_Kind == ECodeNodeKind::mc_Statement
					&& umint(iBefore) >= pStatement->m_iFirstToken
					&& umint(iNext) <= pStatement->m_iLastToken
					&& !bBlockFollows
					&& !bListRow
					&& !m_Tokens.f_IsText(Tokens[umint(iBefore)], ";")
				;
			}

			// Code over the limit as written keeps its comment, also where the layout then splits it into rows one of which is too
			// long with it, as a designated initializer's can be, which the next pass then moves above that row.
			umint nComment = 0;
			bool bTooLong
				= nMaxColumns
				&& !bDocumenting
				&& !bForward
				&& !bInsideStatement
				&& Code.m_bStartsStatement
				&& Code.m_bMeasured
				&& Code.m_nColumns <= nMaxColumns
				&& fg_MeasureTextColumns(Converted, nTab, nComment)
				&& Code.m_nColumns + 1 + nComment > nMaxColumns
			;
			if (!bTooLong)
			{
				if (!bBlock || fp_IsDisabled(Comment.m_iOffset, Comment.m_nLength) || !fp_IsSelected(Comment.m_iOffset, Comment.m_nLength))
					continue;

				auto &Replace = m_Structural.f_Insert();
				Replace.m_iOffset = Comment.m_iOffset;
				Replace.m_nLength = Comment.m_nLength;
				Replace.m_Replacement = Converted;
				Replace.m_Rule = "trailing-comment";

				continue;
			}

			// One continued on the lines below it, written further in than the code it trails, moves with its continuation;
			// moved alone, the lines below would stand aligned with a comment no longer there.
			TCVector<umint> Continuation;
			for (auto iNextLine = iLine + 1; !bBlock && iNextLine < m_Lines.f_GetLineCount(); ++iNextLine)
			{
				auto iStart = m_Lines.f_GetLineStart(iNextLine);
				auto iEnd = m_Lines.f_GetLineContentEnd(iNextLine);
				auto iIndent = iStart;
				while (iIndent < iEnd && fg_IsSpaceOrTab(m_pSource[iIndent]))
					++iIndent;

				umint nColumns = 0;
				auto iNext = iIndent < iEnd ? m_Tokens.f_FindToken(iIndent) : Tokens.f_GetLen();
				auto const *pNext = iNext < Tokens.f_GetLen() ? &Tokens[iNext] : nullptr;
				bool bContinues
					= pNext
					&& !m_ProtectedStart[iNextLine]
					&& pNext->m_Kind == ECodeTokenKind::mc_LineComment
					&& pNext->m_iOffset == iIndent
					&& fg_MeasureTextColumns(m_pSource + iStart, iIndent - iStart, nTab, nColumns)
					&& nColumns > fp_GetTrailedCodeIndent(i)
					&& !fp_StartsBodyBehind(i, nColumns)
				;
				if (!bContinues)
					break;

				// A formatter directive and a documentation comment belong to the code below them: '// malterlib-format align: ='
				// and '/// The next declaration'.
				auto Continued = m_Tokens.f_GetText(Tokens[iNext]);
				bool bDocumentation = (Continued.f_StartsWith("///") && !Continued.f_StartsWith("////")) || Continued.f_StartsWith("//!");
				if (Continued.f_Find("malterlib-format") >= 0 || bDocumentation)
					break;

				Continuation.f_Insert(iNext);
			}

			// A suppression among the lines below is the next code's, which moving it along would take away, and a fallthrough
			// annotation is read only behind its statement.
			bool bSuppression = false;
			for (auto iContinuation : Continuation)
			{
				auto Continued = m_Tokens.f_GetText(Tokens[iContinuation]);
				bSuppression = bSuppression || Continued.f_Find("NOLINT") >= 0 || fg_IsFallthroughComment(Continued);
			}

			if (bSuppression || fg_IsFallthroughComment(m_Tokens.f_GetText(Comment)))
				continue;

			// A block comment in front would be left ending the line, for the next pass to convert. Behind a block's opening
			// brace ending the code that pass finds the comments on the brace's own line, and converts a block comment where
			// that line fits it.
			if (Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_BlockComment)
			{
				umint nBraceLine = 0;
				bool bFits = bBlock && fp_MeasureOnOpeningBraceLine(i, Code, nBraceLine) && nBraceLine + 1 + nComment <= nMaxColumns;
				if (bFits && !fp_IsDisabled(Comment.m_iOffset, Comment.m_nLength) && fp_IsSelected(Comment.m_iOffset, Comment.m_nLength))
				{
					auto &Replace = m_Structural.f_Insert();
					Replace.m_iOffset = Comment.m_iOffset;
					Replace.m_nLength = Comment.m_nLength;
					Replace.m_Replacement = Converted;
					Replace.m_Rule = "trailing-comment";
				}

				continue;
			}

			// An empty statement taken out from in front leaves the comment a line of its own.
			if (iFirst == umint(iBefore))
			{
				bool bTakenOut = false;
				auto const &First = Tokens[iFirst];
				for (auto const &Edit : m_Structural)
					bTakenOut = bTakenOut || (Edit.m_Rule == "empty-statement" && Edit.m_iOffset <= First.m_iOffset && First.m_iOffset < Edit.f_GetEnd());

				if (bTakenOut)
					continue;
			}

			// The comment's line takes the indentation the code's has, so that the layout moves the two and the comments
			// above them together.
			auto iRemove = Tokens[umint(iBefore)].f_GetEnd();
			auto nRemove = (Continuation.f_IsEmpty() ? Comment : Tokens[Continuation.f_GetLast()]).f_GetEnd() - iRemove;
			auto iInsert = Tokens[iFirst].m_iOffset;
			if (fp_IsDisabled(iRemove, nRemove) || !fp_IsSelected(iRemove, nRemove) || fp_IsDisabled(iInsert, 0) || !fp_IsSelected(iInsert, 0))
				continue;

			// Above a line a NOLINTNEXTLINE suppresses, the comment would take the suppression from the code.
			if (fp_IsSuppressed(iInsert, 0))
				continue;

			// The moved lines are a run of comment lines, whose shared blanks are one space as that run's would be.
			TCVector<CStr> Moved;
			Moved.f_Insert(Converted);
			for (auto iContinuation : Continuation)
				Moved.f_Insert(fg_TrimCommentTrailingSpace(m_Tokens.f_GetText(Tokens[iContinuation])));

			// One ending in a backslash, as only one ending the file can, would splice the code it moves above into itself.
			bool bSplices = false;
			for (auto const &Line : Moved)
				bSplices = bSplices || fg_TrimCommentTrailingSpace(Line).f_EndsWith("\\");

			if (bSplices)
				continue;

			umint nShared = 0;
			if (fg_SharedCommentBlanks(Moved, nShared) && fg_ConvertsCommentBlanks(Moved, nShared))
			{
				for (auto &Line : Moved)
				{
					if (2 + fg_CommentBlanks(Line) < Line.f_GetLen())
						Line = "// " + Line.f_Extract(2 + nShared);
				}
			}

			// Behind a block's brace the comment takes a line of its own between the two, one level in from the brace.
			auto &Insert = m_Structural.f_Insert();
			Insert.m_iOffset = iInsert;
			Insert.m_nLength = 0;
			if (Code.m_bBehindBrace)
			{
				auto Indent = fp_MakeIndent(fp_GetSourceLineIndent(Code.m_iBehindOf) + nTab);
				CStr::CAppender Appender(Insert.m_Replacement);
				Appender += LineEnding;
				for (auto const &Line : Moved)
				{
					Appender += Indent;
					Appender += Line;
					Appender += LineEnding;
				}

				Appender += Indent;
			}
			else
			{
				auto iCodeLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(iInsert));
				auto pCodeIndent = m_pSource + iCodeLineStart;
				auto nCodeIndent = iInsert - iCodeLineStart;
				CStr::CAppender Appender(Insert.m_Replacement);
				for (umint iLine = 0; iLine < Moved.f_GetLen(); ++iLine)
				{
					if (iLine)
						Appender.f_AddString(pCodeIndent, nCodeIndent);

					Appender += Moved[iLine];
					Appender += LineEnding;
				}

				Appender.f_AddString(pCodeIndent, nCodeIndent);
			}

			Insert.m_Rule = "trailing-comment";

			auto &Remove = m_Structural.f_Insert();
			Remove.m_iOffset = iRemove;
			Remove.m_nLength = nRemove;
			Remove.m_Rule = "trailing-comment";
		}
	}

	// The line comments on consecutive lines of their own read as one text, whose blanks every line of it starts with
	// behind its markers are one space: '//\t\t{A}' over '//\t\t\t{B}' is '// {A}' over '// \t{B}'. A documenting
	// marker, '///' or '//!', is the text's own.
	void CFormattingAnalyzer::fp_ConvertCommentPrefixes()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();

		// Behind a block comment ending on the line, as '*/ // A', the comment is no line of its own.
		auto fStartsLine = [&](umint _iComment)
			{
				// A directive taken out of the code keeps its text, and the comments around it are runs of their own.
				if (Tokens[_iComment].m_Kind != ECodeTokenKind::mc_LineComment || fp_IsCommentedDirective(_iComment))
					return false;

				auto iOffset = Tokens[_iComment].m_iOffset;
				for (auto iChar = m_Lines.f_GetLineStart(m_Lines.f_FindLine(iOffset)); iChar < iOffset; ++iChar)
				{
					if (!fg_IsSpaceOrTab(m_pSource[iChar]))
						return false;
				}

				return true;
			}
		;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			if (!fStartsLine(i))
				continue;

			// The lines continuing a trailing comment, further in than the code it trails, are its own, which its conversion
			// can move.
			auto iBefore = aint(i) - 1;
			umint nNewlinesBefore = 0;
			while (iBefore >= 0)
			{
				auto Kind = Tokens[umint(iBefore)].m_Kind;
				if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
					break;

				nNewlinesBefore += Kind == ECodeTokenKind::mc_Newline;
				--iBefore;
			}

			bool bContinues
				= iBefore >= 0
				&& nNewlinesBefore == 1
				&& Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_LineComment
				&& !fp_IsFirstOnLine(umint(iBefore))
				&& fp_GetSourceLineIndent(i) > fp_GetTrailedCodeIndent(umint(iBefore))
			;

			TCVector<umint> Run;
			Run.f_Insert(i);
			for (auto iNext = i + 1; iNext < Tokens.f_GetLen(); ++iNext)
			{
				umint nNewlines = 0;
				while (iNext < Tokens.f_GetLen())
				{
					auto Kind = Tokens[iNext].m_Kind;
					if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
						break;

					nNewlines += Kind == ECodeTokenKind::mc_Newline;
					++iNext;
				}

				if (iNext >= Tokens.f_GetLen() || nNewlines != 1 || !fStartsLine(iNext))
					break;

				Run.f_Insert(iNext);
			}

			i = Run.f_GetLast();
			if (bContinues)
				continue;

			// The blanks each line with text starts with behind its '//', and the part all of them share.
			TCVector<CStr> Texts;
			for (auto iComment : Run)
				Texts.f_Insert(m_Tokens.f_GetText(Tokens[iComment]));

			umint nShared = 0;
			if (!fg_SharedCommentBlanks(Texts, nShared))
				continue;

			// Markers at different columns, as a comment toggled line by line leaves them, are aligned by the layout where
			// it stands the run where the code under it does; then the code keeps where each line had it relative to the
			// others: '\t//\t\t\t\tA' over '\t\t//\t\t\tB' over '\t\t\t//C' is '// A' over '// B' over '// C'.
			bool bSameColumn = true;
			for (auto iComment : Run)
				bSameColumn = bSameColumn && fp_GetSourceLineIndent(iComment) == fp_GetSourceLineIndent(Run[0]);

			if (!bSameColumn)
			{
				aint iCode = fp_FindCodeBelowComments(Run.f_GetLast());

				// As the run rule levels it: the comment lines in front of a list's or a call's closing marker stand among its
				// elements.
				auto iClosedGroup = iCode >= 0 ? m_Structure.f_FindNodeClosingAt(umint(iCode)) : m_Structure.f_GetNodes().f_GetLen();
				bool bGroupCloser = iClosedGroup < m_Structure.f_GetNodes().f_GetLen() && m_Structure.f_GetNodes()[iClosedGroup].m_Kind == ECodeNodeKind::mc_Group;
				if (iCode < 0 || bGroupCloser)
					continue;

				auto nTab = m_Request.m_Settings.m_nTabWidth;

				// A line's code stands where its text does behind a tab, as a toggle at the line's start leaves it, and where the
				// marker does otherwise, as one at the code's indentation leaves it.
				auto fTextColumn = [&](umint _iComment, umint _nBlanks) -> umint
					{
						auto iOffset = Tokens[_iComment].m_iOffset;
						bool bTab = false;
						for (umint iBlank = 0; iBlank < _nBlanks; ++iBlank)
							bTab = bTab || m_pSource[iOffset + 2 + iBlank] == '\t';

						auto iStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(iOffset));
						umint nColumns = 0;
						fg_MeasureTextColumns(m_pSource + iStart, (bTab ? iOffset + 2 + _nBlanks : iOffset) - iStart, nTab, nColumns);

						return nColumns;
					}
				;

				umint nMinColumn = TCLimitsInt<umint>::mc_Max;
				for (auto iComment : Run)
				{
					auto const &Comment = Tokens[iComment];
					umint nBlanks = 0;
					while (2 + nBlanks < Comment.m_nLength && fg_IsSpaceOrTab(m_pSource[Comment.m_iOffset + 2 + nBlanks]))
						++nBlanks;

					if (2 + nBlanks < Comment.m_nLength)
						nMinColumn = fg_Min(nMinColumn, fTextColumn(iComment, nBlanks));
				}

				for (auto iComment : Run)
				{
					auto const &Comment = Tokens[iComment];
					umint nBlanks = 0;
					while (2 + nBlanks < Comment.m_nLength && fg_IsSpaceOrTab(m_pSource[Comment.m_iOffset + 2 + nBlanks]))
						++nBlanks;

					if (2 + nBlanks >= Comment.m_nLength || fp_IsDisabled(Comment.m_iOffset, Comment.m_nLength) || !fp_IsSelected(Comment.m_iOffset, Comment.m_nLength))
						continue;

					auto nExtra = fTextColumn(iComment, nBlanks) - nMinColumn;
					CStr Replacement = " ";
					Replacement.f_AddChars('\t', aint(nExtra / nTab));
					Replacement.f_AddChars(' ', aint(nExtra % nTab));
					if (fg_IsSameText(Replacement, m_pSource + Comment.m_iOffset + 2, nBlanks))
						continue;

					auto &Replace = m_Structural.f_Insert();
					Replace.m_iOffset = Comment.m_iOffset + 2;
					Replace.m_nLength = nBlanks;
					Replace.m_Replacement = Replacement;
					Replace.m_Rule = "comment-space";
				}

				continue;
			}

			if (!fg_ConvertsCommentBlanks(Texts, nShared))
				continue;

			for (auto iComment : Run)
			{
				auto const &Comment = Tokens[iComment];
				umint nBlanks = 0;
				while (2 + nBlanks < Comment.m_nLength && fg_IsSpaceOrTab(m_pSource[Comment.m_iOffset + 2 + nBlanks]))
					++nBlanks;

				bool bConverts = 2 + nBlanks < Comment.m_nLength && !fp_IsDisabled(Comment.m_iOffset, Comment.m_nLength) && fp_IsSelected(Comment.m_iOffset, Comment.m_nLength);
				if (!bConverts)
					continue;

				auto &Replace = m_Structural.f_Insert();
				Replace.m_iOffset = Comment.m_iOffset + 2;
				Replace.m_nLength = nShared;
				Replace.m_Replacement = " ";
				Replace.m_Rule = "comment-space";
			}
		}

		// A block comment's text written further in than one space behind its opening marker starts below the marker, at
		// the marker's column, and the lines below keep where they stand relative to it: '/*\t\tA' over '\t\t\tB' is '/*'
		// over 'A' over '\tB'. Text below a marker alone on its line further in than the marker moves out to it the same
		// way. A comment with a line further out than that, or a decorated ' * B', keeps its lines; a closing marker on a
		// line of its own keeps its place.
		auto nTab = m_Request.m_Settings.m_nTabWidth;
		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Comment = Tokens[i];
			if (Comment.m_Kind != ECodeTokenKind::mc_BlockComment || !Comment.m_bMultiLine)
				continue;

			auto iLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Comment.m_iOffset));
			bool bStartsLine = true;
			for (auto iChar = iLineStart; iChar < Comment.m_iOffset; ++iChar)
				bStartsLine = bStartsLine && fg_IsSpaceOrTab(m_pSource[iChar]);

			auto pText = m_pSource + Comment.m_iOffset;
			auto const *pAfterMarker = Comment.m_nLength > 2 ? pText + 2 : nullptr;
			bool bDocumenting = pAfterMarker && (*pAfterMarker == '*' || *pAfterMarker == '!');
			if (!bStartsLine || bDocumenting || fp_IsDisabled(Comment.m_iOffset, Comment.m_nLength) || !fp_IsSelected(Comment.m_iOffset, Comment.m_nLength))
				continue;

			umint nBlanks = 0;
			while (2 + nBlanks < Comment.m_nLength && fg_IsSpaceOrTab(pText[2 + nBlanks]))
				++nBlanks;

			umint nMarkerColumns = 0;
			umint nFirstColumns = 0;
			auto FirstChar = pText[2 + nBlanks];
			bool bMarkerAlone = FirstChar == '\n' || FirstChar == '\r';
			bool bFirstText = nBlanks && !bMarkerAlone;
			bool bMeasured
				= fg_MeasureTextColumns(m_pSource + iLineStart, Comment.m_iOffset - iLineStart, nTab, nMarkerColumns)
				&& fg_MeasureTextColumns(m_pSource + iLineStart, Comment.m_iOffset + 2 + nBlanks - iLineStart, nTab, nFirstColumns)
			;
			if (!bMeasured || (!bFirstText && !bMarkerAlone))
				continue;

			// The lines below with text, each with its indentation's end and columns; one holding only the closing marker
			// keeps its place.
			struct CLine
			{
				umint m_iStart;
				umint m_iContent;
				umint m_nColumns;
			};

			TCVector<CLine> Lines;
			umint nMinColumns = bFirstText ? nFirstColumns : TCLimitsInt<umint>::mc_Max;
			bool bValid = true;
			for (auto iLine = m_Lines.f_FindLine(Comment.m_iOffset) + 1; bValid && iLine < m_Lines.f_GetLineCount() && m_Lines.f_GetLineStart(iLine) < Comment.f_GetEnd(); ++iLine)
			{
				auto iStart = m_Lines.f_GetLineStart(iLine);
				auto iContent = iStart;
				while (iContent < Comment.f_GetEnd() && fg_IsSpaceOrTab(m_pSource[iContent]))
					++iContent;

				auto const *pContent = iContent < Comment.f_GetEnd() ? m_pSource + iContent : nullptr;
				if (!pContent || *pContent == '\n' || *pContent == '\r')
					continue;

				if (iContent + 2 == Comment.f_GetEnd())
					continue;

				umint nColumns = 0;
				bValid = *pContent != '*' && fg_MeasureTextColumns(m_pSource + iStart, iContent - iStart, nTab, nColumns);
				nMinColumns = fg_Min(nMinColumns, nColumns);
				Lines.f_Insert(CLine{iStart, iContent, nColumns});
			}

			auto nTextColumns = nMarkerColumns;
			if (!bValid || Lines.f_IsEmpty() || nMinColumns <= (bFirstText ? nMarkerColumns + 3 : nTextColumns))
				continue;

			// The first line's text moves to a line of its own below the marker, ended as the source ends the marker's line.
			auto nShift = aint(nMinColumns) - aint(nTextColumns);
			CStr Replacement;
			{
				CStr::CAppender Appender(Replacement);
				auto iCopied = Comment.m_iOffset + 2;
				if (bFirstText)
				{
					auto iNewline = Comment.m_iOffset + 2 + nBlanks;
					auto Char = m_pSource[iNewline];
					while (Char != '\n' && Char != '\r')
						Char = m_pSource[++iNewline];

					Appender += Char == '\r' && m_pSource[iNewline + 1] == '\n' ? "\r\n" : Char == '\r' ? "\r" : "\n";
					fp_AppendIndent(Appender, umint(aint(nFirstColumns) - nShift));
					iCopied += nBlanks;
				}

				for (auto const &Line : Lines)
				{
					Appender.f_AddString(m_pSource + iCopied, Line.m_iStart - iCopied);
					fp_AppendIndent(Appender, umint(aint(Line.m_nColumns) - nShift));
					iCopied = Line.m_iContent;
				}

				Appender.f_AddString(m_pSource + iCopied, Comment.f_GetEnd() - iCopied);
			}

			auto &Replace = m_Structural.f_Insert();
			Replace.m_iOffset = Comment.m_iOffset + 2;
			Replace.m_nLength = Comment.m_nLength - 2;
			Replace.m_Replacement = Replacement;
			Replace.m_Rule = "comment-space";
		}
	}
}
