// SPDX-License-Identifier: GPL-3.0-only
// The sweep's weather calls for a game that makes its weather from the hour itself (Whr_IsStorm): setting the time is
// all a scene needs.

void SceneSweep_DropOpeningWeather()
{
}

void SceneSweep_Weather(int hour)
{
}

bool SceneSweep_Storm()
{
	return Whr_IsStorm();
}
