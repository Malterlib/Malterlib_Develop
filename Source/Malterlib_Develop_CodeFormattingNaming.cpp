// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingNaming.h"

#include <Mib/Encoding/Json>

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;

	namespace
	{
		struct CNamingListInfo
		{
			ch8 const *m_pKey;
			ECodeNameRole m_Role;
		};

		constexpr CNamingListInfo gc_NamingLists[] =
			{
				{"FunctionNames", ECodeNameRole::mc_Function}
				, {"TypeNames", ECodeNameRole::mc_Type}
				, {"MacroNames", ECodeNameRole::mc_Macro}
				, {"SpecifierMacros", ECodeNameRole::mc_SpecifierMacro}
				, {"DSLMarkers", ECodeNameRole::mc_DSLMarker}
			}
		;
		static_assert(sizeof(gc_NamingLists) / sizeof(gc_NamingLists[0]) == umint(ECodeNamingList::mc_Count));

		constexpr int64 gc_NamingVersion = 1;

		// Compares a name that is not terminated with one that is, as the sorted order does.
		COrdering_Strong fg_CompareName(ch8 const *_pName, umint _nLength, CStr const &_Other)
		{
			auto pOther = _Other.f_GetStr();
			umint nOther = _Other.f_GetLen();
			for (umint i = 0; i < _nLength && i < nOther; ++i)
			{
				if (_pName[i] != pOther[i])
					return uch8(_pName[i]) <=> uch8(pOther[i]);
			}

			return _nLength <=> nOther;
		}
	}

	// Reads one document. A key the format does not have, a version it does not know, and a
	// list that holds anything but strings are errors: a misspelt key would otherwise leave
	// the names it meant to set with those of the document it extends.
	CCodeFormattingNamingDocument CCodeFormattingNamingDocument::fs_Parse(CStr const &_Yaml)
	{
		auto Document = NEncoding::CJsonSorted::fs_FromStringYaml(_Yaml);
		if (!Document.f_IsObject())
			DMibError("A .malterlib-format document is a mapping of keys to values");

		CCodeFormattingNamingDocument Result;
		bool bVersion = false;
		for (auto &Member : Document.f_Object())
		{
			auto const &Key = Member.f_Name();
			auto const &Value = Member.f_Value();
			if (Key == "Version")
			{
				if (!Value.f_IsInteger() || Value.f_Integer() != gc_NamingVersion)
					DMibError("Version must be {}"_f << gc_NamingVersion);

				bVersion = true;

				continue;
			}

			if (Key == "Extends")
			{
				if (!Value.f_IsString() || Value.f_String().f_IsEmpty())
					DMibError("Extends must name a file");

				Result.m_Extends = Value.f_String();

				continue;
			}

			umint iList = 0;
			while (iList < umint(ECodeNamingList::mc_Count) && Key != gc_NamingLists[iList].m_pKey)
				++iList;

			if (iList == umint(ECodeNamingList::mc_Count))
				DMibError("Unknown key '{}'"_f << Key);

			if (!Value.f_IsArray())
				DMibError("{} must be a list"_f << Key);

			auto &List = Result.m_Lists[iList].f_CreateNew();
			for (auto const &Entry : Value.f_Array())
			{
				if (!Entry.f_IsString() || Entry.f_String().f_IsEmpty())
					DMibError("{} holds only non-empty strings"_f << Key);

				List.f_Insert(Entry.f_String());
			}
		}

		if (!bVersion)
			DMibError("Version is required");

		return Result;
	}

	// A list the document holds replaces the one it extends, which is applied first.
	void CCodeFormattingNaming::f_Apply(CCodeFormattingNamingDocument const &_Document)
	{
		for (umint iList = 0; iList < umint(ECodeNamingList::mc_Count); ++iList)
		{
			if (_Document.m_Lists[iList])
				mp_Lists[iList] = *_Document.m_Lists[iList];
		}

		fp_Compile();
	}

	// A pattern is anchored at both ends and spells literal characters, '.' for any
	// character, a class such as '[A-Z0-9_]', and '*' repeating the atom in front of it.
	// One without any of those is a name, looked up exactly.
	void CCodeFormattingNaming::fp_Compile()
	{
		mp_Exact.f_Clear();
		mp_Patterns.f_Clear();
		for (umint iList = 0; iList < umint(ECodeNamingList::mc_Count); ++iList)
		{
			auto Role = gc_NamingLists[iList].m_Role;
			for (auto const &Entry : mp_Lists[iList])
			{
				auto pEntry = Entry.f_GetStr();
				umint nEntry = Entry.f_GetLen();
				bool bPattern = false;
				for (umint i = 0; i < nEntry; ++i)
					bPattern |= pEntry[i] == '.' || pEntry[i] == '[' || pEntry[i] == '*';

				if (!bPattern)
				{
					auto &Exact = mp_Exact.f_Insert();
					Exact.m_Name = Entry;
					Exact.m_Role = Role;

					continue;
				}

				CPattern Pattern;
				Pattern.m_Role = Role;
				for (umint i = 0; i < nEntry; )
				{
					auto Character = pEntry[i];
					if (Character == '*')
					{
						if (Pattern.m_Atoms.f_IsEmpty() || Pattern.m_Atoms.f_GetLast().m_bRepeat)
							DMibError("'*' repeats nothing in pattern '{}'"_f << Entry);

						Pattern.m_Atoms.f_GetLast().m_bRepeat = true;
						++i;

						continue;
					}

					auto &Atom = Pattern.m_Atoms.f_Insert();
					if (Character == '.')
					{
						Atom.m_bAny = true;
						++i;

						continue;
					}

					if (Character != '[')
					{
						Atom.m_Ranges.f_Insert(CRange{Character, Character});
						++i;

						continue;
					}

					++i;
					while (i < nEntry && pEntry[i] != ']')
					{
						CRange Range{pEntry[i], pEntry[i]};
						if (i + 2 < nEntry && pEntry[i + 1] == '-' && pEntry[i + 2] != ']')
						{
							Range.m_High = pEntry[i + 2];
							i += 3;
						}
						else
							++i;

						Atom.m_Ranges.f_Insert(Range);
					}

					if (i >= nEntry || Atom.m_Ranges.f_IsEmpty())
						DMibError("Unterminated or empty class in pattern '{}'"_f << Entry);

					++i;
				}

				mp_Patterns.f_Insert(fg_Move(Pattern));
			}
		}

		for (auto &Mask : mp_PatternsByFirst)
			Mask = 0;

		for (umint iPattern = 0; iPattern < mp_Patterns.f_GetLen() && iPattern < 64; ++iPattern)
		{
			for (umint Character = 0; Character < 256; ++Character)
			{
				if (fsp_CanStartWith(mp_Patterns[iPattern], ch8(Character)))
					mp_PatternsByFirst[Character] |= uint64(1) << iPattern;
			}
		}

		mp_Exact.f_Sort
			(
				[](CExact const &_Left, CExact const &_Right)
				{
					return _Left.m_Name <=> _Right.m_Name;
				}
			)
		;

		// A name listed twice holds both roles, in one entry the lookup finds.
		TCVector<CExact> Merged;
		for (auto &Exact : mp_Exact)
		{
			if (!Merged.f_IsEmpty() && Merged.f_GetLast().m_Name == Exact.m_Name)
				Merged.f_GetLast().m_Role |= Exact.m_Role;
			else
				Merged.f_Insert(fg_Move(Exact));
		}

		mp_Exact = fg_Move(Merged);
	}

	bool CCodeFormattingNaming::fsp_Accepts(CAtom const &_Atom, ch8 _Character)
	{
		if (_Atom.m_bAny)
			return true;

		for (auto const &Range : _Atom.m_Ranges)
		{
			if (_Character >= Range.m_Low && _Character <= Range.m_High)
				return true;
		}

		return false;
	}

	// Repeated atoms in front can match nothing, so any of them up to the first single one can take the first character.
	bool CCodeFormattingNaming::fsp_CanStartWith(CPattern const &_Pattern, ch8 _Character)
	{
		for (auto const &Atom : _Pattern.m_Atoms)
		{
			if (fsp_Accepts(Atom, _Character))
				return true;

			if (!Atom.m_bRepeat)
				return false;
		}

		return false;
	}

	bool CCodeFormattingNaming::fsp_Matches(CPattern const &_Pattern, umint _iAtom, ch8 const *_pName, umint _nLength)
	{
		if (_iAtom == _Pattern.m_Atoms.f_GetLen())
			return _nLength == 0;

		auto const &Atom = _Pattern.m_Atoms[_iAtom];
		if (!Atom.m_bRepeat)
			return _nLength && fsp_Accepts(Atom, _pName[0]) && fsp_Matches(_Pattern, _iAtom + 1, _pName + 1, _nLength - 1);

		// The longest run first; names are short, and patterns end in their repeat.
		umint nRun = 0;
		while (nRun < _nLength && fsp_Accepts(Atom, _pName[nRun]))
			++nRun;

		for (umint nTaken = nRun + 1; nTaken--; )
		{
			if (fsp_Matches(_Pattern, _iAtom + 1, _pName + nTaken, _nLength - nTaken))
				return true;
		}

		return false;
	}

	ECodeNameRole CCodeFormattingNaming::f_GetRoles(ch8 const *_pName, umint _nLength) const
	{
		ECodeNameRole Roles = ECodeNameRole::mc_None;
		umint iLow = 0;
		umint iHigh = mp_Exact.f_GetLen();
		while (iLow < iHigh)
		{
			auto iMiddle = iLow + (iHigh - iLow) / 2;
			auto Ordering = fg_CompareName(_pName, _nLength, mp_Exact[iMiddle].m_Name);
			if (Ordering == 0)
			{
				Roles = mp_Exact[iMiddle].m_Role;

				break;
			}

			if (Ordering < 0)
				iHigh = iMiddle;
			else
				iLow = iMiddle + 1;
		}

		if (!_nLength)
			return Roles;

		umint nPatterns = mp_Patterns.f_GetLen();
		for (uint64 Candidates = mp_PatternsByFirst[uch8(_pName[0])]; Candidates; Candidates &= Candidates - 1)
		{
			auto const &Pattern = mp_Patterns[fg_GetLowestBitSetNoZero(Candidates)];
			if ((Roles & Pattern.m_Role) == ECodeNameRole::mc_None && fsp_Matches(Pattern, 0, _pName, _nLength))
				Roles |= Pattern.m_Role;
		}

		for (umint iPattern = 64; iPattern < nPatterns; ++iPattern)
		{
			auto const &Pattern = mp_Patterns[iPattern];
			if ((Roles & Pattern.m_Role) == ECodeNameRole::mc_None && fsp_Matches(Pattern, 0, _pName, _nLength))
				Roles |= Pattern.m_Role;
		}

		return Roles;
	}
}
