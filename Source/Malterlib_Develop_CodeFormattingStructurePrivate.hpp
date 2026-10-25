// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDevelop::NPrivate
{
	template <umint t_nTexts>
	bool fg_IsAnyText(CCodeTokenStream const &_Tokens, CCodeToken const &_Token, ch8 const * const (&_pTexts)[t_nTexts])
	{
		for (auto pText : _pTexts)
		{
			if (_Tokens.f_IsText(_Token, pText))
				return true;
		}

		return false;
	}
}
