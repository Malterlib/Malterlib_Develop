// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Develop_CodeFormatting.h"
#include "Malterlib_Develop_CodeFormattingLexer.h"
#include "Malterlib_Develop_CodeFormattingStructure.h"

#include <Mib/String/Appender>

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
	using namespace NStorage;
}

namespace NMib::NDevelop::NPrivate
{
	extern CStr const gc_FormatOffDirective;
	extern CStr const gc_FormatOnDirective;

	// Wider than any line: what a block measures as, so that no range holding one fits.
	inline constexpr umint gc_nBlockWidth = umint(1) << 24;

	bool fg_IsCodeToken(CCodeToken const &_Token);
	bool fg_IsSpaceOrTab(ch8 _Char);
	bool fg_IsValidUtf8(CStr const &_Text);
	bool fg_IsCodePointBoundary(CStr const &_Text, umint _iOffset);
	bool fg_IsObjectiveCVisibility(CCodeTokenStream const &_Tokens, CCodeToken const &_Token);
	CStr fg_TrimCommentTrailingSpace(CStr const &_Text);
	bool fg_IsSameText(CStr const &_Text, ch8 const *_pOther, umint _nOther);
	umint fg_CommentBlanks(CStr const &_Text);
	bool fg_SharedCommentBlanks(TCVector<CStr> const &_Texts, umint &o_nShared);
	bool fg_ConvertsCommentBlanks(TCVector<CStr> const &_Texts, umint _nShared);
	CStr fg_TrailingLineComment(CStr const &_Text);
	bool fg_IsFallthroughComment(CStr const &_Text);
	umint fg_MapOffsetToConverted(TCVector<CCodeFormattingEdit> const &_Conversions, umint _iOffset, bool _bEnd);
	CStr fg_GetReorderInvariant(CStr const &_Source);

	// Maps offsets of the converted source back to the original, resuming where the previous offset left off, so ascending
	// offsets take one pass over the edits; a smaller one starts over.
	struct COffsetToOriginal
	{
		TCVector<CCodeFormattingEdit> const &m_Edits;
		umint m_iEdit = 0;
		aint m_nDelta = 0;
		umint m_iPrevious = 0;
	};

	umint fg_MapOffsetToOriginal(COffsetToOriginal &_Mapper, umint _iOffset, bool _bEnd = false);
	bool fg_HoldsRawStringLines(ch8 const *_pText, umint _nLength);
	bool fg_HoldsSplicedLiteral(ch8 const *_pText, umint _nLength);
	bool fg_JoinsAcrossSplice(ch8 const *_pText, umint _nLength);
	bool fg_HasEquivalentDirectives(CStr const &_First, CStr const &_Second, ECodeLanguage _Language);

	bool fg_ComposeEdits
		(
			TCVector<CCodeFormattingEdit> const &_Conversions
			, CStr const &_Converted
			, TCVector<CCodeFormattingEdit> const &_Edits
			, TCVector<CCodeFormattingEdit> &o_Edits
			, CStr &o_Explanation
		)
	;

	// The spans of a conditional whose branches write alternative heads for one body: '#if A' over 'if (a) {' over '#else'
	// over 'if (b) {' over '#endif'. Every span covers whole lines.
	struct CAlternativeHeads
	{
		NContainer::TCVector<CCodeFormattingRange> m_Directives;
		NContainer::TCVector<CCodeFormattingRange> m_Branches;
	};

	// Lays a file out in two phases. Every statement is first taken as if it were written on
	// one line, and then split, outermost break first, only where that line is too long. The
	// decisions are kept per gap between tokens and written out once, so no decision can
	// depend on an edit already made, or on where the source happened to break its lines.
	struct CFormattingAnalyzer
	{
		explicit CFormattingAnalyzer(CCodeFormattingRequest const &_Request, bool _bAllowConversions = true, bool _bAllowQualifiers = true);

		CCodeFormattingResult f_Analyze(bool _bVerify);
		bool f_FindAlternativeHeads(NContainer::TCVector<CAlternativeHeads> &o_Groups) const;
		NContainer::TCVector<CCodeFormattingRange> f_GetDirectiveSpans() const;
		void f_AddPassDiagnostics
			(
				NContainer::TCVector<CCodeFormattingEdit> const &_Plan
				, NContainer::TCVector<CCodeFormattingDiagnostic> const &_Pass
				, bool _bFixable
				, NContainer::TCVector<CCodeFormattingDiagnostic> &o_Diagnostics
			)
			const
		;
		void f_ReplaceResidualDiagnostics
			(
				NContainer::TCVector<CCodeFormattingEdit> const &_Plan
				, NContainer::TCVector<CCodeFormattingDiagnostic> const &_Residual
				, NContainer::TCVector<CCodeFormattingDiagnostic> &o_Diagnostics
			)
			const
		;

