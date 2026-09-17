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
					// The depth is the line-break rule's; this rule only spells it with tabs.
					fg_ExpectFormat("SpacesToTabs", "void f()\n{\n    int a;\n}\n", "void f()\n{\n\tint a;\n}\n");
					fg_ExpectFormat("MixedIndent", "void f()\n{\n \tint a;\n}\n", "void f()\n{\n\tint a;\n}\n");
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
					// A lone '=' assigns and initializes. A capture default is settled by the
					// markers around it, and behind a DSL marker the same token is the tail of
					// a spelling that hugs the key it follows.
					fg_ExpectFormat("Assign", "void f()\n{\n\tint a=1;\n\tint b =2;\n}\n", "void f()\n{\n\tint a = 1;\n\tint b = 2;\n}\n");
					fg_ExpectFormat("CaptureDefault", "void f()\n{\n\tauto g = [=]{};\n}\n", "void f()\n{\n\tauto g = [=]{};\n}\n");
					fg_ExpectFormat("FormattingDsl", "auto g_Option = \"Names\"_o= _o[\"--file\"];\n", "auto g_Option = \"Names\"_o= _o[\"--file\"];\n");
					// A statement broken at its '=' is brought back together, and what then
					// does not fit gives at its scopes, not in front of the name.
					fg_ExpectFormat
						(
							"AssignJoins"
							, "void f()\n{\n\tauto Value\n\t\t= fg_G(5)\n\t;\n}\n"
							, "void f()\n{\n\tauto Value = fg_G(5);\n}\n"
						)
					;
					// An operator function is named by the keyword and what follows it, and that
					// name stands apart from both: from the keyword, and from the parameter
					// list, whose own parentheses the call operator's name is spelled with.
					fg_ExpectFormat
						(
							"OperatorName"
							, "struct C\n{\n\tbool operator==(C const &_Other) const;\n\tint operator() (int _A);\n\tint operator [](umint _i);\n"
								"\toperator NStr::CStr() const;\n\tusing CBase::operator=;\n};\n"
							, "struct C\n{\n\tbool operator == (C const &_Other) const;\n\tint operator () (int _A);\n\tint operator [] (umint _i);\n"
								"\toperator NStr::CStr () const;\n\tusing CBase::operator =;\n};\n"
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
					// like a trailing return type's. Without a header in front of it the name
					// is spelled like a call, and the arrow keeps what it has.
					fg_ExpectFormat
						(
							"DeductionGuide"
							, "template <typename t_C>\nC(T<t_C>)->C<t_C>;\n\nC(CVoidTag)->C<void>;\n"
							, "template <typename t_C>\nC(T<t_C>) -> C<t_C>;\n\nC(CVoidTag)->C<void>;\n"
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
								"\nTCTuple<tp_CParams && ...> g_A;\n"
							, "template <typename ...tp_CParams>\nvoid fg_A(tp_CParams && ...p_Params, tp_CParams && ...p_Other, tp_CParams && ...);\n"
								"\nTCTuple<tp_CParams &&...> g_A;\n"
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
					// A base clause's 'public' is no label.
					fg_ExpectFormat("BaseClause", "struct C : public CBase\n{\n};\n", "struct C : public CBase\n{\n};\n");
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
					// A unary '!' hugs its operand, so it rejoins the group it stood in front of.
					fg_ExpectFormat
						(
							"Negation"
							, "void f()\n{\n\tif\n\t(\n\t\t!\n\t\t(\n\t\t\ta\n\t\t)\n\t\t|| !!\n\t\t(\n\t\t\tb\n\t\t)\n\t)\n\t\tg();\n}\n"
							, "void f()\n{\n\tif (!(a) || !!(b))\n\t\tg();\n}\n"
						)
					;
					// A declarator is not a binary operator: it rejoins the name it declares
					// where only a type can stand in front of it.
					fg_ExpectFormat("ConstDeclarator", "void fg_F(CStr const &\n\t_A, ch8 const **\n\t_ppB);\n", "void fg_F(CStr const &_A, ch8 const **_ppB);\n");
					fg_ExpectFormat("TemplateDeclarator", "void fg_F(TCVector<int>\n\t*&_pA);\n", "void fg_F(TCVector<int> *&_pA);\n");
					// In a parameter list a declarator stands behind a plain name too.
					fg_ExpectFormat("ParameterDeclarator", "void fg_F(CStr\n\t&_A, CFoo *\n\t_pB);\n", "void fg_F(CStr &_A, CFoo *_pB);\n");
					fg_ExpectFormat
						(
							"TemplateMember"
							, "template <typename t_C>\nTCFoo<t_C>::TCFoo(CStr const &\n\t_A)\n\t: m_A(_A)\n{\n}\n"
							, "template <typename t_C>\nTCFoo<t_C>::TCFoo(CStr const &_A)\n\t: m_A(_A)\n{\n}\n"
						)
					;
					fg_ExpectFormat("QualifiedConstructor", "C::C(CStr &\n\t_A)\n\t: m_A(_A)\n{\n}\n", "C::C(CStr &_A)\n\t: m_A(_A)\n{\n}\n");
					fg_ExpectFormat("DeletedConstructor", "struct C\n{\n\tC(C &\n\t\t_A) = delete;\n};\n", "struct C\n{\n\tC(C &_A) = delete;\n};\n");
					fg_ExpectFormat
						(
							"Catch"
							, "void f()\n{\n\ttry\n\t{\n\t}\n\tcatch (CException &\n\t\t_E)\n\t{\n\t}\n}\n"
							, "void f()\n{\n\ttry\n\t{\n\t}\n\tcatch (CException &_E)\n\t{\n\t}\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"Lambda"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[&](CStr &\n\t\t\t_A)\n\t\t\t{\n\t\t\t\th(_A);\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[&](CStr &_A)\n\t\t\t{\n\t\t\t\th(_A);\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// A template header declares its parameters; an unnamed one ends in its
					// declarator.
					fg_ExpectFormat
						(
							"TemplateParameter"
							, "template\n<\n\ttypename t_C\n\t, TCEnableIf<cFoo<t_C>>\n\t*\n>\nvoid fg_F();\n"
							, "template <typename t_C, TCEnableIf<cFoo<t_C>> *>\nvoid fg_F();\n"
						)
					;
					// A concept behind a template argument list is an operand of '&&', which
					// is written apart from it, and 'requires' from its parenthesis.
					fg_ExpectFormat
						(
							"RequiresClause"
							, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires\n\t(\n\t\tcFoo<t_C>\n\t\t&& cBar<t_C>\n\t)\n;\n"
							, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires (cFoo<t_C> && cBar<t_C>)\n;\n"
						)
					;
					// A bare name in front of the list is a call as well as a constructor, a
					// call's argument is not settled, and neither is a '&&' behind a template
					// argument list in front of a name.
					fg_ExpectFormat("BareName", "void f()\n{\n\tC(CStr &\n\t\t_A);\n}\n", "void f()\n{\n\tC(CStr &\n\t\t_A);\n}\n");
					// In a class body there are no calls, so a bare name declares.
					fg_ExpectFormat("MemberDeclaration", "struct C\n{\n\tC(CStr &\n\t\t_A);\n};\n", "struct C\n{\n\tC(CStr &_A);\n};\n");
					// Behind the '=' of a default argument stands an expression, which settles
					// the operator and lets the line be joined.
					fg_ExpectFormat("DefaultArgument", "void fg_F(int _A = a &\n\tb);\n", "void fg_F(int _A = a & b);\n");
					fg_ExpectFormat("CallArgument", "void f()\n{\n\tg(a &\n\t\tb);\n}\n", "void f()\n{\n\tg(a &\n\t\tb);\n}\n");
					fg_ExpectFormat("Ternary", "void f()\n{\n\tx = y ? g(a &\n\t\tb) : c;\n}\n", "void f()\n{\n\tx = y ? g(a &\n\t\tb) : c;\n}\n");
					fg_ExpectFormat
						(
							"ConceptOrDeclarator"
							, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires (cFoo<t_C> &&\n\t\tcBar<t_C>)\n;\n"
							, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires (cFoo<t_C> &&\n\t\tcBar<t_C>)\n;\n"
						)
					;
				};

				DMibTestCategory("Blocks")
				{
					// A body opens on a line of its own, and its closing brace takes one too.
					fg_ExpectFormat("EmptyBody", "void f(){}\n", "void f()\n{\n}\n");
					fg_ExpectFormat("BodyAfterInitializer", "struct C\n{\n\tC(int _A)\n\t\t: m_A(_A){}\n};\n", "struct C\n{\n\tC(int _A)\n\t\t: m_A(_A)\n\t{\n\t}\n};\n");
					fg_ExpectFormat("BraceOnHead", "void f() {\n\tg();\n}\n", "void f()\n{\n\tg();\n}\n");
					fg_ExpectFormat("OneLine", "void f() { g(); }\n", "void f()\n{\n\tg();\n}\n");
					fg_ExpectFormat("Definition", "struct C { int a; };\n", "struct C\n{\n\tint a;\n};\n");
					// Each statement takes a line of its own at the block's level.
					fg_ExpectFormat("Statements", "void f()\n{\n\tg(); h();\n}\n", "void f()\n{\n\tg();\n\th();\n}\n");
					// What a clause guards stands one level in; a guarded block at the clause's level.
					fg_ExpectFormat("Guarded", "void f()\n{\n\tif (a) g();\n\telse h(); k();\n}\n", "void f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\th();\n\tk();\n}\n");
					fg_ExpectFormat
						(
							"ClauseBlocks"
							, "void f()\n{\n\tif (a) {\n\t\tg();\n\t\tk();\n\t} else {\n\t\th();\n\t\tk();\n\t}\n}\n"
							, "void f()\n{\n\tif (a)\n\t{\n\t\tg();\n\t\tk();\n\t}\n\telse\n\t{\n\t\th();\n\t\tk();\n\t}\n}\n"
						)
					;
					fg_ExpectFormat("DoWhile", "void f()\n{\n\tdo {\n\t\tg();\n\t} while (a);\n}\n", "void f()\n{\n\tdo\n\t{\n\t\tg();\n\t}\n\twhile (a);\n}\n");
					// A lambda's body sits one level in and takes its lines along, and its
					// terminator stands at the statement's indentation.
					fg_ExpectFormat("Lambda", "void f()\n{\n\tauto g = [&] {\n\t\th();\n\t};\n}\n", "void f()\n{\n\tauto g = [&]\n\t\t{\n\t\t\th();\n\t\t}\n\t;\n}\n");
					// 'else if' and a case written on its label's line are kept as written.
					CStr ElseIf = "void f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t\th();\n}\n";
					fg_ExpectFormat("ElseIf", ElseIf, ElseIf);
					CStr Cases = "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: a = 1; break;\n\tcase 2: return;\n\tdefault: break;\n\t}\n}\n";
					fg_ExpectFormat("CompactCases", Cases, Cases);
					// A name behind a closing brace declares a variable of the type just defined.
					CStr Instance = "struct\n{\n\tint a;\n} g_Instance;\n";
					fg_ExpectFormat("Instance", Instance, Instance);
					CStr Attribute = "void f()\n{\n\tif (a) [[unlikely]]\n\t\tg();\n}\n";
					fg_ExpectFormat("Attribute", Attribute, Attribute);
					// A comment on a line of its own is one of the body's lines and follows it:
					// a body that moves takes the comment to the depth the brace moved to.
					fg_ExpectFormat
						(
							"CommentedLambda"
							, "void f()\n{\n\tauto g = [&] {\n\t\t// why\n\t\th();\n\t};\n}\n"
							, "void f()\n{\n\tauto g = [&]\n\t\t{\n\t\t\t// why\n\t\t\th();\n\t\t}\n\t;\n}\n"
						)
					;
					fg_ExpectFormat("CommentedBody", "void f() {\n\t// why\n\tg();\n}\n", "void f()\n{\n\t// why\n\tg();\n}\n");
					// A call holding a lambda body is written split: the body opens under its
					// introducer at the element's indentation, its lines follow it, what
					// comes after the call resumes under the closing parenthesis, and the
					// terminator takes a line of its own.
					CStr Call = "void f()\n{\n\tfg_Dispatch (\n\t\t[Promises = fg_Move(m_Promises), Result]() mutable {\n\t\t\tfor (auto &Promise : Promises)\n"
						"\t\t\t\tPromise.f_SetResult(Result);\n\t\t})\n\t.f_DiscardResult();\n}\n"
					;
					CStr CallResult = "void f()\n{\n\tfg_Dispatch\n\t\t(\n\t\t\t[Promises = fg_Move(m_Promises), Result]() mutable\n\t\t\t{\n\t\t\t\tfor (auto &Promise : Promises)\n"
						"\t\t\t\t\tPromise.f_SetResult(Result);\n\t\t\t}\n\t\t)\n\t\t.f_DiscardResult()\n\t;\n}\n"
					;
					fg_ExpectFormat("LambdaArgument", Call, CallResult);
					fg_ExpectFormat
						(
							"LambdaArgumentOneLine"
							, "void f()\n{\n\tg(a, [&] { h(); });\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t, [&]\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// The scope holding the body is the one opened; what stands in front of it
					// stays on the line, a call's arguments and a member access included.
					fg_ExpectFormat
						(
							"BlockScopeHead"
							, "void f()\n{\n\tfg_Move(x).f_OnResultSet([&] { g(); });\n}\n"
							, "void f()\n{\n\tfg_Move(x).f_OnResultSet\n\t\t(\n\t\t\t[&]\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// An arrow behind a call's arguments is a member access, not a trailing
					// return type.
					fg_ExpectFormat
						(
							"MemberArrow"
							, "void f()\n{\n\tfg_GetSys()->f_GetLogger().f_SetDispatcher([&](int _a) { g(); });\n}\n"
							, "void f()\n{\n\tfg_GetSys()->f_GetLogger().f_SetDispatcher\n\t\t(\n\t\t\t[&](int _a)\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"TrailingReturnAfterHeader"
							, "template <typename t_C>\nauto fg_F(int _A, int _B)\n\t-> TCFuture<int>\n;\n"
							, "template <typename t_C>\nauto fg_F(int _A, int _B) -> TCFuture<int>;\n"
						)
					;
					// A body already on a line of its own moves with its element.
					fg_ExpectFormat
						(
							"BodyFollowsElement"
							, "void f()\n{\n\tif\n\t\t(\n\t\t\tg\n\t\t\t(\n\t\t\t\t[&]\n\t\t\t\t{\n\t\t\t\t\th();\n\t\t\t\t}\n\t\t\t)\n\t\t)\n\t{\n\t}\n}\n"
							, "void f()\n{\n\tif\n\t(\n\t\tg\n\t\t(\n\t\t\t[&]\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t)\n\t{\n\t}\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"NestedLambdaArgument"
							, "void f()\n{\n\tco_await g(h([&]() -> TCFuture<void> {\n\t\tco_return {};\n\t}));\n}\n"
							, "void f()\n{\n\tco_await g\n\t\t(\n\t\t\th\n\t\t\t(\n\t\t\t\t[&]() -> TCFuture<void>\n\t\t\t\t{\n\t\t\t\t\tco_return {};\n\t\t\t\t}\n\t\t\t)\n\t\t)\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("BodyOwner")
				{
					// A body that already has a line of its own still opens where its head
					// puts it: a lambda's one level in, whether the lambda is assigned,
					// returned or handed to an operator.
					fg_ExpectFormat
						(
							"OwnLine"
							, "void f()\n{\n\tauto g = [&]\n\t{\n\t\th();\n\t}\n\t;\n\treturn [&]\n\t{\n\t\ti();\n\t}\n\t;\n}\n"
							, "void f()\n{\n\tauto g = [&]\n\t\t{\n\t\t\th();\n\t\t}\n\t;\n\treturn [&]\n\t\t{\n\t\t\ti();\n\t\t}\n\t;\n}\n"
						)
					;
					// A lambda's terminator likewise stands at the statement's indentation
					// wherever the source left it, on its own line or behind the brace.
					fg_ExpectFormat
						(
							"Terminator"
							, "void f()\n{\n\tauto g = [&]\n\t\t{\n\t\t\th();\n\t\t}\n;\n\treturn [&]\n\t\t{\n\t\t\ti();\n\t\t}\n\t\t\t\t;\n}\n"
							, "void f()\n{\n\tauto g = [&]\n\t\t{\n\t\t\th();\n\t\t}\n\t;\n\treturn [&]\n\t\t{\n\t\t\ti();\n\t\t}\n\t;\n}\n"
						)
					;
					// Whose body it is the capture list in front of the brace says, not the
					// first parenthesis of the statement, which here opens a call.
					fg_ExpectFormat
						(
							"Operand"
							, "void f()\n{\n\tm_Promise.f_Future() > [P](CResult &&_R)\n\t{\n\t\tP.f_Set(fg_Move(_R));\n\t}\n\t;\n}\n"
							, "void f()\n{\n\tm_Promise.f_Future() > [P](CResult &&_R)\n\t\t{\n\t\t\tP.f_Set(fg_Move(_R));\n\t\t}\n\t;\n}\n"
						)
					;
					// A subscript operator's name ends in brackets of its own, which name a
					// declaration and no lambda: its body opens at the statement's level.
					CStr Subscript = "auto C::operator [] (int &&_Key) -> int\n{\n\treturn 0;\n}\n";
					fg_ExpectFormat("SubscriptOperator", Subscript, Subscript);
				};

				DMibTestCategory("Depth")
				{
					// Every statement of a block stands at the block's own level, whatever
					// depth the source gave the line it starts, and so does the closing brace.
					fg_ExpectFormat
						(
							"Statements"
							, "void f()\n{\n\t\tg();\n\th();\n\t\t\ti();\n\t}\n"
							, "void f()\n{\n\tg();\n\th();\n\ti();\n}\n"
						)
					;
					// A label stands one level out from the statements written under it.
					fg_ExpectFormat
						(
							"Labels"
							, "void f()\n{\n\tswitch (a)\n\t{\n\t\tcase 1:\n\t\t\t\tg();\n\t\t\tbreak;\n\t\tdefault:\n\t\t\tbreak;\n\t}\n}\n"
							, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\tbreak;\n\tdefault:\n\t\tbreak;\n\t}\n}\n"
						)
					;
					// What a clause guards stands one level in from the clause, each clause
					// of a chain counting for one.
					fg_ExpectFormat
						(
							"Guarded"
							, "void f()\n{\n\tif (a)\n\t\t\tg();\n\telse\n\t{\n\t\t\th();\n\t}\n\n\tfor (auto &E : R)\n\t\tif (b)\n\t\t\t\ti();\n}\n"
							, "void f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\th();\n\n\tfor (auto &E : R)\n\t\tif (b)\n\t\t\ti();\n}\n"
							, false
						)
					;
					// A lambda body holds statements even where every one of them is
					// compound, which leaves it no terminator of its own to be told by.
					fg_ExpectFormat
						(
							"LambdaBody"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]() mutable\n\t\t\t{\n\t\t\t\t\tfor (auto &E : R)\n\t\t\t\t\t{\n"
								"\t\t\t\t\t\tE.f_Go();\n\t\t\t\t\t\tE.f_Done();\n\t\t\t\t\t}\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]() mutable\n\t\t\t{\n\t\t\t\tfor (auto &E : R)\n\t\t\t\t{\n"
								"\t\t\t\t\tE.f_Go();\n\t\t\t\t\tE.f_Done();\n\t\t\t\t}\n\t\t\t}\n\t\t)\n\t;\n}\n"
						)
					;
					// A statement whose own lines are fixed keeps the one it starts too: a
					// braced initializer written across lines, and a block comment inside.
					CStr Initializer = "void f()\n{\n\tCJsonSorted Json =\n\t\t{\n\t\t\t\"Name\"_j= \"John\"\n\t\t\t, \"Age\"_j= 30\n\t\t}\n\t;\n}\n";
					fg_ExpectFormat("Initializer", Initializer, Initializer);
					// The depth of a line inside a conditional is the file's own business:
					// the sources indent one by conditional nesting and by nothing at all.
					CStr Conditional = "void f()\n{\n#if DDebug\n\t\tg();\n#endif\n\th();\n}\n";
					fg_ExpectFormat("Conditional", Conditional, Conditional);
				};

				DMibTestCategory("Braces")
				{
					// A single guarded statement stands without braces, whatever lines the
					// source wrote them on.
					fg_ExpectFormat("Dropped", "void f()\n{\n\tif (a)\n\t{\n\t\tg();\n\t}\n\telse\n\t{\n\t\th();\n\t}\n}\n", "void f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\th();\n}\n", false);
					fg_ExpectFormat("OneLine", "void f()\n{\n\tif (a) { g(); } else { h(); }\n}\n", "void f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\th();\n}\n", false);
					// A lambda's body belongs to the group it is written in, which is how the
					// walk that decides the conversions reaches the statements inside it.
					fg_ExpectFormat
						(
							"InLambdaBody"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]() mutable\n\t\t\t{\n\t\t\t\tfor (auto &E : R)\n\t\t\t\t{\n\t\t\t\t\tE.f_Go();\n\t\t\t\t}\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]() mutable\n\t\t\t{\n\t\t\t\tfor (auto &E : R)\n\t\t\t\t\tE.f_Go();\n\t\t\t}\n\t\t)\n\t;\n}\n"
							, false
						)
					;
					fg_ExpectFormat
						(
							"Loop"
							, "void f()\n{\n\twhile (a)\n\t{\n\t\tg();\n\t}\n\tfor (;;)\n\t{\n\t\th();\n\t}\n}\n"
							, "void f()\n{\n\twhile (a)\n\t\tg();\n\tfor (;;)\n\t\th();\n}\n"
							, false
						)
					;
					// Two statements, a comment, a nested 'if' in front of an 'else', an empty
					// block, a macro without a terminator, a 'do' body, and a 'switch' body
					// all keep their braces.
					auto fKept = [&](CStr const &_Case, CStr const &_Body)
						{
							CStr Source = "void f()\n{\n" + _Body + "}\n";
							fg_ExpectFormat(_Case, Source, Source);
						}
					;
					CStr Wide;
					for (umint i = 0; i < 90; ++i)
						Wide += "W";

					fKept("Two", "\tif (a)\n\t{\n\t\tg();\n\t\th();\n\t}\n");
					fKept("Comment", "\tif (a)\n\t{\n\t\t// why\n\t\tg();\n\t}\n");
					fKept("Dangling", "\tif (a)\n\t{\n\t\tif (b)\n\t\t\tg();\n\t}\n\telse\n\t\th();\n");
					fKept("Empty", "\tif (a)\n\t{\n\t}\n");
					fKept("Macro", "\tif (a)\n\t{\n\t\tDMibFoo(b)\n\t}\n");
					fKept("Do", "\tdo\n\t{\n\t\tg();\n\t}\n\twhile (a);\n");
					fKept("Switch", "\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t}\n");
					fKept("TrailingComment", "\tif (a)\n\t{\n\t\tg();\n\t} // why\n");
					// A comment on the statement's own line follows it out of the block, also in
					// front of an 'else' on the brace's line; one behind the terminator of a
					// statement that stays split keeps the braces.
					fg_ExpectFormat("StatementComment", "void f()\n{\n\tif (a)\n\t{\n\t\tg(); // why\n\t}\n}\n", "void f()\n{\n\tif (a)\n\t\tg(); // why\n}\n", false);
					fg_ExpectFormat
						(
							"StatementCommentElse"
							, "void f()\n{\n\tif (a)\n\t{\n\t\tg(); // why\n\t} else\n\t\th();\n}\n"
							, "void f()\n{\n\tif (a)\n\t\tg(); // why\n\telse\n\t\th();\n}\n"
							, false
						)
					;
					fKept("SplitStatementComment", "\tif (a)\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t" + Wide + Wide + "\n\t\t\t)\n\t\t; // why\n\t}\n");
					// A statement laid out across lines takes braces, and so does one behind a
					// split clause; an attribute stays on the clause's line in front of them, and
					// a trailing comment stays on the statement's last line.
					fg_ExpectFormat
						(
							"Added"
							, "void f()\n{\n\tif (a)\n\t\tg(" + Wide + Wide + ");\n}\n"
							, "void f()\n{\n\tif (a)\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t" + Wide + Wide + "\n\t\t\t)\n\t\t;\n\t}\n}\n"
							, false
						)
					;
					fg_ExpectFormat
						(
							"AddedAttribute"
							, "void f()\n{\n\tif (a) [[unlikely]]\n\t\tg(" + Wide + Wide + "); // why\n}\n"
							, "void f()\n{\n\tif (a) [[unlikely]]\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t" + Wide + Wide + "\n\t\t\t)\n\t\t; // why\n\t}\n}\n"
							, false
						)
					;
					// A clause split across lines keeps its braces, as the standard requires.
					fKept("SplitClause", "\tif\n\t(\n\t\t" + Wide + "\n\t\t&& " + Wide + "\n\t)\n\t{\n\t\tg();\n\t}\n");
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
					// A line comment ends its line, and the lines around it are laid out as
					// usual: the construct is split, and each line takes its indentation.
					fg_ExpectFormat
						(
							"CommentIndent"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t\t5 // why\n\t\t, 6\n\t\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"CommentMembers"
							, "void f()\n{\n\tco_return co_await m_Promises\n.f_Insert()  // why\n\t\t\t\t\t\t.f_Future()\n\t\t;\n}\n"
							, "void f()\n{\n\tco_return co_await m_Promises\n\t\t.f_Insert()  // why\n\t\t.f_Future()\n\t;\n}\n"
						)
					;
					fg_ExpectFormat("CommentParameter", "void fg_F\n\t(\n\t\t\tint _A // why\n\t\t, int _B\n\t)\n;\n", "void fg_F\n\t(\n\t\tint _A // why\n\t\t, int _B\n\t)\n;\n");
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
					// A line the layout has already broken is broken again while it is still too
					// long: the scopes inside it open in turn until every line of the result
					// fits, whether the source wrote the construct on one line or split.
					CStr Half = "(a + b)";
					for (umint i = 0; i < 3; ++i)
						Half = CStr("(@ + @)").f_Replace("@", Half);

					CStr Element = CStr("(@ + @)").f_Replace("@", Half);
					fg_ExpectFormat
						(
							"Deep"
							, CStr("void f()\n{\n\tint x =\n\t\t(\n\t\t\t#\n\t\t\t+ #\n\t\t)\n\t;\n}\n").f_Replace("#", Element)
							, CStr
								(
									"void f()\n{\n\tint x =\n\t\t(\n\t\t\t(\n\t\t\t\t@\n\t\t\t\t+ @\n\t\t\t)\n"
									"\t\t\t+\n\t\t\t(\n\t\t\t\t@\n\t\t\t\t+ @\n\t\t\t)\n\t\t)\n\t;\n}\n"
								)
								.f_Replace("@", Half)
						)
					;
					// A clause's parenthesis sits at the statement's own indentation, and a
					// split clause puts braces around the statement it guards.
					fg_ExpectFormat
						(
							"Clause"
							, CStr("void f()\n{\n\tif (@ && @ && @)\n\t\th();\n}\n").f_Replace("@", Wide)
							, CStr("void f()\n{\n\tif\n\t(\n\t\t@\n\t\t&& @\n\t\t&& @\n\t)\n\t{\n\t\th();\n\t}\n}\n").f_Replace("@", Wide)
							, false
						)
					;
					fg_ExpectFormat
						(
							"For"
							, CStr("void f()\n{\n\tfor (umint @ = 0; @ < 5; ++@)\n\t\th();\n}\n").f_Replace("@", Wide)
							, CStr("void f()\n{\n\tfor\n\t(\n\t\tumint @ = 0\n\t\t; @ < 5\n\t\t; ++@\n\t)\n\t{\n\t\th();\n\t}\n}\n").f_Replace("@", Wide)
							, false
						)
					;
					// A definition splits its parameter list and keeps its body at statement level.
					fSplit("Definition", "void fg_F(int @, int @)\n{\n}\n", "void fg_F\n\t(\n\t\tint @\n\t\t, int @\n\t)\n{\n}\n");
					// A parameter that still does not fit is not split at its declarator, which
					// is not an operator; it is reported instead.
					fSplit("DeclaratorNotSplit", "void fg_F(C@@ &_A, int _B)\n{\n}\n", "void fg_F\n\t(\n\t\tC@@ &_A\n\t\t, int _B\n\t)\n{\n}\n");
					// A trailing qualifier run is a logical unit of its own.
					fSplit
						(
							// The qualifiers and the pure specifier stand behind the closing
							// parenthesis.
							"Qualifiers"
							, "struct C\n{\n\tvoid f_F(int @, int @) const volatile = 0;\n};\n"
							, "struct C\n{\n\tvoid f_F\n\t\t(\n\t\t\tint @\n\t\t\t, int @\n\t\t) const volatile = 0\n\t;\n};\n"
						)
					;
					// An operator function's name is written apart from its parameter list, and
					// its symbol is a name rather than an operation to be split at.
					fSplit
						(
							"OperatorName"
							, "struct C\n{\n\tvoid operator + (int @, int @) &&;\n};\n"
							, "struct C\n{\n\tvoid operator +\n\t\t(\n\t\t\tint @\n\t\t\t, int @\n\t\t) &&\n\t;\n};\n"
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

				DMibTestCategory("Templates")
				{
					CStr Wide;
					for (umint i = 0; i < 80; ++i)
						Wide += "W";

					auto fSplit = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected)
						{
							fg_ExpectFormat(_Case, _Source.f_Replace("@", Wide), _Expected.f_Replace("@", Wide));
						}
					;
					// Two lists closing with one '>>' are two scopes: the inner one is opened
					// only when the outer one's element still does not fit, and each closing
					// marker stands under the list it ends.
					fSplit
						(
							"NestedOpened"
							, "template TCSharedPointer<TCPromiseData<TCVector<@, @>>>::~TCSharedPointer();\n"
							, "template TCSharedPointer\n\t<\n\t\tTCPromiseData\n\t\t<\n\t\t\tTCVector<@, @>\n\t\t>\n\t>\n\t::~TCSharedPointer()\n;\n"
						)
					;
					// The parameter list is opened first; the name's own argument list only
					// when the name still does not fit in front of it.
					fSplit
						(
							"NameAfterParameters"
							, "template void fg_Delete<TCVector<@, @>, CAllocator &>(CAllocator &, TCVector<@, @> *);\n"
							, "template void fg_Delete\n\t<\n\t\tTCVector<@, @>\n\t\t, CAllocator &\n\t>\n\t(\n\t\tCAllocator &\n\t\t, TCVector<@, @> *\n\t)\n;\n"
						)
					;
					// An operator behind a '>>' is still at the statement's own level.
					fSplit
						(
							"OperatorAfterSharedCloser"
							, "void f()\n{\n\tauto R = Left.f_Bind<&C::f_D<TCFuture<uint32>, @>>(_f, Start, Mid) + Right.f_Bind<&C::f_D<TCFuture<uint32>, @>>(_f, Mid, End);\n}\n"
							, "void f()\n{\n\tauto R = Left.f_Bind<&C::f_D<TCFuture<uint32>, @>>(_f, Start, Mid)\n\t\t+ Right.f_Bind<&C::f_D<TCFuture<uint32>, @>>(_f, Mid, End)\n\t;\n}\n"
						)
					;
					// An explicit instantiation moves its return type like any declaration: a
					// '*' behind a template argument list is a declarator, not a multiplication,
					// and spaced closers collapse to one '>>'.
					CStr Pointer = "template TCCounter<TCOnScopeExit<TCFunction<void ()> >, false, 0> *\n"
						"TCConstruct<TCCounter<@>, TCFunction<void ()> >::f_Create<TCCounter<@>, CAllocator &>(CAllocator &);\n"
					;
					CStr PointerResult = "template auto TCConstruct\n\t<\n\t\tTCCounter<@>\n\t\t, TCFunction<void ()>\n\t>\n\t::f_Create<TCCounter<@>, CAllocator &>(CAllocator &)\n"
						"\t-> TCCounter<TCOnScopeExit<TCFunction<void ()>>, false, 0> *\n;\n"
					;
					fg_ExpectFormat("ExplicitInstantiation", Pointer.f_Replace("@", Wide), PointerResult.f_Replace("@", Wide), false);
					fg_ExpectFormat("SpacedClosers", "TCMap<CStr, TCVector<CStr> > g_Map;\n", "TCMap<CStr, TCVector<CStr>> g_Map;\n");
					// A macro written on a line of its own inside a list stands next to a name,
					// which the standard does not settle, so the list keeps its lines.
					CStr Macro = "template\n<\n\tauto tf_pMember\n\tDMibIfNotSupported(, uint32 tf_NameHash)\n\t, typename tf_CActor\n>\nvoid fg_F();\n";
					fg_ExpectFormat("MacroInList", Macro, Macro);
					// An attribute macro behind a return type is not part of it, and the
					// declaration is not converted around it.
					CStr Attribute = "template TCFuture<CStr> DMibWorkaround fg_Export(TCActor<CTrustManagerInterface> _TrustManager, CStr _UserID, bool _bIncludePrivate@);\n";
					CStr AttributeResult = "template TCFuture<CStr> DMibWorkaround fg_Export\n\t(\n\t\tTCActor<CTrustManagerInterface> _TrustManager\n"
						"\t\t, CStr _UserID\n\t\t, bool _bIncludePrivate@\n\t)\n;\n"
					;
					fSplit("AttributeMacro", Attribute, AttributeResult);
					// A class's 'final' stays behind its template argument list's closing
					// marker, like a function's qualifiers behind its parameter list.
					fSplit
						(
							"FinalAfterArguments"
							, "struct TCFoo<@, @> final : public CBase\n{\n};\n"
							, "struct TCFoo\n\t<\n\t\t@\n\t\t, @\n\t> final\n\t: public CBase\n{\n};\n"
						)
					;
				};

				DMibTestCategory("Calls")
				{
					CStr Wide;
					for (umint i = 0; i < 70; ++i)
						Wide += "W";

					// A call's argument list is opened before the name's template argument list,
					// inside an element as much as at statement level.
					CStr Source = CStr("void f()\n{\n\tg(TCSharedPointer<TCVector<@> const>(fg_Construct<TCVector<@>>(a, b)));\n}\n").f_Replace("@", Wide);
					CStr Expected = CStr("void f()\n{\n\tg\n\t\t(\n\t\t\tTCSharedPointer<TCVector<@> const>\n\t\t\t(\n\t\t\t\tfg_Construct<TCVector<@>>(a, b)\n\t\t\t)\n\t\t)\n\t;\n}\n")
						.f_Replace("@", Wide)
					;
					fg_ExpectFormat("ArgumentsBeforeName", Source, Expected);
				};

				DMibTestCategory("Members")
				{
					CStr Wide;
					for (umint i = 0; i < 60; ++i)
						Wide += "W";

					auto fSplit = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected)
						{
							fg_ExpectFormat(_Case, _Source.f_Replace("@", Wide), _Expected.f_Replace("@", Wide));
						}
					;
					// Member accesses are the last thing to give: only a line that is still too
					// long with every scope on it opened is broken at them, all at once.
					fSplit
						(
							"LastResort"
							, "void f()\n{\n\ta@.b@.c@.f_Call(1);\n}\n"
							, "void f()\n{\n\ta@\n\t\t.b@\n\t\t.c@\n\t\t.f_Call(1)\n\t;\n}\n"
						)
					;
					fSplit
						(
							"ScopeFirst"
							, "void f()\n{\n\ta.b.f_Call(@, @, @);\n}\n"
							, "void f()\n{\n\ta.b.f_Call\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t\t, @\n\t\t)\n\t;\n}\n"
						)
					;
				};

				DMibTestCategory("Braced")
				{
					// A braced initializer is opened like any other scope.
					CStr Wide;
					for (umint i = 0; i < 90; ++i)
						Wide += "W";

					CStr Braced = CStr("void f()\n{\n\treturn CVersions{fg_Max(@), fg_Min(@)};\n}\n").f_Replace("@", Wide);
					CStr Opened = CStr("void f()\n{\n\treturn CVersions\n\t\t{\n\t\t\tfg_Max(@)\n\t\t\t, fg_Min(@)\n\t\t}\n\t;\n}\n").f_Replace("@", Wide);
					fg_ExpectFormat("Opened", Braced, Opened);
					// One that is already open is never closed again, however short it is.
					CStr Short = "void f()\n{\n\treturn CVersions\n\t\t{\n\t\t\t1\n\t\t\t, 2\n\t\t}\n\t;\n}\n";
					fg_ExpectFormat("Kept", Short, Short);
				};

				DMibTestCategory("Clause")
				{
					// A clause ends at its condition, so its body opens a statement of its
					// own. That statement is nothing but the block and ends with it: an
					// 'else' behind it is the next statement, not part of this one.
					fg_ExpectFormat
						(
							"ElseBody"
							, "void f()\n{\n\tif (X)\n\t{\n\t}\n\telse\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t5\n\t\t\t)\n\t\t;\n\t\th();\n\t}\n}\n"
							, "void f()\n{\n\tif (X)\n\t{\n\t}\n\telse\n\t{\n\t\tg(5);\n\t\th();\n\t}\n}\n"
						)
					;
					// A do-while keeps its own shape across the same boundary.
					CStr DoWhile = "void f()\n{\n\tdo\n\t{\n\t\tg(5);\n\t}\n\twhile (X);\n}\n";
					fg_ExpectFormat("DoWhile", DoWhile, DoWhile);
				};

				DMibTestCategory("Lambda")
				{
					// A lambda is given a line of its own first, then its introducer is
					// separated from its parameters, and only then its return type.
					CStr Head = "void f()\n{\n\tm_fOnClose = g_ActorFunctorWeak / ";
					CStr Split = "void f()\n{\n\tm_fOnClose = g_ActorFunctorWeak /\n\t\t";
					CStr Body = "\n\t{\n\t\treturn;\n\t}\n\t;\n}\n";
					CStr Captures = "[this, pConnectionWeak, Sequence, _bRetry, _SomeMoreCaptureNames, _AndYetAnotherOne]";
					CStr Params = "(NWeb::EWebSocketStatus _ReasonForTheClosure, NStr::CStr _MessageDescribingTheClose, NWeb::EWebSocketCloseOrigin _OriginOfTheClose)";
					fg_ExpectFormat("Whole", Head + "[this]" + Params + " -> TCFuture<void>" + Body, Split + "[this]" + Params + " -> TCFuture<void>" + Body);
					fg_ExpectFormat
						(
							"Introducer"
							, Head + Captures + Params + " -> TCFuture<void>" + Body
							, Split + Captures + "\n\t\t" + Params + " -> TCFuture<void>" + Body
						)
					;
					// A directive fixes the capture list's lines, and the parameter list behind
					// it keeps one of its own: a capture list does not own what follows it
					// the way a name owns its argument list.
					CStr Directive = "void f()\n{\n\tg\n\t\t(\n\t\t\t[\n\t\t\t\tKeepAlive = _Context.f_KeepAlive()\n#if DDebug\n\t\t\t\t, Actor = fg_ThisActor(_p)\n#endif\n\t\t\t]\n"
						"\t\t\t(CThreadLocal &_ThreadLocal) mutable\n\t\t\t{\n\t\t\t\treturn;\n\t\t\t}\n\t\t)\n\t;\n}\n"
					;
					fg_ExpectFormat("CaptureDirective", Directive, Directive);
					// A bare name behind a capture list, such as an attribute macro, trails the
					// list on its line, and the parameter list starts the next one.
					CStr Wide;
					for (umint i = 0; i < 90; ++i)
						Wide += "W";

					CStr Macro = "void f()\n{\n\tg([" + Wide + ", " + Wide + "] mark_no_coroutine_debug() mutable { h(); });\n}\n";
					CStr MacroResult = "void f()\n{\n\tg\n\t\t(\n\t\t\t[\n\t\t\t\t" + Wide + "\n\t\t\t\t, " + Wide +
						"\n\t\t\t] mark_no_coroutine_debug\n\t\t\t() mutable\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
					;
					fg_ExpectFormat("CaptureMacro", Macro, MacroResult);

					// An explicit template parameter list is part of the introducer, so all
					// three parts take a line together or none of them does.
					CStr Template = "<typename ...tfp_CParams, typename tf_CActor>";
					fg_ExpectFormat
						(
							"TemplateParameters"
							, Head + Captures + Template + Params + " -> TCFuture<void>" + Body
							, Split + Captures + "\n\t\t" + Template + "\n\t\t" + Params + " -> TCFuture<void>" + Body
						)
					;
				};

				DMibTestCategory("Directives")
				{
					// The branches of a conditional are alternatives inside one construct,
					// so the construct is laid out and each directive only ends the line it
					// stands on: what follows one starts the next line, at the level the
					// construct gives it.
					fg_ExpectFormat
						(
							"Chain"
							, "void f()\n{\n\tco_return co_await m_Promises\n#if 1\n\t\t.f_Insert()\n#else\n\t\t\t.f_Insert()\n#endif\n\t\t\t.f_Future()\n\t;\n}\n"
							, "void f()\n{\n\tco_return co_await m_Promises\n#if 1\n\t\t.f_Insert()\n#else\n\t\t.f_Insert()\n#endif\n\t\t.f_Future()\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"Arguments"
							, "void f()\n{\n\tg(a\n#if 1\n\t\t, b\n#else\n\t\t\t, c\n#endif\n\t\t, d\n\t);\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n#if 1\n\t\t\t, b\n#else\n\t\t\t, c\n#endif\n\t\t\t, d\n\t\t)\n\t;\n}\n"
						)
					;
					// A stretch between two directives is a line of its own and is only
					// broken further where it is too long, so a construct one runs through
					// is not opened up to make room no line of it needs.
					CStr Qualifier = "struct C\n{\n\t~C()\n#if DDebug\n\t\tnoexcept(false)\n#endif\n\t;\n};\n";
					fg_ExpectFormat("Qualifier", Qualifier, Qualifier);
					// An operator standing at a directive is one the construct is written
					// broken at, so every operator binding as loosely keeps its own line.
					CStr Condition = "void f()\n{\n\tif constexpr\n\t(\n\t\t(a || !b)\n\t\t&& (c || d)\n#if DDebug\n\t\t&& false\n#endif\n\t)\n\t{\n\t\tg();\n\t}\n}\n";
					fg_ExpectFormat("Condition", Condition, Condition);
					CStr Statements = "void f()\n{\n\tg();\n#if 1\n\th();\n#else\n\ti();\n#endif\n\tj();\n}\n";
					fg_ExpectFormat("Statements", Statements, Statements);
					// A branch that spells a piece of a construct rather than a whole
					// alternative leaves the file no shape to be laid out against, and a
					// branch that ends a clause the next one begins again leaves the
					// construct around it cut in two. Both keep the lines the source gave.
					CStr Cut = "void f()\n{\n#if 1\n\tg(a\n#else\n\tg(b\n#endif\n\t);\n}\n";
					fg_ExpectFormat("Cut", Cut, Cut);
					CStr Guard = "void f()\n{\n#if 1\n\tif (a)\n#else\n\tif (b)\n#endif\n\t\t\tg();\n}\n";
					fg_ExpectFormat("Guard", Guard, Guard);
				};

				DMibTestCategory("EastQualifier")
				{
					// A qualifier in front of its type moves behind it, which changes where a
					// token stands and nothing else.
					auto fMoves = [&](CStr const &_Case, CStr const &_Source, CStr const &_Expected)
						{
							fg_ExpectFormat(_Case, _Source, _Expected, false);
						}
					;
					fMoves("Parameter", "void f(const int &_A, const NStr::CStr &_B, const auto &_C);\n", "void f(int const &_A, NStr::CStr const &_B, auto const &_C);\n");
					// Specifiers stay in front wherever the source had them, and a run of
					// qualifiers moves as one.
					fMoves("Specifier", "inline static const ch8 *gc_pA = \"\";\nconst static int gs_B;\n", "inline static ch8 const *gc_pA = \"\";\nstatic int const gs_B;\n");
					// A line break inside the span is one the layout takes out, so it is no
					// reason to wait for the next pass.
					fMoves("Broken", "const\n\tint g_A = 0;\n", "int const g_A = 0;\n");
					fMoves("Run", "const volatile int g_A = 0;\nvolatile uint32 g_B;\n", "int const volatile g_A = 0;\nuint32 volatile g_B;\n");
					// The type is followed to its end: through its template arguments, where a
					// qualifier of their own moves as well, a fundamental type's several words,
					// 'typename', an elaborated name, 'decltype', and a leading '::'.
					fMoves("Nested", "const TCVector<TCVector<const int *>> &fg_F();\n", "TCVector<TCVector<int const *>> const &fg_F();\n");
					fMoves("Fundamental", "const unsigned long long g_A = 0;\n", "unsigned long long const g_A = 0;\n");
					fMoves
						(
							"Spelled"
							, "template <typename t_C>\nconst typename t_C::CType *fg_F(const struct CFoo *_p, const decltype(g_A) &_A, const ::NMib::CStr &_B);\n"
							, "template <typename t_C>\ntypename t_C::CType const *fg_F(struct CFoo const *_p, decltype(g_A) const &_A, ::NMib::CStr const &_B);\n"
						)
					;
					// What follows the type is no part of it: a member pointer's class, and the
					// class of a member defined outside it.
					fMoves("Member", "const int CFoo::*g_pA;\nconst NStr::CStr CFoo::ms_Name;\n", "int const CFoo::*g_pA;\nNStr::CStr const CFoo::ms_Name;\n");
					fMoves
						(
							"Places"
							, "struct C\n{\n\toperator const ch8 * () const;\n\tauto f_A() const -> const int &;\n};\n"
							, "struct C\n{\n\toperator ch8 const * () const;\n\tauto f_A() const -> int const &;\n};\n"
						)
					;
					// Behind a name the qualifier can as well belong to it, so only a type and
					// then a declarator or a name behind the qualifier say that it led.
					fMoves("BehindName", "mark_nodebug const CFoo &fg_F();\nmark_nodebug const CFoo fg_G();\n", "mark_nodebug CFoo const &fg_F();\nmark_nodebug CFoo const fg_G();\n");
					// A qualifier already behind its type, a pointer's own, a function's, a macro's
					// bare argument, and one with a comment or an unresolved '<' behind it stay.
					CStr Kept = "struct C\n{\n\tCFoo const m_A;\n\tint *const m_pB = nullptr;\n\tCFoo const &f_C() const override;\n"
						"\tvoid f_D() const requires cFoo<C>;\n\tconst /* why */ int m_E;\n\tDMacro(const, x);\n};\n"
					;
					fg_ExpectFormat("Kept", Kept, Kept);
					// A macro between the type and its declarator would read as a type the moved
					// qualifier leads in turn, and the next pass would move it again.
					CStr Macro = "DExtern const CFoo DFar *fg_F();\nstatic const CFoo DFar *gs_pA;\n";
					fg_ExpectFormat("Macro", Macro, Macro);
					// A disabled region keeps the qualifier where it is.
					fMoves
						(
							"Disabled"
							, "// malterlib-format off\nconst int g_A = 0;\n// malterlib-format on\nconst int g_B = 0;\n"
							, "// malterlib-format off\nconst int g_A = 0;\n// malterlib-format on\nint const g_B = 0;\n"
						)
					;
				};

				DMibTestCategory("SpecifierOrder")
				{
					// 'static' stands in front of 'constexpr', whatever specifiers stand between
					// the two, and a qualifier behind them moves in the same stage.
					fg_ExpectFormat
						(
							"Static"
							, "struct C\n{\n\tconstexpr static umint mc_A = 1;\n\tconstexpr inline_always static int fs_B();\n\tconstexpr static const ch8 *mc_pC = \"\";\n};\n"
							, "struct C\n{\n\tstatic constexpr umint mc_A = 1;\n\tstatic constexpr inline_always int fs_B();\n\tstatic constexpr ch8 const *mc_pC = \"\";\n};\n"
							, false
						)
					;
					fg_ExpectFormat("Broken", "constexpr\nstatic umint gc_A = 1;\n", "static constexpr umint gc_A = 1;\n", false);
					// The order the sources mostly have, a 'constexpr' with no 'static' behind it,
					// a comment between the two, and a disabled region stay as they are.
					CStr Kept = "struct C\n{\n\tstatic constexpr umint mc_A = 1;\n\tconstexpr umint f_B() const;\n\tconstexpr /* c */ static umint mc_C = 1;\n};\n"
						"// malterlib-format off\nconstexpr static int g_Off = 0;\n// malterlib-format on\n"
					;
					fg_ExpectFormat("Kept", Kept, Kept);
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
					// With the trailing type on its own line the signature fits, and the
					// parameter list is left whole: splitting it is the step after this one.
					fSplit("Declaration", "TCLongTemplate<@> fg_F(int _A, int _B);\n", "auto fg_F(int _A, int _B)\n\t-> TCLongTemplate<@>\n;\n");
					// A return type that ends in a declarator stands against the name, and the
					// keyword that takes its place does not.
					fSplit("Declarator", "TCLongTemplate<@> &fg_F(int _A);\n", "auto fg_F(int _A)\n\t-> TCLongTemplate<@> &\n;\n");
					// A qualifier in front of the return type has moved behind it before the type
					// moves, each in a stage of its own.
					fSplit("Qualified", "const TCLongTemplate<@> &fg_F(const int _A);\n", "auto fg_F(int const _A)\n\t-> TCLongTemplate<@> const &\n;\n");
					fSplit
						(
							"PureSpecifier"
							, "struct C\n{\n\tvirtual TCLongTemplate<@> f_F(int _A) const = 0;\n};\n"
							, "struct C\n{\n\tvirtual auto f_F(int _A) const\n\t\t-> TCLongTemplate<@> = 0\n\t;\n};\n"
						)
					;
					fSplit("Definition", "TCLongTemplate<@> fg_F(int _A, int _B)\n{\n}\n", "auto fg_F(int _A, int _B)\n\t-> TCLongTemplate<@>\n{\n}\n");
					// The trailing type goes after the qualifiers.
					fSplit
						(
							"Qualifiers"
							, "struct C\n{\n\tTCLongTemplate<@> f_F(int _A) const volatile;\n};\n"
							, "struct C\n{\n\tauto f_F(int _A) const volatile\n\t\t-> TCLongTemplate<@>\n\t;\n};\n"
						)
					;
					// 'override' is written after the declarator, so the trailing type goes in
					// front of it rather than behind.
					fSplit
						(
							"VirtSpecifier"
							, "struct C\n{\n\tTCLongTemplate<@> f_F(int _A) override;\n};\n"
							, "struct C\n{\n\tauto f_F(int _A)\n\t\t-> TCLongTemplate<@> override\n\t;\n};\n"
						)
					;
					// Declaration specifiers stay in front of auto.
					fSplit("Specifiers", "static inline_always TCLongTemplate<@> fg_F(int _A);\n", "static inline_always auto fg_F(int _A)\n\t-> TCLongTemplate<@>\n;\n");
					// A constructor has no return type to move. Its name does not fit even with
					// the parameter list opened, so the name opens its own template argument
					// list, and the parameter list then fits on the line the name ends on.
					fSplit("Constructor", "CLongName<@>::CLongName(int _A, int _B);\n", "CLongName\n\t<\n\t\t@\n\t>\n\t::CLongName(int _A, int _B)\n;\n", true);
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
					// An existing trailing return type is only relaid out, and moving it to its
					// own line already makes the signature fit.
					fSplit("AlreadyTrailing", "auto fg_F(int _A, int _B) -> TCLongTemplate<@>;\n", "auto fg_F(int _A, int _B)\n\t-> TCLongTemplate<@>\n;\n", true);
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

					// A function type inside a template argument is written with a space the
					// standard does not settle, and that space counts: a declaration that would
					// be one column too long with it stays open.
					DMibTestCategory("FunctionTypeSpace")
					{
						CStr Split = "\tvoid CClient::f_SetLazyStartApp\n\t\t(\n\t\t\tNFunction::TCFunction"
							"<FStopApp (NEncoding::CEJsonSorted const &_Params, EDistributedAppCommandFlag _Flags)> const &_fLazyStartApp"
						;
						for (umint i = Split.f_GetLen(); i < 208; ++i)
							Split += "X";

						Split = "namespace N\n{\n" + Split + "\n\t\t)\n\t{\n\t}\n}\n";
						DMibExpect(fg_FormatSource(Split), ==, Split);
					};
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
							, "void f()\n{\n\t// malterlib-format off\n        int a;\n\t// malterlib-format on\n\tint b;\n}\n"
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

				DMibExpect(fFormatRange("SecondLine", 11, 15, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");
				// A cursor selects the line it sits on.
				DMibExpect(fFormatRange("Cursor", 14, 0, ECodeRangePolicy::mc_Expand), ==, "void f()\n{\n\tint a;\n        int b;\n        int c;\n}\n");
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
					// A statement that ends at the brace of the block it stands in is how a
					// list without terminators is written, and no violation of anything.
					DMibExpect(fStructureLine("EnumBody", "enum E\n{\n\tmc_A = 0\n\t, mc_B = 1\n};\n"), ==, 0u);
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
