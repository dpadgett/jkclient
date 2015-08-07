// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_info.c -- display information while data is being loading

#include "cg_local.h"

#define MAX_LOADING_PLAYER_ICONS	16
#define MAX_LOADING_ITEM_ICONS		26

//static int			loadingPlayerIconCount;
//static qhandle_t	loadingPlayerIcons[MAX_LOADING_PLAYER_ICONS];

void CG_LoadBar(void);

/*
======================
CG_LoadingString

======================
*/
void CG_LoadingString( const char *s ) {
	Q_strncpyz( cg.infoScreenText, s, sizeof( cg.infoScreenText ) );

	trap_UpdateScreen();
}

/*
===================
CG_LoadingItem
===================
*/
void CG_LoadingItem( int itemNum ) {
	gitem_t		*item;
	char	upperKey[1024];

	item = &bg_itemlist[itemNum];

	if (!item->classname || !item->classname[0])
	{
	//	CG_LoadingString( "Unknown item" );
		return;
	}

	strcpy(upperKey, item->classname);
	CG_LoadingString( CG_GetStringEdString("SP_INGAME",Q_strupr(upperKey)) );
}

/*
===================
CG_LoadingClient
===================
*/
void CG_LoadingClient( int clientNum ) {
	const char		*info;
	char			personality[MAX_QPATH];

	info = CG_ConfigString( CS_PLAYERS + clientNum );

/*
	char			model[MAX_QPATH];
	char			iconName[MAX_QPATH];
	char			*skin;
	if ( loadingPlayerIconCount < MAX_LOADING_PLAYER_ICONS ) {
		Q_strncpyz( model, Info_ValueForKey( info, "model" ), sizeof( model ) );
		skin = Q_strrchr( model, '/' );
		if ( skin ) {
			*skin++ = '\0';
		} else {
			skin = "default";
		}

		Com_sprintf( iconName, MAX_QPATH, "models/players/%s/icon_%s.tga", model, skin );
		
		loadingPlayerIcons[loadingPlayerIconCount] = trap_R_RegisterShaderNoMip( iconName );
		if ( !loadingPlayerIcons[loadingPlayerIconCount] ) {
			Com_sprintf( iconName, MAX_QPATH, "models/players/characters/%s/icon_%s.tga", model, skin );
			loadingPlayerIcons[loadingPlayerIconCount] = trap_R_RegisterShaderNoMip( iconName );
		}
		if ( !loadingPlayerIcons[loadingPlayerIconCount] ) {
			Com_sprintf( iconName, MAX_QPATH, "models/players/%s/icon_%s.tga", DEFAULT_MODEL, "default" );
			loadingPlayerIcons[loadingPlayerIconCount] = trap_R_RegisterShaderNoMip( iconName );
		}
		if ( loadingPlayerIcons[loadingPlayerIconCount] ) {
			loadingPlayerIconCount++;
		}
	}
*/
	Q_strncpyz( personality, Info_ValueForKey( info, "n" ), sizeof(personality) );
	Q_CleanStr( personality );

	/*
	if( cgs.gametype == GT_SINGLE_PLAYER ) {
		trap_S_RegisterSound( va( "sound/player/announce/%s.wav", personality ));
	}
	*/

	CG_LoadingString( personality );
}

/*
============
Q_vsnprintf

vsnprintf portability:

C99 standard: vsnprintf returns the number of characters (excluding the trailing
'\0') which would have been written to the final string if enough space had been available
snprintf and vsnprintf do not write more than size bytes (including the trailing '\0')

win32: _vsnprintf returns the number of characters written, not including the terminating null character,
or a negative value if an output error occurs. If the number of characters to write exceeds count, then count 
characters are written and -1 is returned and no trailing '\0' is added.

Q_vsnprintf: always appends a trailing '\0', returns number of characters written (not including terminal \0)
or returns -1 on failure or if the buffer would be overflowed.
============
*/
static int Q_vsnprintf( char *dest, int size, const char *fmt, va_list argptr ) {
	int ret;

#ifdef _WIN32
	ret = _vsnprintf( dest, size-1, fmt, argptr );
#else
	ret = vsnprintf( dest, size, fmt, argptr );
#endif
	dest[size-1] = '\0';
	if ( ret < 0 || ret >= size ) {
		return -1;
	}
	return ret;
}