	private:
		// What a gap in front of a token becomes: what the source has, the inline spelling,
		// or a line break at a given indentation.
		enum class EGap : uint8
		{
			mc_Keep
			, mc_Inline
			, mc_Break   // A split construct starts a line here.
			, mc_OwnLine // A block's brace or a statement takes a line of its own here.
			, mc_Indent  // The line keeps its break and takes the indentation its body moved to.
		};

		struct CChildSpan
		{
			umint const *begin() const;
			umint const *end() const;

			umint const *m_pBegin = nullptr;
			umint const *m_pEnd = nullptr;
		};

		struct CTrailedCode
		{
			umint m_iFirst = 0;
			umint m_nColumns = 0;
			bool m_bStartsStatement = false;
			bool m_bMeasured = false;
			bool m_bBehindBrace = false; // The statement measured stands behind a block's brace, its clause or its label on the line, and takes its own.
			umint m_iBehindOf = 0;       // With m_bBehindBrace, the token whose line the statement takes its own one level in from.
		};

		enum class EListLayout : uint8
		{
			mc_Columns
			, mc_Table
			, mc_Rows
		};

		struct CListLayout
		{
			umint m_iList = 0;      // The braced list's node.
			umint m_iDirective = 0; // The directive's comment token.
			EListLayout m_Layout = EListLayout::mc_Columns;
			umint m_nColumns = 0;   // Elements per row, zero for as many as fit.
		};

		struct CJoinCandidate
		{
			umint m_iOffset = 0; // Where the joined construct starts in the source.
			NContainer::TCVector<umint> m_Edits;
			bool m_bDropped = false;
		};

		// Per line of a build system source: where its first and last significant tokens are,
		// the tab levels its brackets settle, and the column a continued value aligns to.
		struct CBuildSystemLine
		{
			aint m_iFirst = -1;               // Token index; -1 for a blank line.
			aint m_iLast = -1;
			umint m_Level = 0;
			umint m_nContinuationColumns = 0; // Set on a line a backslash continues.
			bool m_bContinued = false;
		};

		void fp_PrepareLineProtection();
		bool fp_CheckUnbracedAssembly(CStr &o_Explanation) const;
		aint fp_FindAssemblyClose(umint _iOpen) const;
		bool fp_CollectDisabledRegions(CStr &o_Explanation);
		bool fp_ResolveRanges(CStr &o_Explanation);
		void fp_AddEdit(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Replacement, CStr const &_Explanation);
		void fp_AddDiagnostic(CStr const &_Rule, umint _iOffset, umint _nLength, CStr const &_Explanation, bool _bHasAutomaticFix);

		bool fp_IsDisabled(umint _iOffset, umint _nLength) const;
		bool fp_IsFormatOff(umint _iOffset, umint _nLength) const;
		bool fp_CollectListLayouts(CStr &o_Explanation);
		void fp_RenderListLayouts();
		void fp_RenderAlignments();
		bool fp_RenderElement(umint _iFirst, umint _iLast, CStr &o_Text, CStr const *_pContinuation = nullptr) const;
		void fp_AddOwnedEdit(umint _iOffset, umint _nLength, CStr const &_Replacement, CStr const &_Explanation);
		bool fp_IsSelected(umint _iOffset, umint _nLength) const;
		bool fp_IsConversionSelected(umint _iFirst, umint _nFirst, umint _iSecond, umint _nSecond);
		bool fp_IsLineSelected(umint _iLine) const;
		umint fp_GetColumn(umint _iOffset) const;
		ETextLineEnding fp_GetDefaultLineEnding() const;

		aint fp_PreviousSignificant(umint _iToken) const;
		aint fp_NextSignificant(umint _iToken) const;
		aint fp_PreviousCode(umint _iToken) const;
		aint fp_NextCode(umint _iToken) const;
		bool fp_IsFirstOnLine(umint _iToken) const;
		bool fp_IsCommentLine(umint _iToken) const;
		bool fp_IsCommentedDirective(umint _iToken) const;
		aint fp_FindCodeBelowComments(umint _iLast) const;
		bool fp_IsTrailingBlockComment(umint _iToken) const;
		bool fp_IsNameComment(umint _iToken) const;
		bool fp_AreWords(umint _iLeft, umint _iRight) const;
		ECodeSpacing fp_GetJoinSpacing(umint _iLeft, umint _iRight) const;
		bool fp_IsDSLData(umint _iNode) const;
		aint fp_LeadingElse(umint _iToken) const;
		bool fp_IsLastOnLine(umint _iToken) const;

