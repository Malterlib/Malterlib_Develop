// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Test_Malterlib_Develop_CodeFormatting.h"

namespace NMib::NDevelop::NCodeFormattingTest
{
	void fg_TestSplitLineBreaks()
	{
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
			// An actor call's arguments stand behind the parenthesis that names the
			// function, and are what moves down: the two lists are one call expression,
			// so the line gives between them only when nothing later can give.
			fSplit("ActorCall", "void f()\n{\n\tActor.f_CallActor(&C::f_Fn)(@, @);\n}\n", "void f()\n{\n\tActor.f_CallActor(&C::f_Fn)\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t;\n}\n");
			// A value broken at its operators starts a line of its own with the '=', so
			// that every operand stands under the one in front of it. One whose first
			// operand has to be opened, or takes a lambda's body, keeps the '=' on the
			// head, and what follows that body is laid out under its closing brace.
			CStr Name;
			for (umint i = 0; i < 60; ++i)
				Name += "W";

			fg_ExpectFormat
				(
					"Assign"
					, CStr("void f()\n{\n\tauto Value = a_@ + b_@ + c_@ + d_@;\n\tauto Other = fg_Function(a_@, b_@, c_@) + d_@;\n}\n").f_Replace("@", Name)
					, CStr
					(
						"void f()\n{\n\tauto Value\n\t\t= a_@\n\t\t+ b_@\n\t\t+ c_@\n\t\t+ d_@\n\t;\n"
						"\tauto Other = fg_Function\n\t\t(\n\t\t\ta_@\n\t\t\t, b_@\n\t\t\t, c_@\n\t\t)\n\t\t+ d_@\n\t;\n}\n"
					)
					.f_Replace("@", Name)
				)
			;
			fg_ExpectFormat
				(
					"AssignBody"
					, CStr("void f()\n{\n\tauto Test = g_OnScopeExit / []\n\t\t{\n\t\t}\n\t\t% \"a_@\" % \"b_@\" % \"c_@\" % \"d_@\"\n\t;\n}\n").f_Replace("@", Name)
					, CStr("void f()\n{\n\tauto Test = g_OnScopeExit / []\n\t\t{\n\t\t}\n\t\t% \"a_@\"\n\t\t% \"b_@\"\n\t\t% \"c_@\"\n\t\t% \"d_@\"\n\t;\n}\n").f_Replace("@", Name)
				)
			;
			// What stands in front of the '=' is the last thing to give: the value behind it
			// is opened first, and a value with nothing to open takes a line of its own.
			fg_ExpectFormat
				(
					"AssignValueFirst"
					, CStr("void f()\n{\n\tTCPointer<TCData<t_C>> pData = fg_Consume<t_C, a_@ | b_@ | c_Flag>(_A, _B);\n\tauto Value_@_@ = Other_@_@;\n}\n").f_Replace("@", Name)
					, CStr
					(
						"void f()\n{\n\tTCPointer<TCData<t_C>> pData = fg_Consume<t_C, a_@ | b_@ | c_Flag>\n\t\t(\n\t\t\t_A\n\t\t\t, _B\n\t\t)\n\t;\n"
						"\tauto Value_@_@\n\t\t= Other_@_@\n\t;\n}\n"
					)
					.f_Replace("@", Name)
				)
			;
			// Where the type and the name do not fit on one line to begin with, the name goes
			// down with its value and the type stays whole above them, and a type that was
			// opened comes back closed.
			fg_ExpectFormat
				(
					"AssignNameDown"
					, CStr("constinit TCSubSystem\n\t<\n\t\tCSubSystem_@\n\t\t, ESubSystemDestruction_@\n\t>\n\tg_SubSystem_@ = {DAggregateInit}\n;\n").f_Replace("@", Name)
					, CStr("constinit TCSubSystem<CSubSystem_@, ESubSystemDestruction_@>\n\tg_SubSystem_@ = {DAggregateInit}\n;\n").f_Replace("@", Name)
				)
			;
			// Behind a parameter list the '=' makes a declaration pure, and the list is
			// still what it is opened at.
			fg_ExpectFormat
				(
					"AssignPure"
					, CStr("struct C\n{\n\tvirtual TCFuture<TCSubscription<>> f_Register(CFoo_@ _A, CBar_@ _B, CBaz_@ _C) = 0;\n};\n").f_Replace("@", Name)
					, CStr("struct C\n{\n\tvirtual TCFuture<TCSubscription<>> f_Register\n\t\t(\n\t\t\tCFoo_@ _A\n\t\t\t, CBar_@ _B\n\t\t\t, CBaz_@ _C\n\t\t)\n\t\t= 0\n\t;\n};\n")
					.f_Replace("@", Name)
				)
			;
			// A value written broken at its operators by the comments behind them is one too.
			fg_ExpectFormat("AssignComment", "void f()\n{\n\tValue = x // Why\n\t\t+ y\n\t;\n}\n", "void f()\n{\n\tValue\n\t\t= x // Why\n\t\t+ y\n\t;\n}\n");
			// A named cast's parenthesis belongs to the head, as a C cast's does: the line
			// gives at the call behind it, and comes back closed where it was opened.
			fg_ExpectFormat
				(
					"NamedCast"
					, CStr
					(
						"void f()\n{\n\treturn reinterpret_cast<CCall>\n\t\t(\n\t\t\t_Actor\n\t\t)\n"
						"\t\t.template f_Bind<&C::f_A_@, &C::f_B_@>(fg_Forward<tf_F>(_f), a_@);\n}\n"
					)
					.f_Replace("@", Name)
					, CStr
					(
						"void f()\n{\n\treturn reinterpret_cast<CCall>(_Actor).template f_Bind\n\t\t<\n\t\t\t&C::f_A_@\n\t\t\t, &C::f_B_@\n\t\t>\n"
						"\t\t(fg_Forward<tf_F>(_f), a_@)\n\t;\n}\n"
					)
					.f_Replace("@", Name)
				)
			;
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
					, "struct C\n{\n\tvoid f_F\n\t\t(\n\t\t\tint @\n\t\t\t, int @\n\t\t)\n\t\tconst volatile = 0\n\t;\n};\n"
				)
			;
			// An operator function's name is written apart from its parameter list, and
			// its symbol is a name rather than an operation to be split at.
			fSplit("OperatorName", "struct C\n{\n\tvoid operator + (int @, int @) &&;\n};\n", "struct C\n{\n\tvoid operator +\n\t\t(\n\t\t\tint @\n\t\t\t, int @\n\t\t)\n\t\t&&\n\t;\n};\n");
			// Every scope marker of a split statement gets its own line.
			fSplit("Chained", "void f()\n{\n\tg(@, @).f_Call(@, @);\n}\n", "void f()\n{\n\tg(@, @)\n\t\t.f_Call\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t;\n}\n");
			// Opening the first call would put the member access on a line of its own,
			// and that line was all the statement needed, so the call stays whole and
			// the chain gives at every member, as one under a closing marker does,
			// whether or not what is left of it would fit on one line.
			fSplit("ChainedRedundant", "void f()\n{\n\tg(@).f_Call(@).f_Wrap(@);\n}\n", "void f()\n{\n\tg(@)\n\t\t.f_Call(@)\n\t\t.f_Wrap(@)\n\t;\n}\n");
			// A call or a subscript on what the chain yields is a link of it like a member
			// access, and gives with the rest.
			fSplit
				(
					"ChainedYielded"
					, "void f()\n{\n\tActor.f_CallActor(&C::f_Fn_@)[1](a, b).f_Timeout(10.0, \"@\") > Results;\n}\n"
					, "void f()\n{\n\tActor.f_CallActor(&C::f_Fn_@)\n\t\t[1]\n\t\t(a, b)\n\t\t.f_Timeout(10.0, \"@\")\n\t\t> Results\n\t;\n}\n"
				)
			;
			fSplit("ChainedMembers", "void f()\n{\n\tg(@, @).f_Call(a).f_Wrap(b);\n}\n", "void f()\n{\n\tg(@, @)\n\t\t.f_Call(a)\n\t\t.f_Wrap(b)\n\t;\n}\n");
			// The outermost level is split first: an operator chain breaks at its
			// loosest operators, and a call on a line that then fits is left alone.
			fSplit("OperatorChain", "void f()\n{\n\to_Str += g(\"@\") << @ << @;\n}\n", "void f()\n{\n\to_Str\n\t\t+= g(\"@\")\n\t\t<< @\n\t\t<< @\n\t;\n}\n");
			// A conditional's '?' and ':' bind loosest of all, so it gives at them before
			// an operand of it is opened, as a statement's value and as an element.
			fSplit
				(
					"Conditional"
					, "void f()\n{\n\tauto Value = bFlag ? (@).f_Get() : (@).f_Get();\n\tg(a, bFlag ? (@).f_Get() : (@).f_Get());\n}\n"
					, "void f()\n{\n\tauto Value\n\t\t= bFlag\n\t\t? (@).f_Get()\n\t\t: (@).f_Get()\n\t;\n"
					"\tg\n\t\t(\n\t\t\ta\n\t\t\t, bFlag\n\t\t\t? (@).f_Get()\n\t\t\t: (@).f_Get()\n\t\t)\n\t;\n}\n"
				)
			;
			// A unary operator is no place to break: behind an operator, an opener or a keyword
			// it starts its operand, so the '=' keeps it and the call behind it opens.
			fSplit
				(
					"UnaryNotInfix"
					, "void f()\n{\n\tpValue = &Internal.f_Queue(@, @);\n\tStr = CFormat(\"@\") << @ << &Serial;\n}\n"
					, "void f()\n{\n\tpValue = &Internal.f_Queue\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t;\n"
					"\tStr\n\t\t= CFormat(\"@\")\n\t\t<< @\n\t\t<< &Serial\n\t;\n}\n"
				)
			;
			// A declarator in parentheses behind a type stands apart from it, and a type's
			// key in front of an initialized variable defines no body.
			fg_ExpectFormat
				(
					"DeclaratorsAndInitializers"
					, "void fg_F(ch8(&_Dest)[4], int(*_pCall)(int));\nvoid f()\n{\n\tstruct CState State = {0};\n\tstruct CState Broken =\n\t{0\n\t};\n}\n"
					, "void fg_F(ch8 (&_Dest)[4], int (*_pCall)(int));\nvoid f()\n{\n\tstruct CState State = {0};\n\tstruct CState Broken = {0};\n}\n"
				)
			;
			// A constraint in parentheses behind a template header holds an expression, whose
			// '&&' behind a template argument list is no declarator; a requires expression's
			// parentheses declare its parameters.
			CStr Constraint = "template <typename t_C>\nrequires\n(\n\tcA<TCFoo<TCBar<t_C>>>\n\t&& cB\n\t<\n\t\tTCBar<TCFoo<TCBar<t_C>>>\n\t\t, TCFoo<t_C>\n\t>\n)\n"
				"void f(t_C _Value)\n{\n}\n\ntemplate <typename t_C>\nrequires (cA<t_C> && cB<t_C>)\nvoid g()\n{\n}\n\ntemplate <typename t_C>\n"
				"concept cC = requires (t_C &&_Value)\n\t{\n\t\t_Value.f_Get();\n\t}\n;\n"
			;
			fg_ExpectFormat("ConstraintBehindHeader", Constraint, Constraint);
			// Its operators are settled, so one written tight is spaced.
			fg_ExpectFormat
				(
					"ConstraintOperatorsSpaced"
					, "template <typename t_C>\nrequires (cA<TCFoo<t_C>> &&cB<t_C> &&!cC<t_C>)\nvoid f();\n"
					, "template <typename t_C>\nrequires (cA<TCFoo<t_C>> && cB<t_C> && !cC<t_C>)\nvoid f();\n"
				)
			;
			// A fundamental type in front of a parenthesis in a template argument list spells
			// a function type, which a named type there may spell or construct a value with.
			fg_ExpectFormat
				(
					"FundamentalFunctionType"
					, "TCFunction<void()> g_fA;\nTCFunction<int(int)> g_fB;\nTCFunction<CFoo()> g_fC;\n"
					, "TCFunction<void ()> g_fA;\nTCFunction<int (int)> g_fB;\nTCFunction<CFoo()> g_fC;\n"
				)
			;
			// In a parameter list a declarator in parentheses follows a type whatever its name,
			// but not in a default argument, which is an expression.
			fg_ExpectFormat
				(
					"ParameterDeclarators"
					, "template <typename tf_CType, size_t tf_n>\nsize_t fg_A(tf_CType(&_Array)[tf_n], int _B = g(&c)[1]);\n"
					, "template <typename tf_CType, size_t tf_n>\nsize_t fg_A(tf_CType (&_Array)[tf_n], int _B = g(&c)[1]);\n"
				)
			;
			// A braced list never opened takes its closing brace back behind its last element,
			// also inside a list that was opened.
			fg_ExpectFormat
				(
					"StrayClosingBrace"
					, "void f()\n{\n\tstruct timespec Times[2] =\n\t{\n\t\t{1000000000, 0\n\t\t}, {1000000000, 0}\n\t};\n}\n"
					, "void f()\n{\n\tstruct timespec Times[2] =\n\t\t{\n\t\t\t{1000000000, 0}\n\t\t\t, {1000000000, 0}\n\t\t}\n\t;\n}\n"
				)
			;
			// A pack expansion stays behind the closing marker of an opened scope, and an empty
			// call starts the line below it with what follows. An element on one line too long for it is laid out even in a
			// braced list that keeps its lines.
			CStr Long;
			for (umint i = 0; i < 170; ++i)
				Long += "W";

