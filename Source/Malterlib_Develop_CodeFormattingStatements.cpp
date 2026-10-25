// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace NMib::NDevelop::NPrivate
{
	// Fallback for a construct whose own lines are fixed: its inner constructs can still be
	// brought back to one line where they fit.
	void CFormattingAnalyzer::fp_JoinNode(umint _iNode)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (Node.m_Kind == ECodeNodeKind::mc_Unsupported)
			return;

		bool bContainer
			= Node.m_Kind == ECodeNodeKind::mc_File || Node.m_Kind == ECodeNodeKind::mc_Block || (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Brace)
		;
		if (!bContainer && Node.f_IsJoinable() && !Node.m_bFixedLineBreaks && !Node.m_bTemplateHeader)
		{
			auto iFirst = Node.m_iFirstToken;
			if (Node.m_Kind == ECodeNodeKind::mc_Group)
			{
				umint iStatement = _iNode;
				while (Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement && iStatement)
					iStatement = Nodes[iStatement].m_iParent;

				auto const &Statement = Nodes[iStatement];
				auto iBoundary = Statement.m_Kind == ECodeNodeKind::mc_Statement ? Statement.m_iFirstToken : iFirst;
				auto iOwner = fp_PreviousCode(iFirst);
				if (iOwner >= 0 && umint(iOwner) >= iBoundary)
				{
					auto const &Owner = m_Tokens.f_GetTokens()[umint(iOwner)];
					bool bOwns = Owner.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Owner, ")") || m_Tokens.f_IsText(Owner, "]") || m_Tokens.f_IsText(Owner, ">");

					// An attribute is no subscript of what stands in front of it: 'template <class t_C>' over '[[nodiscard]]'.
					auto iInner = fp_NextCode(iFirst);
					if (Node.m_Bracket == ECodeBracket::mc_Square && iInner >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iInner)], "["))
						bOwns = false;

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

			// What goes on behind a template argument list, its call or its qualification, starts a line of its own only where
			// the layout splits the list, so the list joins with the rest of that line or not at all: joined alone, 'f_Get<CFoo>'
			// over '()' or 'TCFoo<CBar>' over '::CType = 0' undoes the split that put it there.
			auto iJoinLast = Node.m_iLastToken;
			if (Node.m_Kind == ECodeNodeKind::mc_Group && Node.m_Bracket == ECodeBracket::mc_Angle)
			{
				auto const &Tokens = m_Tokens.f_GetTokens();
				auto iNext = fp_NextCode(Node.m_iLastToken);
				auto const *pNext = iNext >= 0 ? &Tokens[umint(iNext)] : nullptr;
				bool bGoesOn
					= pNext && (m_Tokens.f_IsText(*pNext, "(") || m_Tokens.f_IsText(*pNext, "::")) && fp_IsFirstOnLine(umint(iNext)) && fp_SpansLines(Node.m_iFirstToken, Node.m_iLastToken)
				;
				if (bGoesOn)
				{
					auto Depth = m_TokenDepth[umint(iNext)];
					iJoinLast = umint(iNext);
					for (auto i = umint(iNext) + 1; i < Tokens.f_GetLen() && Tokens[i].m_Kind != ECodeTokenKind::mc_Newline; ++i)
					{
						auto const &Token = Tokens[i];
						if (!fg_IsCodeToken(Token))
							continue;

						auto TokenDepth = m_TokenDepth[i];
						if (TokenDepth < Depth || (TokenDepth == Depth && (m_Tokens.f_IsText(Token, ",") || m_Tokens.f_IsText(Token, ";"))))
							break;

						iJoinLast = i;
					}

					// A line that opens a list it does not close holds the list's opening, which is no leftover of a split.
					auto const &Last = Tokens[iJoinLast];
					bool bCloses
						= m_TokenDepth[iJoinLast] == Depth
						&& !m_Tokens.f_IsText(Last, "(")
						&& !m_Tokens.f_IsText(Last, "[")
						&& !m_Tokens.f_IsText(Last, "{")
						&& !(m_Tokens.f_IsText(Last, "<") && m_Structure.f_IsAngleBracket(iJoinLast))
					;
					if (!bCloses)
						iJoinLast = Node.m_iLastToken;
				}
			}

			// A link of a chain on what a call yields that starts its line is one the chain's layout gave a line of its
			// own, and keeps it: 'f_Get()' over '[0]' over '->f_Name()'.
			bool bOwnLink = fp_IsYieldedScope(Node.m_iFirstToken) && fp_IsFirstOnLine(Node.m_iFirstToken);
			if (!bOwnLink && fp_TryJoin(iFirst, iJoinLast, fp_GetStatementIndent(iFirst)))
				return;
		}

		// A list keeping its lines still lays out an element written on one line too long for
		// it, as an opened list lays out each of its elements.
		TCVector<umint> LaidOut;
		if (Node.m_Kind == ECodeNodeKind::mc_Group && (Node.m_Bracket == ECodeBracket::mc_Brace || Node.m_Bracket == ECodeBracket::mc_Paren))
		{
			// A statement a braced initializer written across lines ends is a split one: the braces stand one level
			// in from it, and its terminator on a line of its own: 'CFoo Value =' over '{' over '}' over ';'. An opening
			// brace written at the end of a line takes a line of its own, and the list's lines move with it.
			bool bTrailingOpen = Node.m_Bracket == ECodeBracket::mc_Brace && !fp_IsFirstOnLine(Node.m_iFirstToken);

			// So does the outer list of '{{' over the elements over '}}', whose closing marker shares the inner one's line:
			// both open, each marker on a line of its own.
			bool bClosesLine = fp_IsFirstOnLine(Node.m_iLastToken);
			for (auto iChild : Node.m_Children)
			{
				if (!bClosesLine && fp_FindHuggingList(iChild) == _iNode)
					bClosesLine = fp_IsFirstOnLine(Nodes[iChild].m_iLastToken);
			}

			bool bOpened = Node.m_bHasMultiLineBrace && (bTrailingOpen || fp_IsFirstOnLine(Node.m_iFirstToken)) && bClosesLine;

			// A parenthesis ending the statement whose lines it keeps for a list inside it stands the same way.
			bool bMoves = Node.m_Bracket == ECodeBracket::mc_Brace || (Node.m_Bracket == ECodeBracket::mc_Paren && fp_IsFirstOnLine(Node.m_iFirstToken));
			if (bMoves && Node.m_iParent < Nodes.f_GetLen() && bOpened)
			{
				auto const &Statement = Nodes[Node.m_iParent];
				auto iTerminator = fp_NextCode(Node.m_iLastToken);
				auto iBefore = fp_PreviousCode(Node.m_iFirstToken);
				bool bEnds
					= Statement.m_Kind == ECodeNodeKind::mc_Statement
					&& iTerminator >= 0
					&& umint(iTerminator) == Statement.m_iLastToken
					&& m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iTerminator)], ";")
					&& iBefore >= 0
					&& umint(iBefore) >= Statement.m_iFirstToken
					&& fp_IsFirstOnLine(Statement.m_iFirstToken)
					&& !fp_HasOpaqueDirective(Statement.m_iFirstToken, Statement.m_iLastToken)
				;
				if (bEnds)
				{
					// Every line of the list moves with its braces; what is nested in it and places its own lines places
					// them against the moved ones.
					auto nStatementIndent = fp_GetStatementIndent(Statement.m_iFirstToken);
					auto nBraceIndent = nStatementIndent + m_Request.m_Settings.m_nTabWidth;
					// The lines move from where the source wrote them, so the brace they move with is measured there too.
					auto nBraceWritten = fp_GetStatementIndent(bTrailingOpen ? Node.m_iLastToken : Node.m_iFirstToken);
					// The lines move only with their braces, which a range leaving the statement out does not move.
					auto const &AllTokens = m_Tokens.f_GetTokens();
					auto iStatementStart = AllTokens[Statement.m_iFirstToken].m_iOffset;
					bool bSelected = fp_IsSelected(iStatementStart, AllTokens[Statement.m_iLastToken].f_GetEnd() - iStatementStart);
					if ((bTrailingOpen || nBraceWritten != nBraceIndent) && bSelected && fp_CanPlaceBlock(_iNode))
					{
						auto nDelta = aint(nBraceIndent) - aint(fp_GetSourceLineIndent(bTrailingOpen ? Node.m_iLastToken : Node.m_iFirstToken));

						// Elements that each start their line at one column stand one level in from the braces, and the lines
						// between them keep their place against them.
						{
							auto const &ElementTokens = m_Tokens.f_GetTokens();

							auto fStartsLine = [&](aint _iToken)
								{
									return _iToken >= 0 && (fp_IsFirstOnLine(umint(_iToken)) || fp_StartsLineBehindComments(umint(_iToken)));
								}
							;

							bool bAligned = true;
							aint nColumn = -1;

							auto fElement = [&](aint _iToken)
								{
									if (!fStartsLine(_iToken))
									{
										bAligned = false;

										return;
									}

									auto nElement = aint(fp_GetSourceLineIndent(umint(_iToken)));
									bAligned = bAligned && (nColumn < 0 || nColumn == nElement);
									nColumn = nElement;
								}
							;

							fElement(fp_NextCode(Node.m_iFirstToken));
							for (auto iSplit : Node.m_SplitPoints)
							{
								auto iNext = fp_NextCode(iSplit);
								if (!m_Tokens.f_IsText(ElementTokens[iSplit], ",") || iNext < 0 || umint(iNext) >= Node.m_iLastToken)
									continue;

								fElement(fp_IsFirstOnLine(iSplit) ? aint(iSplit) : iNext);
							}

							if (bAligned && nColumn >= 0)
								nDelta = aint(nBraceIndent + m_Request.m_Settings.m_nTabWidth) - nColumn;
						}

						if (bTrailingOpen)
							fp_OwnLineBefore(Node.m_iFirstToken, nBraceIndent);
						else
							fp_IndentBefore(Node.m_iFirstToken, nBraceIndent);

						auto const &ListTokens = m_Tokens.f_GetTokens();
						for (auto i = Node.m_iFirstToken + 1; i <= Node.m_iLastToken; ++i)
						{
							auto const &Token = ListTokens[i];
							auto Kind = Token.m_Kind;
							bool bCode
								= Kind != ECodeTokenKind::mc_Whitespace
								&& Kind != ECodeTokenKind::mc_Newline
								&& Kind != ECodeTokenKind::mc_LineComment
								&& Kind != ECodeTokenKind::mc_BlockComment
								&& Kind != ECodeTokenKind::mc_Preprocessor
							;
							bool bLine
								= bCode
								&& (fp_IsFirstOnLine(i) || fp_StartsLineBehindComments(i))
								&& EGap(m_GapState[i]) != EGap::mc_Inline
								&& !m_ProtectedStart[m_Lines.f_FindLine(Token.m_iOffset)]
							;
							auto nIndent = aint(fp_GetSourceLineIndent(i)) + nDelta;
							if (bLine && nIndent >= 0)
								fp_IndentBefore(i, umint(nIndent));

							// A comment line among the rows moves with them.
							bool bCommentLine
								= (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
								&& fp_IsFirstOnLine(i)
								&& !m_ProtectedStart[m_Lines.f_FindLine(Token.m_iOffset)]
							;
							if (bCommentLine && nIndent >= 0 && fp_GetSourceLineIndent(i))
							{
								m_CommentMoved[i] = 1;
								m_CommentIndent[i] = umint(nIndent);
							}

							// A nested list never opened takes its stray closing brace back behind its last element here, as
							// the pass that does so elsewhere would leave a line this move has decided.
							auto iNested = bLine && m_Tokens.f_IsText(Token, "{") ? m_Structure.f_FindNodeOpeningAt(i) : Nodes.f_GetLen();
							auto const *pNested = iNested < Nodes.f_GetLen() ? &Nodes[iNested] : nullptr;
							if (pNested && pNested->m_Kind == ECodeNodeKind::mc_Group && fp_IsFirstOnLine(pNested->m_iLastToken))
							{
								auto const &Nested = *pNested;
								auto iNestedLast = fp_PreviousCode(Nested.m_iLastToken);
								bool bNeverOpened = iNestedLast >= 0 && umint(iNestedLast) > Nested.m_iFirstToken && !ListTokens[umint(iNestedLast)].m_bMultiLine;
								for (auto iInner = Nested.m_iFirstToken + 1; bNeverOpened && iInner < Nested.m_iLastToken; ++iInner)
								{
									auto InnerKind = ListTokens[iInner].m_Kind;
									if (iInner > umint(iNestedLast))
										bNeverOpened = InnerKind == ECodeTokenKind::mc_Whitespace || InnerKind == ECodeTokenKind::mc_Newline;
									else
										bNeverOpened = InnerKind != ECodeTokenKind::mc_Newline && InnerKind != ECodeTokenKind::mc_LineComment && InnerKind != ECodeTokenKind::mc_Preprocessor;
								}

								if (bNeverOpened)
									fp_MarkInline(umint(iNestedLast), Nested.m_iLastToken);
							}
						}
					}

					// The elements of one holding a string over lines, which keeps the list from being levelled, stand one
					// level in from its braces all the same where they start their lines; the string's own lines are its text.
					if (Node.m_Bracket == ECodeBracket::mc_Brace && Node.m_bHasMultiLineToken && fp_IsFirstOnLine(Node.m_iLastToken) && fp_CanPlaceBlock(_iNode))
					{
						auto const &ListTokens = m_Tokens.f_GetTokens();
						auto nElementIndent = nBraceIndent + m_Request.m_Settings.m_nTabWidth;

						auto fLevel = [&](aint _iToken)
							{
								if (_iToken < 0 || umint(_iToken) >= Node.m_iLastToken || !fp_IsFirstOnLine(umint(_iToken)))
									return;

								if (m_ProtectedStart[m_Lines.f_FindLine(ListTokens[umint(_iToken)].m_iOffset)])
									return;

								if (fp_GetStatementIndent(umint(_iToken)) != nElementIndent)
									fp_IndentBefore(umint(_iToken), nElementIndent);
							}
						;

						fLevel(fp_NextCode(Node.m_iFirstToken));
						for (auto iSplit : Node.m_SplitPoints)
						{
							if (m_Tokens.f_IsText(ListTokens[iSplit], ","))
							{
								fLevel(aint(iSplit));
								if (!fp_IsFirstOnLine(iSplit))
									fLevel(fp_NextCode(iSplit));
							}
						}
					}

					// The inner list of '{{' over the elements over '}}' opens one level in, its markers on lines of their own,
					// and the outer list's closing marker under its opening one.
					for (auto iChild : Node.m_Children)
					{
						if (fp_FindHuggingList(iChild) != _iNode)
							continue;

						auto nInnerIndent = nBraceIndent + m_Request.m_Settings.m_nTabWidth;
						fp_BreakBefore(Nodes[iChild].m_iFirstToken, nInnerIndent);
						fp_BreakBefore(Nodes[iChild].m_iLastToken, nInnerIndent);
						fp_BreakBefore(Node.m_iLastToken, nBraceIndent);
					}

					if (fp_IsFirstOnLine(umint(iTerminator)))
					{
						if (fp_GetStatementIndent(umint(iTerminator)) != nStatementIndent)
							fp_IndentBefore(umint(iTerminator), nStatementIndent);
					}
					else
						fp_OwnLineBefore(umint(iTerminator), nStatementIndent);
				}
			}

			// A list its directive lays out is written by it, once the layout has placed its braces.
			if (_iNode < m_ListLayoutOfNode.f_GetLen() && m_ListLayoutOfNode[_iNode])
				return;

			// Each element on a line of its own stands one level in from a list's opening marker on a line of its own,
			// with the commas in front of the others, however the first was aligned: '{1, 2}' over ', {3, 4}'. The
			// closing marker stands under the opening one.
			bool bLevelled = fp_IsLevelled(_iNode);
			auto nOpenLevel = bLevelled ? fp_GetStatementIndent(Node.m_iFirstToken) : 0;
			auto nElementLevel = nOpenLevel + m_Request.m_Settings.m_nTabWidth;
			if (bLevelled && fp_GetStatementIndent(Node.m_iLastToken) != nOpenLevel)
				fp_IndentBefore(Node.m_iLastToken, nOpenLevel);

			// A comma a directive stands behind is one the conversion could not move in front of the element after
			// it, and the list keeps all of its commas behind its elements. So does one a block comment sharing a line
			// stands on either side of, which names an element rather than trailing it: '/*bFlag=*/true,'.
			bool bMovesCommas = Node.m_Bracket == ECodeBracket::mc_Brace;
			for (auto i = Node.m_iFirstToken + 1; bMovesCommas && i < Node.m_iLastToken; ++i)
			{
				if (!m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], ","))
					continue;

				auto fNames = [&](umint _iGap)
					{
						return m_Tokens.f_GetTokens()[_iGap].m_Kind == ECodeTokenKind::mc_BlockComment && (!fp_IsFirstOnLine(_iGap) || !fp_IsLastOnLine(_iGap));
					}
				;

				auto iNext = fp_NextCode(i);
				for (auto iGap = i + 1; iNext >= 0 && iGap < umint(iNext) && bMovesCommas; ++iGap)
					bMovesCommas = m_Tokens.f_GetTokens()[iGap].m_Kind != ECodeTokenKind::mc_Preprocessor && !fNames(iGap);

				auto iPrevious = fp_PreviousCode(i);
				for (auto iGap = iPrevious >= 0 ? umint(iPrevious) + 1 : i; iGap < i && bMovesCommas; ++iGap)
					bMovesCommas = !fNames(iGap);
			}

			umint iElement = Node.m_iFirstToken + 1;
			umint iChildCursor = 0;
			for (umint iSplit = 0; iElement <= Node.m_iLastToken; ++iSplit)
			{
				auto iEnd = iSplit < Node.m_SplitPoints.f_GetLen() ? Node.m_SplitPoints[iSplit] : Node.m_iLastToken;
				auto iLast = fp_PreviousCode(iEnd);
				auto iStart = fp_NextCode(iElement - 1);
				bool bElement = iEnd > iElement && iStart >= 0 && iLast >= 0 && umint(iStart) <= umint(iLast) && fp_IsFirstOnLine(umint(iStart));
				for (auto i = bElement ? umint(iStart) + 1 : umint(0); bElement && i < umint(iLast); ++i)
					bElement = m_Tokens.f_GetTokens()[i].m_Kind != ECodeTokenKind::mc_Newline;

				// A braced list written across lines is not brought back onto one, but is laid out as an opened list
				// otherwise: each element written on one line takes a line of its own with its comma in front, a comma
				// written at the end of the line in front of it included, and is laid out as an element.
				auto iElementFirst = iStart;
				if (iSplit && iStart >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iStart)], ","))
					iElementFirst = fp_NextCode(umint(iStart));

				bool bOneLine = iElementFirst >= 0 && iLast >= 0 && umint(iElementFirst) <= umint(iLast) && !fp_SpansLines(umint(iElementFirst), umint(iLast));

				// One broken only inside its groups, which join with what they belong to, is one written on one line as the
				// next pass reads it: 'std::regex(' over 'R"(...)")'.
				if (!bOneLine && bLevelled && bMovesCommas && iElementFirst >= 0 && iLast >= 0 && umint(iElementFirst) <= umint(iLast))
				{
					bool bInsideGroups = fp_IsBrokenInsideGroups(_iNode, umint(iElementFirst), umint(iLast));

					// One too long for its line is laid out as one written on a line is, split where it does not fit.
					bOneLine = bInsideGroups && fp_IsRangeJoinable(_iNode, umint(iElementFirst), umint(iLast));
				}

				// So does one only a string continued over lines takes across them, which keeps its continuation.
				if (bLevelled && !bOneLine && bMovesCommas && iEnd > iElement && iElementFirst >= 0 && iLast >= 0 && !fp_IsFirstOnLine(umint(iStart)))
				{
					auto const &AllTokens = m_Tokens.f_GetTokens();
					bool bContinuedString = umint(iElementFirst) <= umint(iLast);
					for (auto i = umint(iElementFirst) + 1; bContinuedString && i <= umint(iLast); ++i)
					{
						auto Kind = AllTokens[i].m_Kind;
						if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor)
							bContinuedString = false;
						else if (Kind == ECodeTokenKind::mc_Newline)
						{
							auto iNext = fp_NextCode(i);
							bContinuedString = iNext >= 0 && umint(iNext) <= umint(iLast) && fp_ContinuesLiteral(umint(iNext)) >= 0;
						}
					}

					if (bContinuedString)
						fp_OwnLineBefore(umint(iStart), nElementLevel);
				}

				// A group holding a string continued over lines, which keeps it from being joined, is opened, as one too long
				// for its line is: its comma on a line of its own and the group under it.
				if (bLevelled && bMovesCommas && iEnd > iElement && iElementFirst >= 0 && iLast >= 0 && umint(iElementFirst) < umint(iLast))
				{
					auto const &AllTokens = m_Tokens.f_GetTokens();
					auto iGroup = m_Structure.f_FindNodeOpeningAt(umint(iElementFirst));
					auto const *pGroup = iGroup < Nodes.f_GetLen() ? &Nodes[iGroup] : nullptr;
					bool bOpens = pGroup && pGroup->m_Kind == ECodeNodeKind::mc_Group && pGroup->m_iLastToken == umint(iLast);
					bool bContinuedString = false;
					for (auto i = umint(iElementFirst) + 1; bOpens && i < umint(iLast); ++i)
					{
						auto Kind = AllTokens[i].m_Kind;
						bOpens
							= Kind != ECodeTokenKind::mc_LineComment
							&& Kind != ECodeTokenKind::mc_BlockComment
							&& Kind != ECodeTokenKind::mc_Preprocessor
							&& (!AllTokens[i].m_bMultiLine || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_Whitespace)
							&& (i >= m_BlockEnds.f_GetLen() || m_BlockEnds[i] >= AllTokens.f_GetLen())
						;
						if (bOpens && Kind == ECodeTokenKind::mc_Newline)
						{
							auto iNext = fp_NextCode(i);
							bContinuedString |= iNext >= 0 && fp_ContinuesLiteral(umint(iNext)) >= 0;
						}
					}

					if (bOpens && bContinuedString)
					{
						if (umint(iElementFirst) != umint(iStart))
							fp_OwnLineBefore(umint(iStart), nElementLevel);

						fp_LayoutGroup(iGroup, nElementLevel, true);
						LaidOut.f_Insert(umint(iStart));
						LaidOut.f_Insert(umint(iLast));

						if (iSplit >= Node.m_SplitPoints.f_GetLen())
							break;

						iElement = iEnd;

						continue;
					}
				}

				if (bLevelled && bOneLine && bMovesCommas && iEnd > iElement)
				{
					// Comment lines above an element that starts its own line stand apart from it, and leave it to be laid out.
					auto const &AllTokens = m_Tokens.f_GetTokens();
					bool bTrivia = false;
					bool bOwnLine = fp_IsFirstOnLine(umint(iStart));
					for (auto i = iElement; i < umint(iStart) && !bTrivia; ++i)
					{
						auto Kind = AllTokens[i].m_Kind;
						bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
						bTrivia = (bComment && !(bOwnLine && fp_IsFirstOnLine(i))) || Kind == ECodeTokenKind::mc_Preprocessor;
					}

					umint iContent = umint(iStart);
					bool bComma = iSplit && m_Tokens.f_IsText(AllTokens[umint(iStart)], ",");
					if (bComma)
					{
						auto iBehind = fp_NextCode(umint(iStart));
						for (auto i = umint(iStart) + 1; iBehind >= 0 && i < umint(iBehind) && !bTrivia; ++i)
						{
							auto Kind = AllTokens[i].m_Kind;
							bTrivia = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment || Kind == ECodeTokenKind::mc_Preprocessor;
						}

						iContent = iBehind >= 0 ? umint(iBehind) : umint(iStart);
					}

					bool bJoinable = !bTrivia && iContent <= umint(iLast) && fp_IsRangeJoinable(_iNode, umint(iStart), umint(iLast));

					while (iChildCursor < Node.m_Children.f_GetLen() && Nodes[Node.m_Children[iChildCursor]].m_iFirstToken < umint(iStart))
						++iChildCursor;

					for (auto iChild = iChildCursor; iChild < Node.m_Children.f_GetLen() && bJoinable; ++iChild)
					{
						auto iChildNode = Node.m_Children[iChild];
						auto const &Child = Nodes[iChildNode];
						if (Child.m_iFirstToken > umint(iLast))
							break;

						bool bKeeps = iChildNode < m_KeepsListLines.f_GetLen() && m_KeepsListLines[iChildNode];
						if (Child.m_iLastToken <= umint(iLast) && (Child.m_Kind == ECodeNodeKind::mc_Block || bKeeps))
							bJoinable = false;
					}

					if (bJoinable)
					{
						if (!fp_IsFirstOnLine(umint(iStart)))
							fp_OwnLineBefore(umint(iStart), nElementLevel);
						else if (fp_GetStatementIndent(umint(iStart)) != nElementLevel)
							fp_IndentBefore(umint(iStart), nElementLevel);

						if (fp_FitsInline(umint(iStart), umint(iLast), nElementLevel))
							fp_MarkInline(umint(iStart), umint(iLast));
						else
							fp_LayoutRange(_iNode, umint(iStart), umint(iLast), nElementLevel, false, false);

						LaidOut.f_Insert(umint(iStart));
						LaidOut.f_Insert(umint(iLast));

						if (iSplit >= Node.m_SplitPoints.f_GetLen())
							break;

						iElement = iEnd;

						continue;
					}
				}

				// A comma behind the element that moves in front of the next one leaves the element ending its line, and
				// where the list moves its commas the ones behind take lines of their own: an element a comment above
				// keeps from being laid out stands at the level too.
				bool bEndsLine = (iLast >= 0 && fp_IsLastOnLine(umint(iLast))) || bMovesCommas;
				if (!bEndsLine && bMovesCommas && iLast >= 0 && iEnd < Node.m_iLastToken && fp_NextCode(umint(iLast)) == aint(iEnd))
					bEndsLine = m_Tokens.f_IsText(m_Tokens.f_GetTokens()[iEnd], ",") && fp_IsLastOnLine(iEnd);

				if (bElement && bLevelled && bEndsLine && fp_GetStatementIndent(umint(iStart)) != nElementLevel)
					fp_IndentBefore(umint(iStart), nElementLevel);

				// So does the first line of one written across lines, and its other lines move with it.
				bool bAcrossLines
					= !bElement
					&& bLevelled
					&& bEndsLine
					&& iStart >= 0
					&& iLast >= 0
					&& umint(iStart) < umint(iLast)
					&& fp_IsFirstOnLine(umint(iStart))
					&& fp_GetStatementIndent(umint(iStart)) != nElementLevel
				;

				// One broken only inside its groups that fits at the level is joined there, as the next pass finds it, its comma
				// leading it or not. Moved, one its comma leads keeps the place the source gives it.
				bool bJoinsAtLevel
					= bAcrossLines
					&& fp_IsBrokenInsideGroups(_iNode, umint(iStart), umint(iLast))
					&& fp_IsRangeJoinable(_iNode, umint(iStart), umint(iLast))
					&& fp_FitsInline(umint(iStart), umint(iLast), nElementLevel)
				;
				if (bJoinsAtLevel)
				{
					fp_IndentBefore(umint(iStart), nElementLevel);
					fp_MarkInline(umint(iStart), umint(iLast));
				}
				else if (bAcrossLines && !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iStart)], ","))
				{
					auto nDelta = aint(nElementLevel) - aint(fp_GetStatementIndent(umint(iStart)));
					fp_IndentBefore(umint(iStart), nElementLevel);
					for (auto i = umint(iStart) + 1; i <= umint(iLast); ++i)
					{
						auto Gap = EGap(m_GapState[i]);
						if (!fg_IsCodeToken(m_Tokens.f_GetTokens()[i]) || !fp_IsFirstOnLine(i) || (Gap != EGap::mc_Keep && Gap != EGap::mc_Indent))
							continue;

						auto nIndent = aint(fp_GetStatementIndent(i)) + nDelta;
						if (nIndent >= 0)
							fp_IndentBefore(i, umint(nIndent));
					}
				}

				// One a list moving its commas has written behind the comma ending the line in front takes that comma in
				// front of its first line, and its other lines move with that line: ', CFoo(a,' over '     b)'.
				bool bCommaAcrossLines
					= bLevelled
					&& bMovesCommas
					&& !bOneLine
					&& iSplit
					&& iStart >= 0
					&& iElementFirst >= 0
					&& iLast >= 0
					&& umint(iElementFirst) < umint(iLast)
					&& m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iStart)], ",")
					&& !fp_IsFirstOnLine(umint(iStart))
					&& fp_IsFirstOnLine(umint(iElementFirst))
					&& fp_NextCode(umint(iStart)) == iElementFirst
				;
				for (auto i = bCommaAcrossLines ? umint(iStart) + 1 : 0u; bCommaAcrossLines && i < umint(iElementFirst); ++i)
					bCommaAcrossLines = m_Tokens.f_GetTokens()[i].m_Kind == ECodeTokenKind::mc_Whitespace || m_Tokens.f_GetTokens()[i].m_Kind == ECodeTokenKind::mc_Newline;

				// Only one whose first line ends inside the argument list it opens: a braced element or a value behind a key
				// lays out its own lines.
				if (bCommaAcrossLines)
				{
					auto const &AllTokens = m_Tokens.f_GetTokens();
					auto iLineLast = umint(iElementFirst);
					for (auto i = umint(iElementFirst) + 1; i <= umint(iLast) && AllTokens[i].m_Kind != ECodeTokenKind::mc_Newline; ++i)
					{
						if (fg_IsCodeToken(AllTokens[i]))
							iLineLast = i;
					}

					auto iList = m_Structure.f_FindEnclosingNode(iLineLast);
					while (iList < Nodes.f_GetLen() && Nodes[iList].m_Kind != ECodeNodeKind::mc_Group)
						iList = Nodes[iList].m_iParent;

					auto const *pList = iList < Nodes.f_GetLen() ? &Nodes[iList] : nullptr;
					bCommaAcrossLines
						= pList && pList->m_Bracket == ECodeBracket::mc_Paren && pList->m_iFirstToken > umint(iElementFirst) && !fp_SpansLines(umint(iElementFirst), pList->m_iFirstToken)
					;
				}

				if (bCommaAcrossLines)
				{
					// The element's first token stands behind the ', '. A body in it is a block of statements, which stands at the
					// element's level.
					auto nDelta = aint(nElementLevel) + 2 - aint(fp_GetStatementIndent(umint(iElementFirst)));
					fp_OwnLineBefore(umint(iStart), nElementLevel);
					fp_MarkInline(umint(iStart), umint(iElementFirst));

					auto fInBody = [&](umint _iToken)
						{
							auto iClosed = m_Structure.f_FindNodeClosingAt(_iToken);
							if (iClosed < Nodes.f_GetLen() && Nodes[iClosed].m_Kind == ECodeNodeKind::mc_Block)
								return true;

							for (auto iInner = m_Structure.f_FindEnclosingNode(_iToken); iInner && iInner < Nodes.f_GetLen() && iInner != _iNode; iInner = Nodes[iInner].m_iParent)
							{
								if (Nodes[iInner].m_Kind == ECodeNodeKind::mc_Block)
									return true;
							}

							return false;
						}
					;

					for (auto i = umint(iElementFirst) + 1; i <= umint(iLast); ++i)
					{
						auto Gap = EGap(m_GapState[i]);
						if (!fg_IsCodeToken(m_Tokens.f_GetTokens()[i]) || !fp_IsFirstOnLine(i) || (Gap != EGap::mc_Keep && Gap != EGap::mc_Indent))
							continue;

						auto nIndent = aint(fp_GetStatementIndent(i)) + nDelta - (fInBody(i) ? 2 : 0);
						if (nIndent >= 0)
							fp_IndentBefore(i, umint(nIndent));
					}
				}

				// One broken only inside its groups is one written on one line as the next pass reads it, its comma moved in
				// front of it where the list moves its commas.
				bool bBrokenInside
					= !bElement
					&& bLevelled
					&& bMovesCommas
					&& iElementFirst >= 0
					&& iLast >= 0
					&& umint(iElementFirst) < umint(iLast)
					&& fp_IsFirstOnLine(umint(iElementFirst))
					&& fp_IsBrokenInsideGroups(_iNode, umint(iElementFirst), umint(iLast))
				;

				// The comma behind it ends no line where the list moves it in front of the next element.
				auto iBehindElement = iLast >= 0 ? fp_NextCode(umint(iLast)) : aint(-1);
				bool bCommaMoves = bLevelled && bMovesCommas && iBehindElement >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBehindElement)], ",");
				if ((bElement && (fp_IsLastOnLine(umint(iLast)) || bCommaMoves)) || bBrokenInside)
				{
					// So is one holding a lambda's body, which takes lines of its own wherever it fits. The
					// layout places the body, so only the head in front of it has to be joinable.
					aint iHeadLast = -1;
					bool bEndsInBody = false;

					while (iChildCursor < Node.m_Children.f_GetLen() && Nodes[Node.m_Children[iChildCursor]].m_iFirstToken <= umint(iStart))
						++iChildCursor;

					for (auto iChild = iChildCursor; iChild < Node.m_Children.f_GetLen() && iHeadLast < 0; ++iChild)
					{
						auto const &Child = Nodes[Node.m_Children[iChild]];
						if (Child.m_iLastToken > umint(iLast))
							break;

						if (Child.m_Kind == ECodeNodeKind::mc_Block)
						{
							iHeadLast = fp_PreviousCode(Child.m_iFirstToken);
							bEndsInBody = Child.m_iLastToken == umint(iLast);
						}
					}

					// Broken only inside its groups, only one ending in the body is laid out here: what goes on behind it, as the
					// call of a lambda called where it stands, is the groups' to join.
					auto nIndent = bBrokenInside ? nElementLevel : fp_GetStatementIndent(umint(iStart));
					bool bLayout
						= iHeadLast >= 0
						? umint(iHeadLast) >= umint(iStart) && fp_IsRangeJoinable(_iNode, umint(iStart), umint(iHeadLast)) && (!bBrokenInside || bEndsInBody)
						: !bBrokenInside && !fp_FitsInline(umint(iStart), umint(iLast), nIndent) && fp_IsRangeJoinable(_iNode, umint(iStart), umint(iLast))
					;
					if (bLayout && bBrokenInside)
						fp_OwnLineBefore(umint(iStart), nElementLevel);

					if (bLayout)
					{
						fp_LayoutRange(_iNode, umint(iStart), umint(iLast), nIndent, false, false);
						LaidOut.f_Insert(umint(iStart));
						LaidOut.f_Insert(umint(iLast));
					}
				}

				if (iSplit >= Node.m_SplitPoints.f_GetLen())
					break;

				iElement = iEnd;
			}
		}

		// The ranges laid out and the children both stand in order, so each range is passed once.
		umint iRange = 0;
		for (auto iChild : Node.m_Children)
		{
			auto const &Child = Nodes[iChild];
			while (iRange + 1 < LaidOut.f_GetLen() && LaidOut[iRange + 1] < Child.m_iFirstToken)
				iRange += 2;

			bool bLaidOut = iRange + 1 < LaidOut.f_GetLen() && Child.m_iFirstToken >= LaidOut[iRange] && Child.m_iLastToken <= LaidOut[iRange + 1];

			if (bLaidOut)
				continue;

			if (Child.m_Kind == ECodeNodeKind::mc_Block)
			{
				fp_LayoutLambdaHead(_iNode, iChild);

				// A body whose brace ends the line the construct keeps stands one level in from that line, its closing brace
				// at the line's level: '[](CFoo &_Foo) {' over 'return 1;' over '},'.
				auto iBrace = Child.m_iFirstToken;
				auto iInside = fp_NextCode(iBrace);
				bool bEndsLine = !fp_IsFirstOnLine(iBrace) && iInside >= 0 && umint(iInside) < Child.m_iLastToken && fp_IsFirstOnLine(umint(iInside));
				if (bEndsLine && EGap(m_GapState[iBrace]) == EGap::mc_Keep && fp_CanPlaceBlock(iChild))
				{
					// The line is the one the lambda's introducer starts, which its head's layout can still join the rest onto.
					auto iLineFirst = iBrace;
					for (auto iBefore = fp_PreviousCode(iLineFirst); iBefore >= 0; iBefore = fp_PreviousCode(iLineFirst))
					{
						auto const &Before = m_Tokens.f_GetTokens()[umint(iBefore)];
						if (m_TokenDepth[umint(iBefore)] != m_TokenDepth[iBrace] || m_Tokens.f_IsText(Before, ",") || m_Tokens.f_IsText(Before, "="))
							break;

						auto iGroup = m_Structure.f_FindNodeClosingAt(umint(iBefore));
						iLineFirst = iGroup < Nodes.f_GetLen() ? Nodes[iGroup].m_iFirstToken : umint(iBefore);
					}

					for (auto iBefore = fp_PreviousCode(iLineFirst); !fp_IsFirstOnLine(iLineFirst) && iBefore >= 0; iBefore = fp_PreviousCode(iLineFirst))
						iLineFirst = umint(iBefore);

					fp_LayoutBlockLines(iChild, fp_GetStatementIndent(iLineFirst));
				}

				fp_LayoutNode(iChild, fp_GetStatementIndent(Child.m_iFirstToken));
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
		// 'g_ActorSubscription(Actor) / [...]', and is laid out with it, an operator the
		// source left trailing the line above the capture list included.
		auto iLineFirst = iHeadFirst;

		auto fTrailsOperator = [&]
			{
				auto iOperator = fp_PreviousCode(iHeadFirst);
				if (iLineFirst != iHeadFirst || iOperator < 0 || umint(iOperator) <= Nodes[_iNode].m_iFirstToken)
					return false;

				// Only one that then leads the capture list's line takes it back; an opened list stays below.
				auto iOperatorLineFirst = umint(iOperator);
				while (!fp_IsFirstOnLine(iOperatorLineFirst) && fp_PreviousCode(iOperatorLineFirst) >= 0)
					iOperatorLineFirst = umint(fp_PreviousCode(iOperatorLineFirst));

				auto iCapture = m_Structure.f_FindNodeOpeningAt(iHeadFirst);
				if (iCapture >= Nodes.f_GetLen() || !fp_FitsInline(iHeadFirst, Nodes[iCapture].m_iLastToken, fp_GetStatementIndent(iOperatorLineFirst) + 2))
					return false;

				auto const &Operator = m_Tokens.f_GetTokens()[umint(iOperator)];
				if (Operator.m_Kind != ECodeTokenKind::mc_Punctuator || m_Structure.f_IsAngleBracket(umint(iOperator)))
					return false;

				constexpr ch8 const *c_pOperators[] =
					{
						"/"
						, "%"
						, ">"
						, "<<"
						, ">>"
						, "|"
						, "^"
						, "+"
						, "-"
					}
				;
				for (auto pOperator : c_pOperators)
				{
					if (m_Tokens.f_IsText(Operator, pOperator))
						return true;
				}

				return false;
			}
		;

		// A comma the layout moved in front of the element starts its line: ', [](...)'.
		while (!fp_IsFirstOnLine(iLineFirst) || EGap(m_GapState[iLineFirst]) == EGap::mc_Inline || fTrailsOperator())
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

		auto iPreviousBody = m_iRangeBody;
		m_iRangeBody = iBrace;
		fp_LayoutRange(_iNode, iLineFirst, umint(iHeadLast), nIndent, false, bCaptureBelow);
		m_iRangeBody = iPreviousBody;
	}

	// A member initializer list is its own line structure: one entry per line, each split
	// only when that entry does not fit. It is never folded onto the signature.
	umint CFormattingAnalyzer::fp_FindInitializerList(umint _iNode, umint _iFirstParen, umint _iLast) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		if (!_iFirstParen || Node.m_bObjectiveCMethod || Node.m_bObjectiveCHeader)
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
			{
				auto const &Child = Nodes[iChild];
				bInside |= i >= Child.m_iFirstToken && i <= Child.m_iLastToken;
			}

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
			{
				auto const &Child = Nodes[iChild];
				bInside |= i >= Child.m_iFirstToken && i <= Child.m_iLastToken;
			}

			if (!bInside && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], ","))
				Entries.f_Insert(i);
		}

		for (umint iEntry = 0; iEntry < Entries.f_GetLen(); ++iEntry)
		{
			auto iEntryFirst = Entries[iEntry];
			auto iEnd = iEntry + 1 < Entries.f_GetLen() ? umint(fp_PreviousCode(Entries[iEntry + 1])) : _iLast;

			// A directive or a comment inside an entry fixes its lines. Measuring such an
			// entry as one line makes it look far too wide and splits what already fits.
			if (!fp_IsRangeJoinable(_iNode, iEntryFirst, iEnd))
				continue;

			// An entry is a unit of its own: its scope markers align with it, as a call's
			// do inside a split expression, and a lambda body in it opens under it.
			fp_BreakBefore(iEntryFirst, _iIndent);
			fp_LayoutRange(_iNode, iEntryFirst, iEnd, _iIndent, false, false);
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
		// A leading group the layout has yet to decide is joined by its own layout where it fits on its line, which the
		// next pass reads as a statement that did not open at it.
		umint iLeadingClose = TCLimitsInt<umint>::mc_Max;

		auto fOpenedAtParen = [&]
			{
				if (iLeadingClose == TCLimitsInt<umint>::mc_Max || !fp_IsFirstOnLine(iLeadingClose))
					return false;

				return fp_IsBreakGap(iLeadingClose) || !fp_IsRangeJoinable(_iNode, Node.m_iFirstToken, iLeadingClose) || !fp_FitsInline(Node.m_iFirstToken, iLeadingClose, _iIndent);
			}
		;

		// The parenthesis a statement opens with, where it has one: '(a + b).f_Call();'.
		for (auto iChild : Node.m_Children)
		{
			// A message send the statement opens with is laid out the same way: '[pMenu addItem: pItem];'.
			auto const &Child = Nodes[iChild];
			bool bLeading = Child.m_Bracket == ECodeBracket::mc_Paren || (Child.m_Bracket == ECodeBracket::mc_Square && Child.m_bMessageSend);
			if (Child.m_Kind == ECodeNodeKind::mc_Group && bLeading && Child.m_iFirstToken == Node.m_iFirstToken)
				iLeadingClose = Child.m_iLastToken;
		}

		// A block always occupies its own lines, so only the head decides the statement's shape. A requires expression's
		// body in front of the body a requires clause constrains is part of the head; any other is the statement's body,
		// laid out as a lambda's is: 'concept cA = requires (t_C _Value)' over '{'.
		umint iHeadLast = Node.m_iLastToken;
		umint iHeadLastWithInitializers = Node.m_iLastToken;
		umint iBlock = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind != ECodeNodeKind::mc_Block)
				continue;

			// The body a requires clause constrains follows it, behind a constructor's initializers too.
			auto iBehind = fp_NextCode(Child.m_iLastToken);
			auto const *pBehind = iBehind >= 0 ? &Tokens[umint(iBehind)] : nullptr;
			bool bBodyBehind = pBehind && (m_Tokens.f_IsText(*pBehind, "{") || m_Tokens.f_IsText(*pBehind, ":"));
			for (auto iLater : Node.m_Children)
			{
				auto const &Later = Nodes[iLater];
				bBodyBehind |= Later.m_iFirstToken > Child.m_iLastToken && Later.m_Kind == ECodeNodeKind::mc_Block && !fp_IsRequirementsBody(_iNode, Later.m_iFirstToken);
			}

			if (bBodyBehind && fp_IsRequirementsBody(_iNode, Child.m_iFirstToken))
				continue;

			iBlock = iChild;
			auto iFirst = Child.m_iFirstToken;
			auto iPrevious = fp_PreviousCode(iFirst);
			iHeadLast = iPrevious >= 0 ? umint(iPrevious) : iFirst;
			iHeadLastWithInitializers = iHeadLast;

			break;
		}

		// An Objective-C section's header is a line of its own, which is joined and never split; one too long for it is
		// reported. Its instance variables' block is a body under it.
		if (Node.m_bObjectiveCHeader)
		{
			auto iHeadEnd = iBlock != TCLimitsInt<umint>::mc_Max ? iHeadLast : Node.m_iLastToken;
			if (fp_IsRangeJoinable(_iNode, Node.m_iFirstToken, iHeadEnd))
				fp_MarkInline(Node.m_iFirstToken, iHeadEnd);

			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, _iIndent, true);
				fp_LayoutNode(iBlock, _iIndent);
			}

			return;
		}

		// An Objective-C method's signature is one line where it fits and gives at each keyword part behind the first,
		// one level in, where it does not; its body is a function's.
		if (Node.m_bObjectiveCMethod)
		{
			auto iHeadEnd = iBlock != TCLimitsInt<umint>::mc_Max ? iHeadLast : Node.m_iLastToken;
			if (fp_FitsInline(Node.m_iFirstToken, iHeadEnd, _iIndent))
				fp_MarkInline(Node.m_iFirstToken, iHeadEnd);
			else
			{
				auto nLevel = m_TokenDepth[Node.m_iFirstToken];
				TCVector<umint> Parts;
				for (auto i = Node.m_iFirstToken; i <= iHeadEnd; ++i)
				{
					if (m_TokenDepth[i] != nLevel || Tokens[i].m_Kind != ECodeTokenKind::mc_Identifier)
						continue;

					auto iNext = fp_NextCode(i);
					auto iPrevious = fp_PreviousCode(i);
					if (iNext < 0 || iPrevious < 0 || !m_Tokens.f_IsText(Tokens[umint(iNext)], ":"))
						continue;

					if (Tokens[umint(iPrevious)].m_Kind == ECodeTokenKind::mc_Identifier)
						Parts.f_Insert(i);
				}

				auto iStart = Node.m_iFirstToken;
				for (auto iPart : Parts)
				{
					fp_MarkInline(iStart, umint(fp_PreviousCode(iPart)));
					fp_BreakBefore(iPart, _iIndent + nTab);
					iStart = iPart;
				}

				fp_MarkInline(iStart, iHeadEnd);
			}

			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, _iIndent, true);
				fp_LayoutNode(iBlock, _iIndent);
			}

			return;
		}

		// A statement that does not start its own line, such as one behind an attribute on
		// a clause's line, has no indentation of its own to lay anything out against.
		umint iFirstParenGroup = 0;
		umint iFirstParenGroupStart = 0;
		for (umint iIndex = 0; iIndex < Node.m_Children.f_GetLen(); ++iIndex)
		{
			auto iChild = Node.m_Children[iIndex];
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_Bracket == ECodeBracket::mc_Paren)
			{
				// An attribute's parenthesis stands among the specifiers: '__attribute__((weak)) f()'.
				auto iAttribute = fp_PreviousCode(Child.m_iFirstToken);
				bool bAttribute = false;
				for (auto pAttribute : {"__attribute__", "__attribute", "__declspec", "alignas"})
					bAttribute |= iAttribute >= 0 && m_Tokens.f_IsText(Tokens[umint(iAttribute)], pAttribute);

				if (bAttribute)
					continue;

				// A pointer to function's declarator names what the parameter list behind it declares:
				// 'NTSTATUS (NTAPI *m_fQuery)(IN HANDLE _Thread)'.
				if (iIndex + 1 < Node.m_Children.f_GetLen())
				{
					auto const &Next = Nodes[Node.m_Children[iIndex + 1]];
					bool bDeclaratorFirst
						= Next.m_Kind == ECodeNodeKind::mc_Group
						&& Next.m_Bracket == ECodeBracket::mc_Paren
						&& fp_NextCode(Child.m_iLastToken) == aint(Next.m_iFirstToken)
						&& fg_ClosesParameterList(m_Tokens, m_Structure, Next.m_iLastToken)
					;
					if (bDeclaratorFirst)
						iChild = Node.m_Children[iIndex + 1];
				}

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
			bool bIntroducer
				= fg_IsCaptureList(m_Tokens, m_Structure, umint(iBeforeParen)) || (m_Structure.f_IsAngleBracket(umint(iBeforeParen)) && fp_ClosesLambdaIntroducer(umint(iBeforeParen)))
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

			// An Objective-C block literal is introduced by its '^', alone or with its parameter list: '^(int _A) { ... }'.
			if (auto iBefore = fp_PreviousCode(iBrace); iBefore >= 0)
			{
				auto iCaret = iBefore;
				if (m_Tokens.f_IsText(Tokens[umint(iBefore)], ")"))
				{
					auto iGroup = m_Structure.f_FindNodeClosingAt(umint(iBefore));
					iCaret = iGroup < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iGroup].m_iFirstToken) : aint(-1);
				}

				bLambdaBody |= iCaret >= 0 && m_Tokens.f_IsText(Tokens[umint(iCaret)], "^");
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
		bool bOpensWithLambda
			= (m_Tokens.f_IsText(Tokens[Node.m_iFirstToken], "[") && !(iSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSecond)], "[")))
			|| fg_IsBlockLiteralCaret(m_Tokens, m_Structure, Node.m_iFirstToken)
		;
		bool bBodyIn = bLambdaBody && !bOpensWithLambda;

		// A guard's head cut by a conditional continues behind the directive with its parenthesis, one
		// level in from the guard, and the body behind it opens where the guard stands: 'for' '#if'
		// '(...)' '#else' '(...)' '#endif' '{'.
		umint nBodyIndent = _iIndent;
		if (m_Tokens.f_IsText(Tokens[Node.m_iFirstToken], "(") && Node.m_iParent < Nodes.f_GetLen())
		{
			auto iBlank = Node.m_iFirstToken;
			for (; iBlank; --iBlank)
			{
				auto Kind = Tokens[iBlank - 1].m_Kind;
				if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
					break;
			}

			umint iGuard = TCLimitsInt<umint>::mc_Max;
			for (auto iSibling : Nodes[Node.m_iParent].m_Children)
			{
				if (iSibling == _iNode)
					break;

				iGuard = iSibling;
			}

			bool bContinuesGuard = iBlank && Tokens[iBlank - 1].m_Kind == ECodeTokenKind::mc_Preprocessor && iGuard != TCLimitsInt<umint>::mc_Max && fp_IsGuard(iGuard);
			if (bContinuesGuard)
				nBodyIndent = fp_GetStatementIndent(Nodes[iGuard].m_iFirstToken);
		}

		// A name can end in a template argument list, and nothing before it is ever broken.
		// A lambda's own template parameter list ends the same way but names nothing.
		bool bNamed = bDeclarator || (iBeforeParen >= 0 && m_Structure.f_IsAngleBracket(umint(iBeforeParen)) && !fp_FollowsScope(umint(iBeforeParen)));

		// A trailing return type is one logical unit on its own line; its own scope markers
		// are only split when it does not fit there. A lambda writes one of its own and a
		// member access is spelled the same way, so the arrow only counts when nothing but
		// the function's qualifiers stands between it and the parameter list.
		umint iTrailingReturn = TCLimitsInt<umint>::mc_Max;
		if (bDeclarator && iFirstParenGroupStart && fg_ClosesParameterList(m_Tokens, m_Structure, iFirstParenGroup))
		{
			constexpr ch8 const *c_pQualifiers[] =
				{
					"const"
					, "volatile"
					, "noexcept"
					, "override"
					, "final"
					, "&"
					, "&&"
				}
			;

			bool bAfterQualifier = false;
			for (auto i = fp_NextCode(iFirstParenGroup); i >= 0 && umint(i) <= Node.m_iLastToken;)
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

			// A clause written across lines goes on where its line ends in a conjunction or the next starts with one.
			auto nLevel = m_TokenDepth[iDeclFirst];
			aint iClauseEnd = -1;
			for (auto iNext = fp_NextCode(iDeclFirst); iNext >= 0 && umint(iNext) <= iSignatureLast; iNext = fp_NextCode(umint(iNext)))
			{
				if (m_TokenDepth[umint(iNext)] != nLevel || !fp_IsFirstOnLine(umint(iNext)))
					continue;

				auto iBefore = fp_PreviousCode(umint(iNext));

				auto fConjunction = [&](aint _iToken)
					{
						auto const *pToken = _iToken >= 0 ? &Tokens[umint(_iToken)] : nullptr;

						return pToken && (m_Tokens.f_IsText(*pToken, "&&") || m_Tokens.f_IsText(*pToken, "||"));
					}
				;

				if (fConjunction(iBefore) || fConjunction(iNext))
					continue;

				iClauseEnd = iNext;

				break;
			}

			if (iClauseEnd < 0)
				break;

			// A clause on its own line behind a template header stands at the header's level or one level in, as the source
			// has it in or not.
			auto DeclGap = EGap(m_GapState[iDeclFirst]);
			bool bUndecided = DeclGap == EGap::mc_Keep || DeclGap == EGap::mc_Indent;
			bool bProtected = m_ProtectedStart[m_Lines.f_FindLine(Tokens[iDeclFirst].m_iOffset)];
			if (!m_bProbing && bUndecided && !bProtected && fp_IsFirstOnLine(iDeclFirst) && iDeclFirst != Node.m_iFirstToken)
			{
				auto nClause = fp_GetSourceLineIndent(iDeclFirst) > fp_GetSourceLineIndent(Node.m_iFirstToken) ? _iIndent + nTab : _iIndent;
				if (fp_GetStatementIndent(iDeclFirst) != nClause)
					fp_IndentBefore(iDeclFirst, nClause);
			}

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

				// A requires expression stands where an operand does, as in a concept's definition: '= cA<T> &&' over
				// 'requires (T _Value)'. It is no clause the declaration ends in.
				constexpr ch8 const *c_pOperandLeads[] =
					{
						"="
						, "("
						, ","
						, "||"
						, "&&"
						, "!"
						, "?"
						, ":"
						, "return"
						, "requires"
					}
				;

				auto iLead = fp_PreviousCode(umint(i));
				bool bExpression = false;
				for (auto pLead : c_pOperandLeads)
					bExpression |= iLead >= 0 && m_Tokens.f_IsText(Tokens[umint(iLead)], pLead);

				if (!bExpression && m_TokenDepth[umint(i)] == m_TokenDepth[iDeclFirst] && fp_IsFirstOnLine(umint(i)))
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

			// One line is the one the layout gives it: a clause broken only inside its brackets, which their layout joins,
			// counts as one already written on one line.
			bool bOneLineClause = iClause >= 0 && iClauseLast >= 0 && !m_Tokens.f_IsText(Tokens[umint(iClauseLast)], "requires");
			bool bWrittenOnOneLine = bOneLineClause;
			bool bBrokenInside = bOneLineClause;
			for (auto i = iClause >= 0 ? fp_NextCode(umint(iClause)) : aint(-1); bOneLineClause && i >= 0 && i <= iClauseLast; i = fp_NextCode(umint(i)))
			{
				if (!fp_IsFirstOnLine(umint(i)))
					continue;

				bWrittenOnOneLine = false;
				bBrokenInside &= m_TokenDepth[umint(i)] > m_TokenDepth[umint(iClause)];
			}

			if (!bWrittenOnOneLine)
				bOneLineClause = bBrokenInside;

			if (bOneLineClause && !bWrittenOnOneLine)
				bOneLineClause = fp_IsRangeJoinable(_iNode, umint(iClause), umint(iClauseLast)) && fp_FitsInline(umint(iClause), umint(iClauseLast), _iIndent + nTab);

			// The clause's line stands one level in from the declaration's.
			bool bUndecided = false;
			bool bProtected = false;
			if (iClause >= 0)
			{
				auto ClauseGap = EGap(m_GapState[umint(iClause)]);
				bUndecided = ClauseGap == EGap::mc_Keep || ClauseGap == EGap::mc_Indent;
				bProtected = m_ProtectedStart[m_Lines.f_FindLine(Tokens[umint(iClause)].m_iOffset)];
			}

			if (bUndecided && !bProtected && !m_bProbing && fp_GetStatementIndent(umint(iClause)) != _iIndent + nTab)
				fp_IndentBefore(umint(iClause), _iIndent + nTab);

			auto iBeforeClause = iClause >= 0 ? fp_PreviousCode(umint(iClause)) : aint(-1);
			if (bOneLineClause && iBeforeClause >= 0 && umint(iBeforeClause) >= iDeclFirst && !bElsewhere)
			{
				if (!bWrittenOnOneLine)
					fp_MarkInline(umint(iClause), umint(iClauseLast));

				iSignatureLast = umint(iBeforeClause);
				iHeadLast = iSignatureLast;
				bFixedLineBreaks = false;
			}

			// A requires expression's body in the clause opens under the clause's line: 'requires requires (T _A)' over '{'.
			for (auto iChild : Node.m_Children)
			{
				auto const &Body = Nodes[iChild];
				bool bInClause = iClause >= 0 && Body.m_iFirstToken > umint(iClause) && iClauseLast >= 0 && Body.m_iLastToken <= umint(iClauseLast);
				if (bInClause && Body.m_Kind == ECodeNodeKind::mc_Block && fp_IsRequirementsBody(_iNode, Body.m_iFirstToken) && fp_CanPlaceBlock(iChild))
				{
					fp_PlaceBody(_iNode, iChild, _iIndent + nTab, true);
					fp_LayoutNode(iChild, _iIndent + nTab);

					// What the clause goes on with behind it starts a line of its own: '}' over '&& cB<T>'.
					auto iTail = fp_NextCode(Body.m_iLastToken);
					if (iTail >= 0 && umint(iTail) <= umint(iClauseLast) && !fp_IsFirstOnLine(umint(iTail)))
						fp_BreakBefore(umint(iTail), _iIndent + nTab);
				}
			}

			// Requires expressions are operands, whose bodies are laid out as lambdas' are.
			if (iClause < 0 && iBlock != TCLimitsInt<umint>::mc_Max && fp_IsRequirementsBody(_iNode, Nodes[iBlock].m_iFirstToken))
				bFixedLineBreaks = false;
		}

		// A declaration on its template header's line is measured with the header, from the line's start.
		auto iElse = fp_LeadingElse(iDeclFirst);
		umint iLineFirst = iElse >= 0 ? umint(iElse) : iDeclFirst;
		bool bOnHeaderLine = iDeclFirst != Node.m_iFirstToken && !fp_IsFirstOnLine(iDeclFirst) && fp_IsFirstOnLine(Node.m_iFirstToken);
		if (bOnHeaderLine)
			iLineFirst = Node.m_iFirstToken;

		bool bJoinable = fp_IsRangeJoinable(_iNode, iDeclFirst, iSignatureLast) && !bFixedLineBreaks && Node.m_Kind != ECodeNodeKind::mc_Unsupported && fp_IsFirstOnLine(iLineFirst);
		if (iInitializerList != TCLimitsInt<umint>::mc_Max)
			iHeadLast = iSignatureLast;

		bool bFits = bJoinable && fp_FitsInline(iLineFirst, iHeadLast, _iIndent);
		// One that does not fit there starts a line of its own at the header's level.
		if (bOnHeaderLine && !bFits && !m_bProbing)
		{
			fp_BreakBefore(iDeclFirst, _iIndent);
			iLineFirst = iDeclFirst;
			bJoinable = fp_IsRangeJoinable(_iNode, iDeclFirst, iSignatureLast) && !bFixedLineBreaks && Node.m_Kind != ECodeNodeKind::mc_Unsupported;
			bFits = bJoinable && fp_FitsInline(iLineFirst, iHeadLast, _iIndent);
		}

		// A declaration its attributes lead keeps them on a line of their own, as a template's header keeps its, where what
		// stands in front of its first bracket does not fit behind them, and starts the next line at their level:
		// '__attribute__((weak))' over 'void f()'. Elsewhere the declaration is split at its brackets.
		if (bJoinable && !bFits)
		{
			auto iAttributesEnd = fp_FindLeadingAttributesEnd(iLineFirst, iSignatureLast);
			auto iRest = iAttributesEnd >= 0 ? fp_NextCode(umint(iAttributesEnd)) : aint(-1);
			auto iPrefixLast = iHeadLast;
			for (auto iChild : Node.m_Children)
			{
				auto const &Child = Nodes[iChild];
				if (iRest >= 0 && Child.m_iFirstToken > umint(iRest) && Child.m_iFirstToken <= iPrefixLast)
				{
					auto iBefore = fp_PreviousCode(Child.m_iFirstToken);
					iPrefixLast = iBefore >= 0 ? umint(iBefore) : iPrefixLast;

					break;
				}
			}

			bool bSplits = iRest >= 0 && fp_FitsInline(iLineFirst, umint(iAttributesEnd), _iIndent) && !fp_FitsInline(iLineFirst, iPrefixLast, _iIndent);
			if (bSplits)
			{
				if (!m_bProbing)
				{
					fp_MarkInline(iLineFirst, umint(iAttributesEnd));
					fp_BreakBefore(umint(iRest), _iIndent);
				}

				iDeclFirst = umint(iRest);
				iLineFirst = iDeclFirst;
				bJoinable = fp_IsRangeJoinable(_iNode, iDeclFirst, iSignatureLast) && !bFixedLineBreaks && Node.m_Kind != ECodeNodeKind::mc_Unsupported;
				bFits = bJoinable && fp_FitsInline(iLineFirst, iHeadLast, _iIndent);
			}
		}

		// With the project's TrailingScopeAtStatementLevel, a lambda's body that ends the statement's only
		// expression stands where the statement does, as a declaration's body does, and the terminator ends
		// its closing brace's line: '};'.
		auto const *pStyle = m_Request.m_pStyle.f_Get();
		bool bTrailingBody
			= bFits
			&& bBodyIn
			&& pStyle
			&& pStyle->m_bTrailingScopeAtStatementLevel
			&& m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";")
			&& fp_PreviousCode(Node.m_iLastToken) == aint(Nodes[iBlock].m_iLastToken)
		;
		if (bTrailingBody)
			bBodyIn = false;

		// The first phase only decides which return types move. A declaration that fits as
		// it stands has no reason to; one that does not is converted where that lets its
		// name fit, and the converted source is what gets laid out. Anything else is laid
		// out as the second phase will, so the bodies inside it are measured at the depth
		// they are moved to rather than the one the source gave them.
		// A head whose width no gap's spelling settles does not fit for want of a measure, not of room, and moves nothing:
		// 'void f() override sealed {}'.
		umint nHead = 0;
		bool bMeasured = fp_MeasureJoinedWidth(iLineFirst, iHeadLast, nHead);
		if (m_bProbing && bJoinable && !bFits && bMeasured && !fp_IsInFunctionBody(_iNode) && fp_ConvertTrailingReturn(_iNode, iDeclFirst, _iIndent))
		{
			if (iBlock != TCLimitsInt<umint>::mc_Max)
			{
				fp_PlaceBody(_iNode, iBlock, nBodyIndent, !bBodyIn);
				fp_LayoutNode(iBlock, nBodyIndent);
			}

			fp_ProbeBodies(_iNode, _iIndent);

			return;
		}

		// What an expression goes on with behind a lambda's body resumes under the body's
		// closing brace: an operator or a member access and what it takes, or the arguments
		// the lambda is called with at once. It is laid out
		// there like any other line, broken at its operators where it does not fit, and the
		// terminator of a statement that has such a tail takes a line of its own.
		// The declarators a type's definition declares stand behind its closing brace: '} CFoo;'.
		auto fLayoutDeclarators = [&]
			{
				if (bLambdaBody || iBlock == TCLimitsInt<umint>::mc_Max)
					return;

				auto iClose = Nodes[iBlock].m_iLastToken;
				if (iClose >= Node.m_iLastToken)
					return;

				auto iKey = aint(fp_SkipTemplateHeader(Node.m_iFirstToken));
				if (m_Tokens.f_IsText(Tokens[umint(iKey)], "typedef"))
					iKey = fp_NextCode(umint(iKey));

				// The specifiers of the variables it declares stand in front of the class key: 'static const struct {'.
				for (bool bSpecifier = true; bSpecifier && iKey >= 0;)
				{
					bSpecifier = false;
					for (auto pSpecifier : {"static", "const", "volatile", "constexpr", "constinit", "thread_local", "extern", "inline"})
						bSpecifier |= m_Tokens.f_IsText(Tokens[umint(iKey)], pSpecifier);

					if (bSpecifier)
						iKey = fp_NextCode(umint(iKey));
				}

				bool bClassKey = false;
				for (auto pKey : {"struct", "class", "union", "enum"})
					bClassKey |= iKey >= 0 && m_Tokens.f_IsText(Tokens[umint(iKey)], pKey);

				auto iTail = fp_NextCode(iClose);
				if (!bClassKey)
					return;

				// Ones the layout cannot relay out still have their inner constructs laid out: '} g_Foo[] =' over a list
				// written across lines.
				if (!fp_IsRangeJoinable(_iNode, umint(iTail), Node.m_iLastToken))
				{
					for (auto iChild : Node.m_Children)
					{
						auto const &Child = Nodes[iChild];
						if (Child.m_iFirstToken > iClose && Child.m_Kind != ECodeNodeKind::mc_Block)
							fp_JoinNode(iChild);
					}

					return;
				}

				if (fp_FitsInline(iClose, Node.m_iLastToken, _iIndent))
				{
					fp_MarkInline(iClose, Node.m_iLastToken);

					return;
				}

				// Too long for the brace's line, they are laid out as a declaration's head is, from that line, and the
				// terminator of one split takes a line of its own: '} g_Foo[] =' over the list over ';'.
				bool bTerminated = m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";");
				auto iRangeLast = bTerminated ? fp_PreviousCode(Node.m_iLastToken) : aint(Node.m_iLastToken);
				if (iRangeLast < iTail)
					return;

				bool bMustSplit = bTerminated && fp_FitsInline(iClose, umint(iRangeLast), _iIndent);
				fp_MarkInline(iClose, umint(iTail));
				bool bSplit = fp_LayoutRange(_iNode, iClose, umint(iRangeLast), _iIndent, false, true, bMustSplit);
				if (bSplit && bTerminated)
					fp_BreakBefore(Node.m_iLastToken, _iIndent);
			}
		;

		// The handlers of a function-try-block follow its body, each 'catch (...)' on a line of its own where the body opens
		// and its block a body like the first.
		auto fLayoutHandlers = [&]
			{
				if (bLambdaBody || iBlock == TCLimitsInt<umint>::mc_Max)
					return;

				auto iClose = Nodes[iBlock].m_iLastToken;
				for (auto iChild : Node.m_Children)
				{
					auto const &Handler = Nodes[iChild];
					if (Handler.m_Kind != ECodeNodeKind::mc_Block || Handler.m_iFirstToken <= iClose)
						continue;

					auto iCatch = fp_NextCode(iClose);
					auto iHeadEnd = fp_PreviousCode(Handler.m_iFirstToken);
					if (iCatch < 0 || iHeadEnd < iCatch || !m_Tokens.f_IsText(Tokens[umint(iCatch)], "catch"))
						return;

					fp_BreakBefore(umint(iCatch), nBodyIndent);
					fp_MarkInline(umint(iCatch), umint(iHeadEnd));
					fp_PlaceBody(_iNode, iChild, nBodyIndent, !bBodyIn);
					fp_LayoutNode(iChild, nBodyIndent);
					iClose = Handler.m_iLastToken;
				}
			}
		;

		auto fLayoutTail = [&]
			{
				fLayoutHandlers();
				fLayoutDeclarators();

				if (!bLambdaBody || iBlock == TCLimitsInt<umint>::mc_Max || !fp_IsFirstOnLine(Nodes[iBlock].m_iFirstToken))
					return;

				auto iTail = fp_NextCode(Nodes[iBlock].m_iLastToken);
				if (!bBodyIn)
				{
					// Under the brace of a statement that opens with its lambda, what it is
					// called with stands where the statement does, as written otherwise.
					bool bMoves = iTail >= 0 && umint(iTail) < Node.m_iLastToken && fp_IsFirstOnLine(umint(iTail)) && fp_GetStatementIndent(umint(iTail)) != _iIndent;
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

				// Each further body in the tail opens as the first one does, what stands between two bodies taking a line of
				// its own: '}' over '() && []' over '{'.
				for (auto iChild : Node.m_Children)
				{
					auto const &Body = Nodes[iChild];
					if (Body.m_Kind != ECodeNodeKind::mc_Block || Body.m_iFirstToken <= umint(iTail))
						continue;

					auto iSegmentLast = fp_PreviousCode(Body.m_iFirstToken);
					if (iSegmentLast < iTail || !fp_IsRangeJoinable(_iNode, umint(iTail), umint(iSegmentLast)) || !fp_CanPlaceBlock(iChild))
						return;

					fp_BreakBefore(umint(iTail), _iIndent + nTab);
					if (fp_FitsInline(umint(iTail), umint(iSegmentLast), _iIndent + nTab))
						fp_MarkInline(umint(iTail), umint(iSegmentLast));
					else
						fp_LayoutRange(_iNode, umint(iTail), umint(iSegmentLast), _iIndent + nTab, false, false);

					fp_PlaceBody(_iNode, iChild, _iIndent, false);
					fp_LayoutNode(iChild, _iIndent + nTab);

					iTail = fp_NextCode(Body.m_iLastToken);
					if (iTail >= 0 && umint(iTail) == Node.m_iLastToken && m_Tokens.f_IsText(Tokens[umint(iTail)], ";"))
						fp_BreakBefore(umint(iTail), _iIndent);

					if (iTail < 0 || umint(iTail) >= Node.m_iLastToken)
						return;
				}

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
				fp_PlaceBody(_iNode, iBlock, nBodyIndent, !bBodyIn);
				fp_LayoutNode(iBlock, nBodyIndent);
				fLayoutTail();

				// A lambda's terminator stands on a line of its own, at the statement's
				// indentation, where a declaration's stays behind its closing brace: '};'.
				// A body that could not be placed keeps its terminator as written too.
				auto const &Block = Nodes[iBlock];
				auto iLast = Node.m_iLastToken;
				bool bLambdaTerminator = bBodyIn && fp_IsFirstOnLine(Block.m_iFirstToken) && m_Tokens.f_IsText(Tokens[iLast], ";") && fp_PreviousCode(iLast) == aint(Block.m_iLastToken);
				if (bLambdaTerminator && !fp_IsFirstOnLine(iLast))
					fp_OwnLineBefore(iLast, _iIndent);
				else if (bLambdaTerminator && fp_GetStatementIndent(iLast) != _iIndent)
					fp_IndentBefore(iLast, _iIndent);
				else if (bTrailingBody)
					fp_MarkInline(Block.m_iLastToken, iLast);
			}

			return;
		}

		if (bJoinable)
		{
			auto const &DeclFirst = Tokens[iDeclFirst];
			bool bClause
				= m_Tokens.f_IsText(DeclFirst, "if")
				|| m_Tokens.f_IsText(DeclFirst, "for")
				|| m_Tokens.f_IsText(DeclFirst, "while")
				|| m_Tokens.f_IsText(DeclFirst, "switch")
				|| m_Tokens.f_IsText(DeclFirst, "catch")
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
				// A type too long for its own line that is one parenthesis behind its keyword, 'decltype(...)', gives there; any
				// other keeps the lines it is written on.
				auto iType = fp_NextCode(iTrailingReturn);
				auto iOpen = iType >= 0 ? fp_NextCode(umint(iType)) : aint(-1);
				auto iGroup = iOpen >= 0 && m_Tokens.f_IsText(Tokens[umint(iOpen)], "(") ? m_Structure.f_FindNodeOpeningAt(umint(iOpen)) : m_Structure.f_GetNodes().f_GetLen();
				bool bParenthesis = iGroup < m_Structure.f_GetNodes().f_GetLen() && m_Structure.f_GetNodes()[iGroup].m_iLastToken == iRangeLast;
				if (fp_FitsInline(iTrailingReturn, iRangeLast, _iIndent + nTab))
					fp_MarkInline(iTrailingReturn, iRangeLast);
				else if (bParenthesis)
				{
					m_iSplitTrailingReturn = TCLimitsInt<umint>::mc_Max;
					fp_MarkInline(iTrailingReturn, umint(iType));
					fp_LayoutRange(_iNode, umint(iType), iRangeLast, _iIndent + nTab, false, false);
				}

				bSplit = true;
			}

			m_iSplitFirstParen = 0;
			m_iSplitTrailingReturn = TCLimitsInt<umint>::mc_Max;

			// A split statement's terminator takes a line of its own, except behind a
			// declaration's body, where it stays on the closing brace's line: '};'.
			bool bBodyTerminator = iBlock != TCLimitsInt<umint>::mc_Max && !bBodyIn;
			bOpenedAtParen = fOpenedAtParen();

			// A scope the layout opened at the statement's level, with the project's TrailingScopeAtStatementLevel, has
			// its closing marker there as well, and the terminator ends that marker's line: ');'.
			auto iTrailingClose = bHasTerminator ? fp_PreviousCode(Node.m_iLastToken) : aint(-1);
			bool bTrailingScope
				= iTrailingClose >= 0
				&& pStyle
				&& pStyle->m_bTrailingScopeAtStatementLevel
				&& m_Structure.f_FindNodeClosingAt(umint(iTrailingClose)) < Nodes.f_GetLen()
				&& fp_IsBreakGap(umint(iTrailingClose))
				&& m_GapIndent[umint(iTrailingClose)] == _iIndent
			;

			// A string continued on the lines below spans them as a split does: '"a"' over '"b"' over ';'.
			for (auto i = iLineFirst + 1; !bSplit && i <= iSignatureEnd; ++i)
				bSplit = fp_IsFirstOnLine(i) && fp_ContinuesLiteral(i) >= 0;

			if (bTrailingScope)
				fp_MarkInline(umint(iTrailingClose), Node.m_iLastToken);
			else if (bSplit && bHasTerminator && !bBodyTerminator && !bOpenedAtParen)
				fp_BreakBefore(Node.m_iLastToken, _iIndent);
		}

		bOpenedAtParen = fOpenedAtParen();
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
			// expression's body opens one level in wherever the source put its brace.
			for (auto iChild : Node.m_Children)
			{
				if (Nodes[iChild].m_Kind != ECodeNodeKind::mc_Block)
					fp_JoinNode(iChild);
				else if (fp_IsRequirementsBody(_iNode, Nodes[iChild].m_iFirstToken))
				{
					fp_PlaceBody(_iNode, iChild, _iIndent, false);
					fp_LayoutNode(iChild, _iIndent + nTab);
				}
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
				fp_PlaceBody(_iNode, iBlock, nBodyIndent, !bBodyIn);
			else if (bJoinable && !m_bOperatorSplit)
			{
				// Behind an opaque directive a line is only ever indented, as a block it moves with indents it:
				// 'for' '#if' '(...)' '#endif' '{'.
				auto nBrace = bBodyIn ? nBodyIndent + nTab : nBodyIndent;
				if (fp_FollowsOpaqueDirective(iBrace))
					fp_IndentBefore(iBrace, nBrace);
				else
					fp_BreakBefore(iBrace, nBrace);
			}
			else if (bJoinable && bBodyIn)
			{
				// A body already on a line of its own behind a split head stands at the level of the line its lambda's
				// introducer ends on: '> fg_Move(Promise) / [this]' over '{', and 'fg_Call() > [this]' over
				// '(CResult &&_Result)' over '{'.
				umint iCapture = TCLimitsInt<umint>::mc_Max;
				for (auto iChild : Node.m_Children)
				{
					auto const &Child = Nodes[iChild];
					if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_Bracket == ECodeBracket::mc_Square && Child.m_iLastToken < iBrace)
					{
						if (fp_ClosesLambdaIntroducer(Child.m_iLastToken))
							iCapture = Child.m_iFirstToken;
					}
				}

				aint iLine = fp_PreviousCode(iBrace);
				while (iLine > aint(Node.m_iFirstToken) && !fp_IsBreakGap(umint(iLine)))
					iLine = fp_PreviousCode(umint(iLine));

				bool bIntroducerLine = iCapture != TCLimitsInt<umint>::mc_Max && iLine > aint(Node.m_iFirstToken);
				auto nPlace = bIntroducerLine ? m_GapIndent[umint(iLine)] : 0;
				if (bIntroducerLine && fp_GetStatementIndent(iBrace) != nPlace && fp_CanPlaceBlock(iBlock))
				{
					fp_IndentBefore(iBrace, nPlace);
					fp_ShiftBlock(iBlock, aint(nPlace) - aint(fp_GetSourceLineIndent(iBrace)));
				}
			}

			fp_LayoutNode(iBlock, nBodyIndent);
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

		// A '&&' is also a member function's reference qualifier, which a requires clause in parentheses can follow, but a
		// clause never ends in a brace: '&& requires {'.
		auto iLead = fp_PreviousCode(umint(iBeforeBrace));
		bool bBraceBehind = umint(iBeforeBrace) == umint(fp_PreviousCode(_iBrace));
		if (iLead >= 0 && bBraceBehind && m_Tokens.f_IsText(Tokens[umint(iLead)], "&&"))
			return true;

		constexpr ch8 const *c_pOperandLeads[] =
			{
				"="
				, "("
				, ","
				, "||"
				, "!"
				, "?"
				, ":"
				, "return"
				, "requires"
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
		if (iBrace == Nodes[_iNode].m_iFirstToken)
			return;

		// An operator split places the brace on its continuation line, unless it left it sharing a line undecided.
		if (m_bOperatorSplit && (fp_IsFirstOnLine(iBrace) || EGap(m_GapState[iBrace]) != EGap::mc_Keep))
			return;

		auto nTab = m_Request.m_Settings.m_nTabWidth;
		auto nPlace = _bDeclarator ? _iIndent : _iIndent + nTab;
		// The lines inside were written against the brace's line, which stands where the head does unless something else
		// starts it: '[]()' over '/* A */ {'.
		if (!fp_IsFirstOnLine(iBrace))
		{
			auto nLineOffset = aint(fp_GetSourceLineIndent(iBrace)) - aint(fp_GetSourceLineIndent(Nodes[_iNode].m_iFirstToken));
			fp_PlaceBlock(_iBlock, nPlace, umint(fg_Max(aint(_iIndent) + nLineOffset, aint(0))));

			return;
		}

		// A body that already has a line of its own still takes the depth its head gives
		// it, and its lines move with it, so that a lambda written one level too far out
		// is brought back under the expression it belongs to. They move by the distance from
		// where the source wrote the brace, which the head's layout may have placed already.
		// The move of a block around it can have placed the brace already, where it stands for the source's column.
		auto nReference = fp_GetSourceLineIndent(iBrace);
		if ((nReference == nPlace && fp_GetStatementIndent(iBrace) == nPlace) || !fp_CanPlaceBlock(_iBlock))
			return;

		// A brace behind the end of a comment over several lines stays there, its line's indentation the comment's: ' b */ {'.
		auto const &AllTokens = m_Tokens.f_GetTokens();
		auto iBraceLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(AllTokens[iBrace].m_iOffset));
		for (auto iByte = iBraceLineStart; iByte < AllTokens[iBrace].m_iOffset; ++iByte)
		{
			if (!fg_IsSpaceOrTab(m_pSource[iByte]))
				return;
		}

		if (fp_GetStatementIndent(iBrace) != nPlace)
			fp_IndentBefore(iBrace, nPlace);

		if (nReference != nPlace)
			fp_ShiftBlock(_iBlock, aint(nPlace) - aint(nReference));
	}

	bool CFormattingAnalyzer::fp_FollowsOpaqueDirective(umint _iToken) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto i = _iToken; i; --i)
		{
			auto Kind = Tokens[i - 1].m_Kind;
			if (Kind == ECodeTokenKind::mc_Preprocessor)
				return m_OpaqueDirective[i - 1];

			if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
				return false;
		}

		return false;
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

	// Whether a comment can continue a trailing line comment on a line of its own below it: a line comment, or a block
	// comment on one line that ends its line.
	bool CFormattingAnalyzer::fp_IsContinuationComment(umint _iComment) const
	{
		auto const &Comment = m_Tokens.f_GetTokens()[_iComment];
		if (Comment.m_Kind == ECodeTokenKind::mc_LineComment)
			return true;

		return Comment.m_Kind == ECodeTokenKind::mc_BlockComment && !Comment.m_bMultiLine && fp_IsLastOnLine(_iComment);
	}

	// Whether the block's lines can follow a move. A multiline token spells its own lines, so
	// they cannot be brought to a new depth; a string literal's are its value, which a move leaves
	// as it is. The lines around an opaque directive keep their
	// depths relative to each other, which a move as a whole leaves them, and a directive is
	// written by its own rule either way.
	bool CFormattingAnalyzer::fp_CanPlaceBlock(umint _iBlock) const
	{
		auto const &Block = m_Structure.f_GetNodes()[_iBlock];
		if (!Block.m_bHasMultiLineToken)
			return true;

		auto const &Tokens = m_Tokens.f_GetTokens();
		for (auto i = Block.m_iFirstToken; i <= Block.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_bMultiLine && fg_IsCodeToken(Token) && Token.m_Kind != ECodeTokenKind::mc_StringLiteral)
				return false;
		}

		return true;
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
			auto const &Token = Tokens[i];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
			{
				if (!fp_IsFirstOnLine(i))
					continue;

				auto nSource = fp_GetSourceLineIndent(i);
				auto nIndent = aint(nSource) + _nDelta;
				if (nIndent < 0)
					continue;

				m_CommentMoved[i] = 1;
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

			// A gap the layout decided, broken or joined, is not the move's to write.
			if (fp_IsBreakGap(i) || EGap(m_GapState[i]) == EGap::mc_Inline)
				continue;

			// A line behind a directive starts with the token as well.
			auto iLineStart = m_Lines.f_GetLineStart(m_Lines.f_FindLine(Token.m_iOffset));
			bool bStartsLine = true;
			for (auto iByte = iLineStart; iByte < Token.m_iOffset && bStartsLine; ++iByte)
				bStartsLine = fg_IsSpaceOrTab(m_pSource[iByte]);

			if (!bStartsLine)
				continue;

			auto nIndent = aint(fp_GetSourceLineIndent(i)) + _nDelta;
			if (nIndent >= 0)
				fp_IndentBefore(i, umint(nIndent));
		}
	}
}