		void fp_RuleIndentation();
		void fp_RuleDirectiveIndentation();
		void fp_RuleTrailingWhitespace();
		void fp_RuleLineEndings();
		void fp_RuleFinalNewline();
		void fp_RuleTokenSpacing();
		bool fp_HasOperand(umint _iToken, bool _bBefore) const;
		bool fp_EndsOperand(aint _iToken) const;
		aint fp_ContinuesLiteral(umint _iToken) const;
		bool fp_ContinuesValue(umint _iContinued) const;
		bool fp_IsKeyAssign(umint _iAssign) const;
		aint fp_FindContinuedStringLead(umint _iContinued) const;
		umint fp_FindStringRunStart(umint _iLiteral) const;
		void fp_RuleBlankLines();
		void fp_RuleLineBreaks();
		void fp_LayoutNode(umint _iNode, umint _iIndent);
		void fp_LayoutStatement(umint _iNode, umint _iIndent);
		void fp_ProbeBodies(umint _iNode, umint _iIndent);
		void fp_LayoutInitializerList(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent);
		umint fp_FindInitializerList(umint _iNode, umint _iFirstParen, umint _iLast) const;
		bool fp_LayoutGroup(umint _iNode, umint _iIndent, bool _bBreakBefore = true);
		void fp_LayoutElements(umint _iNode, umint _iIndent);
		void fp_LayoutElementRange(umint _iNode, umint _iFirst, umint _iEnd, NContainer::TCVector<umint> const &_Splits, umint _iIndent);
		void fp_LayoutFileElements(umint _iNode);
		bool fp_LayoutRange(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndentContinuations, bool _bMustSplit = false);
		bool fp_BreakAtAssign(umint _iFirst, umint _iOperator, umint _iIndent, umint _nContinuation);
		bool fp_IsLambdaIntroducer(umint _iToken) const;
		bool fp_FollowsScope(umint _iToken) const;
		bool fp_IsFunctionQualifier(umint _iToken) const;
		bool fp_IsAssignment(umint _iToken) const;
		bool fp_IsTrailingReturnArrow(umint _iToken) const;
		bool fp_ClosesLambdaIntroducer(umint _iToken) const;
		umint fp_SkipTemplateHeader(umint _iToken) const;
		bool fp_IsCastGroup(umint _iNode) const;
		bool fp_IsStructuredBinding(umint _iNode) const;
		CChildSpan fp_ChildrenIn(CCodeNode const &_Node, umint _iFirst, umint _iLast) const;
		bool fp_StartsLineBehindComments(umint _iToken) const;
		aint fp_FindSharedCaseLabel(umint _iToken) const;
		umint fp_GetTrailedCodeIndent(umint _iComment) const;
		bool fp_IsContinuationComment(umint _iComment) const;
		bool fp_StartsSharedCaseLabel(umint _iLineFirst, umint _iEnd) const;
		bool fp_StartsBodyBehind(umint _iComment, umint _nColumns) const;
		bool fp_IsNamedCast(umint _iOpen) const;
		bool fp_LayoutScopes(umint _iNode, umint _iFirst, umint _iLast, umint _iIndent, bool _bClause, bool _bIndent, bool _bMustSplit = false);
		bool fp_IsYieldedScope(umint _iOpen) const;
		ECodeSpacing fp_GetInlineSpacing(umint _iLeft, umint _iRight) const;
		ECodeSpacing fp_DecideInlineSpacing(umint _iLeft, umint _iRight) const;
		bool fp_ClosesBehindComma(umint _iToken) const;
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
		void fp_ConvertTrailingComments();
		void fp_ConvertCommentPrefixes();
		bool fp_MeasureTrailedCode(umint _iComment, CTrailedCode &o_Code) const;
		bool fp_MeasureOnOpeningBraceLine(umint _iComment, CTrailedCode const &_Code, umint &o_nColumns) const;
		bool fp_MultiLineTrailingCommentLines(umint _iComment, CStr &o_Replacement) const;
		void fp_ConvertSeparatedCommas(CCodeNode const &_Body, bool _bEnum);
		bool fp_MayBeMacroArgument(CCodeNode const &_Node) const;
		bool fp_IsInMacroArgument(umint _iToken);
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
		void fp_PlaceCutBody(umint _iHead, umint _iBody);
		bool fp_IsGuardedInEveryBranch(umint _iEndif) const;
		bool fp_KeepsOwnLines(umint _iNode, bool _bListsMove = false) const;
		bool fp_IsRequirementsBody(umint _iNode, umint _iBrace) const;
		void fp_PlaceBody(umint _iNode, umint _iBlock, umint _iIndent, bool _bDeclarator);
		bool fp_FollowsOpaqueDirective(umint _iToken) const;
		bool fp_PlaceBlock(umint _iBlock, umint _iIndent, umint _nReference);
		bool fp_CanPlaceBlock(umint _iBlock) const;
		void fp_ShiftBlock(umint _iBlock, aint _nDelta);
		void fp_PrepareBlockEnds();
		void fp_PrepareDirectives();
		NStr::CStr fp_GetDirectiveKeyword(umint _iToken) const;
		bool fp_IsDirectiveParallel(umint _iDirective, umint _iOpen, umint _iClose) const;
		bool fp_HasOpaqueDirective(umint _iFirst, umint _iLast) const;
		bool fp_SpansLines(umint _iFirst, umint _iLast) const;
		umint fp_GetSourceLineIndent(umint _iToken) const;
		void fp_MarkInline(umint _iFirst, umint _iLast);
		void fp_EmitLayout();
		void fp_KeepSuppressedLines();
		bool fp_IsInSelectedStatement(umint _iOffset) const;
		NContainer::TCVector<uint8> const &fp_GetSuppressedLines() const;
		bool fp_IsSuppressed(umint _iOffset, umint _nLength) const;
		bool fp_IsRangeJoinable(umint _iNode, umint _iFirst, umint _iLast) const;
		bool fp_IsAttachedComment(umint _iToken) const;
		bool fp_FitsInline(umint _iFirst, umint _iLast, umint _iIndent) const;
		umint fp_GetStatementIndent(umint _iToken) const;
		bool fp_IsLevelled(umint _iNode) const;
		umint fp_FindHuggingList(umint _iNode) const;
		aint fp_FindLeadingAttributesEnd(umint _iFirst, umint _iLast) const;
		bool fp_IsBrokenInsideGroups(umint _iNode, umint _iFirst, umint _iLast) const;
		bool fp_CanShiftCommentLines(umint _iComment, aint _nDelta) const;
		void fp_ShiftCommentLines(umint _iComment, aint _nDelta, ch8 const *_pRule, ch8 const *_pExplanation);
		bool fp_HoldsNameAlone(umint _iGroup) const;
		NStr::CStr fp_MakeIndent(umint _nColumns) const;
		void fp_AppendIndent(NStr::CStr::CAppender &o_Appender, umint _nColumns) const;
		void fp_LimitJoinedLines();
		void fp_BuildPlan(NContainer::TCVector<CCodeFormattingEdit> &o_Edits, NContainer::TCVector<umint> &o_Sources) const;
		void fp_JoinNode(umint _iNode);
		void fp_LayoutLambdaHead(umint _iNode, umint _iBody);
		bool fp_TryJoin(umint _iFirstToken, umint _iLastToken, umint _iStartColumn);
		bool fp_MeasureJoinedWidth(umint _iFirstToken, umint _iLastToken, umint &o_nColumns, bool _bBlockComments = false) const;
		umint fp_GetTokenColumns(CCodeToken const &_Token) const;
		void fp_DiagnoseLineLength(NContainer::TCVector<CCodeFormattingEdit> const &_Edits);
		void fp_EnsureSingleSpace(umint _iToken, bool _bBefore, ch8 const *_pRule, ch8 const *_pExplanation);
		void fp_RemoveSpaceBefore(umint _iToken, ch8 const *_pRule, ch8 const *_pExplanation);
		void fp_RemoveFollowingBlankLines(umint _iToken, ch8 const *_pRule, ch8 const *_pExplanation);
		void fp_AlignContinuations();
		void fp_AlignListColumns();
		bool fp_PrepareBuildSystemLines(CStr &o_Explanation);
		void fp_RuleBuildSystemIndentation();
		void fp_RuleBuildSystemBlankLines();
		void fp_RuleBuildSystemLineBreaks();

