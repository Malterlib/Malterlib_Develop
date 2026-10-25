// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace NMib::NDevelop::NPrivate
{
	// Writes a declaration's specifiers in one order where the sources have two: 'static'
	// in front of 'constexpr'. Only what a run of specifiers is made of may stand between
	// the two, with nothing but spaces and line breaks around it, so what moves is a
	// specifier among its like and never a word of something else.
	void CFormattingAnalyzer::fp_ConvertSpecifiers()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		constexpr ch8 const *c_pSpecifiers[] =
			{
				"inline"
				, "constinit"
				, "extern"
				, "virtual"
				, "friend"
				, "thread_local"
				, "mutable"
				, "explicit"
			}
		;

		for (umint i = 0; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind != ECodeTokenKind::mc_Identifier || !m_Tokens.f_IsText(Token, "constexpr"))
				continue;

			// A macro's argument can paste to or stringize its words: 'DDeclare(constexpr static)' with 'S##_type'. An argument
			// holding a brace it does not close leaves the structure incomplete, and the call unseen: 'DStr({ constexpr static)'.
			if (!m_Structure.f_IsComplete() || fp_IsInMacroArgument(i))
				continue;

			umint iStatic = TCLimitsInt<umint>::mc_Max;
			bool bQualifierInFront = false;
			for (auto iNext = fp_NextCode(i); iNext >= 0; iNext = fp_NextCode(umint(iNext)))
			{
				auto const &Next = Tokens[umint(iNext)];
				if (m_Tokens.f_IsText(Next, "static"))
				{
					iStatic = umint(iNext);

					break;
				}

				bQualifierInFront |= m_Tokens.f_IsText(Next, "const") || m_Tokens.f_IsText(Next, "volatile");

				// A qualifier among the specifiers is one the qualifier conversion moves behind the type in the same stage.
				bool bSpecifier = m_Tokens.f_HasRole(Next, ECodeNameRole::mc_SpecifierMacro) || m_Tokens.f_IsText(Next, "const") || m_Tokens.f_IsText(Next, "volatile");
				for (auto pSpecifier : c_pSpecifiers)
					bSpecifier |= m_Tokens.f_IsText(Next, pSpecifier);

				if (!bSpecifier)
					break;
			}

			if (iStatic == TCLimitsInt<umint>::mc_Max)
				continue;

			auto iBehind = fp_NextCode(iStatic);
			if (iBehind < 0)
				continue;

			// Qualifiers on both sides of the 'static' come together once it moves, which the qualifier conversion, refusing
			// them apart, would then move in a pass of its own: 'constexpr const static volatile int'.
			if (bQualifierInFront && (m_Tokens.f_IsText(Tokens[umint(iBehind)], "const") || m_Tokens.f_IsText(Tokens[umint(iBehind)], "volatile")))
				continue;

			bool bPlain = true;
			for (umint iGap = i; iGap < umint(iBehind) && bPlain; ++iGap)
			{
				auto const &Gap = Tokens[iGap];
				bPlain = fg_IsCodeToken(Gap) || Gap.m_Kind == ECodeTokenKind::mc_Whitespace || Gap.m_Kind == ECodeTokenKind::mc_Newline;
			}

			if (!bPlain)
				continue;

			auto iRemove = Tokens[iStatic].m_iOffset;
			auto nRemove = Tokens[umint(iBehind)].m_iOffset - iRemove;
			auto iInsert = Token.m_iOffset;
			auto nInsert = Token.m_nLength;
			if
			(
				fp_IsDisabled(iInsert, nInsert)
				|| fp_IsDisabled(iRemove, nRemove)
				|| fp_IsSuppressed(iInsert, nInsert)
				|| fp_IsSuppressed(iRemove, nRemove)
				|| !fp_IsConversionSelected(iInsert, nInsert, iRemove, nRemove)
			)
			{
				continue;
			}

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

	// Whether the token stands in the argument of a call to a macro the naming lists, which can place it anywhere, paste to
	// it or turn it into a string, and so is a call behind a chain of calls one of which is a macro's, which can expand to
	// another macro's name: 'DGet()(const int)'. Out of scope by decision: the layout changes the whitespace in an argument
	// as anywhere else, which a stringizing macro writes into its string where it adds or takes out a blank between two tokens.
	bool CFormattingAnalyzer::fp_IsInMacroArgument(umint _iToken)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		auto fIsMacroCall = [&](umint _iOpen)
			{
				auto iCallee = fp_PreviousCode(_iOpen);
				if (iCallee < 0 || Tokens[umint(iCallee)].m_Kind != ECodeTokenKind::mc_Identifier)
					return false;

				auto const &Callee = Tokens[umint(iCallee)];
				return m_Tokens.f_HasRole(Callee, ECodeNameRole::mc_Macro) || m_Tokens.f_HasRole(Callee, ECodeNameRole::mc_SpecifierMacro);
			}
		;

		for (auto iGroup = m_Structure.f_FindEnclosingNode(_iToken); iGroup && iGroup < Nodes.f_GetLen(); iGroup = Nodes[iGroup].m_iParent)
		{
			auto const &Group = Nodes[iGroup];
			if (Group.m_Kind != ECodeNodeKind::mc_Group || !m_Tokens.f_IsText(Tokens[Group.m_iFirstToken], "("))
				continue;

			// Back along the chain of calls in front, any of which can expand to the next one's name: 'DGet()()(const int)'.
			for (auto iOpen = Group.m_iFirstToken;;)
			{
				if (fIsMacroCall(iOpen))
					return true;

				auto iBefore = fp_PreviousCode(iOpen);
				if (iBefore < 0 || !m_Tokens.f_IsText(Tokens[umint(iBefore)], ")"))
					break;

				auto iCall = m_Structure.f_FindNodeClosingAt(umint(iBefore));
				if (iCall >= Nodes.f_GetLen())
					break;

				iOpen = Nodes[iCall].m_iFirstToken;
			}
		}

		return false;
	}

	// Takes out a statement terminator that ends nothing: the second ';' of 'f_Call();;'
	// is an empty statement of its own. Only one standing alone as a statement goes, never
	// a separator of 'for (;;)', and only where nothing but spaces, line breaks and comments
	// stand between it and the terminator in front of it. Comments stay where they are: the
	// layout can move one from between the two, so whether it stands there decides nothing.
	void CFormattingAnalyzer::fp_ConvertEmptyStatements()
	{
		auto const &Tokens = m_Tokens.f_GetTokens();

		// A structure cut by a macro or a conditional can make a terminator a separator: 'DLoop;;)' with 'for (int i = 0'.
		if (!m_Structure.f_IsComplete())
			return;

		auto fIsSpace = [&](umint _iToken)
			{
				return Tokens[_iToken].m_Kind == ECodeTokenKind::mc_Whitespace;
			}
		;

		umint iLastRemove = TCLimitsInt<umint>::mc_Max;
		for (auto const &Node : m_Structure.f_GetNodes())
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Statement || Node.m_iFirstToken != Node.m_iLastToken || !m_Tokens.f_IsText(Tokens[Node.m_iFirstToken], ";"))
				continue;

			auto iEmpty = Node.m_iFirstToken;
			auto iBefore = fp_PreviousCode(iEmpty);
			if (iBefore < 0 || !m_Tokens.f_IsText(Tokens[umint(iBefore)], ";"))
				continue;

			if (fp_IsInMacroArgument(iEmpty))
				continue;

			// A macro in front can open what the terminators separate in, 'DBegin;;DEnd' with 'for (' and ')'. Out of scope by
			// decision: such macros are contrived, and keeping the terminator behind every macro would leave the stray ones
			// behind ordinary macro statements, 'DMibCheck(a);;'.

			// In a GNU statement expression the last statement gives its type, which a macro expanding to nothing can leave the
			// empty one to be: '({ 1;; DEmpty })'.
			auto iBlock = Node.m_iParent;
			if (iBlock && iBlock < m_Structure.f_GetNodes().f_GetLen())
			{
				auto iOpenBefore = fp_PreviousCode(m_Structure.f_GetNodes()[iBlock].m_iFirstToken);
				if (iOpenBefore >= 0 && m_Tokens.f_IsText(Tokens[umint(iOpenBefore)], "("))
					continue;
			}

			bool bPlain = true;
			bool bComment = false;
			for (auto iGap = umint(iBefore) + 1; iGap < iEmpty && bPlain; ++iGap)
			{
				auto Kind = Tokens[iGap].m_Kind;
				bComment |= Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
				bPlain = Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
			}

			if (!bPlain)
				continue;

			// The last statement of a GNU statement expression gives its type, which an empty one makes 'void': '({ 1;; })'.
			// Behind a directive the next statement can be one a configuration leaves out.
			bool bDirectiveBehind = false;
			for (auto iAfter = iEmpty + 1; iAfter < Tokens.f_GetLen() && !bDirectiveBehind; ++iAfter)
			{
				bDirectiveBehind = Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Preprocessor;
				if (fg_IsCodeToken(Tokens[iAfter]))
					break;
			}

			if (bDirectiveBehind)
				continue;

			auto iClose = fp_NextCode(iEmpty);
			auto iAfterClose = iClose >= 0 ? fp_NextCode(umint(iClose)) : aint(-1);
			if (iAfterClose >= 0 && m_Tokens.f_IsText(Tokens[umint(iClose)], "}") && m_Tokens.f_IsText(Tokens[umint(iAfterClose)], ")"))
				continue;

			auto iRemove = Tokens[umint(iBefore)].f_GetEnd();
			auto iRemoveEnd = Tokens[iEmpty].f_GetEnd();

			auto fOverlapsLast = [&]
				{
					return iLastRemove != TCLimitsInt<umint>::mc_Max && iRemove < m_Structural[iLastRemove].f_GetEnd();
				}
			;

			umint iPrevious = iEmpty - 1;
			while (fIsSpace(iPrevious))
				--iPrevious;

			umint iNext = iEmpty + 1;
			while (iNext < Tokens.f_GetLen() && fIsSpace(iNext))
				++iNext;

			// The terminator in front of this one can be an empty statement taken out with its line, so this one goes
			// with its line too, or, sharing that line, with the text taken out. A comment behind one starting its line
			// keeps the line, rather than coming to trail the code in front.
			bool bOverlaps = fOverlapsLast();
			bool bFirstOnLine = Tokens[iPrevious].m_Kind == ECodeTokenKind::mc_Newline;
			auto const *pNext = iNext < Tokens.f_GetLen() ? &Tokens[iNext] : nullptr;
			bool bCommentBehind = pNext && (pNext->m_Kind == ECodeTokenKind::mc_LineComment || pNext->m_Kind == ECodeTokenKind::mc_BlockComment);
			if (bComment || bOverlaps || (bFirstOnLine && bCommentBehind))
			{
				// A block comment in front of it on its line would come to end the line, which converts it.
				if (bFirstOnLine)
				{
					iRemove = Tokens[iPrevious].f_GetEnd();
					if (iNext == Tokens.f_GetLen())
						iRemoveEnd = Tokens.f_GetLast().f_GetEnd();
					else if (Tokens[iNext].m_Kind == ECodeTokenKind::mc_Newline)
						iRemoveEnd = Tokens[iNext].f_GetEnd();
					else
					{
						iRemove = Tokens[iEmpty].m_iOffset;
						iRemoveEnd = Tokens[iNext].m_iOffset;
					}
				}
				else if (!bOverlaps)
					continue;
			}

			auto nRemove = iRemoveEnd - iRemove;
			if (fp_IsDisabled(iRemove, nRemove) || fp_IsSuppressed(iRemove, nRemove) || !fp_IsSelected(iRemove, nRemove))
				continue;

			if (fOverlapsLast())
			{
				auto &Last = m_Structural[iLastRemove];
				Last.m_nLength = fg_Max(Last.f_GetEnd(), iRemoveEnd) - Last.m_iOffset;
				continue;
			}

			iLastRemove = m_Structural.f_GetLen();
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
		for (umint iNode = 0; iNode < Nodes.f_GetLen(); ++iNode)
		{
			if (m_Structure.f_IsEnumBody(iNode))
				fp_ConvertSeparatedCommas(Nodes[iNode], true);
		}

		// A comment behind a list's comma describes the element in front of it, so the comma
		// moves in front of the element behind it instead, as the layout puts it: 'a, // A'
		// over 'b' is 'a // A' over ', b'.
		// A braced list holding a directive has its commas moved across the directives as an enum's are. A list
		// its layout directive owns has its commas moved all the same, since its rows are written with them in front.
		auto fKeepsCommas = [&](CCodeNode const &_List)
			{
				if (_List.m_Bracket != ECodeBracket::mc_Brace)
					return false;

				for (auto i = _List.m_iFirstToken; i < _List.m_iLastToken; ++i)
				{
					if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
						return true;
				}

				return false;
			}
		;

		// Whether a statement is too long for its line, measured once for all the lists in it: a table's rows each ask.
		TCMap<umint, bool> StatementTooLong;
		for (auto const &Node : Nodes)
		{
			if (Node.m_Kind != ECodeNodeKind::mc_Group)
				continue;

			if (fKeepsCommas(Node))
			{
				if (fp_SpansLines(Node.m_iFirstToken, Node.m_iLastToken))
					fp_ConvertSeparatedCommas(Node, false);

				continue;
			}

			// A comma ending a braced list written across lines separates nothing, and goes. So does one ending a list on
			// one line in a statement too long for its line, which the layout may open: whether the list ends up across lines
			// is not known until then, and the next pass would take the comma out of an opened one.
			bool bTrailingComma
				= Node.m_Bracket == ECodeBracket::mc_Brace
				&& !Node.m_SplitPoints.f_IsEmpty()
				&& m_Tokens.f_IsText(Tokens[Node.m_SplitPoints.f_GetLast()], ",")
				&& fp_NextCode(Node.m_SplitPoints.f_GetLast()) == aint(Node.m_iLastToken)
			;
			bool bMayOpen = bTrailingComma && fp_SpansLines(Node.m_iFirstToken, Node.m_iLastToken);

			// A block in the list, a lambda's body, takes lines of its own, which opens the list around it.
			for (auto i = Node.m_iFirstToken + 1; bTrailingComma && !bMayOpen && i < Node.m_iLastToken; ++i)
			{
				if (!m_Tokens.f_IsText(Tokens[i], "{"))
					continue;

				auto iOpened = m_Structure.f_FindNodeOpeningAt(i);
				bMayOpen = iOpened < Nodes.f_GetLen() && Nodes[iOpened].m_Kind == ECodeNodeKind::mc_Block;
			}

			if (bTrailingComma && !bMayOpen)
			{
				auto iStatement = Node.m_iParent;
				while (iStatement && iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind != ECodeNodeKind::mc_Statement)
					iStatement = Nodes[iStatement].m_iParent;

				umint nJoined = 0;
				auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;
				if (auto pTooLong = StatementTooLong.f_FindEqual(iStatement))
					bMayOpen = *pTooLong;
				else if (iStatement && iStatement < Nodes.f_GetLen() && nMaxColumns)
				{
					auto const &Statement = Nodes[iStatement];
					umint nBlocks = 0;
					for (auto iNode = Statement.m_iParent; iNode && iNode < Nodes.f_GetLen(); iNode = Nodes[iNode].m_iParent)
						nBlocks += Nodes[iNode].m_Kind == ECodeNodeKind::mc_Block;

					// Behind a type's body the list stands on the line its closing brace starts: '} g_Foo = {1, 2,};'.
					auto iLineFirst = Statement.m_iFirstToken;
					for (auto iChild : Statement.m_Children)
					{
						auto const &Child = Nodes[iChild];
						if (Child.m_Kind == ECodeNodeKind::mc_Block && Child.m_iLastToken < Node.m_iFirstToken)
							iLineFirst = Child.m_iLastToken;
					}

					// The continuation a lambda's body in an expression takes is not counted, so a list there within a few columns of
					// the limit can keep its comma until the layout opens it, which the next pass then takes out.
					auto nIndent = fg_Max(fp_GetSourceLineIndent(Statement.m_iFirstToken), nBlocks * m_Request.m_Settings.m_nTabWidth);
					bMayOpen = !fp_MeasureJoinedWidth(iLineFirst, Statement.m_iLastToken, nJoined) || nIndent + nJoined > nMaxColumns;
					StatementTooLong[iStatement] = bMayOpen;
				}
			}

			if (bMayOpen)
			{
				auto iComma = Node.m_SplitPoints.f_GetLast();
				auto iBefore = fp_PreviousCode(iComma);

				// One behind a block comment stays: taking it out would leave the comment ending the line, for the next
				// pass to convert. A comment trailing the element goes with the element, and is converted with the comma gone.
				bool bBehindComment = false;
				for (auto i = iBefore >= 0 ? umint(iBefore) + 1 : iComma; i < iComma; ++i)
					bBehindComment |= Tokens[i].m_Kind == ECodeTokenKind::mc_BlockComment && !fp_IsTrailingBlockComment(i);

				bool bRemove
					= m_Tokens.f_IsText(Tokens[iComma], ",")
					&& fp_NextCode(iComma) == aint(Node.m_iLastToken)
					&& iBefore > aint(Node.m_iFirstToken)
					&& !bBehindComment
					&& !fp_MayBeMacroArgument(Node)
					&& !fp_IsInMacroArgument(Node.m_iFirstToken)
				;
				if (bRemove)
				{
					auto iRemove = Tokens[iComma].m_iOffset;
					bool bTight = true;
					for (auto i = umint(iBefore) + 1; i < iComma && bTight; ++i)
						bTight = Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace;

					if (bTight)
						iRemove = Tokens[umint(iBefore)].f_GetEnd();

					auto nRemove = Tokens[iComma].f_GetEnd() - iRemove;
					if (!fp_IsFormatOff(iRemove, nRemove) && !fp_IsSuppressed(iRemove, nRemove) && fp_IsSelected(iRemove, nRemove))
					{
						auto &Remove = m_Structural.f_Insert();
						Remove.m_iOffset = iRemove;
						Remove.m_nLength = nRemove;
						Remove.m_Rule = "list-comma";
					}
				}
			}

			for (auto iComma : Node.m_SplitPoints)
			{
				auto const &Comma = Tokens[iComma];
				if (!m_Tokens.f_IsText(Comma, ","))
					continue;

				// Block comments in front of the comment ending the line describe the element with it.
				auto fIsBeforeComment = [&](umint _iToken)
					{
						auto const &Token = Tokens[_iToken];

						return Token.m_Kind == ECodeTokenKind::mc_Whitespace || (Token.m_Kind == ECodeTokenKind::mc_BlockComment && !Token.m_bMultiLine && !fp_IsLastOnLine(_iToken));
					}
				;

				auto iComment = iComma + 1;
				while (iComment < Tokens.f_GetLen() && fIsBeforeComment(iComment))
					++iComment;

				// A block comment ending the line is one too, since it is written as line comments in the same pass.
				auto const *pComment = iComment < Tokens.f_GetLen() ? &Tokens[iComment] : nullptr;
				bool bLineComment = pComment && pComment->m_Kind == ECodeTokenKind::mc_LineComment;
				if (!bLineComment && pComment && pComment->m_Kind == ECodeTokenKind::mc_BlockComment && !pComment->m_bMultiLine)
				{
					auto iAfter = iComment + 1;
					while (iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Whitespace)
						++iAfter;

					bLineComment = iAfter < Tokens.f_GetLen() && Tokens[iAfter].m_Kind == ECodeTokenKind::mc_Newline && !fg_TrailingLineComment(m_Tokens.f_GetText(*pComment)).f_IsEmpty();
				}

				CStr Lines;
				if (!bLineComment && iComment < Tokens.f_GetLen())
					bLineComment = fp_MultiLineTrailingCommentLines(iComment, Lines);

				auto iNext = fp_NextCode(iComma);
				if (iNext < 0 || umint(iNext) >= Node.m_iLastToken)
					continue;

				// So do the comment lines between a comma ending its line and the element behind it: '{1, 2},' over '// B'
				// over '{3, 4}' is '{1, 2}' over '// B' over ', {3, 4}'.
				bool bCommentLines = false;
				if (!bLineComment && iComment < umint(iNext) && Tokens[iComment].m_Kind == ECodeTokenKind::mc_Newline)
				{
					bCommentLines = true;
					bool bAnyComment = false;
					for (auto i = iComment + 1; i < umint(iNext) && bCommentLines; ++i)
					{
						auto Kind = Tokens[i].m_Kind;
						bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
						bAnyComment |= bComment;
						bCommentLines = Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline || (bComment && fp_IsFirstOnLine(i) && fp_IsLastOnLine(i));
					}

					bCommentLines = bCommentLines && bAnyComment;
				}

				if (!bLineComment && !bCommentLines)
					continue;

				bool bPlain = true;
				for (auto i = iComment + 1; i < umint(iNext) && bPlain; ++i)
				{
					auto Kind = Tokens[i].m_Kind;
					bPlain
						= Kind == ECodeTokenKind::mc_Whitespace
						|| Kind == ECodeTokenKind::mc_Newline
						|| Kind == ECodeTokenKind::mc_LineComment
						|| (bCommentLines && Kind == ECodeTokenKind::mc_BlockComment)
					;
				}

				auto iBefore = fp_PreviousCode(iComma);
				if (!bPlain || iBefore < 0)
					continue;

				auto iRemove = Comma.m_iOffset;
				bool bTight = true;
				for (auto i = umint(iBefore) + 1; i < iComma && bTight; ++i)
					bTight = Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace;

				if (bTight)
					iRemove = Tokens[umint(iBefore)].f_GetEnd();

				auto nRemove = Comma.f_GetEnd() - iRemove;
				auto iInsert = Tokens[umint(iNext)].m_iOffset;
				bool bLeft
					= fp_IsFormatOff(iRemove, nRemove)
					|| fp_IsFormatOff(iInsert, 0)
					|| fp_IsSuppressed(iRemove, nRemove)
					|| fp_IsSuppressed(iInsert, 0)
					|| !fp_IsConversionSelected(iRemove, nRemove, iInsert, 0)
				;
				if (bLeft)
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

		// A comment trailing a comma taken out trails the element in front of it.
		m_CommaGone.f_Clear();
		m_CommaGone.f_SetLen(Tokens.f_GetLen());
		for (auto &bGone : m_CommaGone)
			bGone = 0;

		for (auto const &Edit : m_Structural)
		{
			if ((Edit.m_Rule != "list-comma" && Edit.m_Rule != "enum-comma") || !Edit.m_Replacement.f_IsEmpty() || !Edit.m_nLength)
				continue;

			auto iComma = m_Tokens.f_FindToken(Edit.f_GetEnd() - 1);
			if (iComma < Tokens.f_GetLen() && m_Tokens.f_IsText(Tokens[iComma], ","))
				m_CommaGone[iComma] = 1;
		}
	}

	// A comma separated from the enumerator or element behind it by a comment or a conditional's
	// directive, and the one behind the last, goes, and one is put in front of each that has
	// another in front of it in every configuration it is compiled in. Every configuration then
	// spells the elements it did, and the layout moves the commas that only blanks separate from
	// their elements. A body holding another directive, or an element that is the first in some
	// configurations only, keeps its commas where they are.
	// Whether the node can stand in a macro's argument, where its commas separate the macro's arguments as well: only a
	// parenthesis shields them, braces at no depth do, 'DList({1, 2,})', 'DList([] { int a[] = {1, 2,}; })'. The nearest
	// parenthesis around the node decides, a call's being one a macro's can be, also behind a parenthesis, where another
	// macro can expand to the name: 'DGetList()({1, 2,})'.
	bool CFormattingAnalyzer::fp_MayBeMacroArgument(CCodeNode const &_Node) const
	{
		auto const &Tokens = m_Tokens.f_GetTokens();
		auto const &Nodes = m_Structure.f_GetNodes();
		for (auto iEnclosing = _Node.m_iParent; iEnclosing && iEnclosing < Nodes.f_GetLen(); iEnclosing = Nodes[iEnclosing].m_iParent)
		{
			auto const &Enclosing = Nodes[iEnclosing];
			if (Enclosing.m_Kind != ECodeNodeKind::mc_Group || !m_Tokens.f_IsText(Tokens[Enclosing.m_iFirstToken], "("))
				continue;

			auto iCallee = fp_PreviousCode(Enclosing.m_iFirstToken);
			return iCallee >= 0 && (Tokens[umint(iCallee)].m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Tokens[umint(iCallee)], ")"));
		}

		return false;
	}

	void CFormattingAnalyzer::fp_ConvertSeparatedCommas(CCodeNode const &_Body, bool _bEnum)
	{
		auto const &Tokens = m_Tokens.f_GetTokens();

		if (fp_MayBeMacroArgument(_Body) || fp_IsInMacroArgument(_Body.m_iFirstToken))
			return;

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

		// A body whose elements block comments name on their lines keeps its commas behind them: a comma moved in front
		// would part the name from what it names. '/* SQLITE_OK */ "not an error",'.
		for (auto i = _Body.m_iFirstToken + 1; i < _Body.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			if (Token.m_Kind != ECodeTokenKind::mc_BlockComment || Token.m_bMultiLine || m_TokenDepth[i] != nLevel || !fp_IsFirstOnLine(i))
				continue;

			auto iNamed = fp_NextSignificant(i);
			if (iNamed >= 0 && fg_IsCodeToken(Tokens[umint(iNamed)]))
				return;
		}

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

			if (m_TokenDepth[i] != nLevel || (_bEnum && Token.m_Kind != ECodeTokenKind::mc_Identifier))
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
					auto Kind = Tokens[i].m_Kind;
					if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
						return false;
				}

				return true;
			}
		;

		// A comma stays where only blanks separate it from the element behind it, unless a comment separates it from the
		// one in front of it, which it would end the line behind: 'A = 1 /* One */,' over 'B'.
		auto fMoves = [&](umint _iComma)
			{
				auto iNext = fp_NextCode(_iComma);
				if (iNext < 0 || umint(iNext) >= _Body.m_iLastToken || !fPlain(_iComma + 1, umint(iNext)))
					return true;

				auto iBefore = fp_PreviousCode(_iComma);
				bool bComment = false;
				for (auto iGap = umint(iBefore) + 1; iBefore >= 0 && iGap < _iComma; ++iGap)
					bComment |= Tokens[iGap].m_Kind == ECodeTokenKind::mc_BlockComment && !fp_IsFirstOnLine(iGap);

				return bComment && fp_SpansLines(_iComma, umint(iNext));
			}
		;

		TCVector<CCodeFormattingEdit> Edits;
		for (umint iEnumerator = 0; iEnumerator < Enumerators.f_GetLen(); ++iEnumerator)
		{
			auto const &Enumerator = Enumerators[iEnumerator];
			bool bAlways = false;
			bool bNever = true;
			// One predecessor always in front settles it, which in an enumeration without conditionals is the first.
			for (umint iBefore = 0; iBefore < iEnumerator && !bAlways; ++iBefore)
			{
				auto Relation = fRelation(Enumerators[iBefore].m_Path, Enumerator.m_Path);
				bAlways |= Relation == 1;
				bNever &= Relation == -1;
			}

			if (!bAlways && !bNever)
				return;

			auto iPrevious = fp_PreviousCode(Enumerator.m_iFirst);
			bool bCommaInFront = iPrevious >= 0 && m_Tokens.f_IsText(Tokens[umint(iPrevious)], ",") && !fMoves(umint(iPrevious));
			if (bCommaInFront && !bAlways)
				return;

			if (bAlways && !bCommaInFront)
			{
				auto &Insert = Edits.f_Insert();
				Insert.m_iOffset = Tokens[Enumerator.m_iFirst].m_iOffset;
				Insert.m_nLength = 0;
				Insert.m_Replacement = ", ";
				Insert.m_Rule = _bEnum ? "enum-comma" : "list-comma";
			}
		}

		for (auto iComma : Commas)
		{
			if (!fMoves(iComma))
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
			Remove.m_Rule = _bEnum ? "enum-comma" : "list-comma";
		}

		for (auto const &Edit : Edits)
		{
			if (fp_IsDisabled(Edit.m_iOffset, Edit.m_nLength) || fp_IsSuppressed(Edit.m_iOffset, Edit.m_nLength) || !fp_IsSelected(Edit.m_iOffset, Edit.m_nLength))
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
				"static"
				, "inline"
				, "constexpr"
				, "consteval"
				, "constinit"
				, "extern"
				, "virtual"
				, "friend"
				, "thread_local"
				, "typedef"
				, "mutable"
				, "explicit"
				, "register"
			}
		;
		constexpr ch8 const *c_pOpeners[] =
			{
				"("
				, ","
				, "<"
				, ";"
				, "{"
				, "}"
				, "->"
				, "="
				, "]"
				, ":"
				, "operator"
				, "new"
			}
		;
		constexpr ch8 const *c_pFundamental[] =
			{
				"signed"
				, "unsigned"
				, "short"
				, "long"
				, "int"
				, "char"
				, "char8_t"
				, "char16_t"
				, "char32_t"
				, "wchar_t"
				, "bool"
				, "float"
				, "double"
				, "void"
			}
		;
		constexpr ch8 const *c_pElaborated[] =
			{
				"struct"
				, "class"
				, "union"
				, "enum"
			}
		;

		// What a function's qualifier is followed by, and what an expression starts with:
		// names a type is never spelled with.
		constexpr ch8 const *c_pNoType[] =
			{
				"override"
				, "final"
				, "noexcept"
				, "requires"
				, "try"
				, "throw"
				, "operator"
				, "new"
				, "delete"
				, "return"
				, "co_return"
				, "co_await"
				, "co_yield"
				, "sizeof"
				, "alignof"
				, "template"
				, "using"
				, "namespace"
				, "if"
				, "for"
				, "while"
				, "switch"
				, "do"
				, "else"
				, "case"
				, "default"
				, "this"
				, "nullptr"
				, "true"
				, "false"
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
				auto const &Token = Tokens[_iToken];

				return m_Tokens.f_IsText(Token, "const") || m_Tokens.f_IsText(Token, "volatile");
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

					// 'enum class' and 'enum struct' name a scoped enumeration.
					if (fIsAny(j, c_pElaborated))
					{
						iNext = fp_NextCode(j);
						if (iNext < 0)
							return c_NoType;

						j = umint(iNext);
					}
				}

				if (m_Tokens.f_IsText(Tokens[j], "auto"))
					iEnd = j;
				else if (m_Tokens.f_IsText(Tokens[j], "decltype"))
				{
					auto iOpen = fp_NextCode(j);
					if (iOpen < 0 || !m_Tokens.f_IsText(Tokens[umint(iOpen)], "(") || GroupEnd[umint(iOpen)] == TCLimitsInt<umint>::mc_Max)
						return c_NoType;

					iEnd = GroupEnd[umint(iOpen)];

					// A name qualified by the type it gives goes on past it: 'decltype(c)::template X<int>'.
					auto iBehind = fp_NextCode(iEnd);
					if (iBehind >= 0 && m_Tokens.f_IsText(Tokens[umint(iBehind)], "::"))
						return c_NoType;
				}
				else if (fIsAny(j, c_pFundamental))
				{
					// In C 'bool' combines with no other word, and behind one it is a name until C23 reserves it: 'const int bool'.
					auto fContinuesFundamental = [&](aint _iToken)
						{
							if (_iToken < 0 || !fIsAny(umint(_iToken), c_pFundamental))
								return false;

							auto const &Token = Tokens[umint(_iToken)];
							return !(m_Tokens.f_IsC(Token) && m_Tokens.f_IsText(Token, "bool"));
						}
					;

					iEnd = j;
					for (auto iNext = fp_NextCode(iEnd); fContinuesFundamental(iNext); iNext = fp_NextCode(iEnd))
						iEnd = umint(iNext);

					// Words going on behind specifiers are the same type's, which the next pass would move the qualifier behind:
					// 'const long static int'.
					auto iBehind = fp_NextCode(iEnd);
					while (iBehind >= 0 && fIsAny(umint(iBehind), c_pSpecifiers))
						iBehind = fp_NextCode(umint(iBehind));

					if (fContinuesFundamental(iBehind))
						return c_NoType;
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

					// A name taking an operand, '_Atomic(int)' or '__typeof__(x)', a type defined where it is named, 'struct S
					// { ... }', 'struct S final' or 'enum E : int { ... }', and a concept constraining a placeholder, 'cAny auto',
					// end past the name.
					auto iAfter = fp_NextCode(iEnd);
					bool bGoesOn
						= iAfter >= 0
						&&
						(
							m_Tokens.f_IsText(Tokens[umint(iAfter)], "(")
							|| m_Tokens.f_IsText(Tokens[umint(iAfter)], "{")
							|| m_Tokens.f_IsText(Tokens[umint(iAfter)], ":")
							|| m_Tokens.f_IsText(Tokens[umint(iAfter)], "auto")
							|| m_Tokens.f_IsText(Tokens[umint(iAfter)], "decltype")
							|| m_Tokens.f_IsText(Tokens[umint(iAfter)], "final")
							|| m_Tokens.f_IsText(Tokens[umint(iAfter)], "sealed")
						)
					;
					if (bGoesOn)
						return c_NoType;
				}

				return iEnd;
			}
		;

		// What a type is followed by where it declares something: a declarator, or a name. An attribute or an 'asm' label
		// follows the name declared, 'int const value __attribute__((unused))', and so can a macro: 'CFoo const a DAttr'.
		constexpr ch8 const *c_pDeclaratorSuffixes[] = {"__attribute__", "__attribute", "asm", "__asm__", "__asm"};
		auto fDeclares = [&](aint _iToken)
			{
				if (_iToken < 0)
					return false;

				auto const &Token = Tokens[umint(_iToken)];

				return m_Tokens.f_IsText(Token, "*")
					|| m_Tokens.f_IsText(Token, "&")
					|| m_Tokens.f_IsText(Token, "&&")
					||
					(
						Token.m_Kind == ECodeTokenKind::mc_Identifier
						&& !fIsAny(umint(_iToken), c_pNoType)
						&& !fIsAny(umint(_iToken), c_pDeclaratorSuffixes)
						&& !m_Tokens.f_HasRole(Token, ECodeNameRole::mc_Macro)
						&& !m_Tokens.f_HasRole(Token, ECodeNameRole::mc_SpecifierMacro)
					)
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

			// Behind a parameter list it qualifies the member function, whatever follows it: 'f() const DNoexcept'.
			if (iBefore >= 0 && m_Tokens.f_IsText(Tokens[umint(iBefore)], ")"))
				continue;

			// A C array parameter's bracket qualifies the pointer it stands for, and its bound is no type: 'int a[static const N]'.
			auto iEnclosing = m_Structure.f_FindEnclosingNode(i);
			if (iEnclosing && iEnclosing < m_Structure.f_GetNodes().f_GetLen())
			{
				auto const &Enclosing = m_Structure.f_GetNodes()[iEnclosing];
				if (m_Tokens.f_IsText(Tokens[Enclosing.m_iFirstToken], "["))
					continue;
			}

			// A macro's argument is text the macro places, which can paste to it or name no type: 'DType(const int)' with
			// 'T##32', 'DDeclare(const a)'.
			if (fp_IsInMacroArgument(i))
				continue;

			umint iRunLast = i;
			for (auto iNext = fp_NextCode(iRunLast); iNext >= 0 && fIsQualifier(umint(iNext)); iNext = fp_NextCode(iRunLast))
				iRunLast = umint(iNext);

			// Specifiers in front lead only where nothing names the type in front of them: 'int static const x' has its type.
			auto iBeforeSpecifiers = iBefore;
			while (iBeforeSpecifiers >= 0 && fIsAny(umint(iBeforeSpecifiers), c_pSpecifiers))
				iBeforeSpecifiers = fp_PreviousCode(umint(iBeforeSpecifiers));

			bool bLeads = iBeforeSpecifiers < 0 || fIsAny(umint(iBeforeSpecifiers), c_pOpeners);

			// Attributes behind a declarator's '*' or '&' belong to it, and so does the qualifier behind them:
			// 'int *[[]] [[]] const p'.
			if (bLeads && iBeforeSpecifiers >= 0 && m_Tokens.f_IsText(Tokens[umint(iBeforeSpecifiers)], "]"))
			{
				constexpr ch8 const *c_pDeclarators[] = {"*", "&", "&&"};
				auto const &Nodes = m_Structure.f_GetNodes();
				auto iBeforeOpen = aint(iBeforeSpecifiers);
				while (iBeforeOpen >= 0 && m_Tokens.f_IsText(Tokens[umint(iBeforeOpen)], "]"))
				{
					auto iClosed = m_Structure.f_FindNodeClosingAt(umint(iBeforeOpen));
					iBeforeOpen = iClosed < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iClosed].m_iFirstToken) : aint(-1);
				}

				if (iBeforeOpen >= 0 && fIsAny(umint(iBeforeOpen), c_pDeclarators))
					continue;
			}
			if (!bLeads && iBeforeSpecifiers != iBefore && Tokens[umint(iBeforeSpecifiers)].m_Kind == ECodeTokenKind::mc_Identifier)
				continue;

			// A brace in front can close the type the declaration defines, which the qualifier then stands behind:
			// 'struct S { int n; } const x'.
			if (iBeforeSpecifiers >= 0 && m_Tokens.f_IsText(Tokens[umint(iBeforeSpecifiers)], "}"))
			{
				auto iClosed = m_Structure.f_FindNodeClosingAt(umint(iBeforeSpecifiers));
				auto const &Nodes = m_Structure.f_GetNodes();
				auto iStatement = iClosed < Nodes.f_GetLen() ? Nodes[iClosed].m_iParent : Nodes.f_GetLen();
				bool bDefinition = false;
				if (iStatement < Nodes.f_GetLen() && Nodes[iStatement].m_Kind == ECodeNodeKind::mc_Statement && Nodes[iStatement].m_iLastToken >= i)
				{
					for (auto iKey = Nodes[iStatement].m_iFirstToken; iKey < Nodes[iClosed].m_iFirstToken && !bDefinition; ++iKey)
						bDefinition = fIsAny(iKey, c_pElaborated);
				}

				if (bDefinition)
					continue;
			}

			iBefore = iBeforeSpecifiers;

			if (!bLeads)
			{
				auto const &Before = Tokens[umint(iBefore)];
				bool bAmbiguous
					= Before.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Before, ")") || (m_Structure.f_IsAngleBracket(umint(iBefore)) && m_Tokens.f_IsText(Before, ">"))
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

			// A type a macro the naming lists names, 'DPointer' or 'N::DPointer', may expand to more than a name, 'int *',
			// which the qualifier would change sides of.
			bool bMacro = false;
			for (auto iName = umint(iType); iName <= iEnd && !bMacro; ++iName)
				bMacro = m_Tokens.f_HasRole(Tokens[iName], ECodeNameRole::mc_Macro);

			if (bMacro)
				continue;

			auto iAfter = fp_NextCode(iEnd);
			if (iAfter >= 0 && fIsQualifier(umint(iAfter)))
				continue;

			// A pack index goes on with the type, which the qualifier cannot stand inside of: 'const Ts...[0]'.
			auto iIndex = iAfter >= 0 && m_Tokens.f_IsText(Tokens[umint(iAfter)], "...") ? fp_NextCode(umint(iAfter)) : aint(-1);
			if (iIndex >= 0 && m_Tokens.f_IsText(Tokens[umint(iIndex)], "["))
				continue;

			// Only a declarator or a name behind the type says the qualifier led it.
			if (!bLeads && !fDeclares(iAfter))
				continue;

			// A name that ends the declaration, bounds an array or is one of a list in C is its declarator, the type C90 leaves
			// out an 'int': 'static const value = 1;', 'static const values[3]', 'static const x, y;'.
			bool bEndsDeclaration
				= iAfter >= 0
				&&
				(
					m_Tokens.f_IsText(Tokens[umint(iAfter)], "=")
					|| m_Tokens.f_IsText(Tokens[umint(iAfter)], ";")
					|| m_Tokens.f_IsText(Tokens[umint(iAfter)], "[")
					|| (m_Tokens.f_IsC(Tokens[umint(iAfter)]) && m_Tokens.f_IsText(Tokens[umint(iAfter)], ","))
				)
			;
			// Only a declaration has a declarator to leave the type out in front of; an alias's type or a trailing return type
			// is a type whole, and a fundamental type is never the declarator: 'using A = const int;', '-> const CFoo;'.
			auto iBeforeAlias = iBefore >= 0 ? fp_PreviousCode(umint(iBefore)) : aint(-1);
			auto iUsing = iBeforeAlias >= 0 ? fp_PreviousCode(umint(iBeforeAlias)) : aint(-1);
			bool bTypeOnly
				= iBefore >= 0
				&&
				(
					m_Tokens.f_IsText(Tokens[umint(iBefore)], "->")
					|| (m_Tokens.f_IsText(Tokens[umint(iBefore)], "=") && iUsing >= 0 && m_Tokens.f_IsText(Tokens[umint(iUsing)], "using"))
				)
			;
			constexpr ch8 const *c_pFundamentals[] = {"int", "char", "short", "long", "signed", "unsigned", "bool", "float", "double", "void", "auto"};
			if (iType == aint(iEnd) && bEndsDeclaration && !bTypeOnly && !fIsAny(umint(iType), c_pFundamentals))
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
			{
				auto const &Gap = Tokens[iGap];
				bPlain = fg_IsCodeToken(Gap) || Gap.m_Kind == ECodeTokenKind::mc_Whitespace || Gap.m_Kind == ECodeTokenKind::mc_Newline;
			}

			if (!bPlain)
				continue;

			auto iRunStart = Tokens[i].m_iOffset;
			auto nRun = Tokens[iFirstBehind].m_iOffset - iRunStart;
			auto iLastStart = Tokens[iEnd].m_iOffset;
			auto nLast = Tokens[iEnd].m_nLength;
			if
			(
				fp_IsDisabled(iRunStart, nRun)
				|| fp_IsDisabled(iLastStart, nLast)
				|| fp_IsSuppressed(iRunStart, nRun)
				|| fp_IsSuppressed(iLastStart, nLast)
				|| !fp_IsConversionSelected(iRunStart, nRun, iLastStart, nLast)
			)
			{
				continue;
			}

			CStr Moved = m_Tokens.f_GetText(Tokens[iEnd]);
			for (auto iRun = aint(i); iRun >= 0 && umint(iRun) <= iRunLast; iRun = fp_NextCode(umint(iRun)))
			{
				auto const &Run = Tokens[umint(iRun)];
				Moved += " ";
				Moved.f_AddStr(m_pSource + Run.m_iOffset, Run.m_nLength);
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

		// Out of scope by decision: a function template's types are substituted in lexical order, so behind the parameters
		// a dependent return type can turn a substitution failure into a hard error; the standard's trailing return type is
		// taken to be the form such a template is written in. C has no trailing return type.
		if (m_Tokens.f_IsC(m_Tokens.f_GetTokens()[Node.m_iFirstToken]))
			return false;

		// A declaration in a macro's argument is text the macro places or turns into a string.
		if (fp_IsInMacroArgument(Node.m_iFirstToken))
			return false;

		auto const &Tokens = m_Tokens.f_GetTokens();

		// An attribute's parenthesis is no parameter list: 'struct __declspec(dllexport) CFoo {...};' declares no function.
		umint iParen = TCLimitsInt<umint>::mc_Max;
		for (auto iChild : Node.m_Children)
		{
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_Bracket != ECodeBracket::mc_Paren)
				continue;

			auto iKeyword = fp_PreviousCode(Child.m_iFirstToken);
			bool bAttribute = false;
			for (auto pKeyword : {"__attribute__", "__attribute", "__declspec", "alignas", "_Alignas"})
				bAttribute |= iKeyword >= 0 && m_Tokens.f_IsText(Tokens[umint(iKeyword)], pKeyword);

			if (bAttribute)
				continue;

			iParen = iChild;

			break;
		}

		if (iParen == TCLimitsInt<umint>::mc_Max)
			return false;

		auto iOpen = Nodes[iParen].m_iFirstToken;
		auto iClose = Nodes[iParen].m_iLastToken;
		if (iOpen == Node.m_iFirstToken)
			return false;

		// The node the head's own tokens stand in, the parameter list's included.
		auto iHeadNode = Nodes[iParen].m_iParent;

		// A conditional in the head gives each branch a declaration of its own, and the return type would move behind
		// only the last one's parameter list.
		auto iHeadEnd = Node.m_iLastToken;
		for (auto iChild : Node.m_Children)
		{
			if (Nodes[iChild].m_Kind == ECodeNodeKind::mc_Block)
			{
				iHeadEnd = Nodes[iChild].m_iFirstToken;

				break;
			}
		}

		// So does a comma between declarators, 'unsigned f(int), g(void);', whose type every declarator shares.
		for (auto i = _iDeclFirst; i < iHeadEnd; ++i)
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
				return false;

			if (m_Tokens.f_IsText(Tokens[i], ",") && m_Structure.f_FindEnclosingNode(i) == iHeadNode)
				return false;
		}

		// A typedef declares a type, and a parenthesis with another behind it is a pointer to function's
		// declarator, which has no return type to move: 'typedef LONG (NTAPI *FHandler)(PVOID _pContext)'.
		auto iBehindClose = fp_NextCode(iClose);
		if (iBehindClose >= 0 && m_Tokens.f_IsText(Tokens[umint(iBehindClose)], "("))
			return false;

		for (auto i = _iDeclFirst; i < iOpen; ++i)
		{
			if (m_Tokens.f_IsText(Tokens[i], "typedef"))
				return false;
		}

		// A variable constructed with arguments has the shape of a declaration too; what the
		// parenthesis holds is what tells them apart.
		if (fg_HoldsArguments(m_Tokens, m_Structure, iParen))
			return false;

		// Without a body, only a parenthesis that holds parameters proves the declaration a function's: 'CType Value(g_Value);'
		// constructs a variable.
		bool bDefinition = iHeadEnd != Node.m_iLastToken;
		if (!bDefinition && !fg_HoldsParameters(m_Tokens, m_Structure, iParen))
			return false;

		// No parameter starts with a parenthesis, an initializer may: 'CType Value((42))'.
		auto iFirstInside = fp_NextCode(Nodes[iParen].m_iFirstToken);
		if (iFirstInside >= 0 && umint(iFirstInside) < Nodes[iParen].m_iLastToken && m_Tokens.f_IsText(Tokens[umint(iFirstInside)], "("))
			return false;

		auto iBeforeOpen = fp_PreviousCode(iOpen);
		if (iBeforeOpen < 0)
			return false;

		// A head with a gap the standard does not settle has no width to be too long by.
		umint nHead = 0;
		if (!fp_MeasureJoinedWidth(_iDeclFirst, iClose, nHead))
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
				"static"
				, "virtual"
				, "inline"
				, "constexpr"
				, "consteval"
				, "constinit"
				, "explicit"
				, "friend"
				, "extern"
				, "mutable"
				, "thread_local"
				, "export"
				, "__inline__"
				, "__inline"
				, "__forceinline"
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

		// An elaborated return type can declare the class it names, which the parameters in front of it then could not use:
		// 'struct CNew *fg_Get(CNew *_pNew)'.
		for (auto i = iReturn; i <= umint(iReturnLast); ++i)
		{
			for (auto pElaborated : {"struct", "class", "union", "enum"})
			{
				if (m_Tokens.f_IsText(Tokens[i], pElaborated))
					return false;
			}
		}

		// A macro, by the project's naming, can expand to specifiers as readily as to a type,
		// and a specifier moved behind the parameter list is no type. One naming the function can expand to the whole
		// declarator, a pure specifier behind it included: 'DDeclare(int x)' with 'f(__VA_ARGS__) = 0'.
		for (auto i = iReturn; i <= umint(iBeforeOpen); ++i)
		{
			if (m_Tokens.f_HasRole(Tokens[i], ECodeNameRole::mc_Macro))
				return false;
		}

		// A calling convention belongs to the declarator, and behind the parameter list it is ignored: '__stdcall int f()'. A GNU
		// attribute in front can apply to the type, which 'auto' would take instead: '__attribute__((vector_size(16))) int f()'.
		constexpr ch8 const *c_pConventions[] =
			{
				"__stdcall"
				, "__cdecl"
				, "__fastcall"
				, "__thiscall"
				, "__vectorcall"
				, "__regcall"
				, "__clrcall"
				, "__pascal"
				, "_stdcall"
				, "_cdecl"
				, "_fastcall"
			}
		;
		// The attribute can stand in front of where the declaration is taken to start, as a statement of its own.
		if (auto iBeforeDeclaration = fp_PreviousCode(_iDeclFirst); iBeforeDeclaration >= 0 && m_Tokens.f_IsText(Tokens[umint(iBeforeDeclaration)], ")"))
		{
			auto iGroup = m_Structure.f_FindNodeClosingAt(umint(iBeforeDeclaration));
			auto iAttribute = iGroup < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iGroup].m_iFirstToken) : aint(-1);
			if (iAttribute >= 0 && (m_Tokens.f_IsText(Tokens[umint(iAttribute)], "__attribute__") || m_Tokens.f_IsText(Tokens[umint(iAttribute)], "__attribute")))
				return false;
		}

		for (auto i = _iDeclFirst; i < iDeclarator; ++i)
		{
			if (m_Tokens.f_IsText(Tokens[i], "__attribute__") || m_Tokens.f_IsText(Tokens[i], "__attribute"))
				return false;

			for (auto pConvention : c_pConventions)
			{
				if (m_Tokens.f_IsText(Tokens[i], pConvention))
					return false;
			}
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
		auto const *pBeforeDeclarator = iBeforeDeclarator >= 0 ? &Tokens[umint(iBeforeDeclarator)] : nullptr;
		if (pBeforeDeclarator && (m_Tokens.f_IsText(*pBeforeDeclarator, ".") || m_Tokens.f_IsText(*pBeforeDeclarator, "->")))
			return false;

		// A keyword is spelled like an identifier, so a statement that opens with one reads
		// as a type unless it is named here. 'return g_Dispatch(x) / [] {}' is an expression
		// whose first word would otherwise pass for its return type.
		constexpr ch8 const *c_pStatementKeywords[] =
			{
				"return"
				, "co_return"
				, "co_await"
				, "co_yield"
				, "throw"
				, "new"
				, "delete"
				, "this"
				, "sizeof"
				, "alignof"
				, "if"
				, "else"
				, "for"
				, "while"
				, "do"
				, "switch"
				, "case"
				, "default"
				, "break"
				, "continue"
				, "goto"
				, "try"
				, "catch"
				, "using"
				, "namespace"
				, "nullptr"
				, "true"
				, "false"
				, "operator"
				, "static_cast"
				, "dynamic_cast"
				, "const_cast"
				, "reinterpret_cast"
			}
		;

		// A name that follows a complete type at the type's own level is not part of it:
		// an attribute macro stands there, and moving it along would misplace it. Only the
		// words a type is spelled with may follow another name.
		constexpr ch8 const *c_pTypeWords[] =
			{
				"const"
				, "volatile"
				, "typename"
				, "struct"
				, "class"
				, "union"
				, "enum"
				, "unsigned"
				, "signed"
				, "short"
				, "long"
				, "int"
				, "char"
				, "double"
				, "float"
				, "bool"
				, "void"
			}
		;

		// A template argument list follows the name it belongs to, which a return type starting with one is cut off from:
		// 'TCBox' over '<TCBox<int>> f_Get(int x);'.
		for (umint i = iReturn; i <= umint(iReturnLast); ++i)
		{
			if (!fg_IsCodeToken(Tokens[i]))
				continue;

			if (m_Tokens.f_IsText(Tokens[i], "<") || m_Tokens.f_IsText(Tokens[i], ">") || m_Tokens.f_IsText(Tokens[i], ">>"))
				return false;

			break;
		}

		umint nAngle = 0;
		bool bAfterName = false;
		bool bAfterPlainName = false;
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

			if (nAngle || Token.m_Kind == ECodeTokenKind::mc_Whitespace || Token.m_Kind == ECodeTokenKind::mc_Newline)
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

				// These introduce the name behind them rather than completing a type: 'typename TCFoo<t_C>::CNode'. Behind a
				// name one starts another type, which leaves the name a specifier: 'BOOST_MOVE_FORCEINLINE typename N::CFoo'.
				bool bIntroduces
					= m_Tokens.f_IsText(Token, "typename")
					|| m_Tokens.f_IsText(Token, "struct")
					|| m_Tokens.f_IsText(Token, "class")
					|| m_Tokens.f_IsText(Token, "union")
					|| m_Tokens.f_IsText(Token, "enum")
				;
				if (bAfterName && (!bTypeWord || bIntroduces))
					return false;

				// A fundamental type's word behind a name no type word is leaves the name a specifier, or a macro standing for
				// one: 'API int', where no spelling of a type holds a name in front of 'int'.
				bool bFundamental = bTypeWord && !m_Tokens.f_IsText(Token, "const") && !m_Tokens.f_IsText(Token, "volatile") && !bIntroduces;
				if (bAfterPlainName && bFundamental)
					return false;

				bAfterPlainName = !bTypeWord;
				bAfterName = !bIntroduces;

				continue;
			}

			// A declarator completes the type in front of it, so a name behind 'void *' is
			// as much a bare name as one behind 'void': 'void * DMibCrossmoduleAPI fs_Alloc'.
			bool bDeclarator = m_Tokens.f_IsText(Token, "*") || m_Tokens.f_IsText(Token, "&") || m_Tokens.f_IsText(Token, "&&");
			bAfterName = bAfterName && bDeclarator;
			bAfterPlainName = false;

			if (Token.m_Kind == ECodeTokenKind::mc_Number)
				continue;

			if (Token.m_Kind != ECodeTokenKind::mc_Punctuator)
				return false;

			bool bTypePunctuation
				= m_Tokens.f_IsText(Token, "::")
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

			// What a group behind the parameter list holds, a lambda in a 'noexcept(...)' as well, is no part of the head.
			auto iGroup = m_Structure.f_FindNodeOpeningAt(umint(i));
			if (iGroup < Nodes.f_GetLen() && Nodes[iGroup].m_Kind == ECodeNodeKind::mc_Group && !m_Tokens.f_IsText(Token, "{"))
			{
				i = aint(Nodes[iGroup].m_iLastToken);

				continue;
			}

			if (m_Tokens.f_IsText(Token, "->"))
				return false;

			// A GNU attribute behind the parameter list has no place beside a trailing return type: written behind it, it
			// is read as the type's and dropped, and in front of it no compiler takes it. An asm label ends the declarator,
			// which the trailing type belongs inside of.
			if
			(
				m_Tokens.f_IsText(Token, "__attribute__")
				|| m_Tokens.f_IsText(Token, "__attribute")
				|| m_Tokens.f_IsText(Token, "asm")
				|| m_Tokens.f_IsText(Token, "__asm__")
				|| m_Tokens.f_IsText(Token, "__asm")
			)
			{
				return false;
			}

			// Nor does a word that is no qualifier, a macro that may stand for an attribute or for 'final': 'f() DFinal'.
			bool bQualifierWord
				= m_Tokens.f_IsText(Token, "const")
				|| m_Tokens.f_IsText(Token, "volatile")
				|| m_Tokens.f_IsText(Token, "noexcept")
				|| m_Tokens.f_IsText(Token, "throw")
				|| m_Tokens.f_IsText(Token, "requires")
				|| m_Tokens.f_IsText(Token, "override")
				|| m_Tokens.f_IsText(Token, "final")
				|| m_Tokens.f_IsText(Token, "try")
			;
			if (Token.m_Kind == ECodeTokenKind::mc_Identifier && !bQualifierWord)
				return false;

			// The trailing type follows the qualifiers but comes in front of 'override' and
			// 'final', which are written after the declarator, and in front of a pure
			// specifier, a body, a function try block, a requires clause and the terminator.
			bool bInsert
				= m_Tokens.f_IsText(Token, "=")
				|| m_Tokens.f_IsText(Token, "{")
				|| m_Tokens.f_IsText(Token, "try")
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
		CStr ReturnType(m_pSource + iReturnStart, Tokens[umint(iReturnLast)].f_GetEnd() - iReturnStart);

		// Behind the parameter list a parameter's name hides a name of the return type that it shares: 'CType f(int CType)'.
		// A parameter's name is the last of its names in front of a default argument, when it has more than one. A member of
		// the class an out-of-line definition names would hide an unqualified name of its return type the same way. That is
		// out of scope by decision: the class is not known here, the code standard converts every split definition, and
		// refusing every out-of-line definition for a type a class shadows would refuse nearly all of them. A return type
		// naming a type outside a class that holds one of the same name is left to the source to qualify.
		TCVector<umint> ParameterNames;
		bool bOpaqueName = false;
		{
			aint iName = -1;
			umint nNames = 0;
			bool bDefault = false;
			bool bLastMacro = false;
			auto fEndParameter = [&]
				{
					if (iName >= 0 && nNames > 1)
						ParameterNames.f_Insert(umint(iName));

					// A macro standing last can expand to the name, which could hide the return type: 'int DName', and so can one
					// standing for the whole parameter: 'DParam' with 'int CType'.
					bOpaqueName = bOpaqueName || bLastMacro;
					bLastMacro = false;

					iName = -1;
					nNames = 0;
					bDefault = false;
				}
			;

			for (auto iParameter = iOpen + 1; iParameter < iClose; ++iParameter)
			{
				auto const &ParameterToken = Tokens[iParameter];
				bool bTopLevel = m_Structure.f_FindEnclosingNode(iParameter) == iParen;
				if (bTopLevel && m_Tokens.f_IsText(ParameterToken, ","))
					fEndParameter();
				else if (bTopLevel && m_Tokens.f_IsText(ParameterToken, "="))
					bDefault = true;
				else if (bTopLevel && !bDefault && ParameterToken.m_Kind == ECodeTokenKind::mc_Identifier)
				{
					// An attribute behind the name, or a macro the naming lists, is no name: 'int CType ATTR'. A
					// macro still counts as a word in front of the name, which it can stand for a type with: 'DInt CType'.
					bool bAttribute
						= m_Tokens.f_IsText(ParameterToken, "__attribute__")
						|| m_Tokens.f_IsText(ParameterToken, "__attribute")
						|| m_Tokens.f_IsText(ParameterToken, "__declspec")
						|| m_Tokens.f_IsText(ParameterToken, "alignas")
					;
					bool bMacro = m_Tokens.f_HasRole(ParameterToken, ECodeNameRole::mc_Macro) || m_Tokens.f_HasRole(ParameterToken, ECodeNameRole::mc_SpecifierMacro);
					if (!bAttribute)
						++nNames;

					// A keyword is a word of the type, never the name: an unnamed parameter ends in one, 'CArgument const &'.
					constexpr ch8 const *c_pTypeKeywords[] =
						{
							"const"
							, "volatile"
							, "unsigned"
							, "signed"
							, "short"
							, "long"
							, "int"
							, "char"
							, "bool"
							, "float"
							, "double"
							, "void"
							, "auto"
							, "__restrict"
							, "__restrict__"
						}
					;

					bool bKeyword = false;
					for (auto pKeyword : c_pTypeKeywords)
						bKeyword = bKeyword || m_Tokens.f_IsText(ParameterToken, pKeyword);

					if (!bAttribute && !bMacro && !bKeyword)
						iName = aint(iParameter);

					if (!bAttribute)
						bLastMacro = bMacro;
				}
				else if (!bTopLevel && !bDefault && ParameterToken.m_Kind == ECodeTokenKind::mc_Identifier)
				{
					// The name of a parameter declared through a nested declarator: 'int (*CType)(int)', 'int (* const CType)(int)',
					// 'int (&&CType)', 'int ((CType[1]))', 'Ts (&...CType)[1]', 'int (N)'. A name an opening parenthesis or a declarator operator leads may
					// be one, which also takes in an unnamed parameter's type, as 'void (CType)' is, and so may one behind an attribute:
					// 'int (* [[]] CType)()', 'int (* __attribute__((x)) CType)()', or behind a word: 'int (* __restrict CType)[1]'.
					auto iBefore = fp_PreviousCode(iParameter);
					while (iBefore >= 0 && (m_Tokens.f_IsText(Tokens[umint(iBefore)], "const") || m_Tokens.f_IsText(Tokens[umint(iBefore)], "volatile")))
						iBefore = fp_PreviousCode(umint(iBefore));

					if (iBefore < 0)
						continue;

					auto const &BeforeToken = Tokens[umint(iBefore)];
					bool bDeclarator
						= m_Tokens.f_IsText(BeforeToken, "(")
						|| m_Tokens.f_IsText(BeforeToken, "*")
						|| m_Tokens.f_IsText(BeforeToken, "&")
						|| m_Tokens.f_IsText(BeforeToken, "&&")
						|| m_Tokens.f_IsText(BeforeToken, "^")
						|| m_Tokens.f_IsText(BeforeToken, "...")
						|| m_Tokens.f_IsText(BeforeToken, "]")
						|| m_Tokens.f_IsText(BeforeToken, ")")
						|| BeforeToken.m_Kind == ECodeTokenKind::mc_Identifier
					;
					// A macro there can expand to the name, which could hide the return type: 'int (*DName)(int)'.
					bool bMacro = m_Tokens.f_HasRole(ParameterToken, ECodeNameRole::mc_Macro) || m_Tokens.f_HasRole(ParameterToken, ECodeNameRole::mc_SpecifierMacro);
					bOpaqueName = bOpaqueName || (bDeclarator && bMacro);
					if (bDeclarator)
						ParameterNames.f_Insert(iParameter);
				}
			}

			fEndParameter();
		}

		if (bOpaqueName)
			return false;

		// A keyword of the return type is no name a parameter can hide: 'unsigned long f(void (*_p)(unsigned long))'.
		constexpr ch8 const *c_pReturnKeywords[] =
			{
				"const"
				, "volatile"
				, "unsigned"
				, "signed"
				, "short"
				, "long"
				, "int"
				, "char"
				, "bool"
				, "float"
				, "double"
				, "void"
				, "auto"
			}
		;

		for (auto iType = iReturn; iType <= umint(iReturnLast); ++iType)
		{
			auto const &TypeToken = Tokens[iType];
			if (TypeToken.m_Kind != ECodeTokenKind::mc_Identifier)
				continue;

			bool bKeyword = false;
			for (auto pKeyword : c_pReturnKeywords)
				bKeyword = bKeyword || m_Tokens.f_IsText(TypeToken, pKeyword);

			if (bKeyword)
				continue;

			for (auto iParameter : ParameterNames)
			{
				auto const &ParameterToken = Tokens[iParameter];
				auto pParameter = (uint8 const *)(m_pSource + ParameterToken.m_iOffset);
				auto pType = (uint8 const *)(m_pSource + TypeToken.m_iOffset);
				if (ParameterToken.m_nLength == TypeToken.m_nLength && NMemory::fg_MemCmp(pParameter, pType, TypeToken.m_nLength) == 0)
					return false;
			}
		}

		auto iPrevious = fp_PreviousCode(iInsert);
		if (iPrevious < 0)
			return false;

		// The gap before the insertion point is written whole, so no other rule may claim it.
		auto iGap = Tokens[umint(iPrevious)].f_GetEnd();
		auto const &InsertAt = Tokens[iInsert];
		auto nGap = InsertAt.m_iOffset - iGap;
		auto bBody = m_Tokens.f_IsText(InsertAt, "{") || m_Tokens.f_IsText(InsertAt, ";");

		// 'override', 'final' and a pure specifier are written behind the trailing type, on
		// its line.
		bool bVirtSpecifier = m_Tokens.f_IsText(InsertAt, "override") || m_Tokens.f_IsText(InsertAt, "final") || m_Tokens.f_IsText(InsertAt, "=");
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
		if
		(
			fp_IsDisabled(iReturnStart, nReturn)
			|| fp_IsDisabled(iGap, nGap)
			|| fp_IsSuppressed(iReturnStart, nReturn)
			|| fp_IsSuppressed(iGap, nGap)
			|| !fp_IsConversionSelected(iReturnStart, nReturn, iGap, nGap)
		)
		{
			return false;
		}

		// The gap is written whole, so a comment in it, 'f_Get(int x) /* keep */;', would be lost.
		for (auto iChar = iGap; iChar < iGap + nGap; ++iChar)
		{
			auto Char = m_pSource[iChar];
			if (Char != ' ' && Char != '\t' && Char != '\n' && Char != '\r')
				return false;
		}

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

		// An 'else if' is measured from its 'else', which the layout puts at the start of a line one level in per block
		// around it, also where the source wrote it behind the closing brace in front: '} else if (...)'.
		// Only an 'else' on the clause's line is one the layout keeps there; the 'else' of a conditional's other branch,
		// with the directive between them, is no part of the clause.
		umint iLineFirst = Guard.m_iFirstToken;
		if (auto iElse = fp_PreviousCode(Guard.m_iFirstToken); iElse >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iElse)], "else"))
		{
			bool bPlain = true;
			for (auto i = umint(iElse) + 1; i < Guard.m_iFirstToken && bPlain; ++i)
				bPlain = m_Tokens.f_GetTokens()[i].m_Kind == ECodeTokenKind::mc_Whitespace;

			if (bPlain)
				iLineFirst = umint(iElse);
		}

		umint nIndent = fp_GetStatementIndent(iLineFirst);
		if (!fp_IsFirstOnLine(iLineFirst))
		{
			auto const &Nodes = m_Structure.f_GetNodes();
			umint nBlocks = 0;
			for (auto iNode = m_Structure.f_FindEnclosingNode(Guard.m_iFirstToken); iNode && iNode < Nodes.f_GetLen(); iNode = Nodes[iNode].m_iParent)
				nBlocks += Nodes[iNode].m_Kind == ECodeNodeKind::mc_Block;

			nIndent = nBlocks * m_Request.m_Settings.m_nTabWidth;
		}

		o_bClauseFits = !bClause || fp_FitsInline(iLineFirst, Guard.m_iLastToken, nIndent);

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
		// The children stand in order, so the first one that can lie in the range is searched for.
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Children = Nodes[_iNode].m_Children;
		umint iLow = 0;
		umint iHigh = Children.f_GetLen();
		while (iLow < iHigh)
		{
			auto iMiddle = (iLow + iHigh) / 2;
			if (Nodes[Children[iMiddle]].m_iFirstToken < _iFirst)
				iLow = iMiddle + 1;
			else
				iHigh = iMiddle;
		}

		for (auto iIndex = iLow; iIndex < Children.f_GetLen(); ++iIndex)
		{
			auto const &Child = Nodes[Children[iIndex]];
			if (Child.m_iFirstToken > _iLast)
				break;

			if (Child.m_iLastToken > _iLast)
				continue;

			if (Child.m_Kind == ECodeNodeKind::mc_Block || Child.m_bHasBlock)
				return false;
		}

		// A line break inside a group is one the layout takes out where the group joins what it belongs to, which a
		// block comment beside neither keeps from doing: 'f_Get</*bOut=*/false>(' over 'a, b);'. A group keeping its
		// lines keeps them.
		bool bInsideGroups = true;
		for (auto iIndex = iLow; iIndex < Children.f_GetLen() && bInsideGroups; ++iIndex)
		{
			auto const &Child = Nodes[Children[iIndex]];
			if (Child.m_iFirstToken > _iLast)
				break;

			bInsideGroups = !Child.m_bHasMultiLineBrace && !Child.m_bHasMultiLineToken && !Child.m_bHasDirective && !Child.m_bFixedLineBreaks;
		}

		bool bBroken = false;
		for (auto i = _iFirst; i <= _iLast; ++i)
		{
			if (Tokens[i].m_Kind != ECodeTokenKind::mc_Newline)
				continue;

			bBroken = true;
			bInsideGroups = bInsideGroups && m_Structure.f_FindEnclosingNode(i) != _iNode;
		}

		if (!bBroken)
			return true;

		umint nJoined = 0;
		auto nMaxColumns = m_Request.m_Settings.m_nMaxColumns;

		return bInsideGroups && fp_MeasureJoinedWidth(_iFirst, _iLast, nJoined, true) && (!nMaxColumns || _iIndent + nJoined <= nMaxColumns);
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

		// A macro's argument can be turned into a string, whose text the braces are part of: 'DStr([] { if (b) { f(); } })'.
		if (fp_IsInMacroArgument(Guard.m_iFirstToken))
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

		// A lone name is a statement only as a macro, which may expand to more than one, or to an 'if' that would take an
		// 'else' behind, so it keeps the braces. A macro called as a statement is one statement: the code standard writes a
		// statement macro as one, 'do { ... } while (false)'.
		auto iBehindFirst = fp_NextCode(Inner.m_iFirstToken);
		auto const &FirstToken = Tokens[Inner.m_iFirstToken];
		bool bJump
			= m_Tokens.f_IsText(FirstToken, "return")
			|| m_Tokens.f_IsText(FirstToken, "break")
			|| m_Tokens.f_IsText(FirstToken, "continue")
			|| m_Tokens.f_IsText(FirstToken, "throw")
			|| m_Tokens.f_IsText(FirstToken, "co_return")
		;
		bool bLoneName = FirstToken.m_Kind == ECodeTokenKind::mc_Identifier && !bJump && iBehindFirst >= 0 && umint(iBehindFirst) == Inner.m_iLastToken;
		if (bLoneName)
			return false;

		// Nor is a call with a name behind it, which a macro makes a clause, as an 'if' that would take an 'else' behind, or a
		// declaration's start: 'IF(b) f_Call();', 'DAlign(16) int x;'. A placement 'new' names the type it makes behind its
		// parenthesis: 'new (pMemory) CFoo(1);'.
		bool bCallFirst
			= FirstToken.m_Kind == ECodeTokenKind::mc_Identifier && !m_Tokens.f_IsText(FirstToken, "new") && iBehindFirst >= 0 && m_Tokens.f_IsText(Tokens[umint(iBehindFirst)], "(")
		;
		// A name in front of another is a declaration or a macro, which can be a clause too: 'GUARD f_Call();'.
		bool bNameFirst
			= FirstToken.m_Kind == ECodeTokenKind::mc_Identifier
			&& !bJump
			&& !m_Tokens.f_IsText(FirstToken, "delete")
			&& !m_Tokens.f_IsText(FirstToken, "goto")
			&& !m_Tokens.f_IsText(FirstToken, "co_await")
			&& !m_Tokens.f_IsText(FirstToken, "co_yield")
			&& iBehindFirst >= 0
			&& Tokens[umint(iBehindFirst)].m_Kind == ECodeTokenKind::mc_Identifier
		;
		if (bNameFirst)
			return false;

		if (bCallFirst)
		{
			auto iCall = m_Structure.f_FindNodeOpeningAt(umint(iBehindFirst));
			auto iBehindCall = iCall < Nodes.f_GetLen() ? fp_NextCode(Nodes[iCall].m_iLastToken) : aint(-1);
			if (iBehindCall >= 0 && umint(iBehindCall) < Inner.m_iLastToken && Tokens[umint(iBehindCall)].m_Kind == ECodeTokenKind::mc_Identifier)
				return false;
		}

		// An attribute in front hides what the statement is: '[[likely]] if (b) ...' would take an 'else' behind the braces,
		// and in C '[[maybe_unused]] int x' declares.
		if (m_Tokens.f_IsText(Tokens[Inner.m_iFirstToken], "["))
			return false;

		// So does GNU's '__extension__', which a declaration starts with as readily as an expression: '__extension__ int a', and
		// a pragma or a GNU attribute in front of an 'if' that would take an 'else' behind. GNU's '__label__' only starts a block.
		for (auto pPrefix : {"__extension__", "_Pragma", "__pragma", "__attribute__", "__attribute", "__declspec", "__label__"})
		{
			if (m_Tokens.f_IsText(Tokens[Inner.m_iFirstToken], pPrefix))
				return false;
		}

		// C has no declaration statement, so a guarded declaration keeps its braces, and so does a statement that could be
		// one: one opening with a word that starts a declaration, or with a name followed by a name or a '*'.
		if (m_Tokens.f_IsC(Tokens[Inner.m_iFirstToken]) && Tokens[Inner.m_iFirstToken].m_Kind == ECodeTokenKind::mc_Identifier)
		{
			constexpr ch8 const *c_pDeclarationStarts[] =
				{
					"auto"
					, "char"
					, "const"
					, "double"
					, "enum"
					, "extern"
					, "float"
					, "inline"
					, "int"
					, "long"
					, "register"
					, "restrict"
					, "short"
					, "signed"
					, "static"
					, "struct"
					, "typedef"
					, "union"
					, "unsigned"
					, "void"
					, "volatile"
					, "_Alignas"
					, "alignas"
					, "_Atomic"
					, "_Bool"
					, "_Complex"
					, "_Static_assert"
					, "_Thread_local"
					, "bool"
					, "static_assert"
					, "thread_local"
					, "typeof"
					, "constexpr"
					, "typeof_unqual"
					, "__typeof__"
					, "__typeof"
					, "__typeof_unqual__"
					, "__typeof_unqual"
					, "_BitInt"
					, "_ExtInt"
					, "__attribute__"
					, "__attribute"
					, "__extension__"
					, "_Noreturn"
					, "__declspec"
					, "__int128"
					, "_Float16"
					, "_Float32"
					, "_Float64"
					, "_Float128"
					, "_Float32x"
					, "_Float64x"
					, "__fp16"
					, "__bf16"
					, "__float128"
					, "__ibm128"
					, "_Decimal32"
					, "_Decimal64"
					, "_Decimal128"
					, "__signed__"
					, "__signed"
					, "__const__"
					, "__const"
					, "__volatile__"
					, "__volatile"
					, "__inline__"
					, "__inline"
					, "__restrict__"
					, "__restrict"
					, "__thread"
					, "__auto_type"
					, "__complex__"
					, "__complex"
					, "__int8"
					, "__int16"
					, "__int32"
					, "__int64"
				}
			;

			auto const &First = Tokens[Inner.m_iFirstToken];
			for (auto pWord : c_pDeclarationStarts)
			{
				if (m_Tokens.f_IsText(First, pWord))
					return false;
			}

			auto iSecond = fp_NextCode(Inner.m_iFirstToken);
			if (iSecond >= 0)
			{
				// Objective-C's protocols and generics qualify a type with angle brackets, 'id<P> value;', where an expression
				// would only compare and discard.
				auto const &Second = Tokens[umint(iSecond)];
				if (Second.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Second, "*") || m_Tokens.f_IsText(Second, "<"))
					return false;

				// So does an attribute behind the type, which no subscript can start: 'T [[maybe_unused]] *x;'.
				auto iThird = fp_NextCode(umint(iSecond));
				if (m_Tokens.f_IsText(Second, "[") && iThird >= 0 && m_Tokens.f_IsText(Tokens[umint(iThird)], "["))
					return false;

				// A parenthesized declarator: 'T (*x)(int);', 'T ((x));', 'T (x)[2];', or a type the naming lists. A call
				// with one name for its argument, 'T (x);', is read as the call, which a C typedef can make a declaration
				// by decision: the typedefs are not known here, and refusing every such call would keep nearly all braces.
				// A name behind the parenthesis declares, as no expression goes on with one: 'DAlign(16) int x;', and so can an
				// initializer, 'T (x) = 1;', which a call's result cannot take in C but a macro's can, whose braces then stay.
				auto iGroup = m_Tokens.f_IsText(Second, "(") ? m_Structure.f_FindNodeOpeningAt(umint(iSecond)) : Nodes.f_GetLen();
				if (iGroup < Nodes.f_GetLen())
				{
					auto iInside = fp_NextCode(umint(iSecond));
					auto iBehind = fp_NextCode(Nodes[iGroup].m_iLastToken);
					bool bDeclarator
						= m_Tokens.f_HasRole(First, ECodeNameRole::mc_Type)
						|| (iInside >= 0 && (m_Tokens.f_IsText(Tokens[umint(iInside)], "*") || m_Tokens.f_IsText(Tokens[umint(iInside)], "(")))
						||
						(
							iBehind >= 0
							&&
							(
								m_Tokens.f_IsText(Tokens[umint(iBehind)], "(")
								|| m_Tokens.f_IsText(Tokens[umint(iBehind)], "[")
								|| Tokens[umint(iBehind)].m_Kind == ECodeTokenKind::mc_Identifier
								|| m_Tokens.f_IsText(Tokens[umint(iBehind)], "=")
							)
						)
					;
					if (bDeclarator)
						return false;
				}
			}
		}

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
		// Behind a case label on its line, a body within a few columns of the limit can have its braces taken and given back
		// by the next pass, which the repeated analysis settles.
		auto iElse = fp_LeadingElse(Guard.m_iFirstToken);
		auto nGuardIndent = fp_GetStatementIndent(iElse >= 0 ? umint(iElse) : Guard.m_iFirstToken);
		if (!fp_IsRangeOneLine(Block.m_Children[0], Inner.m_iFirstToken, Inner.m_iLastToken, nGuardIndent + nTab))
			return false;

		auto iCloseStart = Tokens[Inner.m_iLastToken].f_GetEnd();
		bool bTrailingComment = false;
		for (auto i = Inner.m_iLastToken + 1; i < Block.m_iLastToken; ++i)
		{
			auto const &Token = Tokens[i];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace || Kind == ECodeTokenKind::mc_Newline)
				continue;

			bool bComment = Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment;
			if (!bComment || bTrailingComment || Token.m_bMultiLine)
				return false;

			bool bOnStatementLine = true;
			for (auto iGap = Inner.m_iLastToken + 1; iGap < i; ++iGap)
				bOnStatementLine &= Tokens[iGap].m_Kind != ECodeTokenKind::mc_Newline;

			if (!bOnStatementLine)
				return false;

			// A block comment the brace stands behind would be left ending the line, for the next pass to convert.
			bool bBraceBehind = true;
			for (auto iGap = i + 1; bBraceBehind && iGap < Block.m_iLastToken; ++iGap)
				bBraceBehind = Tokens[iGap].m_Kind != ECodeTokenKind::mc_Newline;

			if (Kind == ECodeTokenKind::mc_BlockComment && bBraceBehind)
				return false;

			bTrailingComment = true;
			iCloseStart = Token.f_GetEnd();
		}

		// A comment behind the closing brace would land on the statement's line, so the
		// brace must end its line, or be followed by the 'else' the layout moves down; that
		// 'else' then starts a line of its own here, so a trailing comment never swallows it.
		// So does the closing brace of the block around it.
		auto iAfter = fp_NextCode(Block.m_iLastToken);
		bool bLastOnLine = fp_IsLastOnLine(Block.m_iLastToken);
		bool bElseFollows = iAfter >= 0 && m_Tokens.f_IsText(Tokens[umint(iAfter)], "else");

		// A comment in front of the 'else' keeps the braces wherever it stands, as the layout moves it onto the 'else''s
		// line, from where the gap the braces go from holds it as well: '} /* note */ else'.
		for (auto i = Block.m_iLastToken + 1; bElseFollows && i < umint(iAfter); ++i)
		{
			if (Tokens[i].m_Kind == ECodeTokenKind::mc_LineComment || Tokens[i].m_Kind == ECodeTokenKind::mc_BlockComment)
				return false;
		}
		auto iOuter = Statement.m_iParent;
		auto const *pOuter = iOuter < Nodes.f_GetLen() ? &Nodes[iOuter] : nullptr;
		bool bOuterCloses = iAfter >= 0 && pOuter && pOuter->m_Kind == ECodeNodeKind::mc_Block && pOuter->m_iLastToken == umint(iAfter);
		for (auto i = Block.m_iLastToken + 1; bOuterCloses && i < umint(iAfter); ++i)
			bOuterCloses = Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace;

		// And so does the next statement in the block, which the layout starts a line of its own with.
		// The children stand in source order, so the one starting there is found by halving.
		bool bStatementFollows = false;
		if (!bLastOnLine && !bElseFollows && !bOuterCloses && pOuter && iAfter >= 0)
		{
			auto const &Siblings = pOuter->m_Children;
			umint iLow = 0;
			umint iHigh = Siblings.f_GetLen();
			while (iLow < iHigh)
			{
				auto iMiddle = iLow + (iHigh - iLow) / 2;
				if (Nodes[Siblings[iMiddle]].m_iFirstToken < umint(iAfter))
					iLow = iMiddle + 1;
				else
					iHigh = iMiddle;
			}

			if (iLow < Siblings.f_GetLen())
			{
				auto const &Sibling = Nodes[Siblings[iLow]];
				bStatementFollows = Sibling.m_Kind == ECodeNodeKind::mc_Statement && Sibling.m_iFirstToken == umint(iAfter);
			}
		}

		for (auto i = Block.m_iLastToken + 1; bStatementFollows && i < umint(iAfter); ++i)
			bStatementFollows = Tokens[i].m_Kind == ECodeTokenKind::mc_Whitespace;

		if (!bLastOnLine && !bElseFollows && !bOuterCloses && !bStatementFollows)
			return false;

		auto iOpenStart = Tokens[Guard.m_iLastToken].f_GetEnd();
		auto nOpen = Tokens[Inner.m_iFirstToken].m_iOffset - iOpenStart;
		auto iCloseEnd = bLastOnLine ? Tokens[Block.m_iLastToken].f_GetEnd() : Tokens[umint(iAfter)].m_iOffset;
		auto nClose = iCloseEnd - iCloseStart;

		// The edits rewrite the gaps whole, so a comment in one, '} /* keep */ else', would go with the braces.
		auto fHoldsOnlyBraceAndBlanks = [&](umint _iStart, umint _nLength)
			{
				for (auto iChar = _iStart; iChar < _iStart + _nLength; ++iChar)
				{
					auto Char = m_pSource[iChar];
					if (Char != ' ' && Char != '\t' && Char != '\n' && Char != '\r' && Char != '{' && Char != '}')
						return false;
				}

				return true;
			}
		;
		if (!fHoldsOnlyBraceAndBlanks(iOpenStart, nOpen) || !fHoldsOnlyBraceAndBlanks(iCloseStart, nClose))
			return false;

		if
		(
			fp_IsDisabled(iOpenStart, nOpen)
			|| fp_IsDisabled(iCloseStart, nClose)
			|| fp_IsSuppressed(iOpenStart, nOpen)
			|| fp_IsSuppressed(iCloseStart, nClose)
			|| !fp_IsConversionSelected(iOpenStart, nOpen, iCloseStart, nClose)
		)
		{
			return false;
		}

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

		if (fp_IsInMacroArgument(Guard.m_iFirstToken))
			return false;

		// A guarded clause owns only its condition, and the braces go around all it starts: the statements it
		// guards in turn and the 'else' branches behind them, 'for (...)' over 'if (...)' over '{ ... }'.
		umint iLast = Statement.m_iLastToken;
		bool bChain = fp_IsGuard(_iStatement);
		bool bLabelled = !bChain && m_Tokens.f_IsText(Tokens[iLast], ":");
		if (bLabelled)
		{
			// A guarded label takes the statement it labels into the braces: 'if (b)' over 'Retry: f();'. Labels in a row
			// and a labelled clause or block keep their lines.
			if (Statement.m_iParent >= Nodes.f_GetLen())
				return false;

			auto const &Siblings = Nodes[Statement.m_iParent].m_Children;
			umint iIndex = 0;
			while (iIndex < Siblings.f_GetLen() && Siblings[iIndex] != _iStatement)
				++iIndex;

			if (iIndex + 1 >= Siblings.f_GetLen())
				return false;

			auto iLabelled = Siblings[iIndex + 1];
			auto const &Labelled = Nodes[iLabelled];
			auto const &LabelledLast = Tokens[Labelled.m_iLastToken];
			if (fp_IsGuard(iLabelled) || m_Tokens.f_IsText(Tokens[Labelled.m_iFirstToken], "{") || !m_Tokens.f_IsText(LabelledLast, ";"))
				return false;

			iLast = Labelled.m_iLastToken;
		}
		else if (bChain)
		{
			if (Statement.m_iParent >= Nodes.f_GetLen())
				return false;

			auto const &Siblings = Nodes[Statement.m_iParent].m_Children;
			umint iIndex = 0;
			while (iIndex < Siblings.f_GetLen() && Siblings[iIndex] != _iStatement)
				++iIndex;

			// Each 'if' of the chain takes one 'else'. One behind them all belongs to a clause outside the chain.
			umint nUnmatched = 0;
			while (true)
			{
				if (iIndex >= Siblings.f_GetLen())
					return false;

				if (fp_IsGuard(Siblings[iIndex]))
				{
					// A clause macro may expand to an 'if', which takes an 'else' that cannot be counted: 'IF(b) f(); else g();'.
					if (Nodes[Siblings[iIndex]].m_bClauseMacro)
						return false;

					// An 'else' whose 'if' is a guard of its own leaves that 'if' to be counted where it stands.
					auto iKey = Nodes[Siblings[iIndex]].m_iFirstToken;
					if (m_Tokens.f_IsText(Tokens[iKey], "else"))
					{
						auto iIf = fp_NextCode(iKey);
						bool bOwnGuard = iIf >= 0 && iIndex + 1 < Siblings.f_GetLen() && Nodes[Siblings[iIndex + 1]].m_iFirstToken == umint(iIf);
						iKey = iIf >= 0 && !bOwnGuard ? umint(iIf) : iKey;
					}

					nUnmatched += m_Tokens.f_IsText(Tokens[iKey], "if");
					++iIndex;

					continue;
				}

				// An attribute hides the clause it stands in front of, whose 'else' is then not counted: '[[likely]] if (b) ...'. A
				// lambda's captures start with one bracket.
				auto iSiblingFirst = Nodes[Siblings[iIndex]].m_iFirstToken;
				auto iSiblingSecond = fp_NextCode(iSiblingFirst);
				if (m_Tokens.f_IsText(Tokens[iSiblingFirst], "[") && iSiblingSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSiblingSecond)], "["))
					return false;

				if (nUnmatched && iIndex + 1 < Siblings.f_GetLen() && m_Tokens.f_IsText(Tokens[Nodes[Siblings[iIndex + 1]].m_iFirstToken], "else"))
				{
					--nUnmatched;
					++iIndex;

					continue;
				}

				break;
			}

			iLast = Nodes[Siblings[iIndex]].m_iLastToken;
			auto const &Last = Tokens[iLast];
			if (!m_Tokens.f_IsText(Last, ";") && !m_Tokens.f_IsText(Last, "}"))
				return false;
		}
		else if (!m_Tokens.f_IsText(Tokens[Statement.m_iLastToken], ";"))
			return false;

		if (m_Tokens.f_IsText(Tokens[Statement.m_iFirstToken], "{"))
			return false;

		auto nTab = m_Request.m_Settings.m_nTabWidth;

		// The 'if' of an 'else if' stands behind the 'else', whose line the braces go under.
		auto iElse = fp_LeadingElse(Guard.m_iFirstToken);
		auto nGuardIndent = fp_GetStatementIndent(iElse >= 0 ? umint(iElse) : Guard.m_iFirstToken);

		// The brace opens behind a '[[likely]]' or '[[unlikely]]' on the clause's line, which weighs the branch. Any other
		// attribute applies to the statement itself, which it has to stay on: '[[assume(x)]];', '[[clang::musttail]] return'.
		auto iOpenAfter = Guard.m_iLastToken;
		auto iFirst = Statement.m_iFirstToken;
		auto iSecond = fp_NextCode(iFirst);
		if (m_Tokens.f_IsText(Tokens[iFirst], "[") && iSecond >= 0 && m_Tokens.f_IsText(Tokens[umint(iSecond)], "["))
		{
			auto iName = fp_NextCode(umint(iSecond));
			auto iNameEnd = iName >= 0 ? fp_NextCode(umint(iName)) : aint(-1);
			bool bLikelihood
				= iName >= 0
				&& (m_Tokens.f_IsText(Tokens[umint(iName)], "likely") || m_Tokens.f_IsText(Tokens[umint(iName)], "unlikely"))
				&& iNameEnd >= 0
				&& m_Tokens.f_IsText(Tokens[umint(iNameEnd)], "]")
			;
			if (!bLikelihood)
				return false;

			for (auto iChild : Statement.m_Children)
			{
				if (Nodes[iChild].m_iFirstToken != iFirst)
					continue;

				iOpenAfter = Nodes[iChild].m_iLastToken;
				auto iNext = fp_NextCode(iOpenAfter);
				if (iNext < 0 || umint(iNext) > iLast)
					return false;

				iFirst = umint(iNext);

				break;
			}

			if (iOpenAfter == Guard.m_iLastToken)
				return false;

			// Another attribute behind it can hide a clause as well: '[[likely]] [[likely]] if (b) f(); else g();'.
			auto iAfterAttribute = fp_NextCode(iFirst);
			if (m_Tokens.f_IsText(Tokens[iFirst], "[") && iAfterAttribute >= 0 && m_Tokens.f_IsText(Tokens[umint(iAfterAttribute)], "["))
				return false;

			// A clause behind the attribute was not read as one, so the 'else' branches it takes are not known here:
			// 'if (a) [[likely]] if (b) f(); else g();'.
			for (auto pClause : {"if", "for", "while", "switch", "do", "try"})
			{
				if (m_Tokens.f_IsText(Tokens[iFirst], pClause))
					return false;
			}
		}

		// Behind the attribute the statement can be a block already: 'if (a) [[unlikely]] {'.
		// The builder then reads the 'else' behind that block as more of the statement, and
		// braces around the two would take the 'else' away from its clause. No statement
		// that holds one at its own level is ever wrapped.
		if (m_Tokens.f_IsText(Tokens[iFirst], "{"))
			return false;

		for (auto i = iFirst; !bChain && i <= iLast; ++i)
		{
			if (m_TokenDepth[i] == m_TokenDepth[iFirst] && m_Tokens.f_IsText(Tokens[i], "else"))
				return false;
		}

		// A 'do' or a 'try' in the chain goes on past where its body ends, 'while (c);' or 'catch', which the chain's walk does
		// not follow, and a directive in it can end the chain in one branch and not in another.
		for (auto i = iFirst; i <= iLast; ++i)
		{
			if (m_Tokens.f_IsText(Tokens[i], "do") || m_Tokens.f_IsText(Tokens[i], "try") || Tokens[i].m_Kind == ECodeTokenKind::mc_Preprocessor)
				return false;
		}

		// A guard the statement is in turn puts what it guards on a line of its own, so a chain is never one line.
		if (bClauseFits && !bChain && fp_IsRangeOneLine(_iStatement, iFirst, iLast, nGuardIndent + nTab))
			return false;

		for (auto i = iOpenAfter + 1; i < iFirst; ++i)
		{
			auto Kind = Tokens[i].m_Kind;
			if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
				return false;
		}

		auto iCloseAt = Tokens[iLast].f_GetEnd();
		for (auto i = iLast + 1; i < Tokens.f_GetLen(); ++i)
		{
			auto const &Token = Tokens[i];
			auto Kind = Token.m_Kind;
			if (Kind == ECodeTokenKind::mc_Whitespace)
				continue;

			if ((Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment) && !Token.m_bMultiLine)
			{
				iCloseAt = Token.f_GetEnd();

				continue;
			}

			break;
		}

		auto iOpenStart = Tokens[iOpenAfter].f_GetEnd();
		auto nOpen = Tokens[iFirst].m_iOffset - iOpenStart;
		if
		(
			fp_IsDisabled(iOpenStart, nOpen)
			|| fp_IsDisabled(iCloseAt, 0)
			|| fp_IsSuppressed(iOpenStart, nOpen)
			|| fp_IsSuppressed(iCloseAt, 0)
			|| !fp_IsConversionSelected(iOpenStart, nOpen, iCloseAt, 0)
		)
		{
			return false;
		}

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
}
