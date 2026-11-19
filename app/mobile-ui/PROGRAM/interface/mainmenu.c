// SPDX-License-Identifier: GPL-3.0-only
// Adapted for Android from program/interface/mainmenu.c of storm-devs/sd-teho-public (GPL-3.0) by the
// Seawolves contributors: the touch title screen replaces the 3D menu labels, «Продолжить» loads the
// newest record, and the store client's update check is removed. Origin and upstream hash:
// mobile-ui/SOURCE.json.
string isUsersName="";

int iChar;
#event_handler("Control Activation","TempIControlProcess");
#event_handler("CannonFire", "CannonFire");
#event_handler("AndroidContinue", "AndroidContinue");

void TempIControlProcess()
{
	string ControlName;
	ControlName = GetEventData();
	if (ControlName == "WhrPrevWeather")
	{
		Sea.MaxSeaHeight = 1.0;
		Sea.isDone = "";
	}
	if (ControlName == "WhrNextWeather")
	{
		Sea.MaxSeaHeight = 1.0;
		Sea.isDone = "";
	}
	if (ControlName == "WhrUpdateWeather")
	{
		Sea.MaxSeaHeight = 1.0;
		Sea.isDone = "";
	}
}

void InitInterface(string iniName)
{
    if (LoadSegment("interface/android_newgame.c"))
    {
        string androidValidate = "AndroidTutorialSkipValidate";
        call androidValidate();
    }
	string Vers;

	Event("DoInfoShower","sl","MainMenuLaunch",false);

	aref arScrShoter;
	if( !GetEntity(&arScrShoter,"scrshoter") )
	{
		CreateScreenShoter();
	}

	CreateBackEnvironment();

    SendMessage(&GameInterface,"ls",MSG_INTERFACE_INIT,iniName);

	Vers = VERSION_NUMBER1 + GetVerNum();

	CreateString(true,"VerNum", Vers, FONT_NORMAL, COLOR_NORMAL, 780, 580, SCRIPT_ALIGN_RIGHT, 0.9);

	SetEventHandler("backgroundcommand","ProcCommand",0);

	GameInterface.SavePath = "SAVE";

	ResetSound();
	// ВВОД СВОИХ СХЕМ В ЗАВИСИМОСТИ ОТ ПОГОДЫ (BY LOKK)
	if (Whr_IsRain())
	{
		if (Whr_IsNight())
		{
			SetSoundScheme("mainmenu_night_rain");
		}
		else
		{
			SetSoundScheme("mainmenu_day_rain");
		}
    }
	else
	{
		if (Whr_IsNight())
		{
			SetSoundScheme("mainmenu_night");
		}
		else
		{
			SetSoundScheme("mainmenu_day");
		}

	}
	oldMusicID = 0;
	musicName = "";
	SetMusic("music_MainMenu");
}

void NewGamePress()
{
	IDoExit( RC_INTERFACE_DO_NEW_GAME, true );
}

void LoadPress()
{
	IDoExit( RC_INTERFACE_DO_LOAD_GAME, false );
}

