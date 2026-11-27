// SPDX-License-Identifier: GPL-3.0-only
// A musket as the game tells it: characterUtilite.c IsMushketItem, which its equipment code asks too (a script fix of
// TEHO 1.7.3). Implements contracts/musket.c.

bool TouchAdpMusket_Is(string id)
{
	return IsMushketItem(id);
}