static void QDECL Com_sprintf2( char *dest, int size, const char *fmt, ... ) {
	int		ret;
	va_list		argptr;

	va_start (argptr,fmt);
	ret = Q_vsnprintf (dest, size, fmt, argptr);
	va_end (argptr);
	if (ret == -1) {
		Com_Printf ("Com_sprintf: overflow of %i bytes buffer\n", size);
	}
}

// Assumes time is in msec
static void UI_PrintTime ( char *buf, int bufsize, int time ) {
	time /= 1000;  // change to seconds

	if (time > 3600) { // in the hours range
		Com_sprintf2(buf, bufsize, "%d hr %2d min", time / 3600, (time % 3600) / 60);
	} else if (time > 60) { // mins
		Com_sprintf2(buf, bufsize, "%2d min %2d sec", time / 60, time % 60);
	} else  { // secs
		Com_sprintf2(buf, bufsize, "%2d sec", time);
	}
}

// returns either string or NULL for OOR...
//
static const char *GetCRDelineatedString( const char *psStripFileRef, const char *psStripStringRef, int iIndex) {
	static char sTemp[256] = {0};
	const char *psList = CG_GetStringEdString((char *)psStripFileRef, (char *)psStripStringRef);
	char *p;

	while (iIndex--) {
		psList = strchr(psList, '\n');
		if (!psList) {
			return NULL;	// OOR
		}
		psList++;
	}

	Q_strncpyz(sTemp, psList, sizeof(sTemp));
	p = strchr(sTemp, '\n');
	if (p) {
		*p = '\0';
	}

	return sTemp;
}


