// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Test_Malterlib_Develop_CodeFormatting.h"

namespace NMib::NDevelop::NCodeFormattingTest
{
	void fg_TestKeptLineBreaks()
	{
		DMibTestCategory("Kept")
		{
			// A comment or a directive inside fixes the construct's line structure.
			fg_ExpectFormat("Comment", "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n", "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n");
			// A line comment ends its line, and the lines around it are laid out as
			// usual: the construct is split, and each line takes its indentation.
			fg_ExpectFormat("CommentIndent", "void f()\n{\n\tg\n\t\t(\n\t\t\t\t5 // why\n\t\t, 6\n\t\t\t)\n\t;\n}\n", "void f()\n{\n\tg\n\t\t(\n\t\t\t5 // why\n\t\t\t, 6\n\t\t)\n\t;\n}\n");
			// A member chain behind a call that holds a body resumes under the call's
			// closing marker. Where it does not fit it is broken at every member, each
			// under that marker, however the source had broken or joined it.
			CStr Chain;
			for (umint i = 0; i < 16; ++i)
				Chain += ".f_CallSync()";

			CStr Broken;
			for (umint i = 0; i < 16; ++i)
				Broken += "\t.f_CallSync()\n";

			fg_ExpectFormat
				(
					"ResumedChain"
					, CStr("void f()\n{\n\t(\n\t\tg_D / [&]\n\t\t{\n\t\t\tg();\n\t\t}\n\t)\n\t.f_CallSync()\n\t\t.f_CallSync()\n\t\t@;\n}\n").f_Replace("@", Chain)
					, CStr("void f()\n{\n\t(\n\t\tg_D / [&]\n\t\t{\n\t\t\tg();\n\t\t}\n\t)\n\t.f_CallSync()\n\t.f_CallSync()\n@}\n")
					.f_Replace("@", Broken.f_Left(Broken.f_GetLen() - 1) + ";\n")
				)
			;
			// An operator behind an opened leading parenthesis stands under its closing
			// marker, since the statement has no continuation level; behind a named
			// call's it stands under that marker too, which is one level in.
			fg_ExpectFormat
				(
					"LeadingParenOperator"
					, "void f()\n{\n\t(\n\t\tg_D / [&]\n\t\t{\n\t\t\tg();\n\t\t}\n\t)\n\t\t> g_Discard;\n"
					"\tfg_X\n\t\t(\n\t\t\tg_D / [&]\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t\t)\n\t\t\t> g_Discard;\n}\n"
					, "void f()\n{\n\t(\n\t\tg_D / [&]\n\t\t{\n\t\t\tg();\n\t\t}\n\t)\n\t> g_Discard;\n"
					"\tfg_X\n\t\t(\n\t\t\tg_D / [&]\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t\t)\n\t\t> g_Discard\n\t;\n}\n"
				)
			;
			// A statement split at the parenthesis it opens with has no continuation
			// level, so its terminator ends its last line rather than standing among its
			// lines as one more of them. One that only starts with a parenthesis has.
			fg_ExpectFormat
				(
					"LeadingParen"
					, "void f()\n{\n\t(\n\t\tg_Dispatch / []() -> int\n\t\t{\n\t\t\treturn 0;\n\t\t}\n\t)\n\t.f_CallSync()\n\t;\n"
					"\t(*fLocked)() > [&]\n\t\t{\n\t\t\tg();\n\t\t}\n\t;\n}\n"
					, "void f()\n{\n\t(\n\t\tg_Dispatch / []() -> int\n\t\t{\n\t\t\treturn 0;\n\t\t}\n\t)\n\t.f_CallSync();\n"
					"\t(*fLocked)() > [&]\n\t\t{\n\t\t\tg();\n\t\t}\n\t;\n}\n"
				)
			;
			// An operator's explicit template arguments are a template argument list, apart from its name.
			fg_ExpectFormat
				(
					"OperatorTemplateArguments"
					, "template CFoo &CFoo::operator = < CStr const & > (CStr const &);\ntemplate void CFoo::operator ()<int>(int);\n"
					"struct C\n{\n\tbool operator < (C const &) const;\n};\n"
					, "template CFoo &CFoo::operator = <CStr const &>(CStr const &);\ntemplate void CFoo::operator ()<int>(int);\n"
					"struct C\n{\n\tbool operator < (C const &) const;\n};\n"
				)
			;
			// Behind a parameter list or a qualifier 'throw' is an exception specification, spelled like 'noexcept';
			// a throw expression keeps its keyword apart from the parenthesis.
			fg_ExpectFormat
				(
					"ThrowSpecification"
					, "extern \"C\" void *f(size_t _Size) throw ();\nstruct C\n{\n\tvoid g() const throw ();\n};\n\nvoid h()\n{\n\tthrow (b);\n}\n"
					, "extern \"C\" void *f(size_t _Size) throw();\nstruct C\n{\n\tvoid g() const throw();\n};\n\nvoid h()\n{\n\tthrow (b);\n}\n"
				)
			;
			// A declaration's head cut by a comment or a directive keeps its words at the statement's level, the
			// parameter list one level in; a cut in front of the list leaves the list where it is.
			fg_ExpectFormat
				(
					"DeclarationHeadCut"
					, "bool // Comment\n\tCFoo::f_Process\n\t(\n\t\tint _A\n\t\t, int _B // Second\n\t)\n{\n}\n\nint\n#ifdef _MSC_VER\n\t__cdecl\n#endif\n\tmain()\n{\n}\n"
					, "bool // Comment\nCFoo::f_Process\n\t(\n\t\tint _A\n\t\t, int _B // Second\n\t)\n{\n}\n\nint\n#ifdef _MSC_VER\n__cdecl\n#endif\nmain()\n{\n}\n"
				)
			;
			// Behind an operator or a compound assignment no declaration starts, so '*' and '&' are binary there.
			fg_ExpectFormat
				(
					"BinaryBehindOperator"
					, "void f()\n{\n\tm_Sum1 += m_Sum0 - m_Count*(_Out + gc_Offset);\n\tx += y&z;\n\tg(CFoo *p, CBar &q);\n}\n"
					, "void f()\n{\n\tm_Sum1 += m_Sum0 - m_Count * (_Out + gc_Offset);\n\tx += y & z;\n\tg(CFoo *p, CBar &q);\n}\n"
				)
			;
			// A function type's pointer return type hugs its parameter list as it hugs a pointer to function's
			// declarator; a conversion operator's name and an expression keep their spelling.
			fg_ExpectFormat
				(
					"FunctionTypePointerReturn"
					, "struct C\n{\n\tusing FA = void * (void const *_pImpl);\n\tusing FB = TCFunction<CFoo * (int)>;\n\tusing FC = void * (*)(int);\n"
					"\toperator ch8 const * () const;\n\tvoid f()\n\t{\n\t\ta = b * (c + d);\n\t\te = (int) * (pF);\n\t}\n};\n"
					, "struct C\n{\n\tusing FA = void *(void const *_pImpl);\n\tusing FB = TCFunction<CFoo *(int)>;\n\tusing FC = void *(*)(int);\n"
					"\toperator ch8 const * () const;\n\tvoid f()\n\t{\n\t\ta = b * (c + d);\n\t\te = (int)*(pF);\n\t}\n};\n"
				)
			;
			// A pointer to function returned behind a trailing return type's arrow is declared as anywhere
			// else, where a member behind a member access arrow is called.
			fg_ExpectFormat
				(
					"TrailingReturnFunctionPointer"
					, "auto fg_A(int _Signal, void (*_fHandler)(int)) -> void(*)(int);\nvoid f()\n{\n\tpObject->m_fCall (&CFoo::f_Get);\n}\n"
					, "auto fg_A(int _Signal, void (*_fHandler)(int)) -> void (*)(int);\nvoid f()\n{\n\tpObject->m_fCall(&CFoo::f_Get);\n}\n"
				)
			;
			// A destructor behind another function's body is a declaration of its own.
			fg_ExpectFormat
				(
					"DestructorBehindBody"
					, "struct CData\n{\n\tCData() {m_Valid = true;}\n\t~CData() {m_Valid = false;}\n};\n"
					, "struct CData\n{\n\tCData()\n\t{\n\t\tm_Valid = true;\n\t}\n\n\t~CData()\n\t{\n\t\tm_Valid = false;\n\t}\n};\n"
				)
			;
			// A return type that 'typename' introduces moves behind the parameter list like any other, also
			// from a head written across lines; one with a gap nothing settles stays.
			CStr AggregateType = "TCAVLTreeAggregate<t_pLinkMember, t_CCompare, t_CAllocator, t_COverrideNodeType>";
			fg_ExpectFormat
				(
					"TypenameTrailingReturn"
					, "namespace NA\n{\n\tinline_small typename "
					+ AggregateType
					+ "\n\t\t::CNode *"
					+ AggregateType
					+ "::TCIterator<t_Depth>::f_GetCurrent() const\n"
					"\t{\n\t\treturn nullptr;\n\t}\n}\n"
					, "namespace NA\n{\n\tinline_small auto "
					+ AggregateType
					+ "::TCIterator<t_Depth>::f_GetCurrent() const\n\t\t-> typename "
					+ AggregateType
					+ "::CNode *\n\t{\n\t\treturn nullptr;\n\t}\n}\n"
					, false
				)
			;
			// A guard's head cut by a conditional continues in the other branch one level in, and the body behind
			// it opens where the guard stands.
			fg_ExpectFormat
				(
					"GuardHeadAcrossConditional"
					, "void f()\n{\n\tfor\n#if 1\n\t\t(int i = 0; i < 2; ++i)\n#else\n\t\t(int i = 0; i < 3; ++i)\n#endif\n\t{\n\t\tg(i);\n\t}\n}\n"
					, "void f()\n{\n\tfor\n#if 1\n\t\t(int i = 0; i < 2; ++i)\n#else\n\t\t(int i = 0; i < 3; ++i)\n#endif\n\t{\n\t\tg(i);\n\t}\n}\n"
				)
			;
			// Behind a parenthesis grouping an expression, or in front of what only an operand starts with,
			// '&' and '*' are binary; behind a parenthesis holding only a name they keep their spelling.
			fg_ExpectFormat
				(
					"BinaryBehindGroup"
					, "void f()\n{\n\tr = ((ptr + n + offset)&~align) - offset;\n\ts = (Len + 1)*2;\n\tg(a, (task_info_t)&Info);\n}\n"
					"struct C\n{\n\tvoid f() &;\n};\n"
					, "void f()\n{\n\tr = ((ptr + n + offset) & ~align) - offset;\n\ts = (Len + 1) * 2;\n\tg(a, (task_info_t)&Info);\n}\n\n"
					"struct C\n{\n\tvoid f() &;\n};\n"
				)
			;
			// Each of a conditional's heads over one body spells its own type.
			fg_ExpectFormat
				(
					"ConditionalHeads"
					, "#if DA\nextern \"C\" void * fA(int _Size)\n#else\nextern \"C\" void * fB(int _Size)\n#endif\n{\n\treturn nullptr;\n}\n"
					, "#if DA\nextern \"C\" void *fA(int _Size)\n#else\nextern \"C\" void *fB(int _Size)\n#endif\n{\n\treturn nullptr;\n}\n"
				)
			;
			// A linkage specification is one of a declaration's specifiers, and a parameter list behind a name is
			// no cast, whatever it holds.
			fg_ExpectFormat
				(
					"LinkageDeclaration"
					, "extern \"C\" void * _ZdaPv(void *)__attribute__((weak_import));\nextern \"C\" char**    __argv;\n"
					, "extern \"C\" void *_ZdaPv(void *) __attribute__((weak_import));\nextern \"C\" char **__argv;\n"
				)
			;
			// A block pointer's declarator is spelled like a pointer to function's, and '^' anywhere else is the operator.
			fg_ExpectFormat
				(
					"BlockPointerDeclarator"
					, "void f(void(^ fReport)(void *_pMemory));\nvoid g()\n{\n\ta = b ^ c;\n\td = (b) ^ c;\n}\n"
					, "void f(void (^fReport)(void *_pMemory));\nvoid g()\n{\n\ta = b ^ c;\n\td = (b) ^ c;\n}\n"
				)
			;
			// A '*' or '&' behind a cast is unary, behind a cast behind another cast as well; behind a call it
			// is the binary operator.
			fg_ExpectFormat
				(
					"UnaryBehindCast"
					, "void f()\n{\n\ta = (umint)(void *) * pBuffer;\n\tb = (uint64)(umint) & Value;\n\tc = (void *) *pB;\n\td = f(a)(b) * c;\n}\n"
					, "void f()\n{\n\ta = (umint)(void *)*pBuffer;\n\tb = (uint64)(umint)&Value;\n\tc = (void *)*pB;\n\td = f(a)(b) * c;\n}\n"
				)
			;
			// A block comment ending its line is a line comment, and one that makes the line it trails too long moves
			// above it; one documenting what it trails stays, written as a line comment with its marker, and so does one
			// behind code too long by itself.
			CStr CommentWords;
			for (umint i = 0; i < 36; ++i)
				CommentWords += "Word ";

			CommentWords = CommentWords.f_Trim();
			fg_ExpectFormat
				(
					"TrailingCommentMoves"
					, "void f()\n{\n\tint a = g(1); // "
					+ CommentWords
					+ "\n\tint b = g(1); /* "
					+ CommentWords
					+ " */\n\tint c = g(1); ///< "
					+ CommentWords
					+ "\n"
					"\tint d = g(1); /*!< "
					+ CommentWords
					+ " */\n\tg(); /* Checker */\n\th(); /*a*/ k();\n}\n"
					, "void f()\n{\n\t// "
					+ CommentWords
					+ "\n\tint a = g(1);\n\t// "
					+ CommentWords
					+ "\n\tint b = g(1);\n\tint c = g(1); ///< "
					+ CommentWords
					+ "\n"
					"\tint d = g(1); //!< "
					+ CommentWords
					+ "\n\tg(); // Checker\n\th();\n\t/*a*/ k();\n}\n"
					, false
				)
			;
			// A NOLINT comment suppresses the diagnostics of its own line, so it stays there, and the code on that line, or
			// on the line below a NOLINTNEXTLINE, is neither split nor joined with another line's.
			{
				CStr Arguments;
				for (umint i = 0; i < 30; ++i)
					Arguments += "{}Argument{}"_f << (i ? ", " : "") << i;

				CStr Source = "void f()\n{\n\tint a = g(1); // NOLINT(bugprone-xyz) "
					+ CommentWords
					+ "\n\tfg_Call("
					+ Arguments
					+ "); // NOLINT\n"
					"\t// NOLINTNEXTLINE\n\tfg_Call("
					+ Arguments
					+ ");\n}\n"
				;
				fg_ExpectFormat("NoLintStaysOnItsLine", Source, Source);

				// A conversion would take code off the suppressed line too: the return type moved behind the parameters, the
				// braces dropped around a guarded statement.
				CStr Converted = "CLongResultType fg_Test(" + Arguments + "); // NOLINT\nvoid f()\n{\n\tif (a) // NOLINT\n\t{\n\t\tg();\n\t}\n}\n";
				fg_ExpectFormat("NoLintKeepsConversions", Converted, Converted);

				// A comment too long behind the line a NOLINTNEXTLINE suppresses stays there rather than taking the suppression.
				CStr Lifted = "void f()\n{\n\t// NOLINTNEXTLINE\n\tint a = g(1); // " + CommentWords + " " + CommentWords + "\n}\n";
				fg_ExpectFormat("NoLintNextLineKeepsComment", Lifted, Lifted);

				// Nor does the line a NOLINTNEXTLINE suppresses go, an empty statement's or a comment's closing marker's, which
				// would move the suppression onto the code below.
				CStr Targets = "void f()\n{\n\tg(); // NOLINTNEXTLINE\n\t;\n\th();\n\ti(); /* a\n\t   NOLINTNEXTLINE\n\t*/\n\tj();\n}\n";
				fg_ExpectFormat("NoLintNextLineKeepsTarget", Targets, Targets);

				// Nor are braces added around a statement guarded on such a line, which would take it off the line.
				CStr Guarded = "void f()\n{\n\t// NOLINTNEXTLINE\n\tif (a) fg_Call(" + Arguments + ");\n}\n";
				fg_ExpectFormat("NoLintNextLineKeepsGuarded", Guarded, Guarded);

				// Nor does a qualifier move onto it.
				fg_ExpectFormat("NoLintNextLineKeepsQualifier", "// NOLINTNEXTLINE\nconst\nint x = 2;\n", "// NOLINTNEXTLINE\nconst\nint x = 2;\n");

				// A fallthrough annotation too long behind its statement stays there, the only place GCC reads it.
				CStr Fallthrough = "void f(int c)\n{\n\tswitch (c)\n\t{\n\tcase 1:\n\t\tg(1); /* falls through " + CommentWords + " */\n\tcase 2: break;\n\t}\n}\n";
				fg_ExpectFormat("FallthroughCommentStays", Fallthrough, Fallthrough);
				// So does one continuing a trailing comment.
				CStr ContinuedFallthrough = "void f(int c)\n{\n\tswitch (c)\n\t{\n\tcase 1:\n\t\tg(1); // " + CommentWords + "\n\t\t\t  // falls through\n\tcase 2: break;\n\t}\n}\n";
				fg_ExpectFormat("ContinuedFallthroughCommentStays", ContinuedFallthrough, ContinuedFallthrough);
				// A documentation comment without '<' documents the declaration behind it, and stays in front of that, written as
				// a line comment.
				fg_ExpectFormat
					(
						"ForwardDocumentationStays"
						, "struct C\n{\n\tint a; /** " + CommentWords + " */\n\tint b;\n};\n"
						, "struct C\n{\n\tint a; /// " + CommentWords + "\n\tint b;\n};\n"
						, false
					)
				;
			}
			fg_ExpectFormat("TrigraphSpliceKeepsBlockComment", "int a; /* ?" "?/ */\nint b;\n", "int a; /* ?" "?/ */\nint b;\n");
			// A range written across lines is not joined onto the line of a comment behind it that would then be too long.
			CStr MediumWords;
			for (umint i = 0; i < 31; ++i)
				MediumWords += "Word ";

			MediumWords = MediumWords.f_Trim();
			fg_ExpectFormat
				(
					"TrailingCommentKeepsSplit"
					, "void f()\n{\n\tfg_Error\n\t\t(\n\t\t\t\"Message {}\"_f\n\t\t\t<< _Value // " + MediumWords + "\n\t\t)\n\t;\n}\n"
					, "void f()\n{\n\tfg_Error\n\t\t(\n\t\t\t\"Message {}\"_f\n\t\t\t<< _Value // " + MediumWords + "\n\t\t)\n\t;\n}\n"
				)
			;
			// A comment behind a statement too long for its line stays behind it once it is split, on the
			// terminator's line, also where it makes that line too long: only a line starting a statement is
			// one it moves above.
			CStr LongArgument;
			CStr LongComment;
			for (umint i = 0; i < 100; ++i)
				LongArgument += "a";

			for (umint i = 0; i < 185; ++i)
				LongComment += "c";

			fg_ExpectFormat
				(
					"TrailingCommentBehindSplit"
					, "void f()\n{\n\tg(" + LongArgument + ", " + LongArgument + "); // " + LongComment + "\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\t" + LongArgument + "\n\t\t\t, " + LongArgument + "\n\t\t)\n\t; // " + LongComment + "\n}\n"
				)
			;
			// A statement the layout moves in is measured where it goes: the comment fits behind a namespace's member
			// written at the namespace's own level, but not one level in, where it moves above it.
			CStr NamespaceComment;
			for (umint i = 0; i < 175; ++i)
				NamespaceComment += "c";

			fg_ExpectFormat("TrailingCommentMovedIn", "namespace N\n{\nvoid f(); // " + NamespaceComment + "\n}\n", "namespace N\n{\n\t// " + NamespaceComment + "\n\tvoid f();\n}\n", false);
			// The terminator behind a braced initializer the layout splits off is no line starting a statement.
			fg_ExpectFormat
				(
					"TrailingCommentBehindInitializer"
					, "struct CA\n{\n\tCB m_X{1, // A\n\t\t2}; // " + LongComment + "\n};\n"
					, "struct CA\n{\n\tCB m_X\n\t\t{\n\t\t\t1 // A\n\t\t\t, 2\n\t\t}\n\t; // " + LongComment + "\n};\n"
					, false
				)
			;
			// An element a block comment names leads its line with the comment, behind its comma, and a block comment in front
			// of a comma trails the element in front of it, staying behind it where the comma moves to the next line.
			fg_ExpectFormat
				(
					"ElementNamedByBlockComment"
					, "int g_A[] = {\n    gA,    gB,\n    /*x=*/gC, gD,\n};\n"
					, "int g_A[] =\n\t{\n\t\tgA\n\t\t, gB\n\t\t, /*x=*/gC\n\t\t, gD\n\t}\n;\n"
					, false
				)
			;
			fg_ExpectFormat
				(
					"ElementTrailedByBlockComment"
					, "int g_A[] = {\n    gA,    gB,\n    gC, gD /* d */,\n    gE, gF,\n};\n"
					, "int g_A[] =\n\t{\n\t\tgA\n\t\t, gB\n\t\t, gC\n\t\t, gD /* d */\n\t\t, gE\n\t\t, gF\n\t}\n;\n"
					, false
				)
			;
			// An element a block comment names on its line stands where the list's other elements do, its comment in front.
			fg_ExpectFormat
				(
					"NestedElementNamedByBlockComment"
					, "void f() {\n  A = {\n      {\n         gA,\n         /*B=*/gB,\n      },\n  };\n}\n"
					, "void f()\n{\n\tA =\n\t\t{\n\t\t\t{\n\t\t\t\tgA\n\t\t\t\t, /*B=*/gB\n\t\t\t}\n\t\t}\n\t;\n}\n"
					, false
				)
			;
			// An element written across lines takes the comma ending the line in front, and is laid out as any other.
			{
				CStr First;
				CStr Second;
				for (umint i = 0; i < 90; ++i)
				{
					First += "a";
					Second += "b";
				}

				fg_ExpectFormat
					(
						"CommaInFrontOfElementAcrossLines"
						, "int g_A[] = {\n    fg_A(1),\n    fg_B(" + First + ",\n         " + Second + "),\n    fg_C(4),\n};\n"
						, "int g_A[] =\n\t{\n\t\tfg_A(1)\n\t\t, fg_B\n\t\t(\n\t\t\t" + First + "\n\t\t\t, " + Second + "\n\t\t)\n\t\t, fg_C(4)\n\t}\n;\n"
						, false
					)
				;
			}
			// A braced element written open is laid out as one too long for its line is, its comma alone on the line in front.
			CStr BracedElements = "int g_A[][2] =\n\t{\n\t\t{\n\t\t\t1\n\t\t\t, 2\n\t\t}\n\t\t,\n\t\t{\n\t\t\t3\n\t\t\t, 4\n\t\t}\n\t}\n;\n";
			fg_ExpectFormat("BracedElementsAcrossLines", BracedElements, BracedElements);
			// A list written open is opened wherever its closing brace was written, and so is a list holding one, in a call
			// as anywhere: each is laid out as one too long for its line is. Rows written on a line join where they fit.
			fg_ExpectFormat("ListClosedOnElementLine", "void f()\n{\n\tint a[] = {\n\t  1,\n\t  2};\n}\n", "void f()\n{\n\tint a[] =\n\t\t{\n\t\t\t1\n\t\t\t, 2\n\t\t}\n\t;\n}\n");
			fg_ExpectFormat
				(
					"ListArgumentWrittenOpen"
					, "void f()\n{\n\tfoo(a, {\n\t  1,\n\t  2});\n}\n"
					, "void f()\n{\n\tfoo\n\t\t(\n\t\t\ta\n\t\t\t,\n\t\t\t{\n\t\t\t\t1\n\t\t\t\t, 2\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			fg_ExpectFormat
				(
					"NestedRowsWrittenOpen"
					, "int c[] = {\n  {{1, 2},\n   {3, 4}},\n  {{1, 2},\n   {3, 4}}};\n"
					, "int c[] =\n\t{\n\t\t{{1, 2}, {3, 4}}\n\t\t, {{1, 2}, {3, 4}}\n\t}\n;\n"
				)
			;
			// What a list written open ends stays on its line where it fits, and the list opens below it.
			fg_ExpectFormat
				(
					"ListBehindHead"
					, "void f()\n{\n\t_Builder << types::b_dbpointer {\n\t\ta,\n\t\tb\n\t};\n\tA = B[1] = {\n\t\t1\n\t};\n}\n"
					, "void f()\n{\n\t_Builder << types::b_dbpointer\n\t\t{\n\t\t\ta\n\t\t\t, b\n\t\t}\n\t;\n\tA = B[1] =\n\t\t{\n\t\t\t1\n\t\t}\n\t;\n}\n"
				)
			;
			// One with nothing in it yet stays open.
			fg_ExpectFormat
				(
					"EmptyListWrittenOpen"
					, "void f()\n{\n\tint a[] = {\n\t};\n\tg(a, {\n\t});\n}\n"
					, "void f()\n{\n\tint a[] =\n\t\t{\n\t\t}\n\t;\n\tg\n\t\t(\n\t\t\ta\n\t\t\t,\n\t\t\t{\n\t\t\t}\n\t\t)\n\t;\n}\n"
				)
			;
			// A block comment on a line of its own ends its line as a line comment does, and the construct around it is laid
			// out; a comment line in front of a closing marker stands among the elements.
			fg_ExpectFormat
				(
					"BlockCommentLineEndsLine"
					, "void f()\n{\n  g(a,\n    /* Block */\n    b);\n  g(a,\n    b\n    // d\n  );\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\ta\n\t\t\t/* Block */\n\t\t\t, b\n\t\t)\n\t;\n\tg\n\t\t(\n\t\t\ta\n\t\t\t, b\n\t\t\t// d\n\t\t)\n\t;\n}\n"
					, false
				)
			;
			// A comment continuing the one trailing an element stays with it, in front of a closing marker too.
			{
				CStr Continued = "struct C\n{\n\tvoid g\n\t\t(\n\t\t\tint _A // Leave empty\n\t\t\t\t   // applications.\n\t\t)\n\t;\n};\n";
				fg_ExpectFormat("TrailingCommentContinuedBeforeCloser", Continued, Continued);
			}
			// The rows of a list a block comment names, in a block a conditional cuts, are laid out with their names in
			// front, behind their commas, and the list a conditional's other branch starts with hugs its elements.
			fg_ExpectFormat
				(
					"NamedRowsInOpaqueBlock"
					, "namespace N\n{\nint g_A[2][2] = {\n/*A*/{1, 2},\n/*B*/{3, 4}\n};\nchar const *g_s[2] =\n#if A\n{{\"a\"}, {\"b\"}};\n#else\n{\"a\", \"b\"};\n#endif\n}\n"
					, "namespace N\n{\n\tint g_A[2][2] =\n\t\t{\n\t\t\t/*A*/{1, 2}\n\t\t\t, /*B*/{3, 4}\n\t\t}\n\t;\n\tchar const *g_s[2] =\n#if A\n\t{{\"a\"}, {\"b\"}};\n"
					"#else\n\t{\"a\", \"b\"};\n#endif\n}\n"
				)
			;
			// A string the layout joins onto a designator's '=' continues one level in, as one written there does.
			fg_ExpectFormat
				(
					"JoinedValueStringContinued"
					, "constexpr CCase g_Case{\n    .to      = 18,\n    .expected =\n        \"aaaaaaaaaaaaaaaaaa\"\n        \"bbbbbbbbbbbbbbbbbbbb\"};\n"
					, "constexpr CCase g_Case\n\t{\n\t\t.to = 18\n\t\t, .expected = \"aaaaaaaaaaaaaaaaaa\"\n\t\t\t\"bbbbbbbbbbbbbbbbbbbb\"\n\t}\n;\n"
				)
			;
			// A block comment trailing an operand in front of a separator goes with the operand, and the construct around it
			// is laid out.
			fg_ExpectFormat
				(
					"TrailingBlockCommentInRow"
					, "int g_A[][3] = {\n    {1, 2 /* size */,\n     3}};\nbool f(int a /*=0*/,\n       int b /*=0*/)\n{\n}\n"
					, "int g_A[][3] =\n\t{\n\t\t{1, 2 /* size */, 3}\n\t}\n;\nbool f(int a /*=0*/, int b /*=0*/)\n{\n}\n"
				)
			;
			// An inline namespace is a namespace whatever its name is.
			fg_ExpectFormat
				(
					"InlineNamespaceMacroName"
					, "namespace a {\n\tinline namespace DVersion {\n\t\tint n = 2;\n\t}\n}\n"
					, "namespace a\n{\n\tinline namespace DVersion\n\t{\n\t\tint n = 2;\n\t}\n}\n"
				)
			;
			fg_ExpectFormat
				(
					"ContinuedBehindMacro"
					, "void f()\n{\n  g(log,\n    \"a %\" PRId32\n    \" b\",\n    c);\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\tlog\n\t\t\t, \"a %\" PRId32\n\t\t\t\" b\"\n\t\t\t, c\n\t\t)\n\t;\n}\n"
				)
			;
			// A requires clause on a line of its own behind a template header stands at the header's level or one level in.
			fg_ExpectFormat
				(
					"RequiresBehindHeaderLevelled"
					, "struct A\n{\n  template <class T>\n    requires cA<T>\n  void f();\n};\n"
					, "struct A\n{\n\ttemplate <class T>\n\t\trequires cA<T>\n\tvoid f();\n};\n"
				)
			;
			// A statement whose strings continue over lines gives at them, its operators between them staying on their
			// lines and each in front of the first or behind the last starting one of its own.
			fg_ExpectFormat
				(
					"StatementStringsThenOperators"
					, "void f() {\n  os << \"a\"\n        \"b \" + x +\n        \"c\"\n     << c << d;\n}\n"
					, "void f()\n{\n\tos\n\t\t<< \"a\"\n\t\t\"b \" + x + \"c\"\n\t\t<< c\n\t\t<< d\n\t;\n}\n"
				)
			;
			// A row of a list a conditional keeps the lines of is laid out as any element is, split where it is too long.
			{
				CStr Long;
				for (umint i = 0; i < 100; ++i)
					Long += "x";

				fg_ExpectFormat
					(
						"KeptListLongRow"
						, "int g_A[] = {\n    {a" + Long + ",\n      b" + Long + ",\n      c},\n#ifdef D\n    {d,\n      e},\n#endif\n};\n"
						, "int g_A[] =\n\t{\n\t\t{\n\t\t\ta" + Long + "\n\t\t\t, b" + Long + "\n\t\t\t, c\n\t\t}\n#ifdef D\n\t\t, {d, e}\n#endif\n\t}\n;\n"
						, false
					)
				;
			}
			// The variables a type's definition declares, behind specifiers in front of its class key, are laid out behind
			// its closing brace.
			fg_ExpectFormat
				(
					"SpecifiedDefinitionDeclarators"
					, "void f()\n{\n  static const struct {\n    int a;\n  } g_A[] = {\n      // A\n      {1},\n      {2},\n  };\n}\n"
					, "void f()\n{\n\tstatic const struct\n\t{\n\t\tint a;\n\t} g_A[] =\n\t\t{\n\t\t\t// A\n\t\t\t{1}\n\t\t\t, {2}\n\t\t}\n\t;\n}\n"
					, false
				)
			;
			// Two words a line break stands between join with one space.
			fg_ExpectFormat
				(
					"WordsJoined"
					, "struct CNode\n{\n\tCLongTypeName\n\t  m_Value;\n};\nvoid f()\n{\n\tunsigned\n\t\tint Value;\n}\n"
					, "struct CNode\n{\n\tCLongTypeName m_Value;\n};\n\nvoid f()\n{\n\tunsigned int Value;\n}\n"
				)
			;
			// A blank line between its elements sets them apart, on either side of the comma.
			fg_ExpectFormat("ListBlankLines", "int g_A[] =\n\t{\n\t\t1\n\t\t, 2\n\n\t\t, 3,\n\n\t\t4\n\t}\n;\n", "int g_A[] =\n\t{\n\t\t1\n\t\t, 2\n\n\t\t, 3\n\n\t\t, 4\n\t}\n;\n");
			// An element's template argument list joins with the empty argument list the split put below it, or not at all.
			{
				CStr Name;
				for (umint i = 0; i < 168; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"TemplateArgumentsBeforeEmptyCall"
						, "void f()\n{\n\tx = {g.f_Get<C" + Name + ">()};\n}\n"
						, "void f()\n{\n\tx =\n\t\t{\n\t\t\tg.f_Get\n\t\t\t<\n\t\t\t\tC" + Name + "\n\t\t\t>\n\t\t\t()\n\t\t}\n\t;\n}\n"
					)
				;
			}
			// So does one with the rest of a qualified name below it.
			{
				CStr Name;
				for (umint i = 0; i < 160; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"TemplateArgumentsBeforeQualification"
						, "struct CA\n{\n\ttemplate <typename t_C,\n\t\ttypename TCEnableIf<!c"
						+ Name
						+ "<t_C, CSink>::mc_Value,\n\t\t\tint>::CType = 0>\n"
						"\tCA(t_C const & /* Ignored */) {} // Note\n};\n"
						, "struct CA\n{\n\ttemplate\n\t<\n\t\ttypename t_C\n\t\t, typename TCEnableIf\n\t\t<\n\t\t\t!c"
						+ Name
						+ "\n\t\t\t<\n\t\t\t\tt_C\n\t\t\t\t, CSink\n"
						"\t\t\t>\n\t\t\t::mc_Value\n\t\t\t, int\n\t\t>\n\t\t::CType = 0\n\t>\n\tCA(t_C const & /* Ignored */)\n\t{\n\t} // Note\n};\n"
					)
				;
			}
			// A lambda whose comma moves in front of it has its head split from the comma's line, and its body at its level.
			{
				CStr Type;
				for (umint i = 0; i < 90; ++i)
					Type += "a";

				fg_ExpectFormat
					(
						"LambdaBehindMovedComma"
						, "CFoo g_A\n{\n\t[](int _A) {\n\t\treturn _A;\n\t},\n\t[](CLong" + Type + " const &_A,\n\t\tCLong" + Type + " const &_B) {\n\t\treturn 0;\n\t}\n};\n"
						, "CFoo g_A\n\t{\n\t\t[](int _A)\n\t\t{\n\t\t\treturn _A;\n\t\t}\n\t\t, []\n\t\t(\n\t\t\tCLong"
						+ Type
						+ " const &_A\n\t\t\t, CLong"
						+ Type
						+ " const &_B\n\t\t)\n\t\t{\n\t\t\treturn 0;\n\t\t}\n\t}\n;\n"
					)
				;
			}
			// A link of a chain on what a call yields keeps the line the chain's layout gave it.
			{
				CStr Name;
				for (umint i = 0; i < 150; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"YieldedLinkKeepsLine"
						, "void f()\n{\n\treturn fg_Cost({g.f_Id(), CA, {g.f_Types()[0]->f_With(fg_Float" + Name + "(p->f_Context()))}}, Kind);\n}\n"
						, "void f()\n{\n\treturn fg_Cost\n\t\t(\n\t\t\t{\n\t\t\t\tg.f_Id()\n\t\t\t\t, CA\n\t\t\t\t,\n\t\t\t\t{\n\t\t\t\t\tg.f_Types()\n\t\t\t\t\t[0]\n"
						"\t\t\t\t\t->f_With\n\t\t\t\t\t(\n\t\t\t\t\t\tfg_Float"
						+ Name
						+ "\n\t\t\t\t\t\t(\n\t\t\t\t\t\t\tp->f_Context()\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n"
						"\t\t\t\t}\n\t\t\t}\n\t\t\t, Kind\n\t\t)\n\t;\n}\n"
					)
				;
			}
			// An element broken only inside its groups, which join where it fits on a line, is laid out as one on a line.
			fg_ExpectFormat
				(
					"ElementBrokenInsideGroups"
					, "CStr g_A[] =\n{\n\tfg_A(\n\t\t\"aaa\"),\n\tfg_B(\n\t\t\"bbb\"),\n\tfg_C(\"ccc\")\n};\n"
					, "CStr g_A[] =\n\t{\n\t\tfg_A(\"aaa\")\n\t\t, fg_B(\"bbb\")\n\t\t, fg_C(\"ccc\")\n\t}\n;\n"
				)
			;
			// So is one broken only in front of its qualification.
			fg_ExpectFormat
				(
					"TemplateArgumentsWrittenAboveQualification"
					, "auto const g_A =\n\t{\n\t\t&CA::f_Member<CB, CC>\n\t\t\t::template f_Wrap<&CD::f_A>\n"
					"\t, &CA::f_Member<CB, CD>\n\t\t\t::template f_Wrap<&CD::f_B>\n\t}\n;\n"
					, "auto const g_A =\n\t{\n\t\t&CA::f_Member<CB, CC>::template f_Wrap<&CD::f_A>\n\t\t, &CA::f_Member<CB, CD>::template f_Wrap<&CD::f_B>\n\t}\n;\n"
				)
			;
			// The block comments naming the elements of a list lead them behind their commas, and a conditional in a list
			// moves no comma past such a name.
			fg_ExpectFormat
				(
					"NamedElementsLevelled"
					, "char const *f()\n{\n  static char const *g_pA[] = {\n    /* A */ \"a\",\n    /* B */ \"b\",\n  };\n  return g_pA[0];\n}\n"
					, "char const *f()\n{\n\tstatic char const *g_pA[] =\n\t\t{\n\t\t\t/* A */ \"a\"\n\t\t\t, /* B */ \"b\"\n\t\t}\n\t;\n\treturn g_pA[0];\n}\n"
					, false
				)
			;
			fg_ExpectFormat
				(
					"NamedElementsAcrossConditional"
					, "int g_A[] = {\n\t/* A */ 1,\n#ifdef DB\n\t/* B */ 2,\n#endif\n\t/* C */ 3,\n};\n"
					, "int g_A[] =\n\t{\n\t\t/* A */ 1,\n#ifdef DB\n\t\t/* B */ 2,\n#endif\n\t\t/* C */ 3,\n\t}\n;\n"
				)
			;
			// The comma in front of a comment on its line takes one space behind it, whatever the line below does.
			fg_ExpectFormat
				(
					"CommaBeforeComments"
					, "int g_A[] = {\n\t/* A */\tgA,\t\t/* a */\n\t/* B */\tgB,\t/* b */   // b\n\t/* C */\tgC,\t\t/* c */\n};\n"
					, "int g_A[] =\n\t{\n\t\t/* A */\tgA, // a\n\t\t/* B */\tgB, /* b */ // b\n\t\t/* C */\tgC // c\n\t}\n;\n"
					, false
				)
			;
			// A list's trailing comma behind a block comment trailing the last element goes, and the comment, left ending
			// its line, is written as a line comment.
			fg_ExpectFormat("TrailingCommaBehindBlockComment", "int g_A[] = {\n\t1,\n\t2 /* B */,\n};\n", "int g_A[] =\n\t{\n\t\t1\n\t\t, 2 // B\n\t}\n;\n", false);
			// A block comment in front of the comment behind the comma describes the element with it. The list keeps its
			// lines, as one holding a block comment does.
			fg_ExpectFormat("CommaBehindBlockComments", "struct CA\n{\n\tCB m_X{1, /* A */ // B\n\t\t2};\n};\n", "struct CA\n{\n\tCB m_X{1 /* A */ // B\n\t\t, 2};\n};\n", false);
			// Nor is a line a closing marker starts.
			fg_ExpectFormat("TrailingCommentBehindClosingMarker", "void f()\n{\n\t[]\n\t{\n\t}(); // " + LongComment + "\n}\n", "void f()\n{\n\t[]\n\t{\n\t}(); // " + LongComment + "\n}\n");
			// A lambda's body is measured where it goes, in from the call it is an argument of.
			CStr LambdaComment;
			for (umint i = 0; i < 167; ++i)
				LambdaComment += "c";

			fg_ExpectFormat
				(
					"TrailingCommentInLambdaArgument"
					, "void f()\n{\n\tg([] {\n\t\treturn i; // " + LambdaComment + "\n\t});\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\t[]\n\t\t\t{\n\t\t\t\t// " + LambdaComment + "\n\t\t\t\treturn i;\n\t\t\t}\n\t\t)\n\t;\n}\n"
					, false
				)
			;
			// So is a statement a clause guards without braces, one level in from the clause.
			CStr GuardedComment;
			for (umint i = 0; i < 165; ++i)
				GuardedComment += "c";

			fg_ExpectFormat
				(
					"TrailingCommentGuardedInLambda"
					, "void f()\n{\n\th([this]() {\n\t\tif (a)\n\t\t\tg(); // " + GuardedComment + "\n\t});\n}\n"
					, "void f()\n{\n\th\n\t\t(\n\t\t\t[this]()\n\t\t\t{\n\t\t\t\tif (a)\n\t\t\t\t\t// " + GuardedComment + "\n\t\t\t\t\tg();\n\t\t\t}\n\t\t)\n\t;\n}\n"
					, false
				)
			;
			// Nor is a line a block's opening brace starts, which the comment would part from its head.
			fg_ExpectFormat
				(
					"TrailingCommentBehindOpeningBrace"
					, "void f()\n{\n\ttry\n\t{\n\t}\n\tcatch (CException &_Error)\n\t{ // " + LongComment + "\n\t}\n}\n"
					, "void f()\n{\n\ttry\n\t{\n\t}\n\tcatch (CException &_Error)\n\t{ // " + LongComment + "\n\t}\n}\n"
				)
			;
			// Nor is an 'else'.
			fg_ExpectFormat
				(
					"TrailingCommentBehindElse"
					, "void f()\n{\n\tif (a)\n\t{\n\t}\n\telse if (b) // " + LongComment + "\n\t\tg();\n}\n"
					, "void f()\n{\n\tif (a)\n\t{\n\t}\n\telse if (b) // " + LongComment + "\n\t\tg();\n}\n"
				)
			;
			// Nor is the 'while' a 'do' block's closing brace goes on with.
			fg_ExpectFormat
				(
					"TrailingCommentBehindDoWhile"
					, "void f()\n{\n\tdo\n\t{\n\t}\n\twhile (a); // " + LongComment + "\n}\n"
					, "void f()\n{\n\tdo\n\t{\n\t}\n\twhile (a); // " + LongComment + "\n}\n"
				)
			;
			// A statement behind a block's closing brace starts its line.
			fg_ExpectFormat
				(
					"TrailingCommentBehindBlock"
					, "void f()\n{\n\tif (a)\n\t{\n\t}\n\tswitch (n) { // " + LongComment + "\n\t}\n}\n"
					, "void f()\n{\n\tif (a)\n\t{\n\t}\n\t// " + LongComment + "\n\tswitch (n)\n\t{\n\t}\n}\n"
					, false
				)
			;
			// A comment moved above a line at the start of the file goes in front of the qualifier moved there.
			// A statement behind a case label that the layout gives a line of its own is measured there, and its comment
			// too long to trail it goes above it.
			{
				CStr Name;
				for (umint i = 0; i < 172; ++i)
					Name += "W";

				fg_ExpectFormat
					(
						"BehindCaseLabel"
						, CStr("void f()\n{\n\tswitch (n)\n\t{\n\tcase 1: a@ += 1;/* note */\n\tcase 2: b += 2;\n\t}\n}\n").f_Replace("@", Name)
						, CStr("void f()\n{\n\tswitch (n)\n\t{\n\tcase 1:\n\t\t// note\n\t\ta@ += 1;\n\tcase 2: b += 2;\n\t}\n}\n").f_Replace("@", Name)
						, false
					)
				;
			}
			// One trailing a block behind the label and what follows it stays with the label's line.
			{
				CStr Comment;
				for (umint i = 0; i < 175; ++i)
					Comment += "c";

				fg_ExpectFormat
					(
						"BehindCaseLabelBlock"
						, CStr("void f()\n{\n\tswitch (n)\n\t{\n\tdefault: { g(); } break; // @\n\t}\n}\n").f_Replace("@", Comment)
						, CStr("void f()\n{\n\tswitch (n)\n\t{\n\t// @\n\tdefault:\n\t\t{\n\t\t\tg();\n\t\t}\n\t\tbreak;\n\t}\n}\n").f_Replace("@", Comment)
						, false
					)
				;
			}
			// A comment behind a block's opening brace trails the brace on its own line, where the head in front is no
			// part of its width.
			{
				CStr Name;
				for (umint i = 0; i < 162; ++i)
					Name += "W";

				fg_ExpectFormat
					(
						"BehindOpeningBrace"
						, CStr("void f()\n{\n\tif (a@) { /* use a */ /* use b */\n\t\tg();\n\t\th();\n\t}\n}\n").f_Replace("@", Name)
						, CStr("void f()\n{\n\tif (a@)\n\t{ /* use a */ // use b\n\t\tg();\n\t\th();\n\t}\n}\n").f_Replace("@", Name)
						, false
					)
				;
			}
			// A comment line continuing a trailing comment is told apart against the line the layout ends the code on.
			fg_ExpectFormat("ContinuedBehindJoinedCode", "void f()\n{\n    g(a,\n     b); // x\n     // y\n}\n", "void f()\n{\n\tg(a, b); // x\n\t\t\t // y\n}\n");
			// So does one written over several lines, which becomes one line comment per line where the brace's line fits.
			{
				CStr Name;
				for (umint i = 0; i < 150; ++i)
					Name += "W";

				fg_ExpectFormat
					(
						"MultiLineBehindOpeningBrace"
						, CStr("void f(int a@) { /* first part of the note\n                    second part\n                  */\n\tg();\n}\n").f_Replace("@", Name)
						, CStr("void f(int a@)\n{ // first part of the note\n\t// second part\n\tg();\n}\n").f_Replace("@", Name)
						, false
					)
				;
			}
			fg_ExpectFormat("TrailingCommentAboveQualifier", "const char *g_pA; // " + LongComment + "\n", "// " + LongComment + "\nchar const *g_pA;\n", false);
			// One written further in than the layout puts it is measured where it goes too, the code fitting there.
			CStr MovedOutArgument;
			for (umint i = 0; i < 176; ++i)
				MovedOutArgument += "a";

			fg_ExpectFormat
				(
					"TrailingCommentMovedOut"
					, "void f()\n{\n\t\t\tg(" + MovedOutArgument + "); // ccccc\n}\n"
					, "void f()\n{\n\t// ccccc\n\tg(" + MovedOutArgument + ");\n}\n"
					, false
				)
			;
			// An element whose comma alone stands on the line above is not opened for the comment behind it.
			fg_ExpectFormat
				(
					"TrailingCommentBehindElement"
					, "void f()\n{\n\tint a[2] = {\n\t\tg(1),\n\t\th(2) // " + LongComment + "\n\t};\n}\n"
					, "void f()\n{\n\tint a[2] =\n\t\t{\n\t\t\tg(1)\n\t\t\t, h(2) // " + LongComment + "\n\t\t}\n\t;\n}\n"
					, false
				)
			;
			// The comment above the line moves with it and the comment taken off it.
			fg_ExpectFormat
				(
					"TrailingCommentMovedWithCommentAbove"
					, "void f()\n{\n\t\t// A\n\t\tg(); // " + LongComment + "\n}\n"
					, "void f()\n{\n\t// A\n\t// " + LongComment + "\n\tg();\n}\n"
					, false
				)
			;
			// A comment behind a block comment stays: moving it would leave the block comment ending the line.
			fg_ExpectFormat("TrailingCommentBehindBlockComment", "void f()\n{\n\tg(); /* A */ // " + LongComment + "\n}\n", "void f()\n{\n\tg(); /* A */ // " + LongComment + "\n}\n");
			// A statement behind a block's brace on its line is measured on the line the layout gives it, and the
			// comment goes above it there.
			fg_ExpectFormat
				(
					"TrailingCommentBehindBrace"
					, "void f()\n{\n\t{ g(); // " + LongComment.f_Left(180) + "\n\t\th();\n\t}\n}\n"
					, "void f()\n{\n\t{\n\t\t// " + LongComment.f_Left(180) + "\n\t\tg();\n\t\th();\n\t}\n}\n"
					, false
				)
			;
			// A comment over several lines has its first line measured behind its code where the layout indents it.
			fg_ExpectFormat
				(
					"TrailingBlockCommentLinesIndented"
					, "namespace N\n{\nint g_A; /* " + LongComment.f_Left(175) + "\n\tb */\n}\n"
					, "namespace N\n{\n\tint g_A; /* " + LongComment.f_Left(175) + "\n\tb */\n}\n"
				)
			;
			// A structured binding's brackets declare names, and stay whole as a name does.
			fg_ExpectFormat
				(
					"StructuredBinding"
					, "void f()\n{\n\tfor (auto const &[Name, Value] : {g(" + LongArgument + "), h(" + LongArgument + ")})\n\t\tg();\n}\n"
					, "void f()\n{\n\tfor\n\t(\n\t\tauto const &[Name, Value] :\n\t\t{\n\t\t\tg(" + LongArgument + ")\n\t\t\t, h(" + LongArgument + ")\n\t\t}\n\t)\n\t{\n\t\tg();\n\t}\n}\n"
					, false
				)
			;
			// So does the template argument list of the type a range-based for declares its element with.
			fg_ExpectFormat
				(
					"RangeElementType"
					, "void f()\n{\n\tfor (TCOptional<CStr> Value : {g(" + LongArgument + "), h(" + LongArgument + ")})\n\t\tg();\n}\n"
					, "void f()\n{\n\tfor\n\t(\n\t\tTCOptional<CStr> Value :\n\t\t{\n\t\t\tg(" + LongArgument + ")\n\t\t\t, h(" + LongArgument + ")\n\t\t}\n\t)\n\t{\n\t\tg();\n\t}\n}\n"
					, false
				)
			;
			// A comma in front of a closer is measured as the spacing rules write it, tight.
			fg_ExpectFormat("CommaBeforeCloser", "void f()\n{\n\tg(a, )(" + LongComment.f_Left(178) + ");\n}\n", "void f()\n{\n\tg(a,)(" + LongComment.f_Left(178) + ");\n}\n");
			// The comment lines below a case body taken onto its label's line stand at the body's level, a comment of
			// their own rather than one continuing the comment the body ends with.
			fg_ExpectFormat
				(
					"CommentBelowSharedCaseBody"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 0:\n\t\treturn 1; // A\n\t\t// B\n\tcase 1:\n\t\treturn 2; // C\n\t\t// D\n\t}\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 0: return 1; // A\n\t\t// B\n\tcase 1: return 2; // C\n\t\t// D\n\t}\n}\n"
				)
			;
			fg_ExpectFormat
				(
					"CommentBelowCaseBodyOnLabelLine"
					, "void f()\n{\n  switch (a)\n  {\n    case 0: return 1; // A\n        // B\n    default: return 2;\n  }\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 0: return 1; // A\n\t\t// B\n\tdefault: return 2;\n\t}\n}\n"
				)
			;
			// A comment closing a switch behind a case body on its label's line stands one level in from the label.
			fg_ExpectFormat
				(
					"CommentClosingSharedCaseBody"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tdefault:\n\t\tg();\n\t\t/* NOTREACHED */\n\t}\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tdefault: g();\n\t\t/* NOTREACHED */\n\t}\n}\n"
				)
			;
			// A comment closing a block behind a clause's body stands where the clause does.
			fg_ExpectFormat
				(
					"CommentClosingClauseBody"
					, "void f()\n{\n\tfor (auto &A : B)\n\t{\n\t\tif (!A)\n\t\t\tg();\n\t\t// Only direct\n\t}\n}\n"
					, "void f()\n{\n\tfor (auto &A : B)\n\t{\n\t\tif (!A)\n\t\t\tg();\n\t\t// Only direct\n\t}\n}\n"
				)
			;
			// Code a comment toggle commented out at the line's start stands at its scope's level, its tabs one space.
			fg_ExpectFormat("ToggledCommentToScope", "void f()\n{\n//\tg();\n\th();\n//\ti();\n}\n", "void f()\n{\n\t// g();\n\th();\n\t// i();\n}\n", false);
			// A comment continuing one behind a block's opening brace aligns with it, and a line below it that does not continue it
			// stands where the code does.
			fg_ExpectFormat
				(
					"ContinuedBehindOpeningBrace"
					, "void f()\n{\n  {\n  { // A\n    // B\n//[C\n    int y;\n  }\n  }\n}\n"
					, "void f()\n{\n\t{\n\t\t{ // A\n\t\t  // B\n\t\t\t//[C\n\t\t\tint y;\n\t\t}\n\t}\n}\n"
				)
			;
			// A comment below one trailing code the layout keeps where the source has it stands no further in than that code.
			fg_ExpectFormat
				(
					"CommentBelowKeptTrailedCode"
					, "struct X {\n  int foo() {\n    Derived(). // A\n    // B\n  }\n};\n"
					, "struct X\n{\n\tint foo()\n\t{\n\tDerived(). // A\n\t// B\n\t}\n};\n"
				)
			;
			// A block comment on a line of its own continues a trailing comment as a line comment does.
			fg_ExpectFormat
				(
					"BlockCommentContinuesTrailing"
					, "void f()\n{\n  int a = 0; // x\n             /* y */\n  int b;\n}\n"
					, "void f()\n{\n\tint a = 0; // x\n\t\t\t   /* y */\n\tint b;\n}\n"
				)
			;
			// A comment the layout places among a split statement's lines, further in than the line the comment above trails,
			// continues that comment.
			fg_ExpectFormat
				(
					"ContinuedInSplitStatement"
					, "void f()\n{\n  SourceLocation\n      End = // x\n      // y\n      g();\n}\n"
					, "void f()\n{\n\tSourceLocation End = // x\n\t\t\t\t\t\t // y\n\t\tg()\n\t;\n}\n"
				)
			;
			// A line comment behind a block comment's end is no line of its own, and keeps its text.
			fg_ExpectFormat("CommentBehindBlockCommentEnd", "/*\nA\n */   // \t> B\n  // > C\nint a;\n", "/*\nA\n */   // \t> B\n// > C\nint a;\n");
			// The blanks every line of a run of line comments starts with behind its markers are one space.
			fg_ExpectFormat("LineCommentPrefix", "void f()\n{\n\t//\t\tA\n\t//\t\t\tB\n\th();\n}\n", "void f()\n{\n\t// A\n\t// \tB\n\th();\n}\n", false);
			// Code a toggle commented out at the line's start stands at its text, and code one commented out at its
			// indentation at the marker.
			fg_ExpectFormat
				(
					"LineCommentPrefixToggledEitherWay"
					, "void f()\n{\n//\tA();\n\t//B();\n//\t\tC();\n\tg();\n}\n"
					, "void f()\n{\n\t// A();\n\t// B();\n\t// \tC();\n\tg();\n}\n"
					, false
				)
			;
			// Lines whose blanks go on with a tab on one and a space on another keep them.
			fg_ExpectFormat
				(
					"LineCommentPrefixMixedBlanks"
					, "void f()\n{\n\tg();\n\t//\t\tA\n\t//\t      B\n\t// C\n\t//\tD\n\th();\n}\n"
					, "void f()\n{\n\tg();\n\t//\t\tA\n\t//\t      B\n\t// C\n\t//\tD\n\th();\n}\n"
				)
			;
			// A block comment's text written further in than one space behind its marker starts below the marker, at its column,
			// and its lines keep where they stand relative to it.
			fg_ExpectFormat
				(
					"BlockCommentPrefix"
					, "void f()\n{\n\t/*\t\taint A = 0;\n\t\t\t\tif (A)\n\t\t\t\t{\n\t\t\t\t\tg();\n\t\t\t\t}*/\n\th();\n}\n"
					, "void f()\n{\n\t/*\n\taint A = 0;\n\t\tif (A)\n\t\t{\n\t\t\tg();\n\t\t}*/\n\th();\n}\n"
					, false
				)
			;
			// Text below a block comment's marker alone on its line further in than the marker moves out to its column.
			fg_ExpectFormat
				(
					"BlockCommentPrefixMarkerAlone"
					, "void f()\n{\n\t/*\n\t\t\t{\n\t\t\t\tg();\n\t\t\t}\n\t */\n\th();\n}\n"
					, "void f()\n{\n\t/*\n\t{\n\t\tg();\n\t}\n\t */\n\th();\n}\n"
					, false
				)
			;
			fg_ExpectFormat("BlockCommentPrefixMarkerAloneDecorated", "void f()\n{\n\t/*\n\t * A\n\t *   B\n\t */\n\th();\n}\n", "void f()\n{\n\t/*\n\t * A\n\t *   B\n\t */\n\th();\n}\n");
			// A block comment at the line's start stands at its scope's level, commented out code included, and its text with it.
			fg_ExpectFormat
				(
					"BlockCommentAtLineStartToScope"
					, "namespace N\n{\n\tvoid f();\n\n/*\n\tvoid g()\n\t{\n\t}\n*/\n\tvoid h();\n}\n"
					, "namespace N\n{\n\tvoid f();\n\n\t/*\n\tvoid g()\n\t{\n\t}\n\t*/\n\tvoid h();\n}\n"
					, false
				)
			;
			// The comment lines in front of a string run a value starts on a line of its own stand with the run.
			fg_ExpectFormat("CommentInFrontOfValueStrings", "char const g_A[] =\n  /* B */\n\"a\"\n\"b\";\n", "char const g_A[] =\n\t/* B */\n\t\"a\"\n\t\"b\"\n;\n");
			// Line and block comments at the line's start together stand at their scope's level.
			fg_ExpectFormat
				(
					"LineStartLineAndBlockComments"
					, "void f()\n{\n\tg();\n\n//\t\th();\n/*\n\t\ti();\n*/\n\tj();\n}\n"
					, "void f()\n{\n\tg();\n\n\t// h();\n\t/*\n\ti();\n\t*/\n\tj();\n}\n"
					, false
				)
			;
			// In front of a directive a comment stands where the statement above it does.
			fg_ExpectFormat("CommentInFrontOfDirective", "namespace N\n{\n\tvoid f();\n\n// A\n\n#define DB 1\n}\n", "namespace N\n{\n\tvoid f();\n\n\t// A\n\n#define DB 1\n}\n");
			fg_ExpectFormat
				(
					"CommentInFrontOfDirectiveInFunction"
					, "void f()\n{\n\ta();\n\n//\t\tb();\n\n#if DA\n\tc();\n\t\t// d();\n#else\n\te();\n#endif\n}\n"
					, "void f()\n{\n\ta();\n\n\t// b();\n\n#if DA\n\tc();\n\t// d();\n#else\n\te();\n#endif\n}\n"
					, false
				)
			;
			// A directive taken out of the code stands where directives do, and parts the comments around it into runs of
			// their own.
			fg_ExpectFormat
				(
					"CommentedDirective"
					, "void f()\n{\n\ta();\n\t//\t\tb();\n\t\t//#if DA\n\t//\t\tc();\n//  #endif\n\td();\n}\n"
					, "void f()\n{\n\ta();\n\t// b();\n//#if DA\n\t// c();\n//  #endif\n\td();\n}\n"
					, false
				)
			;
			// Behind a class's opening brace a comment written deeper than the access label below it is the members'.
			fg_ExpectFormat("CommentBehindClassBraceBeforeAccessLabel", "class A\n{\n/*\nB\n*/\nprivate:\n\tint a;\n};\n", "class A\n{\n\t/*\n\tB\n\t*/\nprivate:\n\tint a;\n};\n");
			// A comment in front of a template argument list's closing marker stands among the arguments, as in front of
			// a parenthesis's.
			fg_ExpectFormat
				(
					"CommentInFrontOfTemplateArgumentsClose"
					, "using A = B\n\t<\n\t\tC\n\t\t, D\n/*\nE\n\t, F\n*/\n// G\n\t>\n;\n"
					, "using A = B\n\t<\n\t\tC\n\t\t, D\n\t\t/*\n\t\tE\n\t\t\t, F\n\t\t*/\n\t\t// G\n\t>\n;\n"
				)
			;
			// Comment lines under a trailing comment, no further in than the code it trails, are no continuation of it.
			fg_ExpectFormat
				(
					"LineCommentPrefixUnderTrailingComment"
					, "enum E\n{\n\tA\n\t, B // C\n\t//\t\t, D // E\n\t//\t\t, F // G\n};\n"
					, "enum E\n{\n\tA\n\t, B // C\n\t// , D // E\n\t// , F // G\n};\n"
					, false
				)
			;
			// A trailing comment moved above its too long line is a comment line, whose blanks behind the marker are one space.
			fg_ExpectFormat
				(
					"MovedTrailingCommentPrefix"
					, "void f()\n{\n\tg(1); // A\n\tg("
					"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
					"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa); //  B\n}\n"
					, "void f()\n{\n\tg(1); // A\n\t// B\n\tg("
					"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
					"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa);\n}\n"
					, false
				)
			;
			// A line comment behind a block comment's end is no code behind it, and the comment stands where the code below does.
			fg_ExpectFormat
				(
					"BlockCommentWithTrailingLineComment"
					, "struct A\n{\n\tvoid f();\n\n/*\tvoid g()\n\t{\n\t}*/ // B\n\n\tvoid h();\n};\n"
					, "struct A\n{\n\tvoid f();\n\n\t/*\n\tvoid g()\n\t{\n\t}*/ // B\n\n\tvoid h();\n};\n"
					, false
				)
			;
			fg_ExpectFormat
				(
					"CommentedDirectivesAroundToggledLines"
					, "void f()\n{\n   a();\n   //#ifndef DA\n   //if (b)\n   //{\n      //c();\n   //}\n   //#endif\n}\n"
					, "void f()\n{\n\ta();\n//#ifndef DA\n\t// if (b)\n\t// {\n\t//    c();\n\t// }\n//#endif\n}\n"
					, false
				)
			;
			fg_ExpectFormat("CommentedDirectiveInDirectives", "#ifdef DA\n\t// Not compatible\n//#define DB\n#endif\n", "#ifdef DA\n\t// Not compatible\n//#define DB\n#endif\n");
			fg_ExpectFormat
				(
					"CommentInFrontOfDirectiveInClass"
					, "struct A\n{\n\tint a;\n// b\n#if DA\n\tint c;\n#endif\n};\nnamespace N\n{\n// d\n#if DB\n\tint e;\n#endif\n}\n"
					, "struct A\n{\n\tint a;\n\t// b\n#if DA\n\tint c;\n#endif\n};\n\nnamespace N\n{\n// d\n#if DB\n\tint e;\n#endif\n}\n"
				)
			;
			// A file starting with a braced list holds the elements of a list another file includes, and is laid out as they
			// are inside the list's braces.
			fg_ExpectFormat
				(
					"IncludedListFile"
					, "{ /* 0 */\n  {\n    { 1, 2 },\n    { 3, 4 }\n  }\n},\n{ /* 1 */\n  {\n    { 5, 6 }\n  }\n}\n"
					, "{ // 0\n\t{\n\t\t{1, 2}\n\t\t, {3, 4}\n\t}\n}\n,\n{ // 1\n\t{\n\t\t{5, 6}\n\t}\n}\n"
					, false
				)
			;
			fg_ExpectFormat("IncludedRowsFile", "{ 1, 2 },\n{ 3, 4 },\n", "{1, 2}\n, {3, 4}\n,\n");
			fg_ExpectFormat("IncludedSingleRowFile", "{ 1,\n 2 }\n", "{1, 2}\n");
			// A name alone on its line a blank line parts from what follows is a statement of its own, however far in that starts.
			fg_ExpectFormat
				(
					"MacroAloneBeforeBlankLine"
					, "FT_BEGIN_HEADER\n\n  typedef int\n  (*FFunc)( int    a,\n           int    b );\n\nFT_END_HEADER\n"
					, "FT_BEGIN_HEADER\n\ntypedef int (*FFunc)(int a, int b);\n\nFT_END_HEADER\n"
				)
			;
			// An access specifier with a section's name is a label: Qt's 'private Q_SLOTS:'.
			fg_ExpectFormat
				(
					"AccessSectionLabel"
					, "class A : public QObject\n{\n    Q_OBJECT\npublic:\n    void f();\nprivate Q_SLOTS:\n    void g();\npublic slots:\n    void h();\n};\n"
					, "class A : public QObject\n{\n\tQ_OBJECT\npublic:\n\tvoid f();\nprivate Q_SLOTS:\n\tvoid g();\npublic slots:\n\tvoid h();\n};\n"
				)
			;
			// A head no gap's spelling settles the width of moves no return type behind an arrow.
			fg_ExpectFormat
				(
					"UnmeasuredHeadKeepsReturnType"
					, "class C\n{\n    void f() override sealed {}\n    void g() override const sealed {}\n};\n"
					, "class C\n{\n\tvoid f() override sealed {}\n\tvoid g() override const sealed {}\n};\n"
				)
			;
			// Columns are measured at the file's tab width: a body a tab puts no further in than its macro is no guarded statement.
			fg_ExpectFormat
				(
					"ClauseMacroTabColumns"
					, "void f()\n{\n  if (a)\n  {\n      foreach_item (buffer, start, end)\n\tbuffer[start] += 1;\n  }\n}\n"
					, "void f()\n{\n\tif (a)\n\t{\n\t\tforeach_item(buffer, start, end)\n\t  buffer[start] += 1;\n\t}\n}\n"
				)
			;
			// A list ending a statement a comment cuts opens behind the stretch in front of it, written open or not.
			fg_ExpectFormat
				(
					"ListBehindCommentCutHead"
					, "void f()\n{\n    QTest::addRow(\"no\") // Comment\n        << u\"no\"_s << QLocale::NorwegianBokmal\n"
					"        << QStringList{u\"no\"_s, u\"nb-Latn-NO\"_s, u\"nb-NO\"_s, u\"nb-Latn\"_s,\n"
					"                       u\"en-Latn-US\"_s, u\"en-US\"_s, u\"en-Latn\"_s, u\"en\"_s,\n                       u\"nb\"_s};\n}\n"
					, "void f()\n{\n\tQTest::addRow(\"no\") // Comment\n\t\t<< u\"no\"_s << QLocale::NorwegianBokmal << QStringList\n"
					"\t\t{\n\t\t\tu\"no\"_s\n\t\t\t, u\"nb-Latn-NO\"_s\n\t\t\t, u\"nb-NO\"_s\n"
					"\t\t\t, u\"nb-Latn\"_s\n\t\t\t, u\"en-Latn-US\"_s\n\t\t\t, u\"en-US\"_s\n\t\t\t, u\"en-Latn\"_s\n\t\t\t, u\"en\"_s\n\t\t\t, u\"nb\"_s\n\t\t}\n\t;\n}\n"
					, false
				)
			;
			// Code behind the end of a block comment over lines stands where the comment's first line does.
			fg_ExpectFormat("BlockBehindCommentEnd", "void f()\n{\n    /* a\n       b */ {\n        g();\n    }\n}\n", "void f()\n{\n\t/* a\n\t   b */ {\n\t\tg();\n\t}\n}\n");
			// A keyword taking a parenthesis goes on behind it, its braces dropped included: 'new (pMemory)' over 'CFoo(1);'.
			fg_ExpectFormat
				(
					"PlacementNewContinues"
					, "void f()\n{\n  for (unsigned i = 0; i < n; ++i) {\n    new (op->getOutOfLineOpResult(i))\n        detail::OutOfLineOpResult(*resultTypeIt, i);\n  }\n}\n"
					, "void f()\n{\n\tfor (unsigned i = 0; i < n; ++i)\n\t\tnew(op->getOutOfLineOpResult(i)) detail::OutOfLineOpResult(*resultTypeIt, i);\n}\n"
					, false
				)
			;
			// A closing marker on a line of its own keeps its place.
			fg_ExpectFormat("BlockCommentPrefixClosingLine", "void f()\n{\n\t/*\t\tA\n\t\t\t\tB\n\t*/\n\th();\n}\n", "void f()\n{\n\t/*\n\tA\n\t\tB\n\t*/\n\th();\n}\n", false);
			// A decorated block comment keeps its lines.
			fg_ExpectFormat("BlockCommentPrefixDecorated", "void f()\n{\n\t/*\tA\n\t *\tB\n\t */\n\th();\n}\n", "void f()\n{\n\t/*\tA\n\t *\tB\n\t */\n\th();\n}\n");
			// A row a block comment names is measured from that name, where the layout puts the row, and the comment
			// moved above it stands with it.
			fg_ExpectFormat
				(
					"TrailingCommentBehindNamedRow"
					, "int g_A[] = {\n    /* A */ 1, 2, // " + LongComment.f_Left(169) + "\n    /* B */ 3, 4, // d\n};\n"
					, "int g_A[] =\n\t{\n\t\t// " + LongComment.f_Left(169) + "\n\t\t/* A */ 1\n\t\t, 2\n\t\t, /* B */ 3\n\t\t, 4 // d\n\t}\n;\n"
					, false
				)
			;
			// Code a block comment keeps from being joined is measured as the source writes it, from its first token at the
			// depth the layout gives the line.
			fg_ExpectFormat
				(
					"TrailingCommentBehindUnjoinableCode"
					, "void f()\n{\n  switch (a)\n  {\n      case 0: return g(/*b=*/ c); // " + LongComment.f_Left(154) + "\n  }\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 0: return g(/*b=*/ c); // " + LongComment.f_Left(154) + "\n\t}\n}\n"
				)
			;
			// A block's closing brace takes a line of its own, and a comment trailing it is measured behind it there.
			fg_ExpectFormat
				(
					"TrailingCommentBehindClosingBrace"
					, "void f() { g(); } /* " + LongComment.f_Left(180) + "\n                     B */\nint g_A;\n"
					, "void f()\n{\n\tg();\n} // " + LongComment.f_Left(180) + "\n  // B\n\nint g_A;\n"
					, false
				)
			;
			// A line starting with a case label is measured at the label's level, a level out from its statement.
			fg_ExpectFormat
				(
					"TrailingCommentBehindCaseLabel"
					, "void f()\n{\n\tswitch (n)\n\t{\n\tcase 1: return 2; // " + LongComment.f_Left(165) + "\n\t}\n}\n"
					, "void f()\n{\n\tswitch (n)\n\t{\n\tcase 1: return 2; // " + LongComment.f_Left(165) + "\n\t}\n}\n"
				)
			;
			// Code on a case label's line is measured where the spacing rules write it behind the label, however far the
			// source aligned it.
			{
				CStr Name;
				for (umint i = 0; i < 138; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"CaseBodyAlignedBehindLabel"
						, "int f()\n{\n\tswitch (x)\n\t{\n\tcase CA::mc_B:                  return fg_Lower(p" + Name + ", q,\n\t\t\tr);\n\tcase CA::mc_C: return 0;\n\t}\n}\n"
						, "int f()\n{\n\tswitch (x)\n\t{\n\tcase CA::mc_B: return fg_Lower(p" + Name + ", q, r);\n\tcase CA::mc_C: return 0;\n\t}\n}\n"
					)
				;
			}
			// So is code on a jump target's line.
			{
				CStr Name;
				for (umint i = 0; i < 146; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"JumpTargetBodyAligned"
						, "int f()\n{\nl_Retry:                  return fg_Lower(p" + Name + ", q,\n\t\t\tr);\n}\n"
						, "int f()\n{\nl_Retry: return fg_Lower(p" + Name + ", q, r);\n}\n"
					)
				;
			}
			// A statement a clause guards without braces stands a level in from it, a comment's code behind a lambda in it as well.
			{
				CStr Words;
				for (umint i = 0; i < 152; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentBehindGuardedLambda"
						, "void f()\n{\n\tfor (auto x : y)\n\t\tfg_Bench(\"a\", [x](auto &_State) {\n\t\t\tfor (auto _ : _State) {\n\t\t\t\tc.f_Erase(); // "
						+ Words
						+ "\n\t\t\t}\n\t\t});\n}\n"
						, "void f()\n{\n\tfor (auto x : y)\n\t{\n\t\tfg_Bench\n\t\t\t(\n\t\t\t\t\"a\"\n\t\t\t\t, [x](auto &_State)\n"
						"\t\t\t\t{\n\t\t\t\t\tfor (auto _ : _State)\n\t\t\t\t\t{\n"
						"\t\t\t\t\t\t// "
						+ Words
						+ "\n\t\t\t\t\t\tc.f_Erase();\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t)\n\t\t;\n\t}\n}\n"
						, false
					)
				;
			}
			// A comment behind the brace of a clause's body trails the clause from its start once its lines are joined.
			{
				CStr Words;
				for (umint i = 0; i < 150; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentBehindContinuedClause"
						, "void f()\n{\n\tif (bA &&\n\t\tfg_IsAllocatable(pSymbol)) { // " + Words + "\n\t\tg();\n\t\th();\n\t}\n}\n"
						, "void f()\n{\n\t// " + Words + "\n\tif (bA && fg_IsAllocatable(pSymbol))\n\t{\n\t\tg();\n\t\th();\n\t}\n}\n"
						, false
					)
				;
			}
			// One inside a statement written across lines stays behind the part it trails, and one trailing the end of the
			// statement behind the line the split leaves it on.
			{
				CStr Words;
				for (umint i = 0; i < 175; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentBehindSplitStatement"
						, "int64 g_Value = /* " + Words + " */\n\t-42; /* " + Words + " */\n"
						, "int64 g_Value = // " + Words + "\n\t-42\n; // " + Words + "\n"
						, false
					)
				;
			}
			// A statement an 'else' guards on its line takes a line of its own, and its comment goes above it there.
			{
				CStr Words;
				for (umint i = 0; i < 176; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentBehindStatementOfElse"
						, "int f()\n{\n\tif (a)\n\t\tg();\n\telse return 1; // " + Words + "\n\treturn 0;\n}\n"
						, "int f()\n{\n\tif (a)\n\t\tg();\n\telse\n\t\t// " + Words + "\n\t\treturn 1;\n\treturn 0;\n}\n"
						, false
					)
				;
			}
			// An empty statement a clause guards stays on its line, and the clause written over lines is joined onto its first.
			{
				CStr Words;
				for (umint i = 0; i < 170; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentBehindGuardedEmpty"
						, "void f()\n{\n\tif (bA &&\n\t\tfg_B()); // " + Words + "\n}\n"
						, "void f()\n{\n\t// " + Words + "\n\tif (bA && fg_B());\n}\n"
						, false
					)
				;
			}
			// The clauses in front of the one owning a braced body count too: 'for (;;) switch (n) {'.
			{
				CStr Words;
				for (umint i = 0; i < 173; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentInGuardedClauseBody"
						, "void f(int n)\n{\n\tfor (;;) switch (n) {\n\tcase 0:\n\t\tg(); // " + Words + "\n\t\tbreak;\n\t}\n}\n"
						, "void f(int n)\n{\n\tfor (;;)\n\t{\n\t\tswitch (n)\n\t\t{\n\t\tcase 0:\n\t\t\t// " + Words + "\n\t\t\tg();\n\t\t\tbreak;\n\t\t}\n\t}\n}\n"
						, false
					)
				;
			}
			// A comment behind a comma the list's conversion moves in front of the next element trails the element in front.
			{
				CStr Words;
				for (umint i = 0; i < 171; ++i)
					Words += "w";

				fg_ExpectFormat
					(
						"TrailingCommentBehindMovedComma"
						, "enum EKind\n{\n\tEKind_Gnu, /**< " + Words + "\n\t\tmore. */\n\tEKind_Last\n};\n"
						, "enum EKind\n{\n\tEKind_Gnu ///< " + Words + "\n\t\t\t  ///< more.\n\t, EKind_Last\n};\n"
						, false
					)
				;
			}
			// Code a block comment keeps from being joined is measured as the spacing rules write it around the comment.
			{
				CStr Name;
				for (umint i = 0; i < 142; ++i)
					Name += "a";

				fg_ExpectFormat
					(
						"TrailingCommentBehindRespacedNamedCode"
						, "int f()\n{\n\tswitch (n)\n\t{\n\tcase 0: return fg_Exec(      p" + Name + ", /*Argv=*/  pB); // no-warning\n\t}\n}\n"
						, "int f()\n{\n\tswitch (n)\n\t{\n\t// no-warning\n\tcase 0: return fg_Exec(p" + Name + ", /*Argv=*/  pB);\n\t}\n}\n"
						, false
					)
				;
			}
			// An attribute's parenthesis stands among the specifiers, and the parameter list behind the
			// name opens instead.
			CStr AttributeParameters;
			for (umint i = 0; i < 6; ++i)
				AttributeParameters += "{}void (*_fCallback{})(void)"_f << (i ? ", " : "") << i;