// «Продолжить» of the Android title screen: the engine names the newest record (storm engine
// xinterface, main:Continue) and it is loaded at once, with no save list in between: the title
// closes and evntLoad is posted as save_load.c ProcessLoad does. Answers 1 (the engine logs an
// error when no handler answers).
int AndroidContinue()
{
	string profile = GetEventData();
	string file = GetEventData();
	PlayerProfile.name = profile;

	IDoExit(RC_INTERFACE_SAVELOAD_EXIT, true);
	ResetSound();

	SetEventHandler("evntLoad","LoadGame",1);
	PostEvent("evntLoad", 0, "s", "SAVE\"+profile+"\"+file);
	Event("evntPreLoad");
	return 1;
}

void SavePress()
{
	IDoExit( RC_INTERFACE_DO_SAVE_GAME, false );
}

void OptionsPress()
{
	IDoExit( RC_INTERFACE_DO_OPTIONS, false );
}

void ControlsPress()
{
	IDoExit( RC_INTERFACE_DO_CONTROLS, false );
}

void CreditsPress()
{
	IDoExit( RC_INTERFACE_DO_CREDITS, false );
}

void QuitPress()
{
    DelEventHandler("frame","QuitPress");
	EngineLayersOffOn(false);
	IDoExit(-1, false);
	ExitProgram();
}

void procGameQuit()
{
	ExitProgram();
}

void MultiPress()
{
	DeleteBackEnvironment();
	IDoExit(RC_INTERFACE_NET_FINDGAME, false);
	InterfaceStates.BackEnvironmentIsCreated = false;
}

void IDoExit(int exitCode, bool bClear)
{
	InterfaceStates.BackEnvironmentIsCreated = true;

	DelEventHandler("backgroundcommand","ProcCommand");

	interfaceResultCommand = exitCode;
	EndCancelInterface(bClear);
}

void ExitCancel()
{
	IDoExit(-1,true);
}

void ProcCommand()
{
	string comName = GetEventData();

	switch(comName)
	{
		case "New":			NewGamePress(); 	break;
		case "Load":		LoadPress(); 		break;
		case "Save":		SavePress(); 		break;
		case "Options":		OptionsPress(); 	break;
		case "Credits":		CreditsPress(); 	break;
		case "Exit":		SetEventHandler("frame","QuitPress",0);  break;
	}
}

object InterfaceBackScene;
void CreateBackEnvironment()
{
	LayerFreeze(EXECUTE,false);
	LayerFreeze(REALIZE,false);

	if( CheckAttribute(&InterfaceStates,"BackEnvironmentIsCreated") && InterfaceStates.BackEnvironmentIsCreated=="1" )
	{
		return;
	}

	// сбрасываем цвет фона на 0
	Render.BackColor = 0;
	// выключаем эффект моря
	Render.SeaEffect = false;
	// 14.07.2007 - отключаем подводную часть
	Sea.UnderWater = false;
	bMainCharacterInFire = false;
	bMainMenu = true;

	// create weather
	ICreateWeather();

	CreateEntity(&InterfaceBackScene,"InterfaceBackScene");
	LayerAddObject(EXECUTE, &InterfaceBackScene, -1);
	LayerAddObject(REALIZE, &InterfaceBackScene, 1000);

	SendMessage(&InterfaceBackScene, "ls", 0, "MainMenu\MainMenu"); // set model
	SendMessage(&InterfaceBackScene, "ls", 1, "camera"); // set camera

	// No menu list: the title's commands come from the touch interface ("backgroundcommand").

	// create ship
	MainMenu_CreateShip();

/*
	if( Whr_IsNight() )
	{
		InterfaceBackScene.light.turnon = true;
		InterfaceBackScene.light.model = "mainmenu\Fonar_night";
		InterfaceBackScene.light.lightcolormin = argb(0,200,200,120);//argb(255,114,114,80);
		InterfaceBackScene.light.lightcolormax = argb(48,255,255,180);
		InterfaceBackScene.light.colorperiod = 0.4;
		InterfaceBackScene.light.addcolorperiod = 1.0;
		InterfaceBackScene.light.rangemin = 10.0;
		InterfaceBackScene.light.rangemax = 14.0;
		InterfaceBackScene.light.rangeperiod = 1.5;
		InterfaceBackScene.light.locator = "Light";
		InterfaceBackScene.light.lightlocator = "fonar";
		InterfaceBackScene.light.flarelocator = "fire";
		InterfaceBackScene.light.flaresize = 0.5;
		InterfaceBackScene.light.minflarecolor = 120.0;
		InterfaceBackScene.light.maxflarecolor = 200.0;
	} else {
		MainMenu_CreateAnimals();

		InterfaceBackScene.light.turnon = false;
		InterfaceBackScene.light.model = "mainmenu\Fonar_day";
		InterfaceBackScene.light.locator = "Light";
		InterfaceBackScene.light.lightlocator = "fonar";
	}
	SendMessage(&InterfaceBackScene, "ls", 8, "light" );

	if( Whr_IsNight() )
	{
		// create particles
		InitParticles();
		CreateParticleSystem("candle", stf(InterfaceBackScene.lightpos.x),stf(InterfaceBackScene.lightpos.y),stf(InterfaceBackScene.lightpos.z), 0.0,0.0,0.0, 0);
	}
*/
	bMainMenu = false;
}

void MainMenu_CreateShip()
{
	//int
	iChar = GenerateCharacter(rand(4), WITHOUT_SHIP, "citizen", MAN, 0, WARRIOR);   //PIRATE
	characters[iChar].id = "menuCharacter";
	ref	rCharacter = &characters[iChar];
	int nChoosedBaseShipType = rand(SHIP_LUGGER) + 1;//SHIP_TARTANE;SHIP_LUGGER;SHIP_CARAVEL;
	rCharacter.ship.type = GenerateShip(nChoosedBaseShipType, false);
	SetBaseShipData( rCharacter );
	ref refBaseShip = GetRealShip( sti(rCharacter.ship.type) );
	refBaseShip.SpeedRate = 1.0; // чтоб не летал
	Ship_SetFantomData( rCharacter );
	Ship_ClearImpulseStrength( rCharacter );
	Ship_SetLightsAndFlares( rCharacter );
	Ship_SetTrackSettings( rCharacter );

	rCharacter.Features.GeraldSails = true;
	rCharacter.Ship.Speed.z = 2.0;
	//rCharacter.Ship.Speed.z = 5.0;
	//rCharacter.Ship.Stopped = true;
	rCharacter.Ship.Pos.Mode = SHIP_WAR;
	rCharacter.Ship.Pos.x = 38.175;
	rCharacter.Ship.Pos.z = -69.56;
	rCharacter.Ship.Ang.y = -1.92;
	rCharacter.Ship.Strand = false;
	rCharacter.Ship.Strand.SndID = -1;

	rCharacter.Ship.LastBallCharacter = -1;
	rCharacter.Ship.WindDotShip = 1.0;

	rCharacter.TmpPerks = "";
	aref aTmpPerks; makearef(aTmpPerks, rCharacter.TmpPerks);
	aTmpPerks.StormProfessional		= 0;

	SendMessage(&InterfaceBackScene, "lsa", 2, "ship", &rCharacter); // set ship position
	CreateEntity( rCharacter, "ship" );
	ref rBaseShip = GetRealShip(sti(rCharacter.ship.type));
	SendMessage( rCharacter, "laa", MSG_SHIP_CREATE, &rCharacter, &rBaseShip );

	PostEvent("Cannonfire", 3000);
	//CannonFire();
}

void DeleteBackEnvironment()
{
	LayerDelObject(EXECUTE, &InterfaceBackScene);
	LayerDelObject(REALIZE, &InterfaceBackScene);

//	MainMenu_DeleteAnimals();
	DeleteClass( GetCharacter(iChar));//  boal fix FANTOM_CHARACTERS) );
	DeleteShipEnvironment();
	DeleteWeather();
	DeleteSea();
	DeleteClass( InterfaceBackScene );
	DeleteCoastFoamEnvironment();

	iNextWeatherNum = -1;
	iCurWeatherHour = -1;
}

void MainMenu_CreateAnimals()
{
	InterfaceBackScene.seagull.locator = "seagull";
	InterfaceBackScene.seagull.model = "mainmenu\seagull";
	InterfaceBackScene.seagull.animation = "seagull";
	InterfaceBackScene.seagull.aniaction = "idle";
	SendMessage(&InterfaceBackScene, "ls", 9, "seagull" );
}

void MenuCreateLogo()
{
	InterfaceBackScene.Logo.locator = "";
	InterfaceBackScene.Logo.model = "mainmenu\AOPBoard";
	if( !Whr_IsNight() ) {
		InterfaceBackScene.Logo.technique = "InterfaceBackScene_Logo";
	}
	SendMessage(&InterfaceBackScene, "ls", 10, "Logo" );
}

void MainMenu_DeleteAnimals()
{
	if (IsEntity(&Animals))
		DeleteClass(Animals);
}

void ICreateWeather()
{
	// choose random weather
	int n = 0;
	if( CheckAttribute(&InterfaceStates,"mainmenuweather") )
	{
		n = sti(InterfaceStates.mainmenuweather);
	}
	else
	{
		n = rand(iTotalNumWeathers-1);
		int oldN=n;
		while (true)
		{
			if( !CheckAttribute( &Weathers[n], "skip" ) || Weathers[n].skip!="1" )
			{
				break;
			}
			//candle
			n++;
			if( n==iTotalNumWeathers ) {n = 0;}
			if( n==oldN ) {break;}
		}
	}
	if (n < 0 || n >= iTotalNumWeathers) n = 0;
	SetNextWeather(Weathers[n].id);
	iBlendWeatherNum = -1; // залоченная погода
	Whr_LoadNextWeather(0);
	iBlendWeatherNum = -1; // залоченная погода
	InterfaceBackScene.current_weather = n;
	InterfaceStates.mainmenuweather    = n;

	CreateSea(EXECUTE,REALIZE);
	CreateWeather(EXECUTE,REALIZE);
	CreateShipEnvironment();
	Sea.MaxSeaHeight = 1.0;
	Sea.isDone = "";

	CreateCoastFoamEnvironment("MainMenu", EXECUTE, REALIZE);

	iBlendWeatherNum = -1; // залоченная погода
}

void CannonFire()
{
	int iChar = GetCharacterIndex("menuCharacter");
	if(iChar != -1)
	{
		Ship_DoFakeFire(&characters[iChar], "cannonr", 2.0);
		PostEvent("Cannonfire", 8000 + rand(8000));
	}
}
