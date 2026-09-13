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
		, mc_Block				// A braced sequence of statements.
		, mc_Statement			// One statement, including any block it owns.
		, mc_Group				// A bracketed group: (), [], {} as an initializer, or <> as a template argument list.
		, mc_Unsupported		// Structure the builder could not classify; its layout is preserved.
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
		ECodeNodeKind m_Kind = ECodeNodeKind::mc_Unsupported;
		ECodeBracket m_Bracket = ECodeBracket::mc_None;
		umint m_iFirstToken = 0;					// Inclusive.
		umint m_iLastToken = 0;						// Inclusive.
		umint m_iParent = 0;
		NContainer::TCVector<umint> m_Children;

		// Token indices, inside this node, where the canonical split form starts a new line.
		// For a group these are its top-level separators; for a statement its chained parts.
		NContainer::TCVector<umint> m_SplitPoints;

		bool m_bHasComment = false;					// A comment anywhere inside forbids joining to one line.
		bool m_bHasDirective = false;				// A preprocessor directive likewise.
		bool m_bHasBlock = false;					// A braced group holding statements must keep its own lines.
		bool m_bHasMultiLineToken = false;			// A raw literal or block comment fixes its own line breaks.
		bool m_bFixedLineBreaks = false;			// The node's own line structure is prescribed; only its children may be relaid out.
		bool m_bHasMultiLineBrace = false;			// A braced initializer written across lines keeps those lines.

		bool f_IsJoinable() const;
	};

	// Builds the layout tree for a lexed source. Anything the builder cannot classify becomes
	// an unsupported node, so the renderer preserves it instead of guessing a shape for it.
	struct CCodeStructure
	{
		CCodeStructure() = default;
		explicit CCodeStructure(CCodeTokenStream const &_Tokens);

		NContainer::TCVector<CCodeNode> const &f_GetNodes() const;
		CCodeNode const &f_GetRoot() const;
		bool f_IsComplete() const;

		// True for a '<' or '>' the builder resolved as a template argument list bracket
		// rather than a comparison operator.
		bool f_IsAngleBracket(umint _iToken) const;

	private:
		umint fp_AddNode(ECodeNodeKind _Kind, umint _iParent);
		void fp_Finish(umint _iNode, umint _iLastToken);
		umint fp_BuildBlock(umint _iNode, umint _iToken, bool _bBraced);
		umint fp_BuildStatement(umint _iParent, umint _iToken);
		umint fp_BuildGroup(umint _iParent, umint _iToken, ECodeBracket _Bracket);
		umint fp_MatchAngleGroup(umint _iToken) const;
		bool fp_IsBlockBrace(umint _iToken) const;

		void fp_Note(umint _iNode, umint _iSignificant);

		CCodeTokenStream const *mp_pTokens = nullptr;
		NContainer::TCVector<umint> mp_Significant;		// Indices of significant tokens, in order.
		NContainer::TCVector<uint8> mp_GapFlags;		// What the trivia before each significant token contains.
		NContainer::TCVector<uint8> mp_bAngleBracket;	// Indexed by token, set for resolved template brackets.
		NContainer::TCVector<CCodeNode> mp_Nodes;
		bool mp_bComplete = true;
	};

	// The canonical inline separator between two adjacent significant tokens.
	enum class ECodeSpacing
	{
		mc_None					// The tokens are written without a space.
		, mc_Space				// Exactly one space.
		, mc_Preserve			// The standard does not decide; keep what the source has.
	};

	ECodeSpacing fg_GetCanonicalSpacing(CCodeTokenStream const &_Tokens, CCodeStructure const &_Structure, umint _iLeft, umint _iRight);
}
