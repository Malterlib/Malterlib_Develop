// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Malterlib_Develop_CodeFormattingLexer.h"

#include <Mib/Container/Vector>
#include <Mib/Storage/Optional>
#include <Mib/String/String>

namespace NMib::NDevelop
{
	// The lists a naming document can hold, each read as the role it gives a name.
	enum class ECodeNamingList : uint8
	{
		mc_FunctionNames
		, mc_TypeNames
		, mc_MacroNames
		, mc_SpecifierMacros
		, mc_DSLMarkers
		, mc_NoReturnMacros

		, mc_Count
	};

	// One '.malterlib-format' as written, before the document it extends is applied under it.
	struct CCodeFormattingNamingDocument
	{
		struct CListChange
		{
			NContainer::TCVector<NStr::CStr> m_Names;
			bool m_bAppend = false;														// Adds to the list it extends instead of replacing it.
		};

		static CCodeFormattingNamingDocument fs_Parse(NStr::CStr const &_Yaml);

		NStr::CStr m_Extends;															// Relative to the document's directory; empty when it extends nothing.
		NStorage::TCOptional<CListChange> m_Lists[umint(ECodeNamingList::mc_Count)];	// Unset keeps what it extends.
	};

	// The names a project gives its functions, types and macros, compiled for a lookup per identifier.
	struct CCodeFormattingNaming
	{
		void f_Apply(CCodeFormattingNamingDocument const &_Document);
		ECodeNameRole f_GetRoles(ch8 const *_pName, umint _nLength) const;

	private:
		struct CRange
		{
			ch8 m_Low = 0;
			ch8 m_High = 0;
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
		};

		struct CExact
		{
			NStr::CStr m_Name;
			ECodeNameRole m_Role = ECodeNameRole::mc_None;
		};

		void fp_Compile();
		static bool fsp_Matches(CPattern const &_Pattern, umint _iAtom, ch8 const *_pName, umint _nLength);
		static bool fsp_Accepts(CAtom const &_Atom, ch8 _Character);
		static bool fsp_CanStartWith(CPattern const &_Pattern, ch8 _Character);

		NContainer::TCVector<NStr::CStr> mp_Lists[umint(ECodeNamingList::mc_Count)];
		NContainer::TCVector<CExact> mp_Exact;			// Sorted by name.
		NContainer::TCVector<CPattern> mp_Patterns;
		uint64 mp_PatternsByFirst[256] = {};			// Bit i: pattern i can start with the character; patterns past 64 are always tried.
	};
}
