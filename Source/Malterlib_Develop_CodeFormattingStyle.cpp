// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_CodeFormattingStyle.h"

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
				, {"NoReturnMacros", ECodeNameRole::mc_NoReturnMacro}
				, {"UnformattedCalls", ECodeNameRole::mc_UnformattedCall}
			}
		;
		static_assert(sizeof(gc_NamingLists) / sizeof(gc_NamingLists[0]) == umint(ECodeNamingList::mc_Count));

		constexpr int64 gc_StyleVersion = 1;

		// The code point a UTF-8 sequence spells at o_iByte, which it moves past. A byte that starts no valid sequence stands
		// for itself.
		uch32 fg_NextCodePoint(ch8 const *_pText, umint _nLength, umint &o_iByte)
		{
			auto Lead = uch8(_pText[o_iByte]);
			umint nContinuation = Lead >= 0xF0 && Lead < 0xF8 ? 3 : Lead >= 0xE0 && Lead < 0xF0 ? 2 : Lead >= 0xC0 && Lead < 0xE0 ? 1 : 0;
			uch32 CodePoint = nContinuation == 3 ? Lead & 0x07 : nContinuation == 2 ? Lead & 0x0F : Lead & 0x1F;
			bool bValid = nContinuation && o_iByte + nContinuation < _nLength;
			for (umint i = 1; bValid && i <= nContinuation; ++i)
			{
				auto Continuation = uch8(_pText[o_iByte + i]);
				bValid = (Continuation & 0xC0) == 0x80;
				CodePoint = (CodePoint << 6) | (Continuation & 0x3F);
			}

			if (!bValid)
			{
				++o_iByte;

				return Lead;
			}

			o_iByte += nContinuation + 1;

			return CodePoint;
		}

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

	// Reads one document. A list is written as a sequence, which replaces the one extended,
	// or as a mapping holding 'Append', whose sequence adds to it. A key the format does not
	// have, a version it does not know, and a list that holds anything but strings are
	// errors: a misspelt key would otherwise leave the names it meant to set with those of
	// the document it extends.
	CCodeFormattingStyleDocument CCodeFormattingStyleDocument::fs_Parse(CStr const &_Yaml)
	{
		auto Document = NEncoding::CJsonSorted::fs_FromStringYaml(_Yaml);
		if (!Document.f_IsObject())
			DMibError("A .malterlib-format document is a mapping of keys to values");

		CCodeFormattingStyleDocument Result;
		bool bVersion = false;

		for (auto &Member : Document.f_Object())
		{
			auto const &Key = Member.f_Name();
			auto const &Value = Member.f_Value();

			if (Key == "Version")
			{
				if (!Value.f_IsInteger() || Value.f_Integer() != gc_StyleVersion)
					DMibError("Version must be {}"_f << gc_StyleVersion);

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

			if (Key == "AlignTrailingCommentsMaximumColumn")
			{
				if (!Value.f_IsInteger() || Value.f_Integer() < 0)
					DMibError("{} must be a column, or 0 for no limit"_f << Key);

				Result.m_nAlignTrailingCommentsMaximumColumn = umint(Value.f_Integer());

				continue;
			}

			if (Key == "AlignTrailingComments" || Key == "AlignEnumValues" || Key == "TrailingScopeAtStatementLevel")
			{
				if (!Value.f_IsBoolean())
					DMibError("{} must be true or false"_f << Key);

				if (Key == "AlignTrailingComments")
					Result.m_bAlignTrailingComments = Value.f_Boolean();
				else if (Key == "AlignEnumValues")
					Result.m_bAlignEnumValues = Value.f_Boolean();
				else
					Result.m_bTrailingScopeAtStatementLevel = Value.f_Boolean();

				continue;
			}

			umint iList = 0;
			while (iList < umint(ECodeNamingList::mc_Count) && Key != gc_NamingLists[iList].m_pKey)
				++iList;

			if (iList == umint(ECodeNamingList::mc_Count))
				DMibError("Unknown key '{}'"_f << Key);

			auto &Change = Result.m_Lists[iList].f_CreateNew();
			auto const *pNames = &Value;

			if (Value.f_IsObject())
			{
				for (auto &Operation : Value.f_Object())
				{
					if (Operation.f_Name() != "Append")
						DMibError("Unknown key '{}' in {}; a list is a sequence or a mapping holding Append"_f << Operation.f_Name() << Key);
				}

				pNames = Value.f_GetMember("Append");
				if (!pNames)
					DMibError("{} must hold Append when it is a mapping"_f << Key);

				Change.m_bAppend = true;
			}

			if (!pNames->f_IsArray())
				DMibError("{} must be a list, or a mapping holding Append with a list"_f << Key);

			for (auto const &Entry : pNames->f_Array())
			{
				if (!Entry.f_IsString() || Entry.f_String().f_IsEmpty())
					DMibError("{} holds only non-empty strings"_f << Key);

				Change.m_Names.f_Insert(Entry.f_String());
			}
		}

		if (!bVersion)
			DMibError("Version is required");

		return Result;
	}

	// A list the document holds replaces or adds to the one it extends, which is applied first.
	void CCodeFormattingStyle::f_Apply(CCodeFormattingStyleDocument const &_Document)
	{
		if (_Document.m_bAlignTrailingComments)
			m_bAlignTrailingComments = *_Document.m_bAlignTrailingComments;

		if (_Document.m_bAlignEnumValues)
			m_bAlignEnumValues = *_Document.m_bAlignEnumValues;

		if (_Document.m_bTrailingScopeAtStatementLevel)
			m_bTrailingScopeAtStatementLevel = *_Document.m_bTrailingScopeAtStatementLevel;

		if (_Document.m_nAlignTrailingCommentsMaximumColumn)
			m_nAlignTrailingCommentsMaximumColumn = *_Document.m_nAlignTrailingCommentsMaximumColumn;

		for (umint iList = 0; iList < umint(ECodeNamingList::mc_Count); ++iList)
		{
			if (!_Document.m_Lists[iList])
				continue;

			auto const &Change = *_Document.m_Lists[iList];
			if (Change.m_bAppend)
				mp_Lists[iList].f_Insert(Change.m_Names);
			else
				mp_Lists[iList] = Change.m_Names;
		}

		fp_Compile();
	}

	// A pattern is anchored at both ends and spells literal characters, '.' for any
	// character, a class such as '[A-Z0-9_]', and '*' repeating the atom in front of it.
	// One without any of those is a name, looked up exactly. A character is a code point.
	void CCodeFormattingStyle::fp_Compile()
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
				{
					auto Character = pEntry[i];
					bPattern |= Character == '.' || Character == '[' || Character == '*';
				}

				if (!bPattern)
				{
					auto &Exact = mp_Exact.f_Insert();
					Exact.m_Name = Entry;
					Exact.m_Role = Role;

					continue;
				}

				CPattern Pattern;
				Pattern.m_Role = Role;

				TCVector<uch32> CodePoints;
				for (umint iByte = 0; iByte < nEntry;)
					CodePoints.f_Insert(fg_NextCodePoint(pEntry, nEntry, iByte));

				auto pCodePoints = CodePoints.f_GetArray();
				auto nCodePoints = CodePoints.f_GetLen();
				for (umint i = 0; i < nCodePoints;)
				{
					auto Character = pCodePoints[i];
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
					while (i < nCodePoints && pCodePoints[i] != ']')
					{
						auto Low = pCodePoints[i];
						CRange Range{Low, Low};
						if (i + 2 < nCodePoints && pCodePoints[i + 1] == '-' && pCodePoints[i + 2] != ']')
						{
							Range.m_High = pCodePoints[i + 2];
							if (Range.m_High < Low)
								DMibError("Reversed range in pattern '{}'"_f << Entry);

							i += 3;
						}
						else
							++i;

						Atom.m_Ranges.f_Insert(Range);
					}

					if (i >= nCodePoints || Atom.m_Ranges.f_IsEmpty())
						DMibError("Unterminated or empty class in pattern '{}'"_f << Entry);

					++i;
				}

				// The matcher follows every place in the pattern at once as a set of bits.
				if (Pattern.m_Atoms.f_GetLen() >= 64)
					DMibError("Pattern '{}' has more than 63 parts"_f << Entry);

				auto const &Last = Pattern.m_Atoms.f_GetLast();
				Pattern.m_bPrefixThenAny = Last.m_bAny && Last.m_bRepeat;
				for (umint iAtom = 0; iAtom + 1 < Pattern.m_Atoms.f_GetLen(); ++iAtom)
					Pattern.m_bPrefixThenAny = Pattern.m_bPrefixThenAny && !Pattern.m_Atoms[iAtom].m_bRepeat;

				mp_Patterns.f_Insert(fg_Move(Pattern));
			}
		}

		for (auto &Mask : mp_PatternsByFirst)
			Mask = 0;

		// A first byte past ASCII starts any code point past it, which only the full match tells apart.
		for (umint iPattern = 0; iPattern < mp_Patterns.f_GetLen() && iPattern < 64; ++iPattern)
		{
			auto const &Pattern = mp_Patterns[iPattern];
			bool bNonAscii = false;
			for (auto const &Atom : Pattern.m_Atoms)
			{
				bool bAccepts = Atom.m_bAny;
				for (auto const &Range : Atom.m_Ranges)
					bAccepts = bAccepts || Range.m_High >= 0x80;

				bNonAscii = bNonAscii || bAccepts;
				if (!Atom.m_bRepeat)
					break;
			}

			for (umint Byte = 0; Byte < 256; ++Byte)
			{
				if (Byte < 0x80 ? fsp_CanStartWith(Pattern, uch32(Byte)) : bNonAscii)
					mp_PatternsByFirst[Byte] |= uint64(1) << iPattern;
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

	bool CCodeFormattingStyle::fsp_Accepts(CAtom const &_Atom, uch32 _Character)
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
	bool CCodeFormattingStyle::fsp_CanStartWith(CPattern const &_Pattern, uch32 _Character)
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

	// Follows every place the pattern can be at, after each character of the name, as bits: a place is the atom matched
	// next, and a repeat may also be passed by. Time is linear in the name, however the repeats can share its characters.
	bool CCodeFormattingStyle::fsp_Matches(CPattern const &_Pattern, umint _iAtom, ch8 const *_pName, umint _nLength)
	{
		auto const &Atoms = _Pattern.m_Atoms;
		auto nAtoms = Atoms.f_GetLen();

		// The shape every pattern of the tree has, matched by its leading characters alone, as the naming is asked for every name.
		if (_Pattern.m_bPrefixThenAny && !_iAtom)
		{
			umint iByte = 0;
			for (umint iAtom = 0; iAtom + 1 < nAtoms; ++iAtom)
			{
				if (iByte >= _nLength)
					return false;

				uch32 Character = uch8(_pName[iByte]);
				if (Character < 0x80)
					++iByte;
				else
					Character = fg_NextCodePoint(_pName, _nLength, iByte);

				if (!fsp_Accepts(Atoms[iAtom], Character))
					return false;
			}

			return true;
		}

		auto fClosure = [&](uint64 _Places)
			{
				for (umint iAtom = 0; iAtom < nAtoms; ++iAtom)
				{
					if ((_Places & (uint64(1) << iAtom)) && Atoms[iAtom].m_bRepeat)
						_Places |= uint64(1) << (iAtom + 1);
				}

				return _Places;
			}
		;

		auto Places = fClosure(uint64(1) << _iAtom);
		for (umint iByte = 0; iByte < _nLength && Places;)
		{
			uch32 Character = uch8(_pName[iByte]);
			if (Character < 0x80)
				++iByte;
			else
				Character = fg_NextCodePoint(_pName, _nLength, iByte);

			uint64 Next = 0;
			for (umint iAtom = 0; iAtom < nAtoms; ++iAtom)
			{
				if (!(Places & (uint64(1) << iAtom)) || !fsp_Accepts(Atoms[iAtom], Character))
					continue;

				Next |= uint64(1) << (Atoms[iAtom].m_bRepeat ? iAtom : iAtom + 1);
			}

			Places = fClosure(Next);
		}

		return (Places & (uint64(1) << nAtoms)) != 0;
	}

	ECodeNameRole CCodeFormattingStyle::f_GetRoles(ch8 const *_pName, umint _nLength) const
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
