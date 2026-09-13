// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Map>
#include <Mib/Concurrency/ActorFunctorWeak>
#include <Mib/Storage/Optional>
#include <Mib/Storage/SharedPointer>

namespace NMib::NDevelop::NPrivate
{
	struct CEditorConfigData;
	struct CEditorConfigCache;
}

DMibDefineSharedPointerType(NMib::NDevelop::NPrivate::CEditorConfigData, false, false);
DMibDefineSharedPointerType(NMib::NDevelop::NPrivate::CEditorConfigCache, false, false);

namespace NMib::NDevelop
{
	using CEditorConfigProperties = NContainer::TCMap<NStr::CStr, NStr::CStr>;
	using FEditorConfigLoader = NConcurrency::TCActorFunctorWeak<NConcurrency::TCFuture<NStorage::TCOptional<NStr::CStr>> (NStr::CStr _ConfigurationPath)>;

	struct CEditorConfig
	{
		explicit CEditorConfig(NStr::CStr const &_Contents);
		~CEditorConfig();

		bool f_IsRoot() const;
		void f_Apply(NStr::CStr const &_RelativePath, CEditorConfigProperties &o_Properties) const;

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
		void f_ClearCache();

	private:
		NConcurrency::TCFuture<NStorage::TCOptional<CEditorConfig>> fp_LoadConfiguration(NStr::CStr _Path);
		auto fp_GetConfiguration(NStorage::TCSharedPointer<NPrivate::CEditorConfigCache> _pCache, NStr::CStr _Path) -> NConcurrency::TCFuture<NStorage::TCOptional<CEditorConfig>>;

		FEditorConfigLoader mp_fLoader;
		NStr::CStr mp_Boundary;
		NStorage::TCSharedPointer<NPrivate::CEditorConfigCache> mp_pCache;
	};
}
