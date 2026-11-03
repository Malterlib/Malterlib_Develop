// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDevelop::NPrivate
{
	template <umint tf_nTexts>
	bool fg_IsAnyText(CCodeTokenStream const &_Tokens, CCodeToken const &_Token, ch8 const * const (&_Texts)[tf_nTexts])
	{
		for (auto pText : _Texts)
		{
			if (_Tokens.f_IsText(_Token, pText))
				return true;
		}

		return false;
	}
}
