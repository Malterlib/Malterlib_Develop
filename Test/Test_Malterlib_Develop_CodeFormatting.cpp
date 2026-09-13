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

	// Converting to a trailing return type is the one rule that changes tokens, so those
	// cases opt out of the token check and are pinned by their golden output instead.
	void fg_ExpectFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected, bool _bTokensPreserved = true)
	{
		DMibTestPath(_Case);
		auto Formatted = fg_FormatSource(_Source, "First");
		DMibExpect(Formatted, ==, _Expected);
		// Formatting is idempotent: a second pass must find nothing left to do.
		DMibExpect(fg_FormatSource(Formatted, "Second"), ==, _Expected);
		if (_bTokensPreserved)
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
					fg_ExpectFormat("Nested", "void f()\n{\n\tg\n\t\t(\n\t\t\t5\n\t\t\t, h\n\t\t\t\t(\n\t\t\t\t\t6\n\t\t\t\t)\n\t\t)\n\t;\n}\n", "void f()\n{\n\tg(5, h(6));\n}\n");
					// A parameter list rejoins while the body keeps its own lines.
					fg_ExpectFormat("ParameterList", "void fg_F\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n{\n}\n", "void fg_F(int _A, int _B)\n{\n}\n");
					fg_ExpectFormat("Template", "TCMap\n<\n\tCStr\n\t, CStr\n>\ng_Map;\n", "TCMap<CStr, CStr> g_Map;\n");
					// Two nested lists close with one '>>' token.
					fg_ExpectFormat("NestedTemplate", "TCMap\n<\n\tCStr\n\t, TCVector<CStr>\n>\ng_Map;\n", "TCMap<CStr, TCVector<CStr>> g_Map;\n");
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

				DMibTestCategory("Bodies")
				{
					// An empty body after a braced member initializer has no terminator of its
					// own. Reading it as another initializer swallowed the next declaration,
					// which then no longer fit on one line and was split apart.
					CStr Source = "struct C\n{\n\tC::C(int _A)\n\t\t: C{_A}\n\t{\n\t}\n\n\tC::C(CInit const &_B)\n\t\t: mp_p(fg_Construct(_B))\n\t{\n\t}\n};\n";
					fg_ExpectFormat("BracedInitializer", Source, Source);

					// A template header keeps its space; a template argument list does not.
					fg_ExpectFormat("TemplateHeader", "template\n<\n\ttypename t_CType\n\t, umint t_n\n>\nvoid fg_F();\n", "template <typename t_CType, umint t_n>\nvoid fg_F();\n");

					// A directive inside a member initializer list fixes those lines, and
					// measuring the entry that spans it as one line split what already fit.
					CStr Directive = "struct C\n{\n\tC(C &&_Other)\n\t\t: m_A(fg_Move(_Other.m_A))\n\t\t, m_B(fg_Exchange(_Other.m_B, nullptr))\n"
						"#if DDebug\n\t\t, m_C(fg_Move(_Other.m_C))\n#endif\n\t{\n\t}\n};\n"
					;
					fg_ExpectFormat("InitializerDirective", Directive, Directive);

					// The '?' of a conditional operator can stand in front of the first
					// parenthesis, and its ':' was then read as a member initializer list.
					CStr Conditional = "void f()\n{\n\tumint Mask = nBits == c_Per ? ~umint(0) : ((umint(1) << nBits) - 1);\n}\n";
					fg_ExpectFormat("Conditional", Conditional, Conditional);

					// The terminator ends the expression's line and counts towards it. An
					// expression that only fits without it still has to be split.
					CStr Wide;
					for (umint i = 0; i < 183; ++i)
						Wide += "W";

					CStr Terminator = "void f()\n{\n\tg(" + Wide + ");\n}\n";
					fg_ExpectFormat("Terminator", Terminator, "void f()\n{\n\tg\n\t\t(\n\t\t\t" + Wide + "\n\t\t)\n\t;\n}\n");

					// A braced value is a template argument like any other. Stopping the
					// angle match at its brace turned the list's '<' and '>' into
					// comparisons, and the statement was then broken at them.
					CStr Braced = "void f()\n{\n\tg.f_Bind\n\t\t<\n\t\t\t&C::f_D<CReturn, NTraits::TCDecay<tfp_CParams>...>\n"
						"\t\t\t, COptions{EType::mc_Direct, EVirtual::mc_Not}\n\t\t>\n\t\t(\n"
						"\t\t\tNFunction::TCFunctionMovable<CReturn (NTraits::TCRemoveQualifiersAndAddRValueReference<tfp_CParams>...)>\n"
						"\t\t\t(fg_Forward<tf_FToDispatch>(_fDispatch))\n\t\t\t, fg_Forward<tfp_CParams>(p_Params)...\n\t\t)\n\t;\n}\n"
					;
					// The argument list fits where it stands, so only the call is opened up.
					CStr BracedResult = "void f()\n{\n\tg.f_Bind<&C::f_D<CReturn, NTraits::TCDecay<tfp_CParams>...>"
						", COptions{EType::mc_Direct, EVirtual::mc_Not}>\n\t\t(\n"
						"\t\t\tNFunction::TCFunctionMovable<CReturn (NTraits::TCRemoveQualifiersAndAddRValueReference<tfp_CParams>...)>(fg_Forward<tf_FToDispatch>(_fDispatch))\n"
						"\t\t\t, fg_Forward<tfp_CParams>(p_Params)...\n\t\t)\n\t;\n}\n"
					;
					fg_ExpectFormat("BracedTemplateArgument", Braced, BracedResult);
				};

				DMibTestCategory("Kept")
				{
					// A comment or a directive inside fixes the construct's line structure.
					fg_ExpectFormat("Comment", "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n", "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n");
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
					fg_ExpectFormat("ClauseBody", "void f()\n{\n\twhile (auto p = g())\n\t\t(*p)();\n}\n", "void f()\n{\n\twhile (auto p = g())\n\t\t(*p)();\n}\n");
					// A braced initializer is written one element per line on purpose.
					fg_ExpectFormat
						(
							"BracedInitializer"
							, "auto g_Option =\n\t{\n\t\t\"Names\"_o= 1\n\t\t, \"Default\"_o= 2\n\t}\n;\n"
							, "auto g_Option =\n\t{\n\t\t\"Names\"_o= 1\n\t\t, \"Default\"_o= 2\n\t}\n;\n"
						)
					;
					// A call nested inside one still rejoins.
					fg_ExpectFormat("CallInsideInitializer", "auto g_Option =\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t1\n\t\t\t)\n\t}\n;\n", "auto g_Option =\n\t{\n\t\tg(1)\n\t}\n;\n");
					// A lambda body is a block, so the call around it keeps its lines.
					fg_ExpectFormat
						(
							"LambdaBody"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("Split")
				{
					// The forms in Malterlib_Core_CodeStandard_Formatting.dox.
					CStr Wide;
					for (umint i = 0; i < 90; ++i)
						Wide += "W";

					auto fSplit = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected)
						{
							fg_ExpectFormat(_Case, _Source.f_Replace("@", Wide), _Expected.f_Replace("@", Wide));
						}
					;
					fSplit("Call", "void f()\n{\n\tg(@, @, @);\n}\n", "void f()\n{\n\tg\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t\t, @\n\t\t)\n\t;\n}\n");
					// A clause's parenthesis sits at the statement's own indentation.
					fSplit("Clause", "void f()\n{\n\tif (@ && @ && @)\n\t\th();\n}\n", "void f()\n{\n\tif\n\t(\n\t\t@\n\t\t&& @\n\t\t&& @\n\t)\n\t\th();\n}\n");
					fSplit("For", "void f()\n{\n\tfor (umint @ = 0; @ < 5; ++@)\n\t\th();\n}\n", "void f()\n{\n\tfor\n\t(\n\t\tumint @ = 0\n\t\t; @ < 5\n\t\t; ++@\n\t)\n\t\th();\n}\n");
					// A definition splits its parameter list and keeps its body at statement level.
					fSplit("Definition", "void fg_F(int @, int @)\n{\n}\n", "void fg_F\n\t(\n\t\tint @\n\t\t, int @\n\t)\n{\n}\n");
					// A trailing qualifier run is a logical unit of its own.
					fSplit
						(
							"Qualifiers"
							, "struct C\n{\n\tvoid f_F(int @, int @) const volatile = 0;\n};\n"
							, "struct C\n{\n\tvoid f_F\n\t\t(\n\t\t\tint @\n\t\t\t, int @\n\t\t)\n\t\tconst volatile = 0\n\t;\n};\n"
						)
					;
					// Every scope marker of a split statement gets its own line.
					fSplit
						(
							"Chained"
							, "void f()\n{\n\tg(@, @).f_Call(@, @);\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t\t.f_Call\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t;\n}\n"
						)
					;
					// The outermost level is split first: an operator chain breaks at its
					// loosest operators, and a call on a line that then fits is left alone.
					fSplit("OperatorChain", "void f()\n{\n\to_Str += g(\"@\") << @ << @;\n}\n", "void f()\n{\n\to_Str += g(\"@\")\n\t\t<< @\n\t\t<< @\n\t;\n}\n");
					// The loosest operator wins, so a tighter one stays on its line.
					fSplit("Precedence", "void f()\n{\n\treturn g(\"@\") && h(\"@\") == nullptr;\n}\n", "void f()\n{\n\treturn g(\"@\")\n\t\t&& h(\"@\") == nullptr\n\t;\n}\n");
					// An element that still does not fit splits its own scope markers.
					fSplit
						(
							"Nested"
							, "void f()\n{\n\tg(@, h(@, @, @));\n}\n"
							// A call inside a split expression aligns its own scope markers with
							// the name, rather than taking another continuation level.
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t@\n\t\t\t, h\n\t\t\t(\n\t\t\t\t@\n\t\t\t\t, @\n\t\t\t\t, @\n\t\t\t)\n\t\t)\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("TrailingReturn")
				{
					// The name before the parameter list must itself exceed the limit.
					CStr Wide;
					for (umint i = 0; i < 180; ++i)
						Wide += "R";

					auto fSplit = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected, bool _bTokensPreserved = false)
						{
							fg_ExpectFormat(_Case, _Source.f_Replace("@", Wide), _Expected.f_Replace("@", Wide), _bTokensPreserved);
						}
					;
					// The name does not fit before the parameter list, so the return type moves.
					fSplit("Declaration", "TCLongTemplate<@> fg_F(int _A, int _B);\n", "auto fg_F\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n\t-> TCLongTemplate<@>\n;\n");
					fSplit("Definition", "TCLongTemplate<@> fg_F(int _A, int _B)\n{\n}\n", "auto fg_F\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n\t-> TCLongTemplate<@>\n{\n}\n");
					// The trailing type goes after the qualifiers.
					fSplit
						(
							"Qualifiers"
							, "struct C\n{\n\tTCLongTemplate<@> f_F(int _A) const volatile;\n};\n"
							, "struct C\n{\n\tauto f_F\n\t\t(\n\t\t\tint _A\n\t\t)\n\t\tconst volatile\n\t\t-> TCLongTemplate<@>\n\t;\n};\n"
						)
					;
					// Declaration specifiers stay in front of auto.
					fSplit("Specifiers", "static inline_always TCLongTemplate<@> fg_F(int _A);\n", "static inline_always auto fg_F\n\t(\n\t\tint _A\n\t)\n\t-> TCLongTemplate<@>\n;\n");
					// A constructor has no return type to move. Its parameter list stands behind
					// the name, so splitting there means opening the list.
					fSplit("Constructor", "CLongName<@>::CLongName(int _A, int _B);\n", "CLongName<@>::CLongName\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n;\n", true);
					// An expression statement also ends in a call, and must never be rewritten
					// as a declaration.
					fSplit
						(
							"ExpressionStatement"
							, "void f()\n{\n\to_Str = \"@\"_f << m_A << m_Lines.f_FindLine(_Offset) + 1 << m_B;\n}\n"
							, "void f()\n{\n\to_Str = \"@\"_f\n\t\t<< m_A\n\t\t<< m_Lines.f_FindLine(_Offset) + 1\n\t\t<< m_B\n\t;\n}\n"
							, true
						)
					;
					// An existing trailing return type is only relaid out.
					fSplit("AlreadyTrailing", "auto fg_F(int _A, int _B) -> TCLongTemplate<@>;\n", "auto fg_F\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n\t-> TCLongTemplate<@>\n;\n", true);
				};

				DMibTestCategory("TooLong")
				{
					DMibTestPath("DoesNotFit");
					CStr Name;
					for (umint i = 0; i < 100; ++i)
						Name += "A";

					// An empty parameter list is nothing to split, so the terminator stays put
					// rather than being stranded while the line is still too long.
					DMibTestCategory("EmptyParameterList")
					{
						CStr Long = "extern template void NMib::NConcurrency::fg_Delete";
						for (umint i = 0; i < 30; ++i)
							Long += "VeryLongName";

						Long += "();\n";
						DMibExpect(fg_FormatSource(Long), ==, Long);
					};

					// A statement with no scope marker to split keeps its shape and is reported.
					CStr Source = "int a" + Name + " = 0;\n";
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
