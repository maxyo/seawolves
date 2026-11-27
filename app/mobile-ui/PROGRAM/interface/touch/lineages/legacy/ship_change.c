// SPDX-License-Identifier: GPL-3.0-only
// The exchange of the ship screen as the game offers it: interface_utils.c IsShipChangeOffered, which ship.c
// ShipChange asks too (a script fix of TEHO 1.7.3). Implements contracts/ship_change.c.

bool TouchAdpShipChange_Offered(ref chr)
{
	return IsShipChangeOffered(chr);
}