/*
====================
CG_DrawInformation

Draw all the status / pacifier stuff during level loading
====================
overlays UI_DrawConnectScreen
*/
#ifdef WEB_DOWNLOAD
extern void CG_ReadableSize ( char *buf, int bufsize, int value );
extern qboolean CG_GetConfigString(int index, char *buf, int size);
#endif
#define UI_INFOFONT (UI_BIGFONT)
void CG_DrawInformation( void ) {
	const char	*s;
	const char	*info;
	const char	*sysInfo;
	int			y;
	int			value, valueNOFP;
	qhandle_t	levelshot;
	char		buf[1024];
	int			iPropHeight = 18;	// I know, this is total crap, but as a post release asian-hack....  -Ste

#ifdef WEB_DOWNLOAD
	char sDownLoading[256];
	char sEstimatedTimeLeft[256];
	char sTransferRate[256];
	char sOf[20];
	char sCopied[256];
	char sSec[20];
	int downloadSize, downloadCount, downloadTime;
	char dlSizeBuf[64], totalSizeBuf[64], xferRateBuf[64], dlTimeBuf[64];
	int xferRate;
	int leftWidth;
	int centerPoint = 320;
	int yStart = 130;
    float scale = 1.0f;
	const char *downloadName = cgs.dlname;
#endif


#ifdef WEB_DOWNLOAD
	vec4_t colorLtGreyAlpha = {0, 0, 0, .5};

	if(cgs.is_downloading!=0) {
		char motdString[1024];
		char dlInfo[MAX_INFO_VALUE];
		extern int startt,t;
		levelshot = trap_R_RegisterShaderNoMip( "menu/art/unknownmap_mp" );
		trap_R_SetColor( NULL );
		CG_DrawPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, levelshot );
		dlInfo[0] = '\0';
		if( CG_GetConfigString( CS_SERVERINFO, dlInfo, sizeof(dlInfo) ) ) {
		//if ( info = CG_ConfigString( CS_SERVERINFO ) ) {
			const char *psLoading = CG_GetStringEdString("MENUS", "LOADING_MAPNAME");
			UI_DrawProportionalString( 320, 128-32, va( (char *)psLoading, Info_ValueForKey( dlInfo, "mapname" )),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );		
		} else {
			const char *psAwaitingSnapshot = CG_GetStringEdString("MENUS", "AWAITING_SNAPSHOT");
			UI_DrawProportionalString( 320, 128-32, psAwaitingSnapshot,
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );			
		}

		// don't print server lines if playing a local game
		trap_Cvar_VariableStringBuffer( "sv_running", buf, sizeof( buf ) );
		if ( !atoi( buf ) ) {
			trap_Cvar_VariableStringBuffer( "cl_motdString", motdString, sizeof( motdString ) );

			if (motdString[0]) {
				UI_DrawProportionalString( 320, 425, motdString,
					UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			}
		}

		CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, colorLtGreyAlpha );

    	s = GetCRDelineatedString("MENUS","DOWNLOAD_STUFF", 0);	// "Downloading:"
		Q_strncpyz(sDownLoading,s?s:"", sizeof(sDownLoading));
    	s = GetCRDelineatedString("MENUS","DOWNLOAD_STUFF", 1);	// "Estimated time left:"
		Q_strncpyz(sEstimatedTimeLeft,s?s:"", sizeof(sEstimatedTimeLeft));
    	s = GetCRDelineatedString("MENUS","DOWNLOAD_STUFF", 2);	// "Transfer rate:"
		Q_strncpyz(sTransferRate,s?s:"", sizeof(sTransferRate));
    	s = GetCRDelineatedString("MENUS","DOWNLOAD_STUFF", 3);	// "of"
		Q_strncpyz(sOf,s?s:"", sizeof(sOf));
    	s = GetCRDelineatedString("MENUS","DOWNLOAD_STUFF", 4);	// "copied"
		Q_strncpyz(sCopied,s?s:"", sizeof(sCopied));
	    s = GetCRDelineatedString("MENUS","DOWNLOAD_STUFF", 5);	// "sec."
		Q_strncpyz(sSec,s?s:"", sizeof(sSec));

		downloadSize = cgs.dltotal;
		if( downloadSize == 0 ) return;
		downloadCount = cgs.dlnow;
		if(!t)t = trap_Milliseconds();
		downloadTime = t - startt;
		
		leftWidth = 320;

		trap_R_SetColor(colorWhite);

		#define Text_PaintCenter(x,y,scale,color,text,adjust,iMenuFont) \
				UI_DrawScaledProportionalString(x,y,text,UI_CENTER|UI_BIGFONT|UI_DROPSHADOW,color,scale)
		Text_PaintCenter(centerPoint, yStart + 112, scale, colorWhite, sDownLoading, 0, iMenuFont);
		Text_PaintCenter(centerPoint, yStart + 192, scale, colorWhite, sEstimatedTimeLeft, 0, iMenuFont);
		Text_PaintCenter(centerPoint, yStart + 248, scale, colorWhite, sTransferRate, 0, iMenuFont);

		if (downloadSize > 0) {
			s = va("%s (%d%%)", downloadName, (int)((float)downloadCount * 100.0f / downloadSize));
		} else {
			s = downloadName;
		}

		Text_PaintCenter(centerPoint, yStart+136, scale, colorWhite, s, 0, iMenuFont);

		CG_ReadableSize( dlSizeBuf,		sizeof dlSizeBuf,		downloadCount );
		CG_ReadableSize( totalSizeBuf,	sizeof totalSizeBuf,	downloadSize );
    
		if (downloadCount < 4096 || !downloadTime) {
			Text_PaintCenter(leftWidth, yStart+216, scale, colorWhite, "estimating", 0, iMenuFont);
			Text_PaintCenter(leftWidth, yStart+160, scale, colorWhite, va("(%s %s %s %s)", dlSizeBuf, sOf, totalSizeBuf, sCopied), 0, iMenuFont);
		} else {
			if ((downloadTime) / 1000) {
				xferRate = downloadCount / ((downloadTime) / 1000);
			} else {
				xferRate = 0;
			}
			CG_ReadableSize( xferRateBuf, sizeof xferRateBuf, xferRate );
    
			// Extrapolate estimated completion time
			if (downloadSize && xferRate) {
				int n = downloadSize / xferRate; // estimated time for entire d/l in secs

				// We do it in K (/1024) because we'd overflow around 4MB
				UI_PrintTime ( dlTimeBuf, sizeof dlTimeBuf, 
					(n - (((downloadCount/1024) * n) / (downloadSize/1024))) * 1000);
    
				Text_PaintCenter(leftWidth, yStart+216, scale, colorWhite, dlTimeBuf, 0, iMenuFont);
				Text_PaintCenter(leftWidth, yStart+160, scale, colorWhite, va("(%s %s %s %s)", dlSizeBuf, sOf, totalSizeBuf, sCopied), 0, iMenuFont);
			} else {
				Text_PaintCenter(leftWidth, yStart+216, scale, colorWhite, "estimating", 0, iMenuFont);
				if (downloadSize) {
					Text_PaintCenter(leftWidth, yStart+160, scale, colorWhite, va("(%s %s %s %s)", dlSizeBuf, sOf, totalSizeBuf, sCopied), 0, iMenuFont);
				} else {
					Text_PaintCenter(leftWidth, yStart+160, scale, colorWhite, va("(%s %s)", dlSizeBuf, sCopied), 0, iMenuFont);
				}
			}
    
			if (xferRate) {
				Text_PaintCenter(leftWidth, yStart+272, scale, colorWhite, va("%s/%s", xferRateBuf,sSec), 0, iMenuFont);
			}
		}
		return;
	}
