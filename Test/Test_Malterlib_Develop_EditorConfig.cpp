// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Develop/EditorConfig>
#include <Mib/File/File>
#include <Mib/Test/Test>
#include <Mib/Test/Exception>
#include <Mib/Concurrency/AsyncDestroy>

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NFile;
	using namespace NMib::NTest;
	using namespace NMib::NConcurrency;

	struct CEditorConfig_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Properties")
			{
				CEditorConfig Root
					(
						"\xEF\xBB\xBF# comment\n; another comment\nroot = TRUE\n[*]\n"
						"indent_style = SPACE\ncustom = KeepCase\nmax_line_length = 190\n"
						"[*.{cpp,h}]\ncustom = SourceValue\n[*.cpp]\nmax_line_length = 120\n"
					)
				;
				CEditorConfigProperties Properties = {{"inherited", "value"}};
				Root.f_Apply("src/main.cpp", Properties);

				DMibExpectTrue(Root.f_IsRoot());
				DMibExpect(Properties["indent_style"], ==, "space");
				DMibExpect(Properties["custom"], ==, "SourceValue");
				DMibExpect(Properties["max_line_length"], ==, "120");
				DMibExpectTrue(Properties.f_FindEqual("inherited") == nullptr);

				DMibTestPath("Child");
				CEditorConfig Child("[*.cpp]\ncustom = unset\nmax_line_length = unset\nanother = MixedCase\n");
				Child.f_Apply("main.cpp", Properties);

				DMibExpectFalse(Child.f_IsRoot());
				DMibExpectTrue(Properties.f_FindEqual("custom") == nullptr);
				DMibExpectTrue(Properties.f_FindEqual("max_line_length") == nullptr);
				DMibExpect(Properties["another"], ==, "MixedCase");
				DMibExpect(Properties["indent_style"], ==, "space");

				DMibTestCategory("Malformed")
				{
					DMibExpectExceptionType(CEditorConfig("invalid line\n"), NException::CException);
					DMibExpectExceptionType(CEditorConfig("[file{1..10}.cpp]\ncustom = value\n"), NException::CException);
				};
			};

			DMibTestSuite("Resolution") -> TCFuture<void>
			{
				auto Capture = co_await (g_CaptureExceptions % "Testing asynchronous EditorConfig resolution");
				DMibTestCategory("CustomLoader") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing CustomLoader resolution");
					CStr RootPath = CFile::fs_GetProgramDirectory() / "DevelopVirtualProject";
					TCMap<CStr, CStr> Files =
						{
							{RootPath / ".editorconfig", "root = true\n[*]\ncustom = Parent\nindent_style = TAB\n"}
							, {RootPath / "src/.editorconfig", "[*.cpp]\ncustom = Child\nindent_size = 2\n"}
						}
					;
					TCVector<CStr> Loaded;
					TCActor<CEditorConfigResolver> Resolver = fg_Construct
						(
							g_ActorFunctorWeak / [&](CStr _Path) -> TCFuture<TCOptional<CStr>>
							{
								auto Capture = co_await (g_CaptureExceptions % "Loading test configuration");
								Loaded.f_Insert(_Path);
								if (auto pContents = Files.f_FindEqual(_Path))
									co_return *pContents;

								co_return {};
							}
							, RootPath
						)
					;
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
					auto Properties = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "src/nested/main.cpp");

					DMibExpect(Properties["custom"], ==, "Child");
					DMibExpect(Properties["indent_style"], ==, "tab");
					DMibExpect(Properties["indent_size"], ==, "2");
					DMibExpect(Loaded.f_GetLen(), ==, 3);

					Files[RootPath / "src/.editorconfig"] = "root = true\n[*]\ncustom = Updated\n";
					{
						DMibTestPath("Cached");
						auto Cached = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "src/nested/other.cpp");

						DMibExpect(Cached["custom"], ==, "Child");
						DMibExpect(Loaded.f_GetLen(), ==, 3);
					}

					co_await Resolver(&CEditorConfigResolver::f_ClearCache);
					DMibTestPath("Reloaded");
					auto Updated = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "src/nested/other.cpp");

					DMibExpect(Updated["custom"], ==, "Updated");
					DMibExpectTrue(Updated.f_FindEqual("indent_style") == nullptr);
					DMibExpect(Loaded.f_GetLen(), ==, 5);
					auto Outside = co_await Resolver(&CEditorConfigResolver::f_Resolve, CFile::fs_GetPath(RootPath) / "outside.cpp").f_Wrap();
					DMibExpectFalse(Outside);

					co_return {};
				};

				// A directory is judged for every file below it: a section that covers all of
				// them settles a property, one that may cover some leaves it uncertain.
				DMibTestCategory("Below") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Below");

					TCMap<CStr, CStr> Files;
					Files["/repo/.editorconfig"] = "root = true\n[*.cpp]\nmalterlib_format = malterlib\n[**/Cache/**]\nmalterlib_format = unset\n"
						"[**/Cache/**.keep]\nmalterlib_format = malterlib\n[**/Build/**]\nmalterlib_format = unset\n"
					;
					Files["/repo/src/.editorconfig"] = "[*]\ncustom = value\n";
					TCActor<CEditorConfigResolver> Resolver = fg_Construct
						(
							g_ActorFunctorWeak / [Files](CStr _Path) -> TCFuture<TCOptional<CStr>>
							{
								if (auto pContents = Files.f_FindEqual(_Path))
									co_return *pContents;

								co_return {};
							}
							, "/repo"
						)
					;
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);

					auto Source = co_await Resolver(&CEditorConfigResolver::f_ResolveBelow, "/repo/src");
					DMibExpectTrue(Source.m_Properties.f_FindEqual("malterlib_format") == nullptr);
					DMibExpectTrue(Source.m_Uncertain.f_FindEqual("malterlib_format") != nullptr);
					DMibExpect(Source.m_Properties["custom"], ==, "value");

					auto Cache = co_await Resolver(&CEditorConfigResolver::f_ResolveBelow, "/repo/src/Cache");
					DMibExpectTrue(Cache.m_Properties.f_FindEqual("malterlib_format") == nullptr);
					// The later section may opt some files back in.
					DMibExpectTrue(Cache.m_Uncertain.f_FindEqual("malterlib_format") != nullptr);

					// Unset for everything below, with nothing that may opt files back in.
					auto Build = co_await Resolver(&CEditorConfigResolver::f_ResolveBelow, "/repo/src/Build");
					DMibExpectTrue(Build.m_Properties.f_FindEqual("malterlib_format") == nullptr);
					DMibExpectTrue(Build.m_Settled.f_FindEqual("malterlib_format") != nullptr);
					DMibExpectTrue(Build.m_Uncertain.f_FindEqual("malterlib_format") == nullptr);

					auto Root = co_await Resolver(&CEditorConfigResolver::f_ResolveBelow, "/repo");
					DMibExpectTrue(Root.m_Uncertain.f_FindEqual("malterlib_format") != nullptr);
					DMibExpectTrue(Root.m_Properties.f_FindEqual("custom") == nullptr);
					// No document speaks of this key at all, so nothing is known about it.
					DMibExpectTrue(Root.m_Settled.f_FindEqual("other") == nullptr);
					DMibExpectTrue(Root.m_Uncertain.f_FindEqual("other") == nullptr);

					co_return {};
				};

				DMibTestCategory("Boundary") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing Boundary resolution");
					CStr RootPath = CFile::fs_GetProgramDirectory() / "DevelopBoundaryProject";
					TCVector<CStr> Loaded;
					TCActor<CEditorConfigResolver> Resolver = fg_Construct
						(
							g_ActorFunctorWeak / [&](CStr _Path) -> TCFuture<TCOptional<CStr>>
							{
								Loaded.f_Insert(_Path);
								co_return {};
							}
							, RootPath
						)
					;
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
					auto Properties = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "file.cpp");

					DMibExpectTrue(Properties.f_IsEmpty());
					DMibExpect(Loaded.f_GetLen(), ==, 1);
					DMibExpect(Loaded[0], ==, RootPath / ".editorconfig");
					auto Outside = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath).f_Wrap();
					DMibExpectFalse(Outside);

					co_return {};
				};

				DMibTestCategory("ReentrantInvalidation") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing pending loads across cache invalidation");
					CStr RootPath = CFile::fs_GetProgramDirectory() / "DevelopReentrantProject";
					TCPromiseFuturePair<void> Started;
					TCPromiseFuturePair<void> Release;
					umint nLoads = 0;
					TCActor<CEditorConfigResolver> Resolver = fg_Construct
						(
							g_ActorFunctorWeak / [&](CStr _Path) -> TCFuture<TCOptional<CStr>>
							{
								auto Capture = co_await (g_CaptureExceptions % "Loading reentrant test configuration");
								if (++nLoads == 1)
								{
									Started.m_Promise.f_SetResult();
									co_await fg_Move(Release.m_Future);

									co_return "root = true\n[*]\ncustom = Old\n";
								}

								co_return "root = true\n[*]\ncustom = New\n";
							}
							, RootPath
						)
					;
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
					auto ReleasePending = co_await fg_AsyncDestroy
						(
							[&Release]() -> TCFuture<void>
							{
								if (!Release.m_Promise.f_IsSet())
									Release.m_Promise.f_SetResult();

								co_return {};
							}
						)
					;
					auto First = Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "first.cpp").f_Call();
					co_await fg_Move(Started.m_Future);

					auto Second = Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "second.cpp").f_Call();
					co_await Resolver(&CEditorConfigResolver::f_ClearCache);
					auto New = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "new.cpp");

					DMibExpect(New["custom"], ==, "New");
					DMibExpect(nLoads, ==, 2);

					Release.m_Promise.f_SetResult();
					auto FirstProperties = co_await fg_Move(First);
					auto SecondProperties = co_await fg_Move(Second);
					auto Cached = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "cached.cpp");

					DMibExpect(FirstProperties["custom"], ==, "Old");
					DMibExpect(SecondProperties["custom"], ==, "Old");
					DMibExpect(Cached["custom"], ==, "New");

					co_return {};
				};

				DMibTestCategory("FailedLoadRetry") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing shared load failures and retry");
					CStr RootPath = CFile::fs_GetProgramDirectory() / "DevelopRetryProject";
					TCPromiseFuturePair<void> Started;
					TCPromiseFuturePair<void> Release;
					umint nLoads = 0;
					TCActor<CEditorConfigResolver> Resolver = fg_Construct
						(
							g_ActorFunctorWeak / [&](CStr _Path) -> TCFuture<TCOptional<CStr>>
							{
								if (++nLoads == 1)
								{
									Started.m_Promise.f_SetResult();
									co_await fg_Move(Release.m_Future);

									co_return DMibErrorInstance("Test load failure");
								}

								co_return "root = true\n[*]\ncustom = Recovered\n";
							}
							, RootPath
						)
					;
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
					auto ReleasePending = co_await fg_AsyncDestroy
						(
							[&Release]() -> TCFuture<void>
							{
								if (!Release.m_Promise.f_IsSet())
									Release.m_Promise.f_SetResult();

								co_return {};
							}
						)
					;
					auto First = Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "first.cpp").f_Call();
					co_await fg_Move(Started.m_Future);
					auto Second = Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "second.cpp").f_Call();
					co_await (g_Dispatch(Resolver) / [] {});
					Release.m_Promise.f_SetResult();

					auto FirstResult = co_await fg_Move(First).f_Wrap();
					auto SecondResult = co_await fg_Move(Second).f_Wrap();

					DMibExpectFalse(FirstResult);
					DMibExpectFalse(SecondResult);
					DMibExpect(nLoads, ==, 1);

					auto Retried = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "retry.cpp");

					DMibExpect(Retried["custom"], ==, "Recovered");

					co_return {};
				};

				DMibTestCategory("ExpiredLoader") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing expired loader actors");
					CStr RootPath = CFile::fs_GetProgramDirectory() / "DevelopExpiredLoaderProject";
					TCActor<CActor> LoaderActor = fg_Construct();
					TCActor<CEditorConfigResolver> Resolver = fg_Construct
						(
							g_ActorFunctorWeak(LoaderActor) / [](CStr _Path) -> TCFuture<TCOptional<CStr>>
							{
								co_return "root = true\n[*]\ncustom = Loaded\n";
							}
							, RootPath
						)
					;
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
					co_await fg_Move(LoaderActor).f_Destroy();
					auto Result = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "file.cpp").f_Wrap();

					DMibExpectFalse(Result);

					co_return {};
				};

				DMibTestCategory("FileSystem") -> TCFuture<void>
				{
					auto Capture = co_await (g_CaptureExceptions % "Testing FileSystem resolution");
					CStr RootPath = CFile::fs_GetProgramDirectory() / "DevelopTests" / fg_TestGetCurrentPath().f_RemovePrefix("Malterlib/Develop/");
					fg_TestAddCleanupPath(RootPath);

					auto BlockingActor = fg_BlockingActor();
					co_await
						(
							g_Dispatch(BlockingActor) / [RootPath]
							{
								if (CFile::fs_FileExists(RootPath))
									CFile::fs_DeleteDirectoryRecursive(RootPath);
								CFile::fs_CreateDirectory(RootPath / "src");
								CFile::fs_WriteStringToFile(RootPath / ".editorconfig", "root = true\n[*]\ncustom = Disk\n", true);
								CFile::fs_WriteStringToFile(RootPath / "src/.editorconfig", "[*.cpp]\ncustom = NestedDisk\n", false);
							}
						)
					;

					TCActor<CEditorConfigResolver> Resolver = fg_Construct();
					auto DestroyResolver = co_await fg_AsyncDestroy(Resolver);
					auto Properties = co_await Resolver(&CEditorConfigResolver::f_Resolve, RootPath / "src/main.cpp");

					DMibExpect(Properties["custom"], ==, "NestedDisk");

					co_return {};
				};

				co_return {};
			};
		}
	};
}

DMibTestRegister(CEditorConfig_Tests, Malterlib::Develop);
