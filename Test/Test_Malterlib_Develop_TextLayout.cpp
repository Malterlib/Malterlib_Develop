// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Develop/TextLayout>
#include <Mib/Test/Test>

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NStr;
	using namespace NMib::NTest;

	umint fg_Columns(CStr const &_Text, umint _nTabWidth = 4)
	{
		// A measurement failure leaves the sentinel so the caller's comparison fails.
		umint nColumns = TCLimitsInt<umint>::mc_Max;
		fg_MeasureTextColumns(_Text, _nTabWidth, nColumns);

		return nColumns;
	}

	struct CTextLayout_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Columns")
			{
				DMibTestCategory("TabStops")
				{
					DMibExpect(fg_Columns(""), ==, 0u);
					DMibExpect(fg_Columns("abc"), ==, 3u);
					DMibExpect(fg_Columns("\t"), ==, 4u);
					DMibExpect(fg_Columns("a\t"), ==, 4u);
					DMibExpect(fg_Columns("abcd\t"), ==, 8u);
					DMibExpect(fg_Columns("\t", 8), ==, 8u);
				};

				DMibTestCategory("Unicode")
				{
					// Each whole code point counts as one column; a broken sequence counts its bytes.
					DMibExpect(fg_Columns("\xC3\xA5\xC3\xA4\xC3\xB6"), ==, 3u);
					DMibExpect(fg_Columns("\xEF\xBB\xBF"), ==, 1u);
					DMibExpect(fg_Columns(CStr("a\0b", 3)), ==, 3u);
					DMibExpect(fg_Columns("\xC3"), ==, 1u);
				};
			};

			DMibTestSuite("LineMap")
			{
				CTextLineMap Map("a\nbb\r\nccc\rdddd");
				DMibAssert(Map.f_GetLineCount(), ==, 4u);
				DMibExpect(Map.f_GetLine(0).m_nLength, ==, 1u);
				DMibExpectTrue(Map.f_GetLine(0).m_Ending == ETextLineEnding::mc_LF);
				DMibExpectTrue(Map.f_GetLine(1).m_Ending == ETextLineEnding::mc_CRLF);
				DMibExpectTrue(Map.f_GetLine(2).m_Ending == ETextLineEnding::mc_CR);
				DMibExpectTrue(Map.f_GetLine(3).m_Ending == ETextLineEnding::mc_None);
				DMibExpect(Map.f_GetLineStart(3), ==, 10u);
				DMibExpect(Map.f_GetLineEnd(1), ==, 6u);
				DMibExpect(Map.f_FindLine(0), ==, 0u);
				DMibExpect(Map.f_FindLine(5), ==, 1u);
				DMibExpect(Map.f_FindLine(13), ==, 3u);

				DMibTestCategory("TerminatedTail")
				{
					// A trailing terminator leaves an empty final line so every offset maps.
					CTextLineMap Terminated("a\n");
					DMibAssert(Terminated.f_GetLineCount(), ==, 2u);
					DMibExpect(Terminated.f_GetLine(1).m_nLength, ==, 0u);
					DMibExpect(Terminated.f_FindLine(2), ==, 1u);
				};

				DMibTestCategory("Empty")
				{
					CTextLineMap Empty("");
					DMibExpectTrue(Empty.f_IsEmpty());
					DMibExpect(Empty.f_GetLineCount(), ==, 1u);
					DMibExpect(Empty.f_FindLine(0), ==, 0u);
				};
			};

			DMibTestSuite("Signature")
			{
				DMibExpect(fg_GetTextBomLength("\xEF\xBB\xBF" "abc"), ==, 3u);
				DMibExpect(fg_GetTextBomLength("abc"), ==, 0u);
				DMibExpect(fg_GetTextLineEndingBytes(ETextLineEnding::mc_CRLF), ==, "\r\n");
				DMibExpect(fg_GetTextLineEndingBytes(ETextLineEnding::mc_None), ==, "");
			};
		}
	};
}

DMibTestRegister(CTextLayout_Tests, Malterlib::Develop);