		CCodeFormattingRequest const &m_Request;
		ch8 const *m_pSource = nullptr;                            // m_Request's source, which no analysis changes.
		CCodeTokenStream m_Tokens;
		CCodeStructure m_Structure;
		CTextLineMap m_Lines;
		bool m_bAllowConversions = true;                           // Only the analysis of the original source converts return types.
		bool m_bAllowQualifiers = true;                            // Qualifiers move first, in a stage of their own, so no other conversion has to know of them.
		CStr m_Baseline;                                           // The source with every conversion made, which the result matches token for token.
		bool m_bProbing = false;                                   // The statement walk only decides conversions, and lays nothing out.
		bool m_bOperatorSplit = false;                             // The statement broke at operators, so a block belongs to a continuation.
		umint m_iRangeBody = TCLimitsInt<umint>::mc_Max;           // The brace of a body that opens at the level of the range in front of it.
		umint m_iSplitFirstParen = 0;                              // A declaration is never split before its name.
		umint m_nHeadDepth = 0;                                    // How many head layouts are in progress around the current one.
		NContainer::TCVector<umint> m_FailedHeads;                 // First, last, indent and continuation of each head the outermost layout failed to split.
		umint m_iSplitTrailingReturn = TCLimitsInt<umint>::mc_Max;
		NContainer::TCVector<umint> m_TokenDepth;                  // Bracket nesting of each token, for finding a range's own level.
		NContainer::TCVector<umint> m_BlockEnds;                   // Indexed by token: the closing brace of the block the token opens, or the token count.
		NContainer::TCVector<umint> m_OpenListEnds;                // Indexed by token: the closing marker of a list written open that the token opens.
		NContainer::TCVector<uint8> m_KeepsListLines;              // Indexed by node: it holds a list written across lines that keeps them.
		NContainer::TCVector<uint8> m_OpaqueDirective;             // Indexed by token: a directive whose branches cut a construct, so nothing is read across it.
		NContainer::TCVector<umint> m_OpaqueBefore;                // Indexed by token: how many opaque directives stand in front of it.
		NContainer::TCVector<CCodeFormattingRange> m_Conditionals; // Source spans of the '#if' groups, for saying which one a structure was cut by.
		NContainer::TCVector<uint8> m_GapState;                    // Indexed by token: what the gap in front of it becomes.
		mutable NContainer::TCVector<uint8> m_CanonicalSpacing;    // Indexed by token: the standard's spelling of the gap in front of it, plus one, or zero when not yet asked.
		mutable NContainer::TCVector<uint8> m_InlineSpacing;       // Indexed by token: the spacing rules' spelling of the gap in front of it, plus one, or zero when not yet asked.
		mutable NContainer::TCVector<umint> m_TokenColumns;        // Indexed by token: its width in columns, plus one, or zero when not yet measured.
		NContainer::TCVector<umint> m_GapIndent;                   // The indentation a break in front of the token takes.
		NContainer::TCVector<uint8> m_CommentMoved;                // Indexed by token: a comment on a line of its own that moves, 2 to the code under it's level.
		NContainer::TCVector<uint8> m_CommaGone;                   // Indexed by token: a comma a list's conversion takes out.
		NContainer::TCVector<uint8> m_BlankBefore;                 // A blank line stands in front of the token, which the layout writes where it writes the gap.
		NContainer::TCVector<umint> m_CommentIndent;               // The indentation such a comment's line takes.
		NContainer::TCVector<CCodeFormattingEdit> m_Structural;    // Token changes: return types moved behind their parameter lists, braces dropped.
		NContainer::TCVector<uint8> m_ProtectedStart;
		NContainer::TCVector<uint8> m_ProtectedEnd;
		NContainer::TCVector<CCodeFormattingRange> m_Disabled;
		NContainer::TCVector<CCodeFormattingRange> m_Owned;        // Text a directive lays out itself, which no other rule edits.
		NContainer::TCVector<CCodeFormattingRange> m_Effective;
		NContainer::TCVector<CListLayout> m_ListLayouts;
		NContainer::TCVector<umint> m_ListLayoutOfNode;            // Indexed by node: its layout plus one, or zero.
		NContainer::TCVector<umint> m_AlignDirectives;             // The comment tokens of 'align: =' directives.
		NContainer::TCVector<CCodeFormattingEdit> m_Edits;
		NContainer::TCVector<NStr::CStr> m_EditExplanations;
		NContainer::TCVector<uint8> m_EditDropped;
		mutable NContainer::TCVector<uint8> m_SuppressedLines;     // Indexed by line, one more than there are: a NOLINT or NOLINTNEXTLINE suppresses it.
		mutable bool m_bSuppressedLinesFound = false;
		NContainer::TCVector<CJoinCandidate> m_JoinCandidates;
		NContainer::TCVector<CCodeFormattingDiagnostic> m_Diagnostics;
		NContainer::TCVector<CBuildSystemLine> m_BuildSystemLines;
		NContainer::TCVector<uint8> m_BuildSystemPlaced;           // Indexed by token: the value layout writes the gap in front of it.
		bool m_bWholeFile = false;
	};
}