			fg_ExpectFormat
				(
					"EmptyCallAndPackBehindMarker"
					, "void f()\n{\n\tauto X = fs_Calc<"
					+ Long
					+ ">().f_Get();\n}\n\nstruct CA\n{\n\tstatic CIndex constexpr mc_N[1] =\n\t\t{\n\t\t\tCIndex\n\t\t\t(\n\t\t\t\t"
					"fs_Calc<"
					+ Long
					+ ">()\n\t\t\t)...\n\t\t}\n\t;\n};\n"
					, "void f()\n{\n\tauto X = fs_Calc\n\t\t<\n\t\t\t"
					+ Long
					+ "\n\t\t>\n\t\t().f_Get()\n\t;\n}\n\n"
					"struct CA\n{\n\tstatic CIndex constexpr mc_N[1] =\n\t\t{\n\t\t\tCIndex\n\t\t\t(\n\t\t\t\t"
					"fs_Calc\n\t\t\t\t<\n\t\t\t\t\t"
					+ Long
					+ "\n\t\t\t\t>\n\t\t\t\t()\n\t\t\t)...\n\t\t}\n\t;\n};\n"
				)
			;
			// A declaration with a requires clause on a line of its own is laid out in front of
			// it as any other, an initializer list behind the clause and a terminator on a line
			// of their own included, and an operator's brackets are its name, not a scope.
			fg_ExpectFormat
				(
					"RequiresClauseDeclarations"
					, "struct CA\n{\n\tauto f_C(int _A) const\n\t\t-> int\n\t\trequires (cB<int>)\n\t;\n\n\tauto operator ()\n\t\t(int _A) const\n\t\t-> int\n\t;\n};\n"
					, "struct CA\n{\n\tauto f_C(int _A) const -> int\n\t\trequires (cB<int>)\n\t;\n\n\tauto operator () (int _A) const -> int;\n};\n"
				)
			;
			// A macro invoked as a statement of its own ends at its argument list, which is laid
			// out as any other where it does not fit.
			fg_ExpectFormat
				(
					"MacroStatements"
					, "DMibA(int, f_A, int _0)\nDMibB(void, f_B, " + Long + " _0, " + Long + " _1)\n"
					, "DMibA(int, f_A, int _0)\nDMibB\n\t(\n\t\tvoid\n\t\t, f_B\n\t\t, " + Long + " _0\n\t\t, " + Long + " _1\n\t)\n"
				)
			;
			// So does one the naming does not list, where a word on the next line can only start a declaration of
			// its own.
			fg_ExpectFormat
				(
					"UnlistedMacroStatement"
					, "REGISTER_PASS(CA, \"a\")\nnamespace N\n{\nvoid fg_F() {\n}\n}\n"
					, "REGISTER_PASS(CA, \"a\")\nnamespace N\n{\n\tvoid fg_F()\n\t{\n\t}\n}\n"
				)
			;
			// And where a declaration's head follows it behind a blank line, its names on the line the layout writes them on,
			// and where the next line starts further out than the macro, as no line the statement goes on with does.
			fg_ExpectFormat
				(
					"UnlistedMacroBeforeHead"
					, "REGISTER_PASS(pass, \"a\")\n\nresult pass::run\n\t(\n\t\tint _A\n\t\t, int _B\n\t)\n{\n}\n"
					, "REGISTER_PASS(pass, \"a\")\n\nresult pass::run(int _A, int _B)\n{\n}\n"
				)
			;
			fg_ExpectFormat("UnlistedMacroFurtherIn", "    REGISTER_PASS(pass, \"a\")\n\nstruct CB {\n};\n", "REGISTER_PASS(pass, \"a\")\n\nstruct CB\n{\n};\n");
			// A name alone on its line is a macro statement where a declaration follows it, or the file ends.
			fg_ExpectFormat("BareMacroStatements", "\tBEGIN_NAMESPACE\n\tint g_A;\n\tEND_NAMESPACE\n", "BEGIN_NAMESPACE\nint g_A;\nEND_NAMESPACE\n");
			// A statement behind a comment on its line takes the line's indentation from the layout.
			fg_ExpectFormat
				(
					"StatementBehindComment"
					, "int f(){\n  switch( op ){\n    case 2: {\n      if( a )\n        n += x(\n        );\n    }\n      /* c */ ft;\n    case 3:{\n    }\n  }\n}\n"
					, "int f()\n{\n\tswitch (op)\n\t{\n\tcase 2:\n\t\t{\n\t\t\tif (a)\n\t\t\t\tn += x();\n\t\t}\n\t\t/* c */ ft;\n\tcase 3:\n\t\t{\n\t\t}\n\t}\n}\n"
				)
			;
			// So is one in front of a case's label, a template's header or a namespace, wherever these stand.
			fg_ExpectFormat
				(
					"BareMacroBeforeTemplate"
					, "namespace N\n{\nBEGIN_NAMESPACE\n\ttemplate <typename t_C>\n\tvoid fg_A();\n}\n"
					, "namespace N\n{\n\tBEGIN_NAMESPACE\n\ttemplate <typename t_C>\n\tvoid fg_A();\n}\n"
				)
			;
			fg_ExpectFormat
				(
					"BareMacroBeforeCase"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\tFALL_THROUGH\n\tcase 2:\n\t\th();\n\t\tbreak;\n\t}\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\tFALL_THROUGH\n\tcase 2: h(); break;\n\t}\n}\n"
				)
			;
			// A return type alone on its line is part of the declaration below it, which is laid out as one, also with
			// the name the naming lists no role for.
			{
				CStr Parameter;
				for (umint i = 0; i < 90; ++i)
					Parameter += "a";

				fg_ExpectFormat
					(
						"ReturnTypeAboveName"
						, "CResult\nfg_F(int _" + Parameter + ", int _B" + Parameter + ") {\n}\n"
						, "CResult fg_F\n\t(\n\t\tint _" + Parameter + "\n\t\t, int _B" + Parameter + "\n\t)\n{\n}\n"
					)
				;
			}
			// So is one in front of another behind a blank line.
			fg_ExpectFormat("BareMacroStatementsParted", "\tEND_NAMESPACE\n\n\tPOP_MACROS\n", "END_NAMESPACE\n\nPOP_MACROS\n");
			// A run of calls is statements where a blank line ends it, also in front of an attribute, and the head
			// of what follows where a declaration goes on behind it.
			fg_ExpectFormat
				(
					"MacroCallRunStatements"
					, "DBegin(A,\n       B)\nDUse(C)\nDEnd(A,\n     B)\n\n[[nodiscard]] static bool fg_F(int _A) {\n  return _A;\n}\n"
					, "DBegin(A, B)\nDUse(C)\nDEnd(A, B)\n\n[[nodiscard]] static bool fg_F(int _A)\n{\n\treturn _A;\n}\n"
				)
			;
			fg_ExpectFormat("MacroCallRunHead", "GLOBAL(void)\nfg_F(int _A)\n{\n}\n", "GLOBAL(void) fg_F(int _A)\n{\n}\n");
			// What the layout moves behind an opened argument list stands one level in, and stays part of the statement.
			{
				CStr AttributeArgument;
				for (umint i = 0; i < 180; ++i)
					AttributeArgument += "a";

				fg_ExpectFormat
					(
						"UnlistedMacroOpened"
						, "ATTRIBUTE_NEW(\"" + AttributeArgument + "\") void fg_F();\n"
						, "ATTRIBUTE_NEW\n\t(\n\t\t\"" + AttributeArgument + "\"\n\t)\n\tvoid fg_F()\n;\n"
					)
				;
			}
			// A fundamental type in front of a ':' is an unnamed bit-field, also with a named width, and no label.
			fg_ExpectFormat("UnnamedBitFieldNamedWidth", "struct CA\n{\n\tunsigned : NumBits;\n\tint m_A;\n};\n", "struct CA\n{\n\tunsigned : NumBits;\n\tint m_A;\n};\n");
			// A template argument list spaced inside its markers alike is one.
			fg_ExpectFormat("AngleSpacedInside", "using CA = enable_if_t< cA<int> && cB<int> >;\n", "using CA = enable_if_t<cA<int> && cB<int>>;\n");
			// An argument list starting with 'typename' is one however it is spaced, and so is one behind the name a
			// class key declares.
			fg_ExpectFormat("AngleTypename", "using CA = convert< typename CC::CD>;\n", "using CA = convert<typename CC::CD>;\n");
			fg_ExpectFormat("AngleClassKey", "template <>\nstruct convert< int, 0>\n{\n};\n", "template <>\nstruct convert<int, 0>\n{\n};\n");
			// A comma in front of an argument list's closer or a terminator, ending an empty element, takes no space.
			fg_ExpectFormat("CommaBeforeAngleCloser", "using CA = TCB<int, >;\n", "using CA = TCB<int,>;\n");
			fg_ExpectFormat("CommaBeforeTerminator", "void f() DMacro({}, ;)\n", "void f() DMacro({},;)\n");
			// A requires clause goes on across a line ending in a conjunction, and an attribute is never opened.
			{
				CStr Parameter;
				for (umint i = 0; i < 90; ++i)
					Parameter += "a";

				fg_ExpectFormat
					(
						"RequiresAcrossLinesAttribute"
						, "template <typename t_C>\n\trequires (cA<t_C> || cB<t_C>) &&\n\t\tcC<t_C>\n[[nodiscard]] bool fg_F(int _" + Parameter + ", int _B" + Parameter + ");\n"
						, "template <typename t_C>\n\trequires (cA<t_C> || cB<t_C>) &&\n\t\tcC<t_C>\n[[nodiscard]] bool fg_F\n\t(\n\t\tint _"
						+ Parameter
						+ "\n\t\t, int _B"
						+ Parameter
						+ "\n\t)\n;\n"
					)
				;
			}
			// A name qualified from the global scope behind a binary operator stands apart from it.
			fg_ExpectFormat("GlobalBehindOperator", "void f()\n{\n\ta +=::fg_B();\n\tc = d ==::e;\n}\n", "void f()\n{\n\ta += ::fg_B();\n\tc = d == ::e;\n}\n");
			// What 'alignas' and 'decltype' take in parentheses is no cast, and what follows
			// them stands apart.
			fg_ExpectFormat
				(
					"AlignasAndDecltype"
					, "struct CA\n{\n\talignas(CData)uint8 m_Space[sizeof(CData)];\n\tdecltype(CFoo())m_Value;\n\tDMibCache(TCFloat)Cache;\n};\n"
					, "struct CA\n{\n\talignas(CData) uint8 m_Space[sizeof(CData)];\n\tdecltype(CFoo()) m_Value;\n\tDMibCache(TCFloat) Cache;\n};\n"
				)
			;
			// An exception specification too long for the head's line takes a line of its own
			// before the parameter list opens, and a trailing return type behind it the next.
			fg_ExpectFormat
				(
					"NoexceptBelowHead"
					, "struct CA\n{\n\tstatic void fs_Call(t_CThis &&_pThis, t_CVisitor &&_Visitor) noexcept(noexcept(fg_Call("
					+ Long.f_Left(120)
					+ ")))\n\t{\n\t}\n\n"
					"\tstatic auto fs_Get(t_CThis &&_pThis, t_CVisitor &&_Visitor) noexcept(noexcept(fg_Call("
					+ Long.f_Left(120)
					+ "))) -> int\n\t{\n\t\treturn 0;\n\t}\n};\n"
					, "struct CA\n{\n\tstatic void fs_Call(t_CThis &&_pThis, t_CVisitor &&_Visitor)\n\t\tnoexcept(noexcept(fg_Call("
					+ Long.f_Left(120)
					+ ")))\n\t{\n\t}\n\n"
					"\tstatic auto fs_Get(t_CThis &&_pThis, t_CVisitor &&_Visitor)\n\t\tnoexcept(noexcept(fg_Call("
					+ Long.f_Left(120)
					+ ")))\n\t\t-> int\n"
					"\t{\n\t\treturn 0;\n\t}\n};\n"
				)
			;
			// A class's head with an alignment in front of its name has a base clause, not an
			// initializer list.
			CStr AlignedClass = "struct alignas(32) CA : public CB, CC\n{\n\tint m_A = 3;\n};\n";
			fg_ExpectFormat("AlignedClassHead", AlignedClass, AlignedClass);
			CStr MacroCase = "void f()\n{\n\tswitch (a)\n\t{\n\tcase DMibFoo(R32): break;\n\tcase DMibFoo(R8):\n\tcase DMibFoo(B8):\n\t\tbreak;\n\t}\n}\n";
			fg_ExpectFormat("MacroCaseLabel", MacroCase, MacroCase);
			// A case that falls through to the next stays under its label, and so does a body
			// that labels fall through to.
			fg_ExpectFormat
				(
					"FallThroughBody"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: ++p;\n\tcase 2:\n\t\tg();\n\t\tbreak;\n\tcase 3:\n\t\th();\n\tcase 5: DMibError(\"x\");\n"
					"\tcase 4: i();\n\t}\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\t++p;\n\tcase 2: g(); break;\n\tcase 3:\n\t\th();\n\tcase 5: DMibError(\"x\");\n"
					"\tcase 4: i();\n\t}\n}\n"
				)
			;
			// Two names on one line are one space apart however the source aligned them, and a
			// region the formatter is turned off for keeps its alignment.
			fg_ExpectFormat
				(
					"AlignedNames"
					, "void f()\n{\n\tBOOL       bA;   /* 000 */\n\tDWORD      dwB = 0; // B\n\tg(/* a */ 1);\n"
					"\t// malterlib-format off\n\tFCGI_PARAMS      x      x\n\t// malterlib-format on\n}\n"
					, "void f()\n{\n\tBOOL bA; // 000\n\tDWORD dwB = 0; // B\n\tg(/* a */ 1);\n"
					"\t// malterlib-format off\n\tFCGI_PARAMS      x      x\n\t// malterlib-format on\n}\n"
					, false
				)
			;
			// An operator's declaration too long for its line gives at the '::' behind its
			// class's template arguments rather than opening them.
			CStr OperatorClass = "TCFloat<t_CFormatter, t_SignBits, t_ExponentBits, t_MantissaBits, t_PaddingBits, t_bDummyOptimize, t_CIntegerStorage, "
				"t_bReference, t_CImplicitFloat>"
			;
			fg_ExpectFormat
				(
					"OperatorGivesAtScope"
					, "namespace NA\n{\n\ttemplate <typename t_C>\n\tauto " + OperatorClass + "::operator + (TCFloat const &_Value) const -> TCFloat\n\t{\n\t}\n}\n"
					, "namespace NA\n{\n\ttemplate <typename t_C>\n\tauto " + OperatorClass + "\n\t\t::operator + (TCFloat const &_Value) const\n\t\t-> TCFloat\n\t{\n\t}\n}\n"
				)
			;
			// A value assigned to what a member call yields goes behind its '=' whole, the call in front
			// of it closed: only '0', 'default' and 'delete' behind a parameter list end a declaration.
			{
				CStr Value = "fp_ConvertValue(*pWildcard, InputMember.f_Value(), fg_Format(\"{}.{}\", _Identifier, InputMember.f_Name()), true, _AnsiFlags, _ExtraFlags, _MoreFlags)";
				fg_ExpectFormat
					(
						"AssignToMemberCall"
						, "void f()\n{\n\tOutputObject.f_CreateMember(InputMember.f_Name()) = " + Value + ";\n}\n"
						, "void f()\n{\n\tOutputObject.f_CreateMember(InputMember.f_Name())\n\t\t= " + Value + "\n\t;\n}\n"
					)
				;
			}
			// With nothing behind its parameter list to give first, the list opens behind the whole name.
			fg_ExpectFormat
				(
					"OperatorOpensBehindNameEmptyBody"
					, "namespace NA\n{\n\tvoid "
					+ OperatorClass
					+ "::operator > (TCFutureMapBoundKey<tf_CKey, t_CReturn, tf_CForwardKey> &&_BoundKey) &&\n"
					"\t{\n\t}\n}\n"
					, "namespace NA\n{\n\tvoid "
					+ OperatorClass
					+ "::operator >\n\t\t(\n"
					"\t\t\tTCFutureMapBoundKey<tf_CKey, t_CReturn, tf_CForwardKey> &&_BoundKey\n\t\t)\n\t\t&&\n\t{\n\t}\n}\n"
				)
			;
			fg_ExpectFormat
				(
					"OperatorOpensBehindName"
					, "namespace NA\n{\n\ttemplate <typename t_C>\n\tvoid "
					+ OperatorClass
					+ "::operator > (TCFutureMapBoundKey<tf_CKey, t_CReturn, tf_CForwardKey> &&_BoundKey)"
					" &&\n\t{\n\t\tg();\n\t}\n}\n"
					, "namespace NA\n{\n\ttemplate <typename t_C>\n\tvoid "
					+ OperatorClass
					+ "::operator >\n\t\t(\n"
					"\t\t\tTCFutureMapBoundKey<tf_CKey, t_CReturn, tf_CForwardKey> &&_BoundKey\n\t\t)\n\t\t&&\n\t{\n\t\tg();\n\t}\n}\n"
				)
			;
			// One that fits in front of its exception specification gives there instead.
			fg_ExpectFormat
				(
					"OperatorGivesAtSpecification"
					, "namespace NA\n{\n\tauto TCOptional<t_CType, t_MoveSemantics>::operator == (TCOptional<tf_CType, tf_MoveSemantics> const &_Right) const "
					"noexcept(noexcept(fg_GetType<t_CType const &>() == fg_GetType<tf_CType const &>())) -> bool\n\t{\n\t}\n}\n"
					, "namespace NA\n{\n\tauto TCOptional<t_CType, t_MoveSemantics>::operator == (TCOptional<tf_CType, tf_MoveSemantics> const &_Right) const\n"
					"\t\tnoexcept(noexcept(fg_GetType<t_CType const &>() == fg_GetType<tf_CType const &>()))\n\t\t-> bool\n\t{\n\t}\n}\n"
				)
			;
			// The '=' of 'operator =' names the function and gives no value.
			fg_ExpectFormat
				(
					"AssignOperatorGivesAtScope"
					, "namespace NA\n{\n\tconstexpr auto " + OperatorClass + "::operator = (t_CImplicitFloat const &_Value) noexcept -> TCFloat &\n\t{\n\t}\n}\n"
					, "namespace NA\n{\n\tconstexpr auto " + OperatorClass + "\n\t\t::operator = (t_CImplicitFloat const &_Value) noexcept\n\t\t-> TCFloat &\n\t{\n\t}\n}\n"
				)
			;
			// So does one whose rest only fits once its exception specification takes a line of its own.
			fg_ExpectFormat
				(
					"OperatorGivesAtScopeAndSpecification"
					, "namespace NA\n{\n\tbool " + OperatorClass + "::operator == (" + OperatorClass + " &_R) const noexcept\n\t{\n\t}\n}\n"
					, "namespace NA\n{\n\tbool " + OperatorClass + "\n\t\t::operator == (" + OperatorClass + " &_R) const\n\t\tnoexcept\n\t{\n\t}\n}\n"
				)
			;
			// A class's head cut by a conditional keeps its lines at the class's level.
			fg_ExpectFormat("ClassHeadAcrossConditional", "class\n#ifdef DFoo\n\talignas(16)\n#endif\n\tCFoo\n{\n};\n", "class\n#ifdef DFoo\nalignas(16)\n#endif\nCFoo\n{\n};\n");
			// A statement that fits only without its terminator is split, also where the scope
			// it opens is the call behind a member access, so the terminator never takes a line
			// of its own behind a line that was not split.
			CStr Filler;
			for (umint i = 0; i < 112; ++i)
				Filler += "W";

			fg_ExpectFormat
				(
					"FitsOnlyWithoutTerminator"
					, "void f()\n{\n\tco_return co_await mp_A(a, b).f_Timeout(mp_Timeout, \"" + Filler + " {}\"_f << _Namespace);\n}\n"
					, "void f()\n{\n\tco_return co_await mp_A(a, b).f_Timeout\n\t\t(\n\t\t\tmp_Timeout\n\t\t\t, \"" + Filler + " {}\"_f << _Namespace\n\t\t)\n\t;\n}\n"
				)
			;
			// An operator the layout moves off a continued string's line is placed by that move, the string staying.
			fg_ExpectFormat
				(
					"OperatorBehindContinuedKeyString"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\t{\n\t\t\t\t\"A\"_o= \"a\\n\"\n\t\t\t\t\"b\\n\" +\n\t\t\t\tCFoo\n\t\t\t\t{\n\t\t\t\t\t1\n"
					"\t\t\t\t}.f_ToString()\n\t\t\t}\n\t\t)\n\t;\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\t{\n\t\t\t\t\"A\"_o= \"a\\n\"\n\t\t\t\t\"b\\n\"\n\t\t\t\t+ CFoo\n\t\t\t\t{\n\t\t\t\t\t1\n"
					"\t\t\t\t}\n\t\t\t\t.f_ToString()\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			// A string continued as a value behind a DSL key stands at the key's level, with the
			// operators continuing it, as they do behind a single string.
			{
				CStr Warning;
				for (umint i = 0; i < 170; ++i)
					Warning += "w";

				fg_ExpectFormat
					(
						"ContinuedValueString"
						, "auto g_A = _o\n\t{\n\t\t\"Description\"_o= \"a\\n\"\n\t\t\t\"b\\n\"\n\t\t\t+ Warning\n\t\t, \"Other\"_o= \"e\\n\"\n\t\t\t+ "
						+ Warning
						+ "\n\t\t, \"c\"\n\t\t\"d\"\n\t}\n;\n"
						, "auto g_A = _o\n\t{\n\t\t\"Description\"_o= \"a\\n\"\n\t\t\"b\\n\"\n\t\t+ Warning\n\t\t, \"Other\"_o= \"e\\n\"\n\t\t+ "
						+ Warning
						+ "\n\t\t, \"c\"\n\t\t\"d\"\n\t}\n;\n"
					)
				;
			}
			// A value's strings on lines of their own behind its '=' stand one level in, and a conditional's branches go on
			// with them there. So does a string continued behind a macro on its line, '"%" PRId32' over '" b"'.
			fg_ExpectFormat
				(
					"ContinuedStringAcrossConditional"
					, "static ch8 const gc_Help[] =\n\t\t\"Program \" DName\n\t\t\"use\\n\"\n#if DStream\n\t\t\"  --\" DPrefix\n\t\t\"stream\\n\"\n#endif\n\t\t\"\\n\";\n"
					, "static ch8 const gc_Help[] =\n\t\"Program \" DName\n\t\"use\\n\"\n#if DStream\n\t\"  --\" DPrefix\n\t\"stream\\n\"\n#endif\n\t\"\\n\"\n;\n"
				)
			;
			// Without an operator behind them, a DSL key's continued strings stand at the key's level.
			fg_ExpectFormat
				(
					"ContinuedKeyString"
					, "auto g_A = _o\n\t{\n\t\t\"Description\"_o= \"a\\n\"\n\t\t\t\"b\\n\"\n\t\t, \"Other\"_o= \"c\\n\"\n\t\t\"d\\n\"\n\t}\n;\n"
					, "auto g_A = _o\n\t{\n\t\t\"Description\"_o= \"a\\n\"\n\t\t\"b\\n\"\n\t\t, \"Other\"_o= \"c\\n\"\n\t\t\"d\\n\"\n\t}\n;\n"
				)
			;
			// A comment behind a list's comma describes the element in front of it, and the
			// comma goes past it to the element behind.
			fg_ExpectFormat
				(
					"CommentBehindListComma"
					, "void f()\n{\n\tg(\n\t\ta,\n\t\t1,      // Count\n\t\tnSize,  // Size\n\t\tb\n\t);\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t, 1 // Count\n\t\t\t, nSize // Size\n\t\t\t, b\n\t\t)\n\t;\n}\n"
					, false
				)
			;
			// A body that labels fall through to stays under all of them.
			fg_ExpectFormat
				(
					"FallThroughLabels"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\tdefault:\n\t\treturn 1;\n\tcase 2:\n\tcase 3: return 2;\n\tcase 4:\n\t\treturn 3;\n\t}\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\tdefault:\n\t\treturn 1;\n\tcase 2:\n\tcase 3:\n\t\treturn 2;\n\tcase 4: return 3;\n\t}\n}\n"
				)
			;
			// A declarator a cv-qualifier follows declares, in a cast as anywhere.
			fg_ExpectFormat
				(
					"QualifiedPointerCast"
					, "void f()\n{\n\tg(0, (char* const *)Commands);\n\tchar* const pA = nullptr;\n}\n"
					, "void f()\n{\n\tg(0, (char * const *)Commands);\n\tchar * const pA = nullptr;\n}\n"
				)
			;
			// A cv-qualifier ends the type a declarator in parentheses follows, too.
			fg_ExpectFormat
				(
					"QualifiedDeclarators"
					, "void fg_F(char const(&_String)[4], int volatile(*_pValue)[2]);\nusing CA = TCFoo<char const(&)[4]>;\n"
					, "void fg_F(char const (&_String)[4], int volatile (*_pValue)[2]);\nusing CA = TCFoo<char const (&)[4]>;\n"
				)
			;
			// An enumerator list takes the line under the enum's brace, and a statement that
			// opens with its lambda ends on the call behind the body.
			fg_ExpectFormat
				(
					"EnumListsAndLambdaCalls"
					, "void f()\n{\n\tenum { EA = 4 };\n\tenum\n\t{EB = 4\n\t};\n\t[]()\n\t{\n\t\tg();\n\t\th();\n\t}()\n\t;\n}\n"
					, "void f()\n{\n\tenum\n\t{\n\t\tEA = 4\n\t};\n\n\tenum\n\t{\n\t\tEB = 4\n\t};\n\n\t[]()\n\t{\n\t\tg();\n\t\th();\n\t}();\n}\n"
				)
			;
			// A string continued on the next line behind an element's comma stands at the element's level, the comma's
			// line being the one it continues.
			CStr StringHalf;
			for (umint i = 0; i < 100; ++i)
				StringHalf += "x";

			fg_ExpectFormat
				(
					"ContinuedStringBehindComma"
					, "void f()\n{\n\tg(a, \"" + StringHalf + " \"\n\t\t\"" + StringHalf + "\") << b;\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t, \"" + StringHalf + " \"\n\t\t\t\"" + StringHalf + "\"\n\t\t)\n\t\t<< b\n\t;\n}\n"
				)
			;
			// A string continued in a call behind a comma the layout moves is placed against the line the call's
			// list opens for it.
			fg_ExpectFormat
				(
					"ContinuedStringBehindMovedComma"
					, "void fg_F()\n{\n\tfg_A(b,\n\t\tfg_G(\"c\"\n\t\t\t\"d\", 1));\n}\n"
					, "void fg_F()\n{\n\tfg_A\n\t\t(\n\t\t\tb\n\t\t\t, fg_G\n\t\t\t(\n\t\t\t\t\"c\"\n\t\t\t\t\"d\"\n\t\t\t\t, 1\n\t\t\t)\n\t\t)\n\t;\n}\n"
				)
			;
			// A macro between two literals continues their string, and leads no value: every line of it stands at
			// the element's level.
			fg_ExpectFormat
				(
					"ContinuedStringMacroBetween"
					, "void fg_F()\n{\n\tfg_Log(\"a\" DFormat \" b\"\n\t\t\"c\" DFormat \" d\"\n\t\t\"e\" DFormat, 1);\n}\n"
					, "void fg_F()\n{\n\tfg_Log\n\t\t(\n\t\t\t\"a\" DFormat \" b\"\n\t\t\t\"c\" DFormat \" d\"\n\t\t\t\"e\" DFormat\n\t\t\t, 1\n\t\t)\n\t;\n}\n"
				)
			;
			// An operator leads its operand on the operand's line, also a string too long for it.
			fg_ExpectFormat
				(
					"OperatorLeadsLongString"
					, "void f()\n{\n\tg(a &&\n\t\t\"" + StringHalf + StringHalf + "\"\n\t\t\"b\");\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t&& \"" + StringHalf + StringHalf + "\"\n\t\t\t\"b\"\n\t\t)\n\t;\n}\n"
				)
			;
			// A name's template argument list stays whole in front of the braced initializer that opens behind it.
			CStr Initializer;
			for (umint i = 0; i < 90; ++i)
				Initializer += "a";

			fg_ExpectFormat
				(
					"TemplateArgumentsBraced"
					, "void f()\n{\n\tg(b || TCFoo<int>{" + Initializer + ", " + Initializer + "});\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\tb || TCFoo<int>\n\t\t\t{\n\t\t\t\t" + Initializer + "\n\t\t\t\t, " + Initializer + "\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			// Also in a condition that declares what it tests.
			fg_ExpectFormat
				(
					"TemplateArgumentsBeforeBracedNameInCondition"
					, "void f()\n{\n\tif (TCOptional<CStr> Value{fg_Get(" + Initializer + ", " + Initializer + ")})\n\t\tg();\n}\n"
					, "void f()\n{\n\tif\n\t(\n\t\tTCOptional<CStr> Value\n\t\t{\n\t\t\tfg_Get\n\t\t\t(\n\t\t\t\t"
					+ Initializer
					+ "\n\t\t\t\t, "
					+ Initializer
					+ "\n\t\t\t)\n\t\t}\n\t)\n\t{\n\t\tg();\n\t}\n}\n"
					, false
				)
			;
			// Two parentheses holding a name each stay whole in front of the braced list they cast.
			fg_ExpectFormat
				(
					"CastPairBeforeBraced"
					, "void f()\n{\n\tg(1, (__m512i)(__v16si){" + Initializer + ", " + Initializer + "});\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\t1\n\t\t\t, (__m512i)(__v16si)\n\t\t\t{\n\t\t\t\t" + Initializer + "\n\t\t\t\t, " + Initializer + "\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			// So does one alone.
			fg_ExpectFormat
				(
					"CastBeforeBraced"
					, "void f()\n{\n\tg(1, (__m256bh){" + Initializer + ", " + Initializer + "});\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\t1\n\t\t\t, (__m256bh)\n\t\t\t{\n\t\t\t\t" + Initializer + "\n\t\t\t\t, " + Initializer + "\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			// A parenthesis holding a name alone is never opened: the call on what it yields opens instead.
			fg_ExpectFormat
				(
					"NameAloneNotOpened"
					, "void f()\n{\n\tg(NAME(Begin)(" + Initializer + ", " + Initializer + "));\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\tNAME(Begin)\n\t\t\t(\n\t\t\t\t" + Initializer + "\n\t\t\t\t, " + Initializer + "\n\t\t\t)\n\t\t)\n\t;\n}\n"
				)
			;
			// A '<' spaced on both sides is a comparison, also once the layout breaks in front of it.
			fg_ExpectFormat
				(
					"SpacedAngleComparison"
					, "INTF <MyProto1> * Func(INTF <MyProto1> *p" + Initializer + ", INTF<MyProto1> *p3" + Initializer + ")\n{\n}\n"
					, "INTF\n\t< MyProto1\n\t> * Func\n\t(\n\t\tINTF < MyProto1 > *p" + Initializer + "\n\t\t, INTF<MyProto1> *p3" + Initializer + "\n\t)\n{\n}\n"
				)
			;
			// An element a comment line stands above is laid out as any other.
			fg_ExpectFormat
				(
					"ListElementBelowCommentOpened"
					, "CA g_Values[] = {\n\t// Header\n\tfg_A(" + Initializer + ", " + Initializer + "),\n\tfg_A(1, 2),\n};\n"
					, "CA g_Values[] =\n\t{\n\t\t// Header\n\t\tfg_A\n\t\t(\n\t\t\t" + Initializer + "\n\t\t\t, " + Initializer + "\n\t\t)\n\t\t, fg_A(1, 2)\n\t}\n;\n"
					, false
				)
			;
			// Nor in front of the declaration a macro marks with it, which stays part of that declaration.
			{
				CStr Name;
				for (umint i = 0; i < 170; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"MacroMarkedDeclaration"
						, "struct CA\n{\n\tDMibPhase(Destroy) TCFuture<CResult> f_" + Name + "() const;\n};\n"
						, "struct CA\n{\n\tDMibPhase(Destroy) TCFuture<CResult>\n\t\tf_" + Name + "() const\n\t;\n};\n"
					)
				;
			}
			// Cases written on one line each take a line of their own, their statements on their labels' lines.
			fg_ExpectFormat
				(
					"CasesOnOneLine"
					, "void f()\n{\n\tswitch (a) { default: break; case 2: break; }\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tdefault: break;\n\tcase 2: break;\n\t}\n}\n"
				)
			;
			// A block passed to a macro opens on a line of its own behind its comma.
			fg_ExpectFormat
				(
					"MacroBlockArgument"
					, "void f()\n{\n\tDMibTrace(\"a\", {\n\t\tg();\n\t});\n}\n"
					, "void f()\n{\n\tDMibTrace\n\t\t(\n\t\t\t\"a\"\n\t\t\t,\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			// So does one in front of the name a braced initializer gives its value.
			fg_ExpectFormat
				(
					"TemplateArgumentsBeforeBracedName"
					, "void f()\n{\n\tTCVector<CStr> Values{" + Initializer + ", " + Initializer + "};\n}\n"
					, "void f()\n{\n\tTCVector<CStr> Values\n\t\t{\n\t\t\t" + Initializer + "\n\t\t\t, " + Initializer + "\n\t\t}\n\t;\n}\n"
				)
			;
			// An element opening with a bracket that fits on a line of its own but not behind its comma takes the line below the
			// comma whole.
			{
				CStr Numbers;
				for (umint i = 0; i < 22; ++i)
					Numbers += "{}48792"_f << (i ? ", " : "");

				fg_ExpectFormat
					(
						"BracketElementBelowComma"
						, "TEST_CONSTEXPR(_mm_cmplt_epu16_mask(((__m128i)(__v8hu){" + Numbers + "}), ((__m128i)(__v8hu){" + Numbers + ", 12})) == (__mmask8)0xAF);\n"
						, "TEST_CONSTEXPR\n\t(\n\t\t_mm_cmplt_epu16_mask\n\t\t(\n\t\t\t((__m128i)(__v8hu){"
						+ Numbers
						+ "})\n\t\t\t,\n\t\t\t((__m128i)(__v8hu){"
						+ Numbers
						+ ", 12})\n\t\t)\n\t\t== (__mmask8)0xAF\n\t)\n;\n"
					)
				;
			}
			// So does an operand opening with a bracket below the operator leading its line.
			{
				CStr Name;
				for (umint i = 0; i < 140; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"BracketOperandBelowOperator"
						, "void f()\n{\n\tif (pFree && (fg_IsIterator(p" + Name + ") || pFree->f_IsPointer()) && fg_Contains(Names))\n\t\tg();\n}\n"
						, "void f()\n{\n\tif\n\t(\n\t\tpFree\n\t\t&&\n\t\t(fg_IsIterator(p"
						+ Name
						+ ") || pFree->f_IsPointer())\n\t\t&& fg_Contains(Names)\n\t)\n"
						"\t{\n\t\tg();\n\t}\n}\n"
						, false
					)
				;
			}
			// Nor is its parenthesis a parameter list, which a class defined with it would move a return type behind.
			{
				CStr Name;
				for (umint i = 0; i < 150; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"KeywordAttributeNoParameters"
						, "template <typename t_C> struct __declspec(dllexport) TC" + Name + "\n{\n\tint f() { return sizeof(t_C); }\n};\n"
						, "template <typename t_C>\nstruct __declspec(dllexport) TC" + Name + "\n{\n\tint f()\n\t{\n\t\treturn sizeof(t_C);\n\t}\n};\n"
					)
				;
			}
			// An attribute spelled with a keyword keeps its parentheses whole, as one in brackets does.
			{
				CStr Name;
				for (umint i = 0; i < 150; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"KeywordAttributeWhole"
						, "__attribute__((__internal_linkage__)) inline void\n\tDCall fg_A" + Name + "(void const *_pBegin, void const *_pEnd) {}\n"
						, "__attribute__((__internal_linkage__))\ninline void DCall fg_A" + Name + "\n\t(\n\t\tvoid const *_pBegin\n\t\t, void const *_pEnd\n\t)\n{\n}\n"
					)
				;
			}
			// And in front of a variable template's specialization the initializer gives its value.
			{
				CStr Name;
				for (umint i = 0; i < 100; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"TemplateArgumentsBeforeBracedSpecialization"
						, "template <>\nconstexpr TCCase<int> g_Empty<bool>{.m_Initial = {}, .m_Index = 0, .m_Input" + Name + " = {}, .m_Expected = {}};\n"
						, "template <>\nconstexpr TCCase<int> g_Empty<bool>\n\t{\n\t\t.m_Initial = {}\n\t\t, .m_Index = 0\n\t\t, .m_Input" + Name + " = {}\n\t\t, .m_Expected = {}\n\t}\n;\n"
					)
				;
			}
			// A separator stands in front of its element also where the element is too long for that line, in a
			// block the layout moves in as well.
			CStr LongString;
			for (umint i = 0; i < 180; ++i)
				LongString += "x";

			fg_ExpectFormat
				(
					"SeparatorLeadsLongElement"
					, "namespace N\n{\nvoid f()\n{\n\tg(a,\n\t  \"" + LongString + "\"\n\t  \"b\");\n}\n}\n"
					, "namespace N\n{\n\tvoid f()\n\t{\n\t\tg\n\t\t\t(\n\t\t\t\ta\n\t\t\t\t, \"" + LongString + "\"\n\t\t\t\t\"b\"\n\t\t\t)\n\t\t;\n\t}\n}\n"
				)
			;
			// A string continued over lines in an opened call stands at its element's level, written there once: the line
			// break the layout gives it carries the indentation.
			fg_ExpectFormat("ContinuedStringOpened", "void f()\n{\n\tg(\n\"bar\"\n\"nop\"\n);\n}\n", "void f()\n{\n\tg\n\t\t(\n\t\t\t\"bar\"\n\t\t\t\"nop\"\n\t\t)\n\t;\n}\n");
			// A string an element continues on the line below keeps that line, so the list
			// around it opens and a value holding it gives at each of its operators. An asm
			// statement's operand sections start their lines as its operands do.
			fg_ExpectFormat
				(
					"StringContinuationOpens"
					, "void f()\n{\n\tfg_Call(\"a\"\n\t\t\"b\"_f << x << y, z);\n"
					"\tasm volatile(\n\t\t\t\"0:\\n\"\n\t\t\t\"\\tbne 0b\\n\"\n\t\t\t: \"=r\"(a),\"=r\"(b)\n\t\t\t);\n"
					"\tasm volatile(\"x\" : \"=r\"(a) : \"r\"(b));\n}\n"
					, "void f()\n{\n\tfg_Call\n\t\t(\n\t\t\t\"a\"\n\t\t\t\"b\"_f\n\t\t\t<< x\n\t\t\t<< y\n\t\t\t, z\n\t\t)\n\t;\n"
					"\tasm volatile\n\t\t(\n\t\t\t\"0:\\n\"\n\t\t\t\"\\tbne 0b\\n\"\n\t\t\t: \"=r\"(a)\n\t\t\t, \"=r\"(b)\n\t\t)\n\t;\n"
					"\tasm volatile(\"x\" : \"=r\"(a) : \"r\"(b));\n}\n"
				)
			;
			// Each enumerator takes a line, the comma in front of it, and a blank line between
			// two moves in front of the comma. A comma moves across a comment behind it, and
			// the one behind the last enumerator is taken out. A type is set off from a
			// '#define' behind it, not from a conditional or a pragma.
			fg_ExpectFormat
				(
					"EnumeratorLines"
					, "enum EA\n{\n\tEA_A, EA_B\n};\n#define DA 1\nenum EB\n{\n\tEB_A,\n\tEB_B,\n\n\tEB_C = 2, // c\n\tEB_D, // d\n};\n#if 1\n#endif\n"
					, "enum EA\n{\n\tEA_A\n\t, EA_B\n};\n\n#define DA 1\nenum EB\n{\n\tEB_A\n\t, EB_B\n\n\t, EB_C = 2 // c\n\t, EB_D // d\n};\n#if 1\n#endif\n"
					, false
				)
			;
			// A comma behind the comment that describes its enumerator moves in front of the next one, and the comment
			// then ending its line is written as a line comment in the same pass.
			fg_ExpectFormat
				(
					"EnumCommaBehindComment"
					, "enum EA {\n\tEA_A = 1 /*!< A */,\n\tEA_B /*!< B */,\n\tEA_C /* C */,\n\tEA_D\n};\n"
					, "enum EA\n{\n\tEA_A = 1 //!< A\n\t, EA_B //!< B\n\t, EA_C // C\n\t, EA_D\n};\n"
					, false
				)
			;
			// A typedef's type is defined as any other, and the declarators a definition declares stand behind its
			// closing brace.
			fg_ExpectFormat
				(
					"TypedefDefinitions"
					, "typedef enum {\n  EA_A,\n  EA_B,\n} EA;\nstruct CB\n{\n\tint m_B;\n}\ng_B, *g_pB;\nstruct CC {\n\tint m_C;\n} g_C = {1,};\n"
					, "typedef enum\n{\n\tEA_A\n\t, EA_B\n} EA;\n\nstruct CB\n{\n\tint m_B;\n} g_B, *g_pB;\nstruct CC\n{\n\tint m_C;\n} g_C = {1,};\n"
					, false
				)
			;
			// A function's body ends its statement whatever type it returns, and a definition declaring a member is
			// no type set off by a blank line.
			CStr DefinitionEnds = "enum EA fg_A()\n{\n\treturn EA_A;\n}\n\nauto fg_B() -> int\n{\n\treturn 0;\n}\n\n"
				"struct CC\n{\n\tstruct\n\t{\n\t\tint m_A;\n\t} m_Inner;\n\tint m_B;\n};\n"
			;
			fg_ExpectFormat("DefinitionEnds", DefinitionEnds, DefinitionEnds);
			// An enumerator that is the first in some configurations only, and an enum holding
			// a directive other than a conditional's, keep their commas, and so does a braced
			// initializer.
			CStr KeptCommas = "enum EA\n{\n#if 1\n\tEA_A,\n#endif\n\tEA_B,\n};\n\nenum EB\n{\n\tEB_A,\n#include \"EB.inc\"\n\tEB_B,\n};\n\n"
				"int gc_A[] = {1, 2,};\n"
			;
			fg_ExpectFormat("EnumCommasKept", KeptCommas, KeptCommas);
			// The first enumerator moves with its enum also where a comment in another one keeps that one's lines.
			fg_ExpectFormat
				(
					"EnumFirstMovedBesideComment"
					, "namespace N\n{\nenum EA {\n\tEA_A = 0,\n\tMark(/*L=*/EA_A)\n};\n}\n"
					, "namespace N\n{\n\tenum EA\n\t{\n\t\tEA_A = 0\n\t\t, Mark(/*L=*/EA_A)\n\t};\n}\n"
					, false
				)
			;
			// An enum's enumerators are one list, also where a macro spells some of them behind a directive.
			fg_ExpectFormat
				(
					"EnumMacroBehindDirective"
					, "namespace N {\nenum EA {\n\tITER(POPULATE)\n#undef POPULATE\n\tEA_Last\n};\n}\n"
					, "namespace N\n{\n\tenum EA\n\t{\n\t\tITER(POPULATE)\n#undef POPULATE\n\t\tEA_Last\n\t};\n}\n"
				)
			;
			// An enumerator whose comma already starts its line stands at the body's level, with the comment above it.
			fg_ExpectFormat("EnumCommaLineLevelled", "enum EA\n{\n\tEA_A = 0\n\n\t\t/// B\n\t\t, EA_B\n\t , EA_C\n};\n", "enum EA\n{\n\tEA_A = 0\n\n\t/// B\n\t, EA_B\n\t, EA_C\n};\n");
			// A block comment ending its line behind a comma describes the element in front of it as a line comment
			// does, which it is written as, and the comma goes past it.
			fg_ExpectFormat("ListCommaBlockComment", "void f()\n{\n\tg(a, /* A */\n\t\tb);\n}\n", "void f()\n{\n\tg\n\t\t(\n\t\t\ta // A\n\t\t\t, b\n\t\t)\n\t;\n}\n", false);
			// A braced initializer the layout opens, its statement too long for its line, loses the comma ending it as
			// one written across lines does.
			CStr LongName;
			for (umint i = 0; i < 180; ++i)
				LongName += "x";

			fg_ExpectFormat("ListCommaOpened", "int g_" + LongName + "[] = {1, 2,};\n", "int g_" + LongName + "[] =\n\t{\n\t\t1\n\t\t, 2\n\t}\n;\n", false);
			// A comment line among an opened list's rows stands with them, one over several lines included, which keeps no
			// lines of the statement around it.
			fg_ExpectFormat
				(
					"ListCommentRows"
					, "void f()\n{\n  static int const s[][2] = {\n    { 1, 2 },\n    /* a\n       b */\n  };\n"
					"  static int const t[][2] = {\n    { 1, 2 },\n    /* c */\n  };\n}\n"
					, "void f()\n{\n\tstatic int const s[][2] =\n\t\t{\n\t\t\t{1, 2}\n\t\t\t/* a\n\t\t\t   b */\n\t\t}\n\t;\n"
					"\tstatic int const t[][2] =\n\t\t{\n\t\t\t{1, 2}\n\t\t\t/* c */\n\t\t}\n\t;\n}\n"
					, false
				)
			;
			// A comma ending its line moves past the comment lines between it and the next element, as one in front of a
			// comment on its line does.
			fg_ExpectFormat
				(
					"ListCommaBehindCommentLines"
					, "int g_A[][2] = {\n  {1, 2},\n  // B\n  {3, 4},\n\n  /* C */\n  {5, 6},\n};\n\nvoid f() {\n  g(a,\n    // b\n    b);\n}\n"
					, "int g_A[][2] =\n\t{\n\t\t{1, 2}\n\t\t// B\n\t\t, {3, 4}\n\n\t\t/* C */\n\t\t, {5, 6}\n\t}\n;\n\n"
					"void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t// b\n\t\t\t, b\n\t\t)\n\t;\n}\n"
					, false
				)
			;
			// A string continued behind a comma the layout moves onto a line of its own stands at its element's level,
			// also where the source wrote the comma behind another string's continuation.
			fg_ExpectFormat
				(
					"StringContinuedBehindMovedComma"
					, "void f()\n{\n\tg(h(\"a\"\n\t    \"b\"), \"abc\"\n\t  \"def\");\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\th\n\t\t\t(\n\t\t\t\t\"a\"\n\t\t\t\t\"b\"\n\t\t\t)\n\t\t\t, \"abc\"\n\t\t\t\"def\"\n\t\t)\n\t;\n}\n"
				)
			;
			// A comment that names the code behind it on its line moves with that code: joined where the construct fits,
			// and leading the line where it is split. One in a lambda's body leaves the construct around it its layout,
			// and one tight against a template argument list's '<' leaves it a list.
			fg_ExpectFormat
				(
					"NamingCommentJoined"
					, "void f()\n{\n  g(a,\n    /*x=*/b);\n  h(f<\n      /*bSin=*/ false>(x));\n"
					"    g(x)\n        .Case([&](A op) {\n          h(op,\n            /*b=*/false);\n        });\n}\n"
					, "void f()\n{\n\tg(a, /*x=*/b);\n\th(f</*bSin=*/ false>(x));\n"
					"\tg(x).Case\n\t\t(\n\t\t\t[&](A op)\n\t\t\t{\n\t\t\t\th(op, /*b=*/false);\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			{
				CStr Long;
				for (umint i = 0; i < 180; ++i)
					Long += "x";

				fg_ExpectFormat
					(
						"NamingCommentSplit"
						, "void f()\n{\n  g(/*a*/ " + Long + ", /*b=*/ 2);\n}\n"
						, "void f()\n{\n\tg\n\t\t(\n\t\t\t/*a*/ " + Long + "\n\t\t\t, /*b=*/ 2\n\t\t)\n\t;\n}\n"
					)
				;
			}
			// A row a string continued over lines keeps from being joined opens as one too long for its line does.
			fg_ExpectFormat
				(
					"ListRowWithContinuedString"
					, "int g_A[][2] = {\n  {1, 2},\n  { g(\"a\"\n       \"b\"),\n    3 },\n};\n"
					, "int g_A[][2] =\n\t{\n\t\t{1, 2}\n\t\t,\n\t\t{\n\t\t\tg\n\t\t\t(\n\t\t\t\t\"a\"\n\t\t\t\t\"b\"\n\t\t\t)\n\t\t\t, 3\n\t\t}\n\t}\n;\n"
					, false
				)
			;
			// The comma in front of a row a comment line stands above stays in front of the row.
			fg_ExpectFormat
				(
					"ListRowBehindCommentLine"
					, "char const *g_A[][2] = {\n  {\"a\", \"b\"},\n#ifdef DA\n  /* x\n   */\n  {\"c\",\n   \"d \"\n   \"e\"},\n#endif\n  {\"f\", \"g\"},\n};\n"
					, "char const *g_A[][2] =\n\t{\n\t\t{\"a\", \"b\"}\n#ifdef DA\n\t\t/* x\n\t\t */\n\t\t,\n\t\t{\n\t\t\t\"c\"\n\t\t\t, \"d \"\n"
					"\t\t\t\"e\"\n\t\t}\n#endif\n\t\t, {\"f\", \"g\"}\n\t}\n;\n"
					, false
				)
			;
			// The declarators behind a type's body are laid out as a declaration's head is, their list opened from the
			// brace's line.
			fg_ExpectFormat
				(
					"DefinitionDeclaratorList"
					, "void f() {\n  struct {\n    int m_A;\n  } Tests[] = {\n      {1,\n       2},\n      {3, 4},\n  };\n}\n"
					, "void f()\n{\n\tstruct\n\t{\n\t\tint m_A;\n\t} Tests[] =\n\t\t{\n\t\t\t{1, 2}\n\t\t\t, {3, 4}\n\t\t}\n\t;\n}\n"
					, false
				)
			;
			// Across a conditional each enumerator's comma stands in front of it in every
			// branch it is compiled in.
			fg_ExpectFormat
				(
					"EnumCommasAcrossDirectives"
					, "enum EA\n{\n\tEA_A, EA_B,\n#ifdef DA\n\tEA_C = 1,\n#else\n\tEA_C = 0,\n#endif\n\tEA_D,\n};\n"
					, "enum EA\n{\n\tEA_A\n\t, EA_B\n#ifdef DA\n\t, EA_C = 1\n#else\n\t, EA_C = 0\n#endif\n\t, EA_D\n};\n"
					, false
				)
			;
			// A conditional around a whole scope leaves the depths inside it to the scope's
			// braces; only lines directly inside a conditional keep the depth they have.
			fg_ExpectFormat
				(
					"ConditionalAroundScope"
					, "#if 1\nvoid f()\n{\n\t\tg();\n#if DFoo\n\t\th();\n#endif\n}\n#endif\n"
					, "#if 1\nvoid f()\n{\n\tg();\n#if DFoo\n\th();\n#endif\n}\n#endif\n"
				)
			;
			// A body a conditional cuts from its head stands under the line the head starts, wherever the layout
			// moves that line: 'if (a)' or '} else' over '#endif' over '{'.
			fg_ExpectFormat
				(
					"CutBodyUnderHead"
					, "void f()\n{\n  {\n#ifdef DA\n    if (a)\n#else\n      a = g();\n    if (a)\n#endif\n    {\n      h();\n    }\n  }\n}\n"
					, "void f()\n{\n\t{\n#ifdef DA\n\t\tif (a)\n#else\n\t\ta = g();\n\t\tif (a)\n#endif\n\t\t{\n\t\t\th();\n\t\t}\n\t}\n}\n"
				)
			;
			fg_ExpectFormat
				(
					"CutBodyUnderElse"
					, "void f()\n{\n  {\n#ifdef DA\n    if (a)\n    {\n      g();\n    } else\n#endif\n      {\n        h();\n      }\n  }\n}\n"
					, "void f()\n{\n\t{\n#ifdef DA\n\t\tif (a)\n\t\t\tg();\n\t\telse\n#endif\n\t\t{\n\t\t\th();\n\t\t}\n\t}\n}\n"
					, false
				)
			;
			// A comment line in front of a statement moves with it where the statement leaves the stretch a cut body
			// moved it with.
			fg_ExpectFormat
				(
					"CommentBehindCutBody"
					, "int main(void)\n{\n   while( (input = getch()) != 1)\n#ifdef DA\n      if( input == 2)\n         g( 2);\n"
					"      else if( input == 3)\n         g( 3);\n      else\n#endif\n      {\n         g( 4);\n      }\n   h( );\n"
					"                            /* Not really needed */\n   i( );          /* says */\n   return( 0);\n}\n"
					, "int main(void)\n{\n\twhile ((input = getch()) != 1)\n#ifdef DA\n\t\tif (input == 2)\n\t\t\tg(2);\n"
					"\t\telse if (input == 3)\n\t\t\tg(3);\n\t\telse\n#endif\n\t\t{\n\t\t\tg(4);\n\t\t}\n\th();\n"
					"\t/* Not really needed */\n\ti(); // says\n\treturn (0);\n}\n"
					, false
				)
			;
			fg_ExpectFormat
				(
					"GuardInSomeConfigurations"
					, "void f()\n{\n#ifdef DA\n\tif (a)\n#endif\n\tif (b)\n\t\tg();\n#ifdef DB\n\tif (c)\n\t\th();\n\telse\n#endif\n\t  i();\n}\n"
					, "void f()\n{\n#ifdef DA\n\tif (a)\n#endif\n\tif (b)\n\t\tg();\n#ifdef DB\n\tif (c)\n\t\th();\n\telse\n#endif\n\ti();\n}\n"
				)
			;
			// A statement keeping its lines moves them from where the block around it moved them, its body included.
			fg_ExpectFormat
				(
					"KeptStatementInMovedBlock"
					, "namespace {\nstruct C\n{\n  void H(int a,\n         int b = g(R\"(x\ny)\")) override\n  {\n    return;\n  }\n};\n}\n"
					, "namespace\n{\n\tstruct C\n\t{\n\t\tvoid H(int a,\n\t\t\t   int b = g(R\"(x\ny)\")) override\n\t\t{\n\t\t\treturn;\n\t\t}\n\t};\n}\n"
				)
			;
			// The empty statement a clause guards stands one level in from it.
			fg_ExpectFormat
				(
					"GuardedEmptyStatement"
					, "namespace {\nvoid f()\n{\n  for(q = a; q < b && !*q; q++)\n    ;\n}\n}\n"
					, "namespace\n{\n\tvoid f()\n\t{\n\t\tfor (q = a; q < b && !*q; q++)\n\t\t\t;\n\t}\n}\n"
				)
			;
			// A name alone on its line is a macro statement where a statement keyword starts the next line.
			fg_ExpectFormat
				(
					"MacroStatementInFrontOfKeyword"
					, "void f()\n{\n   BOOST_MATH_STD_USING\n   if (z < 1)\n      g();\n}\n"
					, "void f()\n{\n\tBOOST_MATH_STD_USING\n\tif (z < 1)\n\t\tg();\n}\n"
				)
			;
			// A call of a name alone on its line is a macro statement where another such call follows it.
			fg_ExpectFormat
				(
					"MacroStatementsInARow"
					, "IS_SEQUENCE(N::CA)\nIS_SEQUENCE(N::CB)\n\ntemplate <> struct N::TCFoo<CBar> {\n  static void f(N::CIo &io, CBar &V) {\n    io.g(\"Dir\", V.Dir);\n  }\n};\n"
					, "IS_SEQUENCE(N::CA)\nIS_SEQUENCE(N::CB)\n\ntemplate <> struct N::TCFoo<CBar>\n{\n\tstatic void f(N::CIo &io, CBar &V)\n"
					"\t{\n\t\tio.g(\"Dir\", V.Dir);\n\t}\n};\n"
				)
			;
			// A requires clause on a line of its own stands one level in from the declaration.
			fg_ExpectFormat
				(
					"RequiresClauseLevelled"
					, "template <class T>\nstruct S {\n  bool f() const\n    requires requires { g(a); }\n  {\n    return true;\n  }\n};\n"
					, "template <class T>\nstruct S\n{\n\tbool f() const\n\t\trequires requires\n\t\t{\n\t\t\tg(a);\n\t\t}\n\t{\n\t\treturn true;\n\t}\n};\n"
				)
			;
			// An 'if' an 'else' guards stands on the 'else''s line, wherever the source wrote it.
			fg_ExpectFormat
				(
					"ElseOverIf"
					, "void f()\n{\n  if (a) {\n    g();\n  }\n  else\n  if (b) {\n    h();\n    i();\n  }\n  else\n    j();\n}\n"
					, "void f()\n{\n\tif (a)\n\t\tg();\n\telse if (b)\n\t{\n\t\th();\n\t\ti();\n\t}\n\telse\n\t\tj();\n}\n"
					, false
				)
			;
			// A comment line between an 'else' and the 'if' it guards keeps the 'if' on a line of its own, guarded by the
			// 'else', and its clause on one line takes no braces.
			fg_ExpectFormat
				(
					"ElseOverCommentedIf"
					, "int f()\n{\n\tif (a)\n\t\treturn 1;\n\telse\n\t\t/* No final */\n\t\tif (b)\n\t\t\treturn 0;\n\treturn 2;\n}\n"
					, "int f()\n{\n\tif (a)\n\t\treturn 1;\n\telse\n\t\t/* No final */\n\t\tif (b)\n\t\t\treturn 0;\n\treturn 2;\n}\n"
				)
			;
			// A comment standing for a declarator's name goes where the declaration's line goes, joined or split.
			fg_ExpectFormat("NameCommentJoined", "void f(\n  int _A, int * /*_B*/)\n{\n}\n", "void f(int _A, int * /*_B*/)\n{\n}\n");
			{
				CStr Long;
				for (umint i = 0; i < 90; ++i)
					Long += "a";

				fg_ExpectFormat
					(
						"NameCommentSplit"
						, "void f(\n  int _" + Long + ", int &&/*_B*/,\n  int _C" + Long + ")\n{\n}\n"
						, "void f\n\t(\n\t\tint _" + Long + "\n\t\t, int &&/*_B*/\n\t\t, int _C" + Long + "\n\t)\n{\n}\n"
					)
				;
			}
			// A lambda's body in a statement that keeps its lines stands one level in from the line its introducer starts,
			// moved with the statement.
			fg_ExpectFormat
				(
					"KeptStatementLambdaBody"
					, "namespace N {\nvoid f() {\n  c(m)\n      .def(\n          [](P &_A) {\n               return g(_A);\n          },\n          R\"(\n  A)\");\n}\n}\n"
					, "namespace N\n{\n\tvoid f()\n\t{\n\t\tc(m)\n\t\t\t.def(\n\t\t\t\t[](P &_A) {\n\t\t\t\t\treturn g(_A);\n\t\t\t\t},\n"
					"\t\t\t\tR\"(\n  A)\");\n\t}\n}\n"
				)
			;
			// A run of calls too long for its line is laid out in time linear in its length.
			{
				CStr Source = "void f()\n{\n    DA(A)     DA(B)       DA(C)        DA(D)\n";
				for (umint i = 0; i < 60; ++i)
					Source += "    DA(NAME_{}   )\n"_f << i;

				Source += "    { 0, 0 }\n};\n";
				DMibTestPath("CallRunLinear");
				auto Formatted = fg_FormatSource(Source, "First");
				DMibExpect(fg_FormatSource(Formatted, "Second"), ==, Formatted);
			}
			// In a function's body a call ending its line guards the statement written further in on the next, and one with
			// the next statement or a label at its own column ends its statement without a terminator.
			fg_ExpectFormat
				(
					"CallClauseAndStatement"
					, "void f()\n{\n  Catch(Fault)\n     d.ps = Fault;\n  list_for_each(p, Head)\n    g(p);\n"
					"  IOTRACE((\"A\"))\n  rc = h();\n  THROW(\"B\")\nbailout:\n  return;\n}\n"
					, "void f()\n{\n\tCatch(Fault)\n\t\td.ps = Fault;\n\tlist_for_each(p, Head)\n\t\tg(p);\n\tIOTRACE((\"A\"))\n\trc = h();\n\tTHROW(\"B\")\nbailout:\n\treturn;\n}\n"
				)
			;
			// Behind a call to a template-id or a member access an ambiguous operator is binary, since neither names a type.
			fg_ExpectFormat
				(
					"OperatorBehindCallOrMember"
					, "void f()\n{\n\tif (c && !isa<CZ>(c) &&\n\t    (t != K::U))\n\t\tg();\n\tP += S.m_E *\n\t     (C + R);\n\tauto o = parse<int, 2>(s) *\n\t  Sign;\n}\n"
					, "void f()\n{\n\tif (c && !isa<CZ>(c) && (t != K::U))\n\t\tg();\n\tP += S.m_E * (C + R);\n\tauto o = parse<int, 2>(s) * Sign;\n}\n"
				)
			;
			// A requires expression in a requires clause opens under the clause's line wherever the source put its brace, what
			// the clause goes on with behind it starting the next line, and the body it constrains stays the body, an empty one too.
			fg_ExpectFormat
				(
					"RequiresClauseExpressionOpens"
					, "template <class T>\nvoid f()\n\trequires requires (T t) { t.h(); }\n{\n}\n"
					"template <class T>\nvoid g()\n\trequires requires { T::a; } && cB<T>\n{\n}\n"
					, "template <class T>\nvoid f()\n\trequires requires (T t)\n\t{\n\t\tt.h();\n\t}\n{\n}\n"
					"template <class T>\nvoid g()\n\trequires requires\n\t{\n\t\tT::a;\n\t}\n\t&& cB<T>\n{\n}\n"
				)
			;
			// Every lambda body in an expression opens, not only the first, and so does every requires expression's.
			fg_ExpectFormat
				(
					"LaterLambdaBodiesOpen"
					, "void f()\n{\n\tx = [] { g(); }() && [] { h(); }();\n}\ntemplate <class T>\nconcept cA = requires { T::a; } && requires { T::b; };\n"
					, "void f()\n{\n\tx = []\n\t\t{\n\t\t\tg();\n\t\t}\n\t\t() && []\n\t\t{\n\t\t\th();\n\t\t}\n\t\t()\n\t;\n}\n\n"
					"template <class T>\nconcept cA = requires\n\t{\n\t\tT::a;\n\t}\n\t&& requires\n\t{\n\t\tT::b;\n\t}\n;\n"
				)
			;
			// A requires expression ending a concept's definition has its body laid out as a lambda's is.
			fg_ExpectFormat
				(
					"ConceptRequiresBody"
					, "template <class T>\nconcept cA =\n  requires (T _A) {\n    { _A.f() } noexcept;\n  };\n"
					, "template <class T>\nconcept cA = requires (T _A)\n\t{\n\t\t{ _A.f() } noexcept;\n\t}\n;\n"
				)
			;
			// A class key's brace holding what no class body can is the initializer of the variable it declares.
			fg_ExpectFormat
				(
					"ClassKeyInitializer"
					, "void f()\n{\n  struct tm Time{.tm_sec = 0,\n                .tm_min = 0};\n  struct CA A{1,\n      2};\n}\n"
					, "void f()\n{\n\tstruct tm Time{.tm_sec = 0, .tm_min = 0};\n\tstruct CA A{1, 2};\n}\n"
				)
			;
			// The 'if' a comment parts from its 'else' is the statement the 'else' guards, one level in.
			fg_ExpectFormat
				(
					"ElseOverCommentedIfPlaced"
					, "int f()\n{\n  if (a)\n    g();\n  else\n\t/* Text */\n  if (b)\n    h();\n  else\n    k();\n}\n"
					, "int f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\t/* Text */\n\t\tif (b)\n\t\t\th();\n\t\telse\n\t\t\tk();\n}\n"
				)
			;
			// A macro invoked where a clause stands, with an 'else' behind its block, guards that block as an 'if' does.
			fg_ExpectFormat
				(
					"ClauseMacro"
					, "void f()\n{\n  IF_CONSTEXPR(a) {\n    return 1;\n  }\n  else {\n    IF_CONSTEXPR(b) {\n      g();\n      return 2;\n    }\n"
					"    else{\n      return 3;\n    }\n  }\n}\n"
					, "void f()\n{\n\tIF_CONSTEXPR(a)\n\t{\n\t\treturn 1;\n\t}\n\telse\n\t{\n\t\tIF_CONSTEXPR(b)\n\t\t{\n\t\t\tg();\n\t\t\treturn 2;\n\t\t}\n"
					"\t\telse\n\t\t\treturn 3;\n\t}\n}\n"
					, false
				)
			;
			// In an enum keeping its commas behind its enumerators each enumerator and the comment lines in front of it
			// stand at the body's level.
			fg_ExpectFormat
				(
					"EnumTrailingCommasLevelled"
					, "typedef enum {\n  /* A */\n  DA(EA, 1),\n\n  /* B */\n  DA(EB, 2),\n#define EC EB\n  DA(ED, 3),\n  ELast\n} EOption;\n"
					, "typedef enum\n{\n\t/* A */\n\tDA(EA, 1),\n\n\t/* B */\n\tDA(EB, 2),\n#define EC EB\n\tDA(ED, 3),\n\tELast\n} EOption;\n"
				)
			;
			// A label to jump to behind a directive stands one level out, as a case does, and the stretch behind it
			// moves from there. A statement behind an '#endif' that only some configurations guard stands where one
			// of them puts it, the source's choice kept, and at the unguarded level otherwise.
			fg_ExpectFormat
				(
					"LabelBehindDirective"
					, "void f(void)\n{\n  for (;;)\n    {\n    switch (x)\n      {\n      default:\n#if A\n        {\n        }\n      else\n#endif\n"
					"      break;\n#if B\n      L:\n#endif\n      if (c != NULL)\n        {\n#if U\n          {\n          }\n        else\n#endif\n"
					"          {\n          }\n        }\n      }\n    }\n}\n"
					, "void f(void)\n{\n\tfor (;;)\n\t{\n\t\tswitch (x)\n\t\t{\n\t\tdefault:\n#if A\n\t\t\t{\n\t\t\t}\n\t\t\telse\n#endif\n"
					"\t\t\tbreak;\n#if B\n\t\tL:\n#endif\n\t\t\tif (c != NULL)\n\t\t\t{\n#if U\n\t\t\t\t{\n\t\t\t\t}\n\t\t\t\telse\n"
					"#endif\n\t\t\t\t{\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n"
				)
			;
			// A directive starts its line, indented behind its '#' by a level for each conditional it stands in, apart from
			// the code around it.
			fg_ExpectFormat
				(
					"DirectiveIndentation"
					, "#if 0\n#if 0\n  #  if 0\n#elif 1\n#else\n#endif\n#elif 1\n#define DValue 1\n#else\n#endif\n#elif 1\n#else\n#endif\n"
					"namespace N\n{\n\t#ifdef DFoo\n\tint g_A;\n\t#endif\n}\n"
					, "#if 0\n#\tif 0\n#\t\tif 0\n#\t\telif 1\n#\t\telse\n#\t\tendif\n#\telif 1\n#\t\tdefine DValue 1\n#\telse\n#\tendif\n#elif 1\n#else\n#endif\n"
					"namespace N\n{\n#ifdef DFoo\n\tint g_A;\n#endif\n}\n"
				)
			;
			// Indented with spaces the directive's name stands where the tabs put it, and indented with tabs again it
			// takes them back.
			{
				DMibTestPath("DirectiveIndentationSpaces");
				CStr Tabs = "#if 0\n#\tif 0\n#\t\tdefine A\n#\tendif\n#endif\n";
				CStr Spaces = "#if 0\n#   if 0\n#       define A\n#   endif\n#endif\n";

				auto fFormat = [](CStr const &_Source, bool _bTabs, CStr const &_Stage)
					{
						DMibTestPath(_Stage);
						auto Request = fg_Request(_Source);
						auto Properties = fg_MalterlibProperties();
						Properties["indent_style"] = _bTabs ? "tab" : "space";
						Request.m_Settings = CCodeFormattingSettings(Properties);
						auto Result = fg_AnalyzeCodeFormatting(Request);
						DMibExpectTrue(Result.m_Status == ECodeFormattingStatus::mc_Complete);

						return fg_ApplyCodeFormattingEdits(_Source, Result.m_Edits);
					}
				;

				DMibExpect(fFormat(Tabs, false, "ToSpaces"), ==, Spaces);
				DMibExpect(fFormat(Spaces, false, "Spaces"), ==, Spaces);
				DMibExpect(fFormat(Spaces, true, "ToTabs"), ==, Tabs);
			}
			// A directive's continuation lines move together, the least indented one level in from the directive.
			fg_ExpectFormat
				(
					"DirectiveContinuation"
					, "void f()\n{\n#define DApply(d_Name) \\\n\t\t\t\tif (d_Name) \\\n\t\t\t\t\td_Name = 1;\n\tDApply(a);\n}\n"
					"#ifdef DFoo\n#define DTwo(a, b) \\\n\ta + \\\n\t\tb\n#endif\nint g_A;\n"
					, "void f()\n{\n#define DApply(d_Name) \\\n\tif (d_Name) \\\n\t\td_Name = 1;\n\tDApply(a);\n}\n"
					"#ifdef DFoo\n#\tdefine DTwo(a, b) \\\n\t\ta + \\\n\t\t\tb\n#endif\nint g_A;\n"
				)
			;
			// Splices aligned at one column stay aligned, at the next tab stop behind the longest line.
			fg_ExpectFormat
				(
					"DirectiveContinuationAlignedSplices"
					, "#define DPair(a)\t\t\t\\\n\t\t\tfg_First(a);\t\\\n\t\t\tfg_Second(a)\n"
					, "#define DPair(a)\t\\\n\tfg_First(a);\t\\\n\tfg_Second(a)\n"
				)
			;
			// So do ones the continuation lines' indentation aligns.
			fg_ExpectFormat
				(
					"DirectiveContinuationIndentedSplices"
					, "#define DPrintable(c)\t\t\t\t\t\t\\\n      ((c) >= 0 && (c) <= 255 &&\t\t\t\t\\\n\t  ((c) == '\\t' || isprint((c)) ||\t\t\t\\\n"
					"\t      (c) > 127))\n"
					, "#define DPrintable(c)\t\t\t\t\\\n\t((c) >= 0 && (c) <= 255 &&\t\t\\\n\t((c) == '\\t' || isprint((c)) ||\t\\\n\t\t(c) > 127))\n"
				)
			;
			// C++23's '#elifdef' and '#elifndef' are alternatives like '#elif'.
			fg_ExpectFormat
				(
					"DirectiveIndentationElifdef"
					, "int g_A;\n#ifdef A\n#define DValue 1\n#elifdef B\n#define DValue 2\n#elifndef C\n#define DValue 3\n#endif\n"
					"enum EValue\n{\n\tEValue_A,\n#ifdef A\n\tEValue_B\n#elifdef B\n\tEValue_C\n#endif\n};\n"
					, "int g_A;\n#ifdef A\n#\tdefine DValue 1\n#elifdef B\n#\tdefine DValue 2\n#elifndef C\n#\tdefine DValue 3\n#endif\n"
					"enum EValue\n{\n\tEValue_A\n#ifdef A\n\t, EValue_B\n#elifdef B\n\t, EValue_C\n#endif\n};\n"
					, false
				)
			;
			// A conditional without alternatives around all of a file's code adds no depth; one with them does.
			fg_ExpectFormat
				(
					"DirectiveIndentationWrapper"
					, "#pragma once\n#ifdef __cplusplus\n#\tinclude <A>\n#ifdef DFoo\n#define DBar 1\n#endif\nint g_A;\n#endif\n#include <B>\n"
					, "#pragma once\n#ifdef __cplusplus\n#include <A>\n#ifdef DFoo\n#\tdefine DBar 1\n#endif\nint g_A;\n#endif\n#include <B>\n"
				)
			;
			fg_ExpectFormat
				(
					"DirectiveIndentationWrapperAlternatives"
					, "#ifdef __cplusplus\n#include <A>\nint g_A;\n#else\n#include <B>\nint g_B;\n#endif\n"
					, "#ifdef __cplusplus\n#\tinclude <A>\nint g_A;\n#else\n#\tinclude <B>\nint g_B;\n#endif\n"
				)
			;
			fg_ExpectFormat("DirectiveIndentationBeforeCode", "#if DBits < 64\n#define DWide\n#endif\nint g_A;\n", "#if DBits < 64\n#\tdefine DWide\n#endif\nint g_A;\n");
			// The call holding more is the one that breaks. Behind an access it opens, and the
			// chain in front of it stays on the line; in front of the access its closing marker
			// would have the access behind it, so the line gives at the access instead.
			fSplit
				(
					"AccessBreaksWithTheLongerCall"
					, "void f()\n{\n\tp->f_Get(i)->f_Report(@, @);\n\tg(@, @).f_Call(a);\n}\n"
					, "void f()\n{\n\tp->f_Get(i)->f_Report\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t;\n\tg(@, @)\n\t\t.f_Call(a)\n\t;\n}\n"
				)
			;
			// A single member access whose call has to open anyway stays on the line with what
			// it is called on, and the call opens there.
			fSplit("MemberCallOpens", "void f()\n{\n\tg(a).f_Call(@, @);\n}\n", "void f()\n{\n\tg(a).f_Call\n\t\t(\n\t\t\t@\n\t\t\t, @\n\t\t)\n\t;\n}\n");
			// An operand in parentheses with a member access behind it stays whole and gives at
			// the access, whether it starts the operator chain's first line or follows an operator.
			CStr Parenthesised = "(@ % SecondOperandWithAVeryLongNameThatMakesTheParenthesisWideEnoughForThisCase).f_Dispatch()";
			fSplit
				(
					"ParenthesisedOperands"
					, "void f()\n{\n\tx = co_await\n\t\t(\n\t\t\t" + Parenthesised + "\n\t\t\t+ " + Parenthesised + "\n\t\t)\n\t;\n}\n"
					, "void f()\n{\n\tx = co_await\n\t\t(\n\t\t\t"
					+ Parenthesised.f_Replace(".f_Dispatch", "\n\t\t\t.f_Dispatch")
					+ "\n\t\t\t+ "
					+ Parenthesised.f_Replace(".f_Dispatch", "\n\t\t\t.f_Dispatch")
					+ "\n\t\t)\n\t;\n}\n"
				)
			;
			// A conditional groups to the right: one in a branch is a single operand of the
			// outer one, whole where it fits, and set off a level deeper where it gives.
			CStr Operand = "(@, Second, Third, Fourth, Fifth, Sixth, Seventh, Eighth, Ninth, Tenth).f_Get()";
			fSplit
				(
					"NestedConditional"
					, "void f()\n{\n\tauto Value = bFirst ? " + Operand + " : bSecond ? a : b;\n\tauto Other = bFirst ? (@).f_Get() : bSecond ? " + Operand + " : c;\n}\n"
					, "void f()\n{\n\tauto Value\n\t\t= bFirst\n\t\t? "
					+ Operand
					+ "\n\t\t: bSecond ? a : b\n\t;\n"
					"\tauto Other\n\t\t= bFirst\n\t\t? (@).f_Get()\n\t\t: bSecond\n\t\t\t? "
					+ Operand
					+ "\n\t\t\t: c\n\t;\n}\n"
				)
			;
			// A declaration's name that does not fit in front of its parameter list gives in
			// front of the '::' behind its class's template arguments, which stay whole.
			CStr Arguments = "t_CFormatter, t_SignBits, t_ExponentBits, t_MantissaBits, t_PaddingBits, t_CImplicitFloat, t_bDummyOptimize, t_CIntegerStorage, t_bReference";
			fg_ExpectFormat
				(
					"QualifiedNameGivesAtScope"
					, "template <typename t_C>\ninline_medium TCStrFormatType_Float<" + Arguments + ">::COptionsFloat::COptionsFloat()\n\t: COptions(COptionsStatic())\n{\n}\n"
					, "template <typename t_C>\ninline_medium TCStrFormatType_Float<" + Arguments + ">\n\t::COptionsFloat::COptionsFloat()\n\t: COptions(COptionsStatic())\n{\n}\n"
				)
			;
			// A requires expression's body stands one level in under its clause wherever the
			// source put the brace, in a declaration and in front of a body alike.
			fg_ExpectFormat
				(
					"RequirementsBodyPlaced"
					, "struct CA\n{\n\ttemplate <typename t_C>\n\taint f_A(t_C &_Args) const\n\t\trequires requires\n\t{\n\t\t_Args.f_Parse();\n\t}\n\t;\n\n"
					"\ttemplate <typename t_C>\n\taint f_B() const\n\t\trequires requires\n\t{\n\t\tg();\n\t}\n\t{\n\t}\n};\n"
					, "struct CA\n{\n\ttemplate <typename t_C>\n\taint f_A(t_C &_Args) const\n\t\trequires requires\n\t\t{\n\t\t\t_Args.f_Parse();\n\t\t}\n\t;\n\n"
					"\ttemplate <typename t_C>\n\taint f_B() const\n\t\trequires requires\n\t\t{\n\t\t\tg();\n\t\t}\n\t{\n\t}\n};\n"
				)
			;
			// An empty parameter list has nothing to open, even where the name fits in front of it.
			fg_ExpectFormat
				(
					"QualifiedNameEmptyParameters"
					, "template <typename t_C>\naint TCStrFormatType_Float<" + Arguments + ">::f_Get_aint_WWWWWWWW() const\n{\n}\n"
					, "template <typename t_C>\naint TCStrFormatType_Float<" + Arguments + ">\n\t::f_Get_aint_WWWWWWWW() const\n{\n}\n"
				)
			;
			// A parameter's default is given the way a statement's value is, from its own
			// line, and a conditional behind a template argument list is laid out the same
			// wherever the source broke it.
			CStr Default = "cHas_f_GetStringFormatType<t_CData, t_CFormatter> && cHas_f_CreateStringFormatter<t_CData, t_CFormatter>";
			CStr Expected = "template\n<\n\ttypename t_CData\n\t, EType t_Type\n\t= "
				+ Default
				+ "\n\t? EType::mc_InlineCreateStringFormatter\n"
				"\t: cHas_f_Format<t_CData> ? EType::mc_Inline : EType::mc_FormatterTemplate\n>\nstruct CHelper;\n"
			;
			fg_ExpectFormat
				(
					"DefaultConditional"
					, "template <typename t_CData, EType t_Type = "
					+ Default
					+ " ? EType::mc_InlineCreateStringFormatter : cHas_f_Format<t_CData> ? EType::mc_Inline "
					": EType::mc_FormatterTemplate>\nstruct CHelper;\n"
					, Expected
				)
			;
			fg_ExpectFormat
				(
					"DefaultConditionalWrittenSplit"
					, "template\n<\n\ttypename t_CData\n\t, EType t_Type = "
					+ Default
					+ "\n\t? EType::mc_InlineCreateStringFormatter\n\t: cHas_f_Format<t_CData>\n"
					"\t? EType::mc_Inline\n\t: EType::mc_FormatterTemplate\n>\nstruct CHelper;\n"
					, Expected
				)
			;
			// A parenthesised operand stands apart from the '?' and ':' like any other,
			// which is what lets a conditional written across lines be joined. A range
			// with an unsettled gap cannot be measured, and is not broken at its
			// operators either.
			fg_ExpectFormat("ConditionalJoined", "void f()\n{\n\tx = a ?\n\t\t(b) :\n\t\t(c);\n}\n", "void f()\n{\n\tx = a ? (b) : (c);\n}\n");
			CStr Unsettled = "void f()\n{\n\ty = a + g(b &\n\t\tc) + d;\n}\n";
			fg_ExpectFormat("UnsettledOperators", Unsettled, Unsettled);
			// An operand too long for its line gives at the operators it holds before its
			// scopes open, and they line up with the looser one it was cut at. One that
			// holds a lambda keeps the operator that takes it.
			fSplit
				(
					"OperandOperators"
					, "void f()\n{\n\tg(@) + h(@) + k(1) > Actor / [&](int _A)\n\t\t{\n\t\t\tx = _A;\n\t\t}\n\t;\n}\n"
					, "void f()\n{\n\tg(@)\n\t\t+ h(@)\n\t\t+ k(1)\n\t\t> Actor / [&](int _A)\n\t\t{\n\t\t\tx = _A;\n\t\t}\n\t;\n}\n"
				)
			;
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
	}
}