			fg_ExpectFormat
				(
					"AttributeParenthesis"
					, "void __attribute__\n\t(\n\t\t(weak)\n\t)\n\t__libc_init(" + AttributeParameters + ")\n{\n}\n"
					, "void __attribute__((weak)) __libc_init\n\t(\n\t\tvoid (*_fCallback0)(void)\n\t\t, void (*_fCallback1)(void)\n"
					"\t\t, void (*_fCallback2)(void)\n\t\t, void (*_fCallback3)(void)\n\t\t, void (*_fCallback4)(void)\n\t\t, void (*_fCallback5)(void)\n\t)\n{\n}\n"
				)
			;
			// A trailing return type's declarator belongs to its type, behind an attribute macro as well, and
			// a specifier behind it stays apart.
			fg_ExpectFormat
				(
					"TrailingReturnDeclarator"
					, "void f()\n{\n\tg = [](int _A)\n\t\tDMibSuppress -> void\n\t\t*\n\t\t{\n\t\t\treturn nullptr;\n\t\t}\n\t;\n}\n"
					"struct C\n{\n\tauto f() -> CFoo * override;\n};\n"
					, "void f()\n{\n\tg = [](int _A) DMibSuppress -> void *\n\t\t{\n\t\t\treturn nullptr;\n\t\t}\n\t;\n}\n\n"
					"struct C\n{\n\tauto f() -> CFoo * override;\n};\n"
				)
			;
			// A pointer to function's declarator hugs its name, behind a calling convention macro as well, a
			// pointer return type's '*' hugs the declarator, and the lists behind it are parameter lists.
			fg_ExpectFormat
				(
					"FunctionPointerDeclarators"
					, "struct C\n{\n\tvoid * (DMibCrossmoduleAPI * m_fA)(void * _pB, bool (* _fC)(void * _pD));\n};\n"
					"extern int (* g_fE)(int);\nvoid g()\n{\n\th((*pF)(x) * y);\n}\n"
					, "struct C\n{\n\tvoid *(DMibCrossmoduleAPI *m_fA)(void *_pB, bool (*_fC)(void *_pD));\n};\n\n"
					"extern int (*g_fE)(int);\nvoid g()\n{\n\th((*pF)(x) * y);\n}\n"
				)
			;
			// A macro spelling a parameter's type is followed by the parameter's declarator; in an expression
			// the same spelling is an operator.
			fg_ExpectFormat
				(
					"MacroTypeDeclarator"
					, "struct C\n{\n\tC(DMacro(TCBlock<t_C>, m_Link) & _Blocks, umint _Size);\n\tvoid f()\n\t{\n\t\tDMacro(CChunk) * lLists[] = {&a};\n"
					"\t\tauto a = DMacro(1) & b;\n\t}\n};\n"
					, "struct C\n{\n\tC(DMacro(TCBlock<t_C>, m_Link) &_Blocks, umint _Size);\n\tvoid f()\n\t{\n\t\tDMacro(CChunk) *lLists[] = {&a};\n"
					"\t\tauto a = DMacro(1) & b;\n\t}\n};\n"
				)
			;
			// A placement new gives in front of the type it constructs where the type does not fit behind it,
			// rather than opening the type's template arguments.
			CStr PlacementArgument;
			for (umint i = 0; i < 150; ++i)
				PlacementArgument += "W";

