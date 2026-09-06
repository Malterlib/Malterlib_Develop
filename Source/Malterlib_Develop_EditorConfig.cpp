// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_EditorConfig.h"

#include <Mib/File/File>
#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NDevelop
{
	using namespace NStr;
	using namespace NContainer;
	using namespace NStorage;
	using namespace NFile;
	using namespace NConcurrency;
}

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;

	bool fg_IsNumericGlobRange(ch8 const *_pStart, ch8 const *_pEnd)
	{
		auto pParse = _pStart;
		for (umint i = 0; i < 2; ++i)
		{
			if (pParse != _pEnd && (*pParse == '-' || *pParse == '+'))
				++pParse;

			auto pDigits = pParse;
			while (pParse != _pEnd && *pParse >= '0' && *pParse <= '9')
				++pParse;

			if (pParse == pDigits)
				return false;

			if (i == 0)
			{
				if (_pEnd - pParse < 2 || *pParse != '.' || *(pParse + 1) != '.')
					return false;

				pParse += 2;
			}
		}

		return pParse == _pEnd;
	}

	CUStr fg_GetGlobUnicode(CStr const &_Value)
	{
		for (auto pParse = _Value.f_GetStr(); pParse != _Value.f_GetStr() + _Value.f_GetLen(); ++pParse)
		{
			if (*pParse == 0)
				DMibError("NUL is not allowed in EditorConfig patterns or file paths");
		}

		return CUStr(_Value);
	}

	struct CGlobMatcher
	{
		explicit CGlobMatcher(CStr const &_Pattern)
		{
			mp_bHasSeparator = _Pattern.f_StartsWith("/");
			auto Pattern = fg_GetGlobUnicode(_Pattern.f_RemovePrefix("/"));
			if (Pattern.f_GetLen() > 1024)
				DMibError("EditorConfig section patterns longer than 1024 characters are not supported");

			mp_Nodes.f_Insert().m_Kind = EKind::mc_Accept;
			mp_iStart = fp_Compile(Pattern.f_GetStr(), Pattern.f_GetStr() + Pattern.f_GetLen(), 0);
		}

		bool f_Match(CStr const &_Path) const
		{
			auto Path = fg_GetGlobUnicode(mp_bHasSeparator ? _Path : CFile::fs_GetFile(_Path));
			TCVector<umint> Active, Next;
			TCVector<uint8> Visited;
			Visited.f_SetLen(mp_Nodes.f_GetLen());
			for (auto &Value : Visited)
				Value = 0;
			fp_AddStates(mp_iStart, true, Active, Visited);

			auto pEnd = Path.f_GetStr() + Path.f_GetLen();
			for (auto pParse = Path.f_GetStr(); pParse != pEnd; ++pParse)
			{
				Next.f_Clear();
				for (auto &Value : Visited)
					Value = 0;

				for (auto iState : Active)
				{
					auto const &Node = mp_Nodes[iState];
					bool bMatches = false;
					switch (Node.m_Kind)
					{
						case EKind::mc_Literal:
							bMatches = *pParse == Node.m_Character;

							break;
						case EKind::mc_Any:
						case EKind::mc_Star:
							bMatches = *pParse != '/';

							break;
						case EKind::mc_RecursiveStar:
							bMatches = true;

							break;
						case EKind::mc_Set:
						{
							for (auto const &Range : Node.m_Ranges)
								bMatches |= *pParse >= Range.m_First && *pParse <= Range.m_Last;
							bMatches = *pParse != '/' && bMatches != Node.m_bNegated;

							break;
						}
						default: break;
					}

					if (bMatches)
					{
						bool bRepeat = Node.m_Kind == EKind::mc_Star || Node.m_Kind == EKind::mc_RecursiveStar;
						fp_AddStates(bRepeat ? iState : Node.m_iNext, *pParse == '/', Next, Visited);
					}
				}

				Active = fg_Move(Next);
				if (Active.f_IsEmpty())
					return false;
			}

			for (auto iState : Active)
			{
				if (mp_Nodes[iState].m_Kind == EKind::mc_Accept)
					return true;
			}

			return false;
		}

	private:
		enum struct EKind
		{
			mc_Accept
			, mc_Empty
			, mc_Branch
			, mc_Literal
			, mc_Any
			, mc_Star
			, mc_RecursiveStar
			, mc_Set
		};

		struct CRange
		{
			ch32 m_First = 0;
			ch32 m_Last = 0;
		};

		struct CNode
		{
			EKind m_Kind = EKind::mc_Literal;
			umint m_iNext = 0;
			ch32 m_Character = 0;
			bool m_bNegated = false;
			TCVector<CRange> m_Ranges;
			TCVector<umint> m_Alternatives;
		};

		static ch32 const *fsp_SetEnd(ch32 const *_pStart, ch32 const *_pEnd)
		{
			auto pParse = _pStart + 1;
			while (pParse != _pEnd && *pParse != ']')
			{
				if (*pParse == '\\' && pParse + 1 != _pEnd)
					++pParse;

				++pParse;
			}

			return pParse;
		}

		umint fp_Compile(ch32 const *_pStart, ch32 const *_pEnd, umint _iNext)
		{
			TCVector<umint> Sequence;
			for (auto pParse = _pStart; pParse != _pEnd; ++pParse)
			{
				CNode Node;
				Node.m_Character = *pParse;
				if (*pParse == '\\' && pParse + 1 != _pEnd)
					Node.m_Character = *++pParse;
				else if (*pParse == '*')
				{
					Node.m_Kind = EKind::mc_Star;
					if (pParse + 1 != _pEnd && *(pParse + 1) == '*')
					{
						Node.m_Kind = EKind::mc_RecursiveStar;
						++pParse;
					}
				}
				else if (*pParse == '?')
					Node.m_Kind = EKind::mc_Any;
				else if (*pParse == '[' && fsp_SetEnd(pParse, _pEnd) != _pEnd)
				{
					Node.m_Kind = EKind::mc_Set;
					auto pEnd = fsp_SetEnd(pParse, _pEnd);
					++pParse;
					Node.m_bNegated = *pParse == '!';
					if (Node.m_bNegated)
						++pParse;

					while (pParse != pEnd)
					{
						auto First = *pParse++;
						if (First == '\\' && pParse != pEnd)
							First = *pParse++;
						auto Last = First;
						if (pEnd - pParse >= 2 && *pParse == '-')
						{
							++pParse;
							Last = *pParse++;
							if (Last == '\\' && pParse != pEnd)
								Last = *pParse++;
						}

						Node.m_Ranges.f_Insert({First, Last});
					}
				}
				else if (*pParse == '{')
				{
					umint nDepth = 1;
					TCVector<ch32 const *> Separators;
					auto pEnd = pParse + 1;
					for (; pEnd != _pEnd; ++pEnd)
					{
						if (*pEnd == '\\' && pEnd + 1 != _pEnd)
							++pEnd;
						else if (*pEnd == '[' && fsp_SetEnd(pEnd, _pEnd) != _pEnd)
							pEnd = fsp_SetEnd(pEnd, _pEnd);
						else if (*pEnd == '{')
							++nDepth;
						else if (*pEnd == '}' && --nDepth == 0)
							break;
						else if (*pEnd == ',' && nDepth == 1)
							Separators.f_Insert(pEnd);
					}

					if (pEnd != _pEnd && !Separators.f_IsEmpty())
					{
						auto iJoin = mp_Nodes.f_GetLen();
						mp_Nodes.f_Insert().m_Kind = EKind::mc_Empty;
						Node.m_Kind = EKind::mc_Branch;
						Separators.f_Insert(pEnd);
						auto pAlternative = pParse + 1;
						for (auto pSeparator : Separators)
						{
							Node.m_Alternatives.f_Insert(fp_Compile(pAlternative, pSeparator, iJoin));
							pAlternative = pSeparator + 1;
						}

						Sequence.f_Insert(mp_Nodes.f_GetLen());
						mp_Nodes.f_Insert(fg_Move(Node));
						Sequence.f_Insert(iJoin);
						pParse = pEnd;

						continue;
					}

					if (pEnd != _pEnd)
					{
						CStr Range(CUStr(pParse + 1, pEnd - pParse - 1));
						if (fg_IsNumericGlobRange(Range.f_GetStr(), Range.f_GetStr() + Range.f_GetLen()))
							DMibError("Numeric ranges in EditorConfig globs are not supported: {{{}}}"_f << Range);
					}
				}

				if (Node.m_Kind == EKind::mc_Literal && Node.m_Character == '/')
					mp_bHasSeparator = true;
				Sequence.f_Insert(mp_Nodes.f_GetLen());
				mp_Nodes.f_Insert(fg_Move(Node));
			}

			for (umint i = 0; i < Sequence.f_GetLen(); ++i)
				mp_Nodes[Sequence[i]].m_iNext = i + 1 == Sequence.f_GetLen() ? _iNext : Sequence[i + 1];

			return Sequence.f_IsEmpty() ? _iNext : Sequence[0];
		}

		void fp_AddStates(umint _iState, bool _bComponentStart, TCVector<umint> &o_States, TCVector<uint8> &o_Visited) const
		{
			struct CPending
			{
				umint m_iState = 0;
				bool m_bSkipSeparator = false;
			};

			TCVector<CPending> Pending = {{_iState, false}};
			while (!Pending.f_IsEmpty())
			{
				auto State = Pending.f_PopBack();
				auto iState = State.m_iState;
				uint8 Mask = State.m_bSkipSeparator ? 2 : 1;
				if (o_Visited[iState] & Mask)
					continue;
				o_Visited[iState] |= Mask;

				auto const &Node = mp_Nodes[iState];
				if (Node.m_Kind == EKind::mc_Branch)
				{
					for (auto iAlternative : Node.m_Alternatives)
						Pending.f_Insert({iAlternative, State.m_bSkipSeparator});
				}
				else if (Node.m_Kind == EKind::mc_Empty)
					Pending.f_Insert({Node.m_iNext, State.m_bSkipSeparator});
				else if (State.m_bSkipSeparator)
				{
					if (Node.m_Kind == EKind::mc_Literal && Node.m_Character == '/')
						Pending.f_Insert({Node.m_iNext, false});
				}
				else
				{
					o_States.f_Insert(iState);
					if (Node.m_Kind == EKind::mc_Star || Node.m_Kind == EKind::mc_RecursiveStar)
					{
						Pending.f_Insert({Node.m_iNext, false});
						if (Node.m_Kind == EKind::mc_RecursiveStar && _bComponentStart)
							Pending.f_Insert({Node.m_iNext, true});
					}
				}
			}
		}

		TCVector<CNode> mp_Nodes;
		umint mp_iStart = 0;
		bool mp_bHasSeparator = false;
	};
}

