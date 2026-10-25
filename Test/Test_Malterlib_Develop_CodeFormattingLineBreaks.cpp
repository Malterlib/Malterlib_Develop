// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Test_Malterlib_Develop_CodeFormatting.h"

namespace NMib::NDevelop::NCodeFormattingTest
{
	void fg_TestLineBreaks()
	{
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
				fg_ExpectFormat("TemplateDeclarator", "void fg_F(TCVector<int>\n\t*&_pA, TCVector<int> **ppB);\n", "void fg_F(TCVector<int> * &_pA, TCVector<int> **ppB);\n");
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
				// A requires clause's constraint declares nothing, which settles it too.
				fg_ExpectFormat
					(
						"ConceptOrDeclarator"
						, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires (cFoo<t_C> &&\n\t\tcBar<t_C>)\n;\n"
						, "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires (cFoo<t_C> && cBar<t_C>)\n;\n"
					)
				;
			};

			DMibTestCategory("Blocks")
			{
				// A body opens on a line of its own, and its closing brace takes one too.
				fg_ExpectFormat("EmptyBody", "void f(){}\n", "void f()\n{\n}\n");
				// A brace behind a lambda's introducer is its body whatever it holds, where an
				// empty one behind a subscript initializes an array. A guarded statement that
				// holds a body is laid out across lines, and so stands within braces.
				fg_ExpectFormat
					(
						"EmptyLambdaBody"
						, "void f()\n{\n\tfg_X([]{});\n\tint a[3]{};\n\tauto p = new int[3]{};\n\tif (b)\n\t\ty = g_S / [] {};\n}\n"
						, "void f()\n{\n\tfg_X\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t}\n\t\t)\n\t;\n\tint a[3]{};\n\tauto p = new int[3]{};\n"
						"\tif (b)\n\t{\n\t\ty = g_S / []\n\t\t\t{\n\t\t\t}\n\t\t;\n\t}\n}\n"
						, false
					)
				;
				fg_ExpectFormat("BodyAfterInitializer", "struct C\n{\n\tC(int _A)\n\t\t: m_A(_A){}\n};\n", "struct C\n{\n\tC(int _A)\n\t\t: m_A(_A)\n\t{\n\t}\n};\n");
				fg_ExpectFormat("BraceOnHead", "void f() {\n\tg();\n}\n", "void f()\n{\n\tg();\n}\n");
				fg_ExpectFormat("OneLine", "void f() { g(); }\n", "void f()\n{\n\tg();\n}\n");
				fg_ExpectFormat("Definition", "struct C { int a; };\n", "struct C\n{\n\tint a;\n};\n");
				// A statement too long for its line gives at its declarators' commas first.
				{
					CStr Name;
					for (umint i = 0; i < 50; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"Declarators"
							, CStr("void f()\n{\n\tint a@ = fg_Long(x@), b@ = fg_Long(y@), c = 3;\n\tint g_A = (x@), g_B = {y@}, g_C = {(z@)}, g_D = 4;\n}\n").f_Replace("@", Name)
							, CStr
							(
								"void f()\n{\n\tint a@ = fg_Long(x@)\n\t\t, b@ = fg_Long(y@)\n\t\t, c = 3\n\t;\n"
								"\tint g_A = (x@)\n\t\t, g_B = {y@}\n\t\t, g_C = {(z@)}\n\t\t, g_D = 4\n\t;\n}\n"
							)
							.f_Replace("@", Name)
						)
					;
				}
				// A comment or directive in an otherwise empty list puts its closing marker on a line of its own, where the list
				// would open.
				fg_ExpectFormat
					(
						"EmptyListHoldingDirectives"
						, "void f()\n{\n\tp.add<\n#define A\n#undef A\n      >(c, d);\n\tp.add<\n// A\n>(c, d);\n}\n"
						, "void f()\n{\n\tp.add<\n#define A\n#undef A\n\t\t>\n\t\t(c, d)\n\t;\n\tp.add<\n\t\t\t// A\n\t\t>\n\t\t(c, d)\n\t;\n}\n"
					)
				;
				// A line splice inside brackets keeps their lines.
				fg_ExpectFormat
					(
						"LineSpliceInCode"
						, "void f()\n{\n    int const a[][2] = {\\\n        {\n            \\\n            {0, 1}, \\\n            {2, 3}\\\n        }\\\n    };\n}\n"
						, "void f()\n{\n\tint const a[][2] = {\\\n\t\t{\n\t\t\t\\\n\t\t\t{0, 1}, \\\n\t\t\t{2, 3}\\\n\t\t}\\\n\t};\n}\n"
					)
				;
				// So does a statement a splice joins to the line of the one in front of it.
				fg_ExpectFormat
					(
						"SplicedStatement"
						, "void f()\n{\n      a = g(x);      /* c */ \\\n      b = g(y);  /* d */ \\\n      ROW(0, 2)\n\n      /* e */\n      a = g(z);\n}\n"
						, "void f()\n{\n\ta = g(x);      /* c */ \\\n\t  b = g(y);  /* d */ \\\n\t  ROW(0, 2)\n\n\t/* e */\n\ta = g(z);\n}\n"
					)
				;
				// A brace behind a label opens a statement of its own, and a comment in front of it stands where it does.
				fg_ExpectFormat
					(
						"BraceBehindLabel"
						, "void f()\n{\n  if(s) {\n#ifndef A\n  case 1:\n#endif\n    /*\n     * B\n     */\n#ifdef C\n    {\n    }\n#endif\n#ifdef C\n    else\n#endif\n  }\n}\n"
						, "void f()\n{\n\tif (s)\n\t{\n#ifndef A\n\tcase 1:\n#endif\n\t\t/*\n\t\t * B\n\t\t */\n#ifdef C\n\t\t{\n\t\t}\n#endif\n#ifdef C\n\t\telse\n#endif\n\t}\n}\n"
					)
				;
				// A statement whose requires clause fixes its line breaks still stands at its block's level, and its lines move with it.
				fg_ExpectFormat
					(
						"RequiresClauseIndentation"
						, "namespace n\n{\ntemplate <class T> requires true void f();\nvoid h()\n{\n[](auto) requires true { return 1; }(1);\n}\n}\n"
						, "namespace n\n{\n\ttemplate <class T>\n\trequires true void f();\n\tvoid h()\n\t{\n\t\t[](auto) requires true\n\t\t{\n\t\t\treturn 1;\n\t\t}(1);\n\t}\n}\n"
					)
				;
				// A requires clause's '&&' and '||' join constraints, also behind a template-id or a parenthesis, and the
				// declaration behind the clause keeps its declarators.
				fg_ExpectFormat
					(
						"RequiresClauseOperators"
						, "template <class T>\nrequires cA<T>\n\t&& cB<T>\nvoid g();\ntemplate <class T>\nrequires cB<T> && (cA<T>)\nvoid k(int _A, T &&_Other);\n"
						"template <class T>\nrequires cA<T> TCFoo<T> &&fg_F(T &&_A);\n"
						, "template <class T>\nrequires cA<T>\n\t&& cB<T>\nvoid g();\ntemplate <class T>\nrequires cB<T> && (cA<T>)\nvoid k(int _A, T &&_Other);\n"
						"template <class T>\nrequires cA<T> TCFoo<T> &&fg_F(T &&_A);\n"
					)
				;
				// A body written on its label's line takes lines under the label where that line is too long.
				{
					CStr Name;
					for (umint i = 0; i < 165; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"LabelLineTooLong"
							, CStr("void f(int a)\n{\n\tswitch (a)\n\t{\n\tcase 1: fg_Call(1); break;\n\tcase 2: fg_Call(@); break;\n\t}\n}\n").f_Replace("@", Name)
							, CStr("void f(int a)\n{\n\tswitch (a)\n\t{\n\tcase 1: fg_Call(1); break;\n\tcase 2:\n\t\tfg_Call(@);\n\t\tbreak;\n\t}\n}\n").f_Replace("@", Name)
						)
					;
				}
				// An attribute behind a template header in a statement whose lines a requires clause fixes stays on its line, and
				// the requires expression in the clause opens.
				fg_ExpectFormat
					(
						"AttributeBehindHeader"
						, "template <class T>\n[[nodiscard]] inline int f(T a)\n\trequires requires {\n\t\t{ a + 1 } -> cA;\n\t}\n{\n\treturn a;\n}\n"
						, "template <class T>\n[[nodiscard]] inline int f(T a)\n\trequires requires\n\t{\n\t\t{ a + 1 } -> cA;\n\t}\n{\n\treturn a;\n}\n"
					)
				;
				// A requires expression in a concept's definition is no requires clause the declaration ends in, and the
				// definition keeps its lines.
				{
					CStr Kept = "template <class T>\nconcept cA =\n\tcB<T> &&\n\trequires (T &a)\n{\n\t{ a.f() } -> cC;\n};\n";
					fg_ExpectFormat("ConceptRequiresExpression", Kept, Kept);
				}
				// A requires clause the layout joins is one written on one line, and the initializer list behind it is laid out.
				fg_ExpectFormat
					(
						"RequiresClauseJoined"
						, "struct C\n{\n\tC(int a)\n\t\trequires (cA<int,\n\t\t\tint>)\n\t\t\t: m_A{a}, m_B{a}\n\t{\n\t}\n"
						"\tC(C const &_Other)\n\t\trequires (cB<int>)\n\t= default;\n};\n"
						, "struct C\n{\n\tC(int a)\n\t\trequires (cA<int, int>)\n\t\t: m_A{a}\n\t\t, m_B{a}\n\t{\n\t}\n\n"
						"\tC(C const &_Other)\n\t\trequires (cB<int>)\n\t= default;\n};\n"
					)
				;
				// A macro behind which a blank line parts the names of a declaration is a statement of its own, wherever the
				// source breaks those names.
				fg_ExpectFormat
					(
						"MacroBeforeDeclaration"
						, "struct a\n{\n\tKMP_ALIGN(8) // c\n\n\tCFoo volatile\n\t\tm_A; // d\n\tint m_B;\n};\n"
						, "struct a\n{\n\tKMP_ALIGN(8) // c\n\n\tCFoo volatile m_A; // d\n\tint m_B;\n};\n"
					)
				;
				// A comment line behind a block's opening brace and the comment trailing it is the block's own unless it is
				// aligned with that comment.
				fg_ExpectFormat
					(
						"CommentBehindOpeningBrace"
						, "void f()\n{\n\tif (b)\n\t{\n\t}\n\telse if (c) { // e.g. x\n\t\t// These cannot be specified.\n\t}\n\telse\n\t{\n\t}\n}\n"
						, "void f()\n{\n\tif (b)\n\t{\n\t}\n\telse if (c)\n\t{ // e.g. x\n\t\t// These cannot be specified.\n\t}\n\telse\n\t{\n\t}\n}\n"
					)
				;
				// A braced list whose one element is another written against its markers, '{{' over the elements over '}}',
				// opens as any list holding a list does, each marker on a line of its own.
				{
					CStr Name;
					for (umint i = 0; i < 60; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"ListWrittenAgainstElementMarkers"
							, CStr("void f()\n{\n   std::array<std::array<int, 2>, 4> const g_A =\n   {{\n      {{a@, b@}}, {{c@, d@}}, {{e@, f@}}\n   }};\n}\n").f_Replace("@", Name)
							, CStr
							(
								"void f()\n{\n\tstd::array<std::array<int, 2>, 4> const g_A =\n\t\t{\n\t\t\t{\n\t\t\t\t{{a@, b@}}\n\t\t\t\t, {{c@, d@}}\n"
								"\t\t\t\t, {{e@, f@}}\n\t\t\t}\n\t\t}\n\t;\n}\n"
							)
							.f_Replace("@", Name)
						)
					;
				}
				// A comment behind a declaration's first token stays behind it where the declaration goes on below it, also
				// once its return type moves behind the parameter list.
				{
					CStr Comment;
					for (umint i = 0; i < 170; ++i)
						Comment += "c";

					fg_ExpectFormat
						(
							"CommentBehindReturnType"
							, "TCFoo<int &>::CType // " + Comment + "\nfg_Function();\n"
							, "auto // " + Comment + "\nfg_Function()\n\t-> TCFoo<int &>::CType\n;\n"
							, false
						)
					;
				}
				// A line a group joins is no line the move of its statement re-indents as well.
				fg_ExpectFormat("JoinedMovedLine", "void f() {\n  a =  /*tail*/ g(b ,\nc);\n}\n", "void f()\n{\n\ta =  /*tail*/ g(b, c);\n}\n");
				// A group is measured for joining from where the move of the line it stands on puts it.
				fg_ExpectFormat
					(
						"JoinMeasuredOnMovedLine"
						, CStr
						(
							"namespace clang {\n"
							"TEST(MatcherCrashDeathTest, CrashOnCallbackDump) {\n"
							"  crashTestNodeDump(\n"
							"      forStmt(hasLoopInit(declStmt(hasSingleDecl(\n"
							"                                       varDecl(hasType(qualType().bind(\"QT\")),\n"
							"                                               hasType(type().bind(\"T\")),\n"
							"                                               hasInitializer(\n"
							"                                                   integerLiteral().bind(\"IL\")))\n"
							"                                           .bind(\"VD\")))\n"
							"                              .bind(\"DS\")))\n"
							"          .bind(\"FS\"),\n"
							"      {\"FS - { ForStmt : <input.cc:3:5, line:4:5> }\",\n"
							"       \"VD - { VarDecl I : <input.cc:3:10, col:18> }\"},\n"
							"      R\"cpp(\n"
							"  void foo() {\n"
							"  }\n"
							"  )cpp\");\n"
							"}\n"
							"} // end namespace clang\n"
						)
						, CStr
						(
							"namespace clang\n"
							"{\n"
							"\tTEST(MatcherCrashDeathTest, CrashOnCallbackDump)\n"
							"\t{\n"
							"\t\tcrashTestNodeDump(\n"
							"\t\t\tforStmt(hasLoopInit(declStmt(hasSingleDecl(varDecl(hasType(qualType().bind(\"QT\")), "
							"hasType(type().bind(\"T\")), hasInitializer(integerLiteral().bind(\"IL\"))).bind(\"VD\")))\n"
							"\t\t\t\t\t\t\t\t\t.bind(\"DS\")))\n"
							"\t\t\t\t.bind(\"FS\"),\n"
							"\t\t\t{\"FS - { ForStmt : <input.cc:3:5, line:4:5> }\",\n"
							"\t\t\t \"VD - { VarDecl I : <input.cc:3:10, col:18> }\"},\n"
							"\t\t\tR\"cpp(\n"
							"  void foo() {\n"
							"  }\n"
							"  )cpp\");\n"
							"\t}\n"
							"} // end namespace clang\n"
						)
					)
				;
				// A lambda in a braced list broken only inside its parameter list is laid out as one written on one line, its
				// head joined and its body on lines of its own.
				fg_ExpectFormat
					(
						"LambdaElementBrokenInsideGroups"
						, "void f() {\n  static CFoo const g_A = {\n      [](int a, int b,\n         int c) { return 1; },\n      [](int a,\n         int c) { return 2; }\n  };\n}\n"
						, "void f()\n{\n\tstatic CFoo const g_A =\n\t\t{\n\t\t\t[](int a, int b, int c)\n\t\t\t{\n\t\t\t\treturn 1;\n\t\t\t}\n"
						"\t\t\t, [](int a, int c)\n\t\t\t{\n\t\t\t\treturn 2;\n\t\t\t}\n\t\t}\n\t;\n}\n"
					)
				;
				// So is every lambda written on one line in a list moving its commas, a comma behind it included.
				fg_ExpectFormat
					(
						"LambdaElementsBehindCommas"
						, "CFoo f()\n{\n\treturn {\n\t\t[&] () { g(1); },\n\t\t[&] () { g(2); }\n\t};\n}\n"
						, "CFoo f()\n{\n\treturn\n\t\t{\n\t\t\t[&]()\n\t\t\t{\n\t\t\t\tg(1);\n\t\t\t}\n\t\t\t, [&]()\n\t\t\t{\n\t\t\t\tg(2);\n\t\t\t}\n\t\t}\n\t;\n}\n"
					)
				;
				// A requires expression's body in a lambda's requires clause is no body of the lambda's.
				fg_ExpectFormat
					(
						"LambdaRequiresExpression"
						, "void g()\n{\n\tauto x = [](auto) requires requires (int t) { f(); } { return 1; };\n}\n"
						, "void g()\n{\n\tauto x = [](auto) requires requires (int t)\n\t\t{\n\t\t\tf();\n\t\t}\n\t{\n\t\treturn 1;\n\t};\n}\n"
					)
				;
				// An expansion statement keeps its lines, too long for them as well.
				{
					CStr Name;
					for (umint i = 0; i < 160; ++i)
						Name += "W";

					CStr Kept = CStr("int f()\n{\n\ttemplate for (auto &&x : static_cast<Wrapper<N@> &&>(nm)) sum += x.x;\n\treturn sum;\n}\n").f_Replace("@", Name);
					fg_ExpectFormat("ExpansionStatement", Kept, Kept);
				}
				// So does 'if consteval', with the 'else' of a clause around it, and its blocks keep their spacing.
				{
					CStr Kept = "void f(bool a)\n{\n\tif (a) if consteval { g(); } else { h(); } else i();\n\tif !consteval { g(); }\n\ttemplate for (auto x : T) { g(x); }\n}\n";
					fg_ExpectFormat("IfConsteval", Kept, Kept);
				}
				// An element of a braced list broken only between a cast and its operand is one written on one line.
				fg_ExpectFormat("ListElementCastOperand", "P g_a[] = {\n  (P)&::a,\n  (P)\n  (g),\n  (P)&::b\n};\n", "P g_a[] =\n\t{\n\t\t(P)&::a\n\t\t, (P)(g)\n\t\t, (P)&::b\n\t}\n;\n");
				// A statement behind the end of a comment over several lines stays where the comment leaves it, and is joined
				// there where it fits.
				fg_ExpectFormat
					(
						"BehindMultiLineComment"
						, "int main()\n{\n\n/*<  A\n     B.\n>*/  typedef \n        n::token<char const*, n::omit, n::false_> \n     token_type;\n}\n"
						, "int main()\n{\n/*<  A\n\t B.\n>*/  typedef n::token<char const *, n::omit, n::false_> token_type;\n}\n"
					)
				;
				// A statement opening with a group the layout joins did not open at it, and its terminator takes a line of its own.
				{
					CStr Name;
					for (umint i = 0; i < 85; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"JoinedLeadingGroup"
							, CStr("void f()\n{\n    (\n        g@(a),\n        g@(b)\n    )\n    (h, c);\n}\n").f_Replace("@", Name)
							, CStr("void f()\n{\n\t(g@(a), g@(b))\n\t\t(h, c)\n\t;\n}\n").f_Replace("@", Name)
						)
					;
				}
				// The body of a statement whose lines move with its first moves along, where the statement's layout keeps them.
				{
					CStr Name;
					for (umint i = 0; i < 60; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"BodyMovesWithStatement"
							, CStr
							(
								"namespace n {\n   template <class T>\n   e_c\n      < e@<T>::value && !h@<T>::value\n      , typename a@<T>::type &\n"
								"      >::type\n         f(T& x)\n   {\n      return x;\n   }\n}\n"
							)
							.f_Replace("@", Name)
							, CStr
							(
								"namespace n\n{\n\ttemplate <class T>\n\te_c < e@<T>::value\n\t\t&& !h@<T>::value, typename a@<T>::type & > ::type f(T &x)\n"
								"\t{\n\t\treturn x;\n\t}\n}\n"
							)
							.f_Replace("@", Name)
						)
					;
				}
				// An empty body sharing the last line of a head split at its operators takes its lines.
				{
					CStr Name = "add_reference";
					for (umint i = 0; i < 30; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"BodyBehindOperatorSplit"
							, CStr
							(
								"struct BOOST_JOIN(BOOST_TT_TRAIT_NAME, _ret_imp)<T, U, Ret, typename boost::make_void<decltype(std::declval<typename @<T>::type>()"
								" BOOST_TT_TRAIT_OP std::declval<typename @<U>::type>())>::type>\n\t: public boost::integral_constant<bool, ::boost::is_convertible"
								"<decltype(std::declval<typename @<T>::type>() BOOST_TT_TRAIT_OP std::declval<typename @<U>::type>()), Ret>::value> {};\n"
							)
							.f_Replace("@", Name)
							, CStr
							(
								"struct BOOST_JOIN(BOOST_TT_TRAIT_NAME, _ret_imp)\n\t< T, U, Ret, typename boost::make_void\n\t<\n\t\tdecltype\n\t\t(\n"
								"\t\t\tstd::declval<typename @<T>::type>() BOOST_TT_TRAIT_OP std::declval<typename @<U>::type>()\n\t\t)\n\t>\n\t::type\n"
								"\t> : public boost::integral_constant\n\t<\n\t\tbool\n\t\t, ::boost::is_convertible\n\t\t<\n\t\t\tdecltype\n\t\t\t(\n"
								"\t\t\t\tstd::declval<typename @<T>::type>() BOOST_TT_TRAIT_OP std::declval<typename @<U>::type>()\n\t\t\t)\n\t\t\t, Ret\n"
								"\t\t>\n\t\t::value\n\t>\n{\n};\n"
							)
							.f_Replace("@", Name)
							, false
						)
					;
				}
				// Behind a namespace's or a linkage specification's braces a declaration starts its own line, a name too,
				// since nothing is declared of them.
				fg_ExpectFormat
					(
						"BehindNamespace"
						, "namespace a { namespace b { int x; } namespace c { int y; } }\nextern \"C\" { int z; } int w;\n"
						, "namespace a\n{\n\tnamespace b\n\t{\n\t\tint x;\n\t}\n\tnamespace c\n\t{\n\t\tint y;\n\t}\n}\nextern \"C\"\n{\n\tint z;\n}\nint w;\n"
					)
				;
				// A compound requirement opens its statement with a brace that holds an expression,
				// not a block; its braces stand apart from the expression and the arrow behind it
				// names a concept. One written across lines comes back to one.
				fg_ExpectFormat
					(
						"CompoundRequirement"
						, "template <typename t_F>\nconcept cFoo = requires (t_F _f)\n\t{\n\t\t{_f(1)\n\t\t} ->cIsSame<bool>;\n\t\t{ _f(2) };\n\t\t{_f(3)} noexcept;\n\t}\n;\n"
						, "template <typename t_F>\nconcept cFoo = requires (t_F _f)\n\t{\n\t\t{ _f(1) } -> cIsSame<bool>;\n\t\t{ _f(2) };\n\t\t{ _f(3) } noexcept;\n\t}\n;\n"
					)
				;
				// An attribute behind a body starts the next declaration; it subscripts nothing.
				fg_ExpectFormat
					(
						"AttributeAfterBody"
						, "struct C\n{\n\tbool f_A() const\n\t{\n\t\treturn true;\n\t}\n\t[[nodiscard]] bool f_B() const {return false;}\n};\n"
						, "struct C\n{\n\tbool f_A() const\n\t{\n\t\treturn true;\n\t}\n\n\t[[nodiscard]] bool f_B() const\n\t{\n\t\treturn false;\n\t}\n};\n"
					)
				;
				// A definition's keyword stands behind the template header that declares it,
				// and a template's body holds statements the same way: one whose only member
				// is a function definition has no terminator at its own level to say so.
				fg_ExpectFormat
					(
						"TemplateDefinition"
						, "template <typename t_C>\nstruct TCFoo\n{\n\tint f_Get(t_C &&_Value) { return 0; }\n};\n"
						, "template <typename t_C>\nstruct TCFoo\n{\n\tint f_Get(t_C &&_Value)\n\t{\n\t\treturn 0;\n\t}\n};\n"
					)
				;
				fg_ExpectFormat
					(
						"SpecializedDefinition"
						, "template <>\nstruct TCFoo<int>\n{\n\tint f_Get(int &&_Value) { return 0; }\n};\n"
						, "template <>\nstruct TCFoo<int>\n{\n\tint f_Get(int &&_Value)\n\t{\n\t\treturn 0;\n\t}\n};\n"
					)
				;
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
				// 'else if' and a case of one statement written on its label's line, a 'break'
				// behind it included, are kept as written; a block or more statements take lines of
				// their own under it.
				CStr ElseIf = "void f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t\th();\n}\n";
				fg_ExpectFormat("ElseIf", ElseIf, ElseIf);
				fg_ExpectFormat
					(
						"CompactCases"
						, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: a = 1; break;\n\tcase 2: return;\n\tcase 3: {int b; g(b);} break;\n\tcase 4: a = 1; b = 2; break;\n"
						"\tdefault: break;\n\t}\n}\n"
						, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: a = 1; break;\n\tcase 2: return;\n\tcase 3:\n\t\t{\n\t\t\tint b;\n\t\t\tg(b);\n\t\t}\n"
						"\t\tbreak;\n\tcase 4:\n\t\ta = 1;\n\t\tb = 2;\n\t\tbreak;\n\tdefault: break;\n\t}\n}\n"
					)
				;
				// A name behind a closing brace declares a variable of the type just defined.
				CStr Instance = "struct\n{\n\tint a;\n} g_Instance;\n";
				fg_ExpectFormat("Instance", Instance, Instance);
				// A gap is joined the way it is measured, so an operator the spacing rules write
				// apart joins its line like any other.
				fg_ExpectFormat
					(
						"JoinSpacedOperator"
						, "template <typename tf_C>\nTCEnableIf\n<\n\tcA<tf_C>\n\t|| cB<tf_C>\n\t, void\n>\nfg_Call(tf_C &_Type)\n{\n}\n"
						, "template <typename tf_C>\nTCEnableIf<cA<tf_C> || cB<tf_C>, void> fg_Call(tf_C &_Type)\n{\n}\n"
					)
				;
				// A name in one branch of a conditional is not followed by the word the other
				// branch opens with, so the declarator in front of it still hugs it.
				{
					CStr Branches = "void f()\n{\n\tauto fAdd = [&]\n\t\t(\n#if defined(X)\n\t\t\tauto &&_fThis\n#else\n\t\t\tthis auto &&_fThis\n#endif\n"
						"\t\t\t, int _A\n\t\t)\n\t\t{\n\t\t}\n\t;\n}\n"
					;
					fg_ExpectFormat("DeclaratorBeforeBranch", Branches, Branches);
				}
				// A condition spelled like a pointer to function's declarator is no declarator: the
				// arrow behind the statement it guards is a member access.
				{
					CStr Condition = "void f()\n{\n\tif (*pManager)\n\t\t(*pManager)->f_BlockDestroy(x);\n}\n";
					fg_ExpectFormat("ConditionNoDeclarator", Condition, Condition);
				}
				// A comment opening a statement's line keeps its text; only the indentation moves.
				fg_ExpectFormat("LeadingComment", "void f()\n{\n  /*x*/ g();\n}\n", "void f()\n{\n\t/*x*/ g();\n}\n");
				// Space behind a directive is trailing space like any other.
				fg_ExpectFormat("DirectiveTrailingSpace", "#include \"a.h\" \nint g_A;\n", "#include \"a.h\"\nint g_A;\n");
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
				// A call on what another call yields spells one call expression with it, and
				// gives its own line only where the line has nothing later to give: the
				// body below opens whatever else is done, so the expression in front of it
				// stays whole. Without one the argument list moves down, as an actor call's
				// does.
				fg_ExpectFormat
					(
						"YieldedCall"
						, "void f()\n{\n\tActor.f_CallActor(&C::f_Fn)(a, b).f_OnResultSet([&](int _R) { g(_R); });\n}\n"
						, "void f()\n{\n\tActor.f_CallActor(&C::f_Fn)(a, b).f_OnResultSet\n\t\t(\n\t\t\t[&](int _R)\n\t\t\t{\n\t\t\t\tg(_R);\n\t\t\t}\n\t\t)\n\t;\n}\n"
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
				// A member function's qualifiers stand between its parameter list and its body, 'volatile' as well as 'const'.
				fg_ExpectFormat
					(
						"VolatileMember"
						, "struct CFoo\n{\n\tvoid operator ()\n\t\t(\n\t\t\tint _A\n\t\t) const volatile\n\t{\n\t}\n};\n"
						, "struct CFoo\n{\n\tvoid operator () (int _A) const volatile\n\t{\n\t}\n};\n"
					)
				;
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
				// An attribute macro may stand between a lambda's capture list and its
				// parameters, apart from both, and the introducer comes back onto one line where
				// it fits. The body is still a lambda's: one level in. The arguments an
				// inner lambda is called with at once stand under its closing brace, and they
				// and the terminator follow the body wherever it moves.
				fg_ExpectFormat
					(
						"AttributeMacro"
						, "void f()\n{\n\tx =\n\t\t[\n\t\t\tpA\n\t\t] mark_nodebug\n\t\t() mutable -> int\n\t{\n\t\treturn [&] mark_nodebug (int _A) -> int\n\t\t\t{\n"
						"\t\t\t\treturn _A;\n\t\t\t}\n\t\t\t\t(5)\n\t\t\t;\n\t}\n\t;\n}\n"
						, "void f()\n{\n\tx = [pA] mark_nodebug () mutable -> int\n\t\t{\n\t\t\treturn [&] mark_nodebug (int _A) -> int\n\t\t\t\t{\n"
						"\t\t\t\t\treturn _A;\n\t\t\t\t}\n\t\t\t\t(5)\n\t\t\t;\n\t\t}\n\t;\n}\n"
					)
				;
				// A statement that opens with its lambda has no expression for the body to
				// stand under, so the body stands where the statement does, and what it is
				// called with under the brace, written as the source has it.
				fg_ExpectFormat
					(
						"CalledAtOnce"
						, "void f()\n{\n\t[&]() inline_never\n\t\t{\n\t\t\tg();\n\t\t}();\n\t[&]<typename ...tfp_C>(CList<tfp_C...> &&)\n\t\t{\n\t\t\tg();\n\t\t}\n\t\t(CParams());\n}\n"
						, "void f()\n{\n\t[&]() inline_never\n\t{\n\t\tg();\n\t}();\n\t[&]<typename ...tfp_C>(CList<tfp_C...> &&)\n\t{\n\t\tg();\n\t}\n\t(CParams());\n}\n"
					)
				;
				// Whether a statement broke at its operators is its own, and a statement that
				// opens with an attribute does not open with its lambda.
				CStr Wide;
				for (umint i = 0; i < 90; ++i)
					Wide += "W";

				fg_ExpectFormat
					(
						"AfterOperatorSplit"
						, CStr
						(
							"void f()\n{\n\tReturn += g(@) + h(@);\n\t[&]() inline_never\n\t\t{\n\t\t\tg();\n\t\t}\n\t();\n"
							"\t[[maybe_unused]] auto fA = [&]()\n\t{\n\t\treturn;\n\t}\n\t;\n}\n"
						)
						.f_Replace("@", Wide)
						, CStr
						(
							"void f()\n{\n\tReturn\n\t\t+= g(@)\n\t\t+ h(@)\n\t;\n\t[&]() inline_never\n\t{\n\t\tg();\n\t}\n\t();\n"
							"\t[[maybe_unused]] auto fA = [&]()\n\t\t{\n\t\t\treturn;\n\t\t}\n\t;\n}\n"
						)
						.f_Replace("@", Wide)
					)
				;
				// With no continuation level for the call to stand at, the terminator ends
				// the call's line rather than standing at that level on one of its own.
				fg_ExpectFormat
					(
						"CalledAtOnceTerminator"
						, "void f()\n{\n\t[&]() -> int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t\t() > fg_TempCopy(Promise)\n\t;\n}\n"
						, "void f()\n{\n\t[&]() -> int\n\t{\n\t\treturn 1;\n\t}\n\t() > fg_TempCopy(Promise);\n}\n"
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
				fg_ExpectFormat("Statements", "void f()\n{\n\t\tg();\n\th();\n\t\t\ti();\n\t}\n", "void f()\n{\n\tg();\n\th();\n\ti();\n}\n");
				// A label stands one level out from the statements written under it.
				fg_ExpectFormat
					(
						"Labels"
						, "void f()\n{\n\tswitch (a)\n\t{\n\t\tcase 1:\n\t\t\t\tg();\n\t\t\tbreak;\n\t\tdefault:\n\t\t\tbreak;\n\t}\n}\n"
						, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: g(); break;\n\tdefault: break;\n\t}\n}\n"
					)
				;
				// What a clause guards stands one level in from the clause, each clause
				// of a chain counting for one.
				fg_ExpectFormat
					(
						"Guarded"
						, "void f()\n{\n\tif (a)\n\t\t\tg();\n\telse\n\t{\n\t\t\th();\n\t}\n\n\tfor (auto &E : R)\n\t\tif (b)\n\t\t\t\ti();\n}\n"
						, "void f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\th();\n\n\tfor (auto &E : R)\n\t{\n\t\tif (b)\n\t\t\ti();\n\t}\n}\n"
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
				// A statement's lines its layout keeps move with its first, and a statement opening with an attribute moves
				// like any other.
				fg_ExpectFormat
					(
						"MovedStatementLines"
						, "namespace N\n{\n\t\ttemplate <typename t_CType>\n\t\tconcept cFoo = true;\n\n\t\t[[maybe_unused]] static int g_A = 1;\n}\n"
						, "namespace N\n{\n\ttemplate <typename t_CType>\n\tconcept cFoo = true;\n\n\t[[maybe_unused]] static int g_A = 1;\n}\n"
					)
				;
				// A call ending its statement keeps the lines a list inside it gives it, but its parenthesis stands one level
				// in from the statement, and every line inside moves with it.
				fg_ExpectFormat
					(
						"KeptCallMoves"
						, "void f()\n{\n\tfg_Call\n\t\t\t(\n\t\t\t\ta\n\t\t\t\t, fg_Inner\n\t\t\t\t(\n\t\t\t\t\t{\n\t\t\t\t\t\t1\n"
						"\t\t\t\t\t\t, 2\n\t\t\t\t\t}\n\t\t\t\t)\n\t\t\t)\n\t\t;\n}\n"
						, "void f()\n{\n\tfg_Call\n\t\t(\n\t\t\ta\n\t\t\t, fg_Inner\n\t\t\t(\n\t\t\t\t{\n\t\t\t\t\t1\n"
						"\t\t\t\t\t, 2\n\t\t\t\t}\n\t\t\t)\n\t\t)\n\t;\n}\n"
					)
				;
				// The declaration under a template's header stands at the header's indentation.
				fg_ExpectFormat
					(
						"TemplateDeclarationLine"
						, "namespace N\n{\n\ttemplate <typename t_CType>\n\t\tvoid fg_Test()\n\t{\n\t}\n}\n"
						, "namespace N\n{\n\ttemplate <typename t_CType>\n\tvoid fg_Test()\n\t{\n\t}\n}\n"
					)
				;
				// A conditional cutting a construct inside a function leaves the depths of the function's own lines alone,
				// not those of the scope around the function.
				fg_ExpectFormat
					(
						"OpaqueConditionalNested"
						, "struct CFoo\n{\n\tvoid f()\n\t{\n#if A\n\t\tif (a)\n\t\t{\n\t\t}\n\t\telse\n#endif\n\t\t{\n\t\t}\n\t}\n\n"
						"#if B\n\t\tvoid g()\n\t\t{\n\t\t}\n#endif\n};\n"
						, "struct CFoo\n{\n\tvoid f()\n\t{\n#if A\n\t\tif (a)\n\t\t{\n\t\t}\n\t\telse\n#endif\n\t\t{\n\t\t}\n\t}\n\n"
						"#if B\n\tvoid g()\n\t{\n\t}\n#endif\n};\n"
					)
				;
				// A body holding a conditional that cuts a construct moves under its head as a whole, the lines behind the
				// directives included, keeping their depths relative to each other.
				fg_ExpectFormat
					(
						"OpaqueConditionalBodyMoves"
						, "struct CFoo\n{\n\tvoid f()\n\t\t{\n#if A\n\t\t\tif (a)\n\t\t\t\tg();\n\t\t\telse\n#endif\n\t\t\t\th();\n\t\t}\n};\n"
						, "struct CFoo\n{\n\tvoid f()\n\t{\n#if A\n\t\tif (a)\n\t\t\tg();\n\t\telse\n#endif\n\t\t\th();\n\t}\n};\n"
					)
				;
				// Inside such a body the first statement stands one level in from the brace, the other lines and comments
				// as far from it as the source has them, and the closing brace under the opening one.
				fg_ExpectFormat
					(
						"OpaqueConditionalBodyLevels"
						, "struct CFoo\n{\n\tvoid f()\n\t{\n#if A\n\t\t\tif (a)\n\t\t\t\tg();\n\t\t\telse\n#endif\n\t\t\t\t// Comment\n\t\t\t\th();\n\t\t}\n};\n"
						, "struct CFoo\n{\n\tvoid f()\n\t{\n#if A\n\t\tif (a)\n\t\t\tg();\n\t\telse\n#endif\n\t\t\t// Comment\n\t\t\th();\n\t}\n};\n"
					)
				;
				// A statement behind an 'else' a directive cuts it from, written neither at the 'else''s level nor one level in,
				// stands at the block's level, where it does in the configuration without the 'else'.
				fg_ExpectFormat
					(
						"OpaqueConditionalElseStatement"
						, "void f()\n{\n#if A\n  if (a)\n    g();\n  else\n#endif\n     if (b)\n    h();\n}\n"
						, "void f()\n{\n#if A\n\tif (a)\n\t\tg();\n\telse\n#endif\n\tif (b)\n\t\th();\n}\n"
					)
				;
				// Each stretch between such a body's directives moves on its own, so stretches written at different depths
				// all come to the body's level.
				fg_ExpectFormat
					(
						"OpaqueConditionalBodyStretches"
						, "void f()\n{\n#if A\n\t\tg();\n#endif\n\n#if B\n\tif (a)\n\t\th();\n\telse\n#endif\n\t\tk();\n}\n"
						, "void f()\n{\n#if A\n\tg();\n#endif\n\n#if B\n\tif (a)\n\t\th();\n\telse\n#endif\n\t\tk();\n}\n"
					)
				;
				// An alternative branch is placed by what stands in front of its conditional, and a stretch inside an argument
				// list moves with the statement it is in.
				fg_ExpectFormat
					(
						"OpaqueConditionalBodyAlternatives"
						, "void f()\n{\n\tg();\n#ifdef A\n\t errno_t Errno = h();\n\t if (a)\n#else\n\tb = h();\n\tif (b)\n#endif\n\t{\n\t\tk();\n\t}\n"
						"\tfg_Call\n\t\t(\n#if B\n\t\t\tfg_Wrap\n\t\t\t(\n#endif\n\t\t\t\tx\n#if B\n\t\t\t)\n#endif\n\t\t\t, y\n\t\t)\n\t;\n}\n"
						, "void f()\n{\n\tg();\n#ifdef A\n\terrno_t Errno = h();\n\tif (a)\n#else\n\tb = h();\n\tif (b)\n#endif\n\t{\n\t\tk();\n\t}\n"
						"\tfg_Call\n\t\t(\n#if B\n\t\t\tfg_Wrap\n\t\t\t(\n#endif\n\t\t\t\tx\n#if B\n\t\t\t)\n#endif\n\t\t\t, y\n\t\t)\n\t;\n}\n"
					)
				;
				// A directive inside an initializer's braces is part of its statement, not a stretch of the body.
				fg_ExpectFormat
					(
						"OpaqueConditionalBodyInitializer"
						, "extern \"C\"\n{\n#ifndef A\n\tstatic\n#endif\n\tint g_A[] =\n\t\t{\n#include \"B.h\"\n\t\t}\n\t;\n\n\tstruct CFoo\n\t{\n\t\tint m_A;\n\t};\n}\n"
						, "extern \"C\"\n{\n#ifndef A\n\tstatic\n#endif\n\tint g_A[] =\n\t\t{\n#include \"B.h\"\n\t\t}\n\t;\n\n\tstruct CFoo\n\t{\n\t\tint m_A;\n\t};\n}\n"
					)
				;
				// A statement no cutting conditional stands in or in front of is placed as in any block.
				fg_ExpectFormat
					(
						"OpaqueConditionalBodyUncut"
						, "extern \"C\"\n{\n#ifndef A\n\tstatic\n#endif\n\tint g_A = 1;\n\nstruct CFoo\n{\n\tint m_A;\n};\n}\n"
						, "extern \"C\"\n{\n#ifndef A\n\tstatic\n#endif\n\tint g_A = 1;\n\n\tstruct CFoo\n\t{\n\t\tint m_A;\n\t};\n}\n"
					)
				;
				// Alternative heads of one function each stand at the statement's depth, and the body under them, at file
				// scope as well; the comments in front of a head stand at its depth.
				fg_ExpectFormat
					(
						"OpaqueConditionalAlternativeHeads"
						, "int g_A;\n\n#if A\n\t// First\nvoid fg_First(int _A)\n#else\nvoid fg_First(long _A)\n#endif\n\t{\n\t\tfg_Use(_A);\n\t}\n"
						, "int g_A;\n\n#if A\n// First\nvoid fg_First(int _A)\n#else\nvoid fg_First(long _A)\n#endif\n{\n\tfg_Use(_A);\n}\n"
					)
				;
				// The body of a guard a conditional cuts moves with the block it stands in, its brace behind the directive included.
				fg_ExpectFormat
					(
						"OpaqueConditionalGuardBodyMoves"
						, "void f()\n{\n\tauto fA = [&]\n\t{\n\t\tfor\n#if A\n\t\t(int i = 0; i < 1; ++i)\n#else\n\t\t(\n\t\t\tint i = 0\n#\tif B\n"
						"\t\t\t; i < 2\n#\telse\n\t\t\t; i < 4\n#\tendif\n\t\t\t; ++i\n\t\t)\n#endif\n\t\t{\n\t\t\tg(i);\n\t\t}\n\t}\n\t;\n}\n"
						, "void f()\n{\n\tauto fA = [&]\n\t\t{\n\t\t\tfor\n#if A\n\t\t\t(int i = 0; i < 1; ++i)\n#else\n\t\t\t(\n\t\t\t\tint i = 0\n#\tif B\n"
						"\t\t\t\t; i < 2\n#\telse\n\t\t\t\t; i < 4\n#\tendif\n\t\t\t\t; ++i\n\t\t\t)\n#endif\n\t\t\t{\n\t\t\t\tg(i);\n\t\t\t}\n\t\t}\n\t;\n}\n"
					)
				;
				// A lambda's body below the operator line its lambda is written on stands at that line's level.
				fg_ExpectFormat
					(
						"OperatorLineBody"
						, "void f()\n{\n\tfg_Call(a, Argument0, Argument1, Argument2, Argument3, Argument4, Argument5, "
						"Argument6, Argument7, Argument8, Argument9, Argument10, Argument11)\n"
						"\t\t> fg_Move(Promise) / [this](CResult &&_Result)\n\t\t\t{\n\t\t\t\tfg_Use(_Result);\n\t\t\t}\n\t;\n}\n"
						, "void f()\n{\n\tfg_Call(a, Argument0, Argument1, Argument2, Argument3, Argument4, Argument5, "
						"Argument6, Argument7, Argument8, Argument9, Argument10, Argument11)\n"
						"\t\t> fg_Move(Promise) / [this](CResult &&_Result)\n\t\t{\n\t\t\tfg_Use(_Result);\n\t\t}\n\t;\n}\n"
					)
				;
				// The first code in a file stands at the file's depth as well, behind directives alone or nothing at all.
				fg_ExpectFormat
					(
						"FirstCodeInFile"
						, "#if A\n#include <a>\n\n\tnamespace std\n\t{\n\t\tint a;\n\t}\n#endif\nint b;\n"
						, "#if A\n#\tinclude <a>\n\nnamespace std\n{\n\tint a;\n}\n#endif\nint b;\n"
					)
				;
				// Directives are indented apart from the code, so a statement inside a conditional stands at its block's depth.
				fg_ExpectFormat("Conditional", "void f()\n{\n\t#if DDebug\n\t\tg();\n\t#endif\n\th();\n}\n", "void f()\n{\n#if DDebug\n\tg();\n#endif\n\th();\n}\n");
			};

			DMibTestCategory("Braces")
			{
				// A block behind an attribute on the clause's line is braced already, and the
				// 'else' behind it belongs to the clause, not inside another pair.
				CStr AttributeBlock = "void f()\n{\n\tif (a) [[unlikely]]\n\t{\n\t\tg();\n\n\t\treturn h();\n\t}\n\telse\n\t\treturn i();\n}\n";
				fg_ExpectFormat("AttributeBlock", AttributeBlock, AttributeBlock);
				// Each lambda an operator joins opens its body, the ones behind the operator as well.
				fg_ExpectFormat
					(
						"LambdasJoinedByOperator"
						, "void f()\n{\n\tint x = ([] { return 1; }() + [] { return 2; }());\n}\n"
						, "void f()\n{\n\tint x =\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\treturn 1;\n\t\t\t}()\n\t\t\t+ []\n\t\t\t{\n\t\t\t\treturn 2;\n\t\t\t}()\n\t\t)\n\t;\n}\n"
					)
				;
				// A guarded label's statement is the guarded one, and the braces take the two.
				{
					CStr Argument;
					for (umint i = 0; i < 190; ++i)
						Argument += "x";

					fg_ExpectFormat
						(
							"GuardedLabel"
							, "void f()\n{\n\tif (b)\n\t\tRetry: g(" + Argument + ", 1);\n\tif (b)\n\t\tAgain: g();\n}\n"
							, "void f()\n{\n\tif (b)\n\t{\n\tRetry:\n\t\tg\n\t\t\t(\n\t\t\t\t" + Argument + "\n\t\t\t\t, 1\n\t\t\t)\n\t\t;\n\t}\n\tif (b)\n\t\tAgain: g();\n}\n"
							, false
						)
					;
				}
				// A statement broken only inside a group it joins with goes without braces, a block comment in it or not.
				fg_ExpectFormat
					(
						"NamedArgumentBrokenInGroup"
						, "void f()\n{\n\tif (bEncrypt) {\n\t\tfg_ForEach<4, /*bOut=*/false>(\n\t\t\tVecs, fWhole);\n\t} else {\n\t\tg();\n\t}\n}\n"
						, "void f()\n{\n\tif (bEncrypt)\n\t\tfg_ForEach<4, /*bOut=*/false>(Vecs, fWhole);\n\telse\n\t\tg();\n}\n"
						, false
					)
				;
				// Braces dropped below a body the layout expands, whose trailing comment has a comment line under it.
				fg_ExpectFormat
					(
						"BelowExpandedBody"
						, "struct F\n{\npublic:\n\tint c() {} // A\n\t// B\n};\nvoid g()\n{\n\tif (a)\n\t{\n\t\tc();\n\t}\n}\n"
						, "struct F\n{\npublic:\n\tint c()\n\t{\n\t} // A\n\n\t// B\n};\n\nvoid g()\n{\n\tif (a)\n\t\tc();\n}\n"
						, false
					)
				;
				// One holding a braced list that keeps its lines keeps its braces.
				fg_ExpectFormat
					(
						"BracedListBrokenInGroup"
						, "void f()\n{\n\tif (bA) {\n\t\tm_Variables[\"PREFIX\"] = {\n\t\t\t\"PATH\", Dir\n\t\t};\n\t}\n}\n"
						, "void f()\n{\n\tif (bA)\n\t{\n\t\tm_Variables[\"PREFIX\"] =\n\t\t\t{\n\t\t\t\t\"PATH\"\n\t\t\t\t, Dir\n\t\t\t}\n\t\t;\n\t}\n}\n"
					)
				;
				// Nested guards close at one place, the inner brace first, also where a qualifier moves in the inner
				// clause on the way.
				CStr Value;
				for (umint i = 0; i < 170; ++i)
					Value += "x";

				fg_ExpectFormat
					(
						"NestedWithQualifier"
						, "void f()\n{\n\tif (a)\n\t\tif (const auto *pValue = " + Value + ")\n\t\t\tg(a,\n\t\t\t\tb);\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t\tif\n\t\t(\n\t\t\tauto const *pValue\n\t\t\t= " + Value + "\n\t\t)\n\t\t{\n\t\t\tg(a, b);\n\t\t}\n\t}\n}\n"
						, false
					)
				;
				// An 'else if' behind a closing brace is measured from its 'else' where the layout puts it, at the start of
				// a line, and its clause fitting there lets its single statement go without braces.
				CStr ElseCondition;
				for (umint i = 0; i < 175; ++i)
					ElseCondition += "b";

				fg_ExpectFormat
					(
						"ElseIfBehindBrace"
						, "void f()\n{\n\tif (a)\n\t{\n\t} else if (" + ElseCondition + ")\n\t{\n\t\tg();\n\t}\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t}\n\telse if (" + ElseCondition + ")\n\t\tg();\n}\n"
						, false
					)
				;
				// The braces around a guarded 'if' that has its 'else' close in front of the 'else' behind them, which
				// belongs to the clause outside.
				fg_ExpectFormat
					(
						"ChainElseOutside"
						, "void f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t\tif (c)\n\t\t{\n\t\t\th();\n\t\t}\n\t\telse\n\t\t{\n\t\t\tk();\n\t\t}\n"
						"\telse\n\t\tm();\n}\n"
						, "void f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t{\n\t\tif (c)\n\t\t\th();\n\t\telse\n\t\t\tk();\n\t}\n\telse\n\t\tm();\n}\n"
						, false
					)
				;
				// Braces around a single statement go where the block around them closes behind them.
				fg_ExpectFormat("DroppedBeforeOuterClose", "void f() { for (auto &Value : g_List) { g(Value); } }\n", "void f()\n{\n\tfor (auto &Value : g_List)\n\t\tg(Value);\n}\n", false);
				// A guard holding another guard and its statement on one line gets braces: the inner statement takes a
				// line of its own.
				fg_ExpectFormat("ChainOnOneLine", "void f()\n{\n\tfor (;;)\n\t\tif (a) return;\n}\n", "void f()\n{\n\tfor (;;)\n\t{\n\t\tif (a)\n\t\t\treturn;\n\t}\n}\n", false);
				// The first element of a braced list behind a comment stands with the others, whose commas move
				// in front of them.
				fg_ExpectFormat("ListCommentFirst", "int g_A[] = {\n /* Main */\n { 1, 2 },\n { 0 }\n};\n", "int g_A[] =\n\t{\n\t\t/* Main */\n\t\t{1, 2}\n\t\t, {0}\n\t}\n;\n");
				// A single guarded statement stands without braces, whatever lines the
				// source wrote them on.
				fg_ExpectFormat("Dropped", "void f()\n{\n\tif (a)\n\t{\n\t\tg();\n\t}\n\telse\n\t{\n\t\th();\n\t}\n}\n", "void f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\th();\n}\n", false);
				// A lone name is a statement only as a macro, which may expand to more than one.
				// An attribute in front hides what the statement is, an 'if' that would take the 'else' behind among them.
				fg_ExpectFormat
					(
						"KeptAroundAttributed"
						, "void f()\n{\n\tif (a)\n\t{\n\t\t[[likely]] if (b) x = 1;\n\t}\n\telse\n\t\tx = 2;\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t\t[[likely]] if (b) x = 1;\n\t}\n\telse\n\t\tx = 2;\n}\n"
					)
				;
				fg_ExpectFormat("KeptAroundLoneName", "void f()\n{\n\tif (a)\n\t{\n\t\tDAct;\n\t}\n}\n", "void f()\n{\n\tif (a)\n\t{\n\t\tDAct;\n\t}\n}\n");
				fg_ExpectFormat("DroppedAroundJump", "void f()\n{\n\twhile (a)\n\t{\n\t\tbreak;\n\t}\n}\n", "void f()\n{\n\twhile (a)\n\t\tbreak;\n}\n", false);
				// A statement behind the closing brace on its line goes to a line of its own, and the braces go.
				fg_ExpectFormat
					(
						"StatementBehindBrace"
						, "void f()\n{\n\tif (a)\n\t{\n\t\tif (b) { g(); } return;\n\t}\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t\tif (b)\n\t\t\tg();\n\t\treturn;\n\t}\n}\n"
						, false
					)
				;
				// The body a conditional cuts from its 'else' opens where that 'else' stands, at its 'if's level.
				fg_ExpectFormat
					(
						"BodyBehindConditionalElse"
						, "void f()\n{\n#if DA\n  if (b) {\n    g();\n  }else\n#endif\n  {\n    h();\n  }\n}\n"
						, "void f()\n{\n#if DA\n\tif (b)\n\t\tg();\n\telse\n#endif\n\t{\n\t\th();\n\t}\n}\n"
						, false
					)
				;
				// So does one behind the 'else' ending an 'else if' chain, at the level of the 'if' heading the chain.
				fg_ExpectFormat
					(
						"BodyBehindConditionalElseIf"
						, "void f()\n{\n#if DA\n  if (a) {\n    g();\n  } else if (b) {\n    h();\n  } else\n#endif\n  {\n    i();\n  }\n}\n"
						, "void f()\n{\n#if DA\n\tif (a)\n\t\tg();\n\telse if (b)\n\t\th();\n\telse\n#endif\n\t{\n\t\ti();\n\t}\n}\n"
						, false
					)
				;
				// An 'else' behind a chain of clauses without braces stands where the 'if' it answers does, which is
				// where its braces are measured from.
				CStr ElseArgument;
				for (umint i = 0; i < 175; ++i)
					ElseArgument += "a";

				fg_ExpectFormat
					(
						"ElseBehindUnbracedChain"
						, "void f()\n{\n\tif (a)\n\t\tif (b)\n\t\t\tif (c) {\n\t\t\t\tg();\n\t\t\t} else {\n\t\t\t\th(" + ElseArgument + ");\n\t\t\t}\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t\tif (b)\n\t\t{\n\t\t\tif (c)\n\t\t\t\tg();\n\t\t\telse\n\t\t\t{\n"
						"\t\t\t\th\n\t\t\t\t\t(\n\t\t\t\t\t\t"
						+ ElseArgument
						+ "\n\t\t\t\t\t)\n\t\t\t\t;\n\t\t\t}\n\t\t}\n\t}\n}\n"
						, false
					)
				;
				// An 'if' an 'else' in front already answers is passed over: the second 'else' answers the outer 'if'.
				CStr OuterElseArgument;
				for (umint i = 0; i < 176; ++i)
					OuterElseArgument += "a";

				fg_ExpectFormat
					(
						"ElseAnswersOuterIf"
						, "void f()\n{\n\tif (a)\n\t\tif (b) {\n\t\t\tg();\n\t\t} else {\n\t\t\th();\n\t\t}\n\telse {\n\t\tj(" + OuterElseArgument + ");\n\t}\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t\tif (b)\n\t\t\tg();\n\t\telse\n\t\t\th();\n\t}\n\telse\n\t\tj(" + OuterElseArgument + ");\n}\n"
						, false
					)
				;
				// An 'else' of a chain a conditional cuts stands where the 'if' it answers does, which the conditional does not
				// separate from it.
				fg_ExpectFormat
					(
						"ElseOfChainConditionalCuts"
						, "void f()\n{\n\tif (a)\n\t\tg();\n#if DA\n\telse if (b)\n\t\th();\n\telse\n#endif\n\t\tif (c)\n\t\t\ti();\n\telse if (d)\n\t\tj();\n}\n"
						, "void f()\n{\n\tif (a)\n\t\tg();\n#if DA\n\telse if (b)\n\t\th();\n\telse\n#endif\n\t\tif (c)\n\t\t\ti();\n"
						"\t\telse if (d)\n\t\t\tj();\n}\n"
					)
				;
				// A block comment the closing brace stands behind keeps the braces.
				fg_ExpectFormat("BlockCommentBeforeBrace", "void f()\n{\n\twhile (a) { g(); /* c */ }\n}\n", "void f()\n{\n\twhile (a)\n\t{\n\t\tg(); /* c */ }\n}\n");
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
				// A parameter list whose line would give at its specification anyway stays behind
				// the capture list, the specification and the return type taking lines below.
				{
					CStr Spec = "noexcept(noexcept(t_CKey(fg_Forward<decltype(_Key)>(_Key))) && noexcept(t_CValue(fg_Forward<decltype(_Value)>(_Value))) && !c_bCanThrowCheck)";
					fg_ExpectFormat
						(
							"LambdaParametersBehindCapture"
							, "void f()\n{\n\tauto fA = [&]\n\t\t(this auto &&_fThis, auto &&_Key, auto &&_Value)\n\t\t" + Spec + "\n\t\t-> void\n\t\t{\n\t\t\tg();\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tauto fA = [&](this auto &&_fThis, auto &&_Key, auto &&_Value)\n\t\t" + Spec + "\n\t\t-> void\n\t\t{\n\t\t\tg();\n\t\t}\n\t;\n}\n"
						)
					;
				}
				// A list keeping its lines because of a braced element still opens a lambda body
				// written on one line in another of its elements.
				fg_ExpectFormat
					(
						"LambdaBodyInKeptList"
						, "void f()\n{\n\tg\n\t\t(\n\t\t\t{\n\t\t\t\ta\n\t\t\t\t, b\n\t\t\t}\n\t\t\t, [](CA const &) -> uint32 { return 0; }\n\t\t)\n\t;\n}\n"
						, "void f()\n{\n\tg\n\t\t(\n\t\t\t{\n\t\t\t\ta\n\t\t\t\t, b\n\t\t\t}\n\t\t\t, [](CA const &) -> uint32\n\t\t\t{\n\t\t\t\treturn 0;\n\t\t\t}\n"
						"\t\t)\n\t;\n}\n"
					)
				;
				// An address passed to a member call is no declarator, so the call hugs its name and a chain
				// behind an empty call on what it yields gives at its members, however the source opened it.
				{
					CStr Message = "\"Timed out waiting for application pre stop (1 hour) and then some more text to fill the line\"";
					fg_ExpectFormat
						(
							"ChainBehindYieldedCall"
							, "void f()\n{\n\tResult = co_await pApp->m_Interface.f_CallActor\n\t\t(\n\t\t\t&CInterface::f_PreStop\n\t\t)().f_Timeout(60.0, "
							+ Message
							+ ")"
							".f_Wrap()\n\t;\n}\n"
							, "void f()\n{\n\tResult = co_await pApp->m_Interface.f_CallActor(&CInterface::f_PreStop)()\n\t\t.f_Timeout(60.0, " + Message + ")\n\t\t.f_Wrap()\n\t;\n}\n"
						)
					;
				}
				// An operand taking a lambda whose capture list does not fit behind it gives at the
				// lambda's operator before any of its own scopes opens.
				{
					CStr Captures = "this, FileName, _Description, _Environment, _fOnStdOutput, pState, _WarningErrorStatus, BlockingActorCheckout = fg_Move(BlockingActorCheckout)";
					fg_ExpectFormat
						(
							"LambdaOperatorInOperand"
							, "void f()\n{\n\tfg_Dispatch(Actor)\n\t\t> pState->m_Promise % fg_Format(\"[{}] Failed to save\", _Description) / ["
							+ Captures
							+ "]\n"
							"\t\t{\n\t\t\th();\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tfg_Dispatch(Actor)\n\t\t> pState->m_Promise % fg_Format(\"[{}] Failed to save\", _Description)\n\t\t/ ["
							+ Captures
							+ "]\n"
							"\t\t{\n\t\t\th();\n\t\t}\n\t;\n}\n"
						)
					;
				}
				// A capture holding a conditional stays whole in an opened capture list.
				{
					CStr Source = "void f()\n{\n\tg_Dispatch(BlockingActorCheckout) /\n\t\t[\n\t\t\tUserName = mp_MongoUser.m_UserName\n\t\t\t, MongoVersion = mp_MongoVersion\n"
						"\t\t\t, MongoPort = mp_MongoConnectionSettings.f_GetSingleHost().m_Port\n\t\t\t, MongoReplicaName = fp_ShouldUseReplica() ? mp_MongoReplicaName : CStr{}\n"
						"\t\t]\n\t\t{\n\t\t\tg();\n\t\t}\n\t;\n}\n"
					;
					fg_ExpectFormat("ConditionalCapture", Source, Source);
				}
				// A braced initializer in an operand is a value, so the operand still gives at its operators.
				{
					CStr Call = "pMonitoredPath->m_FreePercentReporter->m_fReportReadings(TCVector<CDistributedAppSensorReporter::CSensorReading>{fg_Move(Reading)})";
					fg_ExpectFormat
						(
							"BracedValueInOperand"
							, "void f()\n{\n\t" + Call + " % (\"Failed to report readings (free %) for path {}\"_f << Path) > ReportResults;\n}\n"
							, "void f()\n{\n\t" + Call + "\n\t\t% (\"Failed to report readings (free %) for path {}\"_f << Path)\n\t\t> ReportResults\n\t;\n}\n"
						)
					;
				}
				// A lambda body splits the expression it ends: every loosest operator of the head, and one
				// behind the body, starts a line of its own at the body's level.
				fg_ExpectFormat
					(
						"OperatorsAroundLambdaBody"
						, "void f()\n{\n\tco_await\n\t\t(\n\t\t\tfg_A(1) + g_Dispatch(Actor) / [=]() -> int\n\t\t\t{\n\t\t\t\treturn g();\n\t\t\t} + fg_B(2)\n\t\t)\n\t;\n}\n"
						, "void f()\n{\n\tco_await\n\t\t(\n\t\t\tfg_A(1)\n\t\t\t+ g_Dispatch(Actor) / [=]() -> int\n\t\t\t{\n\t\t\t\treturn g();\n\t\t\t}\n"
						"\t\t\t+ fg_B(2)\n\t\t)\n\t;\n}\n"
					)
				;
				// A chain on a braced value or an empty call gives at every member, its first included,
				// wherever the value stands.
				{
					CStr Chain = ".f_SetNotes(\"Testing12\")\n\t\t\t.f_SetSecret(fg_TempCopy(ByteVectorSecret))\n\t\t\t.f_SetTags({\"Shared1\", \"Shared2\", \"Unique2\"})\n"
						"\t\t\t.f_SetSemanticID(\"Semantic2\")\n"
					;
					fg_ExpectFormat
						(
							"ChainOnBracedValue"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\tCSecretsManager::CSecretID{\"Folder1\", \"Name2\"}\n\t\t\t, CSecretsManager::CSecretProperties{}" + Chain + "\t\t)\n\t;\n}\n"
							, "void f()\n{\n\tg\n\t\t(\n\t\t\tCSecretsManager::CSecretID{\"Folder1\", \"Name2\"}\n\t\t\t, CSecretsManager::CSecretProperties{}\n\t\t\t"
							+ Chain
							+ "\t\t)\n\t;\n}\n"
						)
					;
					CStr Bind = ".f_Bind<fg_ImportUser<TCDistributedActorWrapper<CDistributedActorTrustManagerInterface>>>(ServerTrust, ExportedWithoutPrivate)";
					fg_ExpectFormat
						(
							"ChainOnEmptyCall"
							, "void f()\n{\n\tfg_CurrentActor()" + Bind + ".f_CallSync(RunLoopHelper.m_pRunLoop, g_Timeout);\n}\n"
							, "void f()\n{\n\tfg_CurrentActor()\n\t\t" + Bind + "\n\t\t.f_CallSync(RunLoopHelper.m_pRunLoop, g_Timeout)\n\t;\n}\n"
						)
					;
				}
				// A value that fits on a line of its own behind its '=' takes that line whole, unless a comment or a
				// directive cuts it, where it gives at each operand as its other stretches do.
				{
					CStr Head = "\t\t\t\tpWatch->m_ChildFiles[File.m_Path.f_Extract(_Path.f_GetLen() + 1)]";
					CStr Value = "(File.m_Attribs & (NFile::EFileAttrib_Directory | NFile::EFileAttrib_Link)) == NFile::EFileAttrib_Directory";
					fg_ExpectFormat
						(
							"ValueLineBehindAssign"
							, "void f()\n{\n\t{\n\t\t{\n\t\t\t{\n" + Head + " = " + Value + ";\n\t\t\t}\n\t\t}\n\t}\n}\n"
							, "void f()\n{\n\t{\n\t\t{\n\t\t\t{\n" + Head + "\n\t\t\t\t\t= " + Value + "\n\t\t\t\t;\n\t\t\t}\n\t\t}\n\t}\n}\n"
						)
					;
					CStr Cut = "void f()\n{\n\tName\n\t\t= \"{}_{}\"_f\n\t\t<< m_FriendlyName // Never mixed between hosts\n\t\t<< f_GetRealHostID()\n\t;\n"
						"\tSize\n\t\t= sizeof(void *) * 2\n\t\t+ sizeof(uint32)\n#if DDebug\n\t\t+ sizeof(void *)\n#endif\n\t;\n}\n"
					;
					fg_ExpectFormat("ValueLineCut", Cut, Cut);
				}
				// A lambda is a template argument like any other value, its body skipped with the
				// groups inside the list: 'f_Test<[](umint _Len) { ... }>(...)' is no comparison.
				fg_ExpectFormat
					(
						"LambdaTemplateArgument"
						, "void f()\n{\n\tf_Test < [](umint _Len)\n\t\t{\n\t\t\tif (_Len)\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t\t\treturn h(_Len);\n\t\t}\n\t\t> (\"A\", 5)\n\t;\n}\n"
						, "void f()\n{\n\tf_Test\n\t\t<\n\t\t\t[](umint _Len)\n\t\t\t{\n\t\t\t\tif (_Len)\n\t\t\t\t\tg();\n\t\t\t\treturn h(_Len);\n\t\t\t}\n"
						"\t\t>\n\t\t(\"A\", 5)\n\t;\n}\n"
						, false
					)
				;
				// A guarded clause spanning lines with what it guards takes the braces around
				// all of it: a block, a statement and an 'else' branch behind it.
				fg_ExpectFormat
					(
						"GuardedClause"
						, "void f()\n{\n\tfor (;;)\n\t\tif (a)\n\t\t{\n\t\t\tg();\n\t\t\th();\n\t\t}\n\twhile (b)\n\t\tif (c)\n\t\t\tg();\n\t\telse\n\t\t\th();\n}\n"
						, "void f()\n{\n\tfor (;;)\n\t{\n\t\tif (a)\n\t\t{\n\t\t\tg();\n\t\t\th();\n\t\t}\n\t}\n\twhile (b)\n\t{\n\t\tif (c)\n\t\t\tg();\n"
						"\t\telse\n\t\t\th();\n\t}\n}\n"
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
				fKept("Switch", "\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\th();\n\t}\n");
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
				// Any other attribute applies to the statement it stands on, which takes no braces in front of it.
				{
					DMibTestPath("AttributeOfStatement");
					auto Formatted = fg_FormatSource("void f()\n{\n\tif (a)\n\t\t[[clang::musttail]] return g(" + Wide + Wide + ");\n}\n");
					DMibExpect(Formatted.f_Find("[[clang::musttail]] return"), >=, 0);
				}
				// A clause split across lines keeps its braces, as the standard requires.
				fKept("SplitClause", "\tif\n\t(\n\t\t" + Wide + "\n\t\t&& " + Wide + "\n\t)\n\t{\n\t\tg();\n\t}\n");
				// The 'if' of an 'else if' stays behind the 'else' and is split against its line,
				// which counts the 'else' too: this condition fits only without it.
				fg_ExpectFormat
					(
						"ElseIfSplit"
						, "void f()\n{\n\tif (a)\n\t\tg();\n\telse if (" + Wide + Wide + ")\n\t\th();\n}\n"
						, "void f()\n{\n\tif (a)\n\t\tg();\n\telse if\n\t(\n\t\t" + Wide + Wide + "\n\t)\n\t{\n\t\th();\n\t}\n}\n"
						, false
					)
				;
				// No ref-qualifier is followed by a '&', so the '&&' in front of one is an
				// operation like the others and the condition breaks there too.
				fg_ExpectFormat
					(
						"AddressOperand"
						, "void f()\n{\n\tif (a && " + Wide + " && &" + Wide + " == p)\n\t\tg();\n}\n"
						, "void f()\n{\n\tif\n\t(\n\t\ta\n\t\t&& " + Wide + "\n\t\t&& &" + Wide + " == p\n\t)\n\t{\n\t\tg();\n\t}\n}\n"
						, false
					)
				;
				// A line is measured as the spacing rule writes it: this call fits only as
				// written, with no space around its '-'.
				CStr Rest;
				for (umint i = 0; i < 84; ++i)
					Rest += "R";

				fg_ExpectFormat
					(
						"MeasuredRespaced"
						, "void f()\n{\n\tif (a)\n\t\tg(" + Wide + "-1, " + Rest + ");\n}\n"
						, "void f()\n{\n\tif (a)\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t" + Wide + " - 1\n\t\t\t\t, " + Rest + "\n\t\t\t)\n\t\t;\n\t}\n}\n"
						, false
					)
				;
				// A clause written against its parenthesis still opens it on a line of its own.
				fg_ExpectFormat
					(
						"ClauseAgainstParen"
						, "void f()\n{\n\tif(g(" + Wide + ", " + Rest + ") != FALSE)\n\t\th();\n}\n"
						, "void f()\n{\n\tif\n\t(\n\t\tg(" + Wide + ", " + Rest + ")\n\t\t!= FALSE\n\t)\n\t{\n\t\th();\n\t}\n}\n"
						, false
					)
				;
				// The body an 'else' behind a closing brace opens behind a directive stands where the 'else' goes.
				fg_ExpectFormat
					(
						"ElseBodyBehindDirective"
						, "void f()\n{\n#ifndef DA\n\tif (a) {\n\t\tg();\n\t\th();\n\t} else\n#endif\n\t{\n\t\tk();\n\t}\n}\n"
						, "void f()\n{\n#ifndef DA\n\tif (a)\n\t{\n\t\tg();\n\t\th();\n\t}\n\telse\n#endif\n\t{\n\t\tk();\n\t}\n}\n"
					)
				;
				// Braces added under an 'else if' go under the 'else', also where a conditional
				// keeps each line's depth as written.
				fg_ExpectFormat
					(
						"ElseIfBracesInConditional"
						, "#if X\nvoid f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t\tg(" + Wide + ", " + Wide + ");\n}\n#endif\n"
						, "#if X\nvoid f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\t" + Wide + "\n\t\t\t\t, " + Wide + "\n\t\t\t)\n\t\t;\n\t}\n}\n#endif\n"
						, false
					)
				;
			};

			DMibTestCategory("Bodies")
			{
				// A lambda in a braced initializer written across lines still has a head of its
				// own, split where it does not fit on its line as in any statement.
				{
					CStr Params;
					for (umint i = 0; i < 6; ++i)
						Params += "CBuildSystemEvaluationContext const &_Context{}, "_f << i;

					Params += "int _Last";
					fg_ExpectFormat
						(
							"LambdaHeadInInitializer"
							, "CMap g_Map =\n\t{\n\t\tCFoo\n\t\t(\n\t\t\t[](" + Params + ") -> int\n\t\t\t{\n\t\t\t\treturn 1;\n\t\t\t}\n\t\t)\n\t\t, 5\n\t}\n;\n"
							, "CMap g_Map =\n\t{\n\t\tCFoo\n\t\t(\n\t\t\t[]\n\t\t\t(\n\t\t\t\t"
							+ CStr(Params).f_Replace(", ", "\n\t\t\t\t, ")
							+ "\n\t\t\t)\n\t\t\t-> int\n\t\t\t{\n\t\t\t\treturn 1;\n\t\t\t}\n\t\t)\n\t\t, 5\n\t}\n;\n"
						)
					;
					CStr Captures;
					for (umint i = 0; i < 4; ++i)
						Captures += "pStateWithName{} = fg_Move(pState{}), "_f << i << i;

					Captures += "this";
					fg_ExpectFormat
						(
							"CaptureBelowOperatorInInitializer"
							, "void f()\n{\n\tco_return CFoo\n\t\t{\n\t\t\tg_ActorSubscription(Actor) / [" + Captures + "]() mutable\n\t\t\t{\n\t\t\t}\n\t\t\t, 5\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tco_return CFoo\n\t\t{\n\t\t\tg_ActorSubscription(Actor)\n\t\t\t/ ["
							+ Captures
							+ "]() mutable\n"
							"\t\t\t{\n\t\t\t}\n\t\t\t, 5\n\t\t}\n\t;\n}\n"
						)
					;
					fg_ExpectFormat
						(
							"CaptureBelowTrailingOperatorRepaired"
							, "void f()\n{\n\tco_return CFoo\n\t\t{\n\t\t\tg_ActorSubscription(Actor) /\n\t\t\t\t["
							+ Captures
							+ "]() mutable\n"
							"\t\t\t{\n\t\t\t}\n\t\t\t, 5\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tco_return CFoo\n\t\t{\n\t\t\tg_ActorSubscription(Actor)\n\t\t\t/ ["
							+ Captures
							+ "]() mutable\n"
							"\t\t\t{\n\t\t\t}\n\t\t\t, 5\n\t\t}\n\t;\n}\n"
						)
					;
					// In a statement the operator leads the continuation, and the '=' stays on the head.
					fg_ExpectFormat
						(
							"CaptureBelowOperatorInStatement"
							, "void f()\n{\n\tauto X = g_ActorSubscription(Actor) / [" + Captures + "]() mutable\n\t\t{\n\t\t}\n\t;\n}\n"
							, "void f()\n{\n\tauto X = g_ActorSubscription(Actor)\n\t\t/ [" + Captures + "]() mutable\n\t\t{\n\t\t}\n\t;\n}\n"
						)
					;
				}
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

			fg_TestKeptLineBreaks();

			fg_TestSplitLineBreaks();

			DMibTestCategory("Templates")
			{
				// A '<' behind a name the naming lists as a type's or a function's opens a list
				// however it is spaced, so a spaced list, and one an earlier pass spaced as a
				// comparison, both hug again; a spaced '<' behind any other name compares.
				fg_ExpectFormat
					(
						"SpacedAfterTypeOrFunction"
						, "NContainer::TCVector< CStr > g_A;\nTCVector < NContainer::TCVector<int> > g_B;\nauto X = Array.f_Get < CStr > ();\nbool Y = i < n;\nbool Z = j< k;\n"
						, "NContainer::TCVector<CStr> g_A;\nTCVector<NContainer::TCVector<int>> g_B;\nauto X = Array.f_Get<CStr>();\nbool Y = i < n;\nbool Z = j < k;\n"
					)
				;
				// A '<' behind a name only a type can stand at opens a list however it is spaced and qualified: the type a
				// 'typedef', an alias declaration or 'typename' starts with. An assignment's is a comparison.
				fg_ExpectFormat
					(
						"TypeOnlyPositions"
						, "void f()\n{\n\ttypedef a::b::pool\n\t\t< (2*sizeof(void*)+sizeof(int))\n\t\t, A::mc_B> CPool;\n\tusing CAlias = a::b < c, d >;\n"
						"\ttypename T::template X < A > Value;\n\tx = a::b < c;\n}\n"
						, "void f()\n{\n\ttypedef a::b::pool<(2 * sizeof(void *) + sizeof(int)), A::mc_B> CPool;\n\tusing CAlias = a::b<c, d>;\n"
						"\ttypename T::template X<A> Value;\n\tx = a::b < c;\n}\n"
					)
				;
				// A comparison's '<' behind a name keeps its operand on its line: with a line break on both sides it would
				// read as a list's opening.
				{
					CStr Name;
					for (umint i = 0; i < 90; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"ComparisonKeepsOperand"
							, CStr("void f()\n{\n\tg(aName < (b@ + c@), d > e);\n}\n").f_Replace("@", Name)
							, CStr("void f()\n{\n\tg\n\t\t(\n\t\t\taName\n\t\t\t< (\n\t\t\t\tb@\n\t\t\t\t+ c@\n\t\t\t)\n\t\t\t, d > e\n\t\t)\n\t;\n}\n").f_Replace("@", Name)
						)
					;
				}
				// Behind 'operator' a '<' or a '>' names the operator, and opens or closes no list.
				{
					CStr Kept = "struct S\n{\n\tint k1 = T1<0, operator <, operator >, operator < >::val, l1;\n\tbool b = TCFoo<&S::operator >, int>::mc_bValue;\n};\n";
					fg_ExpectFormat("OperatorNames", Kept, Kept);
				}
				// Inside a list a spaced '<' behind a name the list's match counts against a '>' opens a list of its own, also
				// one closing at a '>>' it shares with the list around it. One behind no name opens none.
				fg_ExpectFormat
					(
						"SpacedInsideList"
						, "template <typename A, typename B>\nstruct S <\n\tA, B,\n\tenable_if_t < x<A>::value&&\n\ty<B>::value >>\n{\n\tint a;\n};\n"
						"using CC = TCC<!is_same < A, B > ::value, int>;\nauto x = static_cast< < A > >(b);\n"
						, "template <typename A, typename B>\nstruct S<A, B, enable_if_t<x<A>::value && y<B>::value>>\n{\n\tint a;\n};\n\n"
						"using CC = TCC<!is_same<A, B>::value, int>;\nauto x = static_cast< < A > >(b);\n"
					)
				;

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
						, "void f()\n{\n\tauto R\n\t\t= Left.f_Bind<&C::f_D<TCFuture<uint32>, @>>(_f, Start, Mid)\n"
						"\t\t+ Right.f_Bind<&C::f_D<TCFuture<uint32>, @>>(_f, Mid, End)\n\t;\n}\n"
					)
				;
				// An explicit instantiation moves its return type like any declaration: a
				// '*' behind a template argument list is a declarator, not a multiplication,
				// and spaced closers collapse to one '>>'.
				CStr Pointer = "template TCCounter<TCOnScopeExit<TCFunction<void ()> >, false, 0> *\n"
					"TCConstruct<TCCounter<@>, TCFunction<void ()> >::f_Create<TCCounter<@>, CAllocator &>(CAllocator &);\n"
				;
				// The qualification gives before the name's own argument list does, so the
				// class it names stays whole and '::f_Create' takes the line below it.
				CStr PointerResult = "template auto TCConstruct<TCCounter<@>, TCFunction<void ()>>\n\t::f_Create<TCCounter<@>, CAllocator &>(CAllocator &)\n"
					"\t-> TCCounter<TCOnScopeExit<TCFunction<void ()>>, false, 0> *\n;\n"
				;
				fg_ExpectFormat("ExplicitInstantiation", Pointer.f_Replace("@", Wide), PointerResult.f_Replace("@", Wide), false);
				// Where one qualification is not enough to settle the head, the class's
				// arguments are opened after all, and the qualification follows them.
				CStr Deep = "template auto NMib::TCConstruct<TCCounter<@>, TCFunction<@>>::f_Create<TCCounter<@>, CAllocator &>(CAllocator &) -> TCCounter<@> *;\n";
				CStr DeepResult = "template auto NMib::TCConstruct\n\t<\n\t\tTCCounter<@>\n\t\t, TCFunction<@>\n\t>\n"
					"\t::f_Create<TCCounter<@>, CAllocator &>(CAllocator &)\n\t-> TCCounter<@> *\n;\n"
				;
				fg_ExpectFormat("QualifiedInstantiation", Deep.f_Replace("@", Wide), DeepResult.f_Replace("@", Wide));
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
				// Opening a type's argument list would leave the name it declares on a line
				// of its own anyway, so the type stays whole and the name takes the line
				// below it.
				fSplit("DeclaredName", "struct C\n{\n\tTCMap<CStr, TCFunction<void (@)>> m_Values_@;\n};\n", "struct C\n{\n\tTCMap<CStr, TCFunction<void (@)>>\n\t\tm_Values_@\n\t;\n};\n");
				// A class's own argument list opens at the head's level where the head is too long by
				// itself, since nothing extends the head as an expression; 'final' stays behind its
				// closing marker and the base clause continues the head below it. A head that fits
				// keeps its arguments, and only the base clause moves down.
				fSplit("FinalAfterArguments", "struct TCFoo<@, @, @> final : public CBase\n{\n};\n", "struct TCFoo\n<\n\t@\n\t, @\n\t, @\n> final\n\t: public CBase\n{\n};\n");
				fSplit("BaseClauseMovesDown", "struct TCFoo<@, @> final : public CBase\n{\n};\n", "struct TCFoo<@, @> final\n\t: public CBase\n{\n};\n");
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
				fSplit("LastResort", "void f()\n{\n\ta@.b@.c@.f_Call(1);\n}\n", "void f()\n{\n\ta@\n\t\t.b@\n\t\t.c@\n\t\t.f_Call(1)\n\t;\n}\n");
				fSplit("ScopeFirst", "void f()\n{\n\ta.b.f_Call(@, @, @);\n}\n", "void f()\n{\n\ta.b.f_Call\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t\t, @\n\t\t)\n\t;\n}\n");
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
				// A lambda's capture list stays on the line of the operator that takes it,
				// and the rest of the introducer follows below: the parameter list with the
				// return type behind it, opened where it does not fit on a line of its own.
				CStr Head = "void f()\n{\n\tm_fOnClose = g_ActorFunctorWeak / ";
				CStr Split = "void f()\n{\n\tm_fOnClose = g_ActorFunctorWeak /\n\t\t";
				CStr Body = "\n\t{\n\t\treturn;\n\t}\n\t;\n}\n";
				// The body stands at the level of the line the introducer ends on.
				CStr BodyIn = "\n\t\t{\n\t\t\treturn;\n\t\t}\n\t;\n}\n";
				CStr Captures = "[this, pConnectionWeak, Sequence, _bRetry, _SomeMoreCaptureNames, _AndYetAnotherOne]";
				CStr Params = "(NWeb::EWebSocketStatus _ReasonForTheClosure, NStr::CStr _MessageDescribingTheClose, NWeb::EWebSocketCloseOrigin _OriginOfTheClose)";
				fg_ExpectFormat("Whole", Head + "[this]" + Params + " -> TCFuture<void>" + Body, Head + "[this]\n\t\t" + Params + " -> TCFuture<void>" + BodyIn);
				fg_ExpectFormat("Introducer", Head + Captures + Params + " -> TCFuture<void>" + Body, Head + Captures + "\n\t\t" + Params + " -> TCFuture<void>" + BodyIn);
				CStr Filler;
				for (umint i = 0; i < 80; ++i)
					Filler += "W";

				CStr LongParams = "(NWeb::EWebSocketStatus _ReasonForTheClosure, NStr::CStr _Message@, NWeb::EWebSocketCloseOrigin _Origin@)";
				fg_ExpectFormat
					(
						"OpenedParameters"
						, (Head + "[this]" + LongParams + " -> TCFuture<void>" + Body).f_Replace("@", Filler)
						,
						(
							Head
							+ "[this]\n\t\t(\n\t\t\tNWeb::EWebSocketStatus _ReasonForTheClosure\n\t\t\t, NStr::CStr _Message@\n\t\t\t, NWeb::EWebSocketCloseOrigin _Origin@\n\t\t)"
							"\n\t\t-> TCFuture<void>"
							+ BodyIn
						)
						.f_Replace("@", Filler)
					)
				;
				// Only a capture list too long for the operator's line goes below it, opened.
				CStr LongCaptures = "[this, pConnectionWeak, Sequence, _bRetry, pThis@, _Last@]";
				fg_ExpectFormat
					(
						"OpenedCaptures"
						, (Head + LongCaptures + "() -> TCFuture<void>" + Body).f_Replace("@", Filler)
						,
						(
							Split
							+ "[\n\t\t\tthis\n\t\t\t, pConnectionWeak\n\t\t\t, Sequence\n\t\t\t, _bRetry\n\t\t\t, pThis@\n\t\t\t, _Last@\n\t\t]"
							"\n\t\t() -> TCFuture<void>"
							+ BodyIn
						)
						.f_Replace("@", Filler)
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
				CStr MacroResult
					= "void f()\n{\n\tg\n\t\t(\n\t\t\t[\n\t\t\t\t"
					+ Wide
					+ "\n\t\t\t\t, "
					+ Wide
					+ "\n\t\t\t] mark_no_coroutine_debug\n\t\t\t() mutable\n\t\t\t{\n\t\t\t\th();\n\t\t\t}\n\t\t)\n\t;\n}\n"
				;
				fg_ExpectFormat("CaptureMacro", Macro, MacroResult);

				// An explicit template parameter list is part of the introducer, so all
				// three parts take a line together or none of them does.
				CStr Template = "<typename ...tfp_CParams, typename tf_CActor>";
				fg_ExpectFormat
					(
						"TemplateParameters"
						, Head + Captures + Template + Params + " -> TCFuture<void>" + Body
						, Head + Captures + "\n\t\t" + Template + "\n\t\t" + Params + " -> TCFuture<void>" + BodyIn
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
				fg_ExpectFormat("Guard", "void f()\n{\n#if 1\n\tif (a)\n#else\n\tif (b)\n#endif\n\t\t\tg();\n}\n", "void f()\n{\n#if 1\n\tif (a)\n#else\n\tif (b)\n#endif\n\t\tg();\n}\n");
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
				fMoves("Member", "const int CFoo::*g_pA;\nconst NStr::CStr CFoo::ms_Name;\n", "int const CFoo:: *g_pA;\nNStr::CStr const CFoo::ms_Name;\n");
				// A type defined where it is named, and a name taking an operand, keep the qualifier in front of them.
				fMoves
					(
						"DefinedOrOperand"
						, "const struct CS\n{\n\tint m_X;\n} g_Value = {1};\nconst enum class EKind\n{\n\tmc_A\n} g_Kind;\nconst _Atomic(int) g_Atomic;\nconst __typeof__(g_Value) g_Copy;\n"
						, "const struct CS\n{\n\tint m_X;\n} g_Value = {1};\nconst enum class EKind\n{\n\tmc_A\n} g_Kind;\nconst _Atomic(int) g_Atomic;\nconst __typeof__(g_Value) g_Copy;\n"
					)
				;
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
				CStr Kept = "struct C\n{\n\tCFoo const m_A;\n\tint * const m_pB = nullptr;\n\tCFoo const &f_C() const override;\n"
					"\tvoid f_D() const requires cFoo<C>;\n\tconst /* why */ int m_E;\n\tDMacro(const, x);\n};\n"
				;
				fg_ExpectFormat("Kept", Kept, Kept);
				// So does one leading a type whose words go on behind specifiers, which the next pass would move it behind.
				CStr KeptInterleaved = "struct C\n{\n\tconst long static int constexpr unsigned f_A();\n};\n";
				fg_ExpectFormat("KeptInterleaved", KeptInterleaved, KeptInterleaved);
				// A macro between the type and its declarator would read as a type the moved
				// qualifier leads in turn, and the next pass would move it again.
				CStr Macro = "DExtern const CFoo DFar *fg_F();\nstatic const CFoo DFar *gs_pA;\n";
				fg_ExpectFormat("Macro", Macro, Macro);
				// So does a type that is a macro, which may expand to more than a name: 'DPtr' as 'int *'.
				fg_ExpectFormat("MacroType", "const DPtr g_pA = nullptr;\nconst N::DPtr g_pB = nullptr;\n", "const DPtr g_pA = nullptr;\nconst N::DPtr g_pB = nullptr;\n");
				// A disabled region keeps the qualifier where it is.
				fMoves
					(
						"Disabled"
						, "// malterlib-format off\nconst int g_A = 0;\n// malterlib-format on\nconst int g_B = 0;\n"
						, "// malterlib-format off\nconst int g_A = 0;\n// malterlib-format on\nint const g_B = 0;\n"
					)
				;
			};

			DMibTestCategory("EmptyStatement")
			{
				// A terminator that ends nothing goes, in a class body and in a block alike.
				// A separator of 'for (;;)' and a guarded empty statement stay.
				fg_ExpectFormat
					(
						"Removed"
						, "struct C\n{\n\tusing CA = int;;\n};;\n\nvoid f()\n{\n\tg();;\n\tx = 1;\n\t;\n}\n"
						, "struct C\n{\n\tusing CA = int;\n};\n\nvoid f()\n{\n\tg();\n\tx = 1;\n}\n"
						, false
					)
				;
				// The terminator goes before the braces are decided, so a body left with one
				// statement stands without them in the same pass.
				fg_ExpectFormat("BeforeBraces", "void f()\n{\n\tfor (int i = 0; i < n; ++i)\n\t{\n\t\tg(i);;\n\t}\n}\n", "void f()\n{\n\tfor (int i = 0; i < n; ++i)\n\t\tg(i);\n}\n", false);
				CStr Kept = "void f()\n{\n\tfor (;;)\n\t\th();\n\tif (a)\n\t\t;\n}\n";
				fg_ExpectFormat("Kept", Kept, Kept);
				// One behind a comment goes too and the comments stay, since the layout can move a comment
				// from between the two. A block comment in front of it on its line keeps it, since the
				// comment would come to end its line.
				fg_ExpectFormat
					(
						"AcrossComment"
						, "void f()\n{\n\ty = 2; // Why\n\t;\n\tz = 3; /* A */ ;\n\tw = 4; // B\n\t; // C\n}\n"
						, "void f()\n{\n\ty = 2; // Why\n\tz = 3; /* A */ ;\n\tw = 4; // B\n\t// C\n}\n"
						, false
					)
				;
				// Several behind a comment go together, also one that shares a line with another.
				fg_ExpectFormat("SeveralAcrossComment", "void f()\n{\n\ta; // x\n\t;\n\t;\n\tb; // y\n\t; ;\n}\n", "void f()\n{\n\ta; // x\n\tb; // y\n}\n", false);
				// A comment behind one starting its line keeps the line, also one too long to trail the code in front.
				CStr Long = "// ";
				for (umint i = 0; i < 190; ++i)
					Long += "w";

				fg_ExpectFormat("CommentBehind", "void f()\n{\n\ta;\n\t; // A\n\tb;\n\t; " + Long + "\n}\n", "void f()\n{\n\ta;\n\t// A\n\tb;\n\t" + Long + "\n}\n", false);
				fg_ExpectFormat("AcrossMovedComment", "struct C\n{\n\tvoid f() { };; // A\n\t; // B\n};\n", "struct C\n{\n\tvoid f()\n\t{\n\t}; // A\n\t// B\n};\n", false);
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
				// A qualifier between the two moves behind the type at once.
				fg_ExpectFormat("BehindQualifier", "constexpr const static umint gc_A = 1;\n", "static constexpr umint const gc_A = 1;\n", false);
				// The order the sources mostly have, a 'constexpr' with no 'static' behind it,
				// a comment between the two, and a disabled region stay as they are.
				CStr Kept = "struct C\n{\n\tstatic constexpr umint mc_A = 1;\n\tconstexpr umint f_B() const;\n\tconstexpr /* c */ static umint mc_C = 1;\n};\n\n"
					"// malterlib-format off\nconstexpr static int g_Off = 0;\n// malterlib-format on\n"
				;
				fg_ExpectFormat("Kept", Kept, Kept);
			};

			DMibTestCategory("TrailingReturn")
			{
				// The trailing return type goes in front of a function try block, and a declaration of several declarators,
				// which share the return type, is not converted.
				{
					CStr Name;
					for (umint i = 0; i < 160; ++i)
						Name += "W";

					DMibTestPath("FunctionTryBlock");
					CStr Source = CStr("unsigned long long f@(int _Value) try\n{\n\treturn _Value;\n}\ncatch (...)\n{\n\treturn 0;\n}\n").f_Replace("@", Name);
					auto Formatted = fg_FormatSource(Source);
					DMibExpect(Formatted, ==, CStr("auto f@(int _Value)\n\t-> unsigned long long try\n{\n\treturn _Value;\n}\ncatch (...)\n{\n\treturn 0;\n}\n").f_Replace("@", Name));
				}
				// The handlers of a function-try-block are laid out as its body is, each on lines of its own.
				fg_ExpectFormat
					(
						"FunctionTryBlockHandlers"
						, "struct C\n{\n\tC() try : m_A(1) { f(); } catch (int) { g(); } catch (...) { h(); }\n\tint m_A;\n};\nint f(int a) try { return a; } catch (...) { return 1; }\n"
						, "struct C\n{\n\tC() try\n\t\t: m_A(1)\n\t{\n\t\tf();\n\t}\n\tcatch (int)\n\t{\n\t\tg();\n\t}\n\tcatch (...)\n\t{\n\t\th();\n\t}\n\n\tint m_A;\n};\n\n"
						"int f(int a) try\n{\n\treturn a;\n}\ncatch (...)\n{\n\treturn 1;\n}\n"
					)
				;
				{
					CStr Name;
					for (umint i = 0; i < 160; ++i)
						Name += "W";

					DMibTestPath("SeveralDeclarators");
					{
						// A GNU attribute behind the parameter list has no place beside a trailing return type.
						DMibTestPath("GnuAttribute");
						CStr Attributed = CStr("unsigned long long f@(int _Value) __attribute__((noinline));\n").f_Replace("@", Name);
						auto Formatted = fg_FormatSource(Attributed);
						DMibExpect(Formatted.f_Find("->"), <, 0);
					}

					CStr Source = CStr("unsigned long long f@(int _Value), g(void);\n").f_Replace("@", Name);
					auto Formatted = fg_FormatSource(Source);
					DMibExpect(Formatted.f_Find("->"), <, 0);
					DMibExpectTrue(fg_HasEquivalentCodeTokens(Source, Formatted));
				}
				// A parameter named as a name of the return type would hide it behind the parameter list, and a group behind the
				// parameter list, a lambda in a 'noexcept(...)', holds no place for the trailing type. A parenthesis nothing proves
				// holds parameters may construct a variable.
				{
					CStr Name;
					for (umint i = 0; i < 160; ++i)
						Name += "W";

					DMibTestPath("ParameterHidesType");
					CStr Source = CStr
						(
							"CLongResultType f@(int CLongResultType);\nCLongResultType g@(int (*CLongResultType)(int), int _Value);\n"
							"CLongResultType h@(int (* const CLongResultType)(int));\nCLongResultType i@(int (CLongResultType));\n"
							"CLongResultType Value@((42));\nCLongResultType j@(int (&&CLongResultType));\n"
							"CLongResultType k@(int ((CLongResultType[1])));\nCLongResultType m@(g_Value);\nCLongResultType n@(a * b);\n"
							"CLongResultType o@(new CObject());\nlong long p@(int{42});\nlong long q@(int(42));\n"
							"CLongResultType r@(int _A) asm(\"r\");\n__stdcall CLongResultType s@(int _A);\nCLongResultType WINAPI t@(int _A);\n"
							"template <class... Ts> CLongResultType u@(Ts (&...CLongResultType)[1]);\nCLongResultType v@(int CLongResultType __attribute__((unused)));\n"
							"CLongResultType w@(int _A) DFinal;\n__attribute__((vector_size(16))) unsigned long long y@(int _A);\n"
						)
						.f_Replace("@", Name)
					;
					auto Formatted = fg_FormatSource(Source);
					DMibExpect(Formatted.f_Find("->"), <, 0);
				}
				{
					CStr Name;
					for (umint i = 0; i < 160; ++i)
						Name += "W";

					DMibTestPath("ExportAndInlineStayInFront");
					for (CStr Specifier : {"export", "__inline__", "__forceinline"})
					{
						DMibTestPath(Specifier);
						auto Formatted = fg_FormatSource(CStr(Specifier + " CLongResultType f@(int _A);\n").f_Replace("@", Name));
						DMibExpect(Formatted.f_Find(Specifier + " auto f"), ==, 0);
						DMibExpect(Formatted.f_Find("-> CLongResultType"), >=, 0);
					}
				}
				{
					CStr Name;
					for (umint i = 0; i < 160; ++i)
						Name += "W";

					DMibTestPath("LambdaInNoexcept");
					CStr Source = CStr("CLongResultType f@() noexcept([] { return true; }());\n").f_Replace("@", Name);
					auto Formatted = fg_FormatSource(Source);
					DMibExpect(Formatted, ==, CStr("CLongResultType f@()\n\tnoexcept\n\t(\n\t\t[]\n\t\t{\n\t\t\treturn true;\n\t\t}()\n\t)\n;\n").f_Replace("@", Name));
				}
				// A name in front of a 'typename' or a class key is a specifier, or a macro expanding to one, which a
				// return type moved behind the parameter list would take along.
				{
					CStr Name;
					for (umint i = 0; i < 80; ++i)
						Name += "W";

					DMibTestPath("SpecifierInFront");
					CStr Source = CStr("template <class T>\nBOOST_X typename N::TCResult<T@, T@>::CType f(T &_Value)\n{\n\treturn _Value;\n}\n").f_Replace("@", Name);
					auto Formatted = fg_FormatSource(Source);
					DMibExpect(Formatted.f_Find("->"), <, 0);
					DMibExpect(Formatted.f_Find("BOOST_X typename"), >=, 0);
					DMibExpectTrue(fg_HasEquivalentCodeTokens(Source, Formatted));
				}
				// A head a conditional cuts declares one function per branch, and its return type stays in front of them.
				{
					DMibTestPath("AcrossConditional");
					CStr Name;
					for (umint i = 0; i < 80; ++i)
						Name += "W";

					CStr Source = "struct C\n{\n\ttemplate <typename T>\n\tfriend TCGroup<typename TCResult<T@>::CType, typename TCOther<T@>::CType>\n#ifdef A\n"
						"\tf(TCGroup &&_Left, TCGroup<T> &&_Other)\n#else\n\tf(TCGroup &_Left, TCGroup<T> &_Other)\n#endif\n\t{\n\t\treturn g();\n\t}\n};\n"
					;
					auto Formatted = fg_FormatSource(Source.f_Replace("@", Name));
					DMibExpect(Formatted.f_Find("->"), <, 0);
					DMibExpect(Formatted.f_Find("#endif"), >=, 0);
					DMibExpectTrue(fg_HasEquivalentCodeTokens(Source.f_Replace("@", Name), Formatted));
				}
				// A parameter's default argument is given to it as a statement's value is: the
				// '=' and the value take the line below, and the type stays whole with the name.
				{
					CStr Type = "NFunction::TCFunction<void (NFunction::TCFunction<void ()> const &_Functor)>";
					fg_ExpectFormat
						(
							"DefaultArgumentBelow"
							, "struct C\n{\n\tstatic C fs_Launch\n\t\t(\n\t\t\tint _Operation\n\t\t\t, " + Type + " const &_fDispatcherWithALongName = " + Type + "()\n\t\t)\n\t;\n};\n"
							, "struct C\n{\n\tstatic C fs_Launch\n\t\t(\n\t\t\tint _Operation\n\t\t\t, "
							+ Type
							+ " const &_fDispatcherWithALongName\n\t\t\t= "
							+ Type
							+ "()\n\t\t)\n\t;\n};\n"
						)
					;
				}
				{
					CStr Params;
					for (umint i = 0; i < 5; ++i)
						Params += "NMib::CLongParameterTypeName{} _Param{}, "_f << i << i;

					Params += "int _Last";
					CStr Split = CStr(Params).f_Replace(", ", "\n\t\t, ");
					// The parenthesis of 'operator new' is its parameter list, not placement arguments.
					fg_ExpectFormat("OperatorNewParameters", "void *operator new (" + Params + ")\n{\n}\n", "void *operator new\n\t(\n\t\t" + Split + "\n\t)\n{\n}\n");
					// The declarations in a linkage specification's braces are laid out as any others.
					fg_ExpectFormat
						(
							"LinkageSpecification"
							, "extern \"C\"\n{\n\tvoid fg_Function(" + Params + ")\n\t{\n\t}\n}\n"
							, "extern \"C\"\n{\n\tvoid fg_Function\n\t\t(\n\t\t\t" + CStr(Params).f_Replace(", ", "\n\t\t\t, ") + "\n\t\t)\n\t{\n\t}\n}\n"
						)
					;
					// A requires clause on a line of its own behind the declaration keeps that line,
					// and the declaration is laid out as one that ends there.
					fg_ExpectFormat
						(
							"TrailingRequiresClause"
							, "template <typename t_C>\nvoid fg_Function(" + Params + ")\n\trequires (cFoo<t_C>)\n{\n}\n"
							, "template <typename t_C>\nvoid fg_Function\n\t(\n\t\t" + Split + "\n\t)\n\trequires (cFoo<t_C>)\n{\n}\n"
						)
					;
					// A requires expression's body stands under the line its 'requires' is on, like a
					// lambda's, apart from the function body a requires clause constrains.
					fg_ExpectFormat
						(
							"RequiresExpressionBody"
							, "template <typename t_C>\nconcept cFoo =\n\trequires ()\n{\n\tt_C::f_Get();\n}\n;\n"
							"template <typename t_C>\nvoid fg_Bar(t_C _Value)\n\trequires requires ()\n{\n\t_Value.f_Get();\n}\n{\n}\n"
							, "template <typename t_C>\nconcept cFoo = requires ()\n\t{\n\t\tt_C::f_Get();\n\t}\n;\n"
							"template <typename t_C>\nvoid fg_Bar(t_C _Value)\n\trequires requires ()\n\t{\n\t\t_Value.f_Get();\n\t}\n{\n}\n"
						)
					;
				}
				// An operator named by its symbol has its return type moved like any other name,
				// and the attribute macros in front of it are specifiers that stay in front.
				{
					CStr Name;
					for (umint i = 0; i < 40; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"OperatorTrailingReturn"
							, CStr
							(
								"mark_artificial mark_nodebug inline_always NFunction::TCBoundFunctor@<t_CMemberPtr, t_CType *> "
								"TCReference@<t_CType>::operator ->* (t_CMemberPtr const &_MemberPtr) const\n{\n}\n"
							)
							.f_Replace("@", Name)
							, CStr
							(
								"mark_artificial mark_nodebug inline_always auto TCReference@<t_CType>::operator ->* (t_CMemberPtr const &_MemberPtr) const\n"
								"\t-> NFunction::TCBoundFunctor@<t_CMemberPtr, t_CType *>\n{\n}\n"
							)
							.f_Replace("@", Name)
							, false
						)
					;
				}
				// Only a return type moves. A macro in front of the name can expand to specifiers,
				// which are no type behind the parameter list.
				{
					CStr Name;
					for (umint i = 0; i < 60; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"MacroKeepsPlace"
							, CStr("DMibConstexpr int TCFoo<t_CA@, t_CB@>::CInner::f_Get(TCFoo const &_Source)\n{\n}\n").f_Replace("@", Name)
							, CStr("DMibConstexpr int TCFoo<t_CA@, t_CB@>::CInner::f_Get\n\t(\n\t\tTCFoo const &_Source\n\t)\n{\n}\n").f_Replace("@", Name)
						)
					;
				}
				// A variable constructed with arguments has a declaration's shape, and is told
				// by what its parenthesis holds: a call to a function is no parameter.
				fg_ExpectFormat
					(
						"ConstructedVariable"
						, "void f()\n{\n\tTCSet<int, CCompare, CDiffAllocator> Testing(CAllocatorConstructTag(), CCompareConstructTag(), "
						"fg_Construct(&MemoryManagerWithAMuchLongerName), fg_Construct(CCompare{.m_bReverse = true}));\n}\n"
						, "void f()\n{\n\tTCSet<int, CCompare, CDiffAllocator> Testing\n\t\t(\n\t\t\tCAllocatorConstructTag()\n\t\t\t, CCompareConstructTag()\n"
						"\t\t\t, fg_Construct(&MemoryManagerWithAMuchLongerName)\n\t\t\t, fg_Construct(CCompare{.m_bReverse = true})\n\t\t)\n\t;\n}\n"
					)
				;
				// A requires clause behind a header keeps its line, and so does a member template's
				// header behind the clause; the declaration behind them is laid out as usual.
				fg_ExpectFormat
					(
						"RequiresClauseHeader"
						, "template <typename t_C>\n\trequires cFoo<t_C>\ntemplate <typename tf_C>\nauto TCFoo<t_C>::f_Get(tf_C _A)\n\tconst -> int\n{\n\treturn 0;\n}\n"
						, "template <typename t_C>\n\trequires cFoo<t_C>\ntemplate <typename tf_C>\nauto TCFoo<t_C>::f_Get(tf_C _A) const -> int\n{\n\treturn 0;\n}\n"
					)
				;
				// A template header too long for its line opens its parameter list, with
				// 'template' alone and the markers at the header's level.
				{
					CStr Name;
					for (umint i = 0; i < 60; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"LongHeader"
							, CStr("template <typename t_CFirst@, typename t_CSecond@, template <umint t_n> class t_TCThird@>\nstruct TCFoo;\n").f_Replace("@", Name)
							, CStr("template\n<\n\ttypename t_CFirst@\n\t, typename t_CSecond@\n\t, template <umint t_n> class t_TCThird@\n>\nstruct TCFoo;\n").f_Replace("@", Name)
						)
					;
				}
				// The specifiers in front of the return type stay in front of 'auto' and take
				// their columns on its line, so they count when the converted signature is
				// measured: without them this one looks as if it would fit on one line.
				{
					CStr Name;
					for (umint i = 0; i < 130; ++i)
						Name += "W";

					fg_ExpectFormat
						(
							"SpecifiersCounted"
							, CStr("template <typename t_C>\ninline_small void TCFoo<t_C>::f_@(t_C &&_Functor, umint _Start) const\n{\n}\n").f_Replace("@", Name)
							, CStr("template <typename t_C>\ninline_small void TCFoo<t_C>::f_@\n\t(\n\t\tt_C &&_Functor\n\t\t, umint _Start\n\t)\n\tconst\n{\n}\n").f_Replace("@", Name)
						)
					;
				}

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
				fSplit("VirtSpecifier", "struct C\n{\n\tTCLongTemplate<@> f_F(int _A) override;\n};\n", "struct C\n{\n\tauto f_F(int _A)\n\t\t-> TCLongTemplate<@> override\n\t;\n};\n");
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
					// A fundamental type's keyword written tight against its parenthesis is the function type it is
					// written apart as, and a declarator behind that stands apart too.
					fg_ExpectFormat("FundamentalTight", "void f()\n{\n\tg<int(int)  &, int(int)  &>();\n}\n", "void f()\n{\n\tg<int (int) &, int (int) &>();\n}\n");
				};
			};
		};
	}
}
