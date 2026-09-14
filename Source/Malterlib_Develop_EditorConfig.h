// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Map>
#include <Mib/Container/Set>
#include <Mib/Concurrency/ActorFunctorWeak>
#include <Mib/Storage/Optional>
#include <Mib/Storage/SharedPointer>

namespace NMib::NDevelop::NPrivate
{
	struct CEditorConfigData;
	struct CEditorConfigCache;
	struct CEditorConfigChain;
}

DMibDefineSharedPointerType(NMib::NDevelop::NPrivate::CEditorConfigData, false, false);
DMibDefineSharedPointerType(NMib::NDevelop::NPrivate::CEditorConfigCache, false, false);

namespace NMib::NDevelop
{
	using CEditorConfigProperties = NContainer::TCMap<NStr::CStr, NStr::CStr>;

	// What holds for every file under a directory: the properties each of them gets, the keys
	// a section covering all of them set or unset, and the keys a section that may match
	// some of them can still change. A key absent from every set is one no document above
	// speaks of, which a document deeper down may yet do.
	struct CEditorConfigSubtreeProperties
	{
		CEditorConfigProperties m_Properties;
		NContainer::TCSet<NStr::CStr> m_Settled;
		NContainer::TCSet<NStr::CStr> m_Uncertain;
	};
	using FEditorConfigLoader = NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<NStorage::TCOptional<NStr::CStr>> (NStr::CStr _ConfigurationPath)>;

	struct CEditorConfig
	{
		explicit CEditorConfig(NStr::CStr const &_Contents);
		~CEditorConfig();

		bool f_IsRoot() const;
		void f_Apply(NStr::CStr const &_RelativePath, CEditorConfigProperties &o_Properties) const;
		void f_ApplyBelow(NStr::CStr const &_RelativeDirectory, CEditorConfigSubtreeProperties &o_Properties) const;

	private:
		NStorage::TCSharedPointer<NPrivate::CEditorConfigData> mp_pData;
	};

	struct CEditorConfigResolver : NConcurrency::CActor
	{
		// An empty boundary permits discovery up to the filesystem root.
		explicit CEditorConfigResolver(NStr::CStr const &_Boundary = {});
		CEditorConfigResolver(FEditorConfigLoader &&_fLoader, NStr::CStr const &_Boundary = {});
		~CEditorConfigResolver() override;

		// Loaders receive absolute .editorconfig paths. Missing files return an empty optional.
		NConcurrency::TCFuture<CEditorConfigProperties> f_Resolve(NStr::CStr _FilePath);

		// What holds for every file under a directory, for a walk deciding whether to enter it.
		NConcurrency::TCFuture<CEditorConfigSubtreeProperties> f_ResolveBelow(NStr::CStr _Directory);
		void f_ClearCache();

	private:
		NConcurrency::TCFuture<NStorage::TCOptional<CEditorConfig>> fp_LoadConfiguration(NStr::CStr _Path);
		auto fp_GetConfiguration(NStorage::TCSharedPointer<NPrivate::CEditorConfigCache> _pCache, NStr::CStr _Path) -> NConcurrency::TCFuture<NStorage::TCOptional<CEditorConfig>>;
		auto fp_GetChain(NStorage::TCSharedPointer<NPrivate::CEditorConfigCache> _pCache, NStr::CStr _Directory)
			-> NConcurrency::TCFuture<NStorage::TCSharedPointer<NPrivate::CEditorConfigChain>>
		;
		NStr::CStr fp_GetBoundedPath(NStr::CStr const &_Path, bool _bDirectory) const;

		FEditorConfigLoader mp_fLoader;
		NStr::CStr mp_Boundary;
		NStorage::TCSharedPointer<NPrivate::CEditorConfigCache> mp_pCache;
		NConcurrency::CBlockingActorCheckout mp_BlockingActor;	// Loads queue on one thread; many resolves in flight must not each take one.
	};
}
