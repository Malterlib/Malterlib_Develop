// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Test_Malterlib_Develop_CodeFormatting.h"

namespace NMib::NDevelop::NCodeFormattingTest
{
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

	// The naming Malterlib/Core/.malterlib-format gives, which the golden cases are written in.
	TCSharedPointer<CCodeFormattingStyle const> fg_MalterlibStyle()
	{
		static TCSharedPointer<CCodeFormattingStyle const> s_pStyle = []
			{
				CCodeFormattingStyle Style;
				Style.f_Apply
					(
						CCodeFormattingStyleDocument::fs_Parse
						(
							"Version: 1\n"
							"FunctionNames:\n"
							"  - 'f_.*'\n"
							"  - 'fp_.*'\n"
							"  - 'fs_.*'\n"
							"  - 'fsp_.*'\n"
							"  - 'fg_.*'\n"
							"  - 'fsg_.*'\n"
							"TypeNames:\n"
							"  - 'C[A-Z].*'\n"
							"  - 'TC[A-Z].*'\n"
							"  - 'IC[A-Z].*'\n"
							"  - 'TIC[A-Z].*'\n"
							"  - 'E[A-Z].*'\n"
							"  - 'F[A-Z].*'\n"
							"  - 'int8'\n"
							"  - 'int16'\n"
							"  - 'int32'\n"
							"  - 'int64'\n"
							"  - 'uint8'\n"
							"  - 'uint16'\n"
							"  - 'uint32'\n"
							"  - 'uint64'\n"
							"  - 'aint'\n"
							"  - 'umint'\n"
							"  - 'smint'\n"
							"  - 'mint'\n"
							"  - 'fp32'\n"
							"  - 'fp64'\n"
							"  - 'ch8'\n"
							"  - 'ch16'\n"
							"  - 'ch32'\n"
							"  - 'uch8'\n"
							"  - 'uch16'\n"
							"  - 'uch32'\n"
							"  - 'usize'\n"
							"  - 'ssize'\n"
							"MacroNames:\n"
							"  - 'D[A-Z].*'\n"
							"SpecifierMacros:\n"
							"  - 'inline_always'\n"
							"  - 'inline_always_debug'\n"
							"  - 'inline_always_lto'\n"
							"  - 'inline_never'\n"
							"  - 'inline_never_debug'\n"
							"  - 'inline_never_coro_exception_workaround'\n"
							"  - 'inline_small'\n"
							"  - 'inline_medium'\n"
							"  - 'inline_large'\n"
							"  - 'inline_extralarge'\n"
							"  - 'mark_nodebug'\n"
							"  - 'mark_artificial'\n"
							"  - 'mark_no_coroutine_debug'\n"
							"  - 'mark_no_stack_protector'\n"
							"  - 'assure_used'\n"
							"  - 'function_does_not_return'\n"
							"  - 'malloc_like'\n"
							"  - 'only_parameters_aliased'\n"
							"  - 'return_not_aliased'\n"
							"DSLMarkers:\n"
							"  - '_[a-z]*'\n"
							"NoReturnMacros:\n"
							"  - 'DMibError'\n"
							"  - 'DError'\n"
						)
					)
				;

				return TCSharedPointer<CCodeFormattingStyle const>(fg_Construct(fg_Move(Style)));
			}
			()
		;

		return s_pStyle;
	}

	CCodeFormattingRequest fg_Request(CStr const &_Source)
	{
		CCodeFormattingRequest Request;
		Request.m_Source = _Source;
		Request.m_Path = "Source/Example.cpp";
		Request.m_Settings = CCodeFormattingSettings(fg_MalterlibProperties());
		Request.m_pStyle = fg_MalterlibStyle();

		return Request;
	}

	CCodeFormattingResult fg_Analyze(CStr const &_Source)
	{
		return fg_AnalyzeCodeFormatting(fg_Request(_Source));
	}

	CStr fg_FormatSource(CStr const &_Source, CStr const &_Stage)
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
	void fg_ExpectFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected, bool _bTokensPreserved)
	{
		DMibTestPath(_Case);

		auto Formatted = fg_FormatSource(_Source, "First");
		DMibExpect(Formatted, ==, _Expected);

		// Formatting is idempotent: a second pass must find nothing left to do.
		DMibExpect(fg_FormatSource(Formatted, "Second"), ==, _Expected);
		if (_bTokensPreserved)
			DMibExpectTrue(fg_HasEquivalentCodeTokens(_Source, Formatted));
	}

	// A project indented with spaces, two to a level, and lines at most 80 columns long, as LLVM's.
	CEditorConfigProperties fg_SpacesProperties()
	{
		auto Properties = fg_MalterlibProperties();
		Properties["indent_style"] = "space";
		Properties["indent_size"] = "2";
		Properties["tab_width"] = "2";
		Properties["max_line_length"] = "80";

		return Properties;
	}

	// As fg_ExpectFormat, with the project's settings given.
	void fg_ExpectFormatWith(CStr const &_Case, CEditorConfigProperties const &_Properties, CStr const &_Source, CStr const &_Expected)
	{
		DMibTestPath(_Case);

		auto fFormat = [&](CStr const &_Text, CStr const &_Stage)
			{
				DMibTestPath(_Stage);

				auto Request = fg_Request(_Text);
				Request.m_Settings = CCodeFormattingSettings(_Properties);

				auto Result = fg_AnalyzeCodeFormatting(Request);
				DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				if (Result.m_Status != ECodeFormattingStatus::mc_Complete)
					return Result.m_Explanation;

				return fg_ApplyCodeFormattingEdits(_Text, Result.m_Edits);
			}
		;

		auto Formatted = fFormat(_Source, "First");
		DMibExpect(Formatted, ==, _Expected);
		DMibExpect(fFormat(Formatted, "Second"), ==, _Expected);
		DMibExpectTrue(fg_HasEquivalentCodeTokens(_Source, Formatted));
	}

	CCodeFormattingRequest fg_BuildSystemRequest(CStr const &_Source)
	{
		CCodeFormattingRequest Request;
		Request.m_Source = _Source;
		Request.m_Path = "Example.MHeader";

		auto Properties = fg_MalterlibProperties();
		Properties["malterlib_format"] = "malterlib-buildsystem";
		Request.m_Settings = CCodeFormattingSettings(Properties);

		return Request;
	}

	CStr fg_FormatBuildSystem(CStr const &_Source, CStr const &_Stage)
	{
		DMibTestPath(_Stage);

		auto Result = fg_AnalyzeCodeFormatting(fg_BuildSystemRequest(_Source));
		DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
		if (Result.m_Status != ECodeFormattingStatus::mc_Complete)
			return Result.m_Explanation;

		return fg_ApplyCodeFormattingEdits(_Source, Result.m_Edits);
	}

	void fg_ExpectBuildSystemFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected)
	{
		DMibTestPath(_Case);

		auto Formatted = fg_FormatBuildSystem(_Source, "First");
		DMibExpect(Formatted, ==, _Expected);
		DMibExpect(fg_FormatBuildSystem(Formatted, "Second"), ==, _Expected);
		DMibExpectTrue(fg_HasEquivalentCodeTokens(_Source, Formatted, ECodeLanguage::mc_BuildSystem));
	}

	CStr fg_FormatC(CStr const &_Source, CStr const &_Stage)
	{
		DMibTestPath(_Stage);

		auto Request = fg_Request(_Source);
		auto Properties = fg_MalterlibProperties();
		Properties["malterlib_format"] = "malterlib-c";
		Request.m_Settings = CCodeFormattingSettings(Properties);

		auto Result = fg_AnalyzeCodeFormatting(Request);
		DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
		if (Result.m_Status != ECodeFormattingStatus::mc_Complete)
			return Result.m_Explanation;

		return fg_ApplyCodeFormattingEdits(_Source, Result.m_Edits);
	}

	void fg_ExpectCFormat(CStr const &_Case, CStr const &_Source, CStr const &_Expected)
	{
		DMibTestPath(_Case);

		auto Formatted = fg_FormatC(_Source, "First");
		DMibExpect(Formatted, ==, _Expected);
		DMibExpect(fg_FormatC(Formatted, "Second"), ==, _Expected);
	}
}

