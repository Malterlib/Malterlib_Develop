// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingAnalyzer.h"

namespace NMib::NDevelop::NPrivate
{
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

			auto const &Token = Tokens[i];
			if (!m_Tokens.f_IsText(Token, ".") && !m_Tokens.f_IsText(Token, "->"))
				continue;

			auto iBefore = fp_PreviousCode(i);
			if (iBefore < 0 || umint(iBefore) < _iFirst)
				continue;

			// An arrow behind a parameter list or a function's qualifiers introduces a
			// trailing return type, which is a break of its own and not a member access.
			auto const &Before = Tokens[umint(iBefore)];
			if (m_Tokens.f_IsText(Token, "->") && fp_IsTrailingReturnArrow(i))
				continue;

			bool bOperand
				= Before.m_Kind == ECodeTokenKind::mc_Identifier
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
			bool bScope = Before.m_Kind == ECodeTokenKind::mc_Identifier || (m_Structure.f_IsAngleBracket(umint(iBefore)) && m_Tokens.f_IsText(Before, ">"));
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
	//
	// A head that does not split is tried again inside each longer head that holds it, the next scope's head on the same
	// line, so the outermost layout remembers the heads that failed: without it a run of calls, 'A(1) B(2) C(3)', takes
	// twice as long for every call in it.
	bool CFormattingAnalyzer::fp_LayoutHead(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, umint _nContinuation)
	{
		if (!m_nHeadDepth)
			m_FailedHeads.f_Clear();

		for (umint i = 0; i + 3 < m_FailedHeads.f_GetLen(); i += 4)
		{
			bool bFailed = m_FailedHeads[i] == _iFirst && m_FailedHeads[i + 1] == _iLast && m_FailedHeads[i + 2] == _iIndent && m_FailedHeads[i + 3] == _nContinuation;
			if (bFailed)
				return false;
		}

		if (fp_BreakAtQualification(_iFirst, _iLast, _iIndent, _nContinuation))
			return true;

		auto iFirstParen = m_iSplitFirstParen;
		m_iSplitFirstParen = 0;
		++m_nHeadDepth;
		bool bSplit = fp_LayoutScopes(_iNode, _iFirst, _iLast, _iIndent, false, _nContinuation != _iIndent) || fp_LayoutMembers(_iNode, _iFirst, _iLast, _iIndent, _nContinuation);
		--m_nHeadDepth;
		m_iSplitFirstParen = iFirstParen;

		if (!bSplit)
		{
			m_FailedHeads.f_Insert(_iFirst);
			m_FailedHeads.f_Insert(_iLast);
			m_FailedHeads.f_Insert(_iIndent);
			m_FailedHeads.f_Insert(_nContinuation);
		}

		return bSplit;
	}

	// Fills lines greedily from the range's start: a line that does not fit is first broken
	// in front of a scope that follows another, or a trailing return type, and only when no
	// such break helps is the first scope on the line opened up. What follows an opened
	// scope starts under its closing marker and is filled the same way.
	umint const *CFormattingAnalyzer::CChildSpan::begin() const
	{
		return m_pBegin;
	}

	umint const *CFormattingAnalyzer::CChildSpan::end() const
	{
		return m_pEnd;
	}

	// The children of the node that overlap the range, which stand in the order of their tokens.
	CFormattingAnalyzer::CChildSpan CFormattingAnalyzer::fp_ChildrenIn(CCodeNode const &_Node, umint _iFirst, umint _iLast) const
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const *pChildren = _Node.m_Children.f_GetArray();
		umint nChildren = _Node.m_Children.f_GetLen();
		umint iBegin = 0;
		umint iEnd = nChildren;
		while (iBegin < iEnd)
		{
			auto iMiddle = iBegin + (iEnd - iBegin) / 2;
			if (Nodes[pChildren[iMiddle]].m_iLastToken < _iFirst)
				iBegin = iMiddle + 1;
			else
				iEnd = iMiddle;
		}

		auto iLast = iBegin;
		iEnd = nChildren;
		while (iLast < iEnd)
		{
			auto iMiddle = iLast + (iEnd - iLast) / 2;
			if (Nodes[pChildren[iMiddle]].m_iFirstToken <= _iLast)
				iLast = iMiddle + 1;
			else
				iEnd = iMiddle;
		}

