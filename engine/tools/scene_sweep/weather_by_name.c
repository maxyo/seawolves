// SPDX-License-Identifier: GPL-3.0-only
// The sweep's weather calls for a game whose weathers are named per hour (SetNextWeather, bStorm).

void SceneSweep_DropOpeningWeather()
{
	// A new game leaves its opening weather (seadogs.c startGameWeather) pending until the next
	// CreateWeatherEnvironment, which applies it over the location's QuestlockWeather and then clears it. The
	// intro did not use it up, so the first scene of the sweep got it. Every scene sets its own time and weather.
	startGameWeather = false;
}

// The weather of a whole hour, for the time the scene has just set. Without rain: the day's rain is drawn at random
// (WhrWeather.c) and its clouds would change a scene's sky from one sweep to the next.
void SceneSweep_Weather(int hour)
{
	bRains = false;
	WeatherParams.Rain.ThisDay = false;
	string name = "0 Hours";
	if(hour != 0) name = its(hour) + " Hour";
	SetNextWeather(name);
}

bool SceneSweep_Storm()
{
	return bStorm;
}
