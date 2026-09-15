// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Develop_EditorConfig.h"

#include <Mib/File/PathGlob>

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

			CPathGlob m_Glob;
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
					Key == "root"
					|| Key == "indent_style"
					|| Key == "indent_size"
					|| Key == "tab_width"
					|| Key == "end_of_line"
					|| Key == "charset"
					|| Key == "trim_trailing_whitespace"
					|| Key == "insert_final_newline"
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

		static bool fs_IsUnset(CStr const &_Value)
		{
			if (_Value.f_GetLen() != 5)
				return false;

			auto pValue = _Value.f_GetStr();
			for (auto pExpected = "unset"; *pExpected; ++pExpected, ++pValue)
			{
				if (*pValue != *pExpected && *pValue != *pExpected - ('a' - 'A'))
					return false;
			}

			return true;
		}

		void f_Apply(CStr const &_RelativePath, TCMap<CStr, CStr> &o_Properties) const
		{
			if (m_bRoot)
				o_Properties.f_Clear();

			auto Path = CPathGlob::fs_ToUnicode(_RelativePath);
			auto FileName = CPathGlob::fs_ToUnicode(CFile::fs_GetFile(_RelativePath));
			CPathGlob::CScratch Scratch;
			for (auto const &Section : m_Sections)
			{
				if (!Section.m_Glob.f_Match(Path, FileName, Scratch))
					continue;

				for (auto const &Property : Section.m_Properties.f_Entries())
				{
					if (fs_IsUnset(Property.f_Value()))
						o_Properties.f_Remove(Property.f_Key());
					else
						o_Properties[Property.f_Key()] = Property.f_Value();
				}
			}
		}

		// A section that covers every file under the directory settles its properties for all
		// of them; one that may cover some of them leaves a property uncertain unless it would
		// give it the value already settled, as the exclusions nested under an excluded tree
		// do. A root document starts over for everything below it.
		void f_ApplyBelow(CStr const &_RelativeDirectory, TCMap<CStr, CStr> &o_Properties, TCSet<CStr> &o_Settled, TCSet<CStr> &o_Uncertain) const
		{
			if (m_bRoot)
			{
				o_Properties.f_Clear();
				o_Settled.f_Clear();
				o_Uncertain.f_Clear();
			}

			auto Directory = CPathGlob::fs_ToUnicode(_RelativeDirectory == "." ? CStr() : _RelativeDirectory);
			CPathGlob::CScratch Scratch;
			for (auto const &Section : m_Sections)
			{
				auto Cover = Section.m_Glob.f_MatchBelow(Directory, Scratch);
				if (Cover == EPathGlobCover::mc_None)
					continue;

				for (auto const &Property : Section.m_Properties.f_Entries())
				{
					if (Cover == EPathGlobCover::mc_Some)
					{
						if (!fs_IsSettledTo(Property.f_Key(), Property.f_Value(), o_Properties, o_Settled))
							o_Uncertain.f_Insert(Property.f_Key());

						continue;
					}

					o_Uncertain.f_Remove(Property.f_Key());
					o_Settled.f_Insert(Property.f_Key());
					if (fs_IsUnset(Property.f_Value()))
						o_Properties.f_Remove(Property.f_Key());
					else
						o_Properties[Property.f_Key()] = Property.f_Value();
				}
			}
		}

		static bool fs_IsSettledTo(CStr const &_Key, CStr const &_Value, TCMap<CStr, CStr> const &_Properties, TCSet<CStr> const &_Settled)
		{
			if (!_Settled.f_FindEqual(_Key))
				return false;

			auto pCurrent = _Properties.f_FindEqual(_Key);
			if (fs_IsUnset(_Value))
				return !pCurrent;

			return pCurrent && *pCurrent == _Value;
		}

		bool m_bRoot = false;
		TCVector<CSection> m_Sections;
	};

	// The documents that apply to the files of one directory, outermost first.
	struct CEditorConfigChainEntry
	{
		CStr m_Directory;
		CEditorConfig m_Configuration;
	};

	struct CEditorConfigChain : TCVector<CEditorConfigChainEntry>
	{
	};

	struct CEditorConfigCache
	{
		struct CEntry
		{
			TCAsyncResult<TCOptional<CEditorConfig>> m_Result;
			TCVector<TCPromise<TCOptional<CEditorConfig>>> m_Waiters;
		};

		TCMap<CStr, TCSharedPointer<CEntry>> m_Entries;
		TCMap<CStr, TCSharedPointer<CEditorConfigChain>> m_Chains;
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

	void CEditorConfig::f_ApplyBelow(CStr const &_RelativeDirectory, CEditorConfigSubtreeProperties &o_Properties) const
	{
		mp_pData->f_ApplyBelow(_RelativeDirectory, o_Properties.m_Properties, o_Properties.m_Settled, o_Properties.m_Uncertain);
	}

	CEditorConfigResolver::CEditorConfigResolver(CStr const &_Boundary, TCSharedPointer<CSharedRoundRobinBlockingActors> const &_pBlockingActors)
		: mp_Boundary(_Boundary ? CFile::fs_GetFullPath(_Boundary, CFile::fs_GetCurrentDirectory()) : CStr())
		, mp_pCache(fg_Construct())
		, mp_pBlockingActors(_pBlockingActors)
	{
		if (!mp_pBlockingActors)
			mp_pBlockingActors = fg_Construct(umint(1));
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
			Contents = co_await
				(
					g_Dispatch(mp_pBlockingActors->f_Next()) / [Path = fg_Move(_Path)]() -> TCOptional<CStr>
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

	auto CEditorConfigResolver::fp_GetConfiguration(TCSharedPointer<NPrivate::CEditorConfigCache> _pCache, CStr _Path) -> TCFuture<TCOptional<CEditorConfig>>
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

	// A path is resolved against the documents in its own directory and above it, within the
	// boundary. Empty when the path lies outside the boundary; the boundary itself is a
	// directory to judge, never a file to resolve.
	CStr CEditorConfigResolver::fp_GetBoundedPath(CStr const &_Path, bool _bDirectory) const
	{
		CStr Path = CFile::fs_IsPathAbsolute(_Path) ? _Path : CFile::fs_GetFullPath(_Path, CFile::fs_GetCurrentDirectory());
		if (!mp_Boundary)
			return Path;

		auto Relative = CFile::fs_MakePathRelative(Path, mp_Boundary);
		bool bBoundary = !Relative || Relative == ".";
		if ((bBoundary && !_bDirectory) || CFile::fs_IsPathAbsolute(Relative) || Relative == ".." || Relative.f_StartsWith("../") || Relative.f_StartsWith("..\\"))
			return {};

		return bBoundary ? mp_Boundary : Path;
	}

	// The documents that apply to the files of one directory, outermost first. A module
	// holds many files per directory, so the chain is walked once per directory.
	auto CEditorConfigResolver::fp_GetChain(TCSharedPointer<NPrivate::CEditorConfigCache> _pCache, CStr _Directory) -> TCFuture<TCSharedPointer<NPrivate::CEditorConfigChain>>
	{
		if (auto pCached = _pCache->m_Chains.f_FindEqual(_Directory))
			co_return *pCached;

		TCSharedPointer<NPrivate::CEditorConfigChain> pChain = fg_Construct();
		auto Directory = _Directory;
		while (Directory)
		{
			auto ConfigurationPath = Directory / ".editorconfig";
			auto Configuration = co_await fp_GetConfiguration(_pCache, fg_Move(ConfigurationPath));
			if (Configuration)
			{
				pChain->f_Insert({Directory, *Configuration});
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

		// The cache may have been replaced while the chain was loading.
		if (_pCache == mp_pCache)
			_pCache->m_Chains(_Directory, pChain);

		co_return pChain;
	}

	TCFuture<CEditorConfigProperties> CEditorConfigResolver::f_Resolve(CStr _FilePath)
	{
		auto Capture = co_await (g_CaptureExceptions % "Resolving EditorConfig properties");
		auto pCache = mp_pCache;
		auto FilePath = fp_GetBoundedPath(_FilePath, false);
		if (!FilePath)
			co_return DMibErrorInstance("EditorConfig path '{}' is outside boundary '{}'"_f << _FilePath << mp_Boundary);

		auto pChain = co_await fp_GetChain(pCache, CFile::fs_GetPath(FilePath));
		CEditorConfigProperties Properties;
		for (umint i = pChain->f_GetLen(); i; --i)
		{
			auto const &Entry = (*pChain)[i - 1];
			Entry.m_Configuration.f_Apply(CFile::fs_MakePathRelative(FilePath, Entry.m_Directory), Properties);
		}

		co_return Properties;
	}

	TCFuture<CEditorConfigSubtreeProperties> CEditorConfigResolver::f_ResolveBelow(CStr _Directory)
	{
		auto Capture = co_await (g_CaptureExceptions % "Resolving EditorConfig properties below a directory");
		auto pCache = mp_pCache;
		auto Directory = fp_GetBoundedPath(_Directory, true);
		if (!Directory)
			co_return DMibErrorInstance("EditorConfig path '{}' is outside boundary '{}'"_f << _Directory << mp_Boundary);

		// The documents in the directory itself count: they reach every file below it.
		auto pChain = co_await fp_GetChain(pCache, Directory);
		CEditorConfigSubtreeProperties Properties;
		for (umint i = pChain->f_GetLen(); i; --i)
		{
			auto const &Entry = (*pChain)[i - 1];
			Entry.m_Configuration.f_ApplyBelow(CFile::fs_MakePathRelative(Directory, Entry.m_Directory), Properties);
		}

		co_return Properties;
	}

	void CEditorConfigResolver::f_ClearCache()
	{
		mp_pCache = fg_Construct();
	}
}
