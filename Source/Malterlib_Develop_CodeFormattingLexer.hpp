// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NDevelop
{
	// Every rule asks this of every token it looks at, so it is inline. A plain name is none of the keywords its text spells.
	inline_always bool CCodeTokenStream::f_IsText(CCodeToken const &_Token, ch8 const *_pText) const
	{
		if (_Token.m_bPlainName)
			return false;

		if (_Token.m_pOperator)
		{
			umint iOperator = 0;
			while (_Token.m_pOperator[iOperator] && _Token.m_pOperator[iOperator] == _pText[iOperator])
				++iOperator;

			if (!_Token.m_pOperator[iOperator] && !_pText[iOperator])
				return true;
		}

		auto pToken = mp_Source.f_GetStr() + _Token.m_iOffset;
		umint i = 0;
		for (; i < _Token.m_nLength; ++i)
		{
			if (_pText[i] != pToken[i])
				return false;
		}

		return _pText[i] == 0;
	}

	inline_always bool CCodeTokenStream::f_HasRole(CCodeToken const &_Token, ECodeNameRole _Role) const
	{
		return (_Token.m_Roles & _Role) != ECodeNameRole::mc_None;
	}
}
