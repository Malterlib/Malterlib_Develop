// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Develop/CodeFormatting>
#include <Mib/Develop/CodeFormattingLexer>
#include <Mib/Develop/CodeFormattingStructure>
#include <Mib/Test/Test>
#include <Mib/Test/Exception>

namespace NMib::NDevelop::NCodeFormattingTest
{
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NTest;

	CEditorConfigProperties fg_MalterlibProperties();
	TCSharedPointer<CCodeFormattingStyle const> fg_MalterlibStyle();
	CCodeFormattingRequest fg_Request(CStr const &_Source);
	CCodeFormattingResult fg_Analyze(CStr const &_Source);
	CStr fg_FormatSource(CStr const &_Source, CStr const &_Stage = "Pass");
	void fg_ExpectFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected, bool _bTokensPreserved = true);
	CEditorConfigProperties fg_SpacesProperties();
	void fg_ExpectFormatWith(CStr const &_Case, CEditorConfigProperties const &_Properties, CStr const &_Source, CStr const &_Expected);
	CCodeFormattingRequest fg_BuildSystemRequest(CStr const &_Source);
	CStr fg_FormatBuildSystem(CStr const &_Source, CStr const &_Stage);
	void fg_ExpectBuildSystemFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected);
	CStr fg_FormatC(CStr const &_Source, CStr const &_Stage);
	void fg_ExpectCFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected);

	void fg_TestLineBreaks();
	void fg_TestKeptLineBreaks();
	void fg_TestSplitLineBreaks();
}
