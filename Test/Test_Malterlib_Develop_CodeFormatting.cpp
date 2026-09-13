// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Develop/CodeFormatting>
#include <Mib/Develop/CodeFormattingLexer>
#include <Mib/Test/Test>
#include <Mib/Test/Exception>

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NTest;

	CEditorConfigProperties fg_MalterlibProperties()
	{
		return
			{
				{"malterlib_format", "malterlib"}
				, {"indent_style", "tab"}
				, {"indent_size", "4"}
				, {"tab_width", "4"}
				, {"max_line_length", "190"}
			}
		;
	}

	CCodeFormattingRequest fg_Request(CStr const &_Source)
	{
		CCodeFormattingRequest Request;
		Request.m_Source = _Source;
		Request.m_Path = "Source/Example.cpp";
		Request.m_Language = ECodeLanguage::mc_Cpp;
		Request.m_Settings = CCodeFormattingSettings(fg_MalterlibProperties());

		return Request;
	}

	CCodeFormattingResult fg_Analyze(CStr const &_Source)
	{
		return fg_AnalyzeCodeFormatting(fg_Request(_Source));
	}

	CStr fg_FormatSource(CStr const &_Source, CStr const &_Stage = "Pass")
	{
		DMibTestPath(_Stage);
		auto Result = fg_Analyze(_Source);
		DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
		if (Result.m_Status != ECodeFormattingStatus::mc_Complete)
			return Result.m_Explanation;

		return fg_ApplyCodeFormattingEdits(_Source, Result.m_Edits);
	}

	void fg_ExpectFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected)
	{
		DMibTestPath(_Case);
		auto Formatted = fg_FormatSource(_Source, "First");
		DMibExpect(Formatted, ==, _Expected);
		// Formatting is idempotent: a second pass must find nothing left to do.
		DMibExpect(fg_FormatSource(Formatted, "Second"), ==, _Expected);
		DMibExpectTrue(fg_HasEquivalentCodeTokens(_Source, Formatted));
	}

	struct CCodeFormatting_Tests : CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Settings")
			{
				DMibTestCategory("Defaults")
				{
					CCodeFormattingSettings Disabled{CEditorConfigProperties{{"max_line_length", "120"}}};
					DMibExpectFalse(Disabled.f_IsFormattingEnabled());
					DMibExpect(Disabled.m_nMaxColumns, ==, 120u);

					CCodeFormattingSettings Profile{CEditorConfigProperties{{"malterlib_format", "malterlib"}}};
					DMibExpectTrue(Profile.f_IsFormattingEnabled());
					DMibExpect(Profile.m_nMaxColumns, ==, 190u);
					DMibExpect(Profile.m_nTabWidth, ==, 4u);
					DMibExpectTrue(Profile.m_bIndentWithTabs);
					DMibExpectFalse(Profile.m_bHasExplicitMaxColumns);

					CCodeFormattingSettings Missing{CEditorConfigProperties{}};
					DMibExpectFalse(Missing.f_IsFormattingEnabled());
					DMibExpect(Missing.m_nMaxColumns, ==, 0u);
				};

				DMibTestCategory("Overrides")
				{
					CCodeFormattingSettings Settings
						{
							CEditorConfigProperties
								{
									{"malterlib_format", "MALTERLIB"}
									, {"indent_style", "space"}
									, {"tab_width", "8"}
									, {"max_line_length", "off"}
									, {"end_of_line", "crlf"}
									, {"trim_trailing_whitespace", "false"}
									, {"insert_final_newline", "false"}
									, {"charset", "UTF-8"}
								}
						}
					;
					DMibExpectTrue(Settings.f_IsFormattingEnabled());
					DMibExpectFalse(Settings.m_bIndentWithTabs);
					DMibExpect(Settings.m_nTabWidth, ==, 8u);
					DMibExpect(Settings.m_nMaxColumns, ==, 0u);
					DMibExpectTrue(Settings.m_bHasExplicitMaxColumns);
					DMibExpectFalse(Settings.m_bTrimTrailingWhitespace);
					DMibExpectFalse(Settings.m_bInsertFinalNewline);
					DMibAssertTrue(bool(Settings.m_EndOfLine));
					DMibExpectTrue(*Settings.m_EndOfLine == ETextLineEnding::mc_CRLF);
					DMibExpect(Settings.m_Charset, ==, "utf-8");
				};

				DMibTestCategory("Unset")
				{
					// A generic unset removes the inherited property, disabling formatting.
					CCodeFormattingSettings Settings{CEditorConfigProperties{{"malterlib_format", "unset"}}};
					DMibExpectFalse(Settings.f_IsFormattingEnabled());

					CCodeFormattingSettings Off{CEditorConfigProperties{{"malterlib_format", "off"}}};
					DMibExpectFalse(Off.f_IsFormattingEnabled());
				};

				DMibTestCategory("Invalid")
				{
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"malterlib_format", "malterlibb"}}), NException::CException);
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"malterlib_format", "true"}}), NException::CException);
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"indent_style", "tabs"}}), NException::CException);
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"end_of_line", "nel"}}), NException::CException);
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"max_line_length", "0"}}), NException::CException);
				};

				DMibTestCategory("Language")
				{
					DMibExpectTrue(fg_DetectCodeLanguage("Source/Example.cpp") == ECodeLanguage::mc_Cpp);
					DMibExpectTrue(fg_DetectCodeLanguage("Source/Example.imp.h") == ECodeLanguage::mc_Cpp);
					DMibExpectTrue(fg_DetectCodeLanguage("Include/Mib/Develop/CodeFormatting") == ECodeLanguage::mc_Cpp);
					DMibExpectTrue(fg_DetectCodeLanguage("Malterlib_Develop.MHeader") == ECodeLanguage::mc_Unknown);
					DMibExpectTrue(fg_DetectCodeLanguage("README.md") == ECodeLanguage::mc_Unknown);
				};
			};

			DMibTestSuite("Lexer")
			{
				DMibTestCategory("Kinds")
				{
					CCodeTokenStream Stream("R\"x(a\"b)x\" 'c' 1'000 /* m\n */ // l\n#define A \\\n\tB\n");
					DMibExpectTrue(Stream.f_IsComplete());
					auto const &Tokens = Stream.f_GetTokens();
					DMibAssertTrue(Tokens.f_GetLen() > 6);
					DMibExpectTrue(Tokens[0].m_Kind == ECodeTokenKind::mc_StringLiteral);
					DMibExpect(Stream.f_GetText(Tokens[0]), ==, "R\"x(a\"b)x\"");
					DMibExpectTrue(Tokens[2].m_Kind == ECodeTokenKind::mc_CharLiteral);
					DMibExpectTrue(Tokens[4].m_Kind == ECodeTokenKind::mc_Number);
					DMibExpect(Stream.f_GetText(Tokens[4]), ==, "1'000");
					DMibExpectTrue(Tokens[6].m_Kind == ECodeTokenKind::mc_BlockComment);
					DMibExpectTrue(Tokens[6].m_bMultiLine);
					DMibExpectTrue(Tokens[8].m_Kind == ECodeTokenKind::mc_LineComment);
					DMibExpectTrue(Tokens[10].m_Kind == ECodeTokenKind::mc_Preprocessor);
					DMibExpectTrue(Tokens[10].m_bMultiLine);
					DMibExpect(Stream.f_GetText(Tokens[10]), ==, "#define A \\\n\tB");
				};

				DMibTestCategory("Lossless")
				{
					auto fRoundTrip = [&](CStr const &_Case, CStr const &_Source)
						{
							DMibTestPath(_Case);
							CCodeTokenStream Stream(_Source);
							CStr Joined;
							for (auto const &Token : Stream.f_GetTokens())
								Joined += Stream.f_GetText(Token);

							DMibExpect(Joined, ==, _Source);
						}
					;
					fRoundTrip("Signature", "\xEF\xBB\xBF#pragma once\r\nint a = 1;\n");
					fRoundTrip("RawString", "auto x = R\"()\";\n");
					fRoundTrip("Escapes", "auto x = \"a\\\"b\\\\\";\nchar c = '\\'';\n");
					fRoundTrip("Conditional", "#if 0\nnot code(\n#endif\nint a;\n");
					fRoundTrip("Empty", "");
				};

				DMibTestCategory("Incomplete")
				{
					DMibExpectFalse(CCodeTokenStream("/* unterminated").f_IsComplete());
					DMibExpectFalse(CCodeTokenStream("auto x = \"open\n").f_IsComplete());
					DMibExpectFalse(CCodeTokenStream("auto x = R\"d(open\n").f_IsComplete());
				};
			};

			DMibTestSuite("Rules")
			{
				DMibTestCategory("Indentation")
				{
					fg_ExpectFormat("SpacesToTabs", "void f()\n{\n        int a;\n}\n", "void f()\n{\n\t\tint a;\n}\n");
					fg_ExpectFormat("MixedIndent", "void f()\n{\n \t int a;\n}\n", "void f()\n{\n\t int a;\n}\n");
					fg_ExpectFormat("AlreadyTabs", "void f()\n{\n\tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					// Layout inside a multiline literal or comment is protected.
					fg_ExpectFormat("RawStringBody", "auto x = R\"(\n    keep\n)\";\n", "auto x = R\"(\n    keep\n)\";\n");
					fg_ExpectFormat("BlockCommentBody", "/* text\n    keep\n */\n", "/* text\n    keep\n */\n");
					fg_ExpectFormat("MacroBody", "#define A \\\n    B\n", "#define A \\\n    B\n");
				};

				DMibTestCategory("TrailingWhitespace")
				{
					fg_ExpectFormat("Code", "int a;   \n", "int a;\n");
					fg_ExpectFormat("BlankLine", "int a;\n\t\nint b;\n", "int a;\n\nint b;\n");
					fg_ExpectFormat("LineComment", "int a; // c  \n", "int a; // c\n");
					fg_ExpectFormat("ProtectedLiteral", "auto x = R\"(a   \nb)\";\n", "auto x = R\"(a   \nb)\";\n");
					fg_ExpectFormat("ProtectedSplice", "#define A B  \\\n\tC\n", "#define A B  \\\n\tC\n");
				};

				DMibTestCategory("FinalNewline")
				{
					fg_ExpectFormat("Missing", "int a;", "int a;\n");
					fg_ExpectFormat("Present", "int a;\n", "int a;\n");
					fg_ExpectFormat("Empty", "", "");
					fg_ExpectFormat("MatchesFile", "int a;\r\nint b;", "int a;\r\nint b;\r\n");
				};

				DMibTestCategory("ClauseSpace")
				{
					fg_ExpectFormat("If", "void f()\n{\n\tif(a)\n\t\tg();\n}\n", "void f()\n{\n\tif (a)\n\t\tg();\n}\n");
					fg_ExpectFormat("While", "void f()\n{\n\twhile  (a)\n\t\tg();\n}\n", "void f()\n{\n\twhile (a)\n\t\tg();\n}\n");
					// A split clause head that fits is brought back to one line.
					fg_ExpectFormat("Split", "void f()\n{\n\tif\n\t(\n\t\ta\n\t)\n\t\tg();\n}\n", "void f()\n{\n\tif (a)\n\t\tg();\n}\n");
					fg_ExpectFormat("Call", "void f()\n{\n\tg(a);\n}\n", "void f()\n{\n\tg(a);\n}\n");
				};

				DMibTestCategory("CommaSpace")
				{
					fg_ExpectFormat("Tight", "void f()\n{\n\tg(a,b);\n}\n", "void f()\n{\n\tg(a, b);\n}\n");
					fg_ExpectFormat("SpaceBefore", "void f()\n{\n\tg(a , b);\n}\n", "void f()\n{\n\tg(a, b);\n}\n");
					fg_ExpectFormat("Leading", "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t, b\n\t\t)\n\t;\n}\n", "void f()\n{\n\tg(a, b);\n}\n");
					fg_ExpectFormat("Pack", "void f()\n{\n\tg(a, ...);\n}\n", "void f()\n{\n\tg(a, ...);\n}\n");
				};

				DMibTestCategory("OperatorSpace")
				{
					fg_ExpectFormat("Compare", "void f()\n{\n\tif (a==b || c!=d)\n\t\tg();\n}\n", "void f()\n{\n\tif (a == b || c != d)\n\t\tg();\n}\n");
					fg_ExpectFormat("Compound", "void f()\n{\n\ta+=1;\n\tb<<=2;\n}\n", "void f()\n{\n\ta += 1;\n\tb <<= 2;\n}\n");
					// A lone '=' is also a capture default and the tail of the '_o=' DSL, so it is
					// left alone rather than rewritten from an ambiguous reading.
					fg_ExpectFormat("AssignIsAmbiguous", "void f()\n{\n\tint a=1;\n}\n", "void f()\n{\n\tint a=1;\n}\n");
					fg_ExpectFormat("CaptureDefault", "void f()\n{\n\tauto g = [=]{};\n}\n", "void f()\n{\n\tauto g = [=]{};\n}\n");
					fg_ExpectFormat("FormattingDsl", "auto g_Option = \"Names\"_o= _o[\"--file\"];\n", "auto g_Option = \"Names\"_o= _o[\"--file\"];\n");
					fg_ExpectFormat("OperatorName", "struct C\n{\n\tbool operator==(C const &_Other) const;\n};\n", "struct C\n{\n\tbool operator==(C const &_Other) const;\n};\n");
					// Declarators keep their Malterlib spelling; they are not expression operators.
					fg_ExpectFormat("Declarator", "void f(CStr const &_A, CStr &&_B);\n", "void f(CStr const &_A, CStr &&_B);\n");
					fg_ExpectFormat("Pointer", "void f()\n{\n\tauto *pA = &B;\n\tauto C = *pA * 2;\n}\n", "void f()\n{\n\tauto *pA = &B;\n\tauto C = *pA * 2;\n}\n");
					fg_ExpectFormat("PureVirtual", "struct C\n{\n\tvirtual void f() = 0;\n};\n", "struct C\n{\n\tvirtual void f() = 0;\n};\n");
				};

				DMibTestCategory("BlankLines")
				{
					fg_ExpectFormat("AfterBrace", "void f()\n{\n\n\tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					fg_ExpectFormat("AfterBraceMultiple", "void f()\n{\n\t\n\n\tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					fg_ExpectFormat
						(
							"AfterCase"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\n\t\tg();\n\n\t\tbreak;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\n\t\tbreak;\n\t}\n}\n"
						)
					;
					fg_ExpectFormat("AfterDefault", "void f()\n{\n\tswitch (a)\n\t{\n\tdefault:\n\n\t\tbreak;\n\t}\n}\n", "void f()\n{\n\tswitch (a)\n\t{\n\tdefault:\n\t\tbreak;\n\t}\n}\n");
					// A defaulted member is not a label.
					fg_ExpectFormat("DefaultedMember", "struct C\n{\n\tC() = default;\n\n\tint m_A;\n};\n", "struct C\n{\n\tC() = default;\n\n\tint m_A;\n};\n");
					fg_ExpectFormat("BetweenStatements", "void f()\n{\n\tint a;\n\n\tint b;\n}\n", "void f()\n{\n\tint a;\n\n\tint b;\n}\n");
				};

				DMibTestCategory("LineEndings")
				{
					DMibTestCategory("Normalize")
					{
						auto Request = fg_Request("int a;\r\nint b;\n");
						Request.m_Settings.m_EndOfLine = ETextLineEnding::mc_LF;
						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
						DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits), ==, "int a;\nint b;\n");
					};

					fg_ExpectFormat("Preserve", "int a;\r\nint b;\n", "int a;\r\nint b;\n");
				};
			};

			DMibTestSuite("LineBreaks")
			{
				DMibTestCategory("Join")
				{
					fg_ExpectFormat("Call", "void f()\n{\n\tg\n\t\t(\n\t\t\t5\n\t\t\t, 6\n\t\t)\n\t;\n}\n", "void f()\n{\n\tg(5, 6);\n}\n");
					fg_ExpectFormat
						(
							"Nested"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t5\n\t\t\t, h\n\t\t\t\t(\n\t\t\t\t\t6\n\t\t\t\t)\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg(5, h(6));\n}\n"
						)
					;
					// A parameter list rejoins while the body keeps its own lines.
					fg_ExpectFormat
						(
							"ParameterList"
							, "void fg_F\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n{\n}\n"
							, "void fg_F(int _A, int _B)\n{\n}\n"
						)
					;
					fg_ExpectFormat("Template", "TCMap\n<\n\tCStr\n\t, CStr\n>\ng_Map;\n", "TCMap<CStr, CStr> g_Map;\n");
					// A function type is a template argument, and its parameter list is separated.
					fg_ExpectFormat
						(
							"FunctionType"
							, "using FOnUse = TCActorFunctor\n\t<\n\t\tTCFuture<void>\n\t\t(\n\t\t\tCStr _HostID\n\t\t\t, int _Info\n\t\t)\n\t>\n;\n"
							, "using FOnUse = TCActorFunctor<TCFuture<void> (CStr _HostID, int _Info)>;\n"
						)
					;
					fg_ExpectFormat("Condition", "void f()\n{\n\tif\n\t(\n\t\ta\n\t\t&& b\n\t)\n\t\tg();\n}\n", "void f()\n{\n\tif (a && b)\n\t\tg();\n}\n");
				};

				DMibTestCategory("Kept")
				{
					// A comment or a directive inside fixes the construct's line structure.
					fg_ExpectFormat
						(
							"Comment"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"Directive"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t5\n#if 0\n\t\t\t, 6\n#endif\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t5\n#if 0\n\t\t\t, 6\n#endif\n\t\t)\n\t;\n}\n"
						)
					;
					// A template header and a requires clause stay on their own lines.
					fg_ExpectFormat("TemplateHeader", "template <typename t_C>\nvoid fg_F(t_C _A);\n", "template <typename t_C>\nvoid fg_F(t_C _A);\n");
					fg_ExpectFormat
						(
							"Requires"
							, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires cFoo<t_C>\n;\n"
							, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires cFoo<t_C>\n;\n"
						)
					;
					// A label and its body are separate statements.
					fg_ExpectFormat
						(
							"CaseLabel"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\tbreak;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\tbreak;\n\t}\n}\n"
						)
					;
					fg_ExpectFormat("AccessSpecifier", "struct C\n{\npublic:\n\tint m_A;\n};\n", "struct C\n{\npublic:\n\tint m_A;\n};\n");
					// A lambda body is part of the expression around it, so the statement keeps
					// its lines and its terminator keeps its own line.
					fg_ExpectFormat
						(
							"TerminatorAfterLambda"
							, "void f()\n{\n\tself / []() -> int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t\t> g_Discard\n\t;\n}\n"
							, "void f()\n{\n\tself / []() -> int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t\t> g_Discard\n\t;\n}\n"
						)
					;
					// A type definition still ends at its terminator.
					fg_ExpectFormat("TypeDefinition", "struct C\n{\n\tint m_A;\n};\n", "struct C\n{\n\tint m_A;\n};\n");
					// A clause's condition does not own the statement it guards, even when that
					// statement starts with a parenthesis.
					fg_ExpectFormat
						(
							"ClauseBody"
							, "void f()\n{\n\twhile (auto p = g())\n\t\t(*p)();\n}\n"
							, "void f()\n{\n\twhile (auto p = g())\n\t\t(*p)();\n}\n"
						)
					;
					// A braced initializer is written one element per line on purpose.
					fg_ExpectFormat
						(
							"BracedInitializer"
							, "auto g_Option =\n\t{\n\t\t\"Names\"_o= 1\n\t\t, \"Default\"_o= 2\n\t}\n;\n"
							, "auto g_Option =\n\t{\n\t\t\"Names\"_o= 1\n\t\t, \"Default\"_o= 2\n\t}\n;\n"
						)
					;
					// A call nested inside one still rejoins.
					fg_ExpectFormat
						(
							"CallInsideInitializer"
							, "auto g_Option =\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t1\n\t\t\t)\n\t}\n;\n"
							, "auto g_Option =\n\t{\n\t\tg(1)\n\t}\n;\n"
						)
					;
					// A lambda body is a block, so the call around it keeps its lines.
					fg_ExpectFormat
						(
							"LambdaBody"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("TooLong")
				{
					DMibTestPath("DoesNotFit");
					CStr Name;
					for (umint i = 0; i < 100; ++i)
						Name += "A";

					CStr Source = "void f()\n{\n\tg\n\t\t(\n\t\t\t" + Name + "\n\t\t\t, " + Name + "\n\t\t)\n\t;\n}\n";
					DMibExpect(fg_FormatSource(Source), ==, Source);
				};
			};

			DMibTestSuite("Directives")
			{
				DMibTestCategory("Disabled")
				{
					fg_ExpectFormat
						(
							"Region"
							, "void f()\n{\n\t// malterlib-format off\n        int a;\n\t// malterlib-format on\n        int b;\n}\n"
							, "void f()\n{\n\t// malterlib-format off\n        int a;\n\t// malterlib-format on\n\t\tint b;\n}\n"
						)
					;
				};

				DMibTestCategory("Unmatched")
				{
					auto fExpectFailed = [&](CStr const &_Case, CStr const &_Source)
						{
							DMibTestPath(_Case);
							DMibExpectTrue(fg_Analyze(_Source).m_Status == ECodeFormattingStatus::mc_Failed);
						}
					;
					fExpectFailed("MissingOn", "// malterlib-format off\nint a;\n");
					fExpectFailed("MissingOff", "// malterlib-format on\nint a;\n");
					fExpectFailed("Nested", "// malterlib-format off\n// malterlib-format off\nint a;\n");
				};
			};

			DMibTestSuite("Unsupported")
			{
				auto fExpectUnsupported = [&](CStr const &_Case, CCodeFormattingRequest const &_Request)
					{
						DMibTestPath(_Case);
						auto Result = fg_AnalyzeCodeFormatting(_Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Unsupported);
						DMibExpectTrue(Result.m_Edits.f_IsEmpty());
						DMibExpectTrue(!Result.m_Explanation.f_IsEmpty());
					}
				;

				auto Disabled = fg_Request("int a;   \n");
				Disabled.m_Settings = CCodeFormattingSettings(CEditorConfigProperties{{"max_line_length", "190"}});
				fExpectUnsupported("FormattingDisabled", Disabled);

				auto Unknown = fg_Request("int a;   \n");
				Unknown.m_Language = ECodeLanguage::mc_Unknown;
				fExpectUnsupported("UnknownLanguage", Unknown);

				auto Charset = fg_Request("int a;   \n");
				Charset.m_Settings.m_Charset = "latin1";
				fExpectUnsupported("Charset", Charset);

				fExpectUnsupported("BrokenEncoding", fg_Request("int a; // \xC3\n"));
				fExpectUnsupported("EmbeddedNul", fg_Request(CStr("int a;\0   \n", 11)));
				fExpectUnsupported("Unterminated", fg_Request("/* open\n"));
			};

			DMibTestSuite("Ranges")
			{
				CStr Source = "void f()\n{\n        int a;\n        int b;\n        int c;\n}\n";
				auto fFormatRange = [&](CStr const &_Case, umint _iOffset, umint _nLength, ECodeRangePolicy _Policy)
					{
						DMibTestPath(_Case);
						auto Request = fg_Request(Source);
						Request.m_RangePolicy = _Policy;
						auto &Range = Request.m_Ranges.f_Insert();
						Range.m_iOffset = _iOffset;
						Range.m_nLength = _nLength;
						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);

						return fg_ApplyCodeFormattingEdits(Source, Result.m_Edits);
					}
				;

				DMibExpect(fFormatRange("SecondLine", 11, 15, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\t\tint a;\n        int b;\n        int c;\n}\n");
				// A cursor selects the line it sits on.
				DMibExpect(fFormatRange("Cursor", 14, 0, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\t\tint a;\n        int b;\n        int c;\n}\n");
				// A partial selection is expanded to whole lines by default.
				DMibExpect(fFormatRange("PartialExpanded", 14, 2, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\t\tint a;\n        int b;\n        int c;\n}\n");
				// Strict ranges never touch bytes the caller did not select.
				DMibExpect(fFormatRange("PartialStrict", 14, 2, ECodeRangePolicy::mc_Strict), ==, Source);
				DMibExpect(fFormatRange("WholeLineStrict", 11, 15, ECodeRangePolicy::mc_Strict), ==, "void f()\n{\n\t\tint a;\n        int b;\n        int c;\n}\n");

				DMibTestCategory("Reported")
				{
					auto Request = fg_Request(Source);
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 14;
					Range.m_nLength = 2;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibAssert(Result.m_EffectiveRanges.f_GetLen(), ==, 1u);
					DMibExpect(Result.m_EffectiveRanges[0].m_iOffset, ==, 11u);
					DMibExpect(Result.m_EffectiveRanges[0].m_nLength, ==, 15u);
				};

				DMibTestCategory("StrictDiagnostic")
				{
					auto Request = fg_Request(Source);
					Request.m_RangePolicy = ECodeRangePolicy::mc_Strict;
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 14;
					Range.m_nLength = 2;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Edits.f_IsEmpty());
					DMibExpectTrue(Result.f_HasUnfixableDiagnostics());
				};

				DMibTestCategory("Invalid")
				{
					auto fExpectFailed = [&](CStr const &_Case, CStr const &_Source, umint _iOffset, umint _nLength)
						{
							DMibTestPath(_Case);
							auto Request = fg_Request(_Source);
							auto &Range = Request.m_Ranges.f_Insert();
							Range.m_iOffset = _iOffset;
							Range.m_nLength = _nLength;
							auto Result = fg_AnalyzeCodeFormatting(Request);
							DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Failed);
						}
					;
					fExpectFailed("PastEnd", Source, 0, Source.f_GetLen() + 1);
					fExpectFailed("OffsetPastEnd", Source, Source.f_GetLen() + 1, 0);
					fExpectFailed("CodePoint", "auto s = \"\xC3\xA5\";\n", 10, 1);
					fExpectFailed("CarriageReturn", "int a;\r\nint b;\n", 7, 1);
				};

				DMibTestCategory("NoFileWideNormalization")
				{
					// Final newlines and line-ending conversion need a whole-file request.
					auto Request = fg_Request("int a;\nint b;");
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_nLength = 7;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Edits.f_IsEmpty());
				};
			};

			DMibTestSuite("Diagnostics")
			{
				DMibTestCategory("Coordinates")
				{
					auto Result = fg_Analyze("void f()\n{\n        int a;\n}\n");
					DMibAssert(Result.m_Diagnostics.f_GetLen(), ==, 1u);
					DMibExpect(Result.m_Diagnostics[0].m_Rule, ==, "indentation");
					DMibExpect(Result.m_Diagnostics[0].m_iLine, ==, 3u);
					DMibExpect(Result.m_Diagnostics[0].m_iColumn, ==, 1u);
					DMibExpectTrue(Result.m_Diagnostics[0].m_bHasAutomaticFix);
					DMibExpectFalse(Result.f_HasUnfixableDiagnostics());
				};

				DMibTestCategory("LineLength")
				{
					CStr Long = "int a";
					for (umint i = 0; i < 200; ++i)
						Long += "b";

					Long += " = 0;\n";
					auto Result = fg_Analyze(Long);
					DMibAssert(Result.m_Diagnostics.f_GetLen(), ==, 1u);
					DMibExpect(Result.m_Diagnostics[0].m_Rule, ==, "line-length");
					DMibExpectFalse(Result.m_Diagnostics[0].m_bHasAutomaticFix);
					DMibExpectTrue(Result.f_HasUnfixableDiagnostics());
					// An indivisible overlong line stays visible and is not rewritten.
					DMibExpectTrue(Result.m_Edits.f_IsEmpty());
				};
			};

			DMibTestSuite("Apply")
			{
				DMibTestCategory("Validation")
				{
					TCVector<CCodeFormattingEdit> Edits;
					auto &First = Edits.f_Insert();
					First.m_iOffset = 4;
					First.m_nLength = 2;
					auto &Second = Edits.f_Insert();
					Second.m_iOffset = 2;
					DMibExpectExceptionType(fg_ApplyCodeFormattingEdits("abcdefgh", Edits), NException::CException);

					TCVector<CCodeFormattingEdit> Outside;
					auto &Past = Outside.f_Insert();
					Past.m_iOffset = 7;
					Past.m_nLength = 4;
					DMibExpectExceptionType(fg_ApplyCodeFormattingEdits("abcdefgh", Outside), NException::CException);
				};

				DMibTestCategory("Equivalence")
				{
					DMibExpectTrue(fg_HasEquivalentCodeTokens("int  a =  1;\n", "int a = 1;\n"));
					DMibExpectTrue(fg_HasEquivalentCodeTokens("int a; // c  \n", "int a; // c\n"));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("int a;\n", "int b;\n"));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("int a;\n", "int a\n"));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("auto s = \"a b\";\n", "auto s = \"a  b\";\n"));
				};
			};
		}
	};
}

DMibTestRegister(CCodeFormatting_Tests, Malterlib::Develop);