			fg_ExpectFormat
				(
					"PlacementGivesAtType"
					, "void f()\n{\n\tpA = new(fg_Alloc(" + PlacementArgument + ")) TCFooArena<t_CParams>(NumaNode, this, m_Magic);\n}\n"
					, "void f()\n{\n\tpA = new(fg_Alloc(" + PlacementArgument + "))\n\t\tTCFooArena<t_CParams>(NumaNode, this, m_Magic)\n\t;\n}\n"
				)
			;
			// A name qualified from the global scope stands apart from the keyword in front of it.
			fg_ExpectFormat
				(
					"GlobalScopeBehindKeyword"
					, "namespace std\n{\n\tusing::size_t;\n\tusing namespace::NA;\n\tint f()\n\t{\n\t\treturn::fg_A() + NA::fg_B();\n\t}\n}\n"
					, "namespace std\n{\n\tusing ::size_t;\n\tusing namespace ::NA;\n\tint f()\n\t{\n\t\treturn ::fg_A() + NA::fg_B();\n\t}\n}\n"
				)
			;
			// The brackets of 'operator new[]' name the function and capture nothing.
			fg_ExpectFormat
				(
					"OperatorNewArray"
					, "void *operator new[](std::size_t _Size)\n\t{\n\t\treturn operator new(_Size);\n\t}\n"
					, "void *operator new[] (std::size_t _Size)\n{\n\treturn operator new (_Size);\n}\n"
				)
			;
			// A statement in a conditional follows the depth of the one in front of it in the same branch, and the
			// comment written above it moves with it; one written elsewhere keeps its place.
			fg_ExpectFormat
				(
					"ConditionalBranchDepth"
					, "namespace NA\n{\n#if 1\n\tvoid f()\n\t{\n\t}\n#else\n\tvoid h()\n\t{\n\t}\n\n\t\t// C\n\t\tvoid g()\n\t\t{\n\t\t\treturn;\n\t\t}\n#endif\n}\n"
					, "namespace NA\n{\n#if 1\n\tvoid f()\n\t{\n\t}\n#else\n\tvoid h()\n\t{\n\t}\n\n\t// C\n\tvoid g()\n\t{\n\t\treturn;\n\t}\n#endif\n}\n"
				)
			;
			// An asm statement's section separators stand apart from its operands, those of empty sections together.
			fg_ExpectFormat
				(
					"AsmSeparators"
					, "void f()\n{\n\t__asm__ __volatile__(\"\"::\"r\"(_pFirst) : \"memory\");\n\tasm volatile(\"yield\":::\"memory\");\n\tg(NA::CB::fs_C(), a ? b : c);\n}\n"
					, "void f()\n{\n\t__asm__ __volatile__(\"\" :: \"r\"(_pFirst) : \"memory\");\n\tasm volatile(\"yield\" ::: \"memory\");\n\tg(NA::CB::fs_C(), a ? b : c);\n}\n"
				)
			;
			// Separators written apart stay apart, since written together they read as one '::'.
			CStr AsmApart = "void f()\n{\n\t__asm__(\"mov %0\" : : \"r\"(Value) : \"memory\");\n}\n";
			fg_ExpectFormat("AsmSeparatorsApart", AsmApart, AsmApart);
			// A pointer to member function's declarator stands apart from the return type in front of it, and its
			// '*' apart from the class and on the name.
			fg_ExpectFormat
				(
					"MemberPointerParameter"
					, "void f(tf_CReturn(CFoo::* _pA)(int), tf_C(NA::TCB<int>::* _pB)(int));\nvoid g()\n{\n\tf(CFoo::fs_Get(), 1);\n}\n"
					, "void f(tf_CReturn (CFoo:: *_pA)(int), tf_C (NA::TCB<int>:: *_pB)(int));\nvoid g()\n{\n\tf(CFoo::fs_Get(), 1);\n}\n"
				)
			;
			// A clause's variable is declared like any other, and its operands stay operands.
			fg_ExpectFormat
				(
					"ClauseDeclarators"
					, "void f()\n{\n\tfor (auto && Element : _BSON)\n\t\tg(Element);\n\tfor (auto & [A, B] : _BSON)\n\t\tg(A);\n\tfor (char** p = g(); *p; ++p)\n\t\tg(p);\n"
					"\tif (auto && Value = g())\n\t\tg(Value);\n\tif (a && b)\n\t\tg(a);\n}\n"
					, "void f()\n{\n\tfor (auto &&Element : _BSON)\n\t\tg(Element);\n\tfor (auto &[A, B] : _BSON)\n\t\tg(A);\n\tfor (char **p = g(); *p; ++p)\n\t\tg(p);\n"
					"\tif (auto &&Value = g())\n\t\tg(Value);\n\tif (a && b)\n\t\tg(a);\n}\n"
				)
			;
			// A named call streamed into a plain target opens rather than taking a line of its own.
			CStr StreamedWord;
			for (umint i = 0; i < 110; ++i)
				StreamedWord += "W";