namespace NMib::NDevelop::NPrivate
{
	struct CEditorConfigData
	{
		struct CSection
		{
			explicit CSection(CStr const &_Pattern)
				: m_Glob(_Pattern)
			{
			}

			CGlobMatcher m_Glob;
			TCMap<CStr, CStr> m_Properties;
		};

		explicit CEditorConfigData(CStr const &_Contents)
		{
			for (auto const &ConfigLine : _Contents.f_SplitLine())
			{
				auto Line = ConfigLine.f_Trim();
				if (!Line || Line.f_StartsWith("#") || Line.f_StartsWith(";"))
					continue;

				if (Line.f_StartsWith("[") && Line.f_EndsWith("]"))
				{
					m_Sections.f_Insert(CSection(Line.f_Extract(1, Line.f_GetLen() - 2)));
					continue;
				}

				auto iEquals = Line.f_Find("=");
				if (iEquals < 0)
					DMibError("Invalid EditorConfig line: {}"_f << Line);

				auto Key = Line.f_Left(iEquals).f_Trim().f_LowerCase();
				auto Value = Line.f_Extract(iEquals + 1).f_Trim();
				if
				(
					Key == "root" || Key == "indent_style" || Key == "indent_size" || Key == "tab_width" || Key == "end_of_line"
					|| Key == "charset" || Key == "trim_trailing_whitespace" || Key == "insert_final_newline"
				)
				{
					Value = Value.f_LowerCase();
				}
				if (m_Sections.f_IsEmpty())
				{
					if (Key == "root")
						m_bRoot = Value == "true";
				}
				else
					m_Sections[m_Sections.f_GetLen() - 1].m_Properties[Key] = Value;
			}
		}