		return CChildSpan{pChildren + iBegin, pChildren + iLast};
	}

	bool CFormattingAnalyzer::fp_LayoutScopes(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndent, bool _bMustSplit)
	{
		auto const &Nodes = m_Structure.f_GetNodes();
		auto const &Node = Nodes[_iNode];
		// Only the children inside the range: a list's elements are laid out one by one, and passing every child of the
		// list for each would make a long one quadratic.
		auto Children = fp_ChildrenIn(Node, _iFirst, _iLast);
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
			if (m_TokenDepth[i] != m_TokenDepth[_iFirst] || !fp_IsAssignment(i))
				continue;

			// The '=' of 'operator =' is the name of the function it declares.
			auto iBefore = fp_PreviousCode(i);
			if (iBefore >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBefore)], "operator"))
				continue;

			iAssign = i;
		}

		// Behind a declaration's parameter list the '=' makes it pure, defaulted or deleted,
		// and gives nothing a value. Any other value is assigned to what a call yields.
		if (bStatement && m_iSplitFirstParen && iAssign > m_iSplitFirstParen)
		{
			auto iValue = fp_NextCode(iAssign);
			auto const &Value = iValue >= 0 ? m_Tokens.f_GetTokens()[umint(iValue)] : m_Tokens.f_GetTokens()[iAssign];
			if (iValue < 0 || m_Tokens.f_IsText(Value, "0") || m_Tokens.f_IsText(Value, "default") || m_Tokens.f_IsText(Value, "delete"))
				iAssign = 0;
		}

		// What stands in front of a range-based for's ':' declares the element: 'for (TCOptional<T> Value : Values)'.
		umint iRange = 0;
		if (auto iFor = bElement && !iAssign ? fp_PreviousCode(Node.m_iFirstToken) : aint(-1); iFor >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iFor)], "for"))
		{
			for (umint i = _iFirst; i <= _iLast && !iRange; ++i)
			{
				if (m_TokenDepth[i] == m_TokenDepth[_iFirst] && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], ":"))
					iRange = i;
			}
		}

		// A braced initializer behind the declared name gives it its value as an '=' does: 'TCVector<T> Values{...}', also
		// in a condition: 'if (TCOptional<T> Value{...})'.
		umint iInitializer = 0;
		for (auto iChild : Children)
		{
			auto const &Child = Nodes[iChild];
			if ((!bStatement && !bElement) || iAssign || Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_iFirstToken < _iFirst || Child.m_iLastToken > _iLast)
				continue;

			if (Child.m_Bracket == ECodeBracket::mc_Paren)
				break;

			// The name can be a variable template's specialization: 'TCFoo<int> g_Value<bool>{...}'.
			auto iName = fp_PreviousCode(Child.m_iFirstToken);
			if (iName >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iName)], ">") && m_Structure.f_IsAngleBracket(umint(iName)))
			{
				auto iArguments = m_Structure.f_FindNodeClosingAt(umint(iName));
				iName = iArguments < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iArguments].m_iFirstToken) : aint(-1);
			}

			if (Child.m_Bracket != ECodeBracket::mc_Brace || iName < 0 || m_Tokens.f_GetTokens()[umint(iName)].m_Kind != ECodeTokenKind::mc_Identifier)
				continue;

			iInitializer = Child.m_iFirstToken;

			break;
		}

		TCVector<umint> Scopes;
		TCVector<umint> Placements;
		for (umint iPass = 0; iPass < 2; ++iPass)
		{
			for (auto iChild : Children)
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
				bool bPlacement
					= Child.m_Bracket == ECodeBracket::mc_Paren
					&& iBeforeScope >= 0
					&& m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBeforeScope)], "new")
					&& !(iBeforeNew >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBeforeNew)], "operator"))
				;
				if (bPlacement && fp_FitsInline(umint(iBeforeScope), Child.m_iLastToken, _iIndent))
					fp_MarkInline(umint(iBeforeScope), Child.m_iLastToken);

				// The type a placement constructs starts a line of its own before anything in it
				// opens: 'new(pMemory)' over 'TCFoo<t_C>(_Params)'.
				if (bPlacement && iPass && Child.m_iLastToken < _iLast)
					Placements.f_Insert(Child.m_iLastToken);

				// A parenthesis holding a name alone in front of a parenthesis or a brace where no name or operand stands in
				// front of it, or behind another such, is a cast as readily as a call, '(__m512i)(__v16si){...}', and has
				// nothing in it to open.
				auto iBehindScope = fp_NextCode(Child.m_iLastToken);
				bool bCastPair
					= fp_HoldsNameAlone(iChild)
					&&
					(
						(iBeforeScope >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBeforeScope)], ")") && fp_HoldsNameAlone(m_Structure.f_FindNodeClosingAt(umint(iBeforeScope))))
						||
						(
							iBehindScope >= 0
							&& (m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBehindScope)], "{") || m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBehindScope)], "("))
							&& !fp_EndsOperand(iBeforeScope)
						)
					)
				;

				// An attribute's brackets are no scope either: '[[nodiscard]]'.
				auto iInnerScope = fp_NextCode(Child.m_iFirstToken);

				auto fSend = [&](aint _iOpen)
					{
						auto iNode = _iOpen >= 0 ? m_Structure.f_FindNodeOpeningAt(umint(_iOpen)) : Nodes.f_GetLen();

						return iNode < Nodes.f_GetLen() && Nodes[iNode].m_bMessageSend;
					}
				;

				bool bAttribute
					= Child.m_Bracket == ECodeBracket::mc_Square
					&& !Child.m_bMessageSend
					&&
					(
						(iInnerScope >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iInnerScope)], "[") && !fSend(iInnerScope))
						|| (iBeforeScope >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iBeforeScope)], "[") && !fSend(iBeforeScope))
					)
				;

				// Nor are the parentheses of one spelled with a keyword: '__attribute__((__internal_linkage__))'.
				if (Child.m_Bracket == ECodeBracket::mc_Paren && iBeforeScope >= 0)
				{
					auto const &Keyword = m_Tokens.f_GetTokens()[umint(iBeforeScope)];
					for (auto pKeyword : {"__attribute__", "__attribute", "__declspec", "alignas", "_Alignas"})
						bAttribute |= m_Tokens.f_IsText(Keyword, pKeyword);
				}

				if (fp_IsCastGroup(iChild) || bPlacement || bCastPair || bAttribute || fp_IsStructuredBinding(iChild))
					continue;

				// What stands in front of a statement's '=' declares what the value is given
				// to, and is no more a place to break than what stands in front of a name.
				bool bBeforeName
					= (bStatement && Child.m_iLastToken < m_iSplitFirstParen) || Child.m_iLastToken < iAssign || Child.m_iLastToken < iInitializer || Child.m_iLastToken < iRange
				;

				// The template arguments of a class a name is qualified with stand in front of
				// that name too: 'TCFoo<CBar>::f_Function'.
				auto iQualifies = fp_NextCode(Child.m_iLastToken);
				bBeforeName
					|= Child.m_Bracket == ECodeBracket::mc_Angle && iQualifies >= 0 && umint(iQualifies) <= _iLast && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iQualifies)], "::")
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
				// name only gives when it still does not fit in front of them. So does one in
				// front of a braced initializer: 'TCFoo<T>{...}'.
				if (Child.m_Bracket == ECodeBracket::mc_Angle)
				{
					auto iAfter = fp_NextCode(Child.m_iLastToken);
					bool bNamesCall = false;
					for (auto iOther : Children)
					{
						auto const &Other = Nodes[iOther];
						bool bArguments = Other.m_Bracket == ECodeBracket::mc_Paren || Other.m_Bracket == ECodeBracket::mc_Brace;
						if (Other.m_Kind != ECodeNodeKind::mc_Group || !bArguments || Other.m_iLastToken > _iLast)
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
		// An operator's name is no name the parameter list can open behind, so its declaration
		// gives at the '::' wherever it does not fit: 'TCFoo<...>' over '::operator + (...)'.
		umint iParameters = m_iSplitFirstParen;
		bool bOperator = false;
		for (auto i = _iFirst; bStatement && !iParameters && i <= _iLast; ++i)
		{
			auto const &AllTokens = m_Tokens.f_GetTokens();
			if (m_TokenDepth[i] != m_TokenDepth[_iFirst] || !m_Tokens.f_IsText(AllTokens[i], "operator"))
				continue;

			for (auto iParen = i + 1; iParen <= _iLast; ++iParen)
			{
				if (m_TokenDepth[iParen] != m_TokenDepth[_iFirst] || !m_Tokens.f_IsText(AllTokens[iParen], "("))
					continue;

				auto iAfter = fp_NextCode(iParen);
				auto iBefore = fp_PreviousCode(iParen);
				// The brackets of 'operator ()' are its name.
				if (iAfter >= 0 && m_Tokens.f_IsText(AllTokens[umint(iAfter)], ")") && iBefore >= 0 && umint(iBefore) == i)
					continue;

				iParameters = iParen;
				bOperator = true;

				break;
			}

			break;
		}

		// The name in front of a known parameter list can be an operator's as well.
		if (bStatement && iParameters && !bOperator)
		{
			auto iName = fp_PreviousCode(iParameters);
			for (umint nSteps = 0; iName >= 0 && nSteps < 3 && !bOperator; ++nSteps, iName = fp_PreviousCode(umint(iName)))
				bOperator = m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iName)], "operator");
		}

		bool bNameFits = !bOperator && (!bStatement || !iParameters || iParameters > _iLast || fp_FitsInline(_iFirst, iParameters, _iIndent));
		if (bNameFits && bStatement && iParameters && iParameters < _iLast)
		{
			auto iInner = fp_NextCode(iParameters);
			bNameFits = iInner < 0 || !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iInner)], ")");
		}

		// The arguments stay closed where the '::' gives: 'TCFoo<...>' over '::operator == (...) const'
		// over 'noexcept'.
		umint iQualifiedBreak = 0;
		umint iQualifiedScope = 0;
		for (auto iChild : Children)
		{
			auto const &Child = Nodes[iChild];
			if (bNameFits || _bClause || Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_Bracket != ECodeBracket::mc_Angle)
				continue;

			if (Child.m_iFirstToken < _iFirst || Child.m_iLastToken >= iParameters || Child.m_iLastToken >= _iLast)
				continue;

			auto iColons = fp_NextCode(Child.m_iLastToken);
			if (iColons < 0 || !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iColons)], "::"))
				continue;

			if (!fp_FitsInline(_iFirst, Child.m_iLastToken, _iIndent))
				continue;

			// Where only the name itself follows, the '::' is one of the line's breaks, so an
			// exception specification or a trailing return type behind it gives first.
			bool bName = iParameters <= _iLast;
			for (auto i = umint(iColons) + 1; bName && i < iParameters; ++i)
				bName = m_TokenDepth[i] == m_TokenDepth[umint(iColons)] && !m_Tokens.f_IsText(m_Tokens.f_GetTokens()[i], "::");

			// Without either behind the parameter list there is nothing to give first, and a list with
			// parameters opens behind the whole name instead.
			auto iInner = bName ? fp_NextCode(iParameters) : aint(-1);
			bool bGivesBehind = iInner >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iInner)], ")");

			// The range can end in front of a return type the statement was already cut at.
			for (auto i = iParameters + 1; bName && !bGivesBehind && i <= Node.m_iLastToken; ++i)
			{
				if (m_TokenDepth[i] != m_TokenDepth[umint(iColons)])
					continue;

				auto const &Token = m_Tokens.f_GetTokens()[i];
				if (m_Tokens.f_IsText(Token, "{") || m_Tokens.f_IsText(Token, ";"))
					break;

				bGivesBehind = m_Tokens.f_IsText(Token, "noexcept") || fp_IsTrailingReturnArrow(i);
			}

			if (bName && !bGivesBehind)
				break;

			if (bName)
			{
				iQualifiedBreak = umint(iColons);
				iQualifiedScope = iChild;

				break;
			}

			// A further qualification or template argument list behind it is where the line gives
			// instead.
			if (!fp_FitsInline(umint(iColons), _iLast, nContinuation))
				break;

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
			bool bSettled = iHeadEnd >= 0 && umint(iHeadEnd) >= _iFirst && iAssign < _iLast && fp_GetCanonicalSpacing(umint(iHeadEnd), iAssign) == ECodeSpacing::mc_Space;
			bool bHeadFits = bSettled && fp_FitsInline(_iFirst, umint(iHeadEnd), _iIndent);

			// So does one whose first scope cannot be reached on the line, which would otherwise give
			// inside the value's own name: 'TCFoo<...> pValue' over '= NStorage::TCFoo<...>(_pValue)'.
			bool bScopeOutOfReach = false;
			if (bNamedScope && !bElement)
			{
				for (auto iChild : Children)
				{
					auto const &Child = Nodes[iChild];
					if (Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_iFirstToken <= iAssign || Child.m_iLastToken > _iLast)
						continue;

					if (Child.m_Bracket == ECodeBracket::mc_Angle)
						continue;

					if (Child.m_Bracket != ECodeBracket::mc_Paren)
						break;

					auto iScopeHead = fp_PreviousCode(Child.m_iFirstToken);
					bScopeOutOfReach = iScopeHead >= 0 && !fp_FitsInline(_iFirst, umint(iScopeHead), _iIndent);

					break;
				}
			}

			// And so does the value of an assignment to what a member call yields, which the call's
			// arguments would otherwise be opened behind: 'Object.f_CreateMember(Name)' over '= fg_Value(...)'.
			bool bTargetCall = false;
			for (auto iChild : Children)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_iFirstToken >= iAssign)
					break;

				if (Child.m_Kind != ECodeNodeKind::mc_Group || Child.m_Bracket != ECodeBracket::mc_Paren || Child.m_iFirstToken < _iFirst)
					continue;

				auto iCallee = fp_PreviousCode(Child.m_iFirstToken);
				auto iMember = iCallee >= 0 ? fp_PreviousCode(umint(iCallee)) : aint(-1);
				auto const *pMember = iMember >= 0 ? &AllTokens[umint(iMember)] : nullptr;
				bTargetCall
					= iCallee >= 0
					&& AllTokens[umint(iCallee)].m_Kind == ECodeTokenKind::mc_Identifier
					&& pMember
					&& umint(iMember) >= _iFirst
					&& (m_Tokens.f_IsText(*pMember, ".") || m_Tokens.f_IsText(*pMember, "->"))
				;
				if (bTargetCall)
					break;
			}

			if (bHeadFits && (!bNamedScope || bElement || bScopeOutOfReach || bTargetCall) && fp_FitsInline(iAssign, _iLast, nContinuation))
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
				auto const *pQualifier = iQualifier >= 0 ? &AllTokens[umint(iQualifier)] : nullptr;
				if
				(
					iNameFirst != umint(iHeadEnd)
					&& pQualifier
					&& (pQualifier->m_Kind == ECodeTokenKind::mc_Identifier)
					&& (m_Tokens.f_IsText(*pQualifier, "const") || m_Tokens.f_IsText(*pQualifier, "volatile"))
				)
				{
					iNameFirst = umint(iQualifier);
				}

				auto iTypeEnd = fp_PreviousCode(iNameFirst);
				bool bNameDown = iTypeEnd >= 0 && umint(iTypeEnd) >= _iFirst && fp_FitsInline(_iFirst, umint(iTypeEnd), _iIndent) && fp_FitsInline(iNameFirst, _iLast, nContinuation);
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
			auto const &Scope = Nodes[Scopes[i]];
			auto iBefore = fp_PreviousCode(Scope.m_iFirstToken);
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
			auto iInner = fp_NextCode(Scope.m_iFirstToken);
			if (!bIntroducer && iInner >= 0 && umint(iInner) == Scope.m_iLastToken)
				continue;

			// A parameter list behind a pointer to function's declarator opens as a function's does, rather than
			// moving down whole: 'NTSTATUS (NTAPI *m_fQuery)' over '(' over 'IN HANDLE _Thread'.
			if (!bIntroducer && fg_ClosesParameterList(m_Tokens, m_Structure, Scope.m_iLastToken))
				continue;

			// A parenthesis holding a name alone is a cast or the name itself, and either way stays with what it stands in
			// front of: '(__m512i)(__v16si){...}'.
			if (!bIntroducer && m_Tokens.f_IsText(Before, ")") && fp_HoldsNameAlone(m_Structure.f_FindNodeClosingAt(umint(iBefore))))
				continue;

			Breaks.f_Insert(Scope.m_iFirstToken);
			Introducer.f_Insert(bIntroducer);
		}

		auto const &Tokens = m_Tokens.f_GetTokens();
		auto nLevel = m_TokenDepth[_iFirst];
		for (auto iPlacement : Placements)
		{
			auto iType = fp_NextCode(iPlacement);
			if (iType < 0 || umint(iType) > _iLast)
				continue;

			// Only where the type does not fit behind the placement up to its own argument list,
			// which opens instead otherwise.
			umint iTypeLast = _iLast;
			for (auto iScopeNode : Scopes)
			{
				if (Nodes[iScopeNode].m_iFirstToken <= umint(iType))
					continue;

				auto iHead = fp_PreviousCode(Nodes[iScopeNode].m_iFirstToken);
				if (iHead >= 0)
					iTypeLast = fg_Min(iTypeLast, umint(iHead));
			}

			if (fp_FitsInline(_iFirst, iTypeLast, _iIndent))
				continue;

			Breaks.f_Insert(umint(iType));
			Introducer.f_Insert(false);
		}

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
				// An attribute macro behind the parameter list goes down with the return type:
				// '(int _A)' over 'DMibSuppressUndefinedSanitizer -> void *'.
				umint iBreak = i;
				auto iList = iBefore >= 0 ? fp_PreviousCode(umint(iBefore)) : aint(-1);
				bool bAttribute
					= bArrow
					&& Tokens[umint(iBefore)].m_Kind == ECodeTokenKind::mc_Identifier
					&& !fp_IsFunctionQualifier(umint(iBefore))
					&& iList >= 0
					&& umint(iList) >= _iFirst
					&& fg_ClosesParameterList(m_Tokens, m_Structure, umint(iList))
				;
				if (bAttribute)
					iBreak = umint(iBefore);

				Breaks.f_Insert(iBreak);
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

		// The '::' is only taken where the line up to its next break does not fit without it.
		if (iQualifiedBreak)
		{
			umint iNext = 0;
			while (iNext < Breaks.f_GetLen() && Breaks[iNext] <= iQualifiedBreak)
				++iNext;

			auto iHeadLast = iNext < Breaks.f_GetLen() ? fp_PreviousCode(Breaks[iNext]) : aint(_iLast);
			if (iHeadLast >= 0 && !fp_FitsInline(_iFirst, umint(iHeadLast), _iIndent))
			{
				Breaks.f_Insert(iQualifiedBreak);
				Introducer.f_Insert(false);
				for (umint j = Breaks.f_GetLen() - 1; j > iNext; --j)
				{
					fg_Swap(Breaks[j - 1], Breaks[j]);
					fg_Swap(Introducer[j - 1], Introducer[j]);
				}

				for (umint i = 0; i < Scopes.f_GetLen(); ++i)
				{
					if (Scopes[i] == iQualifiedScope)
					{
						Scopes.f_Remove(i);

						break;
					}
				}
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
				auto iCandidate = Breaks[i];
				if (iCandidate <= iLineFirst)
					continue;

				auto iBefore = fp_PreviousCode(iCandidate);
				if (iBefore < 0 || umint(iBefore) < iLineFirst)
					continue;

				if (!fp_FitsInline(iLineFirst, umint(iBefore), nLineIndent))
					break;

				// A call on what another call yields spells one call expression with it,
				// 'f_CallActor(&C::f_Fn)(_Params)', and moves down only where the line has
				// nothing later to give. A scope holding a lambda body is such a thing: it
				// opens below whatever else is done, and what stands in front of it stays on
				// the line where it fits, the whole call expression included.
				bool bYielded = m_Tokens.f_IsText(Tokens[iCandidate], "(") && m_Tokens.f_IsText(Tokens[umint(iBefore)], ")");
				if (bYielded)
				{
					bool bBodyBelow = false;
					for (auto iOther : Scopes)
					{
						auto const &Other = Nodes[iOther];
						if (!Other.m_bHasBlock || Other.m_iFirstToken <= iCandidate)
							continue;

						auto iBodyHead = fp_PreviousCode(Other.m_iFirstToken);
						bBodyBelow |= iBodyHead >= 0 && umint(iBodyHead) >= iLineFirst && fp_FitsInline(iLineFirst, umint(iBodyHead), nLineIndent);
					}

					if (bBodyBelow)
						continue;
				}

				// A parameter list moves below its capture list to stand with what follows it. Where
				// that line gives at its specification or its return type anyway, the list stays
				// behind the capture list: '[&](auto &&_Key)' over 'noexcept(...)' over '-> void'.
				bool bParameters = Introducer[i] && m_Tokens.f_IsText(Tokens[iCandidate], "(") && i + 1 < Breaks.f_GetLen() && !Introducer[i + 1];
				if (bParameters)
				{
					auto iParametersLast = fp_PreviousCode(Breaks[i + 1]);
					if (iParametersLast >= 0 && fp_FitsInline(iLineFirst, umint(iParametersLast), nLineIndent) && !fp_FitsInline(iCandidate, _iLast, nContinuation))
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
				auto const *pMember = iMember >= 0 ? &Tokens[umint(iMember)] : nullptr;
				bool bChained
					= !Link.m_bHasBlock
					&& Link.m_Bracket == ECodeBracket::mc_Paren
					&& fp_IsNamedCast(Link.m_iFirstToken)
					&& pMember
					&& umint(iMember) <= _iLast
					&& (m_Tokens.f_IsText(*pMember, ".") || m_Tokens.f_IsText(*pMember, "->"))
				;

				umint iNextScope = TCLimitsInt<umint>::mc_Max;
				for (auto iChild : Children)
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

				// An empty call on what the scope yields is no link, and stays on its line: 'f_CallActor(&C::f_Fn)()'.
				umint iLinkEnd = Link.m_iLastToken;
				auto iMember = fp_NextCode(iLinkEnd);
				while (iMember >= 0 && umint(iMember) < _iLast && m_Tokens.f_IsText(Tokens[umint(iMember)], "("))
				{
					auto iClose = fp_NextCode(umint(iMember));
					if (iClose < 0 || !m_Tokens.f_IsText(Tokens[umint(iClose)], ")"))
						break;

					iLinkEnd = umint(iClose);
					iMember = fp_NextCode(iLinkEnd);
				}

				bool bOpens = !Link.m_bHasBlock && iInner >= 0 && umint(iInner) != Link.m_iLastToken && Link.m_iFirstToken >= iLineFirst;
				auto const *pMember = iMember >= 0 ? &Tokens[umint(iMember)] : nullptr;
				bool bMember = pMember && umint(iMember) <= _iLast && (m_Tokens.f_IsText(*pMember, ".") || m_Tokens.f_IsText(*pMember, "->")) && !fp_IsTrailingReturnArrow(umint(iMember));

				// The name a type declares resumes under its argument list the same way, and
				// takes the line below the type on its own: 'TCMap<CStr, CValue>' with
				// 'm_Values' under it.
				bool bDeclared
					= pMember
					&& umint(iMember) <= _iLast
					&& Link.m_Bracket == ECodeBracket::mc_Angle
					&& (pMember->m_Kind == ECodeTokenKind::mc_Identifier || fg_IsDeclaratorToken(m_Tokens, m_Structure, umint(iMember)))
				;
				if (bDeclared)
				{
					auto iName = umint(iMember);
					while (iName < _iLast && fg_IsDeclaratorToken(m_Tokens, m_Structure, iName))
						iName = umint(fp_NextCode(iName));

					auto const &Name = Tokens[iName];
					bDeclared = Name.m_Kind == ECodeTokenKind::mc_Identifier && !m_Tokens.f_IsText(Name, "final") && fp_FitsInline(umint(iMember), _iLast, nContinuation);
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
					auto const *pBehindCall = iBehindCall >= 0 ? &Tokens[umint(iBehindCall)] : nullptr;
					bool bAccessBehind = pBehindCall && umint(iBehindCall) <= _iLast && (m_Tokens.f_IsText(*pBehindCall, ".") || m_Tokens.f_IsText(*pBehindCall, "->"));
					umint nLink = 0;
					umint nCall = 0;
					bool bCallHolds
						= bOneAccess
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

				// The chain starts at the first access on what a call, a subscript or a braced value yields, an
				// empty one included, which takes its line with the rest: 'CFoo{}' over '.f_SetNotes(...)' over
				// '.f_SetTags(...)', and 'fg_CurrentActor()' over '.f_Bind<...>(...)'.
				if (bMember)
				{
					for (auto i = iLineFirst + 1; i < Link.m_iFirstToken; ++i)
					{
						auto const &Token = Tokens[i];
						if (m_TokenDepth[i] != m_TokenDepth[Link.m_iFirstToken] || (!m_Tokens.f_IsText(Token, ".") && !m_Tokens.f_IsText(Token, "->")))
							continue;

						auto iOperand = fp_PreviousCode(i);
						auto iYielded = iOperand >= 0 ? m_Structure.f_FindNodeClosingAt(umint(iOperand)) : Nodes.f_GetLen();
						auto const *pYielded = iYielded < Nodes.f_GetLen() ? &Nodes[iYielded] : nullptr;
						if (!pYielded || pYielded->m_Kind != ECodeNodeKind::mc_Group || pYielded->m_Bracket == ECodeBracket::mc_Angle)
							continue;

						if (fp_FitsInline(iLineFirst, umint(iOperand), nLineIndent))
						{
							iLinkEnd = umint(iOperand);
							iMember = aint(i);
						}

						break;
					}
				}

				if (bOpens && (bMember || bDeclared) && fp_FitsInline(iLineFirst, iLinkEnd, nLineIndent))
				{
					fp_MarkInline(iLineFirst, iLinkEnd);
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

			// An empty scope has nothing to open, so the line gives at a later scope or not at all: 's()()()'. A list written
			// open is opened all the same.
			auto fWrittenOpen = [&](umint _iScope)
				{
					return Nodes[_iScope].m_bWrittenOpen && _iScope < m_KeepsListLines.f_GetLen() && !m_KeepsListLines[_iScope];
				}
			;

			auto iInner = fp_NextCode(Nodes[Scopes[iScope]].m_iFirstToken);
			if (iInner >= 0 && umint(iInner) == Nodes[Scopes[iScope]].m_iLastToken && !fWrittenOpen(Scopes[iScope]))
			{
				// A comment or directive ending its line inside puts the closing marker at the start of the next, where the
				// scope would open, and the line continues behind the marker.
				auto iCloser = Nodes[Scopes[iScope]].m_iLastToken;
				auto iPrevious = fp_PreviousSignificant(iCloser);
				if (iPrevious > aint(Nodes[Scopes[iScope]].m_iFirstToken) && Tokens[umint(iPrevious)].m_bMultiLine)
				{
					if (iLineFirst == _iFirst && Nodes[Scopes[iScope]].m_iFirstToken != iLineFirst)
						nLineIndent = nContinuation;

					fp_BreakBefore(iCloser, nLineIndent);
					bSplit = true;
					iLineFirst = iCloser;
				}

				++iScope;

				continue;
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
					// A line a block's move only re-indents is none the head's layout started.
					bSplit = true;

					umint iLine = iLineFirst;
					for (umint i = iLineFirst + 1; i <= umint(iHead); ++i)
					{
						if (fp_IsBreakGap(i) && EGap(m_GapState[i]) != EGap::mc_Indent)
							iLine = i;
					}

					if (iLine == iLineFirst)
						continue;

					nLineIndent = m_GapIndent[iLine];

					bool bCloser = false;
					for (auto iChild : Children)
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
				bClassArguments
					= (m_Tokens.f_IsText(Keyword, "struct") || m_Tokens.f_IsText(Keyword, "class") || m_Tokens.f_IsText(Keyword, "union"))
					&& iName >= 0
					&& fp_NextCode(umint(iName)) == aint(Scope.m_iFirstToken)
				;
			}

			// A line the statement continues on already stands at its continuation level, and
			// behind a leading parenthesis's closing marker at the statement's, which has none:
			// ')' over '.f_Call' over '('.
			auto nMarkerIndent = bStartsLine || bClassArguments || iLineFirst != _iFirst ? nLineIndent : nContinuation;

			// With the project's TrailingScopeAtStatementLevel, a scope that ends the statement's only expression
			// opens at the statement's level, as one the statement opens with does: 'auto Value = fg_Function' over
			// '(' over '5' over ');'. A parameter list is a declaration's, which its return type moves behind instead. A scope
			// holding a directive, whose line takes the scope's opening onto a line of its own, is read by the next pass as
			// starting it, which lays it out as one that does.
			auto const *pStyle = m_Request.m_pStyle.f_Get();
			bool bTrailingScope
				= pStyle
				&& pStyle->m_bTrailingScopeAtStatementLevel
				&& bStatement
				&& _bIndent
				&& !_bClause
				&& !bStartsLine
				&& iLineFirst == _iFirst
				&& _iFirst <= Node.m_iFirstToken
				&& Scope.m_iLastToken == _iLast
				&& (Scope.m_Bracket == ECodeBracket::mc_Paren || Scope.m_Bracket == ECodeBracket::mc_Brace)
				&& m_Tokens.f_IsText(Tokens[Node.m_iLastToken], ";")
				&& fp_PreviousCode(Node.m_iLastToken) == aint(_iLast)
				&& !fg_ClosesParameterList(m_Tokens, m_Structure, Scope.m_iLastToken)
			;
			if (bTrailingScope)
				nMarkerIndent = _iIndent;

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
			for (auto iMarker = Scope.m_iLastToken; iNext >= 0 && umint(iNext) <= _iLast && m_Tokens.f_IsText(Tokens[umint(iNext)], "...");)
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
			auto const &LineFirst = Tokens[iLineFirst];
			bool bMember = m_Tokens.f_IsText(LineFirst, ".") || m_Tokens.f_IsText(LineFirst, "->");
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
		umint iAssign = TCLimitsInt<umint>::mc_Max;
		for (umint i = _iOperator; i > _iFirst; --i)
		{
			if (m_TokenDepth[i] == m_TokenDepth[_iFirst] && fp_IsAssignment(i))
			{
				iAssign = i;

				break;
			}
		}

		if (iAssign == TCLimitsInt<umint>::mc_Max)
			return false;

		auto iHeadEnd = fp_PreviousCode(iAssign);
		auto iOperandEnd = fp_PreviousCode(_iOperator);
		bool bMoves
			= iHeadEnd >= 0
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

		// The children are in source order; a node holding thousands, as a table's rows, is searched, not walked, for each
		// range laid out in it.
		auto const &Children = Nodes[_iNode].m_Children;
		umint iFirstChild = 0;
		for (umint iEnd = Children.f_GetLen(); iFirstChild < iEnd;)
		{
			auto iMiddle = iFirstChild + (iEnd - iFirstChild) / 2;
			if (Nodes[Children[iMiddle]].m_iFirstToken < _iFirst)
				iFirstChild = iMiddle + 1;
			else
				iEnd = iMiddle;
		}

		for (auto iChildIndex = iFirstChild; iChildIndex < Children.f_GetLen() && Nodes[Children[iChildIndex]].m_iFirstToken <= _iLast; ++iChildIndex)
		{
			auto iChild = Children[iChildIndex];
			auto const &Child = Nodes[iChild];
			if (Child.m_Kind != ECodeNodeKind::mc_Block || Child.m_iLastToken > _iLast)
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
			{
				// A lambda body splits the expression it ends, so every operator binding loosest in the head
				// starts a line of its own at the body's level: 'pState->m_Launch(...)' over
				// '+ g_Dispatch(Actor) / [=]'. The operator that takes the lambda stays on its line.
				TCVector<umint> Operators;
				fp_FindLooseOperators(_iFirst, umint(iHeadLast), Operators);

				bool bOperandTakesBody = false;
				if (!Operators.f_IsEmpty() && !fp_IsLambdaIntroducer(Operators.f_GetLast()))
				{
					for (auto i = Operators.f_GetLast() + 1; i <= umint(iHeadLast) && !bOperandTakesBody; ++i)
						bOperandTakesBody = fp_IsLambdaIntroducer(i);
				}

				umint iOperandFirst = _iFirst;
				if (bOperandTakesBody)
				{
					for (auto iOperator : Operators)
					{
						auto iOperandLast = fp_PreviousCode(iOperator);
						if (iOperandLast >= 0 && umint(iOperandLast) >= iOperandFirst)
						{
							if (fp_FitsInline(iOperandFirst, umint(iOperandLast), _iIndent))
								fp_MarkInline(iOperandFirst, umint(iOperandLast));
							else
								fp_LayoutRange(_iNode, iOperandFirst, umint(iOperandLast), _iIndent, _bClause && iOperandFirst == _iFirst, false);
						}

						fp_BreakBefore(iOperator, _iIndent);
						iOperandFirst = iOperator;
					}
				}

				auto iPreviousBody = m_iRangeBody;
				m_iRangeBody = iBrace;
				fp_LayoutRange(_iNode, iOperandFirst, umint(iHeadLast), _iIndent, _bClause && iOperandFirst == _iFirst, _bIndentContinuations && !bOperandTakesBody);
				m_iRangeBody = iPreviousBody;
			}

			// A body already on a line of its own still moves with its element, so that
			// its depth follows the element's new indentation.
			if (!fp_IsFirstOnLine(iBrace) || fp_GetStatementIndent(iBrace) != _iIndent)
				fp_PlaceBlock(iChild, _iIndent, nReference);

			fp_LayoutNode(iChild, _iIndent);

			// A binary operator behind the body goes on with the expression from a line of its own, at
			// the body's level: '}' over '+ pState->m_Launch(...)'.
			auto iTail = fp_NextCode(Child.m_iLastToken);

			// A lambda called at once goes on behind its call's arguments: '}()' over '&& f_Condition(...)'.
			if (iTail >= 0 && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iTail)], "("))
			{
				auto iCall = m_Structure.f_FindNodeOpeningAt(umint(iTail));
				if (iCall < Nodes.f_GetLen() && Nodes[iCall].m_iParent == _iNode)
					iTail = fp_NextCode(Nodes[iCall].m_iLastToken);
			}

			bool bTailOperator = false;
			if (iTail >= 0 && umint(iTail) < _iLast && !m_Structure.f_IsAngleBracket(umint(iTail)))
			{
				constexpr ch8 const *c_pOperators[] =
					{
						"+"
						, "-"
						, "*"
						, "/"
						, "%"
						, "<<"
						, ">>"
						, "<"
						, ">"
						, "<="
						, ">="
						, "=="
						, "!="
						, "&"
						, "^"
						, "|"
						, "&&"
						, "||"
						, "?"
						, ":"
					}
				;

				for (auto pOperator : c_pOperators)
					bTailOperator |= m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iTail)], pOperator);
			}

			if (bTailOperator)
			{
				fp_BreakBefore(umint(iTail), _iIndent);
				fp_LayoutRange(_iNode, umint(iTail), _iLast, _iIndent, false, false);
			}
			else if (iTail >= 0 && umint(iTail) < _iLast && (m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iTail)], ".") || m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iTail)], "->")))
			{
				// A member called on the result goes on behind the body's line, its own brackets laid out from there:
				// '}().f_Call' over '('.
				fp_LayoutRange(_iNode, umint(iTail), _iLast, _iIndent, false, false);
			}

			return true;
		}

		// A list that ends the range and is written open, or holds what makes the range too long for its line, opens below
		// what stands in front of it, which stays on its line where it fits: 'CFoo Value =', '_Builder << CFoo' or
		// ', "Key"_o=' over '{'. The list stands one level in from a statement.
		if (auto iChild = m_Structure.f_FindNodeClosingAt(_iLast); iChild < Nodes.f_GetLen() && Nodes[iChild].m_iParent == _iNode)
		{
			auto const &Child = Nodes[iChild];
			auto iHeadLast = Child.m_iFirstToken > _iFirst && Child.m_iFirstToken < m_OpenListEnds.f_GetLen() ? fp_PreviousCode(Child.m_iFirstToken) : aint(-1);
			bool bHead = iHeadLast >= 0 && umint(iHeadLast) >= _iFirst;

			// One written on a line opens where it is what does not fit behind its head.
			bool bOpens = bHead && Child.m_bList && Child.m_Kind == ECodeNodeKind::mc_Group && !m_KeepsListLines[iChild];

			// A head a comment or a directive cuts is laid out by its stretches whichever way the list is written, each a line
			// of its own: 'f() // A' over '<< B << CFoo' over '{'.
			bool bCutHead = false;
			for (auto i = _iFirst; bHead && i < umint(iHeadLast); ++i)
				bCutHead = bCutHead || m_Tokens.f_GetTokens()[i].m_Kind == ECodeTokenKind::mc_LineComment || m_Tokens.f_GetTokens()[i].m_Kind == ECodeTokenKind::mc_Preprocessor;

			// Whether a list is written open is the source's, which the first pass can change: a list it opens behind an
			// operand the layout breaks in front of, in a split conditional, '? x : d + CType{x}', joins the operand onto its
			// head in the next pass, which the repeated analysis composes into the plan. Making the head's fit decide here too
			// would relay out code that settles today, such as statements macros run together.
			bool bWrittenOpen = bOpens && m_OpenListEnds[Child.m_iFirstToken] == Child.m_iLastToken && !bCutHead;
			bOpens
				= bOpens
				&&
				(
					bWrittenOpen
					||
					(
						fp_NextCode(Child.m_iFirstToken) != aint(Child.m_iLastToken)
						&& (_bMustSplit || !fp_FitsInline(_iFirst, _iLast, _iIndent))
						&& fp_IsRangeJoinable(_iNode, _iFirst, _iLast)
						&& fp_FitsInline(_iFirst, umint(iHeadLast), _iIndent)
					)
				)
			;

			if (bOpens)
			{
				auto nListIndent = _bIndentContinuations ? _iIndent + m_Request.m_Settings.m_nTabWidth : _iIndent;
				auto const *pStyle = m_Request.m_pStyle.f_Get();
				if (pStyle && pStyle->m_bTrailingScopeAtStatementLevel && _bIndentContinuations && !_bClause && Nodes[_iNode].m_Kind == ECodeNodeKind::mc_Statement)
				{
					auto iTerminator = fp_NextCode(_iLast);
					if (iTerminator >= 0 && umint(iTerminator) == Nodes[_iNode].m_iLastToken && m_Tokens.f_IsText(m_Tokens.f_GetTokens()[umint(iTerminator)], ";"))
						nListIndent = _iIndent;
				}

				if (fp_FitsInline(_iFirst, umint(iHeadLast), _iIndent))
					fp_MarkInline(_iFirst, umint(iHeadLast));
				else
					fp_LayoutRange(_iNode, _iFirst, umint(iHeadLast), _iIndent, _bClause, _bIndentContinuations);

				if (!fp_LayoutGroup(iChild, nListIndent, true))
					fp_BreakBefore(Child.m_iFirstToken, nListIndent);

				return true;
			}
		}

		// An element opening with a bracket that fits on a line of its own but not behind its separator takes the line
		// below the separator whole, as it stands once the separator is alone on its line: ',' over '((CFoo){1, 2})'.
		{
			auto const &AllTokens = m_Tokens.f_GetTokens();
			auto iBehind = m_Tokens.f_IsText(AllTokens[_iFirst], ",") ? fp_NextCode(_iFirst) : aint(-1);
			auto const *pBehind = iBehind >= 0 ? &AllTokens[umint(iBehind)] : nullptr;
			bool bOpens = pBehind && umint(iBehind) < _iLast && (m_Tokens.f_IsText(*pBehind, "(") || m_Tokens.f_IsText(*pBehind, "{"));
			bool bAlone = bOpens && !fp_FitsInline(_iFirst, _iLast, _iIndent) && fp_IsRangeJoinable(_iNode, umint(iBehind), _iLast) && fp_FitsInline(umint(iBehind), _iLast, _iIndent);
			if (bAlone)
			{
				fp_BreakBefore(umint(iBehind), _iIndent);
				fp_MarkInline(umint(iBehind), _iLast);

				return true;
			}
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
		umint iFirstString = 0;

		// A statement's value continuing a string gives at it where operators split the value, '<<' among them: a value
		// that is nothing but its strings keeps them on lines of their own behind its '='.
		bool bStringCuts = !_bIndentContinuations;
		if (!bStringCuts)
		{
			TCVector<umint> ValueOperators;
			fp_FindLooseOperators(_iFirst, _iLast, ValueOperators);
			bStringCuts = !ValueOperators.f_IsEmpty();
		}

		for (umint i = _iFirst; i <= _iLast; ++i)
		{
			// Only a directive standing at the range's own level cuts it. One inside a
			// construct within it belongs to that construct, and a closing marker behind a
			// directive stays where the marker's own scope puts it.
			// A line comment ends its line the same way, and what stands behind it starts
			// the next: 'class CFoo // Comment' above its base clause.
			// So does a string an element of a group continues on the line below it.
			auto Kind = Tokens[i].m_Kind;
			bool bEndsLine = (Kind == ECodeTokenKind::mc_Preprocessor && !m_OpaqueDirective[i]) || Kind == ECodeTokenKind::mc_LineComment || fp_IsCommentLine(i);
			auto iNext = fp_NextCode(i);
			bool bContinued = Kind == ECodeTokenKind::mc_StringLiteral || Kind == ECodeTokenKind::mc_Identifier;
			if (bStringCuts && bContinued && iNext >= 0 && fp_ContinuesLiteral(umint(iNext)) == aint(i))
			{
				if (!bContinuedString)
					iFirstString = i;

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

			// A string continued over lines spans them, so the range gives at its loosest operators as well, which a
			// segment between its strings could not tell: the ':' answering a '?' in the segment in front.
			// An operator leads its operand on the operand's line there, as anywhere.
			// In a statement only the operators in front of its first string and behind its last do, each starting a line:
			// 'Header' over '+ "a"' over '"b"_f' over '<< Value'; those between its strings stay on the lines the strings take.
			TCVector<umint> OperatorCuts;
			if (bContinuedString)
			{
				auto iLastString = Cuts.f_GetLast();
				for (auto iOperator : Operators)
				{
					bool bCut = iOperator <= _iFirst || (_bIndentContinuations && iOperator > iFirstString && iOperator < iLastString);
					for (auto iCut : Cuts)
						bCut |= iCut == iOperator;

					if (!bCut)
					{
						Cuts.f_Insert(iOperator);
						OperatorCuts.f_Insert(iOperator);
					}
				}

				Cuts.f_Sort();
			}

			bool bCutAtOperator = false;
			for (auto iOperator : Operators)
			{
				for (auto iCut : Cuts)
					bCutAtOperator |= iOperator == iCut && !(_bIndentContinuations && OperatorCuts.f_Contains(iCut) >= 0);
			}

			bool bCutAtMember = false;
			for (auto iCut : Cuts)
			{
				auto const &Cut = Tokens[iCut];
				bCutAtMember |= m_Tokens.f_IsText(Cut, ".") || m_Tokens.f_IsText(Cut, "->");
			}

			auto nContinuation = _bIndentContinuations ? _iIndent + nTab : _iIndent;

			// A class's head is no expression continued: the words naming it stand at its own
			// level, the directives between them included, and only its base clause one in.
			auto const &RangeKey = Tokens[fp_SkipTemplateHeader(_iFirst)];
			bool bClassHead = m_Tokens.f_IsText(RangeKey, "class") || m_Tokens.f_IsText(RangeKey, "struct") || m_Tokens.f_IsText(RangeKey, "union");

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
					bSplitSegment |= (bCutAtOperator || (bContinuedString && !_bIndentContinuations)) && iOperator > iStart && iOperator <= iEnd;
				}

				// A chain written broken at one member access is broken at each of them.
				bool bSplitMembers = false;
				for (umint iMember = iStart + 1; bCutAtMember && iMember <= iEnd; ++iMember)
				{
					auto const &Member = Tokens[iMember];
					bSplitMembers |= m_TokenDepth[iMember] == nLevel && (m_Tokens.f_IsText(Member, ".") || m_Tokens.f_IsText(Member, "->"));
				}

				auto nSegmentIndent = iCut ? nContinuation : _iIndent;
				auto const &Start = Tokens[iStart];
				if (iCut && bClassHead && !m_Tokens.f_IsText(Start, ":") && !m_Tokens.f_IsText(Start, ","))
					nSegmentIndent = _iIndent;

				// A declaration's head is no expression continued either: the words up to its name's parameter
				// list stand at the statement's level, the list one level in from them.
				// 'bool // Comment' over 'CFoo::f_Process' over '('.
				bool bDeclarationHead = iCut && _bIndentContinuations && m_iSplitFirstParen && iStart < m_iSplitFirstParen;
				if (bDeclarationHead)
				{
					auto iList = m_Structure.f_FindNodeOpeningAt(m_iSplitFirstParen);
					bDeclarationHead = iList < Nodes.f_GetLen() && fg_ClosesParameterList(m_Tokens, m_Structure, Nodes[iList].m_iLastToken);
				}

				for (auto i = iStart; bDeclarationHead && i < m_iSplitFirstParen; ++i)
					bDeclarationHead = m_TokenDepth[i] != nLevel || !m_Tokens.f_IsText(Tokens[i], "=");

				if (bDeclarationHead)
					nSegmentIndent = _iIndent;

				// A string continued as a value stands one level in: 'DPrefix " a"' over '"b"'.
				if (iCut && nContinuation == _iIndent && fp_ContinuesValue(iStart))
					nSegmentIndent = _iIndent + nTab;

				if (iCut)
					fp_BreakBefore(iStart, nSegmentIndent);

				if (bSplitMembers && !bSplitSegment && fp_LayoutMembers(_iNode, iStart, iEnd, nSegmentIndent, nContinuation))
					continue;

				// The first stretch of a value cut at its operators ends its first operand. One whose strings continue over lines
				// keeps its '=' and the first string where the source has them, on one line or the string below the '=':
				// 'CStr Message =' over '"a"' over '"b"_f' over '<< Value'.
				if (!iCut && _bIndentContinuations && bContinuedString)
				{
					auto iValue = Tokens[iEnd].m_Kind == ECodeTokenKind::mc_StringLiteral ? fp_FindStringRunStart(iEnd) : iEnd;
					auto iAssign = fp_PreviousCode(iValue);
					if (iAssign >= 0 && umint(iAssign) >= iStart && fp_IsAssignment(umint(iAssign)) && fp_SpansLines(umint(iAssign), iValue))
					{
						if (fp_FitsInline(iStart, umint(iAssign), _iIndent))
							fp_MarkInline(iStart, umint(iAssign));

						fp_BreakBefore(iValue, nContinuation);
						if (iValue < iEnd)
							fp_MarkInline(iValue, iEnd);

						continue;
					}
				}
				else if (!iCut && _bIndentContinuations && bCutAtOperator && !bSplitSegment && fp_BreakAtAssign(iStart, Cuts[0], _iIndent, nContinuation))
					continue;

				fp_LayoutRange(_iNode, iStart, iEnd, nSegmentIndent, _bClause && !iCut, (_bIndentContinuations && !iCut) || bDeclarationHead, bSplitSegment);

				bool bOperatorCut = false;
				for (auto iOperator : OperatorCuts)
					bOperatorCut |= iCut && iOperator == iStart;

				if (auto iOperand = fp_NextCode(iStart); bOperatorCut && iOperand >= 0 && umint(iOperand) <= iEnd)
					fp_MarkInline(iStart, umint(iOperand));
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

		// A class head too long for its line gives at its base clause first: the clause's colon and
		// each base's comma start a line one level in, and only a base still too long opens.
		// 'class CFoo' over ': public CBar' over ', public CBaz'.
		{
			auto const &Tokens = m_Tokens.f_GetTokens();
			auto const &Key = Tokens[fp_SkipTemplateHeader(_iFirst)];
			bool bClassHead = m_Tokens.f_IsText(Key, "class") || m_Tokens.f_IsText(Key, "struct") || m_Tokens.f_IsText(Key, "union");
			auto nLevel = m_TokenDepth[_iFirst];
			TCVector<umint> Bases;
			for (auto i = _iFirst; bClassHead && i <= _iLast; ++i)
			{
				if (m_TokenDepth[i] != nLevel || m_Structure.f_IsAngleBracket(i))
					continue;

				auto const &Token = Tokens[i];
				if (Bases.f_IsEmpty() ? m_Tokens.f_IsText(Token, ":") : m_Tokens.f_IsText(Token, ","))
					Bases.f_Insert(i);
			}

			bool bMeasured = true;
			for (umint iBase = 0; iBase < Bases.f_GetLen() && bMeasured; ++iBase)
			{
				auto iBefore = fp_PreviousCode(Bases[iBase]);
				umint nWidth = 0;
				bMeasured = iBefore >= 0 && umint(iBefore) >= _iFirst && fp_MeasureJoinedWidth(_iFirst, umint(iBefore), nWidth);
			}

			// A head too long by itself opens its own argument list first, as the scopes lay it out.
			if (!Bases.f_IsEmpty() && bMeasured && !fp_FitsInline(_iFirst, umint(fp_PreviousCode(Bases[0])), _iIndent))
				bMeasured = false;

			if (!Bases.f_IsEmpty() && bMeasured)
			{
				auto nTab = m_Request.m_Settings.m_nTabWidth;
				for (umint iSegment = 0; iSegment <= Bases.f_GetLen(); ++iSegment)
				{
					auto iStart = iSegment ? Bases[iSegment - 1] : _iFirst;
					auto iEnd = iSegment < Bases.f_GetLen() ? umint(fp_PreviousCode(Bases[iSegment])) : _iLast;
					auto nSegmentIndent = iSegment ? _iIndent + nTab : _iIndent;
					if (iSegment)
						fp_BreakBefore(iStart, nSegmentIndent);

					if (fp_FitsInline(iStart, iEnd, nSegmentIndent))
						fp_MarkInline(iStart, iEnd);
					else
						fp_LayoutScopes(_iNode, iStart, iEnd, nSegmentIndent, false, false);
				}

				return true;
			}
		}

		// A statement too long for its line gives at the commas it holds itself first, which part its declarators: each
		// comma starts a line one level in, and only a declarator still too long is laid out further. 'int a = f(x)' over
		// ', b = f(y)'. A '<' read as a comparison could be a list's that holds the comma, and leaves the statement be, as
		// does a gap the standard does not settle, which leaves the statement no single-line form.
		umint nCommaJoined = 0;
		if (Nodes[_iNode].m_Kind == ECodeNodeKind::mc_Statement && _bIndentContinuations && fp_MeasureJoinedWidth(_iFirst, _iLast, nCommaJoined) && !fp_FitsInline(_iFirst, _iLast, _iIndent))
		{
			auto const &Tokens = m_Tokens.f_GetTokens();
			auto nLevel = m_TokenDepth[_iFirst];
			TCVector<umint> Commas;
			bool bUnsettled = false;
			for (auto i = _iFirst; i <= _iLast && !bUnsettled; ++i)
			{
				if (m_TokenDepth[i] != nLevel || m_Structure.f_IsAngleBracket(i))
					continue;

				auto const &Token = Tokens[i];
				bUnsettled = m_Tokens.f_IsText(Token, "<") || m_Tokens.f_IsText(Token, ">") || m_Tokens.f_IsText(Token, "operator");
				if (m_Tokens.f_IsText(Token, ",") && i > _iFirst)
					Commas.f_Insert(i);
			}

			if (!bUnsettled && !Commas.f_IsEmpty())
			{
				auto nTab = m_Request.m_Settings.m_nTabWidth;
				for (umint iSegment = 0; iSegment <= Commas.f_GetLen(); ++iSegment)
				{
					auto iStart = iSegment ? Commas[iSegment - 1] : _iFirst;
					auto iEnd = iSegment < Commas.f_GetLen() ? umint(fp_PreviousCode(Commas[iSegment])) : _iLast;
					auto nSegmentIndent = iSegment ? _iIndent + nTab : _iIndent;
					if (iSegment)
						fp_BreakBefore(iStart, nSegmentIndent);

					if (fp_FitsInline(iStart, iEnd, nSegmentIndent))
						fp_MarkInline(iStart, iEnd);
					else
						fp_LayoutRange(_iNode, iStart, iEnd, nSegmentIndent, false, iSegment == 0, false);
				}

				return true;
			}
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

		// A value that fits on a line of its own behind its '=' takes that line whole, rather than
		// the statement giving at the value's operators: 'pWatch->m_Files[Name]' over '= (Attribs & Mask) == Directory'.
		for (auto i = Operators[0]; i > _iFirst && _bIndentContinuations; --i)
		{
			if (m_TokenDepth[i] != m_TokenDepth[_iFirst] || !fp_IsAssignment(i))
				continue;

			// One that takes a lambda keeps the '=' on the head: 'auto Cleanup = g_OnScopeExit / [&]'. One a
			// comment or a directive cuts goes on behind it with an operand on each line, as its stretches do.
			bool bKeepsSplit = false;
			for (auto iValue = i + 1; iValue <= _iLast && !bKeepsSplit; ++iValue)
				bKeepsSplit = fp_IsLambdaIntroducer(iValue);

			bool bCut = false;
			for (auto iBehind = _iLast + 1; iBehind < Tokens.f_GetLen() && !bKeepsSplit; ++iBehind)
			{
				auto Kind = Tokens[iBehind].m_Kind;
				if (Kind == ECodeTokenKind::mc_Preprocessor || Kind == ECodeTokenKind::mc_LineComment || Kind == ECodeTokenKind::mc_BlockComment)
					bCut = true;
				else if (Kind != ECodeTokenKind::mc_Whitespace && Kind != ECodeTokenKind::mc_Newline)
				{
					constexpr ch8 const *c_pEnds[] =
						{
							";"
							, ","
							, ")"
							, "]"
							, "}"
						}
					;

					bool bEnds = false;
					for (auto pEnd : c_pEnds)
						bEnds |= m_Tokens.f_IsText(Tokens[iBehind], pEnd);

					bKeepsSplit = bCut && !bEnds;

					break;
				}
			}

			auto iHeadEnd = fp_PreviousCode(i);
			bool bValueLine
				= !bKeepsSplit
				&& !fp_IsKeyAssign(i)
				&& iHeadEnd >= 0
				&& umint(iHeadEnd) >= _iFirst
				&& fp_GetCanonicalSpacing(umint(iHeadEnd), i) == ECodeSpacing::mc_Space
				&& fp_FitsInline(_iFirst, umint(iHeadEnd), _iIndent)
				&& fp_FitsInline(i, _iLast, _iIndent + nTab)
			;
			if (bValueLine)
			{
				fp_MarkInline(_iFirst, umint(iHeadEnd));
				fp_BreakBefore(i, _iIndent + nTab);
				fp_MarkInline(i, _iLast);

				return true;
			}

			break;
		}

		// A single stream operator writing a named call into a plain target stays on the line with
		// the call where the line fits up to its parenthesis, and the call opens instead:
		// 'Params << fg_CreateVector<CStr>' over '('.
		auto const *pOperator = Operators.f_GetLen() == 1 ? &Tokens[Operators[0]] : nullptr;
		bool bStreamsCall = pOperator && m_Tokens.f_IsText(Tokens[_iLast], ")") && (m_Tokens.f_IsText(*pOperator, "<<") || m_Tokens.f_IsText(*pOperator, ">>"));
		for (auto i = _iFirst; bStreamsCall && i < Operators[0]; i = umint(fp_NextCode(i)))
		{
			auto const &Token = Tokens[i];
			bStreamsCall = Token.m_Kind == ECodeTokenKind::mc_Identifier || m_Tokens.f_IsText(Token, "::") || m_Tokens.f_IsText(Token, ".") || m_Tokens.f_IsText(Token, "->");
		}

		// The right operand is the call's name and template arguments, and the call.
		if (bStreamsCall)
		{
			auto iCall = m_Structure.f_FindNodeClosingAt(_iLast);
			for (auto i = Operators[0] + 1; bStreamsCall && iCall < Nodes.f_GetLen() && i < Nodes[iCall].m_iFirstToken; ++i)
			{
				auto const &Token = Tokens[i];
				bStreamsCall
					= m_TokenDepth[i] != m_TokenDepth[Operators[0]] || Token.m_Kind != ECodeTokenKind::mc_Punctuator || m_Tokens.f_IsText(Token, "::") || m_Structure.f_IsAngleBracket(i)
				;
			}
		}

		if (bStreamsCall)
		{
			auto iCall = m_Structure.f_FindNodeClosingAt(_iLast);
			auto iName = iCall < Nodes.f_GetLen() ? fp_PreviousCode(Nodes[iCall].m_iFirstToken) : aint(-1);
			auto iInner = iCall < Nodes.f_GetLen() ? fp_NextCode(Nodes[iCall].m_iFirstToken) : aint(-1);
			auto const *pName = iName >= 0 ? &Tokens[umint(iName)] : nullptr;
			bool bNamedCall
				= pName
				&& umint(iName) > Operators[0]
				&& umint(iInner) != _iLast
				&& !fp_IsCastGroup(iCall)
				&& (pName->m_Kind == ECodeTokenKind::mc_Identifier || (m_Structure.f_IsAngleBracket(umint(iName)) && m_Tokens.f_IsText(*pName, ">")))
				&& fp_FitsInline(_iFirst, umint(iName), _iIndent)
			;
			if (bNamedCall)
				return fp_LayoutScopes(_iNode, _iFirst, _iLast, _iIndent, _bClause, _bIndentContinuations, _bMustSplit);
		}

		// A statement's continuation is indented past its own start; an element of a group
		// already sits at the group's content indentation and its continuation aligns there.
		// So does what follows a parenthesis the range opens with, once that parenthesis is
		// opened: its closing marker stands at the range's own level, and the operator
		// behind it under that marker.
		bool bLeadsOpened = false;
		if (_bIndentContinuations && m_Tokens.f_IsText(Tokens[_iFirst], "("))
		{
			auto iChild = m_Structure.f_FindNodeOpeningAt(_iFirst);
			if (iChild < Nodes.f_GetLen() && Nodes[iChild].m_iParent == _iNode)
			{
				auto const &Child = Nodes[iChild];
				if (Child.m_Kind == ECodeNodeKind::mc_Group && Child.m_iLastToken < Operators[0])
					bLeadsOpened = Child.m_bHasBlock || !fp_FitsInline(_iFirst, Child.m_iLastToken, _iIndent);
			}
		}

		// The operators behind a DSL key's string continued over lines stand at the key's level
		// with the string: '"Key"_o= "a"' over '"b"' over '+ Warning'.
		bool bKeyString = false;
		if (auto iOperand = fp_PreviousCode(Operators[0]); iOperand >= 0 && Tokens[umint(iOperand)].m_Kind == ECodeTokenKind::mc_StringLiteral)
		{
			auto iLead = fp_FindContinuedStringLead(umint(iOperand));
			bKeyString = iLead >= 0 && m_Tokens.f_IsText(Tokens[umint(iLead)], "=") && fp_IsKeyAssign(umint(iLead));
		}

		auto nContinuation = _bIndentContinuations && !bLeadsOpened && !bKeyString ? _iIndent + nTab : _iIndent;
		m_bOperatorSplit |= _bIndentContinuations;
		auto iFirstOperator = Operators[0];

		// A lambda is written behind the operator that takes it, so that operator stays on
		// the line its left hand side ends, and so does the capture list where it fits
		// there: 'g_ActorFunctor / [this]' with the parameter list and the return type
		// below it. Only a capture list too long for that line goes below the operator,
		// 'g_Dispatch /' with the list opened under it. Only the first operator qualifies,
		// and only while the line in front of it is whole.
		// A capture list that fits on a line of its own takes the operator with it instead, '/ [this, ...]' leading
		// the line where the body opens: at the range's level where the body follows the range, as it does in an element.
		bool bOperatorTrails = fp_IsLambdaIntroducer(iFirstOperator) && fp_FitsInline(_iFirst, iFirstOperator, _iIndent);
		bool bOperatorLeads = false;
		auto iAfterRange = fp_NextCode(_iLast);
		auto nLambdaIndent = iAfterRange >= 0 && umint(iAfterRange) == m_iRangeBody ? _iIndent : nContinuation;
		umint iHeadEnd = iFirstOperator;
		if (bOperatorTrails)
		{
			auto iCapture = fp_NextCode(iFirstOperator);
			auto iChild = iCapture >= 0 ? m_Structure.f_FindNodeOpeningAt(umint(iCapture)) : Nodes.f_GetLen();
			auto const *pChild = iChild < Nodes.f_GetLen() ? &Nodes[iChild] : nullptr;
			if (pChild && pChild->m_iParent == _iNode && pChild->m_Kind == ECodeNodeKind::mc_Group)
			{
				auto const &Child = *pChild;
				if (Child.m_iLastToken <= _iLast && fp_FitsInline(_iFirst, Child.m_iLastToken, _iIndent))
					iHeadEnd = Child.m_iLastToken;
				else if (Child.m_iLastToken <= _iLast && fp_FitsInline(iFirstOperator, Child.m_iLastToken, nLambdaIndent))
					bOperatorLeads = true;
			}

			if (bOperatorLeads)
				bOperatorTrails = false;
			else
				fp_BreakAfter(iHeadEnd, nContinuation);
		}

		for (umint i = bOperatorTrails ? 1 : 0; i < Operators.f_GetLen(); ++i)
			fp_BreakBefore(Operators[i], i == 0 && bOperatorLeads ? nLambdaIndent : nContinuation);

		// A statement's value broken at its operators starts a line of its own with the
		// '=', so that every operand stands under the one in front of it:
		//   Value
		//       = a
		//       + b
		// That holds where the first operand is a plain line. One that takes a lambda's body
		// or has to be opened keeps the '=' on the head, 'auto Value = fg_Function' with its
		// parenthesis below, and so does the key of a DSL spelling, which hugs its '='. A
		// parameter's default is given the same way, its '=' at the element's level.
		bool bAssignMoved = !bOperatorTrails && !bOperatorLeads && !bLeadsOpened && fp_BreakAtAssign(_iFirst, iFirstOperator, _iIndent, nContinuation);

		// The operator leads its operand on the operand's line, also where the operand is too long for it:
		// '&& "a long string"' rather than '&&' over the string.
		auto fLeadOperand = [&](umint _iSegment, umint _iStart, umint _iEnd)
			{
				if (!_iSegment || _iStart != Operators[_iSegment - 1])
					return;

				auto iBehind = fp_NextCode(_iStart);
				if (iBehind < 0 || umint(iBehind) > _iEnd)
					return;

				auto BehindGap = EGap(m_GapState[umint(iBehind)]);
				bool bUndecided = BehindGap == EGap::mc_Keep || BehindGap == EGap::mc_Indent;
				if (bUndecided && fp_IsRangeJoinable(_iNode, _iStart, umint(iBehind)))
					fp_MarkInline(_iStart, umint(iBehind));
			}
		;

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

			auto nSegmentIndent = !iSegment ? _iIndent : iSegment == 1 && bOperatorLeads ? nLambdaIndent : nContinuation;
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

			// The operator that takes a lambda gives first where its capture list does not fit behind it
			// and what stands in front of it fits on the operand's line, the lambda taking a line of its
			// own at that level: '> pState->m_Promise % fg_Format(...)' over '/ [this, ...]'.
			if (!Inner.f_IsEmpty() && fp_IsLambdaIntroducer(Inner.f_GetLast()))
			{
				auto iOperator = Inner.f_GetLast();
				auto iHeadLast = fp_PreviousCode(iOperator);
				bool bBodyInHead = false;
				for (auto i = iStart; iHeadLast >= 0 && i <= umint(iHeadLast) && !bBodyInHead; ++i)
					bBodyInHead = m_Tokens.f_IsText(Tokens[i], "{") || fp_IsLambdaIntroducer(i);

				auto iCapture = m_Structure.f_FindNodeOpeningAt(umint(fp_NextCode(iOperator)));
				bool bCaptureBehind = iCapture < Nodes.f_GetLen() && fp_FitsInline(iStart, Nodes[iCapture].m_iLastToken, nSegmentIndent);
				bool bCaptureLeads = iCapture < Nodes.f_GetLen() && fp_FitsInline(iOperator, Nodes[iCapture].m_iLastToken, nSegmentIndent);
				bool bLead = iHeadLast >= 0 && umint(iHeadLast) >= iStart && !bBodyInHead && !bCaptureBehind && bCaptureLeads && fp_FitsInline(iStart, umint(iHeadLast), nSegmentIndent);
				if (bLead)
				{
					fp_MarkInline(iStart, umint(iHeadLast));
					fp_BreakBefore(iOperator, nSegmentIndent);
					fp_LayoutScopes(_iNode, iOperator, iEnd, nSegmentIndent, false, false);

					continue;
				}
			}

			// An operand that holds a lambda is laid out by its scopes, which is where its body
			// and the operator that takes it are placed: '> TestActor / [&]' stays whole, and so does a
			// capture the range ends a capture list behind. A braced initializer is a value like any
			// other: 'f_Report(TCVector<CReading>{Reading}) % "Error"'.
			for (auto i = iStart; i <= iEnd && !Inner.f_IsEmpty(); ++i)
			{
				auto iBrace = m_Tokens.f_IsText(Tokens[i], "{") ? m_Structure.f_FindNodeOpeningAt(i) : Nodes.f_GetLen();
				bool bBody = iBrace < Nodes.f_GetLen() && Nodes[iBrace].m_Kind != ECodeNodeKind::mc_Group;
				if (bBody || fp_IsLambdaIntroducer(i) || (m_Tokens.f_IsText(Tokens[i], "]") && fp_ClosesLambdaIntroducer(i)))
					Inner.f_Clear();
			}

			if (!Inner.f_IsEmpty())
			{
				umint nJoined = 0;
				if (fp_MeasureJoinedWidth(iStart, iEnd, nJoined))
				{
					// A part led by an operator gives at the operators binding tighter than its own in turn, deeper
					// conditionals as well: ': c' over '? d' over ': e ? f : g'.
					auto fSplit = [&](auto &_fSplit, umint _iStart, umint _iEnd, TCVector<umint> const &_Inner, umint _nIndent, umint _nFirstIndent) -> void
						{
							// A conditional that is a branch of another binds no tighter than it, and is set
							// off one level deeper instead: ': b' with its '? c' and ': d' indented under it.
							bool bNestedConditional = m_Tokens.f_IsText(Tokens[_Inner[0]], "?");
							auto nInnerIndent = bNestedConditional ? _nIndent + nTab : _nIndent;
							for (auto iInnerOperator : _Inner)
								fp_BreakBefore(iInnerOperator, nInnerIndent);

							for (umint iPart = 0; iPart <= _Inner.f_GetLen(); ++iPart)
							{
								auto iPartStart = iPart ? _Inner[iPart - 1] : _iStart;
								auto iPartEnd = iPart < _Inner.f_GetLen() ? umint(fp_PreviousCode(_Inner[iPart])) : _iEnd;
								if (iPartEnd < iPartStart)
									continue;

								auto nPartIndent = iPart ? nInnerIndent : _nFirstIndent;
								if (fp_FitsInline(iPartStart, iPartEnd, nPartIndent))
								{
									fp_MarkInline(iPartStart, iPartEnd);

									continue;
								}

								TCVector<umint> Deeper;
								auto iDeeperFirst = umint(fp_NextCode(iPartStart));
								if (iPart && iDeeperFirst <= iPartEnd)
									fp_FindLooseOperators(iDeeperFirst, iPartEnd, Deeper);

								for (auto i = iPartStart; i <= iPartEnd && !Deeper.f_IsEmpty(); ++i)
								{
									if (m_Tokens.f_IsText(Tokens[i], "{") || fp_IsLambdaIntroducer(i))
										Deeper.f_Clear();
								}

								if (!Deeper.f_IsEmpty())
									_fSplit(_fSplit, iPartStart, iPartEnd, Deeper, nPartIndent, nPartIndent);
								else
									fp_LayoutScopes(_iNode, iPartStart, iPartEnd, nPartIndent, _bClause && !iSegment && !iPart, false);
							}
						}
					;
					fSplit(fSplit, iStart, iEnd, Inner, nContinuation, nSegmentIndent);

					fLeadOperand(iSegment, iStart, iEnd);

					continue;
				}
			}

			// An operand opening with a bracket that fits on a line of its own but not behind its operator takes the line below
			// the operator whole, as it stands once the opened bracket leaves the operator alone: '&&' over '(a || b)'.
			if (iSegment && iStart == Operators[iSegment - 1])
			{
				auto iOperand = fp_NextCode(iStart);
				auto const *pOperand = iOperand >= 0 ? &Tokens[umint(iOperand)] : nullptr;
				bool bOpens = pOperand && umint(iOperand) < iEnd && (m_Tokens.f_IsText(*pOperand, "(") || m_Tokens.f_IsText(*pOperand, "{"));
				if (bOpens && fp_IsRangeJoinable(_iNode, umint(iOperand), iEnd) && fp_FitsInline(umint(iOperand), iEnd, nSegmentIndent))
				{
					fp_BreakBefore(umint(iOperand), nSegmentIndent);
					fp_MarkInline(umint(iOperand), iEnd);

					continue;
				}
			}

			fp_LayoutScopes(_iNode, iStart, iEnd, nSegmentIndent, _bClause && !iSegment, _bIndentContinuations && !iSegment);
			fLeadOperand(iSegment, iStart, iEnd);
		}

		return true;
	}
}
