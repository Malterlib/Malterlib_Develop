// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Develop_CodeFormattingLexer.h"

#include <Mib/Container/Vector>
#include <Mib/Storage/Optional>
#include <Mib/String/String>

namespace NMib::NDevelop
{
	// The naming lists a style document can hold, each read as the role it gives a name.
	enum class ECodeNamingList : uint8
	{
		mc_FunctionNames
		, mc_TypeNames
		, mc_MacroNames
		, mc_SpecifierMacros
		, mc_DSLMarkers
		, mc_NoReturnMacros
		, mc_UnformattedCalls

		, mc_Count
	};

	// One '.malterlib-format' as written, before the document it extends is applied under it.
	struct CCodeFormattingStyleDocument
	{
		struct CListChange
		{
			NContainer::TCVector<NStr::CStr> m_Names;
			bool m_bAppend = false; // Adds to the list it extends instead of replacing it.
		};

		static CCodeFormattingStyleDocument fs_Parse(NStr::CStr const &_Yaml);

		NStr::CStr m_Extends;                                                        // Relative to the document's directory; empty when it extends nothing.
		NStorage::TCOptional<CListChange> m_Lists[umint(ECodeNamingList::mc_Count)]; // Unset keeps what it extends.
		NStorage::TCOptional<bool> m_bAlignTrailingComments;                         // Unset keeps what it extends.
		NStorage::TCOptional<bool> m_bAlignEnumValues;                               // Unset keeps what it extends.
		NStorage::TCOptional<bool> m_bTrailingScopeAtStatementLevel;                 // Unset keeps what it extends.
		NStorage::TCOptional<umint> m_nAlignTrailingCommentsMaximumColumn;           // Unset keeps what it extends; zero is no limit.
	};

	// A project's style: the names it gives its functions, types and macros, compiled for a lookup per identifier, and its
	// layout options.
	struct CCodeFormattingStyle
	{
		void f_Apply(CCodeFormattingStyleDocument const &_Document);
		ECodeNameRole f_GetRoles(ch8 const *_pName, umint _nLength) const;

		bool m_bAlignTrailingComments = false;           // The comments behind a run of enumerators or members stand at one column.
		bool m_bAlignEnumValues = false;                 // The '=' of a run of enumerators stands at one column.
		bool m_bTrailingScopeAtStatementLevel = false;   // A scope or lambda body ending its statement's only expression has no continuation level.
		umint m_nAlignTrailingCommentsMaximumColumn = 0; // The latest column, from 1, an aligned comment starts at; zero is no limit.

	private:
		struct CRange
		{
			uch32 m_Low = 0;
			uch32 m_High = 0;
		};

		// One character of a pattern: '.' has no ranges and matches any.
		struct CAtom
		{
			NContainer::TCVector<CRange> m_Ranges;
			bool m_bAny = false;
			bool m_bRepeat = false;
		};

		struct CPattern
		{
			NContainer::TCVector<CAtom> m_Atoms;
			ECodeNameRole m_Role = ECodeNameRole::mc_None;
			bool m_bPrefixThenAny = false; // Single atoms ending in '.*': 'f_.*', 'C[A-Z].*'.
		};

		struct CExact
		{
			NStr::CStr m_Name;
			ECodeNameRole m_Role = ECodeNameRole::mc_None;
		};

		void fp_Compile();
		static bool fsp_Matches(CPattern const &_Pattern, umint _iAtom, ch8 const *_pName, umint _nLength);
		static bool fsp_Accepts(CAtom const &_Atom, uch32 _Character);
		static bool fsp_CanStartWith(CPattern const &_Pattern, uch32 _Character);

		NContainer::TCVector<NStr::CStr> mp_Lists[umint(ECodeNamingList::mc_Count)];
		NContainer::TCVector<CExact> mp_Exact; // Sorted by name.
		NContainer::TCVector<CPattern> mp_Patterns;
		uint64 mp_PatternsByFirst[256] = {};   // Bit i: pattern i can start with a character of that first byte; patterns past 64 are always tried.
	};
}
