// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Develop_CodeFormattingLexer.h"

#include <Mib/Container/Vector>

namespace NMib::NDevelop
{
	enum class ECodeNodeKind
	{
		mc_File
		, mc_Block       // A braced sequence of statements.
		, mc_Statement   // One statement, including any block it owns.
		, mc_Group       // A bracketed group: (), [], {} as an initializer, or <> as a template argument list.
		, mc_Unsupported // Structure the builder could not classify; its layout is preserved.
	};

	enum class ECodeBracket
	{
		mc_None
		, mc_Paren
		, mc_Square
		, mc_Brace
		, mc_Angle
	};

	// One node of the layout tree. Token indices address the lexer's flat token vector and
	// always name significant tokens, so trivia between them stays available for edits.
	struct CCodeNode
	{
		bool f_IsJoinable() const;

		ECodeNodeKind m_Kind = ECodeNodeKind::mc_Unsupported;
		ECodeBracket m_Bracket = ECodeBracket::mc_None;
		umint m_iFirstToken = 0; // Inclusive.
		umint m_iLastToken = 0;  // Inclusive.
		umint m_iParent = 0;
		NContainer::TCVector<umint> m_Children;

		// Token indices, inside this node, where the canonical split form starts a new line.
		// For a group these are its top-level separators; for a statement its chained parts.
		NContainer::TCVector<umint> m_SplitPoints;

		bool m_bHasComment = false;        // A comment anywhere inside forbids joining to one line.
		bool m_bHasDirective = false;      // A preprocessor directive likewise.
		bool m_bHasBlock = false;          // A braced group holding statements must keep its own lines.
		bool m_bHasMultiLineToken = false; // A raw literal or block comment fixes its own line breaks.
		bool m_bFixedLineBreaks = false;   // The node's own line structure is prescribed; only its children may be relaid out.
		bool m_bTemplateHeader = false;    // The statement opens with a template header, which keeps the line it is on.
		bool m_bHasMultiLineBrace = false; // A braced initializer written across lines keeps those lines.
		bool m_bList = false;              // A braced initializer or a DSL's array: data, which opens below what it ends.
		bool m_bWrittenOpen = false;       // The list's own opening marker ends its line, so it never fits on one.
		bool m_bMessageSend = false;       // An Objective-C '[Receiver Keyword: Argument]', split at each selector part.
		bool m_bObjectiveCHeader = false;  // The line an '@interface', '@implementation' or '@protocol' section opens with.
		bool m_bClauseMacro = false;       // A macro standing where an 'if' does, the 'else' behind its block telling it: 'BOOST_IF_CONSTEXPR(c)'.
		bool m_bObjectiveCMethod = false;  // An Objective-C method's declaration or definition: '- (void)f_Set: (int)_Value;'.
		bool m_bFileElements = false;      // A file of a list's elements, which another file includes inside its braces.
	};

	// Builds the layout tree for a lexed source. Anything the builder cannot classify becomes
	// an unsupported node, so the renderer preserves it instead of guessing a shape for it.
	struct CCodeStructure
	{
#if defined(DMibSanitizerEnabled_Address) && defined(DPlatformFamily_Windows)
		// AddressSanitizer's frames take about 40 KB a level of the layout, of a Windows thread's 1 MB.
		static constexpr umint mc_MaxBracketDepth = 16;
#else
		static constexpr umint mc_MaxBracketDepth = 256; // Clang's default '-fbracket-depth'.
#endif

		CCodeStructure() = default;
		explicit CCodeStructure(CCodeTokenStream &_Tokens, umint _nTabWidth = 4);

		static void fs_SplitSharedAngleClosers(CCodeTokenStream &_Tokens);

		NContainer::TCVector<CCodeNode> const &f_GetNodes() const;
		CCodeNode const &f_GetRoot() const;
		bool f_IsComplete() const;

		// Byte offset of the construct the builder could not close, when it is not complete.
		umint f_GetIncompleteOffset() const;
		// True when the brackets nest deeper than mc_MaxBracketDepth, which leaves the structure incomplete.
		bool f_IsTooDeep() const;

		// True for a '<' or '>' the builder resolved as a template argument list bracket
		// rather than a comparison operator.
		bool f_IsAngleBracket(umint _iToken) const;

		// The group or block opening or closing at the token, and the innermost node the
		// token stands strictly inside; each is the node count when there is none.
		umint f_FindNodeOpeningAt(umint _iToken) const;
		umint f_FindNodeClosingAt(umint _iToken) const;
		umint f_FindEnclosingNode(umint _iToken) const;
		// The statement ending at the token, or the node count when none does.
		umint f_FindStatementEndingAt(umint _iToken) const;
		// True for the block of a statement that defines an enumeration.
		bool f_IsEnumBody(umint _iBlock) const;