		void f_Apply(CStr const &_RelativePath, TCMap<CStr, CStr> &o_Properties) const
		{
			if (m_bRoot)
				o_Properties.f_Clear();

			for (auto const &Section : m_Sections)
			{
				if (!Section.m_Glob.f_Match(_RelativePath))
					continue;

				for (auto const &Property : Section.m_Properties.f_Entries())
				{
					if (Property.f_Value().f_LowerCase() == "unset")
						o_Properties.f_Remove(Property.f_Key());
					else
						o_Properties[Property.f_Key()] = Property.f_Value();
				}
			}
		}

		bool m_bRoot = false;
		TCVector<CSection> m_Sections;
	};

	struct CEditorConfigCache
	{
		struct CEntry
		{
			TCAsyncResult<TCOptional<CEditorConfig>> m_Result;
			TCVector<TCPromise<TCOptional<CEditorConfig>>> m_Waiters;
		};

		TCMap<CStr, TCSharedPointer<CEntry>> m_Entries;
	};

}

namespace NMib::NDevelop
{
	CEditorConfig::CEditorConfig(CStr const &_Contents)
		: mp_pData(fg_Construct(_Contents.f_RemovePrefix("\xEF\xBB\xBF")))
	{
	}

	CEditorConfig::~CEditorConfig() = default;

	bool CEditorConfig::f_IsRoot() const
	{
		return mp_pData->m_bRoot;
	}

	void CEditorConfig::f_Apply(CStr const &_RelativePath, CEditorConfigProperties &o_Properties) const
	{
		mp_pData->f_Apply(_RelativePath, o_Properties);
	}

	CEditorConfigResolver::CEditorConfigResolver(CStr const &_Boundary)
		: mp_Boundary(_Boundary ? CFile::fs_GetFullPath(_Boundary, CFile::fs_GetCurrentDirectory()) : CStr())
		, mp_pCache(fg_Construct())
	{
	}