			fg_ExpectFormat
				(
					"StreamedCallOpens"
					, "void f()\n{\n\tParams << fg_CreateVector<CStr>(\"" + StreamedWord + "\", \"--forceTableScan\", \"--quiet\", \"--oplog\");\n}\n"
					, "void f()\n{\n\tParams << fg_CreateVector<CStr>\n\t\t(\n\t\t\t\""
					+ StreamedWord
					+ "\"\n\t\t\t, \"--forceTableScan\"\n\t\t\t, \"--quiet\"\n"
					"\t\t\t, \"--oplog\"\n\t\t)\n\t;\n}\n"
				)
			;
			// A call behind a leading parenthesis's closing marker opens at the member's level,
			// which is the statement's.
			fg_ExpectFormat
				(
					"LeadingParenMemberCall"
					, "void f()\n{\n\t(\n\t\tg_D / [A]() mutable -> TCFuture<CB>\n\t\t{\n\t\t\tco_return {};\n\t\t}\n\t)\n\t.f_OnResultSet\n\t\t(\n"
					"\t\t\t[Promise](TCAsyncResult<CB> &&_Result)\n\t\t\t{\n\t\t\t\tPromise.f_SetResult(fg_Move(_Result));\n\t\t\t}\n\t\t);\n}\n"
					, "void f()\n{\n\t(\n\t\tg_D / [A]() mutable -> TCFuture<CB>\n\t\t{\n\t\t\tco_return {};\n\t\t}\n\t)\n\t.f_OnResultSet\n\t(\n"
					"\t\t[Promise](TCAsyncResult<CB> &&_Result)\n\t\t{\n\t\t\tPromise.f_SetResult(fg_Move(_Result));\n\t\t}\n\t);\n}\n"
				)
			;
			// A line comment ends the line it stands on, so what stands behind it is a
			// line of its own, measured as one: a base clause under a commented head
			// stays closed where it fits, and comes back closed where it was opened.
			fg_ExpectFormat
				(
					"CommentHead"
					, "template <typename t_C>\nclass TCFoo // Comment\n\t: public TCBase\n\t<\n\t\tt_C\n\t\t, CEmpty\n\t>\n{\n\tint m_A;\n};\n"
					, "template <typename t_C>\nclass TCFoo // Comment\n\t: public TCBase<t_C, CEmpty>\n{\n\tint m_A;\n};\n"
				)
			;
			fg_ExpectFormat
				(
					"CommentMembers"
					, "void f()\n{\n\tco_return co_await m_Promises\n.f_Insert()  // why\n\t\t\t\t\t\t.f_Future()\n\t\t;\n}\n"
					, "void f()\n{\n\tco_return co_await m_Promises\n\t\t.f_Insert() // why\n\t\t.f_Future()\n\t;\n}\n"
				)
			;
			// A string with a marker behind it starts an object only as a key, with its '='.
			fg_ExpectFormat("DSLMarkerNoKey", "auto g_A{\n\t\"a\"_ch >> b};\n", "auto g_A\n\t{\n\t\t\"a\"_ch >> b\n\t}\n;\n");
			// A DSL's array written across lines keeps them, as an initializer does, and one
			// written on a line is only opened where it does not fit. Its bracket stands on a
			// line of its own, and what it holds moves with it.
			fg_ExpectFormat
				(
					"DSLArrays"
					, "void f()\n{\n\tauto Value =\n\t\t{\n\t\t\t\"A\"_o= _o\n\t\t\t[\n\t\t\t\t1\n\t\t\t]\n\t\t\t, \"B\"_o= _o[\n\t\t\t\t2\n\t\t\t\t, 3\n\t\t\t]\n"
					"\t\t\t, \"C\"_o= _o[4, 5]\n\t\t}\n\t;\n}\n"
					, "void f()\n{\n\tauto Value =\n\t\t{\n\t\t\t\"A\"_o= _o\n\t\t\t[\n\t\t\t\t1\n\t\t\t]\n\t\t\t, \"B\"_o= _o\n\t\t\t[\n\t\t\t\t2\n\t\t\t\t, 3\n\t\t\t]\n"
					"\t\t\t, \"C\"_o= _o[4, 5]\n\t\t}\n\t;\n}\n"
				)
			;
			// Within a list of data, a blank line sets nothing apart, and a closing marker
			// hugging the last element takes a line of its own. A directive keeps its lines.
			fg_ExpectFormat
				(
					"DSLListCorrections"
					, "void f()\n{\n\tauto Value =\n\t\t{\n\t\t\t\"A\"_o= _o[\n\t\t\t\t1\n\n\t\t\t\t, 2]\n"
					"\t\t\t, \"B\"_o=\n\t\t\t{\n\t\t\t\t\"C\"_o= 3\n\n\t\t\t\t, \"D\"_o= 4}\n"
					"\t\t\t, \"E\"_o=\n\t\t\t{\n#ifdef DFoo\n\t\t\t\t\"F\"_o= 5\n#endif\n\t\t\t}\n\t\t}\n\t;\n}\n"
					, "void f()\n{\n\tauto Value =\n\t\t{\n\t\t\t\"A\"_o= _o\n\t\t\t[\n\t\t\t\t1\n\t\t\t\t, 2\n\t\t\t]\n"
					"\t\t\t, \"B\"_o=\n\t\t\t{\n\t\t\t\t\"C\"_o= 3\n\t\t\t\t, \"D\"_o= 4\n\t\t\t}\n"
					"\t\t\t, \"E\"_o=\n\t\t\t{\n#ifdef DFoo\n\t\t\t\t\"F\"_o= 5\n#endif\n\t\t\t}\n\t\t}\n\t;\n}\n"
				)
			;
			// An attribute behind a lambda's capture list belongs to its introducer, apart from
			// the list and the parameters as an attribute macro is, and so does one behind a
			// condition: the body is a lambda's, and the arrow its return type's.
			fg_ExpectFormat
				(
					"LambdaAttribute"
					, "void f()\n{\n\tauto fA = [][[nodiscard]]()->int\n\t{\n\t\treturn 1;\n\t}\n\t;\n\tif (bFlag)[[unlikely]]\n\t\treturn;\n}\n"
					, "void f()\n{\n\tauto fA = [] [[nodiscard]] () -> int\n\t\t{\n\t\t\treturn 1;\n\t\t}\n\t;\n\tif (bFlag) [[unlikely]]\n\t\treturn;\n}\n"
				)
			;
			// A name between a lambda's parameter list and its arrow is an attribute macro,
			// and the arrow behind it starts the return type.
			fg_ExpectFormat
				(
					"LambdaAttributeMacro"
					, "void f()\n{\n\tauto fA = [&](umint _nChars) inline_always_lambda->ch8 *\n\t\t{\n\t\t\treturn nullptr;\n\t\t}\n\t;\n}\n"
					, "void f()\n{\n\tauto fA = [&](umint _nChars) inline_always_lambda -> ch8 *\n\t\t{\n\t\t\treturn nullptr;\n\t\t}\n\t;\n}\n"
				)
			;
			// A string continued on the next line stands one level in under the line that starts
			// its statement, and at the level of an element or an operand line it continues.
			fg_ExpectFormat
				(
					"StringContinuations"
					, "void f()\n{\n\tText = \"a \"\n\"b\";\n\tg\n\t\t(\n\t\t\t\"c \"\n\t\t\t\t\"d\"\n"
					"\t\t\t, \"e \"\n\t\t\t\t\t\"f\"\n\t\t\t\t\"g\"\n\t\t)\n\t;\n}\n"
					, "void f()\n{\n\tText = \"a \"\n\t\t\"b\"\n\t;\n\tg\n\t\t(\n\t\t\t\"c \"\n\t\t\t\"d\"\n"
					"\t\t\t, \"e \"\n\t\t\t\"f\"\n\t\t\t\"g\"\n\t\t)\n\t;\n}\n"
				)
			;
			// A conditional in a bracket whose branches continue strings over lines gives at its '?' and at the ':'
			// answering it, which stands behind a string continued in front of it.
			fg_ExpectFormat
				(
					"ContinuedStringConditional"
					, "void f()\n{\n\tg(c ? \"a\"\n\t\t\"b\"\n\t\t: \"c\"\n\t\t\"d\");\n}\n"
					, "void f()\n{\n\tg\n\t\t(\n\t\t\tc\n\t\t\t? \"a\"\n\t\t\t\"b\"\n\t\t\t: \"c\"\n\t\t\t\"d\"\n\t\t)\n\t;\n}\n"
				)
			;
			// The elements of a braced list holding raw strings over lines stand one level in from its braces, where
			// they start their lines, and the strings' lines stay as they are written.
			fg_ExpectFormat
				(
					"RawStringElementsLevelled"
					, "namespace N\n{\nchar const *g_pA[2] =\n\t{\nR\"(\n\ta\n)\"\n, R\"(\n\tb\n)\"\n\t}\n;\n}\n"
					, "namespace N\n{\n\tchar const *g_pA[2] =\n\t\t{\n\t\t\tR\"(\n\ta\n)\"\n\t\t\t, R\"(\n\tb\n)\"\n\t\t}\n\t;\n}\n"
				)
			;
			// A string continuing one on a line placed the same way stands where that line is placed.
			fg_ExpectFormat
				(
					"StringContinuationChain"
					, "void f()\n{\n  if (a)\n  {\n    e << \"a\"\n      \"b\" << B << \"c\"\n      \"d\";\n  }\n}\n"
					, "void f()\n{\n\tif (a)\n\t{\n\t\te\n\t\t\t<< \"a\"\n\t\t\t\"b\" << B << \"c\"\n\t\t\t\"d\"\n\t\t;\n\t}\n}\n"
				)
			;
			// A trailing comment stands one space behind its code, and the comment lines that
			// continue it follow it there; one at the code's own indentation is its own.
			fg_ExpectFormat
				(
					"TrailingComments"
					, "enum EStatus\n{\n\tEStatus_A\t\t= 1\t/// First\n\t\t\t\t\t\t/// continued\n\t\t\t\t\t\t/// and again\n"
					"\t, EStatus_Longer\t= 2// Second\n\t// Own comment\n\t, EStatus_C = 3\n};\n"
					, "enum EStatus\n{\n\tEStatus_A = 1 /// First\n\t\t\t\t  /// continued\n\t\t\t\t  /// and again\n"
					"\t, EStatus_Longer = 2 // Second\n\t// Own comment\n\t, EStatus_C = 3\n};\n"
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
			// A member function's body under its head moves with the class the layout moves in, also where the source
			// wrote it where it ends up.
			fg_ExpectFormat
				(
					"BodyInMovedClass"
					, "namespace N\n{\nstruct CA\n{\n\t\tvoid operator () (int _A) const\n\t\t{\n\t\t}\n};\n}\n"
					, "namespace N\n{\n\tstruct CA\n\t{\n\t\tvoid operator () (int _A) const\n\t\t{\n\t\t}\n\t};\n}\n"
				)
			;
			// An element a comment stands above takes the list's level like the others, with the comment.
			fg_ExpectFormat
				(
					"ListElementBehindComment"
					, "void f() {\n\t\tch8 const *Names[] = {\n\t\t\t\t// A\n\t\t\t\t\"a\", \"b\"\n\t\t};\n}\n"
					, "void f()\n{\n\tch8 const *Names[] =\n\t\t{\n\t\t\t// A\n\t\t\t\"a\"\n\t\t\t, \"b\"\n\t\t}\n\t;\n}\n"
				)
			;
			// So does one written across lines, its first line at the level.
			fg_ExpectFormat
				(
					"ListElementAcrossLinesLevelled"
					, "void f()\n{\n\tCA Values[] = {\n\t\t\tfg_A(\n\t\t\t\t1)\n\t};\n}\n"
					, "void f()\n{\n\tCA Values[] =\n\t\t{\n\t\t\tfg_A(1)\n\t\t}\n\t;\n}\n"
				)
			;
			// A group a directive keeps open has its closing marker under the opening one the layout moves.
			fg_ExpectFormat("GroupClosedBehindDirective", "int Values{ (\n#include \"Values.inc\"\n) };\n", "int Values\n\t{\n\t\t(\n#include \"Values.inc\"\n\t\t)\n\t}\n;\n");
			// The comment lines closing a block move with the statement above them, also below a trailing comment
			// they are written at the code's depth under.
			fg_ExpectFormat
				(
					"CommentClosingMovedBlock"
					, "void f() {\n\t\tg(); // A\n\t\t// B\n}\nvoid h() {\n\t\tg();\n\t\t// C\n}\n"
					, "void f()\n{\n\tg(); // A\n\t// B\n}\n\nvoid h()\n{\n\tg();\n\t// C\n}\n"
				)
			;
			// And the ones a blank line the layout removes stands behind.
			fg_ExpectFormat("CommentBeforeBlankClosingMovedBlock", "void f() {\n\t\tg(); // A\n\t\t// B\n\n}\n", "void f()\n{\n\tg(); // A\n\t// B\n}\n");
			// One at the start of its line moves with the code it stands in front of, commented out code included.
			fg_ExpectFormat("LineStartCommentMoved", "namespace N\n{\n// A\nint g_A;\n// int g_B;\n\tint g_C;\n}\n", "namespace N\n{\n\t// A\n\tint g_A;\n\t// int g_B;\n\tint g_C;\n}\n");
			// A block comment over several lines moves with each of its lines.
			fg_ExpectFormat("MultiLineCommentMoved", "namespace N\n{\n/**\n * A\n */\nint g_A;\n}\n", "namespace N\n{\n\t/**\n\t * A\n\t */\n\tint g_A;\n}\n");
			// So do the ones a blank line parts from the code below them.
			fg_ExpectFormat("CommentBeforeBlankMovedBlock", "void f() {\n\t\tg(); // A\n\t\t// B\n\n\t\th();\n}\n", "void f()\n{\n\tg(); // A\n\t// B\n\n\th();\n}\n");
			// The statement above starts its line with the block comment naming it.
			fg_ExpectFormat("CommentBeforeBlankBelowNamedStatement", "void f() {\n\t\t/* A */ g();\n\t\t// B\n\n\t\th();\n}\n", "void f()\n{\n\t/* A */ g();\n\t// B\n\n\th();\n}\n");
			// Ones blank lines part from the code on both sides move with the code below.
			fg_ExpectFormat("CommentPartedMovedBlock", "void f() {\n\t\tg();\n\n\t\t// A\n\n\t\th();\n}\n", "void f()\n{\n\tg();\n\n\t// A\n\n\th();\n}\n");
			// So do the ones in front of a directive.
			fg_ExpectFormat
				(
					"CommentBeforeDirectiveMovedBlock"
					, "void f() {\n\t\tg(); // A\n\t\t// B\n#ifdef DX\n\t\th();\n\t\t// C\n#endif\n}\n"
					, "void f()\n{\n\tg(); // A\n\t// B\n#ifdef DX\n\th();\n\t// C\n#endif\n}\n"
				)
			;
			// So does a block holding one.
			fg_ExpectFormat
				(
					"RawStringBlockMoved"
					, "\tnamespace N\n\t{\n\t\tvoid fg_F()\n\t\t{\n\t\t\tch8 const *pA = R\"(\nx\n)\";\n\t\t}\n\t}\n"
					, "namespace N\n{\n\tvoid fg_F()\n\t{\n\t\tch8 const *pA = R\"(\nx\n)\";\n\t}\n}\n"
				)
			;
			// A raw string in a directive holds the directive's lines.
			fg_ExpectFormat("RawStringInDirective", "#define DCode R\"(\na \"\n)\"\nint g_A;\n", "#define DCode R\"(\na \"\n)\"\nint g_A;\n");
			// A statement holding a raw string moves to its depth, the string's lines as they are.
			fg_ExpectFormat("RawStringStatementMoved", "namespace N\n{\nch8 const *g_pA = R\"---(\n\ta\n)---\";\n}\n", "namespace N\n{\n\tch8 const *g_pA = R\"---(\n\ta\n)---\";\n}\n");
			// A statement a block comment keeps the lines of moves to its depth with all of them.
			fg_ExpectFormat("BlockCommentStatementMoved", "void f()\n{\n\t\t\tg(a,\n\t\t\t\t/*x=*/b);\n}\n", "void f()\n{\n\tg(a, /*x=*/b);\n}\n");
			// One on its header's line places its body at the header's level.
			fg_ExpectFormat("TemplateHeaderLineMoved", "\t\ttemplate <typename t_C> struct TCA\n\t\t{\n\t\t\tint m_A;\n\t\t};\n", "template <typename t_C> struct TCA\n{\n\tint m_A;\n};\n");
			// One too long for its header's line starts a line of its own at the header's level.
			{
				CStr Parameter;
				for (umint i = 0; i < 160; ++i)
					Parameter += "a";

				fg_ExpectFormat
					(
						"TemplateHeaderLineSplit"
						, "\ttemplate <typename t_C" + Parameter + "> struct TCA {\n\t};\n"
						, "template <typename t_C" + Parameter + ">\nstruct TCA\n{\n};\n"
					)
				;
			}
			// The declaration under a header stands at the header's level, also in a block the layout moves.
			fg_ExpectFormat
				(
					"TemplateHeaderMoved"
					, "struct CA {\n        template <typename t_C>\n            void f(t_C _A);\n};\n"
					, "struct CA\n{\n\ttemplate <typename t_C>\n\tvoid f(t_C _A);\n};\n"
				)
			;
			fg_ExpectFormat("Requires", "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires cFoo<t_C>\n;\n", "template <typename t_C>\nvoid fg_F(t_C _A)\n\trequires cFoo<t_C>\n;\n");
			// A label and its body are separate statements, and a body of one statement and a
			// 'break' comes onto the label's line where it fits; a clause, more statements and a
			// comment keep it under the label.
			fg_ExpectFormat
				(
					"CaseLabel"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1:\n\t\tg();\n\t\tbreak;\n\tcase 2:\n\t\treturn 3;\n\tcase 3:\n\t\tif (x)\n\t\t\tg();\n\t\tbreak;\n"
					"\tcase 4:\n\t\tg();\n\t\th();\n\t\tbreak;\n\tcase 5:\n\t\t// Why\n\t\tg();\n\t\tbreak;\n\t}\n}\n"
					, "void f()\n{\n\tswitch (a)\n\t{\n\tcase 1: g(); break;\n\tcase 2: return 3;\n\tcase 3:\n\t\tif (x)\n\t\t\tg();\n\t\tbreak;\n"
					"\tcase 4:\n\t\tg();\n\t\th();\n\t\tbreak;\n\tcase 5:\n\t\t// Why\n\t\tg();\n\t\tbreak;\n\t}\n}\n"
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
	}
}