	private:
		void fp_BuildIndex();
		void fp_CollectSignificant();
		bool fp_SplitSharedAngleClosers();
		umint fp_AddNode(ECodeNodeKind _Kind, umint _iParent);
		void fp_Finish(umint _iNode, umint _iLastToken);
		umint fp_BuildBlock(umint _iNode, umint _iToken, bool _bBraced);
		umint fp_BuildStatement(umint _iParent, umint _iToken);
		bool fp_IsUnlistedMacro(umint _iFirst, umint _iNext) const;
		bool fp_IsInFunctionBlock(umint _iBlock) const;
		bool fp_EndsCallStatement(umint _iBlock, umint _iFirst, umint _iNext) const;
		umint fp_GetColumn(umint _iToken) const;
		umint fp_BuildObjectiveCHeader(umint _iNode, umint _iToken);
		umint fp_BuildObjectiveCMethod(umint _iNode, umint _iToken);
		umint fp_BuildGroup(umint _iParent, umint _iToken, ECodeBracket _Bracket, umint _iClose = 0);
		umint fp_MatchAngleGroup(umint _iToken, bool _bNested = false) const;
		void fp_ClassifyMessageSend(umint _iNode, umint _iOpen, umint _iClose);
		bool fp_IsBlockBrace(umint _iToken) const;
		bool fp_IsLambdaBody(umint _iToken) const;
		bool fp_IsRequirementBrace(umint _iToken) const;
		bool fp_StartsAlternativeBranch(umint _iToken) const;

		void fp_Note(umint _iNode, umint _iSignificant);

		CCodeTokenStream *mp_pTokens = nullptr;
		NContainer::TCVector<umint> mp_Significant;         // Indices of significant tokens, in order.
		mutable NContainer::TCVector<uint8> mp_CallRunEnds; // Indexed by significant token: how the run of calls standing at a call ends, plus one, or zero.
		NContainer::TCVector<uint8> mp_GapFlags;            // What the trivia before each significant token contains.
		NContainer::TCVector<uint8> mp_AngleBracket;        // Indexed by token, set for resolved template brackets.
		mutable NContainer::TCVector<uint8> mp_NoAngleCloser; // Indexed by significant token: set for a '<' a search already found no '>' for.
		umint mp_iIncompleteOffset = 0;
		NContainer::TCVector<CCodeNode> mp_Nodes;
		NContainer::TCVector<umint> mp_OpeningAt;           // Indexed by token: the node opening there, or the node count.
		NContainer::TCVector<umint> mp_ClosingAt;           // Indexed by token: the node closing there, or the node count.
		NContainer::TCVector<umint> mp_Enclosing;           // Indexed by token: the innermost node around it, or the node count.
		NContainer::TCVector<umint> mp_StatementEndingAt;   // Indexed by token: the innermost statement ending there, or the node count.
		bool mp_bComplete = true;
		bool mp_bTooDeep = false;
		umint mp_nGroupDepth = 0;                           // The groups being built around the one being built, which the angle brackets add to.
		umint mp_nTabWidth = 4;                             // The columns a tab advances to the next multiple of.
	};

	// The canonical inline separator between two adjacent significant tokens.
	enum class ECodeSpacing
	{
		mc_None       // The tokens are written without a space.
		, mc_Space    // Exactly one space.
		, mc_Preserve // The standard does not decide; keep what the source has.
	};

	ECodeSpacing fg_GetCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight);
	bool fg_CodeTokensMerge(CCodeTokenStream const &_Tokens, CCodeToken const &_Left, CCodeToken const &_Right);

	// True for a '*', '&' or '&&' that declares a pointer or reference rather than operating on two values.
	bool fg_IsDeclaratorToken(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken);

	// True when the ']' at _iToken closes a lambda's capture list rather than a subscript.
	bool fg_IsCaptureList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose);
	bool fg_IsBlockLiteralCaret(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iToken);
	bool fg_HoldsArguments(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup);
	bool fg_HoldsParameters(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iGroup);
	bool fg_IsFunctionBody(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iBlock);

	// True when the ')' at _iClose ends a parameter list rather than a call's arguments.
	bool fg_ClosesParameterList(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iClose);

	// True when the '->' at _iArrow introduces a trailing return type rather than a member access.
	bool fg_IsTrailingReturnArrow(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iArrow);
}