	CEditorConfigResolver::CEditorConfigResolver(FEditorConfigLoader &&_fLoader, CStr const &_Boundary)
		: CEditorConfigResolver(_Boundary)
	{
		DMibRequire(bool(_fLoader));
		mp_fLoader = fg_Move(_fLoader);
	}

	CEditorConfigResolver::~CEditorConfigResolver() = default;

	TCFuture<TCOptional<CEditorConfig>> CEditorConfigResolver::fp_LoadConfiguration(CStr _Path)
	{
		auto Capture = co_await (g_CaptureExceptions % ("Loading EditorConfig '{}'"_f << _Path));
		TCOptional<CStr> Contents;
		if (mp_fLoader)
			Contents = co_await mp_fLoader(fg_Move(_Path));
		else
		{
			auto BlockingActor = fg_BlockingActor();
			Contents = co_await
				(
					g_Dispatch(BlockingActor) / [Path = fg_Move(_Path)]() -> TCOptional<CStr>
					{
						if (!CFile::fs_FileExists(Path))
							return {};

						return CFile::fs_ReadStringFromFile(Path, true);
					}
				)
			;
		}

		if (!Contents)
			co_return {};

		co_return CEditorConfig(*Contents);
	}

	auto CEditorConfigResolver::fp_GetConfiguration(TCSharedPointer<NPrivate::CEditorConfigCache> _pCache, CStr _Path)
		-> TCFuture<TCOptional<CEditorConfig>>
	{
		auto Capture = co_await (g_CaptureExceptions % "Reading cached EditorConfig");
		if (auto pCached = _pCache->m_Entries.f_FindEqual(_Path))
		{
			auto pEntry = *pCached;
			if (pEntry->m_Result.f_IsSet())
				co_return fg_TempCopy(pEntry->m_Result);

			co_return co_await pEntry->m_Waiters.f_Insert().f_Future();
		}

		// Publish the pending load before awaiting so reentrant resolves can join it.
		TCSharedPointer<NPrivate::CEditorConfigCache::CEntry> pEntry = fg_Construct();
		_pCache->m_Entries(_Path, pEntry);
		auto Result = co_await fp_LoadConfiguration(fg_TempCopy(_Path)).f_Wrap();
		pEntry->m_Result = Result;
		if (!Result)
			_pCache->m_Entries.f_Remove(_Path);

		auto Waiters = fg_Move(pEntry->m_Waiters);
		for (auto &Promise : Waiters)
			Promise.f_SetResult(Result);

		co_return fg_Move(Result);
	}

	TCFuture<CEditorConfigProperties> CEditorConfigResolver::f_Resolve(CStr _FilePath)
	{
		auto Capture = co_await (g_CaptureExceptions % "Resolving EditorConfig properties");
		auto pCache = mp_pCache;
		CStr FilePath = CFile::fs_GetFullPath(_FilePath, CFile::fs_GetCurrentDirectory());
		if (mp_Boundary)
		{
			auto Relative = CFile::fs_MakePathRelative(FilePath, mp_Boundary);
			if
			(
				!Relative || Relative == "." || CFile::fs_IsPathAbsolute(Relative)
				|| Relative == ".." || Relative.f_StartsWith("../") || Relative.f_StartsWith("..\\")
			)
			{
				co_return DMibErrorInstance("EditorConfig path '{}' is outside boundary '{}'"_f << FilePath << mp_Boundary);
			}
		}

		struct CEntry
		{
			CStr m_Directory;
			CEditorConfig m_Configuration;
		};

		TCVector<CEntry> Entries;
		auto Directory = CFile::fs_GetPath(FilePath);
		while (Directory)
		{
			auto ConfigurationPath = Directory / ".editorconfig";
			auto Configuration = co_await fp_GetConfiguration(pCache, fg_Move(ConfigurationPath));
			if (Configuration)
			{
				Entries.f_Insert({Directory, *Configuration});
				if (Configuration->f_IsRoot())
					break;
			}

			if (mp_Boundary)
			{
				auto Relative = CFile::fs_MakePathRelative(Directory, mp_Boundary);
				if (!Relative || Relative == ".")
					break;
			}

			auto Parent = CFile::fs_GetPath(Directory);
			if (Parent == Directory)
				break;
			Directory = fg_Move(Parent);
		}

		CEditorConfigProperties Properties;
		for (umint i = Entries.f_GetLen(); i; --i)
		{
			auto const &Entry = Entries[i - 1];
			Entry.m_Configuration.f_Apply(CFile::fs_MakePathRelative(FilePath, Entry.m_Directory), Properties);
		}

		co_return Properties;
	}

	void CEditorConfigResolver::f_ClearCache()
	{
		mp_pCache = fg_Construct();
	}
}