#endif
	
	info = CG_ConfigString( CS_SERVERINFO );
	sysInfo = CG_ConfigString( CS_SYSTEMINFO );

	s = Info_ValueForKey( info, "mapname" );
	levelshot = trap_R_RegisterShaderNoMip( va( "levelshots/%s", s ) );
	if ( !levelshot ) {
		levelshot = trap_R_RegisterShaderNoMip( "menu/art/unknownmap_mp" );
	}
	trap_R_SetColor( NULL );
	CG_DrawPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, levelshot );

	CG_LoadBar();

	// draw the icons of things as they are loaded
//	CG_DrawLoadingIcons();

	// the first 150 rows are reserved for the client connection
	// screen to write into
	if ( cg.infoScreenText[0] ) {
		const char *psLoading = CG_GetStringEdString("MENUS", "LOADING_MAPNAME");
		UI_DrawProportionalString( 320, 128-32, va(/*"Loading... %s"*/ psLoading, cg.infoScreenText),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );		
	} else {
		const char *psAwaitingSnapshot = CG_GetStringEdString("MENUS", "AWAITING_SNAPSHOT");
		UI_DrawProportionalString( 320, 128-32, /*"Awaiting snapshot..."*/psAwaitingSnapshot,
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );			
	}

	// draw info string information

	y = 180-32;

	// don't print server lines if playing a local game
	trap_Cvar_VariableStringBuffer( "sv_running", buf, sizeof( buf ) );
	if ( !atoi( buf ) ) {
		// server hostname
		Q_strncpyz(buf, Info_ValueForKey( info, "sv_hostname" ), 1024);
		Q_CleanStr(buf);
		UI_DrawProportionalString( 320, y, buf,
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;

		// pure server
		s = Info_ValueForKey( sysInfo, "sv_pure" );
		if ( s[0] == '1' ) {
			const char *psPure = CG_GetStringEdString("MP_INGAME", "PURE_SERVER");
			UI_DrawProportionalString( 320, y, psPure,
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}

		// server-specific message of the day
		s = CG_ConfigString( CS_MOTD );
		if ( s[0] ) {
			UI_DrawProportionalString( 320, y, s,
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}

		{	// display global MOTD at bottom (mirrors ui_main UI_DrawConnectScreen
			char motdString[1024];
			trap_Cvar_VariableStringBuffer( "cl_motdString", motdString, sizeof( motdString ) );

			if (motdString[0])
			{
				UI_DrawProportionalString( 320, 425, motdString,
					UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			}
		}

		// some extra space after hostname and motd
		y += 10;
	}

	// map-specific message (long map name)
	s = CG_ConfigString( CS_MESSAGE );
	if ( s[0] ) {
		UI_DrawProportionalString( 320, y, s,
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
	}

	// cheats warning
	s = Info_ValueForKey( sysInfo, "sv_cheats" );
	if ( s[0] == '1' ) {
		UI_DrawProportionalString( 320, y, CG_GetStringEdString("MP_INGAME", "CHEATSAREENABLED"),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
	}

	// game type
	switch ( cgs.gametype ) {
	case GT_FFA:
			s = CG_GetStringEdString("MENUS", "FREE_FOR_ALL");//"Free For All";
//		s = "Free For All";
		break;
	case GT_HOLOCRON:
			s = CG_GetStringEdString("MENUS", "HOLOCRON_FFA");//"Holocron FFA";
//		s = "Holocron FFA";
		break;
	case GT_JEDIMASTER:
			s = CG_GetStringEdString("MENUS", "SAGA");//"Jedi Master";??

//		s = "Jedi Master";
		break;
	case GT_SINGLE_PLAYER:
			s = CG_GetStringEdString("MENUS", "SAGA");//"Team FFA";

		//s = "Single Player";
		break;
	case GT_DUEL:
			s = CG_GetStringEdString("MENUS", "DUEL");//"Team FFA";
		//s = "Duel";
		break;
	case GT_POWERDUEL:
			s = CG_GetStringEdString("MENUS", "POWERDUEL");//"Team FFA";
		//s = "Power Duel";
		break;
	case GT_TEAM:
			s = CG_GetStringEdString("MENUS", "TEAM_FFA");//"Team FFA";

		//s = "Team FFA";
		break;
	case GT_SIEGE:
			s = CG_GetStringEdString("MENUS", "SIEGE");//"Siege";

		//s = "Siege";
		break;
	case GT_CTF:
			s = CG_GetStringEdString("MENUS", "CAPTURE_THE_FLAG");//"Capture the Flag";

		//s = "Capture The Flag";
		break;
	case GT_CTY:
			s = CG_GetStringEdString("MENUS", "CAPTURE_THE_YSALIMARI");//"Capture the Ysalamiri";

		//s = "Capture The Ysalamiri";
		break;
	default:
			s = CG_GetStringEdString("MENUS", "SAGA");//"Team FFA";

		//s = "Unknown Gametype";
		break;
	}
	UI_DrawProportionalString( 320, y, s,
		UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
	y += iPropHeight;
		
	if (cgs.gametype != GT_SIEGE)
	{
		value = atoi( Info_ValueForKey( info, "timelimit" ) );
		if ( value ) {
			UI_DrawProportionalString( 320, y, va( "%s %i", CG_GetStringEdString("MP_INGAME", "TIMELIMIT"), value ),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}

		if (cgs.gametype < GT_CTF ) {
			value = atoi( Info_ValueForKey( info, "fraglimit" ) );
			if ( value ) {
				UI_DrawProportionalString( 320, y, va( "%s %i", CG_GetStringEdString("MP_INGAME", "FRAGLIMIT"), value ),
					UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
				y += iPropHeight;
			}

			if (cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL)
			{
				value = atoi( Info_ValueForKey( info, "duel_fraglimit" ) );
				if ( value ) {
					UI_DrawProportionalString( 320, y, va( "%s %i", CG_GetStringEdString("MP_INGAME", "WINLIMIT"), value ),
						UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
					y += iPropHeight;
				}
			}
		}
	}

	if (cgs.gametype >= GT_CTF) {
		value = atoi( Info_ValueForKey( info, "capturelimit" ) );
		if ( value ) {
			UI_DrawProportionalString( 320, y, va( "%s %i", CG_GetStringEdString("MP_INGAME", "CAPTURELIMIT"), value ),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}
	}

	if (cgs.gametype >= GT_TEAM)
	{
		value = atoi( Info_ValueForKey( info, "g_forceBasedTeams" ) );
		if ( value ) {
			UI_DrawProportionalString( 320, y, CG_GetStringEdString("MP_INGAME", "FORCEBASEDTEAMS"),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}
	}

    if (cgs.gametype != GT_SIEGE)
	{
		valueNOFP = atoi( Info_ValueForKey( info, "g_forcePowerDisable" ) );

		value = atoi( Info_ValueForKey( info, "g_maxForceRank" ) );
		if ( value && !valueNOFP && (value < NUM_FORCE_MASTERY_LEVELS) ) {
			char fmStr[1024]; 

			trap_SP_GetStringTextString("MP_INGAME_MAXFORCERANK",fmStr, sizeof(fmStr));

			UI_DrawProportionalString( 320, y, va( "%s %s", fmStr, CG_GetStringEdString("MP_INGAME", forceMasteryLevels[value]) ),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}
		else if (!valueNOFP)
		{
			char fmStr[1024];
			trap_SP_GetStringTextString("MP_INGAME_MAXFORCERANK",fmStr, sizeof(fmStr));

			UI_DrawProportionalString( 320, y, va( "%s %s", fmStr, (char *)CG_GetStringEdString("MP_INGAME", forceMasteryLevels[7]) ),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}

		if (cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL)
		{
			value = atoi( Info_ValueForKey( info, "g_duelWeaponDisable" ) );
		}
		else
		{
			value = atoi( Info_ValueForKey( info, "g_weaponDisable" ) );
		}
		if ( cgs.gametype != GT_JEDIMASTER && value ) {
			UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "SABERONLYSET") ),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}

		if ( valueNOFP ) {
			UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "NOFPSET") ),
				UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
			y += iPropHeight;
		}
	}

	// Display the rules based on type
		y += iPropHeight;
	switch ( cgs.gametype ) {
	case GT_FFA:					
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_FFA_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_HOLOCRON:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_HOLO_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_HOLO_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_JEDIMASTER:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_JEDI_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_JEDI_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_SINGLE_PLAYER:
		break;
	case GT_DUEL:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_DUEL_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_DUEL_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_POWERDUEL:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_POWERDUEL_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_POWERDUEL_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_TEAM:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_TEAM_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_TEAM_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_SIEGE:
		break;
	case GT_CTF:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_CTF_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_CTF_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	case GT_CTY:
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_CTY_1")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		UI_DrawProportionalString( 320, y, va( "%s", (char *)CG_GetStringEdString("MP_INGAME", "RULES_CTY_2")),
			UI_CENTER|UI_INFOFONT|UI_DROPSHADOW, colorWhite );
		y += iPropHeight;
		break;
	default:
		break;
	}
}

/*
===================
CG_LoadBar
===================
*/
void CG_LoadBar(void)
{
	const int numticks = 9, tickwidth = 40, tickheight = 8;
	const int tickpadx = 20, tickpady = 12;
	const int capwidth = 8;
	const int barwidth = numticks*tickwidth+tickpadx*2+capwidth*2, barleft = ((640-barwidth)/2);
	const int barheight = tickheight + tickpady*2, bartop = 480-barheight;
	const int capleft = barleft+tickpadx, tickleft = capleft+capwidth, ticktop = bartop+tickpady;

	trap_R_SetColor( colorWhite );
	// Draw background
	CG_DrawPic(barleft, bartop, barwidth, barheight, cgs.media.loadBarLEDSurround);

	// Draw left cap (backwards)
	CG_DrawPic(tickleft, ticktop, -capwidth, tickheight, cgs.media.loadBarLEDCap);

	// Draw bar
	CG_DrawPic(tickleft, ticktop, tickwidth*cg.loadLCARSStage, tickheight, cgs.media.loadBarLED);

	// Draw right cap
	CG_DrawPic(tickleft+tickwidth*cg.loadLCARSStage, ticktop, capwidth, tickheight, cgs.media.loadBarLEDCap);
}