namespace
{
	using namespace NMib;
	using namespace NMib::NDevelop;
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NStorage;
	using namespace NMib::NTest;
	using namespace NMib::NDevelop::NCodeFormattingTest;

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
					// 'tab' is a width only indent_size can name.
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"tab_width", "tab"}}), NException::CException);
				};

				DMibTestCategory("Language")
				{
					// The profile says what a file is written in, whatever it is named.
					CCodeFormattingSettings Cpp{CEditorConfigProperties{{"malterlib_format", "malterlib"}}};
					CCodeFormattingSettings BuildSystem{CEditorConfigProperties{{"malterlib_format", "Malterlib-BuildSystem"}}};
					CCodeFormattingSettings C{CEditorConfigProperties{{"malterlib_format", "malterlib-c"}}};
					CCodeFormattingSettings Disabled{CEditorConfigProperties{{"malterlib_format", "off"}}};

					DMibExpectTrue(BuildSystem.f_IsFormattingEnabled());
					DMibExpect(BuildSystem.m_nMaxColumns, ==, 190u);
					DMibExpectTrue(Cpp.f_GetLanguage() == ECodeLanguage::mc_Cpp);
					DMibExpectTrue(BuildSystem.f_GetLanguage() == ECodeLanguage::mc_BuildSystem);
					DMibExpectTrue(C.f_GetLanguage() == ECodeLanguage::mc_C);
					DMibExpectTrue(Disabled.f_GetLanguage() == ECodeLanguage::mc_Unknown);

					// 'off' turns the line length check off however it is cased.
					CCodeFormattingSettings Unlimited{CEditorConfigProperties{{"malterlib_format", "malterlib"}, {"max_line_length", "OFF"}}};
					DMibExpect(Unlimited.m_nMaxColumns, ==, 0u);

					// Objective-C is read by the C++ profile, so it has no profile of its own.
					DMibExpectExceptionType(CCodeFormattingSettings(CEditorConfigProperties{{"malterlib_format", "malterlib-objc"}}), NException::CException);
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

			DMibTestSuite("BuildSystem")
			{
				DMibTestCategory("Lexer")
				{
					CStr Source = "Key `a @(F(`b)`, \"c)\")) d`\\\n\t'x{' /* { */ // }\n";
					CCodeTokenStream Stream(Source, nullptr, ECodeLanguage::mc_BuildSystem);
					DMibExpectTrue(Stream.f_IsComplete());

					TCVector<CStr> Significant;
					for (auto const &Token : Stream.f_GetTokens())
					{
						if (Token.m_Kind != ECodeTokenKind::mc_Whitespace && Token.m_Kind != ECodeTokenKind::mc_Newline)
							Significant.f_Insert("{}:{}"_f << umint(Token.m_Kind) << Stream.f_GetText(Token));
					}

					TCVector<CStr> Expected =
						{
							"{}:Key"_f << umint(ECodeTokenKind::mc_Identifier)
							, "{}:`a @(F(`b)`, \"c)\")) d`"_f << umint(ECodeTokenKind::mc_StringLiteral)
							, "{}:\\"_f << umint(ECodeTokenKind::mc_LineSplice)
							, "{}:'x{{'"_f << umint(ECodeTokenKind::mc_StringLiteral)
							, "{}:/* {{ */"_f << umint(ECodeTokenKind::mc_BlockComment)
							, "{}:// }"_f << umint(ECodeTokenKind::mc_LineComment)
						}
					;

					DMibExpect(Significant, ==, Expected);

					CStr Joined;
					for (auto const &Token : Stream.f_GetTokens())
						Joined += Stream.f_GetText(Token);

					DMibExpect(Joined, ==, Source);

					DMibExpectFalse(CCodeTokenStream("Key `a @(b`\n", nullptr, ECodeLanguage::mc_BuildSystem).f_IsComplete());
					DMibExpectFalse(CCodeTokenStream("Key \"a\n", nullptr, ECodeLanguage::mc_BuildSystem).f_IsComplete());

					// A backslash is part of the value unless it ends the line.
					DMibExpectTrue(fg_HasEquivalentCodeTokens("A `a`\\\n\t`b`\n", "A `a`\\  \n`b`\n", ECodeLanguage::mc_BuildSystem));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("A `a`\\\n`b`\n", "A `a`\n`b`\n", ECodeLanguage::mc_BuildSystem));

					// A backslash in a name escapes the character behind it, a bracket, a quote or a blank, which stays.
					fg_ExpectBuildSystemFormat
						(
							"EscapedNames"
							, "Property\n{\n\tMy\\(Key 1\n\tMy\\\"Key 2\n\tA Value\\   \n\tFast\\ Compile \"x\"\n\tC\\+\\+ 1\n}\n"
							, "Property\n{\n\tMy\\(Key 1\n\tMy\\\"Key 2\n\tA Value\\ \n\tFast\\ Compile \"x\"\n\tC\\+\\+ 1\n}\n"
						)
					;
					// The byte order mark stays where the blank line behind it goes.
					fg_ExpectBuildSystemFormat("OnlyBom", "\xEF\xBB\xBF\n\n\n", "\xEF\xBB\xBF\n");
					fg_ExpectBuildSystemFormat("LeadingBlankBehindBom", "\xEF\xBB\xBF\nProperty\n{\n\tA 1\n}\n", "\xEF\xBB\xBFProperty\n{\n\tA 1\n}\n");
				};

				DMibTestCategory("Scopes")
				{
					fg_ExpectBuildSystemFormat
						(
							"Nested"
							, "Repository\n{\n    !!Name \"A\"\n\t\t%Group \"B\"\n\t{\n  Compile.Type \"C++\"\n\t\t\t{\n!!Enabled true\n}\n  }\n}\n"
							, "Repository\n{\n\t!!Name \"A\"\n\t%Group \"B\"\n\t{\n\t\tCompile.Type \"C++\"\n\t\t{\n\t\t\t!!Enabled true\n\t\t}\n\t}\n}\n"
						)
					;
					fg_ExpectBuildSystemFormat
						(
							"Conditions"
							, "Property\n{\n\tA true\n\t{\n\t&\n\t{\n\tB 1\n\t|\n\t{\n\tC 2\n\t}\n\t}\n\t}\n}\n"
							, "Property\n{\n\tA true\n\t{\n\t\t&\n\t\t{\n\t\t\tB 1\n\t\t\t|\n\t\t\t{\n\t\t\t\tC 2\n\t\t\t}\n\t\t}\n\t}\n}\n"
						)
					;
					// A blank line between members stays whatever ends the file's lines, a lone CR too.
					{
						CStr Separated = "Property\r{\r\tA [\r\t\t1\r\r\t\t, 2\r\t]\r}\r";
						fg_ExpectBuildSystemFormat("BlankLineLoneCR", Separated, Separated);
					}
					// A scope opened behind a value on its line is left untouched.
					{
						DMibTestPath("ScopeBehindValue");
						for (CStr Value : {"\"Stuff\"", "(\"Stuff\")", "[\"Stuff\"]", "1", "-1", "Config<Enabled>"})
						{
							DMibTestPath(Value);
							auto Result = fg_AnalyzeCodeFormatting(fg_BuildSystemRequest("%Group " + Value + " {\n\tProperty\n\t{\n\t\tA 1\n\t}\n}\n"));
							DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Unsupported);
						}
					}
					fg_ExpectBuildSystemFormat
						(
							"ValueBehindKeyOrFunction"
							, "Property\n{\n\tA function(string _Key) {...: string} = {}\n\tB\\+\\+C {}\n}\n"
							, "Property\n{\n\tA function(string _Key) {...: string} = {}\n\tB\\+\\+C {}\n}\n"
						)
					;
					fg_ExpectBuildSystemFormat("BracesInText", "A \"{\" // {\nB `}` /* } */\n", "A \"{\" // {\nB `}` /* } */\n");
					// A comment in an interpolation holds none of its brackets, and the template string in it keeps its text.
					fg_ExpectBuildSystemFormat("CommentInInterpolation", "Value `@(/*)*/ `hello\n    world`)`\n", "Value `@(/*)*/ `hello\n    world`)`\n");
				};

				DMibTestCategory("Values")
				{
					// Commas behind the elements of an opened list move in front of them, and the one ending it goes.
					{
						CStr Source = "P\n{\n\tA [\n\t\t{B: 1},\n\t\t{C: 2},\n\t]\n}\n";
						CStr Expected = "P\n{\n\tA [\n\t\t{B: 1}\n\t\t, {C: 2}\n\t]\n}\n";
						DMibTestPath("TrailingComma");

						DMibExpect(fg_FormatBuildSystem(Source, "First"), ==, Expected);
						DMibExpect(fg_FormatBuildSystem(Expected, "Second"), ==, Expected);
					}
					// A member reached across a line break is joined without a blank.
					fg_ExpectBuildSystemFormat("JoinedMember", "P\n{\n\tA [\n\t\tF(\n\t\t\tB\n\t\t)\n\t\t->G()\n\t]\n}\n", "P\n{\n\tA [\n\t\tF(B)->G()\n\t]\n}\n");
					// A '<' compares where a blank follows it and reaches into its value where none does, so a joined
					// comparison keeps its blanks.
					fg_ExpectBuildSystemFormat("JoinedComparison", "P\n{\n\tA (\n\t\tB\n\t\t< C\n\t)\n}\n", "P\n{\n\tA (B < C)\n}\n");
					fg_ExpectBuildSystemFormat("JoinedSubscript", "P\n{\n\tA (\n\t\tB->F()\n\t\t<[C - 1]>\n\t)->G()\n}\n", "P\n{\n\tA (B->F()<[C - 1]>)->G()\n}\n");
					// A switch always opens, its value, error or default each on a line and each key with its value.
					fg_ExpectBuildSystemFormat
						(
							"Switch"
							, "P\n{\n\tA _Type->SwitchWithError(\"Unsupported: \", \"C++\", \"/TP\", \"C\", \"/TC\")\n\tB SwitchWithDefault(Compile.Type, false, \"C++\", true)\n"
							"\tC X->Switch(\"a\", 1, \"b\", 2)\n}\n"
							, "P\n{\n\tA _Type->SwitchWithError(\n\t\t\"Unsupported: \"\n\t\t, \"C++\", \"/TP\"\n\t\t, \"C\", \"/TC\"\n\t)\n"
							"\tB SwitchWithDefault(\n\t\tCompile.Type\n\t\t, false\n\t\t, \"C++\", true\n\t)\n\tC X->Switch(\n\t\t\"a\", 1\n\t\t, \"b\", 2\n\t)\n}\n"
						)
					;
					// A line too long for its limit opens the outermost object, array or call it holds whole: the opener ends the
					// line, each member stands one level in with its comma in front, and a member still too long opens in turn.
					{
						CStr Long;
						for (umint i = 0; i < 90; ++i)
							Long += "a";

						fg_ExpectBuildSystemFormat
							(
								"LineBreaks"
								, "P\n{\n\tA [\n\t\t{ Source: \""
								+ Long
								+ "\"->MakeAbsolute(), Name: \""
								+ Long
								+ "\" }\n\t\t, { B: 1 }\n\t]\n"
								"\tC fg_Call(\""
								+ Long
								+ "\", [\"x\", \"y\"], \""
								+ Long
								+ "\")\n\tD [\""
								+ Long
								+ "\", \""
								+ Long
								+ "\"] // Comment\n}\n"
								, "P\n{\n\tA [\n\t\t{\n\t\t\tSource: \""
								+ Long
								+ "\"->MakeAbsolute()\n\t\t\t, Name: \""
								+ Long
								+ "\"\n\t\t}\n\t\t, { B: 1 }\n\t]\n"
								"\tC fg_Call(\n\t\t\""
								+ Long
								+ "\"\n\t\t, [\"x\", \"y\"]\n\t\t, \""
								+ Long
								+ "\"\n\t)\n"
								"\tD [\n\t\t\""
								+ Long
								+ "\"\n\t\t, \""
								+ Long
								+ "\"\n\t] // Comment\n}\n"
							)
						;
						// Inside a bracket a line break is whitespace, so operators spelled with several punctuators split a value
						// there, its own operators before a bracket it holds and what follows one a level in, except a grouping
						// parenthesis's operands, which stand at its level. A member chain gives at every member from the first
						// access on what a bracket yields, unless opening a bracket makes it fit. A line break ends a statement, so
						// a statement opens a bracket instead. A string ending a line counts for its width here.
						fg_ExpectBuildSystemFormat
							(
								"Operators"
								, "P\n{\n\tA (F() == \""
								+ Long
								+ "\" || F() == \""
								+ Long
								+ "\")\n"
								"\tB X->Switch(\"k\", (C <==> \"c\") ? \""
								+ Long
								+ "\" : \""
								+ Long
								+ "\")\n"
								"\tC G(\""
								+ Long
								+ "\", \""
								+ Long
								+ "\") / \"d\"\n"
								"\tD [F(\""
								+ Long
								+ "\") || F(\""
								+ Long
								+ "\")]\n"
								"\tE (Y->F(\""
								+ Long
								+ "\")->G(\""
								+ Long
								+ "\")->H(\""
								+ Long
								+ "\"))\n"
								"\tF [G(\""
								+ Long
								+ "\", \""
								+ Long
								+ "\")->H()]\n"
								"\tG (F(\""
								+ Long
								+ "\") + F(\""
								+ Long
								+ "\") * F(\""
								+ Long
								+ "\"))\n}\n"
								, "P\n{\n\tA (\n\t\tF() == \""
								+ Long
								+ "\"\n\t\t|| F() == \""
								+ Long
								+ "\"\n\t)\n"
								"\tB X->Switch(\n\t\t\"k\", (C <==> \"c\")\n\t\t\t? \""
								+ Long
								+ "\"\n\t\t\t: \""
								+ Long
								+ "\"\n\t)\n"
								"\tC G(\n\t\t\""
								+ Long
								+ "\"\n\t\t, \""
								+ Long
								+ "\"\n\t) / \"d\"\n"
								"\tD [\n\t\tF(\""
								+ Long
								+ "\")\n\t\t\t|| F(\""
								+ Long
								+ "\")\n\t]\n"
								"\tE (\n\t\tY->F(\""
								+ Long
								+ "\")\n\t\t\t->G(\""
								+ Long
								+ "\")\n\t\t\t->H(\""
								+ Long
								+ "\")\n\t)\n"
								"\tF [\n\t\tG(\n\t\t\t\""
								+ Long
								+ "\"\n\t\t\t, \""
								+ Long
								+ "\"\n\t\t)->H()\n\t]\n"
								"\tG (\n\t\tF(\""
								+ Long
								+ "\")\n\t\t+ F(\""
								+ Long
								+ "\")\n\t\t\t* F(\""
								+ Long
								+ "\")\n\t)\n}\n"
							)
						;
					}
					fg_ExpectBuildSystemFormat("Array", "P\n{\n\tPaths =+ [\n\"a\"\n        , \"b\"\n\t\t]\n}\n", "P\n{\n\tPaths =+ [\n\t\t\"a\"\n\t\t, \"b\"\n\t]\n}\n");
					// An array written across lines has an element on each line, and a call inside it that fits is joined;
					// a bracket opening behind another on its line takes a line of its own.
					fg_ExpectBuildSystemFormat
						(
							"SharedLevel"
							, "P\n{\n\tFiles: [\"a\", F(\n\t\t\t\"b\"\n\t\t\t, \"c\"\n\t\t)]\n}\n"
							, "P\n{\n\tFiles: [\n\t\t\"a\"\n\t\t, F(\"b\", \"c\")\n\t]\n}\n"
						)
					;
					fg_ExpectBuildSystemFormat("AdjacentClosers", "P\n{\n\tA !([\n\t\t\t\"a\"\n\t\t]->F(B\n\t\t))\n}\n", "P\n{\n\tA !(\n\t\t[\n\t\t\t\"a\"\n\t\t]->F(B)\n\t)\n}\n");
					// Objects written across lines in an array meet on the line one closes on.
					fg_ExpectBuildSystemFormat
						(
							"ObjectsInArray"
							, "P\n{\n\tL =+ [{\n\t\tA: 1\n\t}\n\t, {\n\t\tB: 2\n\t}\n\t]\n}\n"
							, "P\n{\n\tL =+ [\n\t\t{\n\t\t\tA: 1\n\t\t}, {\n\t\t\tB: 2\n\t\t}\n\t]\n}\n"
						)
					;
				};

				DMibTestCategory("Continuation")
				{
					fg_ExpectBuildSystemFormat
						(
							"ValueColumn"
							, "A\n{\n\tB\n\t{\n\t\tContents\t`a\\n`\\\n\t\t`b @(`c`->F())\\n`\\\n            `d`\n\t}\n}\n"
							, "A\n{\n\tB\n\t{\n\t\tContents\t`a\\n`\\\n\t\t\t\t\t`b @(`c`->F())\\n`\\\n\t\t\t\t\t`d`\n\t}\n}\n"
						)
					;
					// A value off a tab stop is aligned with it, the tabs filled out with spaces.
					fg_ExpectBuildSystemFormat("OffTabStop", "P\n{\n\tScript =+ `a`\\\n\t\t`b`\n\t{\n\t\tC 1\n\t}\n}\n", "P\n{\n\tScript =+ `a`\\\n\t\t\t  `b`\n\t{\n\t\tC 1\n\t}\n}\n");
				};

				DMibTestCategory("BlankLines")
				{
					fg_ExpectBuildSystemFormat("Runs", "\n\nA\n{\n\n\tB 1\n\n\n\tC 2\n\t\n}\n\n\n", "A\n{\n\tB 1\n\n\tC 2\n}\n");
					fg_ExpectBuildSystemFormat("UnterminatedEnd", "A 1\n\n  ", "A 1\n");
					fg_ExpectBuildSystemFormat("CommentsSeparate", "A\n{\n\t// c\n\n\tB 1\n}\n", "A\n{\n\t// c\n\n\tB 1\n}\n");
					// A template string's expression can stand behind blanks, '@@' is an '@' of its text, and a quoted string runs over
					// lines: the strings' lines keep their bytes.
					CStr Strings = "A\n{\n\tB `@ (`first\n        second`)`\n\tC `a @@(b`\n\tD \"x\n   y\"\n}\n";
					fg_ExpectBuildSystemFormat("StringSyntax", Strings, Strings);
				};

				DMibTestCategory("Comments")
				{
					// A comment at the start of a line comments out what it stands in front of.
					fg_ExpectBuildSystemFormat
						(
							"CommentedOut"
							, "A\n{\n//\tB 1\n  // Note\n/*\tC 1\n    D 2 */\n\t\t/* E\n    F */\n}\n"
							, "A\n{\n//\tB 1\n\t// Note\n/*\tC 1\n    D 2 */\n\t/* E\n    F */\n}\n"
						)
					;
					fg_ExpectBuildSystemFormat
						(
							"Disabled"
							, "A\n{\n// malterlib-format off\n      B 1\n// malterlib-format on\n   C 1\n}\n"
							, "A\n{\n// malterlib-format off\n      B 1\n// malterlib-format on\n\tC 1\n}\n"
						)
					;
				};

				DMibTestCategory("Unsupported")
				{
					auto fExpectUnsupported = [](CStr const &_Case, CStr const &_Source, CStr const &_Explanation)
						{
							DMibTestPath(_Case);

							auto Result = fg_AnalyzeCodeFormatting(fg_BuildSystemRequest(_Source));
							DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Unsupported);
							DMibExpect(Result.m_Explanation.f_Find(_Explanation), >=, 0);
							DMibExpectTrue(Result.m_Edits.f_IsEmpty());
						}
					;

					fExpectUnsupported("ExtraCloser", "A\n{\n}\n}\n", "'}' at line 4 closes no bracket");
					fExpectUnsupported("NeverClosed", "A\n{\n\tB [\n", "never closed");
					fExpectUnsupported("MismatchedCloser", "A\n{\n\tB [ 1, 2 )\n}\n", "')' at line 3 closes the '[' at line 3");
					fExpectUnsupported("Unterminated", "A `b\n", "ends inside a comment or literal");
				};
			};

			DMibTestSuite("C")
			{
				// A word only C++ reserves is a name in C, a '<' is a comparison, and a bracket subscripts or designates. A raw
				// string is one, as GNU C reads it.
				fg_ExpectCFormat
					(
						"CppWordsAreNames"
						, "struct CNode *new;\nint class = 1;\nvoid f(int template)\n{\n\tdelete(new);\n\tx = a<b>(c);\n\ty = this->m_A;\n}\n"
						"int g_A[] = {[0] = 1, [1] = 2};\nchar const *g_p = R\"x(a\"b)x\";\n"
						, "struct CNode *new;\nint class = 1;\nvoid f(int template)\n{\n\tdelete(new);\n\tx = a < b > (c);\n\ty = this->m_A;\n}\n\n"
						"int g_A[] = {[0] = 1, [1] = 2};\nchar const *g_p = R\"x(a\"b)x\";\n"
					)
				;
				// A branch only C++ compiles is C++ in a C file, the else of '#ifndef __cplusplus' included, while one another
				// compiler can take as well stays C.
				fg_ExpectCFormat
					(
						"CppBranches"
						, "#ifdef __cplusplus\ntemplate<class T> T *f(T* p) { return new(p) T(g<T>(p)); }\n#endif\n"
						"#if defined(__cplusplus) && __cplusplus >= 201103L\nusing CA = int;\n#endif\n"
						"#ifndef __cplusplus\nint *new;\n#else\nint *p = new int;\n#endif\n"
						"#if defined(__cplusplus) || defined(_MSC_VER)\nx = a<b>(c);\n#endif\n"
						, "#ifdef __cplusplus\ntemplate <class T> T *f(T *p)\n{\n\treturn new(p) T(g<T>(p));\n}\n#endif\n"
						"#if defined(__cplusplus) && __cplusplus >= 201103L\nusing CA = int;\n#endif\n"
						"#ifndef __cplusplus\nint *new;\n#else\nint *p = new int;\n#endif\n"
						"#if defined(__cplusplus) || defined(_MSC_VER)\nx = a < b > (c);\n#endif\n"
					)
				;
				// '__cplusplus' is 0 in C, so a branch that compares it as less than a version is C as well.
				fg_ExpectCFormat
					(
						"CppNotProven"
						, "#if __cplusplus < 201103L\nx = a<b>(c);\n#endif\n#if 0 == __cplusplus\nx = a<b>(c);\n#endif\n"
						"#if ! (defined(__cplusplus))\nx = a<b>(c);\n#endif\n#if 1 /* __cplusplus */\nx = a<b>(c);\n#endif\n"
						"#if defined(__cplusplus) && X || defined(TEST)\nx = a<b>(c);\n#endif\n#if defined(__cplusplus) /* a */ || defined(TEST)\nx = a<b>(c);\n#endif\n"
						"#if __cplusplus >= 0\nx = a<b>(c);\n#endif\n"
						, "#if __cplusplus < 201103L\nx = a < b > (c);\n#endif\n#if 0 == __cplusplus\nx = a < b > (c);\n#endif\n"
						"#if ! (defined(__cplusplus))\nx = a < b > (c);\n#endif\n#if 1 /* __cplusplus */\nx = a < b > (c);\n#endif\n"
						"#if defined(__cplusplus) && X || defined(TEST)\nx = a < b > (c);\n#endif\n#if defined(__cplusplus) /* a */ || defined(TEST)\nx = a < b > (c);\n#endif\n"
						"#if __cplusplus >= 0\nx = a < b > (c);\n#endif\n"
					)
				;
				// C has no declaration statement, so a guarded statement that may be one keeps its braces.
				fg_ExpectCFormat
					(
						"DeclarationKeepsBraces"
						, "void f(void)\n{\n\tif (a)\n\t{\n\t\t__typeof__(a) w = a;\n\t}\n\tif (a)\n\t{\n\t\tint v = g();\n\t}\n"
						"\tif (b)\n\t{\n\t\tCType *p = g();\n\t}\n\tif (c)\n\t{\n\t\th();\n\t}\n}\n"
						, "void f(void)\n{\n\tif (a)\n\t{\n\t\t__typeof__(a) w = a;\n\t}\n\tif (a)\n\t{\n\t\tint v = g();\n\t}\n"
						"\tif (b)\n\t{\n\t\tCType *p = g();\n\t}\n\tif (c)\n\t\th();\n}\n"
					)
				;
				// C90 leaves out the 'int' of a declaration, whose name the qualifier then stands in front of.
				CStr ProtocolDeclaration = "void f(void)\n{\n\tif (b)\n\t{\n\t\tid<P> value;\n\t}\n}\n";
				fg_ExpectCFormat("ProtocolDeclarationKeepsBraces", ProtocolDeclaration, ProtocolDeclaration);
				// Objective-C's protocols and generics qualify a type in brackets that hold names only, where a comparison compares.
				fg_ExpectCFormat
					(
						"ObjectiveCQualifiers"
						, "void f(void)\n{\n\tid <A> a = (id < A >)b;\n\tNSArray< NSString * > *pList;\n\tint k = a<b;\n\tx = f(a<b, c);\n\tif (z < a > y)\n\t\tg();\n}\n"
						, "void f(void)\n{\n\tid<A> a = (id<A>)b;\n\tNSArray<NSString *> *pList;\n\tint k = a < b;\n\tx = f(a < b, c);\n\tif (z < a > y)\n\t\tg();\n}\n"
					)
				;
				fg_ExpectCFormat("ImplicitIntKeepsQualifier", "static const value = 1;\n", "static const value = 1;\n");
				CStr Attributed = "void f(void)\n{\n\tif (b)\n\t{\n\t\tT [[]] x;\n\t}\n}\n";
				fg_ExpectCFormat("AttributedTypeKeepsBraces", Attributed, Attributed);
				fg_ExpectCFormat("ImplicitIntListKeepsQualifier", "static const x, y;\n", "static const x, y;\n");
				CStr Int64 = "void f(void)\n{\n\tif (b)\n\t{\n\t\t__int64(x);\n\t}\n}\n";
				fg_ExpectCFormat("MicrosoftIntegerKeepsBraces", Int64, Int64);
				fg_ExpectCFormat("ImplicitIntArrayKeepsQualifier", "static const values[3] = {1, 2, 3};\n", "static const values[3] = {1, 2, 3};\n");
				CStr TypeofUnqual = "void f(void)\n{\n\tif (a)\n\t{\n\t\t__typeof_unqual__(a) w = a;\n\t}\n}\n";
				fg_ExpectCFormat("TypeofUnqualKeepsBraces", TypeofUnqual, TypeofUnqual);
				CStr GnuSigned = "void f(int a)\n{\n\tif (a)\n\t{\n\t\t__signed__(x);\n\t}\n}\n";
				fg_ExpectCFormat("GnuKeywordKeepsBraces", GnuSigned, GnuSigned);
				CStr ExtInt = "void f(void)\n{\n\tif (b)\n\t{\n\t\t_ExtInt(8) v = 1;\n\t}\n}\n";
				fg_ExpectCFormat("ExtIntKeepsBraces", ExtInt, ExtInt);
				fg_ExpectCFormat
					(
						"AlignasKeepsBraces"
						, "void f(void)\n{\n\tif (b)\n\t{\n\t\talignas(16) int x;\n\t}\n}\n"
						, "void f(void)\n{\n\tif (b)\n\t{\n\t\talignas(16) int x;\n\t}\n}\n"
					)
				;
				fg_ExpectCFormat
					(
						"ParenthesizedDeclaratorKeepsBraces"
						, "void f(void)\n{\n\tif (c)\n\t{\n\t\tT (*x)(int);\n\t}\n\tif (c)\n\t{\n\t\tT ((x));\n\t}\n\tif (c)\n\t{\n\t\tT (x)[2];\n\t}\n}\n"
						, "void f(void)\n{\n\tif (c)\n\t{\n\t\tT (*x)(int);\n\t}\n\tif (c)\n\t{\n\t\tT((x));\n\t}\n\tif (c)\n\t{\n\t\tT(x)[2];\n\t}\n}\n"
					)
				;
				// The qualifiers in an array parameter's bracket qualify the pointer it stands for, and stay.
				// A macro's parenthesis with a name behind it declares, which no C guard holds without braces.
				CStr AlignedDeclaration = "void f(void)\n{\n\tif (a)\n\t{\n\t\tALIGN(16) int x;\n\t}\n}\n";
				fg_ExpectCFormat("MacroPrefixedDeclaration", AlignedDeclaration, AlignedDeclaration);
				// Behind another type word 'bool' is a name in C, which the qualifier stays in front of.
				fg_ExpectCFormat("BoolAsName", "const int bool = 1;\n", "int const bool = 1;\n");
				// A compiler's own floating type declares, so the braces around one stay.
				fg_ExpectCFormat("ExtendedFloatDeclaration", "void f(int b) { if (b) { _Float16(x); } }\n", "void f(int b)\n{\n\tif (b)\n\t{\n\t\t_Float16(x);\n\t}\n}\n");
				// Parentheses around the condition still make its branch C++'s, where a qualifier stays behind a class.
				fg_ExpectCFormat
					(
						"ParenthesizedCppGuard"
						, "#if (defined(__cplusplus))\nclass CThing {} const instance = {};\n#endif\n"
						, "#if (defined(__cplusplus))\nclass CThing\n{\n} const instance = {};\n#endif\n"
					)
				;
				// A literal in a condition is no comment, so the '||' behind it still lets C meet the branch.
				{
					DMibTestPath("LiteralInCondition");
					CStr Name;
					for (umint i = 0; i < 200; ++i)
						Name += "W";

					auto Formatted = fg_FormatC("#if __cplusplus && __has_include(\"foo//bar.h\") || defined(_WIN32)\nunsigned long long fg_" + Name + "(int a, int b);\n#endif\n", "Format");
					DMibExpect(Formatted.f_Find("->"), ==, -1);
				}
				// A directive spelled with a comment or a splice in front of its keyword still ends the C++ branch, so what follows
				// it stays C: no return type moves behind an arrow there.
				for (CStr Else : {"#/**/else", "#el\\\nse"})
				{
					DMibTestPath(Else == "#/**/else" ? "CommentedElse" : "SplicedElse");
					CStr Name;
					for (umint i = 0; i < 200; ++i)
						Name += "W";

					auto Formatted = fg_FormatC("#ifdef __cplusplus\nint x;\n" + Else + "\nunsigned long long fg_" + Name + "(int a, int b);\n#endif\n", "Format");
					DMibExpect(Formatted.f_Find("->"), ==, -1);
				}
				// A condition C can meet holds no C++: '1 or 1' with <iso646.h>, and 'defined' a splice joins to the name behind it.
				{
					CEditorConfigProperties Narrow = fg_MalterlibProperties();
					Narrow["malterlib_format"] = "malterlib-c";
					Narrow["max_line_length"] = "60";
					CStr Declaration = "unsigned long long f_GetSelectedValueWithManyLetters(int _Value);\n";
					CStr Split = "unsigned long long f_GetSelectedValueWithManyLetters\n\t(\n\t\tint _Value\n\t)\n;\n";
					fg_ExpectFormatWith("CConditionAlternative", Narrow, "#if __cplusplus && 1 or 1\n" + Declaration + "#endif\n", "#if __cplusplus && 1 or 1\n" + Split + "#endif\n");
					fg_ExpectFormatWith("CConditionSplicedName", Narrow, "#if defined\\\n__cplusplus\n" + Declaration + "#endif\n", "#if defined\\\n__cplusplus\n" + Split + "#endif\n");
				}
				// C's character types are typedefs, which a variable can be named as.
				// A cast still names one as a type.
				fg_ExpectCFormat
					(
						"CharacterTypeNamesAsVariables"
						, "const int wchar_t = 1;\nconst int char8_t = 2;\nconst int char16_t = 3;\nconst int char32_t = 4;\nint x = (wchar_t)-1;\n"
						, "int const wchar_t = 1;\nint const char8_t = 2;\nint const char16_t = 3;\nint const char32_t = 4;\nint x = (wchar_t)-1;\n"
					)
				;
				// Every branch behind one compiled only without C++ is C++, an '#elif' between them included.
				fg_ExpectCFormat
					(
						"CppBranchesBehindElif"
						, "#ifndef __cplusplus\nint a;\n#elif 0\nint b;\n#else\ntemplate <typename T> struct S {};\n#endif\n"
						, "#ifndef __cplusplus\nint a;\n#elif 0\nint b;\n#else\ntemplate <typename T> struct S\n{\n};\n#endif\n"
					)
				;
				// So does an initializer behind it, which no call's result takes in C.
				fg_ExpectCFormat
					(
						"InitializedParenthesizedDeclaration"
						, "typedef int T;\nvoid f(int b)\n{\n\tif (b)\n\t{\n\t\tT (x) = 1;\n\t}\n}\n"
						, "typedef int T;\nvoid f(int b)\n{\n\tif (b)\n\t{\n\t\tT(x) = 1;\n\t}\n}\n"
					)
				;
				fg_ExpectCFormat("ArrayParameterQualifiers", "void f(int N, int a[static const N]);\nconst int g_A = 1;\n", "void f(int N, int a[static const N]);\nint const g_A = 1;\n");
				// C has no trailing return type to move a return type behind.
				{
					CStr Params;
					for (umint i = 0; i < 6; ++i)
						Params += "{}struct CLongParameterTypeName{} *_pParam{}"_f << (i ? ", " : "") << i << i;

					CStr Split = CStr(Params).f_Replace(", ", "\n\t, ");

					fg_ExpectCFormat
						(
							"NoTrailingReturn"
							, "struct CResultWithALongName *fg_CreateTheResult(" + Params + ");\n"
							, "struct CResultWithALongName *fg_CreateTheResult\n\t(\n\t\t" + CStr(Split).f_Replace("\n\t,", "\n\t\t,") + "\n\t)\n;\n"
						)
					;
				}
			};

			DMibTestSuite("ObjectiveC")
			{
				CStr Long;
				for (umint i = 0; i < 80; ++i)
					Long += "a";

				DMibTestCategory("Lexer")
				{
					CCodeTokenStream Stream("@autoreleasepool @\"Text\" @[x]");

					TCVector<CStr> Significant;
					for (auto const &Token : Stream.f_GetTokens())
					{
						if (Token.m_Kind != ECodeTokenKind::mc_Whitespace)
							Significant.f_Insert("{}:{}"_f << umint(Token.m_Kind) << Stream.f_GetText(Token));
					}

					TCVector<CStr> Expected =
						{
							"{}:@autoreleasepool"_f << umint(ECodeTokenKind::mc_Identifier)
							, "{}:@\"Text\""_f << umint(ECodeTokenKind::mc_StringLiteral)
							, "{}:@"_f << umint(ECodeTokenKind::mc_Punctuator)
							, "{}:["_f << umint(ECodeTokenKind::mc_Punctuator)
							, "{}:x"_f << umint(ECodeTokenKind::mc_Identifier)
							, "{}:]"_f << umint(ECodeTokenKind::mc_Punctuator)
						}
					;

					DMibExpect(Significant, ==, Expected);
				};

				// A message send writes its receiver and selector parts apart and each keyword against its ':', with
				// the argument one space behind it; a conditional inside keeps its colon, and '@selector' its name.
				DMibTestCategory("Sends")
				{
					fg_ExpectFormat
						(
							"Spacing"
							, "void f()\n{\n\t[NSApp sendEvent:pEvent];\n\tNSSound *pSound = [[NSSound alloc ]initWithData:pData];\n"
							"\t[NSApp hide:(__bridge NSView*)_pView];\n\t[pMenu addItem:[NSMenuItem separatorItem] atIndex:0];\n"
							"\tid Value = [pDictionary objectForKey:bFirst ? @\"a\" : @\"b\"];\n\tSEL Selector = @selector(setObject:forKey:);\n"
							"\tNSArray *pArray = @[@\"a\", @\"b\"];\n}\n"
							, "void f()\n{\n\t[NSApp sendEvent: pEvent];\n\tNSSound *pSound = [[NSSound alloc] initWithData: pData];\n"
							"\t[NSApp hide: (__bridge NSView *)_pView];\n\t[pMenu addItem: [NSMenuItem separatorItem] atIndex: 0];\n"
							"\tid Value = [pDictionary objectForKey: bFirst ? @\"a\" : @\"b\"];\n\tSEL Selector = @selector(setObject:forKey:);\n"
							"\tNSArray *pArray = @[@\"a\", @\"b\"];\n}\n"
						)
					;
					// A send too long for its line opens its bracket, the receiver and each selector part on a line of
					// its own; one that opens its statement ends it with the terminator behind the bracket.
					fg_ExpectFormat
						(
							"Split"
							, "void f()\n{\n\t[pManager setHandler: "
							+ Long
							+ " andSelector: @selector(f:) forClass: "
							+ Long
							+ " andID: kID];\n"
							"\tNSWindow *pWindow = [[NSWindow alloc] initWithRect: "
							+ Long
							+ " styleMask: "
							+ Long
							+ " defer: NO];\n}\n"
							, "void f()\n{\n\t[\n\t\tpManager\n\t\tsetHandler: "
							+ Long
							+ "\n\t\tandSelector: @selector(f:)\n\t\tforClass: "
							+ Long
							+ "\n\t\tandID: kID\n\t];\n"
							"\tNSWindow *pWindow =\n\t\t[\n\t\t\t[NSWindow alloc]\n\t\t\tinitWithRect: "
							+ Long
							+ "\n\t\t\tstyleMask: "
							+ Long
							+ "\n\t\t\tdefer: NO\n\t\t]\n\t;\n}\n"
						)
					;
				};

				// A block passed along is laid out as a lambda is: '^' behind the comma and the brace below it.
				DMibTestCategory("Blocks")
				{
					fg_ExpectFormat
						(
							"Arguments"
							, "void f()\n{\n\tdispatch_async(pQueue, ^{\n\t\t[NSApp run];\n\t});\n"
							"\t[pMachine startWithCompletionHandler:^(NSError *_pError){\n\t\tfg_Run();\n\t}];\n}\n"
							, "void f()\n{\n\tdispatch_async\n\t\t(\n\t\t\tpQueue\n\t\t\t, ^\n\t\t\t{\n\t\t\t\t[NSApp run];\n\t\t\t}\n\t\t)\n\t;\n"
							"\t[\n\t\tpMachine\n\t\tstartWithCompletionHandler: ^(NSError *_pError)\n\t\t{\n\t\t\tfg_Run();\n\t\t}\n\t];\n}\n"
						)
					;
				};

				// A section's line writes its base, category and protocols apart, a visibility stands at the brace's
				// level, and '@end' is a statement of its own. A method writes its sign apart, a type against the name
				// behind it and each keyword against its ':', on one line where it fits and at each keyword part
				// behind the first, one level in, where it does not.
				DMibTestCategory("Sections")
				{
					// A comment inside a method's signature stays behind the part of it it trails, which keeps the signature on its
					// lines.
					{
						CStr Comment;
						for (umint i = 0; i < 150; ++i)
							Comment += "c";

						fg_ExpectFormat
							(
								"CommentInSignature"
								, CStr("@interface CFoo : NSObject\n-(void)m1;\n-(void)m3:(int)block\n  ATTR(zero_argument, 1) // ~\n  ATTR2(swift_private, 1);\n@end\n")
								.f_Replace("~", Comment)
								, CStr("@interface CFoo : NSObject\n- (void)m1;\n- (void)m3: (int)block ATTR(zero_argument, 1) // ~\n  ATTR2(swift_private, 1);\n@end\n")
								.f_Replace("~", Comment)
								, false
							)
						;
					}
					fg_ExpectFormat
						(
							"Interface"
							, "@interface CFoo:NSObject<CDelegate>\n{\n@public\n\tint m_Value;\n}\n-(void)f_Set:(int)_Value with:(int)_Other;\n@end\n\n"
							"@interface CBar(CCategory)\n@end\n\nnamespace NFoo\n{\n}\n"
							, "@interface CFoo : NSObject <CDelegate>\n{\n@public\n\tint m_Value;\n}\n- (void)f_Set: (int)_Value with: (int)_Other;\n@end\n\n"
							"@interface CBar (CCategory)\n@end\n\nnamespace NFoo\n{\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"Implementation"
							, "@implementation CFoo\n\n-(void)f_Set:(int)_Value\n\twith:(int)_Other\n{\n\tm_Value = _Value;\n}\n\n"
							"- (void)f_"
							+ Long
							+ ": (int)_Value with"
							+ Long
							+ ": (int)_Other\n{\n}\n\n@end\n"
							, "@implementation CFoo\n\n- (void)f_Set: (int)_Value with: (int)_Other\n{\n\tm_Value = _Value;\n}\n\n"
							"- (void)f_"
							+ Long
							+ ": (int)_Value\n\twith"
							+ Long
							+ ": (int)_Other\n{\n}\n\n@end\n"
						)
					;
				};

				// C++ reads the same under the Objective-C profile: a subscript and a capture list holding a cast are no message
				// sends, while a message send behind a clause's condition still is.
				DMibTestCategory("CppBrackets")
				{
					CStr Cpp = "void f()\n{\n\tauto fA = [&, pA = (CFoo *)nullptr]() mutable -> CFoo &\n\t\t{\n\t\t\treturn *pA;\n\t\t}\n\t;\n"
						"\tg_Names[(int)_Method] = g_Wildcards[Path / \"^*\"_f];\n}\n"
					;
					fg_ExpectFormat("Unchanged", Cpp, Cpp);

					CStr Long = "ArgumentArgumentArgumentArgumentArgumentArgumentArgumentArgumentArgumentArgumentArgumentArgument";
					fg_ExpectFormat
						(
							"ClauseSend"
							, "void f()\n{\n\tif (bValid)\n\t{\n\t\t[pObject f_Set: " + Long + " with: " + Long + "];\n\t}\n}\n"
							, "void f()\n{\n\tif (bValid)\n\t{\n\t\t[\n\t\t\tpObject\n\t\t\tf_Set: " + Long + "\n\t\t\twith: " + Long + "\n\t\t];\n\t}\n}\n"
						)
					;
				};

				// A section's instance variables opened on its header's line end the header there, and the section's end
				// takes a line of its own behind them, as a statement does behind a function's body.
				DMibTestCategory("InlineHeader")
				{
					fg_ExpectFormat("InstanceVariables", "@interface CFoo : NSObject <CProto> { int m_X; } @end\n", "@interface CFoo : NSObject <CProto>\n{\n\tint m_X;\n}\n@end\n");
					fg_ExpectFormat("BehindFunctionBody", "void f()\n{\n}  int g_A;\n", "void f()\n{\n}\n\nint g_A;\n");
					// A section's word starts a statement of its own, also behind the attribute above it.
					fg_ExpectFormat
						(
							"SectionBehindAttribute"
							, "__attribute__((objc_root_class))\n@interface CA\n@property (weak) id m_A;\n@end\n"
							, "__attribute__((objc_root_class))\n@interface CA\n@property (weak) id m_A;\n@end\n"
						)
					;
					// A block literal's '^' introduces it and is no operator the statement splits at, and a cast hugs it.
					fg_ExpectFormat
						(
							"BlockLiteral"
							, "void f()\n{\n\t(void)^(int _A,\n\t\tint _B, // B\n\t\tint _C) {\n\t};\n}\n"
							, "void f()\n{\n\t(void)^\n\t\t(\n\t\t\tint _A\n\t\t\t, int _B // B\n\t\t\t, int _C\n\t\t)\n\t\t{\n\t\t}\n\t;\n}\n"
							, false
						)
					;
					// A statement opening with a block literal has its body at its own level, as one opening with a lambda does.
					fg_ExpectFormat("OpensWithBlockLiteral", "void f()\n{\n\t^{\n\t\tg();\n\t}();\n}\n", "void f()\n{\n\t^\n\t{\n\t\tg();\n\t}();\n}\n");
					// A header too long for its line stays on it, where the builder reads it, and is reported.
					CStr Protocols;
					for (umint i = 0; i < 12; ++i)
						Protocols += "{}CProtocolWithALongName{}"_f << (i ? ", " : "") << i;

					CStr LongHeader = "@interface CFoo : NSObject <" + Protocols + ">\n@end\n";
					fg_ExpectFormat("LongHeader", LongHeader, LongHeader);
					fg_ExpectFormat("MethodOnHeaderLine", "@protocol CFoo  - (BOOL)f_Equal:(id)_pObject; @end\n", "@protocol CFoo\n- (BOOL)f_Equal: (id)_pObject;\n@end\n");
					// A header broken inside its parameter list goes on to the category behind it.
					fg_ExpectFormat("HeaderBrokenInList", "@interface CFoo<t_CA : NSObject *,\n\tt_CB : id> (CBar)\n@end\n", "@interface CFoo <t_CA : NSObject *, t_CB : id> (CBar)\n@end\n");
					// The type a 'typedef' starts with takes no comparison: its '<' opens the protocols it is qualified with.
					fg_ExpectFormat("TypedefProtocols", "typedef Foo <Delegate> Bar;\n", "typedef Foo<Delegate> Bar;\n");
					// A header written over several lines is joined and a comment too long behind it moves above it. A comment
					// inside it keeps it on its lines, and none of its comments moves, which would join it.
					CStr Words;
					for (umint i = 0; i < 34; ++i)
						Words += "word ";

					Words = Words.f_Trim();

					fg_ExpectFormat
						(
							"HeaderJoinedCommentAbove"
							, "@interface CFoo<t_CA : NSObject *,\n\tt_CB : id> (CBar) // " + Words + "\n@end\n"
							, "// " + Words + "\n@interface CFoo <t_CA : NSObject *, t_CB : id> (CBar)\n@end\n"
							, false
						)
					;
					fg_ExpectFormat
						(
							"HeaderCommentsKept"
							, "@interface CFoo<t_CA : NSObject *, // " + Words + "\n\tt_CB : id> (CBar) // " + Words + "\n@end\n"
							, "@interface CFoo <t_CA : NSObject * // " + Words + "\n\t, t_CB : id> (CBar) // " + Words + "\n@end\n"
							, false
						)
					;
				};

				DMibTestCategory("Statements")
				{
					fg_ExpectFormat
						(
							"Keywords"
							, "void f()\n{\n\t@autoreleasepool\n\t{\n\t\t[NSApp run];\n\t}\n\t@try\n\t{\n\t\t[pObject f];\n\t}\n"
							"\t@catch(NSException *_pException)\n\t{\n\t}\n\t@synchronized(self)\n\t{\n\t\tfg_Run();\n\t}\n}\n"
							, "void f()\n{\n\t@autoreleasepool\n\t{\n\t\t[NSApp run];\n\t}\n\t@try\n\t{\n\t\t[pObject f];\n\t}\n"
							"\t@catch (NSException *_pException)\n\t{\n\t}\n\t@synchronized (self)\n\t{\n\t\tfg_Run();\n\t}\n}\n"
						)
					;
				};
			};

			// With the project's alignment options the '=' of a run of enumerators, and the comments behind a run of
			// enumerators or members, stand at the first tab stop past the run's longest line.
			DMibTestSuite("Alignment")
			{
				auto fAligned = [](CStr const &_Source, bool _bTabs = true, umint _nMaximumColumn = 0)
					{
						auto Request = fg_Request(_Source);
						CCodeFormattingStyle Style = *fg_MalterlibStyle();
						Style.m_bAlignTrailingComments = true;
						Style.m_bAlignEnumValues = true;
						Style.m_nAlignTrailingCommentsMaximumColumn = _nMaximumColumn;
						Request.m_pStyle = fg_Construct(fg_Move(Style));

						if (!_bTabs)
						{
							auto Properties = fg_MalterlibProperties();
							Properties["indent_style"] = "space";
							Request.m_Settings = CCodeFormattingSettings(Properties);
						}

						return Request;
					}
				;

				auto fExpect = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected, bool _bTabs = true, umint _nMaximumColumn = 0)
					{
						DMibTestPath(_Case);

						auto fFormat = [&](CStr const &_Text, CStr const &_Stage)
							{
								DMibTestPath(_Stage);
								auto Result = fg_AnalyzeCodeFormatting(fAligned(_Text, _bTabs, _nMaximumColumn));
								DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);

								return fg_ApplyCodeFormattingEdits(_Text, Result.m_Edits);
							}
						;

						auto Formatted = fFormat(_Source, "First");
						DMibExpect(Formatted, ==, _Expected);
						DMibExpect(fFormat(Formatted, "Second"), ==, _Expected);
					}
				;

				auto fExpectReported = [&](CStr const &_Case, CStr const &_Source)
					{
						DMibTestPath(_Case);

						umint nReports = 0;
						for (auto const &Diagnostic : fg_AnalyzeCodeFormatting(fAligned(_Source)).m_Diagnostics)
							nReports += Diagnostic.m_Rule == "line-length";

						DMibExpect(nReports, ==, 1u);
					}
				;

				DMibTestCategory("Enum")
				{
					fExpect
						(
							"Values"
							, "enum class EEvent : uint32\n{\n\tmc_None = 0\n\t, mc_Read = 1 << 0 // Readable\n\t, mc_WriteClosed = 1 << 4 // Closed\n"
							"\t, mc_Error = 1 << 5\n};\n"
							, "enum class EEvent : uint32\n{\n\tmc_None          = 0\n\t, mc_Read        = 1 << 0 // Readable\n\t, mc_WriteClosed = 1 << 4 // Closed\n"
							"\t, mc_Error       = 1 << 5\n};\n"
						)
					;
					// A comment in front of the '=' keeps its entry out of the run.
					fExpect
						(
							"CommentBeforeValue"
							, "enum E\n{\n\tA = 1\n\t, B /* note */ = 2\n\t, CLonger = 3\n};\n"
							, "enum E\n{\n\tA         = 1\n\t, B /* note */ = 2\n\t, CLonger = 3\n};\n"
						)
					;
					// A comment continued on the lines below keeps its entry in the run, and its lines follow it.
					fExpect
						(
							"Continued"
							, "enum EStatus\n{\n\tmc_A = 1 // First\n\t, mc_Longer = 2 // Second\n\t                // continued\n\t, mc_B = 3 // Third\n};\n"
							, "enum EStatus\n{\n\tmc_A        = 1 // First\n\t, mc_Longer = 2 // Second\n\t                // continued\n\t, mc_B      = 3 // Third\n};\n"
						)
					;
					// A comment over several lines behind an enumerator's comma becomes line comments, and the comma moves
					// past them, in the same pass.
					fExpect
						(
							"CommaBlockCommentLines"
							, "typedef enum {\n  EFlag_A = 4, /**< Acquire provides\n                            the old one */\n  EFlag_B,\n} EFlag;\n"
							, "typedef enum\n{\n    EFlag_A = 4 ///< Acquire provides\n                ///< the old one\n    , EFlag_B\n} EFlag;\n"
							, false
						)
					;
					fExpect("SingleValue", "enum EVersion\n{\n\tEVersion = 0x102\n};\n", "enum EVersion\n{\n\tEVersion = 0x102\n};\n");
					fExpect("SingleOfSeveral", "enum EKind\n{\n\tmc_A\n\t, mc_Longer = 2\n};\n", "enum EKind\n{\n\tmc_A\n\t, mc_Longer = 2\n};\n");
					// A blank line or a comment line ends a run, and a run with one comment leaves it where it is.
					fExpect
						(
							"Runs"
							, "enum EKind\n{\n\tmc_A // First\n\t, mc_Longer // Second\n\n\t, mc_B // Alone\n\t// Note\n\t, mc_C\n};\n"
							, "enum EKind\n{\n\tmc_A        // First\n\t, mc_Longer // Second\n\n\t, mc_B // Alone\n\t// Note\n\t, mc_C\n};\n"
						)
					;
				};

				DMibTestCategory("ValuesTooLong")
				{
					// A run whose aligned '=' makes a line too long is aligned all the same, and the line reported.
					CStr Long;
					for (umint i = 0; i < 170; ++i)
						Long += "x";

					fExpect("Comment", "enum EKind\n{\n\tmc_A = 1 // " + Long + "\n\t, mc_Longer = 2\n};\n", "enum EKind\n{\n\tmc_A        = 1 // " + Long + "\n\t, mc_Longer = 2\n};\n");
					fExpectReported("Reported", "enum EKind\n{\n\tmc_A        = 1 // " + Long + "\n\t, mc_Longer = 2\n};\n");
				};

				DMibTestCategory("Members")
				{
					fExpect
						(
							"Fields"
							, "struct CNode\n{\n\tumint m_iFirst = 0; // Inclusive.\n\tumint m_iLastToken = 0; // Inclusive.\n\tvoid f_Call(); // Not a field.\n"
							"\tbool m_bA = false; // Shorter.\n\tbool m_bLonger = false; // Longer.\n};\n"
							, "struct CNode\n{\n\tumint m_iFirst = 0;     // Inclusive.\n\tumint m_iLastToken = 0; // Inclusive.\n\tvoid f_Call(); // Not a field.\n"
							"\tbool m_bA = false;      // Shorter.\n\tbool m_bLonger = false; // Longer.\n};\n"
						)
					;
					// A typedef's record aligns its members' comments as a named one does.
					fExpect
						(
							"TypedefRecord"
							, "typedef struct\n{\n\tint m_A; // A.\n\tlong m_Longer; // Longer.\n} CNode;\n"
							, "typedef struct\n{\n\tint m_A;       // A.\n\tlong m_Longer; // Longer.\n} CNode;\n"
						)
					;
					// A field whose type a class key names is a field, which keeps the run going, and a type declared in the
					// run ends it.
					fExpect
						(
							"ClassKeyFields"
							, "struct CNode\n{\n\tint m_A; // A.\n\tstruct CAddr m_Addr;\n\tunsigned short m_Port; // Port.\n\tstruct CFoo;\n"
							"\tint m_B; // B.\n\tlong m_Longer; // Longer.\n};\n"
							, "struct CNode\n{\n\tint m_A;               // A.\n\tstruct CAddr m_Addr;\n\tunsigned short m_Port; // Port.\n\tstruct CFoo;\n"
							"\tint m_B;       // B.\n\tlong m_Longer; // Longer.\n};\n"
						)
					;
					fExpect
						(
							"Spaces"
							, "struct CNode\n{\n    int m_A; // A.\n    int m_Longer; // Longer.\n};\n"
							, "struct CNode\n{\n    int m_A;      // A.\n    int m_Longer; // Longer.\n};\n"
							, false
						)
					;

					// A comment the maximum column cannot take stays one space behind its code, and the others are aligned
					// without it.
					fExpect
						(
							"MaximumColumn"
							, "struct CNode\n{\n\tint m_A; // A.\n\tCLongerTypeName m_Longer = fg_MakeTheDefaultValueForIt(); // Long.\n"
							"\tint m_Shorter; // Shorter.\n};\n"
							, "struct CNode\n{\n\tint m_A;       // A.\n\tCLongerTypeName m_Longer = fg_MakeTheDefaultValueForIt(); // Long.\n"
							"\tint m_Shorter; // Shorter.\n};\n"
							, true
							, 40
						)
					;
					// Commas written behind the enumerators are laid out in front of them, and the run is aligned as written that way.
					fExpect("TrailingCommas", "enum E\n{\n\tmc_A = 1,\n\tmc_B = 2, // B\n\tmc_Cc = 3 // C\n};\n", "enum E\n{\n\tmc_A    = 1\n\t, mc_B  = 2 // B\n\t, mc_Cc = 3 // C\n};\n");
					// A run its comments make too long is aligned all the same, and the line reported.
					CStr Long;
					for (umint i = 0; i < 172; ++i)
						Long += "x";

					fExpect
						(
							"TooLong"
							, "struct CNode\n{\n\tint m_A; // " + Long + "\n\tint m_Longer; // B.\n};\n"
							, "struct CNode\n{\n\tint m_A;      // " + Long + "\n\tint m_Longer; // B.\n};\n"
						)
					;
					fExpectReported("Reported", "struct CNode\n{\n\tint m_A;      // " + Long + "\n\tint m_Longer; // B.\n};\n");
				};

				DMibTestCategory("Off")
				{
					fg_ExpectFormat("Disabled", "enum EKind\n{\n\tmc_A\t\t// First\n\t, mc_Longer = 1\t// Second\n};\n", "enum EKind\n{\n\tmc_A // First\n\t, mc_Longer = 1 // Second\n};\n");
				};
			};

			// With the project's TrailingScopeAtStatementLevel, a lambda's body or an opened scope that ends the statement's
			// only expression stands at the statement's level, the terminator behind its closing marker.
			DMibTestSuite("TrailingScope")
			{
				auto fFormat = [](CStr const &_Text, CStr const &_Stage, bool _bEnabled = true)
					{
						DMibTestPath(_Stage);

						auto Request = fg_Request(_Text);
						CCodeFormattingStyle Style = *fg_MalterlibStyle();
						Style.m_bTrailingScopeAtStatementLevel = _bEnabled;
						Request.m_pStyle = fg_Construct(fg_Move(Style));

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);

						return fg_ApplyCodeFormattingEdits(_Text, Result.m_Edits);
					}
				;

				auto fExpect = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected)
					{
						DMibTestPath(_Case);

						auto Formatted = fFormat(_Source, "First");
						DMibExpect(Formatted, ==, _Expected);
						DMibExpect(fFormat(Formatted, "Second"), ==, _Expected);
					}
				;

				// Turning the option off again gives back the source formatted without it.
				auto fExpectRoundTrip = [&](CStr const &_Case, CStr const &_Source)
					{
						DMibTestPath(_Case);

						DMibExpect(fFormat(_Source, "Off", false), ==, _Source);

						auto Enabled = fFormat(_Source, "On");
						DMibExpect(Enabled, !=, _Source);
						DMibExpect(fFormat(Enabled, "Back", false), ==, _Source);
					}
				;

				CStr Long;
				for (umint i = 0; i < 60; ++i)
					Long += "x";

				// A scope holding a directive is formatted to a stable layout, whose next pass settles what the first left.
				DMibTestCategory("Directive")
				{
					auto Formatted = fFormat("int Values{ (\n#include \"Values.inc\"\n) };\n", "First");
					DMibExpect(Formatted, ==, "int Values\n\t{\n\t\t(\n#include \"Values.inc\"\n\t\t)\n\t}\n;\n");
					DMibExpect(fFormat(Formatted, "Second"), ==, Formatted);
				};

				DMibTestCategory("Lambda")
				{
					fExpect
						(
							"Body"
							, "void f()\n{\n\tauto Cleanup = g_OnScopeExit / [&]()\n\t\t{\n\t\t\tfg_Remove();\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tauto Cleanup = g_OnScopeExit / [&]()\n\t{\n\t\tfg_Remove();\n\t};\n}\n"
						)
					;
					// What the expression goes on with behind the body keeps it one level in.
					fExpect
						(
							"Continued"
							, "void f()\n{\n\tauto Cleanup = g_OnScopeExit / [&]()\n\t\t{\n\t\t\tfg_Remove();\n\t\t}\n\t\t> \"Testing\"\n\t;\n}\n"
							, "void f()\n{\n\tauto Cleanup = g_OnScopeExit / [&]()\n\t\t{\n\t\t\tfg_Remove();\n\t\t}\n\t\t> \"Testing\"\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("Scope")
				{
					fExpect
						(
							"Call"
							, "void f()\n{\n\tauto Value = fg_Function(a" + Long + ", b" + Long + ", c" + Long + ");\n}\n"
							, "void f()\n{\n\tauto Value = fg_Function\n\t(\n\t\ta" + Long + "\n\t\t, b" + Long + "\n\t\t, c" + Long + "\n\t);\n}\n"
						)
					;
					fExpect
						(
							"Await"
							, "void f()\n{\n\tCStr Result = co_await\n\t\t(\n\t\t\tg_Dispatch(Actor) / [Path]() -> CStr\n\t\t\t{\n\t\t\t\treturn fg_Read(Path);\n\t\t\t}\n"
							"\t\t\t% \"Error\"\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tCStr Result = co_await\n\t(\n\t\tg_Dispatch(Actor) / [Path]() -> CStr\n\t\t{\n\t\t\treturn fg_Read(Path);\n\t\t}\n"
							"\t\t% \"Error\"\n\t);\n}\n"
						)
					;
					// An operand behind the scope makes it one part of several, which keeps its continuation level.
					fExpect
						(
							"Continued"
							, "void f()\n{\n\tauto Value = fg_Function(a" + Long + ", b" + Long + ", c" + Long + ") + 6;\n}\n"
							, "void f()\n{\n\tauto Value = fg_Function\n\t\t(\n\t\t\ta" + Long + "\n\t\t\t, b" + Long + "\n\t\t\t, c" + Long + "\n\t\t)\n\t\t+ 6\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("RoundTrip")
				{
					// A comment line moves with the block around it.
					fExpectRoundTrip("CommentLineMovesWithBlock", "void f()\n{\n\tauto Cleanup = g_OnScopeExit / [&]()\n\t\t{\n\t\t\t// fg_Old();\n\t\t\tfg_New();\n\t\t}\n\t;\n}\n");
					// A body below a lambda's parameter list on a line of its own moves back under it.
					fExpectRoundTrip
						(
							"ParameterListLine"
							, "namespace N\n{\n\tvoid f()\n\t{\n\t\tpState->m_fContinue = [this, Promise = fg_Move(Promise.m_Promise), _ApplicationName, _VersionID, _DestinationDir]"
							"(TCSharedPointer<CState> const &_pState) mutable\n\t\t\t{\n\t\t\t\tauto Manager = _pState->m_ManagersToTry.f_Pop();\n"
							"\t\t\t\tfp_DownloadApplicationFromManager(Manager, _ApplicationName, _VersionID, _DestinationDir) > [_pState, Promise = fg_Move(Promise)]\n"
							"\t\t\t\t\t(TCAsyncResult<CVersionInformation> &&_Result)\n\t\t\t\t\t{\n\t\t\t\t\t\tPromise.f_SetResult(fg_Move(*_Result));\n"
							"\t\t\t\t\t}\n\t\t\t\t;\n\t\t\t}\n\t\t;\n\t}\n}\n"
						)
					;
				};

				DMibTestCategory("Options")
				{
					CCodeFormattingStyle Style;
					DMibExpectFalse(Style.m_bTrailingScopeAtStatementLevel);

					Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTrailingScopeAtStatementLevel: true\n"));
					DMibExpectTrue(Style.m_bTrailingScopeAtStatementLevel);

					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTrailingScopeAtStatementLevel: 1\n"), NException::CException);
				};
			};

			DMibTestSuite("Naming")
			{
				auto fStyle = [](CStr const &_Yaml)
					{
						CCodeFormattingStyle Style;
						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse(_Yaml));

						return Style;
					}
				;

				auto fRoles = [](CCodeFormattingStyle const &_Style, CStr const &_Name)
					{
						return _Style.f_GetRoles(_Name.f_GetStr(), _Name.f_GetLen());
					}
				;

				DMibTestCategory("Roles")
				{
					auto const &Style = *fg_MalterlibStyle();

					DMibExpectTrue(fRoles(Style, "f_Get") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "fsg_Hash") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "CStr") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "TCVector") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "umint") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "DMibCheck") == ECodeNameRole::mc_Macro);
					DMibExpectTrue(fRoles(Style, "inline_small") == ECodeNameRole::mc_SpecifierMacro);
					DMibExpectTrue(fRoles(Style, "_j") == ECodeNameRole::mc_DSLMarker);
					DMibExpectTrue(fRoles(Style, "_") == ECodeNameRole::mc_DSLMarker);
					DMibExpectTrue(fRoles(Style, "Count") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "C") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "Ca") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "umint2") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "_J") == ECodeNameRole::mc_None);
				};

				DMibTestCategory("Patterns")
				{
					auto Style = fStyle
						(
							"Version: 1\n"
							"FunctionNames: ['x[0-9]*y', 'a.c', '[_a-c]*z']\n"
							"TypeNames: ['xy', 'Both']\n"
							"MacroNames: ['Both']\n"
						)
					;

					DMibExpectTrue(fRoles(Style, "xy") == (ECodeNameRole::mc_Function | ECodeNameRole::mc_Type));
					DMibExpectTrue(fRoles(Style, "x012y") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "x0a2y") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "x1") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "abc") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "abbc") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "z") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "_cabz") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "_dz") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "Both") == (ECodeNameRole::mc_Type | ECodeNameRole::mc_Macro));

					// A character is a code point, in a class and for '.' alike.
					auto Unicode = fStyle("Version: 1\nMacroNames: ['D[ÄÖ]Pointer', 'E.Value', '[Ä-Ö]*x']\n");
					DMibExpectTrue(fRoles(Unicode, "DÄPointer") == ECodeNameRole::mc_Macro);
					DMibExpectTrue(fRoles(Unicode, "DÖPointer") == ECodeNameRole::mc_Macro);
					DMibExpectTrue(fRoles(Unicode, "DåPointer") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Unicode, "EÄValue") == ECodeNameRole::mc_Macro);
					DMibExpectTrue(fRoles(Unicode, "ÄÅÖx") == ECodeNameRole::mc_Macro);
					DMibExpectTrue(fRoles(Unicode, "Øx") == ECodeNameRole::mc_None);

					// A reversed range is malformed rather than empty.
					DMibExpectExceptionType(fStyle("Version: 1\nMacroNames: ['D[Z-A].*']\n"), NException::CException);
				};

				DMibTestCategory("Extends")
				{
					CCodeFormattingStyle Style = *fg_MalterlibStyle();
					auto Document = CCodeFormattingStyleDocument::fs_Parse("Version: 1\nExtends: ../Core/.malterlib-format\nTypeNames: ['Foo']\nMacroNames: []\n");
					DMibExpect(Document.m_Extends, ==, "../Core/.malterlib-format");
					Style.f_Apply(Document);

					DMibExpectTrue(fRoles(Style, "Foo") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "CStr") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "DMibCheck") == ECodeNameRole::mc_None);
					DMibExpectTrue(fRoles(Style, "f_Get") == ECodeNameRole::mc_Function);
					DMibExpectTrue(fRoles(Style, "inline_small") == ECodeNameRole::mc_SpecifierMacro);
				};

				DMibTestCategory("Append")
				{
					CCodeFormattingStyle Style = *fg_MalterlibStyle();
					auto Document = CCodeFormattingStyleDocument::fs_Parse
						(
							"Version: 1\n"
							"TypeNames:\n"
							"  Append:\n"
							"    - 'Foo'\n"
							"    - 'x[0-9]*'\n"
							"SpecifierMacros: {Append: []}\n"
							"MacroNames: ['DOnly']\n"
						)
					;
					DMibAssertTrue(bool(Document.m_Lists[umint(ECodeNamingList::mc_TypeNames)]));
					DMibExpectTrue(Document.m_Lists[umint(ECodeNamingList::mc_TypeNames)]->m_bAppend);
					DMibExpectFalse(Document.m_Lists[umint(ECodeNamingList::mc_MacroNames)]->m_bAppend);
					Style.f_Apply(Document);

					DMibExpectTrue(fRoles(Style, "Foo") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "x12") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "CStr") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "umint") == ECodeNameRole::mc_Type);
					DMibExpectTrue(fRoles(Style, "inline_small") == ECodeNameRole::mc_SpecifierMacro);
					DMibExpectTrue(fRoles(Style, "DOnly") == ECodeNameRole::mc_Macro);
					DMibExpectTrue(fRoles(Style, "DMibCheck") == ECodeNameRole::mc_None);

					{
						// An append that repeats a name leaves one entry holding the role.
						DMibTestPath("Repeated");

						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTypeNames: {Append: ['Foo', 'umint']}\n"));
						DMibExpectTrue(fRoles(Style, "Foo") == ECodeNameRole::mc_Type);
						DMibExpectTrue(fRoles(Style, "umint") == ECodeNameRole::mc_Type);
					}
				};

				DMibTestCategory("Options")
				{
					CCodeFormattingStyle Style;
					DMibExpectFalse(Style.m_bAlignTrailingComments);
					DMibExpectFalse(Style.m_bAlignEnumValues);

					Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignTrailingComments: true\nAlignEnumValues: true\n"));
					DMibExpectTrue(Style.m_bAlignTrailingComments);
					DMibExpectTrue(Style.m_bAlignEnumValues);

					{
						// A document that leaves an option out keeps what it extends.
						DMibTestPath("Extends");

						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignEnumValues: false\n"));
						DMibExpectTrue(Style.m_bAlignTrailingComments);
						DMibExpectFalse(Style.m_bAlignEnumValues);
					}

					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignTrailingComments: 1\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignEnumValues: 'yes'\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignTrailingCommentsMaximumColumn: -1\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignTrailingCommentsMaximumColumn: 'x'\n"), NException::CException);

					{
						DMibTestPath("MaximumColumn");

						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nAlignTrailingCommentsMaximumColumn: 120\n"));
						DMibExpect(Style.m_nAlignTrailingCommentsMaximumColumn, ==, 120u);

						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\n"));
						{
							DMibTestPath("Kept");
							DMibExpect(Style.m_nAlignTrailingCommentsMaximumColumn, ==, 120u);
						}
					}
				};

				DMibTestCategory("Invalid")
				{
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("FunctionNames: ['f_.*']\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 2\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nFunctionName: ['f_.*']\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nFunctionNames: 'f_.*'\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nFunctionNames: [1]\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nExtends: ''\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("- 1\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTypeNames: {Add: ['Foo']}\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTypeNames: {Append: ['Foo'], Remove: ['C']}\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTypeNames: {Append: 'Foo'}\n"), NException::CException);
					DMibExpectExceptionType(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTypeNames: {}\n"), NException::CException);
					DMibExpectExceptionType(fStyle("Version: 1\nFunctionNames: ['f_[a-z']\n"), NException::CException);
					DMibExpectExceptionType(fStyle("Version: 1\nFunctionNames: ['f_[]']\n"), NException::CException);
					DMibExpectExceptionType(fStyle("Version: 1\nFunctionNames: ['*f']\n"), NException::CException);
					DMibExpectExceptionType(fStyle("Version: 1\nFunctionNames: ['f**']\n"), NException::CException);
				};

				DMibTestCategory("Tokens")
				{
					CStr Source = "fg_Call(CStr, Count);";
					CCodeTokenStream Named(Source, fg_MalterlibStyle().f_Get());
					CCodeTokenStream Plain(Source);
					auto const &NamedTokens = Named.f_GetTokens();
					auto const &PlainTokens = Plain.f_GetTokens();

					DMibAssertTrue(NamedTokens.f_GetLen() > 5);
					DMibExpectTrue(Named.f_HasRole(NamedTokens[0], ECodeNameRole::mc_Function));
					DMibExpectTrue(Named.f_HasRole(NamedTokens[2], ECodeNameRole::mc_Type));
					DMibExpectFalse(Named.f_HasRole(NamedTokens[5], ECodeNameRole::mc_Type));
					DMibExpectFalse(Plain.f_HasRole(PlainTokens[0], ECodeNameRole::mc_Function));
					DMibExpectFalse(Plain.f_HasRole(PlainTokens[2], ECodeNameRole::mc_Type));
				};

				DMibTestCategory("Unnamed")
				{
					// Without a naming, a macro standing where 'inline' does is a word like any other, so no specifier moves past it.
					CStr Source = "constexpr inline_small static int gc_Value = 1;\n";
					auto Request = fg_Request(Source);

					DMibExpect(fg_ApplyCodeFormattingEdits(Source, fg_AnalyzeCodeFormatting(Request).m_Edits), ==, "static constexpr inline_small int gc_Value = 1;\n");

					Request.m_pStyle = nullptr;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpect(fg_ApplyCodeFormattingEdits(Source, Result.m_Edits), ==, Source);
				};
			};

			DMibTestSuite("Rules")
			{
				DMibTestCategory("Indentation")
				{
					// The depth is the line-break rule's; this rule only spells it with tabs.
					fg_ExpectFormat("SpacesToTabs", "void f()\n{\n    int a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					fg_ExpectFormat("MixedIndent", "void f()\n{\n \tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					fg_ExpectFormat("AlreadyTabs", "void f()\n{\n\tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					// Layout inside a multiline literal is protected. A comment's lines keep their columns, spelled as the
					// indentation is.
					fg_ExpectFormat("RawStringBody", "auto x = R\"(\n    keep\n)\";\n", "auto x = R\"(\n    keep\n)\";\n");
					fg_ExpectFormat("BlockCommentBody", "/* text\n    keep\n */\n", "/* text\n\tkeep\n */\n");
					fg_ExpectFormat("BlockCommentLines", "/*\n    a\n      b\n */\nint g_A;\n", "/*\na\n  b\n */\nint g_A;\n", false);
					// A macro's body is not: its lines stand one level in from the directive.
					fg_ExpectFormat("MacroBody", "#define A \\\n    B\n", "#define A \\\n\tB\n");
				};

				DMibTestCategory("TrailingWhitespace")
				{
					fg_ExpectFormat("Code", "int a;   \n", "int a;\n");
					fg_ExpectFormat("BlankLine", "int a;\n\t\nint b;\n", "int a;\n\nint b;\n");
					fg_ExpectFormat("LineComment", "int a; // c  \n", "int a; // c\n");
					fg_ExpectFormat("ProtectedLiteral", "auto x = R\"(a   \nb)\";\n", "auto x = R\"(a   \nb)\";\n");
					fg_ExpectFormat("ProtectedSplice", "#define A B  \\\n\tC\n", "#define A B  \\\n\tC\n");
					// A comment the layout moves takes the indentation it moves to, however the source wrote it.
					fg_ExpectFormat
						(
							"MovedCommentIndentation"
							, "#ifndef ZLIB_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\ntypedef struct z_stream_s {\n} gz_header;\n#ifndef Z_SOLO\n"
							"                        /* utility functions */\n#endif /* !Z_SOLO */\nZEXTERN int            ZEXPORT deflateResetKeep(z_streamp);\n"
							"#if defined(_WIN32) && !defined(Z_SOLO)\n#  ifndef Z_SOLO\nZEXTERN int            ZEXPORTVA gzvprintf(gzFile file,\n"
							"                                           const char *format,\n                                           va_list va);\n"
							"#  endif\n#endif\n#ifdef __cplusplus\n}\n#endif\n#endif /* ZLIB_H */\n"
							, "#ifndef ZLIB_H\n#ifdef __cplusplus\nextern \"C\"\n{\n#endif\n\ttypedef struct z_stream_s\n\t{\n\t} gz_header;\n#ifndef Z_SOLO\n"
							"\t/* utility functions */\n#endif /* !Z_SOLO */\n\tZEXTERN int ZEXPORT deflateResetKeep(z_streamp);\n#if defined(_WIN32) && !defined(Z_SOLO)\n"
							"#\tifndef Z_SOLO\n\tZEXTERN int ZEXPORTVA gzvprintf(gzFile file, char const *format, va_list va);\n#\tendif\n"
							"#endif\n#ifdef __cplusplus\n}\n#endif\n#endif /* ZLIB_H */\n"
							, false
						)
					;
					// A line behind a splice in code keeps its columns, written with tabs.
					fg_ExpectFormat("SplicedLine", "void f()\n{\n   return C \\\n      \"a\" D;\n}\n", "void f()\n{\n\treturn C \\\n\t  \"a\" D;\n}\n");
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
					// An empty macro argument between two commas leaves no space in front of the second.
					fg_ExpectFormat("EmptyArguments", "void f()\n{\n\tt(10, ,);\n\tt(,11,);\n\tt(, ,12);\n}\n", "void f()\n{\n\tt(10,,);\n\tt(, 11,);\n\tt(,, 12);\n}\n");
					fg_ExpectFormat("Tight", "void f()\n{\n\tg(a,b);\n}\n", "void f()\n{\n\tg(a, b);\n}\n");
					fg_ExpectFormat("SpaceBefore", "void f()\n{\n\tg(a , b);\n}\n", "void f()\n{\n\tg(a, b);\n}\n");
					fg_ExpectFormat("Leading", "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t, b\n\t\t)\n\t;\n}\n", "void f()\n{\n\tg(a, b);\n}\n");
					fg_ExpectFormat("Pack", "void f()\n{\n\tg(a, ...);\n}\n", "void f()\n{\n\tg(a, ...);\n}\n");
				};

				DMibTestCategory("OperatorSpace")
				{
					fg_ExpectFormat("Compare", "void f()\n{\n\tif (a==b || c!=d)\n\t\tg();\n}\n", "void f()\n{\n\tif (a == b || c != d)\n\t\tg();\n}\n");
					fg_ExpectFormat("Compound", "void f()\n{\n\ta+=1;\n\tb<<=2;\n}\n", "void f()\n{\n\ta += 1;\n\tb <<= 2;\n}\n");
					// A lone '=' assigns and initializes. A capture default is settled by the
					// markers around it, and behind a DSL marker the same token is the tail of
					// a spelling that hugs the key it follows.
					fg_ExpectFormat("Assign", "void f()\n{\n\tint a=1;\n\tint b =2;\n}\n", "void f()\n{\n\tint a = 1;\n\tint b = 2;\n}\n");
					fg_ExpectFormat("CaptureDefault", "void f()\n{\n\tauto g = [=]{};\n}\n", "void f()\n{\n\tauto g = [=]\n\t\t{\n\t\t}\n\t;\n}\n");
					fg_ExpectFormat("FormattingDsl", "auto g_Option = \"Names\"_o= _o[\"--file\"];\n", "auto g_Option = \"Names\"_o= _o[\"--file\"];\n");
					// A marker hugs the '=' that makes a key and the brackets of an array, and
					// one with no key in front of it spells an object whose '=' hugs the brace.
					fg_ExpectFormat
						(
							"FormattingDslSpaced"
							, "auto g_A = {\"Names\"_o = _o [\"--file\"], \"Type\"_o= _o = {}, \"Array\"_ = _ [1, _ = {\"Key\"_ = 5}], \"Object\"_j = {\"A\"_j= 1}};\n"
							, "auto g_A = {\"Names\"_o= _o[\"--file\"], \"Type\"_o= _o={}, \"Array\"_= _[1, _={\"Key\"_= 5}], \"Object\"_j= {\"A\"_j= 1}};\n"
						)
					;
					// A statement broken at its '=' is brought back together, and what then
					// does not fit gives at its scopes, not in front of the name.
					fg_ExpectFormat("AssignJoins", "void f()\n{\n\tauto Value\n\t\t= fg_G(5)\n\t;\n}\n", "void f()\n{\n\tauto Value = fg_G(5);\n}\n");
					// An operator function is named by the keyword and what follows it, and that
					// name stands apart from both: from the keyword, and from the parameter
					// list, whose own parentheses the call operator's name is spelled with.
					fg_ExpectFormat
						(
							"OperatorName"
							, "struct C\n{\n\tbool operator==(C const &_Other) const;\n\tint operator() (int _A);\n\tint operator [](umint _i);\n"
							"\toperator NStr::CStr() const;\n\tusing CBase::operator=;\n\tvoid operator,(C _Other);\n};\n"
							, "struct C\n{\n\tbool operator == (C const &_Other) const;\n\tint operator () (int _A);\n\tint operator [] (umint _i);\n"
							"\toperator NStr::CStr () const;\n\tusing CBase::operator =;\n\tvoid operator , (C _Other);\n};\n"
						)
					;
					// A conversion type's argument list is part of the name, whatever it holds, and its '<' ends none.
					fg_ExpectFormat
						(
							"ConversionTypeArguments"
							, "struct C\n{\n\toperator TCEnableIf<(c_A == 1), int>() const;\n};\n"
							, "struct C\n{\n\toperator TCEnableIf<(c_A == 1), int> () const;\n};\n"
						)
					;
					// An arrow naming an operator is spaced as a name, and one called through a member access takes arguments, not
					// parameters, so the arrow behind them accesses a member.
					fg_ExpectFormat
						(
							"ArrowOperatorName"
							, "static_assert(&C::operator-> == &C::operator->);\nauto f(C *_p)\n{\n\treturn _p->operator->()->m_A;\n}\n"
							, "static_assert(&C::operator -> == &C::operator ->);\nauto f(C *_p)\n{\n\treturn _p->operator -> ()->m_A;\n}\n"
						)
					;
					// A pointer to function's declarator is followed by its parameter list and
					// holds no separator, so only that keeps its spelling. A call whose first
					// argument takes an address is a call, and can be laid out as one.
					fg_ExpectFormat
						(
							"AddressArgument"
							, "void f()\n{\n\tm_Actor\n\t\t(\n\t\t\t&CActor::f_Get\n\t\t\t, _Value\n\t\t)\n\t;\n\tg (*pValue, 1);\n}\n"
							, "void f()\n{\n\tm_Actor(&CActor::f_Get, _Value);\n\tg(*pValue, 1);\n}\n"
						)
					;
					CStr Pointers = "void (*g_pCall)(int);\nvoid (&g_Call)(int) = fg_F;\nvoid f()\n{\n\tm_Actor.f_CallActor(&CActor::f_Get)(1);\n}\n";
					fg_ExpectFormat("FunctionPointer", Pointers, Pointers);
					// A subscript on what a call or a subscript yields hugs it, which is what lets
					// one written on a line of its own be joined. An attribute's brackets do not.
					fg_ExpectFormat
						(
							"YieldedSubscript"
							, "void f()\n{\n\ta = g(x)\n\t\t[1];\n\tb = h[1] [2];\n\tif (c) [[unlikely]]\n\t\tg();\n}\n"
							, "void f()\n{\n\ta = g(x)[1];\n\tb = h[1][2];\n\tif (c) [[unlikely]]\n\t\tg();\n}\n"
						)
					;
					// A call is not a name, whatever stands in front of it.
					fg_ExpectFormat("CallKept", "void f()\n{\n\tauto A = g(1);\n\tauto B = h(a, i(b));\n}\n", "void f()\n{\n\tauto A = g(1);\n\tauto B = h(a, i(b));\n}\n");
					// Declarators keep their Malterlib spelling; they are not expression operators.
					fg_ExpectFormat("Declarator", "void f(CStr const &_A, CStr &&_B);\n", "void f(CStr const &_A, CStr &&_B);\n");
					// Directly inside a template argument list a parenthesis behind a type spells a
					// function type, whose parameters it declares. A call in a value argument is
					// written tight against its name and is none.
					fg_ExpectFormat
						(
							"FunctionType"
							, "TCFunction<void (CFoo && _A)> g_A;\nTCFunctor<TCFuture<void> (CStr && _B, int * _p)> g_B;\nconstexpr bool gc_C = TCFoo<fg_F(a && b)>::mc_Value;\n"
							, "TCFunction<void (CFoo &&_A)> g_A;\nTCFunctor<TCFuture<void> (CStr &&_B, int *_p)> g_B;\nconstexpr bool gc_C = TCFoo<fg_F(a && b)>::mc_Value;\n"
						)
					;
					// A call's name ends in a template argument list as readily as a type does,
					// and a type's own parentheses construct a value as readily as they declare
					// a function type's parameters. Malterlib's naming settles the call; the
					// rest keeps its spelling.
					fg_ExpectFormat
						(
							"TemplatedCall"
							, "constexpr auto gc_A = TCFoo<::NMib::fg_GetHash<t_pMember> (t_Hash)>::mc_Value;\nTCFunctor<TCFuture<void> (int _A)> g_B;\n"
							"constexpr auto gc_C = TCFoo<CBindOptions(a, b)>::mc_Value;\n"
							, "constexpr auto gc_A = TCFoo<::NMib::fg_GetHash<t_pMember>(t_Hash)>::mc_Value;\nTCFunctor<TCFuture<void> (int _A)> g_B;\n"
							"constexpr auto gc_C = TCFoo<CBindOptions(a, b)>::mc_Value;\n"
						)
					;
					// A cv-qualifier behind a pointer declarator qualifies the pointer and stands
					// apart from it, as it does from a type.
					// An attribute's brackets capture nothing, so the name behind them is no attribute
					// macro of a lambda and hugs its parenthesis.
					fg_ExpectFormat
						(
							"AttributeBeforeCall"
							, "void f()\n{\n\t[[maybe_unused]] decltype (g(p)) Key;\n\t[[nodiscard]] auto x = h (1);\n}\n"
							, "void f()\n{\n\t[[maybe_unused]] decltype(g(p)) Key;\n\t[[nodiscard]] auto x = h(1);\n}\n"
						)
					;
					// A qualified call is no declaration, and behind a declarator stands a name, never
					// a literal or 'sizeof'; '*this' behind a condition is still a dereference.
					fg_ExpectFormat
						(
							"QualifiedCallOperand"
							, "void f()\n{\n\tNMemory::fg_MemMove(&pA[i], &pB[i], nMove *sizeof(t_CKey));\n\tx = (i*8) / n;\n\tif (b)\n\t\t*this = {};\n}\n"
							"void NMib::fg_Foo(CFoo *_pA);\n"
							, "void f()\n{\n\tNMemory::fg_MemMove(&pA[i], &pB[i], nMove * sizeof(t_CKey));\n\tx = (i * 8) / n;\n\tif (b)\n\t\t*this = {};\n}\n\n"
							"void NMib::fg_Foo(CFoo *_pA);\n"
						)
					;
					// A parenthesis holding nothing but fundamental type words is a cast, and a sign
					// behind it is unary; behind 'sizeof' or around a name it is an operand.
					fg_ExpectFormat
						(
							"FundamentalCast"
							, "void f()\n{\n\ts = b ? (smint) - 1 : (smint)1;\n\tx = (unsigned int) -y;\n\tn = sizeof(int) - 1;\n\tz = (Value) - 1;\n}\n"
							, "void f()\n{\n\ts = b ? (smint)-1 : (smint)1;\n\tx = (unsigned int)-y;\n\tn = sizeof(int) - 1;\n\tz = (Value) - 1;\n}\n"
						)
					;
					// A reference to a pointer stands apart from it like a qualifier of the pointer.
					fg_ExpectFormat
						(
							"PointerReference"
							, "void f(ch8 const *&o_pParse, CFoo *const &_pFoo, CFoo *&&_pBar, ch8 **ppArgv);\n"
							, "void f(ch8 const * &o_pParse, CFoo * const &_pFoo, CFoo * &&_pBar, ch8 **ppArgv);\n"
						)
					;
					fg_ExpectFormat
						(
							"PointerQualifier"
							, "void f(char *const argv[], ch8 const *const *ppA)\n{\n\tCFoo *const pA = g();\n\tCFoo *volatile pB;\n\tint *const &pC = pA;\n}\n"
							, "void f(char * const argv[], ch8 const * const *ppA)\n{\n\tCFoo * const pA = g();\n\tCFoo * volatile pB;\n\tint * const &pC = pA;\n}\n"
						)
					;
					// 'decltype' and its operand name a type: what a parenthesis behind the name
					// holds is declared, and a declarator behind the operand is one too. As an
					// expression's own the same keyword declares nothing.
					fg_ExpectFormat
						(
							"Decltype"
							, "decltype(auto) fg_A(tf_C && _A);\ndecltype(g_A) & fg_B(int * _p);\nvoid f()\n{\n\tauto x = decltype(a)(b && c);\n}\n"
							, "decltype(auto) fg_A(tf_C &&_A);\ndecltype(g_A) &fg_B(int *_p);\nvoid f()\n{\n\tauto x = decltype(a)(b && c);\n}\n"
						)
					;
					// A parenthesis that ends in a declarator or a qualifier spells a type, so it
					// is a cast, and what it converts hugs it. Only a parameter list's closing
					// parenthesis has a ref-qualifier behind it.
					fg_ExpectFormat
						(
							"Cast"
							, "struct C\n{\n\tvoid f_A() &;\n};\n\nvoid f()\n{\n\tauto a = (ch8 const *) &Data;\n\tauto b = (CFoo &&) *pFoo;\n\tauto c = (x) & y;\n}\n"
							, "struct C\n{\n\tvoid f_A() &;\n};\n\nvoid f()\n{\n\tauto a = (ch8 const *)&Data;\n\tauto b = (CFoo &&)*pFoo;\n\tauto c = (x) & y;\n}\n"
						)
					;
					// 'if constexpr' holds a condition like any other 'if', whose operators are
					// operators, and a 'new' hugs its placement arguments, which stand apart from
					// the type and are never the scope a statement is opened at.
					fg_ExpectFormat
						(
							"ConstexprCondition"
							, "void f()\n{\n\tif constexpr (tf_Flags &EFlag_A)\n\t{\n\t\tg();\n\t\th();\n\t}\n\telse if constexpr (cA<t_C> &&cB<t_C>)\n\t\tg();\n}\n"
							, "void f()\n{\n\tif constexpr (tf_Flags & EFlag_A)\n\t{\n\t\tg();\n\t\th();\n\t}\n\telse if constexpr (cA<t_C> && cB<t_C>)\n\t\tg();\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"PlacementNew"
							, "void f()\n{\n\tauto *pA = new\n\t\t(\n\t\t\t_pMemory\n\t\t)\n\t\tNPrivate::TCData<t_C>\n\t\t(\n\t\t\t_A\n#if DSafe\n\t\t\t, _B\n#endif\n\t\t)\n\t;\n"
							"\tauto *pB = new (_pMemory) CFoo(1);\n}\n"
							, "void f()\n{\n\tauto *pA = new(_pMemory) NPrivate::TCData<t_C>\n\t\t(\n\t\t\t_A\n#if DSafe\n\t\t\t, _B\n#endif\n\t\t)\n\t;\n"
							"\tauto *pB = new(_pMemory) CFoo(1);\n}\n"
						)
					;
					// A ref-qualifier declares nothing, so what follows it is the rest of the
					// declaration rather than a name, and stands apart from it.
					fg_ExpectFormat
						(
							"RefQualifier"
							, "struct C\n{\n\tCStr f_A() const &noexcept;\n\tCStr f_B() const &;\n\tCStr const &f_C() const;\n};\n"
							, "struct C\n{\n\tCStr f_A() const & noexcept;\n\tCStr f_B() const &;\n\tCStr const &f_C() const;\n};\n"
						)
					;
					// A template header stands in front of a constructor's name in place of a
					// return type, so what the parenthesis behind it holds is declared.
					fg_ExpectFormat
						(
							"ConstructorTemplate"
							, "struct C\n{\n\ttemplate <typename tf_CP0>\n\tC(tf_CP0 && _P0);\n};\n"
							, "struct C\n{\n\ttemplate <typename tf_CP0>\n\tC(tf_CP0 &&_P0);\n};\n"
						)
					;
					// A deduction guide is a declaration too, so its arrow is written apart
					// like a trailing return type's. Without a header in front of it, it is told
					// by naming its template on both sides of the arrow, which a call followed
					// by a member access does not.
					fg_ExpectFormat
						(
							"DeductionGuide"
							, "template <typename t_C>\nC(T<t_C>)->C<t_C>;\n\nC(CVoidTag)->C<void>;\n\nvoid f()\n{\n\tfg_Get(1) -> f_Call();\n}\n"
							, "template <typename t_C>\nC(T<t_C>) -> C<t_C>;\n\nC(CVoidTag) -> C<void>;\n\nvoid f()\n{\n\tfg_Get(1)->f_Call();\n}\n"
						)
					;
					fg_ExpectFormat("Pointer", "void f()\n{\n\tauto *pA = &B;\n\tauto C = *pA * 2;\n}\n", "void f()\n{\n\tauto *pA = &B;\n\tauto C = *pA * 2;\n}\n");
					fg_ExpectFormat("PureVirtual", "struct C\n{\n\tvirtual void f() = 0;\n};\n", "struct C\n{\n\tvirtual void f() = 0;\n};\n");
				};

				DMibTestCategory("TokenSpace")
				{
					// A pack's ellipsis hugs the name it introduces and stands apart from the
					// type in front of it; one that expands a pack hugs what it expands.
					fg_ExpectFormat
						(
							"Ellipsis"
							, "template <typename... tp_CParams>\nauto fg_F(NTraits::TCDecay<tp_CParams>  ... p_Params) -> TCFuture<t_CResult>\n{\n"
							"\tg(fg_Forward<tp_CParams>(p_Params)  ...);\n}\n"
							, "template <typename ...tp_CParams>\nauto fg_F(NTraits::TCDecay<tp_CParams> ...p_Params) -> TCFuture<t_CResult>\n{\n"
							"\tg(fg_Forward<tp_CParams>(p_Params)...);\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"EllipsisExpansion"
							, "template <typename ...tp_CParams>\nstruct C : TCBase<t_CResult, tp_CParams  ...>\n{\n};\n"
							, "template <typename ...tp_CParams>\nstruct C : TCBase<t_CResult, tp_CParams...>\n{\n};\n"
						)
					;
					// Behind a declarator the ellipsis stands apart as it does behind a type, named
					// or not, and hugs only where it expands into a template argument list.
					fg_ExpectFormat
						(
							"EllipsisDeclarator"
							, "template <typename ...tp_CParams>\nvoid fg_A(tp_CParams &&...p_Params, tp_CParams &&  ...p_Other, tp_CParams &&...);\n"
							"\nTCTuple<tp_CParams &&...> g_A;\nCStr f_GetPath(tfp_C const &...p_Component) const;\n"
							, "template <typename ...tp_CParams>\nvoid fg_A(tp_CParams && ...p_Params, tp_CParams && ...p_Other, tp_CParams && ...);\n"
							"\nTCTuple<tp_CParams && ...> g_A;\nCStr f_GetPath(tfp_C const & ...p_Component) const;\n"
						)
					;
					// A lambda's body ends an operand like a closing parenthesis does, so the
					// operator behind it is binary; one behind a statement's block is unary.
					fg_ExpectFormat
						(
							"OperatorAfterBody"
							, "void f()\n{\n\tauto [A, B] = co_await\n\t\t(\n\t\t\tg_Dispatch / []\n\t\t\t{\n\t\t\t\treturn 1;\n\t\t\t}\n"
							"\t\t\t+g_Dispatch / []\n\t\t\t{\n\t\t\t\treturn 2;\n\t\t\t}\n\t\t)\n\t;\n\t{\n\t}\n\t-x;\n}\n"
							, "void f()\n{\n\tauto [A, B] = co_await\n\t\t(\n\t\t\tg_Dispatch / []\n\t\t\t{\n\t\t\t\treturn 1;\n\t\t\t}\n"
							"\t\t\t+ g_Dispatch / []\n\t\t\t{\n\t\t\t\treturn 2;\n\t\t\t}\n\t\t)\n\t;\n\t{\n\t}\n\t-x;\n}\n"
						)
					;
					// Behind the call operator's name a '<' can only open a template argument
					// list, however it is spaced; a call behind the name is not one.
					fg_ExpectFormat
						(
							"CallOperatorArguments"
							, "void f()\n{\n\tfA.template operator () < void > (\"A\");\n\tfB.template operator()<16>(16);\n\tR = a.operator ()(1) < b;\n}\n"
							, "void f()\n{\n\tfA.template operator ()<void>(\"A\");\n\tfB.template operator ()<16>(16);\n\tR = a.operator () (1) < b;\n}\n"
						)
					;
					// 'sizeof...' and a fold's ellipsis are spelled by rules of their own.
					CStr Packs = "template <typename ...tp_CParams>\nconstexpr umint gc_n = sizeof...(tp_CParams);\n";
					fg_ExpectFormat("EllipsisKept", Packs, Packs);
					// An operator with an operand on both sides is written apart from both of
					// them, whatever they are spelled with.
					fg_ExpectFormat
						(
							"Infix"
							, "void f()\n{\n\tx = 5 *  5;\n\ty = a<<2;\n\tz = b  *c;\n\tw = a+(b|c);\n}\n"
							, "void f()\n{\n\tx = 5 * 5;\n\ty = a << 2;\n\tz = b * c;\n\tw = a + (b | c);\n}\n"
						)
					;
					// A '*' or a '&' declares as well as operates, so it is settled only where
					// a declaration cannot stand: behind a literal, behind the '=' that ends a
					// declarator, or in a condition, which declares nothing without an '=' of
					// its own.
					fg_ExpectFormat
						(
							"InfixDeclarator"
							, "void f()\n{\n\tCFoo *p = &x;\n\tif (nFlags&mc_Mask)\n\t\tg(a&b);\n}\n"
							, "void f()\n{\n\tCFoo *p = &x;\n\tif (nFlags & mc_Mask)\n\t\tg(a&b);\n}\n"
						)
					;
					// A cast's parenthesis leaves the reading open; a call's and that of an
					// operator spelled like one do not.
					fg_ExpectFormat
						(
							"InfixOperand"
							, "void f()\n{\n\tx = (int)*p;\n\ty = f_Get()*2;\n\tz = sizeof(void *)*4;\n}\n"
							, "void f()\n{\n\tx = (int)*p;\n\ty = f_Get() * 2;\n\tz = sizeof(void *) * 4;\n}\n"
						)
					;
					// A member access and a scope marker hug their operands; a keyword stands
					// apart from its parenthesis; a label's colon hugs it; a unary sign hugs its
					// operand and a binary one is written apart.
					fg_ExpectFormat("MemberAccess", "void f()\n{\n\ta . f( ) -> g( 1 ) ;\n}\n", "void f()\n{\n\ta.f()->g(1);\n}\n");
					fg_ExpectFormat("Keyword", "void f()\n{\n\tif constexpr(a)\n\t\treturn(b);\n}\n", "void f()\n{\n\tif constexpr (a)\n\t\treturn (b);\n}\n");
					fg_ExpectFormat
						(
							"Label"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1 : return;\n\tdefault : break;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: return;\n\tdefault: break;\n\t}\n}\n"
						)
					;
					fg_ExpectFormat("Sign", "void f()\n{\n\tx = - 1 + y - - z;\n}\n", "void f()\n{\n\tx = -1 + y - -z;\n}\n");
					// A bit-field's width hugs the ':' that introduces it, named or not. Every
					// other ':' at a declaration's level belongs to something else: a base
					// clause, an initializer list behind a parameter list, a conditional.
					fg_ExpectFormat
						(
							"BitField"
							, "struct C : CBase\n{\n\tuint8 m_Priority : 2 = 0;\n\tuint8 : 3;\n\tuint32 m_Value:gc_Bits;\n"
							"\tC(int _A) noexcept\n\t\t: m_A(_A)\n\t{\n\t}\n\n\tint m_A = 1 ? 2 : 3;\n};\n"
							, "struct C : CBase\n{\n\tuint8 m_Priority:2 = 0;\n\tuint8:3;\n\tuint32 m_Value:gc_Bits;\n"
							"\tC(int _A) noexcept\n\t\t: m_A(_A)\n\t{\n\t}\n\n\tint m_A = 1 ? 2 : 3;\n};\n"
						)
					;
					// An enumeration's underlying type and a range-for's ':' are spelled apart.
					fg_ExpectFormat
						(
							"ColonKept"
							, "enum EKind : uint32\n{\n\tmc_A\n};\n\nvoid f()\n{\n\tfor (auto &X : R)\n\t\tg(X);\n}\n"
							, "enum EKind : uint32\n{\n\tmc_A\n};\n\nvoid f()\n{\n\tfor (auto &X : R)\n\t\tg(X);\n}\n"
						)
					;
					// A gap holding a comment is not on one line, and an ambiguous pair keeps
					// its spelling.
					fg_ExpectFormat("Kept", "void f()\n{\n\tx = a /* c */ .b;\n\tg(c*d);\n}\n", "void f()\n{\n\tx = a /* c */ .b;\n\tg(c*d);\n}\n");
					// A template header is spaced whatever the source had, a trailing return
					// type's arrow stands apart on both sides, and a comparison passed to a
					// macro hugs the separators around it.
					fg_ExpectFormat("Header", "template < typename t_C >\nvoid fg_F();\n", "template <typename t_C>\nvoid fg_F();\n");
					fg_ExpectFormat("TrailingArrow", "auto fg_F(int _A)->int\n{\n}\n", "auto fg_F(int _A) -> int\n{\n}\n");
					// So is a lambda's, wherever the lambda stands and whether or not a template
					// parameter list of its own stands between its capture list and its parameters.
					fg_ExpectFormat
						(
							"LambdaArrow"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[&]<typename ...tfp_C>(tfp_C ...p_P) mutable->int\n\t\t\t{\n\t\t\t\treturn 0;\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[&]<typename ...tfp_C>(tfp_C ...p_P) mutable -> int\n\t\t\t{\n\t\t\t\treturn 0;\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// A macro that opens a function is followed by its return type and body the
					// way a parameter list is; a member access behind a macro's result is not.
					fg_ExpectFormat
						(
							"MacroArrow"
							, "void f()\n{\n\tDMibTestSuite(\"A\")->TCFuture<void>\n\t{\n\t\tco_return {};\n\t};\n\tx = DEPTR(p) -> m_Value;\n}\n"
							, "void f()\n{\n\tDMibTestSuite(\"A\") -> TCFuture<void>\n\t{\n\t\tco_return {};\n\t};\n\tx = DEPTR(p)->m_Value;\n}\n"
						)
					;
					// A 'noexcept' with a condition is a qualifier like any other, and the arrow
					// behind it a trailing return type's.
					fg_ExpectFormat("NoexceptArrow", "auto f() const noexcept(noexcept(g()))->int;\n", "auto f() const noexcept(noexcept(g())) -> int;\n");
					// A lambda that takes nothing may leave its parameter list out, and its arrow
					// then stands behind the capture list, its qualifiers, or an attribute macro.
					// It owns its body all the same. The brackets of 'delete []' capture nothing.
					fg_ExpectFormat
						(
							"LambdaArrowNoParameters"
							, "void f()\n{\n\tauto fA = [pA]->TCFuture<void>\n\t\t{\n\t\t\tco_return {};\n\t\t}\n\t;\n\tauto fB = [pA] mutable->int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t;\n"
							"\tauto fC = [pA] mark_nodebug->int { return 1; };\n\tdelete [] pArray -> m_pData;\n\tx = a[0] -> m_p;\n}\n"
							, "void f()\n{\n\tauto fA = [pA] -> TCFuture<void>\n\t\t{\n\t\t\tco_return {};\n\t\t}\n\t;\n\tauto fB = [pA] mutable -> int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t;\n"
							"\tauto fC = [pA] mark_nodebug -> int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t;\n\tdelete [] pArray->m_pData;\n\tx = a[0]->m_p;\n}\n"
						)
					;
					// An operator function's arrow is one too, whatever names it: a keyword an
					// expression uses, or a literal's suffix. Its parameter list is one wherever
					// the function is declared, which is what the arrow behind it follows.
					fg_ExpectFormat
						(
							"OperatorArrow"
							, "struct C\n{\n\tauto operator co_await () &&->CAwaiter;\n};\n\nauto operator \"\"_x(ch8 const *_p)->CStr;\n"
							, "struct C\n{\n\tauto operator co_await () && -> CAwaiter;\n};\n\nauto operator \"\"_x (ch8 const *_p) -> CStr;\n"
						)
					;
					fg_ExpectFormat("MacroOperator", "void f()\n{\n\tDMibExpect(a, < , b);\n}\n", "void f()\n{\n\tDMibExpect(a, <, b);\n}\n");
					// A path in a macro argument and a function type in an alias keep their
					// spelling.
					CStr Spelled = "using FCall = void (int);\nvoid f()\n{\n\tDMibLog(Mib/Core/Log, \"x\");\n}\n";
					fg_ExpectFormat("Spelled", Spelled, Spelled);
					// A '/' tight between names is spaced where the layout breaks one of its sides.
					{
						CStr Name;
						for (umint i = 0; i < 70; ++i)
							Name += "a";

						fg_ExpectFormat
							(
								"SlashBroken"
								, "void f()\n{\n\tx = n" + Name + " * m" + Name + "/k" + Name + ";\n}\n"
								, "void f()\n{\n\tx\n\t\t= n" + Name + "\n\t\t* m" + Name + "\n\t\t/ k" + Name + "\n\t;\n}\n"
							)
						;
					}
					// A parenthesis in a template argument list holding two names in a row declares parameters, however tight
					// against its name: the layout can break it from the name.
					fg_ExpectFormat
						(
							"FunctionTypeDeclaresParameters"
							, "using FCall = function_ref<result(point _Point, value *&_pValue)>;\n"
							, "using FCall = function_ref<result(point _Point, value * &_pValue)>;\n"
						)
					;
				};

				DMibTestCategory("BlankLines")
				{
					fg_ExpectFormat("AfterBrace", "void f()\n{\n\n\tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					// None stands in front of the closing brace either, whatever ends the line
					// above it: a statement, a nested block, or a comment.
					fg_ExpectFormat
						(
							"BeforeBrace"
							, "void f()\n{\n\tif (a)\n\t{\n\t\tg();\n\t\th();\n\n\t}\n\n}\n\nstruct C\n{\n\tint m_A; // Comment\n\n\n};\n"
							, "void f()\n{\n\tif (a)\n\t{\n\t\tg();\n\t\th();\n\t}\n}\n\nstruct C\n{\n\tint m_A; // Comment\n};\n"
						)
					;
					// So is a type's definition, the last one in its scope aside; a variable of a
					// C struct type with a braced initializer defines no type.
					fg_ExpectFormat
						(
							"TypeDefinitions"
							, "struct CA\n{\n};\nstruct CB : CA\n{\n\tenum EKind\n\t{\n\t\tEKind_A\n\t};\n\tEKind m_Kind;\n};\n"
							"template <typename t_C>\nclass TCC\n{\n};\nusing CD = TCC<int>;\nvoid f()\n{\n\tstruct timespec Time =\n\t{\n\t};\n\tg(Time);\n}\n"
							, "struct CA\n{\n};\n\nstruct CB : CA\n{\n\tenum EKind\n\t{\n\t\tEKind_A\n\t};\n\n\tEKind m_Kind;\n};\n\n"
							"template <typename t_C>\nclass TCC\n{\n};\n\nusing CD = TCC<int>;\nvoid f()\n{\n\tstruct timespec Time =\n\t\t{\n\t\t}\n\t;\n\tg(Time);\n}\n"
						)
					;
					// A function's body is set off from what follows it, a comment behind its
					// brace included, unless that is the end of the scope it stands in, a
					// directive, or a macro invoked for the function directly under it. The
					// requirements of a requires expression are no body.
					fg_ExpectFormat
						(
							"AfterFunction"
							, "struct C\n{\n\tbool f_A() const\n\t{\n\t\treturn true;\n\t}\n\tC(int _A)\n\t\t: m_A(_A)\n\t{\n\t} // Comment\n\tint m_A;\n"
							"\tvoid f_B()\n\t{\n\t}\n};\n"
							, "struct C\n{\n\tbool f_A() const\n\t{\n\t\treturn true;\n\t}\n\n\tC(int _A)\n\t\t: m_A(_A)\n\t{\n\t} // Comment\n\n\tint m_A;\n"
							"\tvoid f_B()\n\t{\n\t}\n};\n"
						)
					;
					// The comment lines continuing one behind the brace stay with it, above the blank line.
					fg_ExpectFormat("AfterFunctionCommentContinued", "void f_A()\n{\n} // A\n  // B\nint g_A;\n", "void f_A()\n{\n} // A\n  // B\n\nint g_A;\n");
					CStr AfterFunctionKept = "void f_A()\n{\n}\nDMibImplement(f_A);\n\nvoid f_B()\n{\n}\n#if 1\nint g_A;\n#endif\n\n"
						"auto f_C(auto _f)\n\trequires requires\n\t{\n\t\t_f();\n\t}\n{\n}\n"
					;
					fg_ExpectFormat("AfterFunctionKept", AfterFunctionKept, AfterFunctionKept);
					// One blank line separates what it separates, wherever it stands.
					fg_ExpectFormat("Double", "int g_A;\n\n\n\nvoid f()\n{\n\tg();\n\n \n\th();\n}\n", "int g_A;\n\nvoid f()\n{\n\tg();\n\n\th();\n}\n");
					// An access specifier opens a section of its class: a blank line sets it off
					// from the one in front of it, none follows it, and the first stands under the
					// opening brace. One with a comment above it keeps the lines around that, and
					// one still on the line of a member gets its blank line with its own.
					fg_ExpectFormat
						(
							"Access"
							, "struct C\n{\npublic:\n\n\tC();\nprivate:\n\n\tint m_A;\n\n\n\t// Comment\nprotected:\n\tint m_B; public:\n\tint m_C;\n};\n"
							, "struct C\n{\npublic:\n\tC();\n\nprivate:\n\tint m_A;\n\n\t// Comment\nprotected:\n\tint m_B;\n\npublic:\n\tint m_C;\n};\n"
						)
					;
					// A specifier with an empty section has no blank line after it, so the next
					// one stands directly under it.
					fg_ExpectFormat("EmptyAccess", "struct C\n{\n\tint m_A;\n\nprivate:\n\npublic:\n\tC();\n};\n", "struct C\n{\n\tint m_A;\n\nprivate:\npublic:\n\tC();\n};\n");
					// A base clause's 'public' is no label.
					fg_ExpectFormat("BaseClause", "struct C : public CBase\n{\n};\n", "struct C : public CBase\n{\n};\n");
					fg_ExpectFormat("AfterBraceMultiple", "void f()\n{\n\t\n\n\tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					fg_ExpectFormat
						(
							"AfterCase"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\n\t\tg();\n\t\th();\n\n\t\tbreak;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\th();\n\n\t\tbreak;\n\t}\n}\n"
						)
					;
					// A block comment over several lines trailing code becomes one line comment per line of text, the marker
					// kept and the continuation lines aligned under the first.
					fg_ExpectFormat
						(
							"TrailingBlockCommentLines"
							, "enum EFlag\n{\n\tEFlag_None = 0\n\t, EFlag_Reserve = 0x10000\t\t\t/**< Reserve. Return a\n\t\t\t\t\t\t\t\t\tpointer.\n\t\t\t\t\t\t\t\t\t*/\n};\n"
							, "enum EFlag\n{\n\tEFlag_None = 0\n\t, EFlag_Reserve = 0x10000 ///< Reserve. Return a\n\t\t\t\t\t\t\t  ///< pointer.\n};\n"
							, false
						)
					;
					fg_ExpectFormat("TrailingDocumentingBlockComment", "int a; /** A */\nint b; /*! B */\nint c; /*** C ***/\n", "int a; /// A\nint b; //! B\nint c; /*** C ***/\n", false);
					// A value whose call cannot be reached on the line goes behind its '=' rather than giving inside its
					// own qualified name.
					CStr PointerType = "NStorage::TCUniquePointer<CLinuxStackTraceInfo, NMemory::CAllocator_NonTrackedHeap>";
					fg_ExpectFormat
						(
							"ValueOutOfReach"
							, "struct C\n{\n\tvoid f()\n\t{\n\t\tif (a)\n\t\t{\n\t\t\tif (b)\n\t\t\t{\n\t\t\t\t"
							+ PointerType
							+ " pInfo = "
							+ PointerType
							+ "((CLinuxStackTraceInfo *)_pInfo);\n\t\t\t}\n\t\t}\n\t}\n};\n"
							, "struct C\n{\n\tvoid f()\n\t{\n\t\tif (a)\n\t\t{\n\t\t\tif (b)\n\t\t\t{\n\t\t\t\t"
							+ PointerType
							+ " pInfo\n\t\t\t\t\t= "
							+ PointerType
							+ "((CLinuxStackTraceInfo *)_pInfo)\n\t\t\t\t;\n\t\t\t}\n\t\t}\n\t}\n};\n"
						)
					;
					// An enumerator list's first line stands at the body's level with the commas, however it was aligned.
					fg_ExpectFormat("EnumFirstEnumerator", "enum EType\n{\n\t\tEType_A\n\t,\tEType_B\n\t, EType_C\n};\n", "enum EType\n{\n\tEType_A\n\t, EType_B\n\t, EType_C\n};\n");
					// A braced list keeping its lines stands its elements one level in from its opening brace, however the
					// first was aligned, and its closing brace under the opening one.
					fg_ExpectFormat
						(
							"KeptListElementLevel"
							, "static CMap const gc_Map[] =\n{\n\t\t{0x0000, SCHED_OTHER}\n\t, {0x4000, SCHED_RR}\n};\n\nvoid f()\n{\n\tCFoo Values =\n\t\t{\n\t\t\t Get()\n"
							"\t\t\t, a\n\t}\n\t;\n}\n"
							, "static CMap const gc_Map[] =\n\t{\n\t\t{0x0000, SCHED_OTHER}\n\t\t, {0x4000, SCHED_RR}\n\t}\n;\n\nvoid f()\n{\n\tCFoo Values =\n\t\t{\n\t\t\tGet()\n"
							"\t\t\t, a\n\t\t}\n\t;\n}\n"
						)
					;
					// A braced list written across lines is laid out as an opened list but never joined: each element on a
					// line of its own with its comma in front, blank lines between them kept. Only a directive keeps rows.
					fg_ExpectFormat
						(
							"KeptListElements"
							, "void f()\n{\n\tCSettings Settings\n\t\t{\n\t\t\t.m_A = 1\n\t\t\t, .m_B = 2, .m_C = 3\n\n\t\t\t, .m_D = 4\n\t\t}\n\t;\n"
							"\tuint8 Data[] =\n\t\t{\n\t\t\t0x02, 0x00\n\t\t\t, 0x01, 0x00, 'A'\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tCSettings Settings\n\t\t{\n\t\t\t.m_A = 1\n\t\t\t, .m_B = 2\n\t\t\t, .m_C = 3\n\n\t\t\t, .m_D = 4\n\t\t}\n\t;\n"
							"\tuint8 Data[] =\n\t\t{\n\t\t\t0x02\n\t\t\t, 0x00\n\t\t\t, 0x01\n\t\t\t, 0x00\n\t\t\t, 'A'\n\t\t}\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"KeptListMixedRows"
							, "void f()\n{\n\tTCVector<CStr> Params =\n\t\t{\n\t\t\t\"-c\", \"a\", \"-c\"\n\t\t\t, \"b=\" + Path, \"diff\"\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tTCVector<CStr> Params =\n\t\t{\n\t\t\t\"-c\"\n\t\t\t, \"a\"\n\t\t\t, \"-c\"\n\t\t\t, \"b=\" + Path\n\t\t\t, \"diff\"\n\t\t}\n\t;\n}\n"
						)
					;
					// A 'columns' directive lays its list out in rows of that many elements, a comma in front of each row
					// after the first, which is indented as far as the comma takes, and its columns aligned: numbers to the
					// right, the rest to the left. A comment behind an element ends its row. 'auto' fits as many as it can.
					fg_ExpectFormat
						(
							"ColumnsDirective"
							, "ch8 g_Table[] =\n{\n\t// malterlib-format columns: 3\n\t'A', 'B', 'C', 'D',\n\t'E', // Last\n\t'F'\n};\n\n"
							"int8 gc_Bits[]\n\t{\n\t\t// malterlib-format columns: 4\n\t\t-1, 0, 1, 10, 200, 3\n\t}\n;\n"
							, "ch8 g_Table[] =\n\t{\n\t\t// malterlib-format columns: 3\n\t\t  'A', 'B', 'C'\n\t\t, 'D', 'E' // Last\n\t\t, 'F'\n\t}\n;\n\n"
							"int8 gc_Bits[]\n\t{\n\t\t// malterlib-format columns: 4\n\t\t   -1, 0, 1, 10\n\t\t, 200, 3\n\t}\n;\n"
							, false
						)
					;
					// A comma a directive separates from the element behind it moves in front of that element in every branch
					// it is compiled in, as an enumerator's does.
					fg_ExpectFormat
						(
							"ListDirectiveCommas"
							, "char const *g_Names[] =\n\t{\n\t\t\"a\",\n#ifdef DFirst\n\t\t\"b\",\n#else\n\t\t\"c\",\n#endif\n\t\t\"d\"\n\t}\n;\n"
							, "char const *g_Names[] =\n\t{\n\t\t\"a\"\n#ifdef DFirst\n\t\t, \"b\"\n#else\n\t\t, \"c\"\n#endif\n\t\t, \"d\"\n\t}\n;\n"
							, false
						)
					;
					// A list with an element that is the first in some configurations only keeps its commas behind its elements.
					fg_ExpectFormat
						(
							"KeptListDirectiveCommas"
							, "char const *g_Names[] =\n\t{\n#ifdef DFirst\n\t\t\"a\",\n\t\t\"b\",\n#endif\n#ifdef DSecond\n\t\t\"c\",\n#endif\n\t\t\"d\",\n\t\t\"e\"\n\t}\n;\n"
							, "char const *g_Names[] =\n\t{\n#ifdef DFirst\n\t\t\"a\",\n\t\t\"b\",\n#endif\n#ifdef DSecond\n\t\t\"c\",\n#endif\n\t\t\"d\",\n\t\t\"e\"\n\t}\n;\n"
						)
					;
					// No blank line cuts a construct continued on the next line: an operator, an opening bracket, or a closing one.
					fg_ExpectFormat
						(
							"KeptListBlankBeforeOpen"
							, "namespace N\n{\n\tch8 const *g_pTests[] =\n\n\t\t{\n\t\t\t\"abc\"\n\t\t\t, \"abcd\"\n\t\t}\n\t;\n}\n"
							, "namespace N\n{\n\tch8 const *g_pTests[] =\n\t\t{\n\t\t\t\"abc\"\n\t\t\t, \"abcd\"\n\t\t}\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"BlankLineInSplitCall"
							, "void fg_Test()\n{\n\tfg_Call\n\t\t(\n\n\t\t\t[]\n\t\t\t{\n\t\t\t\tfg_Work();\n\t\t\t}\n\n\t\t)\n\t;\n}\n"
							, "void fg_Test()\n{\n\tfg_Call\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\tfg_Work();\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// An opening brace written at the end of a line takes a line of its own, and the list's lines move with it.
					fg_ExpectFormat
						(
							"KeptListTrailingOpenOperator"
							, "void fg_Test()\n{\n\tg_Testing / {\n\t\t\"abc\",\n\t\t\"abcd\"\n\t};\n}\n"
							, "void fg_Test()\n{\n\tg_Testing /\n\t\t{\n\t\t\t\"abc\"\n\t\t\t, \"abcd\"\n\t\t}\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"KeptListTrailingOpen"
							, "namespace N\n{\n\tch8 const *g_pTests[] = {\n\t\t\"abc\",\n\t\t\"abcd\"\n\t};\n}\n"
							, "namespace N\n{\n\tch8 const *g_pTests[] =\n\t\t{\n\t\t\t\"abc\"\n\t\t\t, \"abcd\"\n\t\t}\n\t;\n}\n"
						)
					;
					// An element a string continues over lines takes a line of its own, the continuation at its level.
					fg_ExpectFormat
						(
							"KeptListContinuedString"
							, "int g_A[] =\n\t{\n\t\t-1, \"x\",\n\t\t2, \"Name\", \"Long\"\n\t\t\"Rest\",\n\t\t-904, \"c\",\n\t}\n;\n"
							, "int g_A[] =\n\t{\n\t\t-1\n\t\t, \"x\"\n\t\t, 2\n\t\t, \"Name\"\n\t\t, \"Long\"\n\t\t\"Rest\"\n\t\t, -904\n\t\t, \"c\"\n\t}\n;\n"
							, false
						)
					;
					// A comma ending the list goes.
					fg_ExpectFormat
						(
							"ColumnsTrailingComma"
							, "CError g_Errors[] =\n\t{\n\t\t// malterlib-format columns: 2\n\t\t-5, \"Five\",\n\t\t-100, \"Hundred\",\n\t}\n;\n"
							, "CError g_Errors[] =\n\t{\n\t\t// malterlib-format columns: 2\n\t\t    -5, \"Five\"\n\t\t, -100, \"Hundred\"\n\t}\n;\n"
							, false
						)
					;
					{
						CStr Words;
						for (umint i = 0; i < 40; ++i)
							Words += "{}\"Word{}\""_f << (i ? ", " : "") << i;

						CStr Expected = "TCVector<CStr> gc_Words =\n\t{\n\t\t// malterlib-format columns: auto\n";
						for (umint iRow = 0; iRow < 3; ++iRow)
						{
							Expected += iRow ? "\t\t, " : "\t\t  ";
							for (umint i = iRow * 18; i < fg_Min(umint(40), iRow * 18 + 18); ++i)
								Expected += "\"Word{}\"{}"_f << i << (i + 1 < fg_Min(umint(40), iRow * 18 + 18) ? (i < 10 ? ",  " : ", ") : "");

							Expected += "\n";
						}

						Expected += "\t}\n;\n";

						fg_ExpectFormat("ColumnsAuto", "TCVector<CStr> gc_Words =\n\t{\n\t\t// malterlib-format columns: auto\n\t\t" + Words + "\n\t}\n;\n", Expected);
					}
					// A 'table' directive lays its records out one to a row, their fields aligned as columns.
					fg_ExpectFormat
						(
							"TableDirective"
							, "void f()\n{\n\tCUnit Units[] =\n\t\t{\n\t\t\t// malterlib-format table\n\t\t\t{\"gib\", 1073741824}, {\"mib\", 1048576}\n\t\t\t, {\"b\", 1}\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tCUnit Units[] =\n\t\t{\n\t\t\t// malterlib-format table\n\t\t\t  {\"gib\", 1073741824}\n\t\t\t, {\"mib\",    1048576}\n"
							"\t\t\t, {\"b\",            1}\n\t\t}\n\t;\n}\n"
						)
					;
					// A 'rows' directive keeps the elements on the lines they were written on, the comma in front of each line
					// after the first, and the comment lines between them.
					fg_ExpectFormat
						(
							"RowsDirective"
							, "void f()\n{\n\tuint8 Data[] =\n\t\t{\n\t\t\t// malterlib-format rows\n\t\t\t// Length\n\t\t\t0x02, 0x00,\n"
							"\t\t\t0x00, 0x00, // Padding\n\t\t\t// Node\n\t\t\t0x01, 'A'\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tuint8 Data[] =\n\t\t{\n\t\t\t// malterlib-format rows\n\t\t\t// Length\n\t\t\t0x02, 0x00\n"
							"\t\t\t, 0x00, 0x00 // Padding\n\t\t\t// Node\n\t\t\t, 0x01, 'A'\n\t\t}\n\t;\n}\n"
							, false
						)
					;
					fg_ExpectFormat
						(
							"RowsDirectiveLeadingCommas"
							, "void f()\n{\n\tuint8 Data[] =\n\t\t{\n\t\t\t// malterlib-format rows\n\t\t\t0x02, 0x00\n\t\t\t// Node\n\t\t\t, 0x01,   'A'  // Last\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tuint8 Data[] =\n\t\t{\n\t\t\t// malterlib-format rows\n\t\t\t0x02, 0x00\n\t\t\t// Node\n\t\t\t, 0x01, 'A' // Last\n\t\t}\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"RowsDirectiveContinuedString"
							, "CError g_Errors[] =\n\t{\n\t\t// malterlib-format rows\n\t\t-1, \"One\", \"First\",\n\t\t-2, \"Two\", \"Long \"\n"
							"\t\t\"rest\",\n\t\t-3, \"Three\", \"\"\n\t}\n;\n"
							, "CError g_Errors[] =\n\t{\n\t\t// malterlib-format rows\n\t\t-1, \"One\", \"First\"\n\t\t, -2, \"Two\", \"Long \"\n"
							"\t\t\"rest\"\n\t\t, -3, \"Three\", \"\"\n\t}\n;\n"
						)
					;
					// An 'align: =' directive aligns the '=' of the run of lines below it, up to a blank line.
					fg_ExpectFormat
						(
							"AlignDirective"
							, "void f()\n{\n\t// malterlib-format align: =\n\tint a = 1;\n\tint Longer = 2;\n\n\tint b = 3;\n}\n\nenum EFoo\n{\n\t// malterlib-format align: =\n"
							"\tEFoo_A = 1\n\t, EFoo_Longer = 2\n};\n"
							, "void f()\n{\n\t// malterlib-format align: =\n\tint a      = 1;\n\tint Longer = 2;\n\n\tint b = 3;\n}\n\nenum EFoo\n{\n\t// malterlib-format align: =\n"
							"\tEFoo_A        = 1\n\t, EFoo_Longer = 2\n};\n"
						)
					;
					// An enumerator too long for its line is laid out as a list's element, its '=' and operators at its level.
					{
						CStr Value = "EFileAttrib_UserExecute | EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_GroupExecute | EFileAttrib_GroupRead | EFileAttrib_GroupWrite"
							" | EFileAttrib_EveryoneExecute | EFileAttrib_EveryoneRead"
						;
						CStr Split = CStr(Value).f_Replace(" | ", "\n\t| ");

						fg_ExpectFormat
							(
								"EnumeratorValueSplit"
								, "enum EFileAttrib\n{\n\tEFileAttrib_None = 0\n\t, EFileAttrib_All = " + Value + "\n};\n"
								, "enum EFileAttrib\n{\n\tEFileAttrib_None = 0\n\t, EFileAttrib_All\n\t= " + Split + "\n};\n"
							)
						;
					}
					// A comment opening a case's body stands where the body does, wherever it was written.
					fg_ExpectFormat
						(
							"CaseBodyComment"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tdefault:\n\t\t\t// Comment\n\t\th();\n\t\treturn;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tdefault:\n\t\t// Comment\n\t\th();\n\t\treturn;\n\t}\n}\n"
						)
					;
					// The blank line in front of a case's 'break' is the one the case under its label has, and joins it
					// onto the label's line all the same.
					fg_ExpectFormat
						(
							"CaseBreakBlankLine"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\n\t\tbreak;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: g(); break;\n\t}\n}\n"
						)
					;
					fg_ExpectFormat("AfterDefault", "void f()\n{\n\tswitch (a)\n\t{\n\tdefault:\n\n\t\tbreak;\n\t}\n}\n", "void f()\n{\n\tswitch (a)\n\t{\n\tdefault: break;\n\t}\n}\n");
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

					// A layout edit of a line's terminator writes the configured ending, so it wins over the line-ending rule's.
					DMibTestCategory("LayoutOverEnding")
					{
						auto Request = fg_Request("int a[] = {\n1,\n2\n};\n");
						Request.m_Settings.m_EndOfLine = ETextLineEnding::mc_CRLF;

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
						DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits), ==, "int a[] =\r\n\t{\r\n\t\t1\r\n\t\t, 2\r\n\t}\r\n;\r\n");
					};
				};
			};

			fg_TestLineBreaks();

			// Each case here is one where the first pass and the second once disagreed on
			// the whole tree: a decision made against a width or a position the rules then
			// changed.
			DMibTestSuite("Convergence")
			{
				CStr Wide;
				for (umint i = 0; i < 60; ++i)
					Wide += "W";

				auto fExpect = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected, bool _bTokensPreserved = true)
					{
						fg_ExpectFormat(_Case, _Source.f_Replace("@", Wide), _Expected.f_Replace("@", Wide), _bTokensPreserved);
					}
				;

				DMibTestCategory("Spelling")
				{
					// Two access specifiers in a row stand together: the second follows the
					// first the way a member does, so no blank line is asked for between them.
					fExpect
						(
							"ConsecutiveAccess"
							, "struct C\n{\nprivate:\n\npublic:\n\tint a;\nprotected:\nprivate:\n\tint b;\n};\n"
							, "struct C\n{\nprivate:\npublic:\n\tint a;\n\nprotected:\nprivate:\n\tint b;\n};\n"
						)
					;
					// A run of pointers is one declarator, 'CFoo **', while a reference to a pointer
					// stands apart from it: 'CFoo * &'.
					fExpect
						(
							"DeclaratorRun"
							, "void f()\n{\n\tauto p = (CFoo **)pValue;\n\tg((int * *)a, (CFoo *&)b);\n}\n"
							, "void f()\n{\n\tauto p = (CFoo **)pValue;\n\tg((int **)a, (CFoo * &)b);\n}\n"
						)
					;
					// A directive's trailing whitespace is layout, trimmed without counting as a
					// change to the token stream.
					fExpect("DirectiveTrailingSpace", "#pragma once\n#include \"x.h\" \n", "#pragma once\n#include \"x.h\"\n");
				};

				DMibTestCategory("Measure")
				{
					// A line is measured at the width the spacing rules leave it: 'X-1' at 189
					// columns is 'X - 1' at 191, and the statement has to give.
					fExpect
						(
							"SpacedWidth"
							, "void f()\n{\n\tif (a)\n\t\tfg_Call(@, @, WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW-1);\n}\n"
							, "void f()\n{\n\tif (a)\n\t{\n\t\tfg_Call\n\t\t\t(\n\t\t\t\t@\n\t\t\t\t, @\n"
							"\t\t\t\t, WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW - 1\n\t\t\t)\n\t\t;\n\t}\n}\n"
							, false
						)
					;
					// A clause written tight against its parenthesis is opened like any other:
					// the space the clause rule would write and the line break land on one
					// offset, and the break is the one that counts.
					fExpect
						(
							"OpenedTightClause"
							, "void f()\n{\n\tif(g(@, @, @))\n\t\tx();\n}\n"
							, "void f()\n{\n\tif\n\t(\n\t\tg\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t\t, @\n\t\t)\n\t)\n\t{\n\t\tx();\n\t}\n}\n"
							, false
						)
					;
				};

				// A pass whose decision depends on a layout it made itself leaves work for the next: the analysis repeats until the
				// file is stable, so such a file is formatted rather than refused.
				DMibTestCategory("Fixpoint")
				{
					auto fExpectStable = [&](CStr const &_Case, umint _nMaxColumns, CStr const &_Source, CStr const &_Expected)
						{
							DMibTestPath(_Case);

							auto fFormat = [&](CStr const &_Text) -> CCodeFormattingResult
								{
									auto Properties = fg_MalterlibProperties();
									Properties["max_line_length"] = CStr::fs_ToStr(_nMaxColumns);
									auto Request = fg_Request(_Text);
									Request.m_Settings = CCodeFormattingSettings(Properties);

									return fg_AnalyzeCodeFormatting(Request);
								}
							;

							auto First = fFormat(_Source);
							DMibAssertTrue(First.m_Status == ECodeFormattingStatus::mc_Complete);
							auto Formatted = fg_ApplyCodeFormattingEdits(_Source, First.m_Edits);
							DMibExpect(Formatted, ==, _Expected);

							auto Second = fFormat(Formatted);
							DMibExpectTrue(Second.m_Status == ECodeFormattingStatus::mc_Complete);
							DMibExpectTrue(Second.m_Edits.f_IsEmpty());
						}
					;

					fExpectStable
						(
							"GuardOnCaseLabel"
							, 80
							, "void f()\n{\n\tswitch (x)\n\t{\n\tcase 1: if (a) {fg_Process(FirstArgument, SecondArgument, ThirdArgument, Fourth);} else {bar();} break;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (x)\n\t{\n\tcase 1:\n\t\tif (a)\n\t\t\tfg_Process(FirstArgument, SecondArgument, ThirdArgument, Fourth);\n"
							"\t\telse\n\t\t\tbar();\n\t\tbreak;\n\t}\n}\n"
						)
					;
					fExpectStable
						(
							"GuardedTrailingCommentAtLimit"
							, 80
							, "namespace N { void f() { if(a) fg_Process(FirstArgument, SecondArgument, ThirdArgument, FourthArg); // comment\nelse bar(); } }\n"
							, "namespace N\n{\n\tvoid f()\n\t{\n\t\tif (a)\n\t\t\t// comment\n\t\t\tfg_Process(FirstArgument, SecondArgument, ThirdArgument, FourthArg);\n"
							"\t\telse\n\t\t\tbar();\n\t}\n}\n"
						)
					;
					fExpectStable
						(
							"DesignatedTrailingComment"
							, 40
							, "void f()\n{\n\tauto Value = CData{.m_FirstValue = f_GetValue(), // important value\n.m_Second = 2};\n}\n"
							, "void f()\n{\n\tauto Value = CData\n\t\t{\n\t\t\t// important value\n\t\t\t.m_FirstValue = f_GetValue()\n\t\t\t, .m_Second = 2\n\t\t}\n\t;\n}\n"
						)
					;
					// What the passes leave is diagnosed on the layout they end on, where every line fits.
					{
						DMibTestPath("DiagnosticsOfStableLayout");

						auto Properties = fg_MalterlibProperties();
						Properties["max_line_length"] = "40";
						auto Request = fg_Request("void f()\n{\n\tauto Value = CData{.m_FirstValue = f_GetValue(), // important value\n.m_Second = 2};\n}\n");
						Request.m_Settings = CCodeFormattingSettings(Properties);

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibAssertTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
						umint nResidual = 0;
						for (auto const &Diagnostic : Result.m_Diagnostics)
							nResidual += !Diagnostic.m_bHasAutomaticFix;

						DMibExpect(nResidual, ==, 0);
					}
					fExpectStable
						(
							"ListInParenthesizedLambda"
							, 30
							, "void f(){ auto x = ([] { return int{1,}; }()); }\n"
							, "void f()\n{\n\tauto x =\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\treturn int\n\t\t\t\t\t{\n\t\t\t\t\t\t1\n\t\t\t\t\t}\n"
							"\t\t\t\t;\n\t\t\t}()\n\t\t)\n\t;\n}\n"
						)
					;
					fExpectStable
						(
							"ListBehindSplitConditional"
							, 50
							, "void f()\n{\n\tauto Value = bConditionValue ? CTypeName{xValue, yValue} : DefaultValue + CTypeName{xValue, yValue};\n}\n"
							, "void f()\n{\n\tauto Value\n\t\t= bConditionValue\n\t\t? CTypeName{xValue, yValue}\n\t\t: DefaultValue + CTypeName{xValue, yValue}\n\t;\n}\n"
						)
					;
					// A body behind a comment on its brace's line is placed from that line, and a statement behind a comment takes its
					// continuation along, once.
					fExpectStable("CommentedLambdaBody", 190, "auto foo = []() /* A */ { /* B */ return\n0; };\n", "auto foo = []()\n\t/* A */ {\n\t\t/* B */ return\n\t\t\t0;\n\t}\n;\n");
					// A comment ending the file in a backslash stays behind its code, which a move above would splice into it.
					fExpectStable("BackslashEndingComment", 20, "int x; // directory C:\\", "int x; // directory C:\\");
					// A macro naming the function can expand to the whole declarator, and one standing for a whole parameter to its
					// name, so neither declaration's return type moves.
					fExpectStable
						(
							"MacroDeclarator"
							, 40
							, "struct C\n{\n#define DDeclare(...) f(__VA_ARGS__) = 0\n\tvirtual CLongReturnType DDeclare(int x, int y);\n};\n"
							, "struct C\n{\n#define DDeclare(...) f(__VA_ARGS__) = 0\n\tvirtual CLongReturnType DDeclare\n\t\t(\n\t\t\tint x\n\t\t\t, int y\n\t\t)\n\t;\n};\n"
						)
					;
					fExpectStable
						(
							"MacroParameter"
							, 40
							, "using CLongReturnType = int;\n#define DParam int CLongReturnType\nCLongReturnType fg_GetExampleValue(DParam) { return {}; }\n"
							, "using CLongReturnType = int;\n#define DParam int CLongReturnType\nCLongReturnType fg_GetExampleValue(DParam)\n{\n\treturn {};\n}\n"
						)
					;
					// A raw string spliced from its prefix in a directive keeps its text, and directives spelled with comments and
					// splices are the conditionals they are.
					fExpectStable("SplicedRawStringPrefix", 60, "#define DCode u\\\n8R\"(\nint   x= \"last\n)\"\nint y;\n", "#define DCode u\\\n8R\"(\nint   x= \"last\n)\"\nint y;\n");
					// A comment behind a byte order mark starts the file's first line, with no space in front.
					fExpectStable("CommentBehindByteOrderMark", 60, "\xEF\xBB\xBF// comment\nint x;\n", "\xEF\xBB\xBF// comment\nint x;\n");
					fExpectStable("SplicedRawStringInDirective", 60, "#define DCode R\\\n\"(\nint x=1;\n\")\"\nint y;\n", "#define DCode R\\\n\"(\nint x=1;\n\")\"\nint y;\n");
					fExpectStable
						(
							"CommentedDirectiveKeywords"
							, 190
							, "void f()\n{\n#/**/if FLAG\n\tif (a)\n#/**/else\n\tif (b)\n#en\\\ndif\n\t\tg();\n}\n"
							, "void f()\n{\n#/**/if FLAG\n\tif (a)\n#/**/else\n\tif (b)\n#en\\\ndif\n\t\tg();\n}\n"
						)
					;
					// GNU's part of a complex number is an operator, so the parenthesis holds a value, not a parameter.
					fExpectStable("ComplexPartArgument", 40, "CLongBoxType instanceWithALongName(__real__ z);\n", "CLongBoxType instanceWithALongName\n\t(\n\t\t__real__ z\n\t)\n;\n");
					// A keyword ending an unnamed parameter is no name the return type could be hidden behind.
					fExpectStable
						(
							"UnnamedParameterKeyword"
							, 50
							, "CResult const *fg_FunctionWithLongNameThatNeedsItsOwnLine(CArgument const &);\n"
							, "auto fg_FunctionWithLongNameThatNeedsItsOwnLine\n\t(\n\t\tCArgument const &\n\t)\n\t-> CResult const *\n;\n"
						)
					;
					// An edit a later pass adds is reported as one the first makes is.
					{
						DMibTestPath("LaterPassDiagnostics");
						auto Properties = fg_MalterlibProperties();
						Properties["max_line_length"] = "80";
						auto Request = fg_Request("namespace N { void f() { if(a) fg_Process(FirstArgument, SecondArgument, ThirdArgument, FourthArg); // comment\nelse bar(); } }\n");
						Request.m_Settings = CCodeFormattingSettings(Properties);

						auto Result = fg_AnalyzeCodeFormatting(Request);
						bool bReported = false;
						for (auto const &Diagnostic : Result.m_Diagnostics)
							bReported = bReported || Diagnostic.m_Rule == "trailing-comment";

						DMibExpectTrue(bReported);
					}
					// A function an 'extern' declares has statements in its body, where no function is declared.
					fExpectStable
						(
							"ExternFunctionBody"
							, 25
							, "extern \"C\" void f() { LongNameVariable * fg_LongFunctionName(); }\n"
							, "extern \"C\" void f()\n{\n\tLongNameVariable *fg_LongFunctionName();\n}\n"
						)
					;
					// GNU's '__extension__' leads an expression, so the parenthesis holds a value.
					fExpectStable("ExtensionArgument", 30, "CValueType value(__extension__ v);\n", "CValueType value\n\t(\n\t\t__extension__ v\n\t)\n;\n");
					// So does GNU's alignment of an expression, '__alignof__ v'.
					fExpectStable("AlignmentArgument", 30, "CValueType value(__alignof__ v);\n", "CValueType value\n\t(\n\t\t__alignof__ v\n\t)\n;\n");
					// A trailing comment moved above its statement leaves a formatter directive and a documentation comment under
					// it with the code they are for.
					fExpectStable
						(
							"MovedCommentLeavesDirective"
							, 60
							, "void f()\n{\n\tint x; // This is a comment that fits alone but not after code.\n\t\t// malterlib-format align: =\n"
							"\tint a = 1;\n\tint bbb = 2;\n}\nint y; // This is a comment that fits alone but not after code.\n\t/// Documents z\nint z;\n"
							, "void f()\n{\n\t// This is a comment that fits alone but not after code.\n\tint x;\n\t// malterlib-format align: =\n"
							"\tint a   = 1;\n\tint bbb = 2;\n}\n\n// This is a comment that fits alone but not after code.\nint y;\n/// Documents z\nint z;\n"
						)
					;
					// A conditional behind a lambda's body goes on from lines of its own, and one in a branch of another gives at
					// its own operators in turn.
					fExpectStable
						(
							"ConditionalBehindLambda"
							, 60
							, "auto r = ([] { return true; }() ? \"the first branch value\" : \"the second branch value\");\n"
							, "auto r =\n\t(\n\t\t[]\n\t\t{\n\t\t\treturn true;\n\t\t}()\n\t\t? \"the first branch value\"\n\t\t: \"the second branch value\"\n\t)\n;\n"
						)
					;
					// A keyword of the return type is no name a parameter of a function pointer can hide.
					{
						CStr Name = "fg_Function";
						for (umint i = 0; i < 60; ++i)
							Name += "W";

						fExpectStable
							(
								"KeywordInNestedParameter"
								, 60
								, "unsigned long " + Name + "(void (*_p)(unsigned long));\n"
								, "auto " + Name + "\n\t(\n\t\tvoid (*_p)(unsigned long)\n\t)\n\t-> unsigned long\n;\n"
							)
						;
					}
					// A parenthesis around a lambda is no cast, and the lambda's body is laid out.
					fExpectStable
						(
							"ParenthesizedLambda"
							, 40
							, "auto x = ([]() -> std::vector<int> { return {}; })();\n"
							, "auto x =\n\t(\n\t\t[]() -> std::vector<int>\n\t\t{\n\t\t\treturn {};\n\t\t}\n\t)\n\t()\n;\n"
						)
					;
					// A trailing return type too long for its own line is laid out in turn.
					fExpectStable
						(
							"LongTrailingReturnType"
							, 70
							, "auto f() -> decltype(f_Call(VariableWithQuiteALongName, VariableWithQuiteALongName));\n"
							, "auto f()\n\t-> decltype\n\t(\n\t\tf_Call(VariableWithQuiteALongName, VariableWithQuiteALongName)\n\t)\n;\n"
						)
					;
					// A requirement and a lambda in a bracket hold no function declaration, so no return type moves there.
					fExpectStable
						(
							"NoReturnTypeInBracketedBody"
							, 55
							, "static_assert(requires { value * fg_ExtraordinarilyLongFunctionName(); });\n"
							, "static_assert\n\t(\n\t\trequires\n\t\t{\n\t\t\tvalue *fg_ExtraordinarilyLongFunctionName();\n\t\t}\n\t)\n;\n"
						)
					;
					// A member called on a lambda's result lays out its own arguments behind the body.
					fExpectStable
						(
							"MemberCallBehindLambda"
							, 60
							, "void f()\n{\n\tg([] { return CValue(); }().f_Call(fg_GetFirstArgumentValue(), fg_GetSecondArgumentValue()));\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\treturn CValue();\n\t\t\t}().f_Call\n\t\t\t(\n"
							"\t\t\t\tfg_GetFirstArgumentValue()\n\t\t\t\t, fg_GetSecondArgumentValue()\n\t\t\t)\n\t\t)\n\t;\n}\n"
						)
					;
					fExpectStable
						(
							"DeeperConditional"
							, 60
							, "auto s = A ? B : C ? D : ThirdLongCondition ? ThirdLongValue : FourthLongValue;\n"
							, "auto s\n\t= A\n\t? B\n\t: C\n\t\t? D\n\t\t: ThirdLongCondition\n\t\t\t? ThirdLongValue\n\t\t\t: FourthLongValue\n;\n"
						)
					;
					// A pack expanded behind a name is a value as well, 'Ns...', which a name behind it would declare.
					fExpectStable("PackExpansionArgument", 36, "CValues<Ns...> values_long_name(Ns...);\n", "CValues<Ns...> values_long_name\n\t(\n\t\tNs...\n\t)\n;\n");
					// A body whose brace stands behind a comment over several lines keeps its place, and so do its lines.
					fExpectStable("BraceBehindCommentLines", 190, "void f() /* a\n b */ { int x /* c\n d */ = 1; }\n", "void f() /* a\n b */ {\n\t\t  int x /* c\n d */ = 1;\n\t  }\n");
					// A template argument list cut off from its name leaves no return type to move.
					fExpectStable
						(
							"ReturnTypeCutFromName"
							, 60
							, "TCBox\n\n<TCBox<int>> f_VeryLongFunctionNameHere(int x, int y, int z);\n"
							, "TCBox\n\n<TCBox<int>> f_VeryLongFunctionNameHere\n\t(\n\t\tint x\n\t\t, int y\n\t\t, int z\n\t)\n;\n"
						)
					;
					// What follows a lambda called at once is laid out behind its call.
					fExpectStable
						(
							"CalledLambdaTail"
							, 60
							, "void f_Test() { bool Value = ([] { return true; }() && f_Condition(111111111, 222222222, 333333333)); }\n"
							, "void f_Test()\n{\n\tbool Value =\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\treturn true;\n\t\t\t}()\n"
							"\t\t\t&& f_Condition(111111111, 222222222, 333333333)\n\t\t)\n\t;\n}\n"
						)
					;
					fExpectStable
						(
							"AlternativeHeadsFallback"
							, 190
							, "#if A\nvoid f(){\n#else\nvoid g(){\n\n#endif\n\nint x;\n}\n"
							, "#if A\nvoid f()\n{\n#else\nvoid g()\n{\n#endif\n\tint x;\n}\n"
						)
					;
				};

				DMibTestCategory("Readings")
				{
					// Readings the whole tree corrected: a postfix '++' ends its operand, a cast
					// of a type named by Malterlib's naming takes a sign as unary while
					// 'sizeof(void *)' does not, 'noexcept(false)' stands in front of a trailing
					// return type like 'noexcept', a body behind the arrow makes a test macro's
					// arrow one too, a calling convention macro stands apart from the declarator
					// in front of it and keeps the return type from being converted, and the
					// parenthesis behind a pointer to function's declarator is its parameter list.
					fExpect
						(
							"TreeCorrections"
							, "struct C\n{\n\tauto operator = (C &&) noexcept(false)->C & = default;\n"
							"\tvoid * (DMibCrossmoduleAPI * m_fFree)(void * _pMemory, umint _Size);\n"
							"\tinline_always static void *DMibCrossmoduleAPI fs_Alloc(CMemoryManagerCrossModule *_pModule, umint &_Size, ch8 const *_pFile, aint _Line, "
							"EHeapDebugFlag _Flags, EAllocationFlag _AllocFlags, ENumaNode _NumaNode);\n"
							"};\n\nvoid f()\n{\n\tValue = Value * 8 + *pParse++ - '0';\n\tValue = (aint) - 16324 + (smint)-1 + sizeof(void *)*4;\n"
							"\tDMibTestSuite(\"Level {}\"_f << nLevel)->TCFuture<void>\n\t{\n\t\tco_return {};\n\t};\n}\n"
							, "struct C\n{\n\tauto operator = (C &&) noexcept(false) -> C & = default;\n"
							"\tvoid *(DMibCrossmoduleAPI *m_fFree)(void *_pMemory, umint _Size);\n\tinline_always static void * DMibCrossmoduleAPI fs_Alloc\n\t\t(\n"
							"\t\t\tCMemoryManagerCrossModule *_pModule\n\t\t\t, umint &_Size\n\t\t\t, ch8 const *_pFile\n\t\t\t, aint _Line\n"
							"\t\t\t, EHeapDebugFlag _Flags\n\t\t\t, EAllocationFlag _AllocFlags\n\t\t\t, ENumaNode _NumaNode\n\t\t)\n\t;\n};\n\nvoid f()\n{\n"
							"\tValue = Value * 8 + *pParse++ - '0';\n\tValue = (aint)-16324 + (smint)-1 + sizeof(void *) * 4;\n"
							"\tDMibTestSuite(\"Level {}\"_f << nLevel) -> TCFuture<void>\n\t{\n\t\tco_return {};\n\t};\n}\n"
							, false
						)
					;
					// An MSVC '__asm' block holds assembly, which keeps its spelling, and the standard library's
					// aliases of fundamental types spell casts: '(size_t)-1'.
					{
						CStr Source = "void f()\n{\n\t__asm\n\t{\n\t\tmov eax, dword ptr fs:[0h]\n\t\tmov  [pRet],eax\n\t}\n}\n";
						fExpect("MsvcAsmBlock", Source, Source, false);

						// A pointer to function's parameter list opens as a function's does, and its declaration, a typedef
						// above all, has no return type to move behind it.
						{
							CStr Params = "IN HANDLE _ThreadHandle, IN THREADINFOCLASS _ThreadInformationClass, OUT PVOID _ThreadInformation, IN ULONG _ThreadInformationLength";
							CStr Opened = "\t\t(\n\t\t\t" + CStr(Params).f_Replace(", ", "\n\t\t\t, ") + "\n\t\t\t, OUT PULONG _ReturnLength\n\t\t)\n\t;\n";

							fExpect
								(
									"FunctionPointerParameters"
									, "struct C\n{\n\tNTSTATUS (__stdcall *m_fNtQueryInformationThread)(" + Params + ", OUT PULONG _ReturnLength);\n};\n"
									, "struct C\n{\n\tNTSTATUS (__stdcall *m_fNtQueryInformationThread)\n" + Opened + "};\n"
									, false
								)
							;
							fExpect
								(
									"FunctionPointerTypedef"
									, "typedef NTSTATUS (__stdcall *FNtQueryInformationThread)(" + Params + ", OUT PULONG _ReturnLength);\n"
									, "typedef NTSTATUS (__stdcall *FNtQueryInformationThread)\n" + CStr(Opened).f_Replace("\n\t", "\n").f_Replace("\t\t(", "\t(") + ""
									, false
								)
							;
						}
						// An attribute qualifies the type in front of it, so the declarator behind it hugs the name.
						fExpect
							(
								"AttributeTypeDeclarator"
								, "void f()\n{\n\tumint __attribute__((address_space(256))) * pAddress = (umint __attribute__((address_space(256))) *)0x8;\n}\n"
								, "void f()\n{\n\tumint __attribute__((address_space(256))) *pAddress = (umint __attribute__((address_space(256))) *)0x8;\n}\n"
								, false
							)
						;
						fExpect
							(
								"StandardAliasCast"
								, "void f()\n{\n\tp = (CFoo *)(void *)(size_t)-1;\n\tq = (uintptr_t) pMemory - (uintptr_t) pHead;\n}\n"
								, "void f()\n{\n\tp = (CFoo *)(void *)(size_t)-1;\n\tq = (uintptr_t)pMemory - (uintptr_t)pHead;\n}\n"
								, false
							)
						;
					}
					// A calling convention only stands in a declarator, so a pointer to function's declarator
					// holding one stands apart from the return type in front of it, whatever that names.
					fExpect
						(
							"CallingConventionDeclarator"
							, "struct C\n{\n\tNTSTATUS  (__stdcall *m_pLoad)(PWCHAR _pPath);\n\tNTSTATUS(__cdecl *m_pFree)(int _A);\n};\n"
							, "struct C\n{\n\tNTSTATUS (__stdcall *m_pLoad)(PWCHAR _pPath);\n\tNTSTATUS (__cdecl *m_pFree)(int _A);\n};\n"
							, false
						)
					;
					// A lambda's template parameter list is one however it is spaced.
					fExpect("LambdaTemplateHeader", "auto f = [] < class T > (T a)\n\t{\n\t\treturn a;\n\t}\n;\n", "auto f = []<class T>(T a)\n\t{\n\t\treturn a;\n\t}\n;\n");
					// A name in front of a return type can be a specifier no conversion knows, so the type stays where it is.
					fExpect("UnknownSpecifierReturn", "API int f(int a);\n", "API int f(int a);\n");
					// A list's trailing comma inside a macro's argument separates the macro's arguments, however deep the list.
					CStr MacroList = "#define DInit(a, b, c) a, b, c\nint a[][2] = DInit\n\t(\n\t\t{\n\t\t\t{\n\t\t\t\t1\n\t\t\t\t, 2\n\t\t\t\t,\n\t\t\t}\n\t\t}\n\t)\n;\n";
					fExpect("NestedMacroListComma", MacroList, MacroList);
					// A header name '__has_include' asks for keeps its spaces across a line continuation.
					fExpect("HasIncludeContinuation", "#if __has_include(<dir/ \\\n    header.h>)\n#endif\n", "#if __has_include(<dir/ \\\n    header.h>)\n#endif\n");
					// So does one in a lambda's body that a macro's argument holds: braces shield no comma from the macro.
					fExpect
						(
							"LambdaInMacroListComma"
							, "#define DWrap(a, b, c) a, b, c\nint f()\n{\n\treturn DWrap([] { int a[] =\n{\n1\n, 2\n,\n}; return a[0]; }());\n}\n"
							, "#define DWrap(a, b, c) a, b, c\nint f()\n{\n\treturn DWrap\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\tint a[] =\n\t\t\t\t\t{\n"
							"\t\t\t\t\t\t1\n\t\t\t\t\t\t, 2\n\t\t\t\t\t\t,\n\t\t\t\t\t}\n\t\t\t\t;\n\t\t\t\treturn a[0];\n\t\t\t}()\n\t\t)\n\t;\n}\n"
						)
					;
					// A macro the source defines can expand to specifiers, which no trailing return type holds.
					fExpect
						(
							"DefinedMacroReturn"
							, "#define DReturn inline int\nDReturn fg_Function(int _@, int _B@, int _C@);\n"
							, "#define DReturn inline int\nDReturn fg_Function\n\t(\n\t\tint _@\n\t\t, int _B@\n\t\t, int _C@\n\t)\n;\n"
						)
					;
					// An operator outside a default argument makes the parenthesis a constructor's arguments.
					fExpect
						(
							"ConstructedFromExpression"
							, "void f()\n{\n\tCResult Variable(CValue::@ + CValue::@ + CValue::@);\n}\n"
							, "void f()\n{\n\tCResult Variable\n\t\t(\n\t\t\tCValue::@\n\t\t\t+ CValue::@\n\t\t\t+ CValue::@\n\t\t)\n\t;\n}\n"
						)
					;
					// A 'do' loop's 'while' stands under its 'do', also where a guard sets the loop in.
					CStr DoWhile = "void f()\n{\n\tif (a)\n\t\tdo\n\t\t\tf_Go();\n\t\twhile (b);\n\tif (c)\n\t\tdo\n\t\t{\n\t\t\tf_Go();\n\t\t}\n\t\twhile (b);\n"
						"\tdo\n\t\tf();\n\twhile (x);\n}\n"
					;
					fExpect("GuardedDoWhile", DoWhile, DoWhile);
					// A block comment split behind its opening keeps the file's lone carriage returns.
					fExpect("BlockCommentSplitCR", "/*  A\r    B\r*/\rint a;\r", "/*\rA\rB\r*/\rint a;\r", false);
					// An MSVC '__asm' block is the assembler's from its brace on, a comment in front of the brace and braces in the
					// assembler's ';' comments included.
					fExpect
						(
							"AssemblyBehindComment"
							, "void f()\n{\n\t__asm /* Assembly */\n\t{\n\t\tmov eax, 1 ; save eax;\n\t}\n}\n"
							, "void f()\n{\n\t__asm // Assembly\n\t{\n\t\tmov eax, 1 ; save eax;\n\t}\n}\n"
							, false
						)
					;
					CStr AssemblyComments = "void f()\n{\n\t__asm\n\t{\n\t\tmov eax, 1 ; } {\n\t\tmov ebx, 2 ; save ebx;\n\t}\n}\n";
					fExpect("AssemblyCommentBraces", AssemblyComments, AssemblyComments);
					// GNU's '__label__' only starts a block, which keeps its braces.
					CStr LocalLabel = "void f()\n{\n\tif (x)\n\t{\n\t\t__label__ done;\n\t}\n}\n";
					fExpect("LocalLabelBraces", LocalLabel, LocalLabel);
					// The heads of a conditional keep the lines their directives stand on.
					CStr SharedParameters = "#if A\nvoid fg_First(\n#else\nvoid fg_Second(\n#endif\nint a);\n";
					fExpect("HeadsKeepDirectiveLines", SharedParameters, SharedParameters);
					// A trailing comment whose lines nest or part paragraphs keeps its block.
					CStr NestedDocumentation = "int a; /**< - Parent\n *   - Nested\n *\n * Separate paragraph.\n */\n";
					fExpect("StructuredTrailingComment", NestedDocumentation, NestedDocumentation);
					// A parameter named behind an attribute in a nested declarator hides a return type of that name.
					fExpect("AttributedNestedParameter", "using X@ = int;\nX@ fg_Function@(int (* [[]] X@)());\n", "using X@ = int;\nX@ fg_Function@\n\t(\n\t\tint (* [[]] X@)()\n\t)\n;\n");
					// Behind a parenthesis another macro can expand to a macro's name, so a list there keeps its trailing comma.
					fExpect
						(
							"IndirectMacroListComma"
							, "#define M(A, B, C) 0\n#define GET() M\nint n = GET()({1,\n2,\n});\n"
							, "#define M(A, B, C) 0\n#define GET() M\nint n = GET()({1, 2,});\n"
						)
					;
					// An embedded resource's name keeps its blanks across a continuation, as a header's does.
					CStr Embed = "#embed <dir/\\\n  data.bin>\n";
					fExpect("EmbedContinuation", Embed, Embed);
					// An operator's alternative spelling makes the parenthesis a constructor's arguments.
					fExpect
						(
							"AlternativeOperatorArguments"
							, "void f()\n{\n\tCBoolType Value@@@(a not_eq b);\n}\n"
							, "void f()\n{\n\tCBoolType Value@@@\n\t\t(\n\t\t\ta not_eq b\n\t\t)\n\t;\n}\n"
						)
					;
					// An enum's list written over lines starts under the brace as one written on one line does.
					fExpect("EnumeratorsOverLines", "enum E { A,\n B };\n", "enum E\n{\n\tA\n\t, B\n};\n");
					// A macro's argument keeps its qualifiers where they are: the macro can paste to them or take a name, no type.
					CStr PastedArgument = "#define DType(T) T##32\nusing int32 = int;\nDType(const int) g_Value = 0;\n";
					fExpect("QualifierInMacroArgument", PastedArgument, PastedArgument);
					CStr NamedArgument = "#define DECL(x) int x\nvoid f()\n{\n\tDECL(const a) = 1;\n}\n";
					fExpect("QualifierInDefinedMacroArgument", NamedArgument, NamedArgument);
					// A literal continued behind a vertical tab keeps its next line's blanks.
					CStr VerticalTabSplice = "#define DText \"A \\\v\n    B\"\n";
					fExpect("LiteralSpliceBehindVerticalTab", VerticalTabSplice, VerticalTabSplice);
					// A '*' starting a comment's first line is its text, and lines without a margin at several depths are a
					// structure the conversion keeps.
					fExpect("CommentStartingWithStar", "int x; /* *p points at the result\ncontinued */\n", "int x; // *p points at the result\n\t   // continued\n", false);
					fExpect("UndecoratedNestedComment", "int x; /* - outer\n     - inner\n   - other */\n", "int x; /* - outer\n\t - inner\n   - other */\n", false);
					// A type the naming lists in front of '::' proves nothing of the name behind it.
					fExpect
						(
							"QualifiedValueArgument"
							, "void f()\n{\n\tCLongNamedTypeForFormatting@ variable_with_long_name@(COptions::mc_Value);\n}\n"
							, "void f()\n{\n\tCLongNamedTypeForFormatting@ variable_with_long_name@\n\t\t(\n\t\t\tCOptions::mc_Value\n\t\t)\n\t;\n}\n"
						)
					;
					// A parameter named behind an extended qualifier hides a return type of that name.
					CStr RestrictParameter = "CResult fg_Function@@(int (* __restrict CResult)[1]);\n";
					fExpect("RestrictNestedParameter", RestrictParameter, RestrictParameter);
					// C++ has no 'restrict', which names a parameter as any other word does.
					CStr RestrictName = "using restrict = int;\nrestrict fg_Function@@(int restrict, int _Other);\n";
					fExpect("RestrictNamedParameter", RestrictName, RestrictName);
					// GNU spells an attribute with or without the trailing underscores, and the parameter's name stands in front of either.
					CStr AttributedName = "using CResult = int;\nCResult fg_Function@@(int CResult __attribute((unused)), int _Other);\n";
					fExpect("ShortAttributeParameter", AttributedName, AttributedName);
					// A clause macro can take an 'else', so no braces are added around it.
					fExpect
						(
							"ClauseMacroElse"
							, "#define IF(x) if (x)\nvoid f()\n{\n\tif (a) IF(b) { f(); } else { g(); }\n}\n"
							, "#define IF(x) if (x)\nvoid f()\n{\n\tif (a)\n\t\tIF(b)\n\t\t{\n\t\t\tf();\n\t\t}\n\telse\n\t\tg();\n}\n"
							, false
						)
					;
					// Specifiers in a macro's argument keep their order, which the macro can paste.
					CStr PastedSpecifiers = "#define DECL(x) int x ## _value\nDECL(constexpr static) = 1;\n";
					fExpect("SpecifiersInMacroArgument", PastedSpecifiers, PastedSpecifiers);
					// The header name of an include continued over lines keeps its blanks, also behind a comment in front of the
					// keyword.
					CStr ContinuedInclude = "#/**/include <dir/\\\n header.h>\n";
					fExpect("CommentedContinuedInclude", ContinuedInclude, ContinuedInclude);
					// A terminator behind a macro statement that ends nothing goes, as behind any other.
					fExpect("TerminatorBehindMacro", "void f() { DMibCheck(a);; }\n", "void f()\n{\n\tDMibCheck(a);\n}\n", false);
					// A call with a name behind it can be a clause macro, which takes an 'else' the braces keep from it.
					CStr ClauseMacroBody = "#define IF(x) if (x)\nvoid f()\n{\n\tif (a)\n\t{\n\t\tIF(b) fg_Call();\n\t}\n\telse\n\t\tfg_Get();\n}\n";
					fExpect("ClauseMacroKeepsBraces", ClauseMacroBody, ClauseMacroBody);
					// A literal continued behind an escaped backslash keeps its next line's blanks: splices go before escapes.
					CStr EscapedSplice = "#define DText \"A \\\\\\\\\n    B\"\n";
					fExpect("LiteralSpliceBehindEscape", EscapedSplice, EscapedSplice);
					// A name the naming lists as a macro's stands for no parameter's name.
					fExpect
						(
							"MacroBehindParameter"
							, "#define DAttr __attribute__((unused))\ntypedef int CReturnTypeWithALongName;\nCReturnTypeWithALongName f_Function@@(int CReturnTypeWithALongName DAttr);\n"
							, "#define DAttr __attribute__((unused))\ntypedef int CReturnTypeWithALongName;\nCReturnTypeWithALongName f_Function@@\n"
							"\t(\n\t\tint CReturnTypeWithALongName DAttr\n\t)\n;\n"
						)
					;
					// An empty statement stays in a structure a macro cuts, and in front of a directive.
					CStr MacroLoop = "#define DLoop for (int i = 0\nvoid f()\n{\n\tDLoop;;)\n\t{\n\t\tbreak;\n\t}\n}\n";
					fExpect("EmptyStatementInCutStructure", MacroLoop, MacroLoop);
					fExpect
						(
							"EmptyStatementBeforeDirective"
							, "int f()\n{\n\treturn ({ 1;;\n#if X\n\t\t2;\n#endif\n\t});\n}\n"
							, "int f()\n{\n\treturn\n\t\t(\n\t\t\t{\n\t\t\t\t1;;\n#if X\n\t\t\t\t2;\n#endif\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// Continuation lines are aligned in a file whose lines end in a lone CR as in any other.
					fExpect
						(
							"ContinuationAlignedCR"
							, "void f()\r{\rauto s = fg_Call(\"a\"\r\"b\", x);\r}\r"
							, "void f()\r{\r\tauto s = fg_Call\r\t\t(\r\t\t\t\"a\"\r\t\t\t\"b\"\r\t\t\t, x\r\t\t)\r\t;\r}\r"
						)
					;
					// A name in front of another can be a macro making a clause, which takes an 'else' the braces keep from it.
					CStr ObjectClause = "#define GUARD if (b)\nvoid f()\n{\n\tif (a)\n\t{\n\t\tGUARD fg_Call();\n\t}\n\telse\n\t\tfg_Other();\n}\n";
					fExpect("ObjectLikeClauseMacroKeepsBraces", ObjectClause, ObjectClause);
					// Only a backslash ending its line splices, so one in a directive's comment text is not aligned.
					CStr CommentBackslashes = "#define DValue(x) x \\\n\t/* a\\b\n\t   c\\d */\nint x;\n";
					fExpect("DirectiveCommentBackslashes", CommentBackslashes, CommentBackslashes);
					// A call behind a macro's call can be a macro's too, whose argument keeps its qualifiers.
					CStr ProducedCallee = "#define DType(T) T##32\n#define DGet() DType\nDGet()(const int) x = 1;\n";
					fExpect("QualifierBehindMacroProducedCallee", ProducedCallee, ProducedCallee);
					CStr ProducedChain = "#define DMakeType(T) T##32\n#define DNext() DMakeType\n#define DGet() DNext\nDGet()()(const int) value = 0;\n";
					fExpect("QualifierBehindMacroProducedChain", ProducedChain, ProducedChain);
					// A macro's argument can be turned into a string, so its braces and empty statements stay.
					fExpect
						(
							"StatementsInMacroArgument"
							, "#define DStr(x) #x\nauto text = DStr([] { if (true) { f(); } });\nauto other = DStr([] { f();; g(); });\n"
							, "#define DStr(x) #x\nauto text = DStr\n\t(\n\t\t[]\n\t\t{\n\t\t\tif (true)\n\t\t\t{\n\t\t\t\tf();\n\t\t\t}\n\t\t}\n\t)\n;\n"
							"auto other = DStr\n\t(\n\t\t[]\n\t\t{\n\t\t\tf();;\n\t\t\tg();\n\t\t}\n\t)\n;\n"
						)
					;
					// The last statement of a GNU statement expression can be the empty one, behind a macro expanding to nothing.
					fExpect
						(
							"EmptyStatementInStatementExpression"
							, "#define DEmpty\nint f()\n{\n\treturn ({ 1;; DEmpty });\n}\n"
							, "#define DEmpty\nint f()\n{\n\treturn\n\t\t(\n\t\t\t{\n\t\t\t\t1;; DEmpty\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// A list in a grouping parenthesis of a macro's argument keeps its trailing comma, as does a declaration there
					// its return type.
					fExpect("ListInGroupedMacroArgument", "#define DStr(x) #x\nauto s = DStr((CThing{1,\n2,\n}));\n", "#define DStr(x) #x\nauto s = DStr((CThing{1, 2,}));\n");
					fExpect
						(
							"DeclarationInMacroArgument"
							, "#define DStr(...) #__VA_ARGS__\nauto s = DStr(struct C { CResult@ fg_Function@@(int a, int b, int c); });\n"
							, "#define DStr(...) #__VA_ARGS__\nauto s = DStr\n\t(\n\t\tstruct C\n\t\t{\n\t\t\tCResult@ fg_Function@@\n\t\t\t\t(\n"
							"\t\t\t\t\tint a\n\t\t\t\t\t, int b\n\t\t\t\t\t, int c\n\t\t\t\t)\n\t\t\t;\n\t\t}\n\t)\n;\n"
						)
					;
					// Every NOLINT a block comment holds suppresses its line.
					fExpect
						(
							"SeveralSuppressionsInComment"
							, "/* NOLINTNEXTLINE(modernize-use-nullptr)\n   NOLINTNEXTLINE(modernize-use-nullptr) */\nvoid f_Test(){int *p=0;}\n"
							, "/* NOLINTNEXTLINE(modernize-use-nullptr)\n   NOLINTNEXTLINE(modernize-use-nullptr) */\nvoid f_Test(){int *p = 0;}\n"
						)
					;
					// A list a lambda's body opens over lines loses its trailing comma at once.
					fExpect("LambdaOpensList", "int values[] = { [] { return 1; }(), };\n", "int values[] =\n\t{\n\t\t[]\n\t\t{\n\t\t\treturn 1;\n\t\t}()\n\t}\n;\n", false);
					// A comment in a gap a conversion rewrites keeps the conversion from being made.
					{
						DMibTestPath("CommentBeforeElse");
						CStr Source = "void f() { if (a) { g(); } /* keep */ else h(); }\n";
						auto Result = fg_AnalyzeCodeFormatting(fg_Request(Source));
						DMibExpect(fg_ApplyCodeFormattingEdits(Source, Result.m_Edits).f_Find("/* keep */"), >=, 0);
					}
					fExpect
						(
							"CommentBehindParameters"
							, "LongReturnTypeName@ f_ComputeSomething@@(int x) /* keep */;\n"
							, "LongReturnTypeName@ f_ComputeSomething@@\n\t(\n\t\tint x\n\t) /* keep */\n;\n"
						)
					;
					// A NOLINT keeps the rows a directive lays out, and one in a directive's comment its next line.
					CStr SuppressedColumns = "int a[] =\n\t{\n\t\t// malterlib-format columns: 2\n\t\t// NOLINTNEXTLINE(readability-magic-numbers)\n\t\t42, 53, 64, 75\n\t}\n;\n";
					fExpect("SuppressedDirectiveRows", SuppressedColumns, SuppressedColumns);
					CStr DirectiveSuppression = "#define A 1 // NOLINTNEXTLINE\nvoid f() { fg_Call(@, @, @, 42); }\n";
					fExpect("SuppressionInDirective", DirectiveSuppression, DirectiveSuppression);
					// Specifiers keep their order where the structure is incomplete, as a brace a macro's argument holds leaves it.
					fExpect("SpecifiersInIncompleteStructure", "const char *s = STR({ constexpr static);\n", "const char *s = STR({constexpr static);\n");
					// An elaborated return type can declare the class the parameters use, so it stays in front of them.
					fExpect("ElaboratedReturnType", "struct CNew@ *fg_Get@@(CNew@ *p);\n", "struct CNew@ *fg_Get@@\n\t(\n\t\tCNew@ *p\n\t)\n;\n");
					// A macro standing last in a parameter can expand to its name, which could hide the return type.
					fExpect
						(
							"MacroAsParameterName"
							, "using CReturnType = int;\n#define NAME CReturnType\nCReturnType f_Function@@(int NAME, int @);\n"
							, "using CReturnType = int;\n#define NAME CReturnType\nCReturnType f_Function@@\n\t(\n\t\tint NAME\n\t\t, int @\n\t)\n;\n"
						)
					;
					// So can one naming a parameter through a nested declarator.
					fExpect
						(
							"MacroAsNestedParameterName"
							, "using CReturnType = int;\n#define NAME CReturnType\nCReturnType f_Function@@(int (*NAME)(int), int @);\n"
							, "using CReturnType = int;\n#define NAME CReturnType\nCReturnType f_Function@@\n\t(\n\t\tint (*NAME)(int)\n\t\t, int @\n\t)\n;\n"
						)
					;
					// 'import' is a name where no declaration starts.
					fExpect("ImportAsName", "void f() { int import = 0; if (import < 3) {} }\n", "void f()\n{\n\tint import = 0;\n\tif (import < 3)\n\t{\n\t}\n}\n");
					// Two '&' joined from across a line break stay apart, which written together would read as one '&&'.
					// A pack index goes on with the type, so the qualifier stays in front of it.
					fExpect("QualifierOnPackIndex", "template <typename... Ts>\nusing First = const Ts...[0];\n", "template <typename ...Ts>\nusing First = const Ts...[0];\n");
					// A qualifier behind a parameter list qualifies the function, also in front of a macro the naming does not list.
					CStr MemberQualifier = "struct C\n{\n\tvirtual char const *what() const any_noexcept;\n};\n";
					fExpect("MemberQualifierBeforeMacro", MemberQualifier, MemberQualifier);
					// A macro's name ending at a splice keeps its next line's start, which tells a function-like macro from another.
					CStr NameAtSplice = "#define F\\\n(x) x\nint y = F(1);\n";
					fExpect("DefineNameAtSplice", NameAtSplice, NameAtSplice);
					// No braces wrap a clause behind two attributes, which can hide a clause taking an 'else'.
					CStr TwoAttributes = "void f()\n{\n\tif (a) while (b) [[likely]] [[likely]] if (c) x++; else x--;\n}\n";
					DMibExpect(fg_FormatSource(TwoAttributes, "TwoAttributesBeforeClause").f_Find("{\n\t\t\t[[likely]]"), <, 0);
					// In a 'typedef' a name in front of a pointer to function's declarator is its return type, which stands apart from
					// it, so the declaration is laid out whatever names the naming lists.
					fExpect
						(
							"TypedefFunctionPointer"
							, "typedef\nLONG\n(__stdcall *PC_LANGUAGE_EXCEPTION_HANDLER)(\n   __in PEXCEPTION_POINTERS ExceptionPointers,\n   __in ULONG64 EstablisherFrame\n   );\n"
							, "typedef LONG (__stdcall *PC_LANGUAGE_EXCEPTION_HANDLER)(__in PEXCEPTION_POINTERS ExceptionPointers, __in ULONG64 EstablisherFrame);\n"
						)
					;
					fExpect("AmpersandsJoinedApart", "int f(CMask mask, CMask value)\n{\n\treturn mask &\n&value;\n}\n", "int f(CMask mask, CMask value)\n{\n\treturn mask & &value;\n}\n");
					fExpect("ImportStartsExpression", "void f(int import) { import < 3 ? f(1) : f(2); }\n", "void f(int import)\n{\n\timport < 3 ? f(1) : f(2);\n}\n");
					// A '>>=' is a shift assignment, which closes no template argument list.
					// A '>>' in front of an Objective-C literal may shift it, as in front of any other operand: it stays one token, and
					// the statement, whose brackets then do not nest, keeps its lines.
					fExpect("ShiftBeforeObjectiveCLiteral", "void f() { x = a<b>>@(c); }\n", "void f() { x = a<b >> @(c); }\n");
					fExpect("ShiftAssignmentClosesNoList", "void f() { a<b ? c<d : e >>= f; }\n", "void f()\n{\n\ta < b ? c < d : e >>= f;\n}\n");
					// A directive's literal is data, and suppresses nothing.
					fExpect("SuppressionInDirectiveLiteral", "#define Message \"NOLINTNEXTLINE\"\nint x; int y;\n", "#define Message \"NOLINTNEXTLINE\"\nint x;\nint y;\n");
					// No braces wrap a chain an attribute hides a clause in, whose 'else' is then not counted.
					fExpect
						(
							"AttributedClauseInChain"
							, "void f()\n{\n\tif (a) for (;a;) [[likely]] if (b) a++; else b++;\n}\n"
							, "void f()\n{\n\tif (a)\n\t\tfor (; a;) [[likely]] if (b) a++;\n\telse\n\t\tb++;\n}\n"
						)
					;
					// A comment in front of an 'else' keeps the braces in front of it on every pass.
					fExpect
						(
							"CommentBeforeElseConverges"
							, "void f(int x) { if (x) { x++; } /* note */ else { x--; } }\n"
							, "void f(int x)\n{\n\tif (x)\n\t{\n\t\tx++;\n\t}\n\t/* note */ else\n\t\tx--;\n}\n"
							, false
						)
					;
					// A definition is set off from the declaration behind a block comment on its line.
					fExpect("BlankBehindDefinitionComment", "struct CValue {}; /* note */ void f();\n", "struct CValue\n{\n};\n\n/* note */ void f();\n");
					// A lambda guarded twice takes braces at both guards in one pass: its captures are no attribute.
					fExpect
						(
							"GuardedLambdaChain"
							, "void f(bool a, bool b) { if (a) if (b) [] { return 0; }(); }\n"
							, "void f(bool a, bool b)\n{\n\tif (a)\n\t{\n\t\tif (b)\n\t\t{\n\t\t\t[]\n\t\t\t{\n\t\t\t\treturn 0;\n\t\t\t}();\n\t\t}\n\t}\n}\n"
							, false
						)
					;
					// A '>>' in front of a lambda shifts, which its two closers would not.
					{
						DMibTestPath("ShiftBeforeLambda");
						CStr Source = "int f(int L, int R)\n{\n\treturn L<R>>[]{ return 1; }();\n}\n";
						auto Result = fg_AnalyzeCodeFormatting(fg_Request(Source));
						DMibExpect(fg_ApplyCodeFormattingEdits(Source, Result.m_Edits).f_Find(">>"), >=, 0);
					}
					// A comma a NOLINT line holds stays where it is.
					CStr SuppressedList = "int a[] =\n\t{\n\t\t1, // NOLINT\n\t\t2 // NOLINT\n\t}\n;\n";
					fExpect("SuppressedListComma", SuppressedList, SuppressedList);
				};

				DMibTestCategory("Body")
				{
					// In a function's body a type in front of a name and a parenthesis defines a
					// variable, never converted to a trailing return type; a comment in front of
					// a statement's first token stays there; and a statement opening with an
					// attribute on its clause's line is laid out at the clause's indentation.
					fExpect
						(
							"VariableCommentAttribute"
							, "void f()\n{\n\tTCUniquePointer<CTestCopyMove> TestMove10(fg_Construct(@, @, CCopyMove()));\n"
							"\tif(DuplicateHandle(@, @, GetCurrentProcess()))\n\t{\n\t\tg();\n\t}\n"
							"\t/*CResult &Result =*/ mp_Results.f_Insert(_Measure);\n\tif (bFlag) [[unlikely]]\n\t{\n\t\tif (a)\n\t\t{\n\t\t\tb();\n\t\t}\n\t\telse\n\t\t\tc();\n\t}\n}\n"
							, "void f()\n{\n\tTCUniquePointer<CTestCopyMove> TestMove10\n\t\t(\n\t\t\tfg_Construct(@, @, CCopyMove())\n\t\t)\n\t;\n"
							"\tif (DuplicateHandle(@, @, GetCurrentProcess()))\n\t\tg();\n"
							"\t/*CResult &Result =*/ mp_Results.f_Insert(_Measure);\n\tif (bFlag) [[unlikely]]\n\t{\n\t\tif (a)\n\t\t\tb();\n\t\telse\n\t\t\tc();\n\t}\n}\n"
							, false
						)
					;
				};
			};

			DMibTestSuite("Directives")
			{
				// A splice that joins two lines' text into one token keeps the directive's lines: indenting the second would part them.
				fg_ExpectFormat("SpliceJoinsTokens", "#define DAdd\\\n(x) ((x) + 1)\n#define DType lo\\\nng\nint x;\n", "#define DAdd\\\n(x) ((x) + 1)\n#define DType lo\\\nng\nint x;\n");
				// A block comment in front of a '#' still leaves it starting a directive, and a splice inside a literal continues
				// the literal, whose blanks stay.
				fg_ExpectFormat("CommentBeforeDirective", "/* label */ #define DValue const int\nDValue X = 0;\n", "/* label */ #define DValue const int\nDValue X = 0;\n");
				fg_ExpectFormat("SplicedLiteralBlankBehind", "#define DText \"A \\ \n  B\"\nint x;\n", "#define DText \"A \\ \n  B\"\nint x;\n");
				fg_ExpectFormat("SplicedLiteral", "#define DText \"A \\\n    B\" \\\n        \"C\"\nint x;\n", "#define DText \"A \\\n    B\" \\\n        \"C\"\nint x;\n");

				DMibTestCategory("AlternativeHeads")
				{
					// A conditional whose branches each open the same brackets writes alternative heads for one body: each
					// is laid out as if it were the only one, and the body once.
					fg_ExpectFormat
						(
							"SharedBody"
							, "bool f()\n{\n  for (auto x : y) {\n#if A\n      if (a(x)) {\n#else\n      if (b(x)) {\n#endif\n        return true;\n      }\n  }\n"
							"  return false;\n}\n"
							, "bool f()\n{\n\tfor (auto x : y)\n\t{\n#if A\n\t\tif (a(x))\n\t\t{\n#else\n\t\tif (b(x))\n\t\t{\n#endif\n\t\t\treturn true;\n"
							"\t\t}\n\t}\n\treturn false;\n}\n"
						)
					;
					// Inside a conditional of its own, as an include guard is.
					fg_ExpectFormat
						(
							"InsideGuard"
							, "#ifndef G\n#define G\nvoid f()\n{\n#if A\n  if (a) {\n#elif B\n  if (b) {\n#else\n  if (c) {\n#endif\n    g();\n  }\n}\n#endif\n"
							, "#ifndef G\n#define G\nvoid f()\n{\n#if A\n\tif (a)\n\t{\n#elif B\n\tif (b)\n\t{\n#else\n\tif (c)\n\t{\n#endif\n\t\tg();\n\t}\n}\n#endif\n"
						)
					;
					// Branches leaving different brackets open are pieces of no one construct, and the file keeps its layout.
					{
						DMibTestPath("DifferentOpens");

						CStr Source = "int f()\n{\n#if A\n  if (a) {\n#else\n  while (b) (\n#endif\n  return 1;\n  }\n}\n";
						auto Result = fg_AnalyzeCodeFormatting(fg_Request(Source));

						bool bStructure = false;
						for (auto const &Diagnostic : Result.m_Diagnostics)
							bStructure = bStructure || (Diagnostic.m_Rule == "structure" && Diagnostic.m_iLine == 3);

						DMibExpectTrue(bStructure);
					}
					// Heads a variant would split leave the indentation of its next line in front of the directive, and the file
					// keeps its layout.
					{
						DMibTestPath("SplitHeads");

						CStr Source = "#if A\nvoid fg_LongFunctionName(\n#else\nvoid fg_ShortFunctionName(\n#endif\nint a,int b)\n{ foo(); }\n";
						auto Request = fg_Request(Source);
						auto Properties = fg_MalterlibProperties();
						Properties["max_line_length"] = "30";
						Request.m_Settings = CCodeFormattingSettings(Properties);

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					}
					// The directive lines take the line ending the settings ask for, as the rest do.
					{
						DMibTestPath("LineEndings");

						CStr Source = "#if A\nint f() {\n#else\nint g() {\n#endif\n\treturn 1;\n}\n";
						auto Request = fg_Request(Source);
						auto Properties = fg_MalterlibProperties();
						Properties["end_of_line"] = "crlf";
						Request.m_Settings = CCodeFormattingSettings(Properties);

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
						DMibExpect(fg_ApplyCodeFormattingEdits(Source, Result.m_Edits), ==, "#if A\r\nint f()\r\n{\r\n#else\r\nint g()\r\n{\r\n#endif\r\n\treturn 1;\r\n}\r\n");
					}
				};

				DMibTestCategory("Disabled")
				{
					// The parenthesis of a call the naming lists as unformatted keeps its text, and the code around it is laid out.
					{
						DMibTestPath("UnformattedCall");

						CCodeFormattingStyle Style = *fg_MalterlibStyle();
						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nUnformattedCalls:\n  - 'BSON_STR'\n"));

						CStr Source = "void f()\n{\n    fg_Check(fg_Json(),BSON_STR({\n      \"a\" : [ 1,\n   2 ]\n    }));\n}\n";
						auto Request = fg_Request(Source);
						Request.m_pStyle = fg_Construct(fg_Move(Style));

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
						DMibExpect(fg_ApplyCodeFormattingEdits(Source, Result.m_Edits), ==, "void f()\n{\n\tfg_Check(fg_Json(), BSON_STR({\n      \"a\" : [ 1,\n   2 ]\n    }));\n}\n");
					}
					// Nothing is inserted in front of its closing parenthesis either, where its argument makes the line too long.
					{
						DMibTestPath("UnformattedCallCloser");

						CCodeFormattingStyle Style = *fg_MalterlibStyle();
						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nUnformattedCalls:\n  - 'f_Unformatted'\n"));

						auto Properties = fg_MalterlibProperties();
						Properties["max_line_length"] = "60";
						CStr Source = "void f()\n{\n\tf_Unformatted(AVeryLongIdentifierNameThatGoesOnAndOnAndOnForeverAndEver);\n}\n";
						auto Request = fg_Request(Source);
						Request.m_Settings = CCodeFormattingSettings(Properties);
						Request.m_pStyle = fg_Construct(fg_Move(Style));

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
						DMibExpect
							(
								fg_ApplyCodeFormattingEdits(Source, Result.m_Edits)
								, ==
								, "void f()\n{\n\tf_Unformatted\n\t\t(AVeryLongIdentifierNameThatGoesOnAndOnAndOnForeverAndEver)\n\t;\n}\n"
							)
						;
					}
					// The call's parenthesis stands behind its template arguments.
					{
						DMibTestPath("UnformattedTemplateCall");

						CCodeFormattingStyle Style = *fg_MalterlibStyle();
						Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nUnformattedCalls:\n  - 'f_Unformatted'\n"));

						CStr Source = "void f()\n{\n\tf_Unformatted<int>( 1,2 );\n}\n";
						auto Request = fg_Request(Source);
						Request.m_pStyle = fg_Construct(fg_Move(Style));

						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpect(fg_ApplyCodeFormattingEdits(Source, Result.m_Edits), ==, Source);
					}
					fg_ExpectFormat
						(
							"Region"
							, "void f()\n{\n\t// malterlib-format off\n        int a;\n\t// malterlib-format on\n        int b;\n}\n"
							, "void f()\n{\n\t// malterlib-format off\n        int a;\n\t// malterlib-format on\n\tint b;\n}\n"
						)
					;
					for (CStr Ending : {"\n", "\r\n", "\r"})
					{
						CStr Source = CStr("/*@malterlib-format off@    verbatim@malterlib-format on@*/@int a;@").f_Replace("@", Ending);
						fg_ExpectFormat(Ending == "\r" ? "BlockCommentRegionCR" : Ending == "\n" ? "BlockCommentRegionLF" : "BlockCommentRegionCRLF", Source, Source);
					}
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
					fExpectFailed("MissingOnInBlockComment", "/*\n\tmalterlib-format off\n*/\nint a;\n");
					fExpectFailed("UnknownDirective", "// malterlib-format sideways\nint a;\n");
					fExpectFailed("ColumnsOutsideList", "// malterlib-format columns: 4\nint a;\n");
					fExpectFailed("ColumnsWithoutCount", "int a[] =\n\t{\n\t\t// malterlib-format columns: x\n\t\t1\n\t}\n;\n");
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

				auto Charset = fg_Request("int a;   \n");
				Charset.m_Settings.m_Charset = "latin1";
				fExpectUnsupported("Charset", Charset);

				fExpectUnsupported("BrokenEncoding", fg_Request("int a; // \xC3\n"));
				fExpectUnsupported("HeaderUnitBeforeConditional", fg_Request("import <vector>\n#if A\n;\n#else\n;\n#endif\n"));
				fExpectUnsupported("HeaderUnitBehindCarriageReturnComment", fg_Request("import <map> // a\r;\n"));
				fExpectUnsupported("OverlongEncoding", fg_Request("int   x=1; // \xC0\xAF\n"));
				fExpectUnsupported("EncodedSurrogate", fg_Request("int   x=1; // \xED\xA0\x80\n"));
				fExpectUnsupported("EmbeddedNul", fg_Request(CStr("int a;\0   \n", 11)));
				fExpectUnsupported("Unterminated", fg_Request("/* open\n"));
				fExpectUnsupported("Digraph", fg_Request("int f() <% return 0; %>\n"));
				fExpectUnsupported("TrigraphSpliceInDirective", fg_Request("#define BODY ?" "?/\nif (a) { f(); g(); }\n"));
				// A user-defined literal's suffix stands against the literal, which a blank between them would change.
				{
					DMibTestPath("LiteralSuffixAdjacency");
					DMibExpectFalse(fg_HasEquivalentCodeTokens("auto x = \"a\"_s;", "auto x = \"a\" _s;"));
					DMibExpectTrue(fg_HasEquivalentCodeTokens("auto x = \"a\"_s;", "auto  x = \"a\"_s;"));
				}
				fExpectUnsupported("TrigraphSplicedCommentEnd", fg_Request("int a; /* x *?" "?/\n/ int b; /* y */\n"));
				fExpectUnsupported("SplicedCommentEnd", fg_Request("int a; /* x *\\\n/ int b; /* y */\n"));
				fExpectUnsupported("DoublySplicedCommentEnd", fg_Request("void f()\n{\n/* x *\\\n\\\n/ int b; /* y */\n}\n"));
				fExpectUnsupported("NamedUniversalCharacter", fg_Request("int \\N{TIBETAN LETTER -A} = 0;\n"));
				// An MSVC '__asm' without braces takes the rest of its line, which no layout may join to the next.
				fExpectUnsupported("UnbracedAsm", fg_Request("void f()\n{\n\t__asm nop\n\treturn;\n}\n"));
				// A diagnostic of a converted source covers the original bytes the text it names replaced.
				{
					DMibTestPath("ConvertedDiagnosticSpan");
					CStr Source = "void f(){if (true) if (true) {f();}}";
					auto Result = fg_AnalyzeCodeFormatting(fg_Request(Source));
					umint nPast = 0;
					for (auto const &Diagnostic : Result.m_Diagnostics)
						nPast += Diagnostic.m_iOffset + Diagnostic.m_nLength > Source.f_GetLen();

					DMibExpect(nPast, ==, 0u);
				}
				// A block comment written as line comments splits on a lone CR as well.
				{
					DMibTestPath("CommentLinesEndInCR");
					auto Result = fg_AnalyzeCodeFormatting(fg_Request("int x; /* a\rb */\rint y;\r"));
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A gap beside a comment is measured as the spacing rules write it, so the result is stable.
				{
					DMibTestPath("CommentGapMeasured");
					auto Request = fg_Request("void f(){auto x=f(/*a*/ 1,/*b*/ 2)+g();}\n");
					Request.m_Settings.m_nMaxColumns = 28;
					DMibExpectTrue(fg_AnalyzeCodeFormatting(Request).m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A qualifier behind a type and a specifier already stands behind its type.
				fg_ExpectFormat("QualifierBehindSpecifier", "int static const x = 1;\n", "int static const x = 1;\n");
				fg_ExpectFormat("QualifierInFrontOfQualifiedDecltype", "const decltype(c)::template X<int> x = 1;\n", "const decltype(c)::template X<int> x = 1;\n");

				// A strict range reports a conversion that needs an edit outside it.
				{
					DMibTestPath("StrictConversion");
					auto Request = fg_Request("const int x;\n");
					Request.m_RangePolicy = ECodeRangePolicy::mc_Strict;
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 0;
					Range.m_nLength = 5;
					bool bBoundary = false;
					for (auto const &Diagnostic : fg_AnalyzeCodeFormatting(Request).m_Diagnostics)
						bBoundary = bBoundary || Diagnostic.m_Rule == "range-boundary";

					DMibExpectTrue(bBoundary);
				}

				// A naming pattern matches in time linear in the name.
				{
					DMibTestPath("PatternBacktracking");
					CCodeFormattingStyle Style = *fg_MalterlibStyle();
					Style.f_Apply(CCodeFormattingStyleDocument::fs_Parse("Version: 1\nTypeNames:\n  - 'a*a*a*a*a*a*a*a*a*a*b.*'\n"));
					CStr Name;
					for (umint i = 0; i < 60; ++i)
						Name += "a";

					DMibExpectTrue(Style.f_GetRoles(Name.f_GetStr(), Name.f_GetLen()) == ECodeNameRole::mc_None);
					Name += "bc";
					DMibExpectTrue(Style.f_GetRoles(Name.f_GetStr(), Name.f_GetLen()) == ECodeNameRole::mc_Type);
				}

				// A '>>' only one '<' matches may be a shift, which stays one token and no line is broken inside.
				{
					DMibTestPath("ShiftAfterLess");
					for (CStr Operand : {"c", "+c"})
					{
						DMibTestPath(Operand);
						auto Request = fg_Request("int f()\n{\n\treturn a<bbbbbbbbbbb>>" + Operand + ";\n}\n");
						Request.m_Settings.m_nMaxColumns = 25;
						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits).f_Find(">>"), >=, 0);
					}
				}

				// A statement behind a head's brace on its line is measured on the line it takes, where its comment no longer fits.
				{
					DMibTestPath("CommentBehindHeadBrace");
					auto Request = fg_Request("namespace N {long long const f(int _A, int _B, int _C, int _D); // comment\n}\n");
					Request.m_Settings.m_nMaxColumns = 60;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits), ==, "namespace N\n{\n\t// comment\n\tlong long const f(int _A, int _B, int _C, int _D);\n}\n");
				}

				fExpectUnsupported("Trigraph", fg_Request("int f(void) ?" "?< return 0; ?" "?>\n"));
				fExpectUnsupported("HeaderUnit", fg_Request("import <vector>;\n"));
				fExpectUnsupported("HeaderUnitWithSlashes", fg_Request("import <sys//types.h>;\n"));
				fExpectUnsupported("HeaderUnitBeforeComment", fg_Request("import <foo/bar.h> /* reason */;\n"));
				fExpectUnsupported("DirectiveCommentAcrossSplice", fg_Request("#define DECL /\\\n* comment\n*/ int x;\nDECL\n"));
				fExpectUnsupported("HeaderUnitBeforeSplice", fg_Request("import <vector> \\\n;\n"));
				fExpectUnsupported("HeaderUnitBehindMacro", fg_Request("#define DECL() int x;\nDECL()\nimport <vector>;\n"));

				// A directive continued over lines ending in a lone CR is verified as one ending in LF is.
				{
					DMibTestPath("DirectiveLinesEndInCR");
					DMibExpectTrue(fg_AnalyzeCodeFormatting(fg_Request("#define X \\\r1\rint y;\r")).m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A line a splice starts still starts a logical line, where a directive is one.
				{
					DMibTestPath("DirectiveAfterSplice");
					auto Request = fg_Request("\\\n#define F (x)\nint b = F;\n");
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits).f_Find("F (x)"), >=, 0);
				}

				fExpectUnsupported("SplicedToken", fg_Request("double x = 1e\\\n+2;\n"));
				fExpectUnsupported("TrigraphSpliceInComment", fg_Request("// ?" "?/\nf(); f();\n"));

				// Template argument lists nested deeper than the structure goes are left unbuilt rather than recursed into.
				{
					DMibTestPath("DeepTemplates");
					CStr Source = "using T = ";
					for (umint i = 0; i < 1000; ++i)
						Source += "A<";

					Source += "int";
					for (umint i = 0; i < 1000; ++i)
						Source += ">";

					Source += ";\n";
					auto Result = fg_AnalyzeCodeFormatting(fg_Request(Source));
					DMibExpectTrue(fg_HasEquivalentCodeTokens(Source, fg_ApplyCodeFormattingEdits(Source, Result.m_Edits)));
				}

				// An include continued over lines keeps them, the blanks of its header name included.
				fg_ExpectFormat("IncludeAcrossSplice", "#include <dir/\\\n header.h>\n", "#include <dir/\\\n header.h>\n");
				// The empty statement ending a GNU statement expression makes it 'void' and stays.
				DMibExpect(fg_FormatSource("auto f() { return ({ 1;; }); }\n", "StatementExpression").f_Find("1;;"), >=, 0);
				// Qualifiers both sides of a 'static' stay in front of the type rather than coming together in a later pass.
				fg_ExpectFormat("QualifiersAroundStatic", "constexpr const static volatile int x = 0;\n", "constexpr const static volatile int x = 0;\n");
				// '__extension__' can start a declaration, which keeps the braces around it.
				fg_ExpectFormat
					(
						"ExtensionKeepsBraces"
						, "void g(bool b)\n{\n\tif (b)\n\t{\n\t\t__extension__ int a = 1;\n\t}\n}\n"
						, "void g(bool b)\n{\n\tif (b)\n\t{\n\t\t__extension__ int a = 1;\n\t}\n}\n"
					)
				;
				// A chain gets its braces around every 'else' its 'if's take, an 'else if' counted once, and they close behind
				// an inner block whose own braces go.
				fg_ExpectFormat
					(
						"ChainElseIf"
						, "void i() { if(a) if(b) g(1); else if(c) g(2); else g(3); else g(4); }\n"
						, "void i()\n{\n\tif (a)\n\t{\n\t\tif (b)\n\t\t\tg(1);\n\t\telse if (c)\n\t\t\tg(2);\n\t\telse\n\t\t\tg(3);\n\t}\n\telse\n\t\tg(4);\n}\n"
						, false
					)
				;
				fg_ExpectFormat
					(
						"ChainInnerBlock"
						, "void j(bool a,bool b){if(a) if(b) {g();} g();}\n"
						, "void j(bool a, bool b)\n{\n\tif (a)\n\t{\n\t\tif (b)\n\t\t\tg();\n\t}\n\tg();\n}\n"
						, false
					)
				;
				// A 'do' in a chain goes on past its body, which the chain's braces would cut off.
				DMibExpect(fg_FormatSource("void h() { if (a) while (b) do g(); while (c); }\n", "ChainDo").f_Find("{\n\t\twhile"), <, 0);
				// A multi-line comment in front of a comma the list's conversion takes out converts in the same pass.
				fg_ExpectFormat("CommentBeforeGoneComma", "enum E {\n A /* x\n y */,\n B\n};\n", "enum E\n{\n\tA // x\n\t  // y\n\t, B\n};\n", false);
				// A tab inside a literal is measured at its widest, so a line it makes too long is split.
				{
					DMibTestPath("TabInLiteral");
					auto Request = fg_Request("auto Value = \"aa\tbbbbbbbbbbbbbbbbbbb\";\n");
					Request.m_Settings.m_nMaxColumns = 40;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits), ==, "auto Value\n\t= \"aa\tbbbbbbbbbbbbbbbbbbb\"\n;\n");
				}
				// A suppression below a comment that moves above its line stays with the code it suppresses.
				{
					DMibTestPath("SuppressionBelowMovedComment");
					auto Request = fg_Request("int x = 0; // This comment fits on its own\n    // NOLINTNEXTLINE\nint y=42;\n");
					Request.m_Settings.m_nMaxColumns = 40;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits).f_Find("// NOLINTNEXTLINE\nint y"), >=, 0);
				}

				// A '>>' one '<' matches with an operand behind it stays a shift, whatever the operand starts with.
				for (CStr Operand : {"*pDelta", "&Delta", "++Delta"})
				{
					DMibTestPath(Operand);
					auto Request = fg_Request("bool r = Alpha<Beta >> " + Operand + ";\n");
					Request.m_Settings.m_nMaxColumns = 25;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits).f_Find(">>"), >=, 0);
				}
				// A list spaced from its name that closes on a '>>' behind another, called, converges.
				{
					DMibTestPath("SpacedListCalled");
					CStr Source = "void f()\n{\n    test_true     <NoexceptDependant<int>>();\n    test_false    <NoexceptDependant<double>>();\n}\n";
					DMibExpectTrue(fg_AnalyzeCodeFormatting(fg_Request(Source)).m_Status == ECodeFormattingStatus::mc_Complete);
				}
				// The comma ending a list in a call can separate a macro's arguments, and stays.
				DMibExpect(fg_FormatSource("int v[] = DList({\n1,\n2,\n});\n", "MacroArgumentComma").f_Find("2\n\t\t\t,"), >=, 0);
				// No blank line parts a NOLINTNEXTLINE from the line it suppresses, and a NOLINT in a block comment over lines
				// suppresses the line it stands on.
				fg_ExpectFormat("NoLintNextLineAfterType", "struct CX\n{\n}; // NOLINTNEXTLINE\nint x;\n", "struct CX\n{\n}; // NOLINTNEXTLINE\nint x;\n");
				fg_ExpectFormat
					(
						"NoLintInBlockComment"
						, "int f() { return 42; } /* NOLINT(readability-magic-numbers)\n more */\n"
						, "int f() { return 42; } /* NOLINT(readability-magic-numbers)\n more */\n"
					)
				;
				// A blank line inside a case's statement is no gap between its statements, and a comment over lines behind a statement
				// too long for its line is measured behind the terminator the statement ends on.
				fg_ExpectFormat
					(
						"CaseBlankInStatement"
						, "void f()\n{\n\tswitch (x)\n\t{\n\tcase 1:\n\t\tfoo(a,\n\n\t\tb); break;\n\t}\n}\n"
						, "void f()\n{\n\tswitch (x)\n\t{\n\tcase 1: foo(a, b); break;\n\t}\n}\n"
					)
				;
				{
					DMibTestPath("CommentOverLinesBehindSplit");
					auto Request = fg_Request("auto Value = fg_Function(Alpha, Beta, Gamma); /* explanation\nmore detail */\n");
					Request.m_Settings.m_nMaxColumns = 30;
					DMibExpectTrue(fg_AnalyzeCodeFormatting(Request).m_Status == ECodeFormattingStatus::mc_Complete);
				}
				// A comment behind an initializer's braces is measured on the line its statement takes.
				{
					DMibTestPath("CommentBehindInitializer");
					auto Request = fg_Request("void f_Test() { int value[] = {1, 2}; /* AAAAAAAAAA */ /* BBBBBBBBBB */\n}\n");
					Request.m_Settings.m_nMaxColumns = 60;
					DMibExpectTrue(fg_AnalyzeCodeFormatting(Request).m_Status == ECodeFormattingStatus::mc_Complete);
				}
				// The blank line behind a definition is written in the line ending the configuration asks for.
				{
					DMibTestPath("BlankLineAndLineEnding");
					auto Request = fg_Request("struct S {};\r\nint x;\r\n");
					Request.m_Settings.m_EndOfLine = ETextLineEnding::mc_LF;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpect(fg_ApplyCodeFormattingEdits(Request.m_Source, Result.m_Edits), ==, "struct S\n{\n};\n\nint x;\n");
				}
				// A qualifier stays in front of a constrained placeholder, and behind the type a declaration defines.
				fg_ExpectFormat("QualifierOnConstrainedPlaceholder", "void f(const cAny auto ...p_Args);\n", "void f(const cAny auto ...p_Args);\n");
				DMibExpect(fg_FormatSource("struct S { int n; } const x = {1};\n", "QualifierBehindDefinition").f_Find("} const x"), >=, 0);
				fg_ExpectFormat("QualifierBehindAttributedPointer", "int *[[]] const p = nullptr;\n", "int *[[]] const p = nullptr;\n");
				fg_ExpectFormat
					(
						"QualifierBehindTypeOfAttributedName"
						, "int const value __attribute__((unused)) = 1;\nint const other asm(\"x\");\n"
						, "int const value __attribute__((unused)) = 1;\nint const other asm(\"x\");\n"
					)
				;
				fg_ExpectFormat
					(
						"QualifierInTypeOnly"
						, "using A = const int;\nusing B = const char[4];\nauto f() -> const int;\nusing C = const CType;\n"
						, "using A = int const;\nusing B = char const[4];\nauto f() -> int const;\nusing C = CType const;\n"
						, false
					)
				;
				fg_ExpectFormat
					(
						"NoexceptRequirementArrow"
						, "template <typename T>\nconcept C = requires (T t)\n\t{\n\t\t{ t() } noexcept -> std::same_as<int>;\n\t}\n;\n"
						, "template <typename T>\nconcept C = requires (T t)\n\t{\n\t\t{ t() } noexcept -> std::same_as<int>;\n\t}\n;\n"
					)
				;
				fg_ExpectFormat("QualifierBehindMacroAttributedName", "CFoo const a DAttr;\nconst CFoo b DAttr;\n", "CFoo const a DAttr;\nCFoo const b DAttr;\n", false);
				// 'final' goes on with the class head a qualifier in front of the class key would be moved into, and a specifier
				// in front of the key leaves the base clause's colon its spaces.
				fg_ExpectFormat
					(
						"QualifierBeforeFinalClass"
						, "const struct S final\n{\n\tint x;\n} s = {};\nstatic struct U : V\n{\n\tint x;\n} u;\n"
						, "const struct S final\n{\n\tint x;\n} s = {};\nstatic struct U : V\n{\n\tint x;\n} u;\n"
					)
				;
				fg_ExpectFormat("QualifierBehindAttributesPointer", "int *[[]] [[]] const p = nullptr;\n", "int *[[]] [[]] const p = nullptr;\n");
				// An attributed statement takes a line of its own like any other.
				fg_ExpectFormat("AttributedDeclaration", "void f() { [[maybe_unused]] int x; }\n", "void f()\n{\n\t[[maybe_unused]] int x;\n}\n");
				// The blank starting the line behind a macro's name tells a function-like macro from another.
				DMibExpectFalse(fg_HasEquivalentCodeTokens("#define F\\\n(x) x\n", "#define F\\\n (x) x\n", ECodeLanguage::mc_Cpp));
				DMibExpectTrue(fg_HasEquivalentCodeTokens("#define F(x) \\\nx\n", "#define F(x) \\\n\tx\n", ECodeLanguage::mc_Cpp));
				// C has no template argument list for '>>' to close two of.
				DMibExpectFalse(fg_HasEquivalentCodeTokens("a >> b;", "a > > b;", ECodeLanguage::mc_C));
				// In C++ only a '>>' closing two lists is two tokens; a shift and a shift assignment are one.
				DMibExpectTrue(fg_HasEquivalentCodeTokens("TCVector<TCVector<int>> x;", "TCVector<TCVector<int> > x;", ECodeLanguage::mc_Cpp));
				DMibExpectFalse(fg_HasEquivalentCodeTokens("x >> 1;", "x > > 1;", ECodeLanguage::mc_Cpp));
				DMibExpectFalse(fg_HasEquivalentCodeTokens("x >>= 1;", "x > >= 1;", ECodeLanguage::mc_Cpp));
				// A pragma in front of an 'if' keeps the braces that keep an 'else' behind them from it.
				DMibExpect
					(
						fg_FormatSource("void f()\n{\n\tif (a) { _Pragma(\"GCC diagnostic push\") if (b) fg_Use(); } else fg_Other();\n}\n", "PragmaKeepsBraces").f_Find("if (a)\n\t{")
						, >=
						, 0
					)
				;
				fExpectUnsupported("UniversalCharacterName", fg_Request("struct C\\u03b1 {}; const C\\u03b1 *p;\n"));
				{
					DMibTestPath("IndentSizeDiffers");
					auto Properties = fg_SpacesProperties();
					Properties["tab_width"] = "8";
					auto Request = fg_Request("void f() {}\n");
					Request.m_Settings = CCodeFormattingSettings(Properties);
					DMibExpectTrue(fg_AnalyzeCodeFormatting(Request).m_Status == ECodeFormattingStatus::mc_Unsupported);
				}

				// A statement whose first line formatting is off for keeps the lines behind it where they are.
				{
					DMibTestPath("FirstLineFormatOff");
					CStr Source = "void f()\n{\n// malterlib-format off\nint x = 1\n// malterlib-format on\n;\n}\n";
					auto Result = fg_AnalyzeCodeFormatting(fg_Request(Source));
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A strict range reports nothing of a statement it leaves alone, from the gap in front of its first token on.
				{
					DMibTestPath("StrictLeavesNextStatement");
					CStr Source = "int a = 1;\n   int b=2;\n";
					auto Request = fg_Request(Source);
					Request.m_RangePolicy = ECodeRangePolicy::mc_Strict;
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 0;
					Range.m_nLength = 10;

					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpect(Result.m_Diagnostics.f_GetLen(), ==, 0u);
				}

				// A range holding a lambda's continuation lines but not the statement's first line leaves them where they are.
				{
					DMibTestPath("ContinuationWithoutFirstLine");
					CStr Source = "auto f() { return ([] {\n\treturn 1; }\n\t()); }\n";
					auto Request = fg_Request(Source);
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = Source.f_Find("\treturn 1");
					Range.m_nLength = Source.f_Find("\n\t()") - Range.m_iOffset;

					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A directive's continuation lines keep their depths together, so a range holding only some of them leaves them.
				{
					DMibTestPath("PartialDirectiveLines");
					CStr Source = "#define A \\\n x \\\n y\nint z;\n";
					auto Request = fg_Request(Source);
					Request.m_RangePolicy = ECodeRangePolicy::mc_Strict;
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 0;
					Range.m_nLength = Source.f_Find(" y");

					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A list's lines move only with its braces, which a range holding one element leaves where they are.
				for (auto Policy : {ECodeRangePolicy::mc_Expand, ECodeRangePolicy::mc_Strict})
				{
					DMibTestPath(Policy == ECodeRangePolicy::mc_Strict ? "ListElementRangeStrict" : "ListElementRangeExpand");
					CStr Source = "int a[]={\n  1,\n  2, /*Two*/\n};\n";
					auto Request = fg_Request(Source);
					Request.m_RangePolicy = Policy;
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 10;
					Range.m_nLength = 5;

					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				}

				// A range is analyzed again where the plan moves it, until it is stable, as a whole file is.
				{
					DMibTestPath("StableRange");
					CStr Source = "void f()\n{\n\tauto Value = CData{.m_FirstValue = f_GetValue(), // important value\n.m_Second = 2};\n}\nint x;\n";
					auto Properties = fg_MalterlibProperties();
					Properties["max_line_length"] = "40";
					auto Request = fg_Request(Source);
					Request.m_Settings = CCodeFormattingSettings(Properties);
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 0;
					Range.m_nLength = Source.f_Find("int x;");

					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibAssertTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpect
						(
							fg_ApplyCodeFormattingEdits(Source, Result.m_Edits)
							, ==
							, "void f()\n{\n\tauto Value = CData\n\t\t{\n\t\t\t// important value\n\t\t\t.m_FirstValue = f_GetValue()\n\t\t\t, .m_Second = 2\n\t\t}\n\t;\n}\n\nint x;\n"
						)
					;

					umint nResidual = 0;
					for (auto const &Diagnostic : Result.m_Diagnostics)
						nResidual += !Diagnostic.m_bHasAutomaticFix;

					DMibExpect(nResidual, ==, 0);
				}

				// A strict range reports no edit a statement it leaves alone needs.
				{
					DMibTestPath("StrictUnrelated");
					auto Request = fg_Request("int a = 1;\nint b=2;\n");
					Request.m_RangePolicy = ECodeRangePolicy::mc_Strict;
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 0;
					Range.m_nLength = 10;
					DMibExpectTrue(fg_AnalyzeCodeFormatting(Request).m_Diagnostics.f_IsEmpty());
				}
				fExpectUnsupported("SplicedCommentOpener", fg_Request("int f()\n{\n\treturn 1 /\\\n/ comment\n\t\t+ 2;\n}\n"));

				// A failed or unsupported result carries no edits.
				{
					DMibTestPath("FailedHasNoEdits");
					auto Request = fg_Request("int g_A[]={1, // one\n 2, // two\n 3,};\n");
					Request.m_Settings.m_nMaxColumns = 10;
					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete || Result.m_Edits.f_IsEmpty());
				}

				// An edit reaching past the source is refused rather than wrapping around.
				{
					DMibTestPath("EditOutsideSource");
					CCodeFormattingEdit Edit;
					Edit.m_iOffset = 1;
					Edit.m_nLength = TCLimitsInt<umint>::mc_Max;
					DMibExpectExceptionType(fg_ApplyCodeFormattingEdits("abc", {Edit}), NException::CException);
				}

				// '<::' is '<' and '::', no digraph.
				{
					DMibTestPath("LessScope");
					DMibExpectTrue(fg_Analyze("TCVector<::CType> g_Values;\n").m_Status == ECodeFormattingStatus::mc_Complete);
				}
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

				DMibExpect(fFormatRange("SecondLine", 11, 15, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");
				// A cursor selects the line it sits on, at the line's start too.
				DMibExpect(fFormatRange("Cursor", 14, 0, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");
				DMibExpect(fFormatRange("CursorAtLineStart", 11, 0, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");
				// A partial selection is expanded to whole lines by default.
				DMibExpect(fFormatRange("PartialExpanded", 14, 2, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");
				// Strict ranges never touch bytes the caller did not select.
				DMibExpect(fFormatRange("PartialStrict", 14, 2, ECodeRangePolicy::mc_Strict), ==, Source);
				DMibExpect(fFormatRange("WholeLineStrict", 11, 15, ECodeRangePolicy::mc_Strict), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");

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

				// A directive's continuation lines keep their depths relative to all of its lines, not to the selected ones.
				DMibTestCategory("PartialDirective")
				{
					CStr Macro = "#define F do { \\\n\t\tf(); \\\n\t} while (0)\nint x;\n";
					auto Whole = fg_AnalyzeCodeFormatting(fg_Request(Macro));
					DMibExpectTrue(Whole.m_Edits.f_IsEmpty());

					auto Request = fg_Request(Macro);
					auto &Range = Request.m_Ranges.f_Insert();
					Range.m_iOffset = 0;
					Range.m_nLength = Macro.f_Find("\t}");

					auto Result = fg_AnalyzeCodeFormatting(Request);
					DMibExpectTrue(Result.m_Edits.f_IsEmpty());
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
					auto Result = fg_Analyze("void f()\n{\n    int a;\n}\n");
					DMibAssert(Result.m_Diagnostics.f_GetLen(), ==, 1u);
					DMibExpect(Result.m_Diagnostics[0].m_Rule, ==, "indentation");
					DMibExpect(Result.m_Diagnostics[0].m_iLine, ==, 3u);
					DMibExpect(Result.m_Diagnostics[0].m_iColumn, ==, 1u);
					DMibExpectTrue(Result.m_Diagnostics[0].m_bHasAutomaticFix);
					DMibExpectFalse(Result.f_HasUnfixableDiagnostics());
				};

				DMibTestCategory("Brackets")
				{
					auto fStructureLine = [&](CStr const &_Case, CStr const &_Source)
						{
							DMibTestPath(_Case);

							auto Result = fg_Analyze(_Source);
							DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);

							umint iLine = 0;
							for (auto const &Diagnostic : Result.m_Diagnostics)
							{
								if (Diagnostic.m_Rule == "structure")
									iLine = Diagnostic.m_iLine;
							}

							return iLine;
						}
					;

					// No line can be placed while the brackets do not nest, whichever kind is
					// left open, so the file keeps its lines and is told why.
					DMibExpect(fStructureLine("OpenBrace", "void f()\n{{\n\tg(1);\n}\n"), ==, 2u);
					DMibExpect(fStructureLine("OpenParen", "void f(()\n{\n\tg(1);\n}\n"), ==, 1u);
					// A closer further down the file would otherwise balance the opener in the
					// place of the one it is missing, which is what left this silent.
					DMibExpect(fStructureLine("Later", "namespace N\n{\n\tvoid f(()\n\t{\n\t\tg(1);\n\t}\n\n\tvoid h()\n\t{\n\t\tg(2);\n\t}\n}\n"), ==, 3u);
					DMibExpect(fStructureLine("StrayCloser", "void f()\n{\n\tg(1));\n}\n"), ==, 3u);
					// Alternative heads leave the rest of the file laid out, its first code behind directives included.
					fg_ExpectFormat
						(
							"FirstCodeBesideAlternativeHeads"
							, "#ifdef A\n    extern int x;\n#endif\nvoid f()\n{\n#ifdef B\n\tif (a)\n\t{\n#else\n\tif (b)\n\t{\n#endif\n\t\tg();\n\t}\n}\n"
							, "#ifdef A\nextern int x;\n#endif\nvoid f()\n{\n#ifdef B\n\tif (a)\n\t{\n#else\n\tif (b)\n\t{\n#endif\n\t\tg();\n\t}\n}\n"
						)
					;
					// A statement that ends at the brace of the block it stands in is how a
					// list without terminators is written, and no violation of anything.
					DMibExpect(fStructureLine("EnumBody", "enum E\n{\n\tmc_A = 0\n\t, mc_B = 1\n};\n"), ==, 0u);

					// Brackets nested deeper than a compiler accepts by default are named where the limit is passed, and
					// a file at the limit is laid out as any other.
					auto fNested = [](umint _nDepth)
						{
							CStr Source = "void f()\n";
							for (umint i = 0; i < _nDepth; ++i)
								Source += "{\n";

							for (umint i = 0; i < _nDepth; ++i)
								Source += "}\n";

							return Source;
						}
					;

					DMibExpect(fStructureLine("TooDeep", fNested(CCodeStructure::mc_MaxBracketDepth + 1)), ==, CCodeStructure::mc_MaxBracketDepth + 2);
					DMibExpect(fStructureLine("DeepestAccepted", fNested(CCodeStructure::mc_MaxBracketDepth)), ==, 0u);
				};

				// A chain of empty calls has nothing to open, and a line too long with it is reported in one pass over
				// the chain rather than one per call.
				DMibTestCategory("EmptyCallChain")
				{
					CStr Source = "void f()\n{\n\ts";
					for (umint i = 0; i < 5000; ++i)
						Source += "()";

					Source += ";\n}\n";

					auto Result = fg_Analyze(Source);
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
				};

				// Each of these once changed the tokens rather than their layout.
				DMibTestCategory("TokenStream")
				{
					// A lone '@' written together with what follows it is the word '@blah', and a number with a '.'
					// behind it is a floating point literal.
					CStr LoneAt = "void f()\n{\n\tid u = @\"123\" @ blah;\n}\n";
					fg_ExpectFormat("LoneAt", LoneAt, LoneAt);
					CStr NumberMember = "void f()\n{\n\t0 .~T();\n}\n";
					fg_ExpectFormat("NumberMember", NumberMember, NumberMember);
					// The lines of a block comment are its text, also where a statement follows it on its last line.
					fg_ExpectFormat("CommentBeforeCode", "void f()\n{\n\t\t/*\n\t\t*/ x = 0;\n\t\ty = 0;\n}\n", "void f()\n{\n\t\t/*\n\t\t*/ x = 0;\n\ty = 0;\n}\n");
					// A blank line taken out behind a raw string starts behind the string's last line, not its first.
					fg_ExpectFormat
						(
							"RawStringBlankLine"
							, "void f()\n{\n\tfor (auto &R : Rs)\n\t{\n\t\tR\"cpp(\n\t\t// )cpp\",\n\n\t};\n}\n"
							, "void f()\n{\n\tfor (auto &R : Rs)\n\t{\n\t\tR\"cpp(\n\t\t// )cpp\"\n\t};\n}\n"
							, false
						)
					;
					// A newline behind a trailing backslash would make it a line splice, so the file is told why it has none.
					auto Result = fg_Analyze("int a;\n// b \\");
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpectTrue(Result.m_Edits.f_IsEmpty());

					umint nReports = 0;
					for (auto const &Diagnostic : Result.m_Diagnostics)
						nReports += Diagnostic.m_Rule == "final-newline" && !Diagnostic.m_bHasAutomaticFix;

					DMibExpect(nReports, ==, 1u);
				};

				// A string continued on the next line at file scope, where no statement encloses it, as a list of macro
				// invocations without terminators writes it.
				DMibTestCategory("FileScopeContinuation")
				{
					auto Result = fg_Analyze("ARG(Value, \"how to \"\n\t\"do it\")\n");
					DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);
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

					// In the build system's files a string ending a line counts for no width; one with more behind it, and the
					// line's code and comments, count for theirs.
					{
						CStr Value;
						for (umint i = 0; i < 200; ++i)
							Value += "v";

						auto fLengthReports = [&](CStr const &_Source)
							{
								umint nReports = 0;
								for (auto const &Diagnostic : fg_AnalyzeCodeFormatting(fg_BuildSystemRequest(_Source)).m_Diagnostics)
									nReports += Diagnostic.m_Rule == "line-length";

								return nReports;
							}
						;

						DMibExpect(fLengthReports("Property\n{\n\tA \"" + Value + "\"\n\tB `" + Value + " @(A)`\n}\n"), ==, 0u);
						DMibExpect(fLengthReports("Property\n{\n\tA \"a\" // " + Value + "\n}\n"), ==, 1u);
						DMibExpect(fLengthReports("Property\n{\n\tA \"" + Value + "\"->F()\n\tB `" + Value + "`\\\n\t\t`b`\n}\n"), ==, 1u);

						// Inside a block comment the directives stand on lines of their own, without the '//'.
						DMibExpect(fLengthReports("/*\tmalterlib-format off\n\tA F(\"a\", `" + Value + "`)\n\tmalterlib-format on\n*/\n"), ==, 0u);
						DMibExpect(fLengthReports("/*\n\tA F(\"a\", `" + Value + "`)\n*/\n"), ==, 1u);
					}

					{
						DMibTestPath("BlockCommentDisabled");

						CStr Comment = "/* malterlib-format off */\n// " + Long + "/* malterlib-format on */\n";
						auto Disabled = fg_Analyze(Comment);
						DMibExpectTrue(Disabled.m_Status == ECodeFormattingStatus::mc_Complete);
						DMibExpectTrue(Disabled.m_Diagnostics.f_IsEmpty());
					}

					// One the layout breaks up is no violation of the result's making, so it
					// is not reported as one left to resolve.
					DMibTestPath("Split");

					CStr Operand = "(a + b)";
					for (umint i = 0; i < 4; ++i)
						Operand = CStr("(@ + @)").f_Replace("@", Operand);

					auto Broken = fg_Analyze(CStr("void f()\n{\n\tint x = @;\n}\n").f_Replace("@", Operand));
					DMibExpectTrue(Broken.m_Status == ECodeFormattingStatus::mc_Complete);
					DMibExpectFalse(Broken.m_Edits.f_IsEmpty());
					DMibExpectFalse(Broken.f_HasUnfixableDiagnostics());

					// A region the formatter is turned off for keeps its long lines as written.
					{
						DMibTestPath("Protected");

						auto Protected = fg_Analyze("// malterlib-format off\n" + Long + "// malterlib-format on\n");
						DMibExpectTrue(Protected.m_Diagnostics.f_IsEmpty());
					}
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
					DMibExpectTrue(fg_HasEquivalentCodeTokens("#include \"a.h\" \n", "#include \"a.h\"\n"));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("int a;\n", "int b;\n"));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("int a;\n", "int a\n"));
					DMibExpectFalse(fg_HasEquivalentCodeTokens("auto s = \"a b\";\n", "auto s = \"a  b\";\n"));
				};
			};
		}
	};
}

DMibTestRegister(CCodeFormatting_Tests, Malterlib::Develop);
