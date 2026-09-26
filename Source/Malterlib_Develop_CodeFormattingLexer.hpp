// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDevelop
{
	// Every rule asks this of every token it looks at, so it is inline.
	inline_always bool CCodeTokenStream::f_IsText(CCodeToken const &_Token, ch8 const *_pText) const
	{
		auto pToken = mp_Source.f_GetStr() + _Token.m_iOffset;
		umint i = 0;
		for (; i < _Token.m_nLength; ++i)
		{
			if (_pText[i] != pToken[i])
				return false;
		}

		return _pText[i] == 0;
	}
}
